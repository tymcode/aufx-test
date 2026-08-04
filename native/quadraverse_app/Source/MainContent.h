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
                               private juce::Timer,
                               private juce::KeyListener
{
public:
    QuadraverseMainContent (HostConfig config);
    ~QuadraverseMainContent() override;

    void resized() override;
    void parentHierarchyChanged() override;
    bool keyPressed (const juce::KeyPress& key) override;

    void openMidiSetup();
    void openHardwareAudioSetup();
    void openSettings();
    void openLevelMeters();
    void openTargetView();
    void toggleHardwareMode();
    void openComparisonReport();
    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();
    bool isHardwareMode() const;
    bool isTargetViewOpen() const;
    bool isLevelMetersOpen() const;

    /** Toggle Target View Begin/Play (opens Target View if needed). */
    void toggleTargetPlayback();
    static bool isEditableFieldFocused();

    void sendPatch();
    void newPatchContext();
    void duplicatePatchContext();
    void renamePatchContext();
    void deletePatchContext();
    bool canDeletePatchContext() const;

    void loadPatchFromDevice();
    void loadPatchFromPlugin();
    void loadPatchFromPresetFile();
    void loadPatchFromSysexDump();
    void savePatchAsPreset();
    void savePatchAsSysex();

    void importSysexBank();
    void saveBulkDump();
    void convertSsx();
    void saveBankAsSysex();
    void saveBankAsPresets();

    juce::String configuredDeviceName() const;

    PluginAudioEngine& getEngine() { return engine; }

private:
    using juce::Component::keyPressed;
    bool keyPressed (const juce::KeyPress& key, juce::Component*) override;

    void timerCallback() override;
    void refreshChrome();
    void sendActivePatchToHardware();
    void onParamChanged (const qverse::ParamAddress& addr, int value);
    void flushLiveEdit();
    void ensurePluginLoaded();
    void onActiveContextChanged();
    juce::File currentLibraryDir() const;
    juce::String sanitizedStem (const juce::String& name) const;
    void importProgramsAsContexts (std::vector<qverse::QuadraverbProgram> programs,
                                   const juce::File& source,
                                   bool batchCompareOff);
    void importSysexFile (const juce::File& file, bool alwaysShowPicker);
    bool handleSpacePlayback();
    void applyConfiguredMidiInputs();

    HostConfig config;
    PluginAudioEngine engine;
    qverse::PatchContextManager contexts;
    qverse::ProjectState project;
    juce::File projectFile;

    juce::TextButton sendButton { utf8 ("Send Patch") };
    juce::ComboBox contextBox;
    juce::ToggleButton compareToggle { utf8 ("Compare") };
    juce::ToggleButton liveEditToggle { utf8 ("Edit live to device") };
    juce::Label statusLabel;

    qverse::PatchEditorPanel editor;
    std::unique_ptr<LevelMetersWindow> metersWindow;
    std::unique_ptr<qverse::PluginHostWindow> hostWindow;
    juce::Component* keyListenerOwner { nullptr };

    qverse::ParamAddress pendingLiveAddr {};
    int pendingLiveValue = 0;
    bool hasPendingLive = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (QuadraverseMainContent)
};
