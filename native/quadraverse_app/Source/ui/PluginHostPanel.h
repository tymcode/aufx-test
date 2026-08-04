#pragma once

#include <JuceHeader.h>
#include "HostChromeControls.h"
#include "HostConfig.h"
#include "HardwareVuMeters.h"
#include "PluginAudioEngine.h"
#include "SourceClipLibrary.h"

namespace qverse
{

/**
 * Explorer-like host chrome for Quadraverse: plugin picker, source clips,
 * transport/loop/BPM, and a body that shows the plugin editor (software) or
 * Send/Return meters (hardware).
 */
class PluginHostPanel : public juce::Component,
                        private juce::Timer,
                        private juce::ComboBox::Listener,
                        private juce::Button::Listener,
                        private juce::TextEditor::Listener
{
public:
    PluginHostPanel (PluginAudioEngine& engine, HostConfig& config);
    ~PluginHostPanel() override;

    void resized() override;

    void paint (juce::Graphics& g) override;

    /** Swap body between plugin editor and hardware meters. */
    void refreshHardwareModeUi();

    /**
     * After the DocumentWindow is on-screen, create the Cocoa/WebView editor
     * (same rule as AUFX Explorer).
     */
    void showEditorWhenReady();

    /** Destroy editor, reload already done by engine — rebuild UI for SW mode. */
    void recreateEditorAfterPluginReload();

    /** Tear down owned editor before engine.reloadCurrentPlugin. */
    void destroyPluginEditor();

    /** Toggle Begin/Play for the loaded source clip. */
    void togglePlayback();

private:
    void timerCallback() override;
    void comboBoxChanged (juce::ComboBox* box) override;
    void buttonClicked (juce::Button* button) override;
    void textEditorReturnKeyPressed (juce::TextEditor&) override;
    void textEditorFocusLost (juce::TextEditor&) override;

    void populatePlugins();
    void populateFixtures();
    void switchToPlugin (int index);
    void selectFixture (int comboId);
    void startPlayback();
    void stopPlayback();
    void applyBpmFromEditor();
    void recreatePluginEditor();
    void layoutEditor();
    void showHardwareMetersBody();
    void showPluginEditorBody();
    void setStatus (const juce::String& text, bool isError = false);

    PluginAudioEngine& engine;
    HostConfig& config;
    int currentPluginIndex { 0 };

    juce::Label pluginLabel { {}, "Plugin" };
    PluginPickerField pluginField;
    juce::Label fixtureLabel { {}, "Source Clip" };
    juce::ComboBox fixtureBox;
    TransportButton transportButton;
    LoopToggleButton loopToggle;
    juce::ToggleButton hostClockToggle { "Host Clock" };
    juce::TextEditor bpmEditor;
    juce::Label bpmLabel { {}, "BPM" };
    juce::TextButton bypassButton { "Bypass" };
    juce::Label positionLabel;
    juce::Label statusLabel;

    juce::Viewport editorViewport;
    juce::Component editorPlaceholder;
    std::unique_ptr<HardwareLoopMeterPanel> hardwareMeterPanel;
    juce::AudioProcessorEditor* pluginEditor { nullptr };

    SourceClipLibrary sourceClips;
    bool editorCreatePending { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginHostPanel)
};

} // namespace qverse
