#include "MainContent.h"
#include "AboutDialog.h"
#include "AddPluginDialog.h"
#include "AuPluginScanner.h"
#include "HardwareAudioSetupDialog.h"
#include "HostFileUtils.h"
#include "HostLog.h"
#include "HostPreferences.h"
#include "MidiSetupDialog.h"
#include "SettingsDialog.h"
#include "AudioUnitSettingsDialog.h"
#include "InstallSourceClipsDialog.h"
#include "RestoreTestcaseStateDialog.h"
#include "SendPluginSettingsToDevice.h"
#include "Utf8.h"

#include <memory>

MainContent::MainContent (PluginAudioEngine& audioEngine,
                 HostConfig& hostConfig,
                 juce::KnownPluginList& knownList,
                 std::function<void (bool)> setLightsOutFn)
        : engine (audioEngine),
          config (hostConfig),
          knownPlugins (knownList),
          presetHardwareState (audioEngine,
                               hostConfig,
                               presetLabel,
                               presetBox,
                               loadPresetButton,
                               savePresetButton,
                               savePresetNameEditor,
                               savePresetNameLabel,
                               bypassButton,
                               [this] (const juce::String& t, bool e) { setStatus (t, e); },
                               [this] { return currentPluginIndex; },
                               [this]() -> const HostPluginEntry& { return currentPlugin(); },
                               [this] { return pluginEditor; },
                               [this] { showHardwareMeters(); },
                               [this] { showPluginEditorArea(); },
                               this),
          testCaseCapture (audioEngine,
                           hostConfig,
                           [this] (const juce::String& t, bool e) { setStatus (t, e); },
                           [this]() -> const HostPluginEntry& { return currentPlugin(); },
                           [this] { presetHardwareState.refreshHardwareModeUi(); },
                           [this] { presetHardwareState.populateHardwareStates(); },
                           std::move (setLightsOutFn)),
          testCaseLoader (audioEngine, sourceClips, presetHardwareState)
{
        setOpaque (true);
        setWantsKeyboardFocus (true);

        currentPluginIndex = 0;
        if (auto* preferred = config.configuredDefaultPlugin())
        {
            for (int i = 0; i < config.plugins.size(); ++i)
            {
                if (&config.plugins.getReference (i) == preferred)
                {
                    currentPluginIndex = i;
                    break;
                }
            }
        }
        else if (auto* fallback = config.firstInstalledPlugin())
        {
            for (int i = 0; i < config.plugins.size(); ++i)
            {
                if (&config.plugins.getReference (i) == fallback)
                {
                    currentPluginIndex = i;
                    break;
                }
            }
            if (config.defaultPluginId.isNotEmpty())
                HostLog::info ("default_plugin \"" + config.defaultPluginId
                               + "\" is not installed; falling back to "
                               + fallback->displayLabel());
        }

        pluginLabel.setText ("Plugin", juce::dontSendNotification);
        pluginLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (pluginLabel);

        pluginField.setTooltip (utf8 ("Configured plugins — use More plugins… to add more"));
        pluginField.onSelect = [this] (int index) { switchToPlugin (index); };
        pluginField.onRemove = [this] (int index) { confirmRemovePlugin (index); };
        pluginField.onMorePlugins = [this] { openAddPlugin(); };
        addAndMakeVisible (pluginField);

        setStatus (config.plugins.isEmpty() ? "Add a plugin from the Plugins menu"
                                            : "Loading plugin...");
        addAndMakeVisible (statusDisplay);

        presetLabel.setText ("Preset", juce::dontSendNotification);
        presetLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (presetLabel);
        addAndMakeVisible (presetBox);
        presetBox.addListener (this);

        fixtureLabel.setText ("Source Clip", juce::dontSendNotification);
        fixtureLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (fixtureLabel);
        addAndMakeVisible (fixtureBox);
        fixtureBox.addListener (this);

        configureButton (resetButton, "Reset");
        resetButton.setTooltip ("Reload the plugin at its default state");
        configureButton (loadPresetButton, "Load");
        configureButton (savePresetButton, "Save");

        transportButton.addListener (this);
        addAndMakeVisible (transportButton);

        bypassButton.setButtonText ("Bypass");
        bypassButton.setClickingTogglesState (true);
        bypassButton.setTooltip ("Pass the source clip through unprocessed (A/B the plugin)");
        bypassButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffc47a1e));
        bypassButton.addListener (this);
        addAndMakeVisible (bypassButton);

        loopToggle.setToggleState (true, juce::dontSendNotification);
        loopToggle.addListener (this);
        addAndMakeVisible (loopToggle);
        engine.setLooping (true);

        sendLabel.setText ("Send", juce::dontSendNotification);
        sendLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (sendLabel);
        addAndMakeVisible (sendSlider);
        sendSlider.onValueChange = [this]
        {
            engine.setSendLevelDb ((float) sendSlider.getValue());
        };
        engine.setSendLevelDb (0.0f);

        mixLabel.setText ("Mix", juce::dontSendNotification);
        mixLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (mixLabel);
        addAndMakeVisible (mixSlider);
        mixSlider.onValueChange = [this]
        {
            engine.setMixAmount ((float) mixSlider.getValue() / 100.0f);
        };
        engine.setMixAmount (1.0f);
        engine.setAllowInstrumentAudioInput (HostPreferences::get().getAllowInstrumentAudioInput());

        savePresetNameLabel.setText ("Save as", juce::dontSendNotification);
        savePresetNameLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (savePresetNameLabel);
        savePresetNameEditor.setText ("Untitled", juce::dontSendNotification);
        savePresetNameEditor.setInputRestrictions (64);
        savePresetNameEditor.setJustification (juce::Justification::centredLeft);
        addAndMakeVisible (savePresetNameEditor);

        midiLabel.setText ("MIDI Sources", juce::dontSendNotification);
        midiLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (midiLabel);
        midiField.setTooltip (utf8 ("MIDI inputs from Audio MIDI Setup — check one or more to merge"));
        midiField.onChange = [this] (const juce::StringArray& identifiers)
        {
            engine.setMidiInputDevices (identifiers);
        };
        addAndMakeVisible (midiField);
        addAndMakeVisible (midiLed);

        hostClockToggle.setButtonText ("Host Clock");
        hostClockToggle.setToggleState (false, juce::dontSendNotification);
        hostClockToggle.addListener (this);
        addAndMakeVisible (hostClockToggle);

        bpmEditor.setText ("120", juce::dontSendNotification);
        bpmEditor.setInputRestrictions (3, "0123456789");
        bpmEditor.setJustification (juce::Justification::centred);
        bpmEditor.addListener (this);
        addAndMakeVisible (bpmEditor);

        bpmLabel.setText ("BPM", juce::dontSendNotification);
        addAndMakeVisible (bpmLabel);

        clickToggle.setToggleState (false, juce::dontSendNotification);
        clickToggle.addListener (this);
        addAndMakeVisible (clickToggle);

        addAndMakeVisible (editorViewport);
        editorViewport.setViewedComponent (&editorPlaceholder, false);

        hardwareMeterPanel = std::make_unique<HardwareLoopMeterPanel> (engine);
        addChildComponent (*hardwareMeterPanel);

        sendPluginSettingsButton.onClick = [this]
        {
            juce::String message;
            const bool ok = SendPluginSettingsToDevice::send (engine, message);
            setStatus (message, ! ok);
        };
        addChildComponent (sendPluginSettingsButton);
        refreshSendPluginSettingsButton();

        populatePluginBox();
        presetHardwareState.populatePresets();
        populateFixtures();
        populateMidiInputs();

        {
            juce::String clickError;
            if (! engine.loadMetronomeClick (config.fixturesDir.getChildFile ("impulse.wav"), clickError))
                HostLog::error (clickError);
        }

        {
            const auto hw = HostPreferences::get().getHardwareLoopSettings();
            engine.setHardwareLoopSettings (hw);

            juce::String midiError;
            engine.setMidiOutputDevice (HostPreferences::get().getMidiOutIdentifier(), midiError);
            if (midiError.isNotEmpty())
                HostLog::error (midiError);

            const auto dumpIn = HostPreferences::get().getMidiDumpInIdentifier();
            if (dumpIn.isNotEmpty())
            {
                auto ids = engine.getSelectedMidiInputIdentifiers();
                if (! ids.contains (dumpIn))
                    ids.add (dumpIn);
                engine.setMidiInputDevices (ids);
            }
        }

        // Load the DSP instance only. Do NOT create the Cocoa/WebView editor here:
        // the host window is not on-screen yet, and creating then destroying the
        // editor (see showPluginEditor) makes many AUs fall back to Apple's
        // AUGenericView — which looks like the wrong plugin loaded while the
        // dropdown still shows the config default.
        loadPluginWithoutEditor();
        startTimerHz (30);
    }

