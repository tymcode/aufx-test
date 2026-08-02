#include "SettingsDialog.h"
#include "HostDialog.h"
#include "HostPreferences.h"
#include "SessionSnap.h"
#include "Utf8.h"

namespace
{
    void wireChooseDirectory (juce::TextButton& button,
                              juce::TextEditor& editor,
                              const juce::String& title)
    {
        button.onClick = [&editor, title]
        {
            auto chooser = std::make_shared<juce::FileChooser> (title,
                                                                juce::File (editor.getText()),
                                                                "*");
            chooser->launchAsync (juce::FileBrowserComponent::openMode
                                      | juce::FileBrowserComponent::canSelectDirectories,
                                  [&editor, chooser] (const juce::FileChooser& fc)
                                  {
                                      const auto result = fc.getResult();
                                      if (result != juce::File())
                                          editor.setText (result.getFullPathName(),
                                                          juce::dontSendNotification);
                                  });
        };
    }

    void wireReveal (juce::TextButton& button, juce::TextEditor& editor)
    {
        button.onClick = [&editor]
        {
            const auto path = juce::File (editor.getText().trim());
            if (path != juce::File())
                path.revealToUser();
        };
    }

    void layoutPathRow (juce::Rectangle<int>& area,
                        juce::Label& label,
                        juce::TextEditor& editor,
                        juce::TextButton& choose,
                        juce::TextButton& secondary)
    {
        label.setBounds (area.removeFromTop (22));
        auto row = area.removeFromTop (28);
        choose.setBounds (row.removeFromRight (88));
        row.removeFromRight (6);
        secondary.setBounds (row.removeFromRight (72));
        row.removeFromRight (6);
        editor.setBounds (row);
        area.removeFromTop (12);
    }

    bool directoryHasContent (const juce::File& dir)
    {
        if (! dir.isDirectory())
            return false;

        for (const auto& child : dir.findChildFiles (juce::File::findFilesAndDirectories, false))
            if (child.getFileName() != ".DS_Store")
                return true;
        return false;
    }

    juce::File aufxTestCliUnder (const juce::File& root)
    {
        if (root == juce::File() || ! root.isDirectory())
            return {};

        const auto candidate = root.getChildFile (".venv")
                                   .getChildFile ("bin")
                                   .getChildFile ("aufx-test");
        return candidate.existsAsFile() ? candidate : juce::File();
    }

    juce::File searchAufxTestNear (juce::File start, int maxParents)
    {
        for (int i = 0; i <= maxParents && start != juce::File(); ++i)
        {
            if (const auto found = aufxTestCliUnder (start); found != juce::File())
                return found;
            start = start.getParentDirectory();
        }
        return {};
    }
}

