#include "RemoteSetupDialog.h"
#include "HostLog.h"
#include "HostPreferences.h"
#include "Utf8.h"

namespace
{
    class RemoteSetupPanel : public juce::Component,
                             private juce::Button::Listener
    {
    public:
        RemoteSetupPanel (PluginAudioEngine& engineIn, const juce::File& fixturesDirIn)
            : engine (engineIn), fixturesDir (fixturesDirIn)
        {
            latencyLabel.setText ("Loop latency (samples)", juce::dontSendNotification);
            latencyLabel.setJustificationType (juce::Justification::centredRight);
            addAndMakeVisible (latencyLabel);

            latencyEditor.setInputRestrictions (8, "0123456789");
            latencyEditor.setJustification (juce::Justification::centred);
            latencyEditor.setText (juce::String (engine.getRemoteLatencySamples()),
                                   juce::dontSendNotification);
            addAndMakeVisible (latencyEditor);

            autoDetectButton.setButtonText ("Auto-Detect");
            autoDetectButton.addListener (this);
            addAndMakeVisible (autoDetectButton);

            statusLabel.setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (statusLabel);

            hintLabel.setText (utf8 ("Connect to your group in SonoBus above, then Auto-Detect measures the "
                                     "network round trip (needs the remote end patched through). Test plays the "
                                     "source clip through the remote loop."),
                               juce::dontSendNotification);
            hintLabel.setJustificationType (juce::Justification::topLeft);
            hintLabel.setFont (juce::FontOptions ((float) 12));
            addAndMakeVisible (hintLabel);

            attachEditor();

            const int editorWidth = pluginEditor != nullptr ? pluginEditor->getWidth() : 720;
            const int editorHeight = pluginEditor != nullptr ? pluginEditor->getHeight() : 420;
            setSize (juce::jlimit (640, 1000, editorWidth),
                     juce::jlimit (300, 640, editorHeight) + footerHeight);
        }

        ~RemoteSetupPanel() override
        {
            detachEditor();
        }

        int getLatencySamples() const
        {
            return latencyEditor.getText().getIntValue();
        }

        void setStatus (const juce::String& text)
        {
            statusLabel.setText (text, juce::dontSendNotification);
            statusLabel.repaint();
        }

        void resized() override
        {
            auto bounds = getLocalBounds();
            auto footer = bounds.removeFromBottom (footerHeight);

            if (pluginEditor != nullptr)
                pluginEditor->setBounds (bounds);

            footer.removeFromTop (6);
            auto row = footer.removeFromTop (26);
            latencyLabel.setBounds (row.removeFromLeft (170));
            row.removeFromLeft (6);
            latencyEditor.setBounds (row.removeFromLeft (80));
            row.removeFromLeft (8);
            autoDetectButton.setBounds (row.removeFromLeft (110));
            row.removeFromLeft (10);
            statusLabel.setBounds (row);

            footer.removeFromTop (4);
            hintLabel.setBounds (footer);
        }

    private:
        static constexpr int footerHeight = 84;

        void attachEditor()
        {
            pluginEditor = engine.getRemoteTransport().createEditor();
            if (pluginEditor != nullptr)
                addAndMakeVisible (pluginEditor);
        }

        void detachEditor()
        {
            if (pluginEditor != nullptr)
            {
                removeChildComponent (pluginEditor);
                delete pluginEditor;
                pluginEditor = nullptr;
            }
        }

        void buttonClicked (juce::Button* button) override
        {
            if (button != &autoDetectButton)
                return;

            const auto impulse = fixturesDir.getChildFile ("impulse.wav");
            if (! impulse.existsAsFile())
            {
                setStatus ("impulse.wav not found in fixtures");
                return;
            }

            setStatus (utf8 ("Detecting network loop latency…"));
            int latency = 0;
            float gainDb = 0.0f;
            juce::String error;
            if (! engine.autoDetectLatency (
                    impulse, latency, gainDb, error,
                    [this] (int current, int total)
                    {
                        setStatus ("Detecting latency (" + juce::String (current) + "/"
                                   + juce::String (total) + utf8 (")…"));
                    }))
            {
                setStatus ("Auto-detect failed: " + error);
                return;
            }

            latencyEditor.setText (juce::String (latency), juce::dontSendNotification);
            setStatus ("Latency " + juce::String (latency) + " samples ("
                       + juce::String (latency * 1000.0 / juce::jmax (1.0, engine.getDeviceSampleRate()), 1)
                       + " ms), loop gain " + juce::String (gainDb, 1) + " dB");
        }

        PluginAudioEngine& engine;
        juce::File fixturesDir;
        juce::AudioProcessorEditor* pluginEditor { nullptr };
        juce::Label latencyLabel, statusLabel, hintLabel;
        juce::TextEditor latencyEditor;
        juce::TextButton autoDetectButton;
    };
}

