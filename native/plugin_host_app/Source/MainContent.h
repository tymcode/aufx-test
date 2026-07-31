#pragma once

#include <JuceHeader.h>
#include <functional>
#include "HostChromeControls.h"
#include "HostConfig.h"
#include "HardwareVuMeters.h"
#include "PluginAudioEngine.h"
#include "PresetHardwareState.h"
#include "SourceClipLibrary.h"
#include "TestCaseCapture.h"
#include "TestCaseLoader.h"

class MainContent : public juce::Component,
                    public juce::FileDragAndDropTarget,
                    private juce::Button::Listener,
                    private juce::ComboBox::Listener,
                    private juce::TextEditor::Listener,
                    private juce::KeyListener,
                    private juce::Timer
{
public:
    MainContent (PluginAudioEngine& audioEngine,
                 HostConfig& hostConfig,
                 juce::KnownPluginList& knownList,
                 std::function<void (bool)> setLightsOutFn);
    ~MainContent() override;

    void openAbout();
    void openHardwareAudioSetup();
    void openMidiSetup();
    void refreshHardwareModeUi();
    void showHardwareMeters();
    void showPluginEditorArea();
    void openSettings();
    void openAddPlugin();
    void rescanPlugins();
    void rescanSourceClips();
    void openCaptureTestCase();
    void togglePlayback();
    /** True when a text field (or other text input target) has keyboard focus. */
    static bool isEditableFieldFocused();
    /** Apply a session snapshot's clip/preset/sysex into the live host (Load Testcase). */
    bool loadTestCase (const SessionSnapshotRef& snapshot, juce::String& error);
    /** Call after the host window is on-screen so AU Cocoa UIs can attach to an NSWindow. */
    void showPluginEditor();

    void parentHierarchyChanged() override;
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void configureButton (juce::TextButton& button, const juce::String& text);
    void setStatus (const juce::String& text, bool isError = false);
    const HostPluginEntry& currentPlugin() const;
    void ensurePluginCache (bool forceRescan);
    void populatePluginBox();
    void confirmRemovePlugin (int index);
    void removePluginAt (int index);
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;
    void switchToPlugin (int pluginIndex);
    void destroyPluginEditor();
    void recreatePluginEditor();
    void populateFixtures();
    void populateMidiInputs();
    void timerCallback() override;
    void applyBpmFromEditor();
    void loadPluginWithoutEditor();
    void layoutEditor();
    void selectFixture (int comboId);
    /** Load an arbitrary audio file as the current source clip (external/"Loaded"). */
    bool loadExternalSourceClip (const juce::File& file, juce::String& error);
    void startPlayback();
    void stopPlayback();
    void resetPluginToDefaults();
    using juce::Component::keyPressed;
    bool keyPressed (const juce::KeyPress& key, juce::Component*) override;
    void buttonClicked (juce::Button* button) override;
    void comboBoxChanged (juce::ComboBox* box) override;
    void textEditorTextChanged (juce::TextEditor&) override;
    void textEditorReturnKeyPressed (juce::TextEditor& editor) override;
    void textEditorFocusLost (juce::TextEditor& editor) override;
    PluginAudioEngine& engine;
    HostConfig& config;
    juce::KnownPluginList& knownPlugins;
    int currentPluginIndex { 0 };
    juce::Label pluginLabel;
    PluginPickerField pluginField;
    juce::TextButton resetButton;
    StatusDisplay statusDisplay;
    juce::Label presetLabel;
    juce::Label fixtureLabel;
    juce::ComboBox presetBox;
    juce::ComboBox fixtureBox;
    juce::TextButton loadPresetButton;
    juce::TextButton savePresetButton;
    juce::Label savePresetNameLabel;
    juce::TextEditor savePresetNameEditor;
    TransportButton transportButton;
    juce::TextButton bypassButton;
    LoopToggleButton loopToggle;
    juce::Label sendLabel;
    SendSlider sendSlider;
    juce::Label mixLabel;
    MixSlider mixSlider;
    juce::Label midiLabel;
    MidiSourceField midiField;
    MidiActivityLed midiLed;
    juce::ToggleButton hostClockToggle;
    juce::TextEditor bpmEditor;
    juce::Label bpmLabel;
    ClickToggleButton clickToggle;
    juce::Array<juce::MidiDeviceInfo> midiDevices;
    double midiLedLitUntil { 0.0 };
    double clockLedLitUntil { 0.0 };
    juce::Array<juce::Rectangle<float>> groupSeparators;
    juce::Rectangle<int> controlStripDivider;
    juce::Viewport editorViewport;
    juce::Component editorPlaceholder;
    std::unique_ptr<HardwareLoopMeterPanel> hardwareMeterPanel;
    juce::AudioProcessorEditor* pluginEditor { nullptr };
    SourceClipLibrary sourceClips;
    juce::Component* keyListenerOwner { nullptr };
    PresetHardwareState presetHardwareState;
    TestCaseCapture testCaseCapture;
    TestCaseLoader testCaseLoader;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainContent)
};