SettingsPanel::SettingsPanel (const HostConfig& config)
{
    dataRootLabel.setText (utf8 ("Exploration data folder"), juce::dontSendNotification);
    addAndMakeVisible (dataRootLabel);
    dataRootEditor.setText (config.projectRoot.getFullPathName(), juce::dontSendNotification);
    addAndMakeVisible (dataRootEditor);
    chooseDataRootButton.setButtonText (utf8 ("Choose..."));
    revealDataRootButton.setButtonText (utf8 ("Reveal"));
    addAndMakeVisible (chooseDataRootButton);
    addAndMakeVisible (revealDataRootButton);

    configLabel.setText (utf8 ("Config file override (optional)"), juce::dontSendNotification);
    addAndMakeVisible (configLabel);
    configEditor.setText (HostPreferences::get().getConfigPathPref(), juce::dontSendNotification);
    configEditor.setTextToShowWhenEmpty (utf8 ("(use data folder / bundled default)"), juce::Colours::grey);
    chooseConfigButton.setButtonText (utf8 ("Choose..."));
    clearConfigButton.setButtonText (utf8 ("Clear"));
    addAndMakeVisible (configEditor);
    addAndMakeVisible (chooseConfigButton);
    addAndMakeVisible (clearConfigButton);

    fixturesLabel.setText (utf8 ("Source Clips Directory"), juce::dontSendNotification);
    addAndMakeVisible (fixturesLabel);
    fixturesEditor.setText (config.fixturesDir.getFullPathName(), juce::dontSendNotification);
    chooseFixturesButton.setButtonText (utf8 ("Choose..."));
    revealFixturesButton.setButtonText (utf8 ("Reveal"));
    addAndMakeVisible (fixturesEditor);
    addAndMakeVisible (chooseFixturesButton);
    addAndMakeVisible (revealFixturesButton);

    sessionsLabel.setText (utf8 ("Sessions directory"), juce::dontSendNotification);
    addAndMakeVisible (sessionsLabel);
    sessionsEditor.setText (config.sessionsRoot.getFullPathName(), juce::dontSendNotification);
    chooseSessionsButton.setButtonText (utf8 ("Choose..."));
    revealSessionsButton.setButtonText (utf8 ("Reveal"));
    addAndMakeVisible (sessionsEditor);
    addAndMakeVisible (chooseSessionsButton);
    addAndMakeVisible (revealSessionsButton);

    pythonCliLabel.setText (utf8 ("Location of aufx-test CLI"), juce::dontSendNotification);
    addAndMakeVisible (pythonCliLabel);
    pythonCliEditor.setText (config.pythonCli != juce::File() ? config.pythonCli.getFullPathName()
                                                             : juce::String(),
                             juce::dontSendNotification);
    pythonCliEditor.addListener (this);
    addAndMakeVisible (pythonCliEditor);
    testPythonCliButton.setButtonText (utf8 ("Test"));
    testPythonCliButton.setTooltip (
        utf8 ("Runs `aufx-test --help` at this path. Green = OK, red = failed, grey = not tested since last edit."));
    addAndMakeVisible (testPythonCliButton);
    clearPythonCliButton.setButtonText (utf8 ("Clear"));
    addAndMakeVisible (clearPythonCliButton);
    pythonCliHint.setFont (juce::FontOptions (12.0f));
    pythonCliHint.setColour (juce::Label::textColourId, juce::Colours::grey);
    pythonCliHint.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (pythonCliHint);
    setCliTestState (CliTestState::untested);

    defaultPluginLabel.setText (utf8 ("Default plugin"), juce::dontSendNotification);
    addAndMakeVisible (defaultPluginLabel);
    defaultPluginBox.addItem (utf8 ("(none)"), 1);
    for (int i = 0; i < config.plugins.size(); ++i)
    {
        const auto& plugin = config.plugins.getReference (i);
        defaultPluginBox.addItem (plugin.displayLabel(), i + 2);
        if (plugin.id == config.defaultPluginId)
            defaultPluginBox.setSelectedId (i + 2, juce::dontSendNotification);
    }
    if (defaultPluginBox.getSelectedId() == 0)
        defaultPluginBox.setSelectedId (1, juce::dontSendNotification);
    addAndMakeVisible (defaultPluginBox);

    resetButton.setButtonText (utf8 ("Reset to defaults"));
    addAndMakeVisible (resetButton);

    wireChooseDirectory (chooseDataRootButton, dataRootEditor, utf8 ("Choose exploration data folder"));
    wireReveal (revealDataRootButton, dataRootEditor);
    dataRootEditor.addListener (this);

    chooseConfigButton.onClick = [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> (utf8 ("Choose host.config.json"),
                                                            juce::File (configEditor.getText()),
                                                            "*.json");
        chooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [this, chooser] (const juce::FileChooser& fc)
                              {
                                  const auto result = fc.getResult();
                                  if (result != juce::File())
                                      configEditor.setText (result.getFullPathName(),
                                                            juce::dontSendNotification);
                              });
    };
    clearConfigButton.onClick = [this] { configEditor.clear(); };

    wireChooseDirectory (chooseFixturesButton, fixturesEditor, utf8 ("Choose Source Clips Directory"));
    wireReveal (revealFixturesButton, fixturesEditor);
    fixturesEditor.addListener (this);

    wireChooseDirectory (chooseSessionsButton, sessionsEditor, utf8 ("Choose Sessions directory"));
    wireReveal (revealSessionsButton, sessionsEditor);
    sessionsEditor.addListener (this);

    testPythonCliButton.onClick = [this] { runCliTest(); };
    clearPythonCliButton.onClick = [this]
    {
        pythonCliEditor.clear();
        setCliTestState (CliTestState::untested);
        updateCliHint();
    };

    resetButton.onClick = [this]
    {
        resetRequested = true;
        dataRootEditor.setText (HostPreferences::get().defaultExplorationDataRoot().getFullPathName(),
                                juce::dontSendNotification);
        configEditor.clear();
        updateCliHint();
    };

    updateCliHint();
    if (config.pythonCli != juce::File() && config.pythonCli.existsAsFile())
    {
        // Seed as untested so the user must confirm after opening Settings,
        // unless they already have a path — leave grey until Test.
        lastTestedCliText.clear();
        setCliTestState (CliTestState::untested);
    }

    setSize (560, 460);
}

