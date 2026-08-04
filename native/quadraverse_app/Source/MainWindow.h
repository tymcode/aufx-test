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
        menuMidiSetup,
        menuHardwareAudio,
        menuUseHardware,
        menuPluginHost,
        menuLevelMeters,
        menuCompareReport,
        menuQuit
    };

    HostConfig config;

    QuadraverseMainContent* content() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (QuadraverseMainWindow)
};
