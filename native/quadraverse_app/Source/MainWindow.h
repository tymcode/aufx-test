#pragma once

#include <JuceHeader.h>
#include "HostConfig.h"
#include "MainContent.h"

class QuadraverseMainWindow : public juce::DocumentWindow,
                              public juce::MenuBarModel
{
public:
    explicit QuadraverseMainWindow (HostConfig config);
    ~QuadraverseMainWindow() override;

    void closeButtonPressed() override;

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int topLevelIndex, const juce::String& name) override;
    void menuItemSelected (int menuItemID, int topLevelIndex) override;

private:
    enum
    {
        menuAbout = 1,
        menuSettings,
        menuNewProject,
        menuOpenProject,
        menuSaveProject,
        menuSaveProjectAs,
        menuImportSysexBank,
        menuSaveBulkDump,
        menuConvertSsx,
        menuSaveBankAsSysex,
        menuSaveBankAsPresets,
        menuMidiSetup,
        menuHardwareAudio,
        menuUseHardware,
        menuTargetView,
        menuLevelMeters,
        menuCompareReport,
        menuQuit,

        menuSendPatch = 3000,
        menuNewPatchContext,
        menuDuplicatePatchContext,
        menuRenamePatchContext,
        menuDeletePatchContext,
        menuLoadFromDevice,
        menuLoadFromPlugin,
        menuLoadFromPresetFile,
        menuLoadFromSysexDump,
        menuSavePatchAsPreset,
        menuSavePatchAsSysex
    };

    struct GlobalKeys : public juce::KeyListener
    {
        explicit GlobalKeys (QuadraverseMainWindow& o) : owner (o) {}
        bool keyPressed (const juce::KeyPress& key, juce::Component*) override;
        QuadraverseMainWindow& owner;
    };

    void syncNativeMenuShortcuts();

    HostConfig config;
    GlobalKeys globalKeys { *this };

    QuadraverseMainContent* content() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (QuadraverseMainWindow)
};
