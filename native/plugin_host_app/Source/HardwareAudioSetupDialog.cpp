#include "HardwareAudioSetupDialog.h"
#include "HardwareVuMeters.h"
#include "HostPreferences.h"
#include "Utf8.h"

namespace
{
    juce::StringArray stereoPairLabels (int numChannels, const juce::StringArray& channelNames)
    {
        juce::StringArray labels;
        for (int i = 0; i + 1 < numChannels; i += 2)
        {
            auto label = HardwareLoopSettings::channelPairLabel (i, i + 1);
            if (i < channelNames.size() && i + 1 < channelNames.size())
            {
                const auto left = channelNames[i].trim();
                const auto right = channelNames[i + 1].trim();
                if (left.isNotEmpty() && right.isNotEmpty()
                    && left != juce::String (i + 1) && right != juce::String (i + 2))
                    label << "  (" << left << " / " << right << ")";
            }
            labels.add (label);
        }
        if (labels.isEmpty() && numChannels >= 1)
            labels.add ("1");
        return labels;
    }

    void selectPair (juce::ComboBox& box, int leftChannel)
    {
        const int id = (leftChannel / 2) + 1;
        if (box.getNumItems() > 0)
            box.setSelectedId (juce::jlimit (1, box.getNumItems(), id), juce::dontSendNotification);
    }

    int selectedLeftChannel (const juce::ComboBox& box)
    {
        const int index = juce::jmax (0, box.getSelectedItemIndex());
        return index * 2;
    }

    /** Probe a device by name for its full I/O channel lists (not just active masks). */
    bool probeDeviceChannels (juce::AudioDeviceManager& dm,
                              const juce::String& deviceName,
                              juce::StringArray& inputNames,
                              juce::StringArray& outputNames)
    {
        inputNames.clear();
        outputNames.clear();

        if (deviceName.isEmpty())
            return false;

        if (auto* current = dm.getCurrentAudioDevice())
        {
            if (current->getName() == deviceName)
            {
                inputNames = current->getInputChannelNames();
                outputNames = current->getOutputChannelNames();
                if (inputNames.size() >= 2 || outputNames.size() >= 2)
                    return true;
            }
        }

        for (auto* type : dm.getAvailableDeviceTypes())
        {
            if (type == nullptr)
                continue;

            type->scanForDevices();
            std::unique_ptr<juce::AudioIODevice> probe (type->createDevice (deviceName, deviceName));
            if (probe == nullptr)
                continue;

            inputNames = probe->getInputChannelNames();
            outputNames = probe->getOutputChannelNames();
            return inputNames.size() > 0 || outputNames.size() > 0;
        }

        return false;
    }