bool showRemoteSetupDialog (PluginAudioEngine& engine,
                            const juce::File& fixturesDir,
                            juce::Component* centreAround)
{
    auto& prefs = HostPreferences::get();

    const bool wasLoaded = engine.isRemoteTransportLoaded();
    const auto transportBefore = engine.getSelectedTransport();
    const bool externalModeBefore = engine.isHardwareMode() || engine.isRemoteMode();

    if (! wasLoaded)
    {
        juce::String error;
        if (! engine.loadRemoteTransport (prefs.getRemotePluginIdentifier(), error))
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "Remote Setup",
                "Could not load the SonoBus transport plugin:\n" + error
                    + "\n\nBuild and install SonoBus (AU) first — see the sonobus repo.");
            return false;
        }

        const auto persistedState = prefs.getRemotePluginState();
        if (! persistedState.isEmpty())
            engine.applyRemoteTransportState (persistedState);

        engine.setRemoteLatencySamples (prefs.getRemoteLatencySamples());
    }

    // Latency detect / Test must run against the remote transport while the
    // dialog is open; snapshot above so Cancel can restore.
    engine.selectTransport (HardwareLoopOps::Transport::remote);

    // Mute the software plugin while open so Test plays the dry clip into the
    // remote loop and the return is what you hear (same as Hardware Setup).
    engine.setSoftwareEffectMuted (true);
    struct UnmuteOnExit
    {
        PluginAudioEngine& e;
        ~UnmuteOnExit() { e.setSoftwareEffectMuted (false); }
    } unmute { engine };

    RemoteSetupPanel panel (engine, fixturesDir);

    juce::AlertWindow window ("Remote Transport Setup",
                              "Route audio to a remote hardware rig over the network (SonoBus).\n",
                              juce::MessageBoxIconType::NoIcon,
                              centreAround);
    window.addCustomComponent (&panel);
    window.addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window.addButton ("Disable Remote", 3);
    window.addButton ("Test", 2);
    window.addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    if (auto* cancel = window.getButton ("Cancel"))
        cancel->setWantsKeyboardFocus (false);

    int result = 0;
    bool testing = false;
    for (;;)
    {
        result = window.runModalLoop();
        if (result != 2)
            break;

        testing = ! testing;
        if (testing)
            engine.playFixture();
        else
            engine.stopFixture();

        if (auto* testBtn = window.getButton ("Test"))
            testBtn->setButtonText (testing ? "Stop Test" : "Test");
    }

    engine.stopFixture();

    if (result == 3)
    {
        // Disable: forget the transport entirely.
        engine.unloadRemoteTransport();
        engine.selectTransport (HardwareLoopOps::Transport::hardware);
        prefs.setRemoteEnabled (false);
        prefs.setRemotePluginState ({});
        return false;
    }

    if (result != 1)
    {
        // Cancel: restore whatever was active before the dialog.
        if (! wasLoaded)
            engine.unloadRemoteTransport();

        engine.selectTransport (transportBefore);
        if (externalModeBefore)
            engine.setHardwareMode (true); // re-enable external monitor on prior transport

        return false;
    }

    engine.setRemoteLatencySamples (panel.getLatencySamples());

    juce::MemoryBlock state;
    if (engine.getRemoteTransportState (state))
        prefs.setRemotePluginState (state);
    prefs.setRemoteLatencySamples (engine.getRemoteLatencySamples());
    prefs.setRemoteEnabled (true);

    HostLog::info ("Remote transport saved: " + engine.getRemoteTransportName()
                   + ", latency " + juce::String (engine.getRemoteLatencySamples()) + " samples");
    return true;
}