MainContent::~MainContent()
{
        hardwareMeterPanel.reset();
        engine.stopFixture();
        engine.stopAudioDevice();
        destroyPluginEditor();

        if (keyListenerOwner != nullptr)
            keyListenerOwner->removeKeyListener (this);
    }

void MainContent::openAbout()
{
        showAboutDialog (this);
    }

void MainContent::openHardwareAudioSetup()
{
        if (showHardwareAudioSetupDialog (engine, config.fixturesDir, this))
            presetHardwareState.refreshHardwareModeUi();
    }

void MainContent::openMidiSetup()
{
        showMidiSetupDialog (engine, config, this);
        refreshSendPluginSettingsButton();
    }

void MainContent::refreshHardwareModeUi()
{
        presetHardwareState.refreshHardwareModeUi();
    }

void MainContent::refreshSendPluginSettingsButton()
{
        const auto deviceName = SendPluginSettingsToDevice::resolveTargetDeviceName();
        if (deviceName.isNotEmpty())
            sendPluginSettingsButton.setButtonText (utf8 ("Send Plugin Settings to ") + deviceName);
        else
            sendPluginSettingsButton.setButtonText (utf8 ("Send Plugin Settings to MIDI Device"));

        sendPluginSettingsButton.setEnabled (deviceName.isNotEmpty() && engine.getPlugin() != nullptr);
    }

void MainContent::showHardwareMeters()
{
        // Tear down the editor view only — plugin instance and its state stay loaded.
        destroyPluginEditor();
        editorViewport.setVisible (false);

        if (hardwareMeterPanel != nullptr)
        {
            hardwareMeterPanel->setVisible (true);
            hardwareMeterPanel->toFront (false);
        }

        refreshSendPluginSettingsButton();
        sendPluginSettingsButton.setVisible (true);
        sendPluginSettingsButton.toFront (false);
        resized();
    }

void MainContent::showPluginEditorArea()
{
        if (hardwareMeterPanel != nullptr)
            hardwareMeterPanel->setVisible (false);

        sendPluginSettingsButton.setVisible (false);
        editorViewport.setVisible (true);

        if (engine.getPlugin() != nullptr && pluginEditor == nullptr)
            recreatePluginEditor();
        else
            layoutEditor();
    }