    class HardwareAudioSetupPanel : public juce::Component,
                                    private juce::Timer,
                                    private juce::ComboBox::Listener,
                                    private juce::Button::Listener
    {
    public:
        HardwareAudioSetupPanel (PluginAudioEngine& engineIn, juce::File fixturesDirIn)
            : engine (engineIn), fixturesDir (std::move (fixturesDirIn))
        {
            deviceLabel.setText ("Audio Interface", juce::dontSendNotification);
            sendLabel.setText ("Send pair (to hardware)", juce::dontSendNotification);
            returnLabel.setText ("Return pair (from hardware)", juce::dontSendNotification);
            monitorLabel.setText ("Monitor pair (interface)", juce::dontSendNotification);
            monitorOutputLabel.setText ("Monitor output", juce::dontSendNotification);
            bufferLabel.setText ("Buffer size", juce::dontSendNotification);
            latencyLabel.setText ("Latency (samples)", juce::dontSendNotification);
            noteLabel.setText ("Auto-detect plays fixtures/impulse.wav. Set the hardware box to a dry/bypass program first.",
                               juce::dontSendNotification);
            noteLabel.setFont (juce::FontOptions (12.0f));
            noteLabel.setColour (juce::Label::textColourId, juce::Colours::grey);

            for (auto* l : { &deviceLabel, &sendLabel, &returnLabel, &monitorOutputLabel, &monitorLabel,
                             &bufferLabel, &latencyLabel, &noteLabel, &statusLabel })
                addAndMakeVisible (l);

            for (auto* c : { &deviceBox, &sendBox, &returnBox, &monitorOutputBox, &monitorBox, &bufferBox })
            {
                addAndMakeVisible (c);
                c->addListener (this);
            }

            addAndMakeVisible (latencyEditor);
            latencyEditor.setInputRestrictions (8, "0123456789");
            latencyEditor.setText ("0");

            testButton.setButtonText ("Test");
            autoDetectButton.setButtonText ("Auto-detect");
            testButton.addListener (this);
            autoDetectButton.addListener (this);
            addAndMakeVisible (testButton);
            addAndMakeVisible (autoDetectButton);
            addAndMakeVisible (vuPanel);

            statusLabel.setText ({}, juce::dontSendNotification);

            populateDevices();
            populateMonitorOutputDevices();
            applySettingsToControls (engine.getHardwareLoopSettings());
            updateMonitorPairEnabled();
            setSize (560, 590);
            startTimerHz (12);
        }

        HardwareLoopSettings getSettings() const
        {
            HardwareLoopSettings s;
            s.deviceName = deviceBox.getText();
            s.sendChannelL = selectedLeftChannel (sendBox);
            s.sendChannelR = s.sendChannelL + 1;
            s.returnChannelL = selectedLeftChannel (returnBox);
            s.returnChannelR = s.returnChannelL + 1;
            s.monitorChannelL = selectedLeftChannel (monitorBox);
            s.monitorChannelR = s.monitorChannelL + 1;
            if (monitorOutputBox.getSelectedId() == 1)
                s.monitorOutputDeviceName.clear();
            else if (monitorOutputBox.getSelectedId() == 2)
                s.monitorOutputDeviceName = HardwareLoopSettings::systemDefaultMonitorOutputName;
            else
                s.monitorOutputDeviceName = monitorOutputBox.getText();
            s.bufferSize = bufferBox.getText().getIntValue();
            if (s.bufferSize <= 0)
                s.bufferSize = 512;
            s.latencySamples = juce::jmax (0, latencyEditor.getText().getIntValue());
            return s;
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
                label.setBounds (r.removeFromLeft (180));
                control.setBounds (r);
            };

            place (row (28), deviceLabel, deviceBox);
            place (row (28), sendLabel, sendBox);
            place (row (28), returnLabel, returnBox);
            place (row (28), monitorOutputLabel, monitorOutputBox);
            place (row (28), monitorLabel, monitorBox);
            place (row (28), bufferLabel, bufferBox);
            place (row (28), latencyLabel, latencyEditor);

            auto buttons = row (28);
            testButton.setBounds (buttons.removeFromLeft (90));
            buttons.removeFromLeft (8);
            autoDetectButton.setBounds (buttons.removeFromLeft (120));

            noteLabel.setBounds (row (36));
            statusLabel.setBounds (row (36));
            vuPanel.setBounds (area);
        }

    private:
        void populateMonitorOutputDevices()
        {
            monitorOutputBox.clear (juce::dontSendNotification);
            monitorOutputBox.addItem ("Interface monitor pair", 1);
            monitorOutputBox.addItem (HardwareLoopSettings::systemDefaultMonitorOutputName, 2);

            auto& dm = engine.getDeviceManager();
            if (dm.getAvailableDeviceTypes().isEmpty())
                dm.initialise (0, 2, nullptr, true);

            juce::StringArray names;
            for (auto* type : dm.getAvailableDeviceTypes())
            {
                if (type == nullptr)
                    continue;
                type->scanForDevices();
                for (const auto& name : type->getDeviceNames (false))
                    if (! names.contains (name))
                        names.add (name);
            }

            names.sort (true);
            for (int i = 0; i < names.size(); ++i)
                monitorOutputBox.addItem (names[i], i + 3);
        }

        void updateMonitorPairEnabled()
        {
            const bool useInterfacePair = monitorOutputBox.getSelectedId() == 1;
            monitorLabel.setEnabled (useInterfacePair);
            monitorBox.setEnabled (useInterfacePair);
        }

        void selectMonitorOutput (const HardwareLoopSettings& s)
        {
            if (s.monitorOutputDeviceName.isEmpty())
            {
                monitorOutputBox.setSelectedId (1, juce::dontSendNotification);
                return;
            }

            if (s.monitorOutputDeviceName == HardwareLoopSettings::systemDefaultMonitorOutputName)
            {
                monitorOutputBox.setSelectedId (2, juce::dontSendNotification);
                return;
            }

            for (int i = 0; i < monitorOutputBox.getNumItems(); ++i)
                if (monitorOutputBox.getItemText (i) == s.monitorOutputDeviceName)
                {
                    monitorOutputBox.setSelectedId (monitorOutputBox.getItemId (i), juce::dontSendNotification);
                    return;
                }

            monitorOutputBox.addItem (s.monitorOutputDeviceName, monitorOutputBox.getNumItems() + 3);
            monitorOutputBox.setSelectedId (monitorOutputBox.getNumItems(), juce::dontSendNotification);
        }

