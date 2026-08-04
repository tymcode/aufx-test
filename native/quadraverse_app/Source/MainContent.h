#pragma once

#include <JuceHeader.h>
#include "Utf8.h"
#include "PluginAudioEngine.h"
#include "HostConfig.h"
#include "LevelMetersWindow.h"
#include "domain/PatchContextManager.h"
#include "project/ProjectFile.h"
#include "ui/PatchEditorPanel.h"
#include "ui/PluginHostWindow.h"

class QuadraverseMainContent : public juce::Component,
                               private juce::Timer
{
public:
    QuadraverseMainContent (HostConfig config);
    ~QuadraverseMainContent() override;

    void resized() override;

    void openMidiSetup();
    void openHardwareAudioSetup();
    void openSettings();
    void openLevelMeters();
    void openPluginHost();
    void toggleHardwareMode();
    void openComparisonReport();
    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();
    bool isHardwareMode() const;

    PluginAudioEngine& getEngine() { return engine; }

private:
    void timerCallback() override;
    void refreshChrome();
    void sendToTarget();
    void sendActivePatchToHardware();
    void saveToDisk();
    void loadIntoContext();
    void importSsx();
    void loadQdv1Preset();
    void onParamChanged (const qverse::ParamAddress& addr, int value);
    void flushLiveEdit();
    void ensurePluginLoaded();
    void onActiveContextChanged();
    juce::File currentPatchDir() const;

    HostConfig config;
    PluginAudioEngine engine;
    qverse::PatchContextManager contexts;
    qverse::ProjectState project;
    juce::File projectFile;

    juce::TextButton sendButton { utf8 ("Send Preset") };
    juce::TextButton saveButton { utf8 ("Save to Disk") };
    juce::TextButton loadButton { utf8 ("Load…") };
    juce::TextButton dupButton { utf8 ("Duplicate") };
    juce::TextButton dropButton { utf8 ("Drop") };
    juce::TextButton ssxButton { utf8 ("Import SSX…") };
    juce::TextButton qdv1Button { utf8 ("QDV-1 Preset…") };
    juce::ComboBox contextBox;
    juce::ToggleButton liveEditToggle { utf8 ("Edit live to device") };
    juce::Label statusLabel;

    qverse::PatchEditorPanel editor;
    std::unique_ptr<LevelMetersWindow> metersWindow;
    std::unique_ptr<qverse::PluginHostWindow> hostWindow;

    qverse::ParamAddress pendingLiveAddr {};
    int pendingLiveValue = 0;
    bool hasPendingLive = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (QuadraverseMainContent)
};