void MainContent::openSettings()
{
        bool fixturesChanged = false;
        if (showSettingsDialog (config, this, &fixturesChanged))
        {
            if (fixturesChanged)
                populateFixtures();

            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::InfoIcon,
                utf8 ("Settings saved"),
                utf8 ("Source Clips, Sessions, aufx-test CLI, and Default plugin were written to host.config.json "
                      "(Default plugin applies on next launch).\n\n"
                      "Relaunch AU Effects Explorer for exploration folder / config override changes to take effect.\n\n"
                      "If you changed the exploration folder, its data (including the AU plugin cache) was moved to the new location."));
        }
    }

void MainContent::openAudioUnitSettings()
{
        if (showAudioUnitSettingsDialog (config, &knownPlugins, this))
        {
            engine.setAllowInstrumentAudioInput (
                HostPreferences::get().getAllowInstrumentAudioInput());

            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::InfoIcon,
                utf8 ("Audio Unit Settings saved"),
                utf8 ("Instrument audio-input preference applies immediately (reload the plugin if an "
                      "instrument was already open). Scan timeout applies to the next scan or retry."));
        }
    }

void MainContent::openInstallSourceClips()
{
    bool fixturesRelocated = false;
    if (showInstallSourceClipsDialog (config, engine, this, &fixturesRelocated)
        || fixturesRelocated)
    {
        populateFixtures();
    }
}

void MainContent::openAddPlugin()
{
        HostLog::info ("Add Plugin: ensuring AU cache...");
        // First use (or missing cache) triggers the AU scan; later opens just load the cache.
        ensurePluginCache (false);
        HostLog::info ("Add Plugin: opening picker (" + juce::String (knownPlugins.getTypes().size())
                       + " cached types)");

        juce::Array<int> added;
        if (! showAddPluginDialog (config, knownPlugins, this, added))
        {
            HostLog::info ("Add Plugin: picker cancelled or nothing selected");
            return;
        }

        juce::String addedNames;
        for (const int index : added)
        {
            if (juce::isPositiveAndBelow (index, config.plugins.size()))
            {
                if (addedNames.isNotEmpty())
                    addedNames += ", ";
                addedNames += config.plugins.getReference (index).displayLabel();
            }
        }

        HostLog::info ("Add Plugin: added " + juce::String (added.size()) + " plugin(s)"
                       + (addedNames.isNotEmpty() ? ": " + addedNames : juce::String()));

        populatePluginBox();
        if (! added.isEmpty())
        {
            const int index = added.getLast();
            // Defer switch so the Add Plugin modal is fully torn down before
            // heavy AU/WebView editor construction begins.
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainContent> (this), index]
                                             {
                                                 if (safe == nullptr)
                                                     return;
                                                 safe->currentPluginIndex = -1;
                                                 safe->switchToPlugin (index);
                                                 safe->setStatus ("Added plugin(s) to the list");
                                             });
            return;
        }
        setStatus ("Added plugin(s) to the list");
    }

void MainContent::rescanPlugins()
{
        HostLog::info ("Rescan Audio Units requested");
        juce::String error;
        if (! AuPluginScanner::ensureCache (config.projectRoot, knownPlugins, this, error, true))
        {
            HostLog::error ("AU rescan failed: " + error);
            setStatus ("AU rescan failed: " + error, true);
            return;
        }

        HostLog::info ("AU rescan finished (" + juce::String (knownPlugins.getTypes().size()) + " cached types)");
        setStatus ("Rescanned Audio Units (" + juce::String (knownPlugins.getTypes().size()) + " found)");
    }

void MainContent::rescanSourceClips()
{
        populateFixtures();
        setStatus ("Rescanned source clips (" + juce::String (sourceClips.getNumClips()) + " found)");
    }

void MainContent::openCaptureTestCase()
{
    testCaseCapture.prompt (this, sourceClips, fixtureBox);
}

void MainContent::openRestoreTestcaseState()
{
    juce::File startDir;
    if (config.sessionsRoot != juce::File() && ! config.plugins.isEmpty())
    {
        startDir = config.sessionsRoot
                       .getChildFile (HostConfig::slugify (currentPlugin().sessionName))
                       .getChildFile ("artifacts");
        if (! startDir.isDirectory())
            startDir.createDirectory();
    }

    if (! startDir.isDirectory())
        startDir = config.sessionsRoot;

    showRestoreTestcaseStateDialog (config, engine, testCaseLoader, sourceClips, fixtureBox,
                                    startDir, this);
}

bool MainContent::loadTestCase (const SessionSnapshotRef& snapshot, juce::String& error)
{
        juce::String status;
        if (! testCaseLoader.apply (snapshot, fixtureBox, error, &status))
            return false;

        setStatus (status, false);
        return true;
}

void MainContent::showPluginEditor()
{
        // Do not AU-scan here — scanning starts the first time Add Plugin / More plugins is used.
        if (config.plugins.isEmpty())
        {
            setStatus (utf8 ("No plugins configured — use More plugins… to add one"));
            return;
        }

        // Window is visible now (MainWindow callAsync after setVisible). Sync
        // SW/HW chrome, then create the editor once — never tear down a freshly
        // built Cocoa UI just to rebuild it (AUGenericView fallback).
        presetHardwareState.refreshHardwareModeUi();

        if (pluginEditor == nullptr && engine.getPlugin() != nullptr && ! engine.isHardwareMode())
            recreatePluginEditor();
        else
            layoutEditor();

        juce::String error;
        if (! engine.startAudioDevice (error))
        {
            setStatus ("Audio device error: " + error, true);
            return;
        }

        if (presetHardwareState.loadDefaultOrFirstPreset())
            return;

        if (sourceClips.getNumClips() > 0)
            selectFixture (fixtureBox.getSelectedId());

        setStatus ("Ready - " + currentPlugin().displayLabel());
    }

