#pragma once

#include <JuceHeader.h>
#include <functional>
#include "HostConfig.h"
#include "MidiEndpointInfo.h"
#include "PluginAudioEngine.h"

class SysexDeviceModule;

const SysexDeviceModule* resolveSelectedSysexModule (const MidiEndpointInfo& info);

class PresetHardwareState
{
public:
    using StatusFn = std::function<void (const juce::String&, bool)>;
    using GetPluginIndexFn = std::function<int()>;
    using GetPluginEntryFn = std::function<const HostPluginEntry&()>;
    using GetPluginEditorFn = std::function<juce::AudioProcessorEditor*()>;
    using VoidFn = std::function<void()>;

    PresetHardwareState (PluginAudioEngine& audioEngine,
                         HostConfig& hostConfig,
                         juce::Label& presetLabelIn,
                         juce::ComboBox& presetBoxIn,
                         juce::TextButton& loadPresetButtonIn,
                         juce::TextButton& savePresetButtonIn,
                         juce::TextEditor& savePresetNameEditorIn,
                         juce::Label& savePresetNameLabelIn,
                         juce::TextButton& bypassButtonIn,
                         StatusFn statusFn,
                         GetPluginIndexFn getPluginIndex,
                         GetPluginEntryFn getPluginEntry,
                         GetPluginEditorFn getPluginEditor,
                         VoidFn onShowHardwareMeters,
                         VoidFn onShowPluginEditorArea,
                         juce::Component* dialogParent);

    void refreshHardwareModeUi();
    void populatePresets();
    void populateHardwareStates();
    void loadSelectedPreset();
    /** Load any .aupreset (library or snapshot artifact) into the plugin. */
    bool loadPresetFile (const juce::File& presetFile, juce::String& error);
    void savePresetFromEditor();
    bool loadDefaultOrFirstPreset();
    void selectPresetInDropdown (const juce::File& presetFile);
    juce::File getSelectedPresetFile() const;
    void importDroppedAupresets (const juce::StringArray& files);
    void sendSelectedHardwareState();
    /** Send any .syx dump to the configured MIDI out (Load Testcase / HW restore). */
    bool sendHardwareStateFile (const juce::File& sysexFile, juce::String& error);
    void handleLoadButton();
    void onPresetBoxChanged();

    bool isValidPluginSelected() const;
    bool canAcceptPresetDrag() const;

    const juce::Array<juce::File>& getPresetFiles() const { return presetFiles; }
    const juce::Array<juce::File>& getHardwareStateFiles() const { return hardwareStateFiles; }

private:
    static juce::String stripAupresetExtension (juce::String name);
    static juce::String presetDisplayPath (const juce::File& file, const juce::File& presetsDir);
    static void collectAupresetFiles (const juce::File& file, juce::Array<juce::File>& out);

    juce::File presetFileForName (const juce::String& presetName) const;
    void commitPresetSave (const juce::File& dest, bool replacing);

    PluginAudioEngine& engine;
    HostConfig& config;
    juce::Label& presetLabel;
    juce::ComboBox& presetBox;
    juce::TextButton& loadPresetButton;
    juce::TextButton& savePresetButton;
    juce::TextEditor& savePresetNameEditor;
    juce::Label& savePresetNameLabel;
    juce::TextButton& bypassButton;
    StatusFn setStatus;
    GetPluginIndexFn getCurrentPluginIndex;
    GetPluginEntryFn getCurrentPlugin;
    GetPluginEditorFn getPluginEditor;
    VoidFn onShowHardwareMeters;
    VoidFn onShowPluginEditorArea;
    juce::Component* dialogParent { nullptr };

    juce::Array<juce::File> presetFiles;
    juce::Array<juce::File> hardwareStateFiles;
    juce::ScopedMessageBox replacePresetDialog;
};
