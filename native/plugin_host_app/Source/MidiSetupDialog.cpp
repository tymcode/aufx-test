/**
 * MIDI Setup dialog: pick the MIDI out to the device under test, the MIDI in
 * that carries its sysex dumps back, which sysex module to use, and which
 * controller ports should be the default MIDI Sources on next launch.
 *
 * The module is an explicit dropdown rather than auto-detected because
 * CoreMIDI metadata identifies the MIDI *interface* (e.g. iConnectMIDI4+ on
 * every port), not the instrument behind it — matching by manufacturer/model
 * picked the wrong module or none at all in practice.
 *
 * Default MIDI Controllers only writes host.config.json default_midi_input;
 * live enablement stays on the main-window MIDI Sources field.
 */
#include "MidiSetupDialog.h"
#include "HostChromeControls.h"
#include "HostDialog.h"
#include "HostPreferences.h"
#include "MidiEndpointInfo.h"
#include "Utf8.h"
#include "sysex/SysexDeviceRegistry.h"

namespace
{
    class MidiSetupPanel : public juce::Component,
                           private juce::ComboBox::Listener
    {
    public:
        MidiSetupPanel (PluginAudioEngine& engineIn, HostConfig& configIn)
            : engine (engineIn), config (configIn)
        {
            outLabel.setText (utf8 ("Device MIDI Out (to hardware)"), juce::dontSendNotification);
            inLabel.setText (utf8 ("Dump MIDI In (from hardware)"), juce::dontSendNotification);
            moduleLabel.setText (utf8 ("Sysex module"), juce::dontSendNotification);
            controllersLabel.setText (utf8 ("Default MIDI Controllers"), juce::dontSendNotification);
            controllersHint.setText (utf8 ("Saved as launch defaults; does not change live MIDI Sources."),
                                     juce::dontSendNotification);
            controllersHint.setFont (juce::FontOptions (12.0f));
            controllersHint.setColour (juce::Label::textColourId, juce::Colours::grey);

            addAndMakeVisible (outLabel);
            addAndMakeVisible (inLabel);
            addAndMakeVisible (moduleLabel);
            addAndMakeVisible (controllersLabel);
            addAndMakeVisible (controllersHint);
            addAndMakeVisible (outBox);
            addAndMakeVisible (inBox);
            addAndMakeVisible (moduleBox);
            addAndMakeVisible (controllersField);

            outBox.addListener (this);
            inBox.addListener (this);

            populate();
            setSize (460, 220);
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

        juce::StringArray getDefaultControllerNames() const
        {
            juce::StringArray names;
            const auto selected = controllersField.getSelectedIdentifiers();
            for (const auto& device : controllerDevices)
                if (selected.contains (device.identifier))
                    names.add (device.name);
            return names;
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
            place (row (28), controllersLabel, controllersField);
            controllersHint.setBounds (row (22).withTrimmedLeft (200));
        }

    private:
        void populate()
        {
            outInfos = getMidiOutputEndpointInfos();
            inInfos = getMidiInputEndpointInfos();
            controllerDevices = engine.getMidiInputDevices();

            outBox.clear (juce::dontSendNotification);
            inBox.clear (juce::dontSendNotification);
            moduleBox.clear (juce::dontSendNotification);
            outBox.addItem (utf8 ("(none)"), 1);
            inBox.addItem (utf8 ("(none)"), 1);

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

            juce::StringArray selectedIds;
            for (const auto& wantedName : config.defaultMidiInputs)
            {
                for (const auto& device : controllerDevices)
                {
                    if (device.name.equalsIgnoreCase (wantedName)
                        || device.name.containsIgnoreCase (wantedName))
                        selectedIds.addIfNotAlreadyThere (device.identifier);
                }
            }
            controllersField.setDevices (controllerDevices, selectedIds);
        }

        void comboBoxChanged (juce::ComboBox*) override {}

        PluginAudioEngine& engine;
        HostConfig& config;
        juce::Array<MidiEndpointInfo> outInfos, inInfos;
        juce::Array<juce::MidiDeviceInfo> controllerDevices;
        juce::Label outLabel, inLabel, moduleLabel, controllersLabel, controllersHint;
        juce::ComboBox outBox, inBox, moduleBox;
        MidiSourceField controllersField;
    };
}

bool showMidiSetupDialog (PluginAudioEngine& engine,
                          HostConfig& config,
                          juce::Component* centreAround)
{
    MidiSetupPanel panel (engine, config);
    const auto previousDumpInId = HostPreferences::get().getMidiDumpInIdentifier();

    if (HostDialog::runCustomPanelModal (
            utf8 ("MIDI Setup"),
            utf8 ("Choose the MIDI ports for the device under test (sysex dump / restore)."),
            panel,
            centreAround) != 1)
        return false;

    const auto outId = panel.getOutIdentifier();
    const auto inId = panel.getInIdentifier();
    const auto moduleName = panel.getModuleName();
    HostPreferences::get().setMidiOutIdentifier (outId);
    HostPreferences::get().setMidiDumpInIdentifier (inId);
    HostPreferences::get().setMidiSysexModule (moduleName);

    config.defaultMidiInputs = panel.getDefaultControllerNames();
    juce::String configError;
    if (! config.saveToFile (configError))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                utf8 ("MIDI Setup"),
                                                utf8 ("Saved MIDI ports but failed to write defaults:\n") + configError);
    }

    juce::String error;
    if (! engine.setMidiOutputDevice (outId, error))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                utf8 ("MIDI Setup"),
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