void MainContent::parentHierarchyChanged()
{
        if (keyListenerOwner != nullptr)
        {
            keyListenerOwner->removeKeyListener (this);
            keyListenerOwner = nullptr;
        }

        if (auto* top = getTopLevelComponent())
        {
            top->addKeyListener (this);
            keyListenerOwner = top;
        }
    }

void MainContent::paint(juce::Graphics& g)
{
        g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

        // Faint divider under the control strip
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (controlStripDivider);
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.fillRect (controlStripDivider.translated (0, 1));

        for (auto& sep : groupSeparators)
        {
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRect (sep);
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRect (sep.translated (1, 0));
        }
    }

void MainContent::resized()
{
        constexpr int ctrlH = 24;
        constexpr int rowH = 28;
        constexpr int gap = 6;
        constexpr int groupGap = 14;
        constexpr int leftLabelW = 72;
        constexpr int leftDropW = 240;
        constexpr int leftButtonW = 56;
        constexpr int leftColW = leftLabelW + gap + leftDropW + gap + leftButtonW;

        groupSeparators.clearQuick();

        auto bounds = getLocalBounds().reduced (12);

        auto place = [] (juce::Rectangle<int>& row, int w)
        {
            return row.removeFromLeft (w).withSizeKeepingCentre (w, ctrlH);
        };

        auto row0 = bounds.removeFromTop (rowH);
        bounds.removeFromTop (4);
        auto row1 = bounds.removeFromTop (rowH);
        bounds.removeFromTop (4);
        auto row2 = bounds.removeFromTop (rowH);

        // Right column: host clock on row1.
        clickToggle.setBounds (row1.removeFromRight (26).withSizeKeepingCentre (26, 26));
        row1.removeFromRight (gap);
        bpmLabel.setBounds (row1.removeFromRight (30).withSizeKeepingCentre (30, ctrlH));
        row1.removeFromRight (gap);
        bpmEditor.setBounds (row1.removeFromRight (40).withSizeKeepingCentre (40, ctrlH));
        row1.removeFromRight (gap);
        hostClockToggle.setBounds (row1.removeFromRight (100).withSizeKeepingCentre (100, ctrlH));
        row1.removeFromRight (groupGap);

        // Left columns — shared field width + identical action buttons
        auto left0 = row0.removeFromLeft (leftColW);
        auto left1 = row1.removeFromLeft (leftColW);
        auto left2 = row2.removeFromLeft (leftColW);

        pluginLabel.setBounds (place (left0, leftLabelW));
        left0.removeFromLeft (gap);
        pluginField.setBounds (place (left0, leftDropW));
        left0.removeFromLeft (gap);
        resetButton.setBounds (place (left0, leftButtonW));

        presetLabel.setBounds (place (left1, leftLabelW));
        left1.removeFromLeft (gap);
        presetBox.setBounds (place (left1, leftDropW));
        left1.removeFromLeft (gap);
        loadPresetButton.setBounds (place (left1, leftButtonW));

        savePresetNameLabel.setBounds (place (left2, leftLabelW));
        left2.removeFromLeft (gap);
        savePresetNameEditor.setBounds (place (left2, leftDropW));
        left2.removeFromLeft (gap);
        savePresetButton.setBounds (place (left2, leftButtonW));

        // Separator between left and middle
        groupSeparators.add (juce::Rectangle<float> ((float) (row0.getX() - groupGap / 2),
                                                     (float) row0.getY() + 3.0f,
                                                     1.0f,
                                                     (float) (row2.getBottom() - row0.getY() - 6)));

        row0.removeFromLeft (groupGap);
        row1.removeFromLeft (groupGap);
        row2.removeFromLeft (groupGap);

        constexpr int bypassButtonW = 64;
        bypassButton.setBounds (row0.removeFromRight (bypassButtonW).withSizeKeepingCentre (bypassButtonW, ctrlH));
        row0.removeFromRight (gap);

        // Status LCD spans the rest of row0 to the right edge
        statusDisplay.setBounds (row0.withSizeKeepingCentre (row0.getWidth(), ctrlH));

        // MIDI + Source Clip share label width; clip dropdown is half-width so Send can grow.
        constexpr int midLabelW = leftLabelW;
        const int mid1W = juce::jmax (180, row1.getWidth());

        {
            auto mid1 = row1.removeFromLeft (mid1W);
            midiLabel.setBounds (place (mid1, midLabelW));
            mid1.removeFromLeft (gap);
            midiLed.setBounds (mid1.removeFromRight (16).withSizeKeepingCentre (16, 16));
            mid1.removeFromRight (gap);
            midiField.setBounds (place (mid1, mid1.getWidth()));
        }

        constexpr int beginButtonW = 72;
        const int clipControlsW = 26 + gap + beginButtonW + gap;
        const int fixtureDropFull = juce::jmax (120, mid1W - midLabelW - gap - clipControlsW);
        const int fixtureDropW = juce::jmax (60, fixtureDropFull / 2);
        const int mid2W = midLabelW + gap + fixtureDropW + gap + clipControlsW;

        {
            auto mid2 = row2.removeFromLeft (mid2W);
            fixtureLabel.setBounds (place (mid2, midLabelW));
            mid2.removeFromLeft (gap);
            loopToggle.setBounds (mid2.removeFromRight (26).withSizeKeepingCentre (26, 26));
            mid2.removeFromRight (gap);
            transportButton.setBounds (mid2.removeFromRight (beginButtonW).withSizeKeepingCentre (beginButtonW, ctrlH));
            mid2.removeFromRight (gap);
            fixtureBox.setBounds (place (mid2, fixtureDropW));
        }

        row2.removeFromLeft (gap);
        sendLabel.setBounds (place (row2, 32));
        row2.removeFromLeft (gap);

        constexpr int mixLabelW = 28;
        const int mixSliderW = juce::jmax (72, row2.getWidth() / 5);
        const int mixSectionW = mixLabelW + gap + mixSliderW;
        auto mixSection = row2.removeFromRight (mixSectionW);
        mixLabel.setBounds (place (mixSection, mixLabelW));
        mixSection.removeFromLeft (gap);
        mixSlider.setBounds (mixSection.withSizeKeepingCentre (mixSection.getWidth(), ctrlH));

        sendSlider.setBounds (row2.withSizeKeepingCentre (juce::jmax (80, row2.getWidth()), ctrlH));

        bounds.removeFromTop (8);
        controlStripDivider = juce::Rectangle<int> (bounds.getX(), bounds.getY(), bounds.getWidth(), 1);
        bounds.removeFromTop (4);

        editorViewport.setBounds (bounds);
        if (hardwareMeterPanel != nullptr && hardwareMeterPanel->isVisible())
        {
            constexpr int sendBtnH = 32;
            constexpr int sendBtnGap = 12;
            auto meterArea = bounds;
            sendPluginSettingsButton.setBounds (
                meterArea.removeFromBottom (sendBtnH).reduced (12, 0).withSizeKeepingCentre (
                    juce::jmin (420, meterArea.getWidth() - 24), sendBtnH));
            meterArea.removeFromBottom (sendBtnGap);
            hardwareMeterPanel->setBounds (meterArea);
        }
        else if (hardwareMeterPanel != nullptr)
        {
            hardwareMeterPanel->setBounds (bounds);
            sendPluginSettingsButton.setBounds ({});
        }
        layoutEditor();
    }