        void populateDevices()
        {
            deviceBox.clear (juce::dontSendNotification);
            auto& dm = engine.getDeviceManager();

            // Ensure device types exist.
            if (dm.getAvailableDeviceTypes().isEmpty())
                dm.initialise (2, 2, nullptr, true);

            juce::StringArray names;
            for (auto* type : dm.getAvailableDeviceTypes())
            {
                if (type == nullptr)
                    continue;
                type->scanForDevices();
                for (const auto& name : type->getDeviceNames (false))
                    if (! names.contains (name))
                        names.add (name);
            }

            for (int i = 0; i < names.size(); ++i)
                deviceBox.addItem (names[i], i + 1);
        }

        void refreshChannelCombos()
        {
            juce::StringArray inputNames, outputNames;
            const auto deviceName = deviceBox.getText();
            probeDeviceChannels (engine.getDeviceManager(), deviceName, inputNames, outputNames);

            // Prefer full channel-name lists. Active-bit counts only reflect the
            // currently enabled pair (often just 1-2 on Apollo), which hid 5-6.
            int numIn = juce::jmax (2, inputNames.size());
            int numOut = juce::jmax (2, outputNames.size());

            if (inputNames.isEmpty() && outputNames.isEmpty())
            {
                // Last resort before a device has been probed successfully.
                numIn = 16;
                numOut = 16;
            }

            auto fill = [] (juce::ComboBox& box, int channels, const juce::StringArray& names)
            {
                const int previous = selectedLeftChannel (box);
                box.clear (juce::dontSendNotification);
                const auto labels = stereoPairLabels (channels, names);
                for (int i = 0; i < labels.size(); ++i)
                    box.addItem (labels[i], i + 1);
                selectPair (box, previous);
            };

            fill (sendBox, numOut, outputNames);
            fill (monitorBox, numOut, outputNames);
            fill (returnBox, numIn, inputNames);

            bufferBox.clear (juce::dontSendNotification);
            juce::Array<int> sizes { 32, 64, 128, 256, 512, 1024, 2048 };
            if (auto* device = engine.getDeviceManager().getCurrentAudioDevice())
            {
                if (device->getName() == deviceName)
                {
                    auto available = device->getAvailableBufferSizes();
                    if (! available.isEmpty())
                        sizes = available;
                }
            }
            for (int i = 0; i < sizes.size(); ++i)
                bufferBox.addItem (juce::String (sizes[i]), i + 1);
            if (bufferBox.getNumItems() > 0 && bufferBox.getSelectedId() == 0)
                bufferBox.setSelectedItemIndex (juce::jmin (4, bufferBox.getNumItems() - 1),
                                                juce::dontSendNotification);
        }

        void applySettingsToControls (const HardwareLoopSettings& s)
        {
            if (s.deviceName.isNotEmpty())
            {
                for (int i = 0; i < deviceBox.getNumItems(); ++i)
                    if (deviceBox.getItemText (i) == s.deviceName)
                    {
                        deviceBox.setSelectedItemIndex (i, juce::dontSendNotification);
                        break;
                    }
            }
            else if (deviceBox.getNumItems() > 0)
            {
                deviceBox.setSelectedItemIndex (0, juce::dontSendNotification);
            }

            refreshChannelCombos();
            selectMonitorOutput (s);
            updateMonitorPairEnabled();
            selectPair (sendBox, s.sendChannelL);
            selectPair (returnBox, s.returnChannelL);
            selectPair (monitorBox, s.monitorChannelL);

            for (int i = 0; i < bufferBox.getNumItems(); ++i)
                if (bufferBox.getItemText (i).getIntValue() == s.bufferSize)
                {
                    bufferBox.setSelectedItemIndex (i, juce::dontSendNotification);
                    break;
                }

            latencyEditor.setText (juce::String (s.latencySamples), juce::dontSendNotification);
        }

        void comboBoxChanged (juce::ComboBox* box) override
        {
            if (box == &deviceBox)
                refreshChannelCombos();
            else if (box == &monitorOutputBox)
                updateMonitorPairEnabled();
        }