SettingsPanel::~SettingsPanel()
{
    dataRootEditor.removeListener (this);
    fixturesEditor.removeListener (this);
    sessionsEditor.removeListener (this);
    pythonCliEditor.removeListener (this);
}

void SettingsPanel::textEditorTextChanged (juce::TextEditor& editor)
{
    if (&editor == &pythonCliEditor)
    {
        if (pythonCliEditor.getText().trim() != lastTestedCliText)
            setCliTestState (CliTestState::untested);
    }
    else if (&editor == &dataRootEditor || &editor == &fixturesEditor || &editor == &sessionsEditor)
    {
        updateCliHint();
    }
}

void SettingsPanel::updateCliHint()
{
    const auto inferred = inferAufxTestCli();
    if (inferred != juce::File())
    {
        pythonCliHint.setText (
            utf8 ("Suggested: ") + inferred.getFullPathName()
                + utf8 (" — inferred from exploration / clips / sessions paths"),
            juce::dontSendNotification);
        if (pythonCliEditor.getText().trim().isEmpty())
            pythonCliEditor.setTextToShowWhenEmpty (inferred.getFullPathName(), juce::Colours::grey);
        else
            pythonCliEditor.setTextToShowWhenEmpty ({}, juce::Colours::grey);
    }
    else
    {
        pythonCliHint.setText (
            utf8 ("Optional — used for Generate report and calibrate PNGs. "
                  "Typical path: <aufx-test repo>/.venv/bin/aufx-test"),
            juce::dontSendNotification);
        pythonCliEditor.setTextToShowWhenEmpty (
            utf8 ("e.g. ~/dev/aufx-test/.venv/bin/aufx-test"),
            juce::Colours::grey);
    }
}

void SettingsPanel::setCliTestState (CliTestState state)
{
    cliTestState = state;

    juce::Colour fill;
    switch (state)
    {
        case CliTestState::ok:
            fill = juce::Colour (0xff2f9e5a);
            break;
        case CliTestState::failed:
            fill = juce::Colour (0xffc44747);
            break;
        case CliTestState::untested:
        default:
            fill = juce::Colour (0xff6a6a6a);
            break;
    }

    testPythonCliButton.setColour (juce::TextButton::buttonColourId, fill);
    testPythonCliButton.setColour (juce::TextButton::buttonOnColourId, fill.brighter (0.15f));
    testPythonCliButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    testPythonCliButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    testPythonCliButton.repaint();
}

juce::File SettingsPanel::expandUserPath (const juce::String& raw) const
{
    auto text = raw.trim().unquoted();
    if (text.isEmpty())
        return {};

    if (text.startsWithChar ('~'))
    {
        const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName();
        text = home + text.substring (1);
    }

    if (juce::File::isAbsolutePath (text))
        return juce::File (text);

    return getSelectedDataRoot().getChildFile (text);
}

juce::File SettingsPanel::inferAufxTestCli() const
{
    if (const auto found = searchAufxTestNear (getSelectedDataRoot(), 4); found != juce::File())
        return found;

    const auto fixtures = getSelectedFixturesDir();
    if (fixtures.getFileName() == "fixtures")
        if (const auto found = searchAufxTestNear (fixtures.getParentDirectory(), 3); found != juce::File())
            return found;

    const auto sessions = getSelectedSessionsRoot();
    if (sessions.getFileName() == "sessions")
        if (const auto found = searchAufxTestNear (sessions.getParentDirectory(), 3); found != juce::File())
            return found;

    return {};
}