void MainContent::configureButton(juce::TextButton& button, const juce::String& text)
{
        button.setButtonText (text);
        button.addListener (this);
        addAndMakeVisible (button);
    }

void MainContent::setStatus (const juce::String& text, bool isError)
{
        statusDisplay.setMessage (text, isError);
        if (isError)
            HostLog::error (text);
    }

const HostPluginEntry& MainContent::currentPlugin() const
{
        jassert (juce::isPositiveAndBelow (currentPluginIndex, config.plugins.size()));
        return config.plugins.getReference (currentPluginIndex);
    }

void MainContent::ensurePluginCache(bool forceRescan)
{
        juce::String error;
        if (! AuPluginScanner::ensureCache (config.projectRoot, knownPlugins, this, error, forceRescan))
        {
            if (error.isNotEmpty())
                HostLog::error ("AU plugin cache: " + error);
            setStatus ("AU scan issue: " + error, true);
        }
        else if (forceRescan)
        {
            setStatus ("Rescanned Audio Units (" + juce::String (knownPlugins.getTypes().size()) + " found)");
        }
    }

void MainContent::populatePluginBox()
{
        if (! juce::isPositiveAndBelow (currentPluginIndex, config.plugins.size()))
        {
            currentPluginIndex = 0;
            for (int i = 0; i < config.plugins.size(); ++i)
            {
                if (config.plugins.getReference (i).installed)
                {
                    currentPluginIndex = i;
                    break;
                }
            }
        }

        pluginField.setPlugins (config.plugins, currentPluginIndex);
    }

void MainContent::confirmRemovePlugin(int index)
{
        if (! juce::isPositiveAndBelow (index, config.plugins.size()))
            return;

        const auto label = config.plugins.getReference (index).displayLabel();
        auto options = juce::MessageBoxOptions()
                           .withIconType (juce::MessageBoxIconType::QuestionIcon)
                           .withTitle ("Remove Plugin")
                           .withMessage ("Remove \"" + label + "\" from the plugin list?")
                           .withButton ("Remove")
                           .withButton ("Cancel");

        juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer<MainContent> (this), index] (int result)
                                      {
                                          if (safe == nullptr || result != 1)
                                              return;
                                          safe->removePluginAt (index);
                                      });
    }

void MainContent::removePluginAt(int index)
{
        if (! juce::isPositiveAndBelow (index, config.plugins.size()))
            return;

        const auto removedId = config.plugins.getReference (index).id;
        config.plugins.remove (index);

        if (config.defaultPluginId == removedId)
            config.defaultPluginId = config.plugins.isEmpty() ? juce::String()
                                                             : config.plugins.getReference (0).id;

        juce::String error;
        if (! config.saveToFile (error))
        {
            setStatus ("Failed to save config: " + error, true);
            return;
        }

        engine.stopFixture();
        destroyPluginEditor();

        if (config.plugins.isEmpty())
        {
            currentPluginIndex = 0;
            populatePluginBox();
            setStatus ("No plugins configured - use Plugins -> Add Plugin...");
            return;
        }

        if (currentPluginIndex >= config.plugins.size())
            currentPluginIndex = config.plugins.size() - 1;
        else if (index < currentPluginIndex)
            --currentPluginIndex;
        else if (index == currentPluginIndex)
            currentPluginIndex = juce::jlimit (0, config.plugins.size() - 1, currentPluginIndex);

        // Force reload if we removed the active plugin.
        const int next = currentPluginIndex;
        currentPluginIndex = -1;
        populatePluginBox();
        switchToPlugin (next);
        setStatus ("Removed plugin from list");
    }

