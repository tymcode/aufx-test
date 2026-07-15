#include "MainWindow.h"
#include "HostLog.h"
#include "OfflineCapture.h"

namespace
{
    juce::Array<juce::File> collectFiles (const juce::File& root, const juce::String& extension, bool recursive)
    {
        juce::Array<juce::File> results;
        juce::Array<juce::File> stack;
        stack.add (root);

        while (! stack.isEmpty())
        {
            const auto dir = stack.removeAndReturn (0);
            for (const auto& entry : dir.findChildFiles (juce::File::findFilesAndDirectories, false))
            {
                if (entry.isDirectory())
                {
                    if (recursive)
                        stack.add (entry);
                }
                else if (entry.hasFileExtension (extension))
                {
                    results.add (entry);
                }
            }
        }

        struct FileComparator
        {
            static int compareElements (const juce::File& a, const juce::File& b)
            {
                return a.getFileName().compareIgnoreCase (b.getFileName());
            }
        };

        FileComparator comparator;
        results.sort (comparator);
        return results;
    }

    juce::String slugify (juce::String value)
    {
        value = value.trim().toLowerCase();
        juce::String out;
        bool lastUnderscore = false;

        for (auto ch : value)
        {
            if (juce::CharacterFunctions::isLetterOrDigit (ch))
            {
                out << ch;
                lastUnderscore = false;
            }
            else if (! lastUnderscore)
            {
                out << '_';
                lastUnderscore = true;
            }
        }

        return out.trimCharactersAtEnd ("_");
    }

    juce::String keywordFromDescription (const juce::String& description)
    {
        const auto slug = slugify (description);
        if (slug.isEmpty())
            return {};

        return slug.upToFirstOccurrenceOf ("_", false, false);
    }

}

