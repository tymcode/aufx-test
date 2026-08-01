/**
 * Hardware Audio Setup dialog: pick the loop interface, its send/return/
 * monitor stereo pairs, an optional separate monitor output device (for
 * Multi-Output-Device screen-recording setups), buffer size, and loop
 * latency (manual or impulse-based auto-detect with live VU meters).
 *
 * Runs as a modal AlertWindow with a custom panel. While it is open the
 * software effect is muted (see showHardwareAudioSetupDialog) so the user
 * hears only the raw hardware loop they are configuring.
 */
#include "HardwareAudioSetupDialog.h"
#include "HardwareVuMeters.h"
#include "HostPreferences.h"
#include "Utf8.h"

namespace
{
    // Send/return combos: ID 1 = "None Selected", pair items start at 2.
    // Monitor combo has no None option — pairs start at ID 1.
    constexpr int kNoneSelectedId = 1;
    constexpr int kSendReturnFirstPairId = 2;

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

    void selectPair (juce::ComboBox& box, int leftChannel, int firstPairId)
    {
        if (box.getNumItems() == 0)
            return;

        const int id = (leftChannel / 2) + firstPairId;
        if (box.indexOfItemId (id) >= 0)
            box.setSelectedId (id, juce::dontSendNotification);
        else
            box.setSelectedItemIndex (firstPairId == kSendReturnFirstPairId && box.getNumItems() > 1 ? 1 : 0,
                                     juce::dontSendNotification);
    }

    int selectedLeftChannel (const juce::ComboBox& box, int firstPairId)
    {
        const int id = box.getSelectedId();
        if (id < firstPairId)
            return 0;
        return (id - firstPairId) * 2;
    }

    bool isNoneSelected (const juce::ComboBox& box)
    {
        return box.getSelectedId() == kNoneSelectedId;
    }

    /**
     * Probe a device by name for its full I/O channel lists (not just active
     * masks). Creating a throwaway juce::AudioIODevice is the only reliable
     * way to see every channel: querying the *open* device reports only the
     * channels currently enabled (an Apollo with just outs 1-2 active would
     * hide ins 5-6 from the pair dropdowns).
     */
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
            noteLabel.setText ("Auto-detect plays fixtures/impulse.wav five times and averages the latency. Set the hardware box to a dry/bypass program first.",
                               juce::dontSendNotification);
            noteLabel.setFont (juce::FontOptions (12.0f));
            noteLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
            testHintLabel.setText ("Click Test to send the selected Source Clip through the hardware loop.",
                                   juce::dontSendNotification);
            testHintLabel.setFont (juce::FontOptions (12.0f));
            testHintLabel.setColour (juce::Label::textColourId, juce::Colours::grey);

            for (auto* l : { &deviceLabel, &sendLabel, &returnLabel, &monitorOutputLabel, &monitorLabel,
                             &bufferLabel, &latencyLabel, &noteLabel, &statusLabel, &testHintLabel })
                addAndMakeVisible (l);

            for (auto* c : { &deviceBox, &sendBox, &returnBox, &monitorOutputBox, &monitorBox, &bufferBox })
            {
                addAndMakeVisible (c);
                c->addListener (this);
            }

            addAndMakeVisible (latencyEditor);
            latencyEditor.setInputRestrictions (8, "0123456789");
            latencyEditor.setText ("0");

            autoDetectButton.setButtonText ("Auto-detect");
            autoDetectButton.addListener (this);
            addAndMakeVisible (autoDetectButton);
            addAndMakeVisible (vuPanel);

            statusLabel.setText ({}, juce::dontSendNotification);