void SettingsPanel::runCliTest()
{
    auto text = pythonCliEditor.getText().trim();
    if (text.isEmpty())
    {
        const auto inferred = inferAufxTestCli();
        if (inferred != juce::File())
        {
            pythonCliEditor.setText (inferred.getFullPathName(), juce::dontSendNotification);
            text = inferred.getFullPathName();
        }
    }

    const auto cli = expandUserPath (text);
    lastTestedCliText = pythonCliEditor.getText().trim();

    if (cli == juce::File() || ! cli.existsAsFile())
    {
        setCliTestState (CliTestState::failed);
        return;
    }

    juce::ChildProcess process;
    juce::StringArray args;
    args.add (cli.getFullPathName());
    args.add ("--help");

    if (! process.start (args))
    {
        setCliTestState (CliTestState::failed);
        return;
    }

    // Keep the Settings modal responsive; aufx-test --help is normally instant.
    if (! process.waitForProcessToFinish (5000))
    {
        process.kill();
        setCliTestState (CliTestState::failed);
        return;
    }

    const auto output = process.readAllProcessOutput();
    const bool ok = process.getExitCode() == 0
                    && (output.containsIgnoreCase ("aufx-test")
                        || output.containsIgnoreCase ("compare")
                        || output.containsIgnoreCase ("usage"));

    setCliTestState (ok ? CliTestState::ok : CliTestState::failed);
}

void SettingsPanel::resized()
{
    auto area = getLocalBounds().reduced (4);
    layoutPathRow (area, dataRootLabel, dataRootEditor, chooseDataRootButton, revealDataRootButton);
    layoutPathRow (area, configLabel, configEditor, chooseConfigButton, clearConfigButton);
    layoutPathRow (area, fixturesLabel, fixturesEditor, chooseFixturesButton, revealFixturesButton);
    layoutPathRow (area, sessionsLabel, sessionsEditor, chooseSessionsButton, revealSessionsButton);

    pythonCliLabel.setBounds (area.removeFromTop (22));
    auto cliRow = area.removeFromTop (28);
    testPythonCliButton.setBounds (cliRow.removeFromRight (88));
    cliRow.removeFromRight (6);
    clearPythonCliButton.setBounds (cliRow.removeFromRight (72));
    cliRow.removeFromRight (6);
    pythonCliEditor.setBounds (cliRow);
    area.removeFromTop (4);
    pythonCliHint.setBounds (area.removeFromTop (36));
    area.removeFromTop (8);

    defaultPluginLabel.setBounds (area.removeFromTop (22));
    auto pluginRow = area.removeFromTop (28);
    defaultPluginBox.setBounds (pluginRow);
    area.removeFromTop (14);

    resetButton.setBounds (area.removeFromTop (28).removeFromLeft (160));
}

juce::File SettingsPanel::getSelectedDataRoot() const
{
    return expandUserPath (dataRootEditor.getText());
}

juce::File SettingsPanel::getSelectedConfigOverride() const
{
    const auto text = configEditor.getText().trim();
    return text.isEmpty() ? juce::File() : expandUserPath (text);
}

juce::File SettingsPanel::getSelectedFixturesDir() const
{
    return expandUserPath (fixturesEditor.getText());
}

juce::File SettingsPanel::getSelectedSessionsRoot() const
{
    return expandUserPath (sessionsEditor.getText());
}

juce::File SettingsPanel::getSelectedPythonCli() const
{
    return expandUserPath (pythonCliEditor.getText());
}

juce::String SettingsPanel::getSelectedDefaultPluginId (const HostConfig& config) const
{
    const int selected = defaultPluginBox.getSelectedId();
    if (selected <= 1)
        return {};

    const int index = selected - 2;
    if (! juce::isPositiveAndBelow (index, config.plugins.size()))
        return {};

    return config.plugins.getReference (index).id;
}