class MainWindow::MainContent : public juce::Component,
                                private juce::Button::Listener,
                                private juce::ComboBox::Listener,
                                private juce::KeyListener
{
public:
    MainContent (PluginAudioEngine& audioEngine, HostConfig hostConfig)
        : engine (audioEngine), config (std::move (hostConfig))
    {
        setOpaque (true);
        setWantsKeyboardFocus (true);

        currentPluginIndex = 0;
        for (int i = 0; i < config.plugins.size(); ++i)
        {
            if (config.plugins.getReference (i).id == config.defaultPluginId)
            {
                currentPluginIndex = i;
                break;
            }
        }

        titleLabel.setText ("Plugin Host", juce::dontSendNotification);
        titleLabel.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        addAndMakeVisible (titleLabel);

        pluginBox.setTooltip ("Plugins from host.config.json");
        addAndMakeVisible (pluginBox);
        pluginBox.addListener (this);

        setStatus ("Loading plugin...");
        addAndMakeVisible (statusLabel);

        presetLabel.setText ("Preset", juce::dontSendNotification);
        addAndMakeVisible (presetLabel);
        addAndMakeVisible (presetBox);
        presetBox.addListener (this);

        fixtureLabel.setText ("Fixture", juce::dontSendNotification);
        addAndMakeVisible (fixtureLabel);
        addAndMakeVisible (fixtureBox);
        fixtureBox.addListener (this);

        configureButton (loadPresetButton, "Load");
        configureButton (savePresetButton, "Save");
        configureButton (playButton, "Play");
        configureButton (stopButton, "Stop");
        configureButton (captureButton, "Capture Test Case");

        savePresetNameLabel.setText ("Save as", juce::dontSendNotification);
        addAndMakeVisible (savePresetNameLabel);
        savePresetNameEditor.setText ("Untitled", juce::dontSendNotification);
        savePresetNameEditor.setInputRestrictions (64);
        addAndMakeVisible (savePresetNameEditor);

        replacePresetToggle.setButtonText ("Replace existing");
        replacePresetToggle.setToggleState (false, juce::dontSendNotification);
        addAndMakeVisible (replacePresetToggle);

        descriptionLabel.setText ("Description", juce::dontSendNotification);
        addAndMakeVisible (descriptionLabel);
        snapshotNameEditor.setText ("snapshot", juce::dontSendNotification);
        snapshotNameEditor.setInputRestrictions (64);
        addAndMakeVisible (snapshotNameEditor);

        // Output WAV role flag: golden→_gld, suspect→_sus, broken→_bkn
        artifactRoleBox.addItem ("golden", 1);
        artifactRoleBox.addItem ("suspect", 2);
        artifactRoleBox.addItem ("broken", 3);
        artifactRoleBox.setSelectedId (3); // default: broken
        addAndMakeVisible (artifactRoleBox);

        addAndMakeVisible (editorViewport);
        editorViewport.setViewedComponent (&editorPlaceholder, false);

        populatePluginBox();
        populatePresets();
        populateFixtures();
        loadPluginWithoutEditor();
    }

    ~MainContent() override
    {
        engine.stopFixture();
        engine.stopAudioDevice();
        destroyPluginEditor();

        if (keyListenerOwner != nullptr)
            keyListenerOwner->removeKeyListener (this);
    }

    /** Call after the host window is on-screen so AU Cocoa UIs can attach to an NSWindow. */
    void showPluginEditor()
    {
        recreatePluginEditor();

        if (loadDefaultOrFirstPreset())
            return;

        if (! fixtureFiles.isEmpty())
        {
            selectFixture (0);
            setStatus ("Ready — " + currentPlugin().displayLabel());
        }
        else
        {
            setStatus ("Ready — " + currentPlugin().displayLabel());
        }
    }

    /** Prefer config default_preset when present; otherwise the first preset in the list. */
    bool loadDefaultOrFirstPreset()
    {
        const auto& defaultPreset = currentPlugin().defaultPreset;
        if (defaultPreset.existsAsFile())
        {
            selectPresetInDropdown (defaultPreset);
            // If the file isn't in the scanned presets folder, select by loading directly.
            if (getSelectedPresetFile() != defaultPreset
                && getSelectedPresetFile().getFullPathName() != defaultPreset.getFullPathName())
            {
                juce::String error;
                if (! engine.loadPreset (defaultPreset, error))
                {
                    setStatus ("Default preset error: " + error, true);
                    return false;
                }

                if (pluginEditor != nullptr)
                    pluginEditor->repaint();

                savePresetNameEditor.setText (defaultPreset.getFileNameWithoutExtension(),
                                              juce::dontSendNotification);
                setStatus ("Loaded default preset: " + defaultPreset.getFileName());
                return true;
            }

            loadSelectedPreset();
            return true;
        }

        if (! presetFiles.isEmpty())
        {
            presetBox.setSelectedItemIndex (0, juce::dontSendNotification);
            loadSelectedPreset();
            return true;
        }

        return false;
    }

    void parentHierarchyChanged() override
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

    void paint (juce::Graphics& g) override
    {
        g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced (12);
        auto top = bounds.removeFromTop (28);
        titleLabel.setBounds (top.removeFromLeft (110));
        top.removeFromLeft (8);
        pluginBox.setBounds (top.removeFromLeft (320));
        top.removeFromLeft (8);
        statusLabel.setBounds (top);

        auto controls = bounds.removeFromTop (108);
        auto row1 = controls.removeFromTop (32);
        presetLabel.setBounds (row1.removeFromLeft (55));
        presetBox.setBounds (row1.removeFromLeft (260));
        row1.removeFromLeft (6);
        loadPresetButton.setBounds (row1.removeFromLeft (60));
        row1.removeFromLeft (10);
        savePresetNameLabel.setBounds (row1.removeFromLeft (55));
        savePresetNameEditor.setBounds (row1.removeFromLeft (160));
        row1.removeFromLeft (6);
        savePresetButton.setBounds (row1.removeFromLeft (60));
        row1.removeFromLeft (6);
        replacePresetToggle.setBounds (row1.removeFromLeft (140));

        auto row2 = controls.removeFromTop (32);
        fixtureLabel.setBounds (row2.removeFromLeft (55));
        fixtureBox.setBounds (row2.removeFromLeft (260));
        row2.removeFromLeft (6);
        playButton.setBounds (row2.removeFromLeft (70));
        row2.removeFromLeft (6);
        stopButton.setBounds (row2.removeFromLeft (70));

        auto row3 = controls.removeFromTop (32);
        captureButton.setBounds (row3.removeFromLeft (160));
        row3.removeFromLeft (8);
        descriptionLabel.setBounds (row3.removeFromLeft (80));
        snapshotNameEditor.setBounds (row3.removeFromLeft (200));
        row3.removeFromLeft (6);
        artifactRoleBox.setBounds (row3.removeFromLeft (110));

        editorViewport.setBounds (bounds);
        layoutEditor();
    }

private:
    void configureButton (juce::TextButton& button, const juce::String& text)
    {
        button.setButtonText (text);
        button.addListener (this);
        addAndMakeVisible (button);
    }

    void setStatus (const juce::String& text, bool isError = false)
    {
        statusLabel.setText (text, juce::dontSendNotification);
        if (isError)
            HostLog::error (text);
    }

    const HostPluginEntry& currentPlugin() const
    {
        return config.plugins.getReference (currentPluginIndex);
    }

    void populatePluginBox()
    {
        pluginBox.clear (juce::dontSendNotification);

        for (int i = 0; i < config.plugins.size(); ++i)
            pluginBox.addItem (config.plugins.getReference (i).displayLabel(), i + 1);

        pluginBox.setSelectedItemIndex (currentPluginIndex, juce::dontSendNotification);
    }

    void populatePresets()
    {
        presetBox.clear();
        presetFiles.clearQuick();

        auto presetsDir = currentPlugin().presetsDir;
        if (presetsDir != juce::File())
            presetsDir.createDirectory();

        if (! presetsDir.isDirectory())
        {
            presetBox.addItem ("(no presets folder)", 1);
            return;
        }

        presetFiles = collectFiles (presetsDir, ".aupreset", true);

        for (int i = 0; i < presetFiles.size(); ++i)
            presetBox.addItem (presetFiles[i].getRelativePathFrom (presetsDir), i + 1);

        if (presetFiles.isEmpty())
            presetBox.addItem ("(no presets found)", 1);
    }

    void switchToPlugin (int pluginIndex)
    {
        if (! juce::isPositiveAndBelow (pluginIndex, config.plugins.size())
            || pluginIndex == currentPluginIndex)
            return;

        engine.stopFixture();
        destroyPluginEditor();

        currentPluginIndex = pluginIndex;
        const auto& plugin = currentPlugin();
        plugin.presetsDir.createDirectory();

        juce::String error;
        if (! engine.loadPlugin (plugin.path, error))
        {
            setStatus ("Failed to load plugin: " + error, true);
            return;
        }

        if (! engine.startAudioDevice (error))
        {
            setStatus ("Audio device error: " + error, true);
            return;
        }

        populatePresets();
        savePresetNameEditor.setText ("Untitled", juce::dontSendNotification);
        recreatePluginEditor();

        if (! loadDefaultOrFirstPreset())
        {
            setStatus ("Ready — " + plugin.displayLabel()
                                     + " (presets: " + plugin.presetsDir.getFullPathName() + ")");
        }
    }

    void selectPresetInDropdown (const juce::File& presetFile)
    {
        for (int i = 0; i < presetFiles.size(); ++i)
        {
            if (presetFiles[i] == presetFile
                || presetFiles[i].getFullPathName() == presetFile.getFullPathName())
            {
                presetBox.setSelectedItemIndex (i, juce::dontSendNotification);
                savePresetNameEditor.setText (presetFile.getFileNameWithoutExtension(),
                                              juce::dontSendNotification);
                return;
            }
        }
    }

    juce::File getSelectedPresetFile() const
    {
        // Trust the selected index. Avoid string equality checks that can reject
        // the first list entry (commonly the Logic "initial" presets).
        const int index = presetBox.getSelectedItemIndex();
        if (index >= 0 && index < presetFiles.size())
            return presetFiles[index];

        // Fallback: match the displayed text to a known preset path/name.
        const auto text = presetBox.getText().trim();
        if (text.isNotEmpty())
        {
            for (const auto& file : presetFiles)
            {
                if (file.getRelativePathFrom (currentPlugin().presetsDir) == text
                    || file.getFileName() == text
                    || file.getFileNameWithoutExtension() == text)
                {
                    return file;
                }
            }
        }

        return {};
    }

    void destroyPluginEditor()
    {
        editorPlaceholder.removeAllChildren();
        engine.destroyEditor (pluginEditor);
    }

    void recreatePluginEditor()
    {
        if (engine.getPlugin() == nullptr)
            return;

        destroyPluginEditor();

        pluginEditor = engine.createEditor();
        if (pluginEditor != nullptr)
        {
            editorPlaceholder.addAndMakeVisible (*pluginEditor);
            layoutEditor();
            HostLog::info ("Opened editor for " + currentPlugin().displayLabel()
                           + " (" + juce::String (pluginEditor->getWidth()) + "x"
                           + juce::String (pluginEditor->getHeight()) + ")");
        }
        else
        {
            HostLog::error ("Plugin reported no editor for " + currentPlugin().displayLabel());
        }
    }

    void loadSelectedPreset()
    {
        const auto presetFile = getSelectedPresetFile();
        if (! presetFile.exists())
        {
            setStatus ("Select a preset to load", true);
            return;
        }

        juce::String error;
        if (! engine.loadPreset (presetFile, error))
        {
            setStatus ("Preset error: " + error, true);
            return;
        }

        // Keep the existing Cocoa UI. Destroying/recreating after setStateInformation
        // often falls back to AUGenericView (placard / property list / channel layout).
        if (pluginEditor != nullptr)
            pluginEditor->repaint();

        savePresetNameEditor.setText (presetFile.getFileNameWithoutExtension(),
                                      juce::dontSendNotification);
        setStatus ("Loaded preset: " + presetFile.getFileName());
    }

    juce::File presetFileForName (const juce::String& presetName) const
    {
        auto fileName = juce::File::createLegalFileName (presetName.trim());
        if (fileName.isEmpty())
            fileName = "Untitled";

        if (! fileName.endsWithIgnoreCase (".aupreset"))
            fileName << ".aupreset";

        return currentPlugin().presetsDir.getChildFile (fileName);
    }

    void savePresetFromEditor()
    {
        if (engine.getPlugin() == nullptr)
        {
            setStatus ("No plugin loaded", true);
            return;
        }

        const auto presetName = savePresetNameEditor.getText().trim();
        if (presetName.isEmpty())
        {
            setStatus ("Enter a preset name before saving", true);
            return;
        }

        const auto dest = presetFileForName (presetName);
        const bool replacing = dest.existsAsFile();
        if (replacing && ! replacePresetToggle.getToggleState())
        {
            setStatus ("Preset exists — enable Replace existing to overwrite", true);
            return;
        }

        juce::String error;
        if (! engine.saveCurrentPreset (dest, error))
        {
            setStatus ("Failed to save preset: " + error, true);
            return;
        }

        populatePresets();
        selectPresetInDropdown (dest);
        setStatus ((replacing ? "Replaced preset: " : "Saved preset: ") + dest.getFileName());
    }

    void populateFixtures()
    {
        fixtureFiles = collectFiles (config.fixturesDir, ".wav", false);
        fixtureBox.clear();

        for (int i = 0; i < fixtureFiles.size(); ++i)
            fixtureBox.addItem (fixtureFiles[i].getFileName(), i + 1);

        if (! fixtureFiles.isEmpty())
            fixtureBox.setSelectedId (1, juce::dontSendNotification);
    }

    void loadPluginWithoutEditor()
    {
        const auto& plugin = currentPlugin();
        plugin.presetsDir.createDirectory();

        juce::String error;
        if (! engine.loadPlugin (plugin.path, error))
        {
            setStatus ("Failed to load plugin: " + error, true);
            return;
        }

        if (! engine.startAudioDevice (error))
        {
            setStatus ("Audio device error: " + error, true);
            return;
        }

        populatePresets();
        setStatus ("Loaded " + plugin.displayLabel() + " — opening UI...");
    }

    void layoutEditor()
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

    void selectFixture (int index)
    {
        if (! juce::isPositiveAndBelow (index, fixtureFiles.size()))
            return;

        juce::String error;
        if (! engine.loadFixture (fixtureFiles[index], error))
            setStatus ("Fixture error: " + error, true);
    }

    void startPlayback()
    {
        const int index = fixtureBox.getSelectedId() - 1;
        selectFixture (index);
        engine.playFixture();
        setStatus ("Looping fixture...");
    }

    void stopPlayback()
    {
        engine.stopFixture();
        setStatus ("Stopped");
    }

    void togglePlayback()
    {
        if (engine.isPlaying())
            stopPlayback();
        else
            startPlayback();
    }

    static bool isEditableFieldFocused()
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

    using juce::Component::keyPressed;

    bool keyPressed (const juce::KeyPress& key, juce::Component*) override
    {
        if (! key.isKeyCode (juce::KeyPress::spaceKey))
            return false;

        if (isEditableFieldFocused())
            return false;

        togglePlayback();
        return true;
    }

    void buttonClicked (juce::Button* button) override
    {
        if (button == &loadPresetButton)
        {
            loadSelectedPreset();
            return;
        }

        if (button == &savePresetButton)
        {
            savePresetFromEditor();
            return;
        }

        if (button == &playButton)
        {
            startPlayback();
            return;
        }

        if (button == &stopButton)
        {
            stopPlayback();
            return;
        }

        if (button == &captureButton)
            captureTestCase();
    }

    void comboBoxChanged (juce::ComboBox* box) override
    {
        if (box == &pluginBox)
        {
            switchToPlugin (pluginBox.getSelectedItemIndex());
            return;
        }

        if (box == &fixtureBox)
        {
            selectFixture (fixtureBox.getSelectedId() - 1);
            return;
        }

        if (box == &presetBox)
        {
            const auto presetFile = getSelectedPresetFile();
            if (presetFile.exists())
            {
                savePresetNameEditor.setText (presetFile.getFileNameWithoutExtension(),
                                              juce::dontSendNotification);
            }
        }
    }

    void captureTestCase()
    {
        const int fixtureIndex = fixtureBox.getSelectedId() - 1;
        if (! juce::isPositiveAndBelow (fixtureIndex, fixtureFiles.size()))
        {
            setStatus ("Select a fixture WAV before capturing", true);
            return;
        }

        if (engine.getPlugin() == nullptr)
        {
            setStatus ("No plugin loaded", true);
            return;
        }

        const auto snapshotName = snapshotNameEditor.getText().trim();
        if (snapshotName.isEmpty())
        {
            setStatus ("Snapshot name is required", true);
            return;
        }

        const auto fixtureFile = fixtureFiles[fixtureIndex];
        const auto captureDir = config.sessionsRoot
                                    .getChildFile (slugify (currentPlugin().sessionName))
                                    .getChildFile ("artifacts");
        captureDir.createDirectory();

        const auto keyword = keywordFromDescription (snapshotName);
        const auto token = juce::Uuid().toString().substring (0, 8);
        const auto stem = keyword.isNotEmpty() ? keyword + "_" + token : token;
        // Always dump the live plugin state — never copy the selected library
        // .aupreset, which may be stale after UI tweaks.
        const auto presetOut = captureDir.getChildFile (stem + ".aupreset");
        const auto roleSuffix = artifactRoleCode();
        const auto outputOut = captureDir.getChildFile (stem + "_output_" + roleSuffix + ".wav");

        juce::String error;
        if (! engine.saveCurrentPreset (presetOut, error))
        {
            setStatus ("Failed to save preset: " + error, true);
            return;
        }

        engine.stopFixture();
        engine.stopAudioDevice();

        OfflineCaptureOptions renderOptions;
        if (! OfflineCapture::renderPluginToFile (*engine.getPlugin(), fixtureFile, outputOut, renderOptions, error))
        {
            engine.startAudioDevice (error);
            setStatus ("Offline capture failed: " + error, true);
            return;
        }

        engine.startAudioDevice (error);

        if (! registerSnapshotWithPython (snapshotName, fixtureFile, outputOut, presetOut, error))
        {
            setStatus ("Capture saved to disk but session update failed: " + error, true);
            return;
        }

        setStatus ("Captured test case: " + snapshotName);
    }

    juce::String artifactRoleCode() const
    {
        switch (artifactRoleBox.getSelectedId())
        {
            case 1:  return "gld";
            case 2:  return "sus";
            default: return "bkn";
        }
    }

    bool registerSnapshotWithPython (const juce::String& snapshotName,
                                     const juce::File& inputFile,
                                     const juce::File& outputFile,
                                     const juce::File& presetFile,
                                     juce::String& error)
    {
        juce::File cli = config.pythonCli;
        if (! cli.existsAsFile())
            cli = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                      .getParentDirectory()
                      .getChildFile ("aufx-test");

        if (! cli.existsAsFile())
        {
            error = "Could not find aufx-test CLI (set python_cli in host.config.json)";
            return false;
        }

        // --root must precede the snap subcommand (parent-parser option).
        juce::StringArray args;
        args.add (cli.getFullPathName());
        args.add ("session");
        args.add ("--root");
        args.add (config.sessionsRoot.getFullPathName());
        args.add ("snap");
        args.add (currentPlugin().sessionName);
        args.add (snapshotName);
        args.add ("--input");
        args.add (inputFile.getFullPathName());
        args.add ("--output");
        args.add (outputFile.getFullPathName());
        args.add ("--aupreset");
        args.add (presetFile.getFullPathName());
        args.add ("--notes");
        args.add ("Captured from plugin_host_app");

        juce::ChildProcess process;
        if (! process.start (args))
        {
            error = "Failed to start aufx-test CLI";
            return false;
        }

        if (! process.waitForProcessToFinish (120000))
        {
            error = "Timed out waiting for aufx-test session snap";
            return false;
        }

        const auto exitCode = process.getExitCode();
        if (exitCode != 0)
        {
            error = process.readAllProcessOutput().trim();
            if (error.isEmpty())
                error = "aufx-test session snap failed with exit code " + juce::String (exitCode);
            return false;
        }

        return true;
    }

    PluginAudioEngine& engine;
    HostConfig config;
    int currentPluginIndex { 0 };

    juce::Label titleLabel;
    juce::ComboBox pluginBox;
    juce::Label statusLabel;
    juce::Label presetLabel;
    juce::Label fixtureLabel;
    juce::ComboBox presetBox;
    juce::ComboBox fixtureBox;
    juce::TextButton loadPresetButton;
    juce::TextButton savePresetButton;
    juce::Label savePresetNameLabel;
    juce::TextEditor savePresetNameEditor;
    juce::ToggleButton replacePresetToggle;
    juce::TextButton playButton;
    juce::TextButton stopButton;
    juce::TextButton captureButton;
    juce::Label descriptionLabel;
    juce::TextEditor snapshotNameEditor;
    juce::ComboBox artifactRoleBox;
    juce::Viewport editorViewport;
    juce::Component editorPlaceholder;
    juce::AudioProcessorEditor* pluginEditor { nullptr };
    juce::Array<juce::File> presetFiles;
    juce::Array<juce::File> fixtureFiles;
    juce::Component* keyListenerOwner { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainContent)
};

MainWindow::MainWindow (HostConfig hostConfig)
    : DocumentWindow ("Plugin Host",
                      juce::Desktop::getInstance().getDefaultLookAndFeel()
                          .findColour (juce::ResizableWindow::backgroundColourId),
                      DocumentWindow::allButtons),
      config (std::move (hostConfig))
{
    engine = std::make_unique<PluginAudioEngine>();
    content = std::make_unique<MainContent> (*engine, config);
    auto* mainContent = content.get();
    setContentOwned (content.release(), true);
    setResizable (true, true);
    centreWithSize (1100, 780);
    setVisible (true);

    // AU Cocoa editors need a real NSWindow parent; create after the host is shown.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainContent> (mainContent)]
                                     {
                                         if (safe != nullptr)
                                             safe->showPluginEditor();
                                     });
}

MainWindow::~MainWindow()
{
    // MainContent holds PluginAudioEngine&; DocumentWindow owns content and
    // would destroy it after our members. Clear it first so ~MainContent
    // still sees a live engine (otherwise quit crashes in destroyEditor).
    clearContentComponent();
    engine.reset();
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}
