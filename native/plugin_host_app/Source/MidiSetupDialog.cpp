/**
 * MIDI Setup dialog: pick the MIDI out to the device under test, the MIDI in
 * that carries its sysex dumps back, and which sysex module to use.
 *
 * The module is an explicit dropdown rather than auto-detected because
 * CoreMIDI metadata identifies the MIDI *interface* (e.g. iConnectMIDI4+ on
 * every port), not the instrument behind it — matching by manufacturer/model
 * picked the wrong module or none at all in practice.
 */
#include "MidiSetupDialog.h"
#include "HostDialog.h"
#include "HostPreferences.h"
#include "MidiEndpointInfo.h"
#include "sysex/SysexDeviceRegistry.h"

namespace
{
    class MidiSetupPanel : public juce::Component,
                           private juce::ComboBox::Listener
    {
    public:
        explicit MidiSetupPanel (PluginAudioEngine&)
        {
            outLabel.setText ("Device MIDI Out (to hardware)", juce::dontSendNotification);
            inLabel.setText ("Dump MIDI In (from hardware)", juce::dontSendNotification);
            moduleLabel.setText ("Sysex module", juce::dontSendNotification);
            addAndMakeVisible (outLabel);
            addAndMakeVisible (inLabel);
            addAndMakeVisible (moduleLabel);
            addAndMakeVisible (outBox);
            addAndMakeVisible (inBox);
            addAndMakeVisible (moduleBox);

            outBox.addListener (this);
            inBox.addListener (this);

            populate();
            setSize (460, 160);
        }

        // Item index 0 in both port boxes is the "(none)" entry, so the
        // endpoint arrays are offset by one from the combo indices.
        juce::String getOutIdentifier() const
        {
            const int i = outBox.getSelectedItemIndex() - 1; // index 0 is "(none)"
            return juce::isPositiveAndBelow (i, outInfos.size()) ? outInfos[i].identifier : juce::String();
        }

        juce::String getInIdentifier() const
        {
            const int i = inBox.getSelectedItemIndex() - 1; // index 0 is "(none)"
            return juce::isPositiveAndBelow (i, inInfos.size()) ? inInfos[i].identifier : juce::String();
        }

        juce::String getModuleName() const
        {
            return moduleBox.getText().trim();
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (8);
            auto row = [&area] (int h)
            {
                auto r = area.removeFromTop (h);
                area.removeFromTop (6);
                return r;
            };

            auto place = [] (juce::Rectangle<int> r, juce::Component& label, juce::Component& control)
            {
                label.setBounds (r.removeFromLeft (200));
                control.setBounds (r);
            };

            place (row (28), outLabel, outBox);
            place (row (28), inLabel, inBox);
            place (row (28), moduleLabel, moduleBox);
        }

    private:
        void populate()
        {
            outInfos = getMidiOutputEndpointInfos();
            inInfos = getMidiInputEndpointInfos();

            outBox.clear (juce::dontSendNotification);
            inBox.clear (juce::dontSendNotification);
            moduleBox.clear (juce::dontSendNotification);
            outBox.addItem ("(none)", 1);
            inBox.addItem ("(none)", 1);

            for (int i = 0; i < outInfos.size(); ++i)
                outBox.addItem (outInfos[i].name, i + 2);
            for (int i = 0; i < inInfos.size(); ++i)
                inBox.addItem (inInfos[i].name, i + 2);
            for (const auto& module : SysexDeviceRegistry::get().getModules())
                moduleBox.addItem (module->getDisplayName(), moduleBox.getNumItems() + 1);

            // Restore saved selections by identifier, not by list position —
            // CoreMIDI enumeration order changes as devices come and go.
            const auto savedOut = HostPreferences::get().getMidiOutIdentifier();
            const auto savedIn = HostPreferences::get().getMidiDumpInIdentifier();
            const auto savedModule = HostPreferences::get().getMidiSysexModule();

            outBox.setSelectedItemIndex (0, juce::dontSendNotification);
            inBox.setSelectedItemIndex (0, juce::dontSendNotification);

            for (int i = 0; i < outInfos.size(); ++i)
                if (outInfos[i].identifier == savedOut)
                    outBox.setSelectedItemIndex (i + 1, juce::dontSendNotification);
            for (int i = 0; i < inInfos.size(); ++i)
                if (inInfos[i].identifier == savedIn)
                    inBox.setSelectedItemIndex (i + 1, juce::dontSendNotification);

            if (savedModule.isNotEmpty())
            {
                for (int i = 0; i < moduleBox.getNumItems(); ++i)
                {
                    if (moduleBox.getItemText (i) == savedModule)
                    {
                        moduleBox.setSelectedItemIndex (i, juce::dontSendNotification);
                        break;
                    }
                }
            }
            if (moduleBox.getSelectedId() == 0 && moduleBox.getNumItems() > 0)
                moduleBox.setSelectedItemIndex (0, juce::dontSendNotification);
        }

        void comboBoxChanged (juce::ComboBox*) override {}

        juce::Array<MidiEndpointInfo> outInfos, inInfos;
        juce::Label outLabel, inLabel, moduleLabel;
        juce::ComboBox outBox, inBox, moduleBox;
    };
}

bool showMidiSetupDialog (PluginAudioEngine& engine, juce::Component* centreAround)
{
    MidiSetupPanel panel (engine);
    const auto previousDumpInId = HostPreferences::get().getMidiDumpInIdentifier();

    if (HostDialog::runCustomPanelModal (
            "MIDI Setup",
            "Choose the MIDI ports for the device under test (sysex dump / restore).",
            panel,
            centreAround) != 1)
        return false;

    const auto outId = panel.getOutIdentifier();
    const auto inId = panel.getInIdentifier();
    const auto moduleName = panel.getModuleName();
    HostPreferences::get().setMidiOutIdentifier (outId);
    HostPreferences::get().setMidiDumpInIdentifier (inId);
    HostPreferences::get().setMidiSysexModule (moduleName);

    juce::String error;
    if (! engine.setMidiOutputDevice (outId, error))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                "MIDI Setup",
                                                error);
        return true;
    }

    // The dump-in port is *added to* the engine's active MIDI inputs rather
    // than replacing them, so a control surface selected in the main UI keeps
    // working. Swap out the previously saved dump port to avoid accumulating
    // stale entries as the user changes their mind.
    auto ids = engine.getSelectedMidiInputIdentifiers();
    if (previousDumpInId.isNotEmpty())
        ids.removeString (previousDumpInId);
    if (inId.isNotEmpty() && ! ids.contains (inId))
        ids.add (inId);
    engine.setMidiInputDevices (ids);

    return true;
}
