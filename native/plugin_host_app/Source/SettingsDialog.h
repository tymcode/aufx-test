#pragma once

#include <JuceHeader.h>
#include "HostConfig.h"

class SettingsPanel : public juce::Component,
                      private juce::ListBoxModel
{
public:
    explicit SettingsPanel (const HostConfig& config, juce::KnownPluginList* knownPlugins);

    void resized() override;

    juce::File getSelectedDataRoot() const;
    juce::File getSelectedConfigOverride() const;
    bool getAllowInstrumentAudioInput() const;
    int getPluginScanTimeoutMs() const;
    bool wantsResetToDefaults() const { return resetRequested; }
    bool didModifyPluginCache() const { return pluginCacheModified; }

private:
    int getNumRows() override;
    void paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void selectedRowsChanged (int lastRowSelected) override;

    void reloadSkippedList();
    void updateRetryEnabled();
    void retrySelectedSkippedPlugin();

    juce::File dataRoot;
    juce::KnownPluginList* knownPlugins { nullptr };
    juce::StringArray skippedIds;

    juce::Label dataRootLabel;
    juce::TextEditor dataRootEditor;
    juce::TextButton chooseDataRootButton { "Choose..." };
    juce::TextButton revealDataRootButton { "Reveal" };
    juce::Label configLabel;
    juce::TextEditor configEditor;
    juce::TextButton chooseConfigButton { "Choose..." };
    juce::TextButton clearConfigButton { "Clear" };
    juce::ToggleButton allowInstrumentInputToggle;
    juce::Label scanTimeoutLabel;
    juce::ComboBox scanTimeoutBox;
    juce::Label skippedHeading;
    juce::ListBox skippedList { "skipped", this };
    juce::TextButton retrySkippedButton { "Retry selected" };
    juce::TextButton resetButton { "Reset to defaults" };
    bool resetRequested { false };
    bool pluginCacheModified { false };
};

/**
 * Shows Settings. When knownPlugins is non-null, skipped-plugin retries update
 * that list (and the on-disk cache) immediately.
 */
bool showSettingsDialog (HostConfig& config,
                         juce::KnownPluginList* knownPlugins,
                         juce::Component* centreAround);