bool MainContent::isInterestedInFileDrag(const juce::StringArray& files)
{
        if (! presetHardwareState.canAcceptPresetDrag())
            return false;

        for (const auto& path : files)
        {
            const juce::File file (path);
            if (file.hasFileExtension (".aupreset"))
                return true;
            if (file.isDirectory())
                return true;
        }

        return false;
    }

void MainContent::filesDropped(const juce::StringArray& files, int, int)
{
        presetHardwareState.importDroppedAupresets (files);
    }

void MainContent::switchToPlugin(int pluginIndex)
{
        if (! juce::isPositiveAndBelow (pluginIndex, config.plugins.size()))
            return;

        // Same index + already loaded: nothing to do. (Re-select used to no-op
        // even when the wrong instance was showing, which made recovery hard.)
        if (pluginIndex == currentPluginIndex && engine.getPlugin() != nullptr)
            return;

        if (! config.plugins.getReference (pluginIndex).installed)
        {
            setStatus ("Plugin not installed: " + config.plugins.getReference (pluginIndex).displayLabel(), true);
            populatePluginBox();
            return;
        }

        engine.stopFixture();
        destroyPluginEditor();

        currentPluginIndex = pluginIndex;
        const auto& plugin = currentPlugin();
        plugin.presetsDir.createDirectory();

        juce::String error;
        if (! engine.loadPlugin (plugin.toPluginDescription(), error))
        {
            setStatus ("Failed to load plugin: " + error, true);
            populatePluginBox();
            return;
        }

        presetHardwareState.populatePresets();
        savePresetNameEditor.setText ("Untitled", juce::dontSendNotification);
        // Build the editor while audio is still stopped / plugin suspended.
        // WebView AUs (e.g. Lunacy BEAM) crash if processBlock runs during UI init.
        recreatePluginEditor();

        if (! engine.startAudioDevice (error))
        {
            setStatus ("Audio device error: " + error, true);
            return;
        }

        populatePluginBox();

        if (! presetHardwareState.loadDefaultOrFirstPreset())
        {
            setStatus ("Ready - " + plugin.displayLabel()
                                     + " (presets: " + plugin.presetsDir.getFullPathName() + ")");
        }
    }

void MainContent::destroyPluginEditor()
{
        editorPlaceholder.removeAllChildren();
        engine.destroyEditor (pluginEditor);
    }

void MainContent::recreatePluginEditor()
{
        if (engine.getPlugin() == nullptr)
            return;

        destroyPluginEditor();

        pluginEditor = engine.createEditor();
        if (pluginEditor != nullptr)
        {
            editorPlaceholder.addAndMakeVisible (*pluginEditor);
            layoutEditor();
            // Log both the config selection and the live instance name — they
            // diverged when AUGenericView was mistaken for a different plugin.
            HostLog::info ("Opened editor for " + currentPlugin().displayLabel()
                           + " [instance: " + engine.getCurrentPluginName() + "]"
                           + " (" + juce::String (pluginEditor->getWidth()) + "x"
                           + juce::String (pluginEditor->getHeight()) + ")");
        }
        else
        {
            HostLog::error ("Plugin reported no editor for " + currentPlugin().displayLabel()
                            + " [instance: " + engine.getCurrentPluginName() + "]");
        }

        // createEditor() intentionally suspends DSP during UI construction.
        // When toggling back from hardware meters, audio is already running, so
        // resume processing immediately to avoid a dry/no-input path.
        if (engine.getDeviceManager().getCurrentAudioDevice() != nullptr)
            engine.setPluginProcessingSuspended (false);
    }

void MainContent::populateFixtures()
{
        const int previousId = fixtureBox.getSelectedId();
        sourceClips.rescan (config.fixturesDir);
        sourceClips.rebuildComboBox (fixtureBox, previousId);
        const int selectedId = fixtureBox.getSelectedId();
        if (selectedId > 0 && selectedId != SourceClipLibrary::selectOtherItemId)
            lastNonOtherFixtureId = selectedId;
}

void MainContent::populateMidiInputs()
{
        midiDevices = engine.getMidiInputDevices();

        juce::StringArray selectedIds;
        for (const auto& wantedName : config.defaultMidiInputs)
        {
            for (const auto& device : midiDevices)
            {
                // Match exact or substring so "Oxygen Pro 49" also selects
                // "Oxygen Pro 49 Mackie/HUI" (where DAW transport usually lives).
                if (device.name.equalsIgnoreCase (wantedName)
                    || device.name.containsIgnoreCase (wantedName))
                {
                    selectedIds.addIfNotAlreadyThere (device.identifier);
                }
            }
        }

        midiField.setDevices (midiDevices, selectedIds);
        engine.setMidiInputDevices (midiField.getSelectedIdentifiers());
    }

void MainContent::timerCallback()
{
        if (engine.consumeMidiActivity())
            midiLedLitUntil = juce::Time::getMillisecondCounterHiRes() + 150.0;

        midiLed.setActive (juce::Time::getMillisecondCounterHiRes() < midiLedLitUntil);

        if (engine.consumeHostClockQuarterPulse())
            clockLedLitUntil = juce::Time::getMillisecondCounterHiRes() + 150.0;

        clickToggle.setLedActive (juce::Time::getMillisecondCounterHiRes() < clockLedLitUntil);

        // Keep the transport glyph in sync (one-shot clips stop themselves).
        transportButton.setPlaying (engine.isPlaying());

        // Bypass resets in the engine whenever a plugin (re)loads.
        if (bypassButton.getToggleState() != engine.isBypassed())
            bypassButton.setToggleState (engine.isBypassed(), juce::dontSendNotification);

        // DAW surface Play/Stop (MIDI Start/Stop or Mackie notes 94/93).
        if (engine.consumeTransportPlayRequest())
            startPlayback();
        if (engine.consumeTransportStopRequest())
            stopPlayback();
    }

