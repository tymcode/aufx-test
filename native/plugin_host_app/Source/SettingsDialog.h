#pragma once

#include <JuceHeader.h>
#include "HostConfig.h"

class SettingsPanel : public juce::Component,
                      private juce::TextEditor::Listener
{
public:
    explicit SettingsPanel (const HostConfig& config);
    ~SettingsPanel() override;

    void resized() override;

    juce::File getSelectedDataRoot() const;
    juce::File getSelectedConfigOverride() const;
    juce::File getSelectedFixturesDir() const;
    juce::File getSelectedSessionsRoot() const;
    juce::File getSelectedPythonCli() const;
    juce::String getSelectedDefaultPluginId (const HostConfig& config) const;
    bool wantsResetToDefaults() const { return resetRequested; }

private:
    enum class CliTestState
    {
        untested,
        ok,
        failed
    };

    void textEditorTextChanged (juce::TextEditor& editor) override;
    void textEditorReturnKeyPressed (juce::TextEditor&) override {}
    void textEditorEscapeKeyPressed (juce::TextEditor&) override {}
    void textEditorFocusLost (juce::TextEditor&) override {}

    void updateCliHint();
    void setCliTestState (CliTestState state);
    void runCliTest();
    juce::File expandUserPath (const juce::String& text) const;
    juce::File inferAufxTestCli() const;

    juce::Label dataRootLabel;
    juce::TextEditor dataRootEditor;
    juce::TextButton chooseDataRootButton;
    juce::TextButton revealDataRootButton;

    juce::Label configLabel;
    juce::TextEditor configEditor;
    juce::TextButton chooseConfigButton;
    juce::TextButton clearConfigButton;

    juce::Label fixturesLabel;
    juce::TextEditor fixturesEditor;
    juce::TextButton chooseFixturesButton;
    juce::TextButton revealFixturesButton;

    juce::Label sessionsLabel;
    juce::TextEditor sessionsEditor;
    juce::TextButton chooseSessionsButton;
    juce::TextButton revealSessionsButton;

    juce::Label pythonCliLabel;
    juce::TextEditor pythonCliEditor;
    juce::TextButton testPythonCliButton;
    juce::TextButton clearPythonCliButton;
    juce::Label pythonCliHint;
    CliTestState cliTestState { CliTestState::untested };
    juce::String lastTestedCliText;

    juce::Label defaultPluginLabel;
    juce::ComboBox defaultPluginBox;

    juce::TextButton resetButton;
    bool resetRequested { false };
};

/**
 * Shows Settings. On success, updates exploration prefs and host.config.json
 * path fields (fixtures, sessions, aufx-test CLI, default_plugin). Folder/config
 * override still require relaunch. Returns true if the user saved.
 */
bool showSettingsDialog (HostConfig& config,
                         juce::Component* centreAround,
                         bool* outFixturesChanged = nullptr);