bool showSettingsDialog (HostConfig& config,
                         juce::Component* centreAround,
                         bool* outFixturesChanged)
{
    if (outFixturesChanged != nullptr)
        *outFixturesChanged = false;

    SettingsPanel panel (config);

    if (HostDialog::runCustomPanelModal (
            utf8 ("Settings"),
            utf8 ("Exploration folder / config override changes apply after relaunch. "
                  "Source Clips, Sessions, aufx-test CLI, and Default plugin save immediately."),
            panel,
            centreAround) != 1)
        return false;

    auto& prefs = HostPreferences::get();
    const auto previousDataRoot = config.projectRoot;

    if (panel.wantsResetToDefaults())
    {
        const auto defaultRoot = prefs.defaultExplorationDataRoot();
        juce::String migrateMessage;
        if (previousDataRoot != juce::File()
            && previousDataRoot.getFullPathName() != defaultRoot.getFullPathName())
        {
            if (! prefs.relocateExplorationData (previousDataRoot, defaultRoot, migrateMessage))
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        utf8 ("Could not move exploration data"),
                                                        migrateMessage);
                return false;
            }
        }

        prefs.clearPrefs();
        return true;
    }

    const auto dataRoot = panel.getSelectedDataRoot();
    if (dataRoot != juce::File())
    {
        if (! dataRoot.createDirectory())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    utf8 ("Settings"),
                                                    utf8 ("Could not create exploration data folder:\n")
                                                        + dataRoot.getFullPathName());
            return false;
        }

        if (previousDataRoot != juce::File()
            && previousDataRoot.getFullPathName() != dataRoot.getFullPathName())
        {
            juce::String migrateMessage;
            if (! prefs.relocateExplorationData (previousDataRoot, dataRoot, migrateMessage))
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        utf8 ("Could not move exploration data"),
                                                        migrateMessage);
                return false;
            }
        }

        prefs.setExplorationDataRootPref (dataRoot.getFullPathName());
    }

    const auto configOverride = panel.getSelectedConfigOverride();
    prefs.setConfigPathPref (configOverride != juce::File() ? configOverride.getFullPathName()
                                                            : juce::String());

    const auto previousFixtures = config.fixturesDir;
    const auto previousSessions = config.sessionsRoot;
    const auto newFixtures = panel.getSelectedFixturesDir();
    const auto newSessions = panel.getSelectedSessionsRoot();
    const auto newPythonCli = panel.getSelectedPythonCli();
    const auto newDefaultPluginId = panel.getSelectedDefaultPluginId (config);

    if (newFixtures != juce::File()
        && newFixtures.getFullPathName() != previousFixtures.getFullPathName())
    {
        juce::String migrateMessage;
        const auto bundled = prefs.bundledFixturesDir();
        const bool previousIsBundled = previousFixtures != juce::File()
                                       && bundled != juce::File()
                                       && previousFixtures.getFullPathName() == bundled.getFullPathName();

        if (directoryHasContent (previousFixtures) && ! previousIsBundled)
        {
            if (! prefs.relocateDirectoryContents (previousFixtures, newFixtures, migrateMessage))
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        utf8 ("Could not move Source Clips"),
                                                        migrateMessage);
                return false;
            }
        }
        else if (bundled != juce::File() && bundled.isDirectory())
        {
            if (! prefs.copyDirectoryContents (bundled, newFixtures, migrateMessage))
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        utf8 ("Could not copy Source Clips"),
                                                        migrateMessage);
                return false;
            }
        }
        else if (! newFixtures.createDirectory())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    utf8 ("Settings"),
                                                    utf8 ("Could not create Source Clips Directory:\n")
                                                        + newFixtures.getFullPathName());
            return false;
        }

        config.fixturesDir = newFixtures;
        if (outFixturesChanged != nullptr)
            *outFixturesChanged = true;
    }

    if (newSessions != juce::File()
        && newSessions.getFullPathName() != previousSessions.getFullPathName())
    {
        juce::String migrateMessage;
        if (directoryHasContent (previousSessions))
        {
            if (! prefs.relocateDirectoryContents (previousSessions, newSessions, migrateMessage))
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        utf8 ("Could not move Sessions"),
                                                        migrateMessage);
                return false;
            }

            juce::String rewriteError;
            if (! SessionSnap::rewritePathsAfterRootMove (previousSessions, newSessions, rewriteError))
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        utf8 ("Sessions moved, but path rewrite failed"),
                                                        rewriteError);
                return false;
            }
        }
        else if (! newSessions.createDirectory())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    utf8 ("Settings"),
                                                    utf8 ("Could not create Sessions directory:\n")
                                                        + newSessions.getFullPathName());
            return false;
        }

        config.sessionsRoot = newSessions;
    }

    config.pythonCli = newPythonCli;
    if (config.pythonCli != juce::File() && ! config.pythonCli.existsAsFile())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                utf8 ("Settings"),
                                                utf8 ("aufx-test CLI path does not exist:\n")
                                                    + config.pythonCli.getFullPathName());
        return false;
    }

    config.defaultPluginId = newDefaultPluginId;

    juce::String saveError;
    if (! config.saveToFile (saveError))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                utf8 ("Settings"),
                                                utf8 ("Failed to write host.config.json:\n") + saveError);
        return false;
    }

    return true;
}