            populateDevices();
            populateMonitorOutputDevices();
            applySettingsToControls (engine.getHardwareLoopSettings());
            updateMonitorPairEnabled();
            updateLoopControlsEnabled();
            setSize (560, 590);
        }

        /** Called when send/return "None Selected" enables or disables Test. */
        std::function<void (bool loopConfigured)> onLoopConfiguredChanged;

        HardwareLoopSettings getSettings() const
        {
            if (! isLoopConfigured())
                return HardwareLoopSettings::unconfigured();

            HardwareLoopSettings s;
            s.deviceName = deviceBox.getText();
            s.sendChannelL = selectedLeftChannel (sendBox, kSendReturnFirstPairId);
            s.sendChannelR = s.sendChannelL + 1;
            s.returnChannelL = selectedLeftChannel (returnBox, kSendReturnFirstPairId);
            s.returnChannelR = s.returnChannelL + 1;
            s.monitorChannelL = selectedLeftChannel (monitorBox, 1);
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

        bool isLoopConfigured() const
        {
            return ! isNoneSelected (sendBox) && ! isNoneSelected (returnBox)
                   && sendBox.getSelectedId() >= kSendReturnFirstPairId
                   && returnBox.getSelectedId() >= kSendReturnFirstPairId;
        }

        bool isTesting() const { return testing; }

        /** Toggle hardware-loop Test playback. Returns the new testing state. */
        bool toggleTest()
        {
            if (! testing && ! isLoopConfigured())
                return false;

            testing = ! testing;

            if (testing)
            {
                auto settings = getSettings();
                engine.setHardwareLoopSettings (settings);
                juce::String error;
                engine.stopAudioDevice();
                if (! engine.startAudioDevice (error))
                {
                    testing = false;
                    statusLabel.setText ("Could not open device: " + error, juce::dontSendNotification);
                    return false;
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

            return testing;
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

            {
                auto r = row (28);
                latencyLabel.setBounds (r.removeFromLeft (180));
                autoDetectButton.setBounds (r.removeFromRight (120));
                r.removeFromRight (8);
                latencyEditor.setBounds (r);
            }

            noteLabel.setBounds (row (36));
            statusLabel.setBounds (row (36));
            testHintLabel.setBounds (row (28));
            vuPanel.setBounds (area);
        }

    private:
        // Monitor output dropdown: fixed IDs 1 ("use the loop interface's
        // monitor pair") and 2 ("follow the system default output") come
        // first; actual device names start at ID 3. getSettings() and
        // selectMonitorOutput() both depend on this ID scheme.
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

            // Saved device is not currently present (e.g. the Multi-Output
            // Device only exists on the user's studio machine). Keep it
            // selectable rather than silently dropping the preference.
            const int itemId = monitorOutputBox.getNumItems() + 3;
            monitorOutputBox.addItem (s.monitorOutputDeviceName, itemId);
            monitorOutputBox.setSelectedId (itemId, juce::dontSendNotification);
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
                // Include both input and output device names so duplex interfaces
                // that only advertise on one side still appear in the picker.
                for (const bool wantInput : { false, true })
                    for (const auto& name : type->getDeviceNames (wantInput))
                        if (! names.contains (name))
                            names.add (name);
            }

            names.sort (true);
            for (int i = 0; i < names.size(); ++i)
                deviceBox.addItem (names[i], i + 1);
        }

        void updateLoopControlsEnabled()
        {
            const bool loopOn = isLoopConfigured();
            bufferLabel.setEnabled (loopOn);
            bufferBox.setEnabled (loopOn);
            latencyLabel.setEnabled (loopOn);
            latencyEditor.setEnabled (loopOn);
            autoDetectButton.setEnabled (loopOn);
            noteLabel.setEnabled (loopOn);
            testHintLabel.setEnabled (loopOn);

            if (! loopOn && testing)
            {
                testing = false;
                engine.stopFixture();
                statusLabel.setText ("Test stopped", juce::dontSendNotification);
            }

            if (onLoopConfiguredChanged != nullptr)
                onLoopConfiguredChanged (loopOn);
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

            auto fillSendReturn = [] (juce::ComboBox& box, int channels, const juce::StringArray& names)
            {
                const bool wasNone = isNoneSelected (box) || box.getNumItems() == 0;
                const int previousLeft = wasNone ? 0
                                                 : selectedLeftChannel (box, kSendReturnFirstPairId);
                box.clear (juce::dontSendNotification);
                box.addItem ("None Selected", kNoneSelectedId);
                const auto labels = stereoPairLabels (channels, names);
                for (int i = 0; i < labels.size(); ++i)
                    box.addItem (labels[i], i + kSendReturnFirstPairId);
                if (wasNone)
                    box.setSelectedId (kNoneSelectedId, juce::dontSendNotification);
                else
                    selectPair (box, previousLeft, kSendReturnFirstPairId);
            };

            auto fillMonitor = [] (juce::ComboBox& box, int channels, const juce::StringArray& names)
            {
                const int previous = selectedLeftChannel (box, 1);
                box.clear (juce::dontSendNotification);
                const auto labels = stereoPairLabels (channels, names);
                for (int i = 0; i < labels.size(); ++i)
                    box.addItem (labels[i], i + 1);
                selectPair (box, previous, 1);
            };

            fillSendReturn (sendBox, numOut, outputNames);
            fillSendReturn (returnBox, numIn, inputNames);
            fillMonitor (monitorBox, numOut, outputNames);

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

            updateLoopControlsEnabled();
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

            if (! s.isConfigured())
            {
                sendBox.setSelectedId (kNoneSelectedId, juce::dontSendNotification);
                returnBox.setSelectedId (kNoneSelectedId, juce::dontSendNotification);
            }
            else
            {
                selectPair (sendBox, s.sendChannelL, kSendReturnFirstPairId);
                selectPair (returnBox, s.returnChannelL, kSendReturnFirstPairId);
            }
            selectPair (monitorBox, s.monitorChannelL, 1);

            for (int i = 0; i < bufferBox.getNumItems(); ++i)
                if (bufferBox.getItemText (i).getIntValue() == s.bufferSize)
                {
                    bufferBox.setSelectedItemIndex (i, juce::dontSendNotification);
                    break;
                }

            latencyEditor.setText (juce::String (s.latencySamples), juce::dontSendNotification);
            updateLoopControlsEnabled();
        }

        void comboBoxChanged (juce::ComboBox* box) override
        {
            if (box == &deviceBox)
            {
                refreshChannelCombos();
            }
            else if (box == &monitorOutputBox)
            {
                updateMonitorPairEnabled();
            }
            else if (box == &sendBox || box == &returnBox)
            {
                // Selecting None on either send or return clears both.
                if (isNoneSelected (*box))
                {
                    sendBox.setSelectedId (kNoneSelectedId, juce::dontSendNotification);
                    returnBox.setSelectedId (kNoneSelectedId, juce::dontSendNotification);
                }
                updateLoopControlsEnabled();
            }
        }

        void buttonClicked (juce::Button* button) override
        {
            if (button == &autoDetectButton)
            {
                if (! isLoopConfigured())
                    return;

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

                statusLabel.setText (utf8 ("Detecting latency…"), juce::dontSendNotification);
                int latency = 0;
                float gainDb = 0.0f;
                if (! engine.autoDetectLatency (
                        impulse, latency, gainDb, error,
                        [this] (int current, int total)
                        {
                            statusLabel.setText ("Detecting latency ("
                                                    + juce::String (current) + "/"
                                                    + juce::String (total) + utf8 (")…"),
                                                juce::dontSendNotification);
                            statusLabel.repaint();
                        }))
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

        PluginAudioEngine& engine;
        juce::File fixturesDir;
        juce::Label deviceLabel, sendLabel, returnLabel, monitorOutputLabel, monitorLabel, bufferLabel, latencyLabel;
        juce::Label noteLabel, statusLabel, testHintLabel;
        juce::ComboBox deviceBox, sendBox, returnBox, monitorOutputBox, monitorBox, bufferBox;
        juce::TextEditor latencyEditor;
        juce::TextButton autoDetectButton;
        HardwareLoopMeterPanel vuPanel { engine };
        bool testing { false };
    };
}

bool showHardwareAudioSetupDialog (PluginAudioEngine& engine,
                                   const juce::File& fixturesDir,
                                   juce::Component* centreAround)
{
    // Mute the software plugin while the dialog is open: the Test button
    // plays the fixture through the hardware loop, and hearing the plugin's
    // processed version simultaneously made level/latency judgement
    // impossible. RAII so every exit (Save, Cancel, error) unmutes.
    engine.setSoftwareEffectMuted (true);
    struct UnmuteOnExit
    {
        PluginAudioEngine& e;
        ~UnmuteOnExit() { e.setSoftwareEffectMuted (false); }
    } unmute { engine };

    // Test / Auto-detect apply UI settings to the engine live. Snapshot so
    // Cancel can restore the previous configuration instead of leaving a
    // half-edited device setup active for the rest of the session.
    const auto settingsBeforeDialog = engine.getHardwareLoopSettings();

    HardwareAudioSetupPanel panel (engine, fixturesDir);

    // Test lives on the AlertWindow button row with Save/Cancel. Clicking it
    // exits the modal loop, so re-enter until Save or Cancel.
    juce::AlertWindow window ("Hardware Audio Setup",
                              "Select the interface and stereo pairs to test external hardware.\n",
                              juce::MessageBoxIconType::NoIcon,
                              centreAround);
    window.addCustomComponent (&panel);
    window.addButton ("Test", 2);
    window.addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window.addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    panel.onLoopConfiguredChanged = [&window, &panel] (bool loopConfigured)
    {
        if (auto* testBtn = window.getButton (0))
        {
            testBtn->setEnabled (loopConfigured);
            testBtn->setButtonText ((! loopConfigured || ! panel.isTesting()) ? "Test" : "Stop Test");
        }
    };
    panel.onLoopConfiguredChanged (panel.isLoopConfigured());

    int result = 0;
    for (;;)
    {
        result = window.runModalLoop();
        if (result != 2)
            break;

        panel.toggleTest();
        if (auto* testBtn = window.getButton (0))
            testBtn->setButtonText (panel.isTesting() ? "Stop Test" : "Test");
    }

    // Cancel (and any other non-Save exit) must stop a running Test — otherwise
    // the fixture keeps playing through the send pair until the next Save
    // restarts the audio device.
    engine.stopFixture();

    if (result != 1)
    {
        engine.setHardwareLoopSettings (settingsBeforeDialog);
        juce::String error;
        engine.stopAudioDevice();
        engine.startAudioDevice (error); // best-effort restore; ignore failure on cancel
        return false;
    }

    const auto settings = panel.getSettings();
    HostPreferences::get().setHardwareLoopSettings (settings);
    engine.setHardwareLoopSettings (settings);

    if (! settings.isConfigured() && engine.isHardwareMode())
        engine.setHardwareMode (false);

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