void MainContent::applyBpmFromEditor()
{
        const auto text = bpmEditor.getText().trim();
        const int bpm = text.isEmpty() ? 120 : text.getIntValue();
        const int clamped = juce::jlimit (20, 999, bpm <= 0 ? 120 : bpm);
        engine.setHostClockBpm ((double) clamped);

        if (clamped != bpm || text.isEmpty())
            bpmEditor.setText (juce::String (clamped), juce::dontSendNotification);
    }

void MainContent::loadPluginWithoutEditor()
{
        auto tryLoadIndex = [this] (int index) -> bool
        {
            if (! juce::isPositiveAndBelow (index, config.plugins.size())
                || ! config.plugins.getReference (index).installed)
                return false;

            currentPluginIndex = index;
            const auto& plugin = currentPlugin();
            plugin.presetsDir.createDirectory();

            juce::String error;
            if (! engine.loadPlugin (plugin.toPluginDescription(), error))
            {
                HostLog::error ("Failed to load " + plugin.displayLabel() + ": " + error);
                setStatus ("Failed to load plugin: " + error, true);
                return false;
            }

            HostLog::info ("Loaded plugin instance: " + engine.getCurrentPluginName()
                           + " (config: " + plugin.displayLabel() + ")");
            presetHardwareState.populatePresets();
            setStatus ("Loaded " + plugin.displayLabel() + " - opening UI...");
            populatePluginBox();
            return true;
        };

        if (tryLoadIndex (currentPluginIndex))
            return;

        // Preferred failed or was missing — fall back to first installed and
        // keep the dropdown in sync with what actually loaded.
        if (auto* fallback = config.firstInstalledPlugin())
        {
            for (int i = 0; i < config.plugins.size(); ++i)
            {
                if (&config.plugins.getReference (i) == fallback)
                {
                    if (i != currentPluginIndex)
                        HostLog::info ("Falling back to " + fallback->displayLabel());
                    if (tryLoadIndex (i))
                        return;
                    break;
                }
            }
        }

        populatePluginBox();
        setStatus (utf8 ("No installed plugin selected — choose one from the list"));
    }

void MainContent::layoutEditor()
{
        if (pluginEditor == nullptr)
        {
            editorPlaceholder.setSize (editorViewport.getWidth(), editorViewport.getHeight());
            return;
        }

        const int width = juce::jmax (pluginEditor->getWidth(), editorViewport.getWidth());
        const int height = juce::jmax (pluginEditor->getHeight(), editorViewport.getHeight());
        pluginEditor->setBounds (0, 0, pluginEditor->getWidth(), pluginEditor->getHeight());
        editorPlaceholder.setSize (width, height);
    }

void MainContent::selectFixture (int comboId)
{
        if (comboId == SourceClipLibrary::selectOtherItemId)
            return;

        const auto file = sourceClips.getFileForId (comboId);
        if (! file.existsAsFile())
            return;

        juce::String error;
        if (! engine.loadFixture (file, error))
            setStatus ("Fixture error: " + error, true);
}

void MainContent::restoreFixtureSelectionAfterSelectOtherCancelled()
{
        if (lastNonOtherFixtureId > 0
            && lastNonOtherFixtureId != SourceClipLibrary::selectOtherItemId
            && fixtureBox.indexOfItemId (lastNonOtherFixtureId) >= 0)
        {
            fixtureBox.setSelectedId (lastNonOtherFixtureId, juce::dontSendNotification);
            return;
        }

        for (int i = 0; i < fixtureBox.getNumItems(); ++i)
        {
            const int id = fixtureBox.getItemId (i);
            if (id != SourceClipLibrary::selectOtherItemId)
            {
                fixtureBox.setSelectedId (id, juce::dontSendNotification);
                lastNonOtherFixtureId = id;
                return;
            }
        }

        fixtureBox.setSelectedId (0, juce::dontSendNotification);
}

void MainContent::showInvalidWavAlert()
{
        auto options = juce::MessageBoxOptions()
                           .withIconType (juce::MessageBoxIconType::WarningIcon)
                           .withTitle (utf8 ("Invalid WAV file."))
                           .withMessage ({})
                           .withButton (utf8 ("OK"))
                           .withAssociatedComponent (this);
        juce::AlertWindow::showAsync (options, nullptr);
}

void MainContent::browseForOtherSourceClip()
{
        auto startDir = HostPreferences::get().getLastSourceClipBrowseDir();
        if (! startDir.isDirectory())
        {
            if (config.fixturesDir.isDirectory())
                startDir = config.fixturesDir;
            else
                startDir = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        }

        auto chooser = std::make_shared<juce::FileChooser> (utf8 ("Please select a WAV file."),
                                                            startDir,
                                                            "*.wav");
        sourceClipFileChooser = chooser;
        chooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [this, chooser] (const juce::FileChooser& fc)
                              {
                                  sourceClipFileChooser.reset();
                                  const auto result = fc.getResult();
                                  if (result == juce::File())
                                  {
                                      restoreFixtureSelectionAfterSelectOtherCancelled();
                                      return;
                                  }

                                  HostPreferences::get().setLastSourceClipBrowseDir (result.getParentDirectory());

                                  if (! SourceClipLibrary::isWavFile (result))
                                  {
                                      showInvalidWavAlert();
                                      restoreFixtureSelectionAfterSelectOtherCancelled();
                                      return;
                                  }

                                  juce::String error;
                                  if (! engine.loadFixture (result, error))
                                  {
                                      showInvalidWavAlert();
                                      restoreFixtureSelectionAfterSelectOtherCancelled();
                                      return;
                                  }

                                  const int id = sourceClips.selectOrAddTemporaryTopLevel (fixtureBox, result);
                                  if (id <= 0)
                                  {
                                      showInvalidWavAlert();
                                      restoreFixtureSelectionAfterSelectOtherCancelled();
                                      return;
                                  }

                                  lastNonOtherFixtureId = id;
                                  setStatus ("Loaded source clip: " + result.getFileNameWithoutExtension(), false);
                              });
}

