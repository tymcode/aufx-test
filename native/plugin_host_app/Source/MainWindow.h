#pragma once

#include <JuceHeader.h>
#include "HostConfig.h"
#include "PluginAudioEngine.h"

class MainWindow : public juce::DocumentWindow,
                   public juce::MenuBarModel
{
public:
    explicit MainWindow (HostConfig config);
    ~MainWindow() override;

    void closeButtonPressed() override;

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

private:
    class MainContent;

    enum MenuIds
    {
        menuAbout = 1,
        menuSettings,
        menuAddPlugin,
        menuRescanPlugins
    };

    HostConfig config;
    std::unique_ptr<PluginAudioEngine> engine;
    std::unique_ptr<MainContent> content;
    juce::KnownPluginList knownPlugins;
#if JUCE_MAC
    std::unique_ptr<juce::PopupMenu> appleMenu;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};