        void buttonClicked (juce::Button* button) override
        {
            if (button == &testButton)
            {
                testing = ! testing;
                testButton.setButtonText (testing ? "Stop Test" : "Test");

                if (testing)
                {
                    auto settings = getSettings();
                    engine.setHardwareLoopSettings (settings);
                    juce::String error;
                    engine.stopAudioDevice();
                    if (! engine.startAudioDevice (error))
                    {
                        testing = false;
                        testButton.setButtonText ("Test");
                        statusLabel.setText ("Could not open device: " + error, juce::dontSendNotification);
                        return;
                    }

                    engine.playFixture();
                    statusLabel.setText (utf8 ("Sending current source clip to the send pair…"),
                                         juce::dontSendNotification);
                }
                else
                {
                    engine.stopFixture();
                    statusLabel.setText ("Test stopped", juce::dontSendNotification);
                }
                return;
            }

            if (button == &autoDetectButton)
            {
                const auto impulse = fixturesDir.getChildFile ("impulse.wav");
                if (! impulse.existsAsFile())
                {
                    statusLabel.setText ("impulse.wav not found in fixtures", juce::dontSendNotification);
                    return;
                }

                // Apply current UI settings to the engine before detecting.
                auto settings = getSettings();
                engine.setHardwareLoopSettings (settings);
                juce::String error;
                engine.stopAudioDevice();
                if (! engine.startAudioDevice (error))
                {
                    statusLabel.setText ("Could not open device: " + error, juce::dontSendNotification);
                    return;
                }

                statusLabel.setText (utf8 ("Detecting latency with impulse.wav…"), juce::dontSendNotification);
                int latency = 0;
                float gainDb = 0.0f;
                if (! engine.autoDetectLatency (impulse, latency, gainDb, error))
                {
                    statusLabel.setText ("Auto-detect failed: " + error, juce::dontSendNotification);
                    return;
                }

                latencyEditor.setText (juce::String (latency), juce::dontSendNotification);
                statusLabel.setText ("Latency " + juce::String (latency) + " samples ("
                                         + juce::String (latency * 1000.0 / juce::jmax (1.0, engine.getDeviceSampleRate()), 2)
                                         + " ms), loop gain "
                                         + formatLevelDb (gainDb),
                                     juce::dontSendNotification);
            }
        }

        void timerCallback() override
        {
            // VU panel has its own timer; keep this for future status updates.
            juce::ignoreUnused (testing);
        }

        PluginAudioEngine& engine;
        juce::File fixturesDir;
        juce::Label deviceLabel, sendLabel, returnLabel, monitorOutputLabel, monitorLabel, bufferLabel, latencyLabel;
        juce::Label noteLabel, statusLabel;
        juce::ComboBox deviceBox, sendBox, returnBox, monitorOutputBox, monitorBox, bufferBox;
        juce::TextEditor latencyEditor;
        juce::TextButton testButton, autoDetectButton;
        HardwareLoopMeterPanel vuPanel { engine };
        bool testing { false };
    };
}

bool showHardwareAudioSetupDialog (PluginAudioEngine& engine,
                                   const juce::File& fixturesDir,
                                   juce::Component* centreAround)
{
    engine.setSoftwareEffectMuted (true);
    struct UnmuteOnExit
    {
        PluginAudioEngine& e;
        ~UnmuteOnExit() { e.setSoftwareEffectMuted (false); }
    } unmute { engine };

    HardwareAudioSetupPanel panel (engine, fixturesDir);

    juce::AlertWindow window ("Hardware Audio Setup",
                              "Select the CoreAudio interface and stereo pairs for the hardware insert loop.\n"
                              "For screen recording, route Monitor output to your Multi-Output Device "
                              "(or System Default Output).",
                              juce::MessageBoxIconType::NoIcon,
                              centreAround);
    window.addCustomComponent (&panel);
    window.addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window.addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    if (window.runModalLoop() != 1)
        return false;

    const auto settings = panel.getSettings();
    HostPreferences::get().setHardwareLoopSettings (settings);
    engine.setHardwareLoopSettings (settings);

    juce::String error;
    engine.stopAudioDevice();
    if (! engine.startAudioDevice (error))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                "Hardware Audio Setup",
                                                "Saved settings but failed to open device:\n" + error);
        return true;
    }

    return true;
}