bool MainContent::loadExternalSourceClip (const juce::File& file, juce::String& error)
{
        if (sourceClips.selectOrAddExternal (fixtureBox, file) <= 0)
        {
            error = "Unsupported or missing audio file: " + file.getFullPathName();
            return false;
        }

        lastNonOtherFixtureId = fixtureBox.getSelectedId();

        if (! engine.loadFixture (file, error))
        {
            error = "Failed to load source clip: " + error;
            return false;
        }

        return true;
}

void MainContent::startPlayback()
{
        selectFixture (fixtureBox.getSelectedId());
        engine.playFixture();
        transportButton.setPlaying (true);
        setStatus (engine.isLooping() ? "Looping clip..." : "Playing clip (one-shot)...");
    }

void MainContent::stopPlayback()
{
        engine.stopFixture();
        transportButton.setPlaying (false);
        setStatus ("Stopped");
    }

void MainContent::togglePlayback()
{
        if (engine.isPlaying())
            stopPlayback();
        else
            startPlayback();
    }

void MainContent::resetPluginToDefaults()
{
        if (! juce::isPositiveAndBelow (currentPluginIndex, config.plugins.size())
            || ! currentPlugin().installed)
        {
            setStatus ("No installed plugin to reset", true);
            return;
        }

        engine.stopFixture();
        transportButton.setPlaying (false);
        destroyPluginEditor();

        const auto& plugin = currentPlugin();
        juce::String error;
        if (! engine.loadPlugin (plugin.toPluginDescription(), error))
        {
            setStatus ("Failed to reset plugin: " + error, true);
            return;
        }

        savePresetNameEditor.setText ("Untitled", juce::dontSendNotification);
        recreatePluginEditor();

        if (! engine.startAudioDevice (error))
        {
            setStatus ("Audio device error: " + error, true);
            return;
        }

        setStatus ("Reset " + plugin.displayLabel() + " to defaults");
    }

bool MainContent::isEditableFieldFocused()
{
        auto* focused = juce::Component::getCurrentlyFocusedComponent();
        if (focused == nullptr)
            return false;

        if (dynamic_cast<juce::TextEditor*> (focused) != nullptr)
            return true;

        if (focused->findParentComponentOfClass<juce::TextEditor>() != nullptr)
            return true;

        // Covers plugin UIs that implement TextInputTarget without using TextEditor.
        if (dynamic_cast<juce::TextInputTarget*> (focused) != nullptr)
            return true;

        return false;
    }

bool MainContent::keyPressed(const juce::KeyPress& key, juce::Component*)
{
        if (! key.isKeyCode (juce::KeyPress::spaceKey))
            return false;

        if (isEditableFieldFocused())
            return false;

        togglePlayback();
        return true;
    }

void MainContent::buttonClicked(juce::Button* button)
{
        if (button == &loadPresetButton)
        {
            presetHardwareState.handleLoadButton();
            return;
        }

        if (button == &savePresetButton)
        {
            presetHardwareState.savePresetFromEditor();
            return;
        }

        if (button == &resetButton)
        {
            resetPluginToDefaults();
            return;
        }

        if (button == &transportButton)
        {
            togglePlayback();
            return;
        }

        if (button == &loopToggle)
        {
            engine.setLooping (loopToggle.getToggleState());
            return;
        }

        if (button == &bypassButton)
        {
            engine.setBypassed (bypassButton.getToggleState());
            setStatus (bypassButton.getToggleState() ? "Plugin bypassed" : "Plugin active");
            return;
        }

        if (button == &hostClockToggle)
        {
            applyBpmFromEditor();
            engine.setHostClockEnabled (hostClockToggle.getToggleState());
            return;
        }

        if (button == &clickToggle)
        {
            engine.setMetronomeClickEnabled (clickToggle.getToggleState());
            return;
        }
    }

void MainContent::comboBoxChanged(juce::ComboBox* box)
{
        if (box == &fixtureBox)
        {
            const int selectedId = fixtureBox.getSelectedId();
            if (selectedId == SourceClipLibrary::selectOtherItemId)
            {
                browseForOtherSourceClip();
                return;
            }

            if (selectedId > 0)
                lastNonOtherFixtureId = selectedId;

            selectFixture (selectedId);
            return;
        }

        if (box == &presetBox)
        {
            presetHardwareState.onPresetBoxChanged();
        }
    }

void MainContent::textEditorTextChanged(juce::TextEditor&)
{}

void MainContent::textEditorReturnKeyPressed(juce::TextEditor& editor)
{
        if (&editor == &bpmEditor)
            applyBpmFromEditor();
    }

void MainContent::textEditorFocusLost(juce::TextEditor& editor)
{
        if (&editor == &bpmEditor)
            applyBpmFromEditor();
    }

