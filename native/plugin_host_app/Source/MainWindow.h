#pragma once

#include <JuceHeader.h>
#include "HostConfig.h"
#include "LightsOutManager.h"
#include "PluginAudioEngine.h"

class MainWindow : public juce::DocumentWindow,
                   public juce::MenuBarModel,
                   private juce::KeyListener
{
public:
    explicit MainWindow (HostConfig config);
    ~MainWindow() override;

    void closeButtonPressed() override;

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

    void toggleLightsOut();

    /** Defer until AppKit menu tracking finishes — immediate toggle aborts the process. */
    void toggleLightsOutFromMenu();

private:
    using juce::Component::keyPressed;

    bool keyPressed (const juce::KeyPress& key, juce::Component* originatingComponent) override;

    class MainContent;

    enum MenuIds
    {
        menuAbout = 1,
        menuSettings,
        menuAddPlugin,
        menuRescanPlugins,
        menuCaptureTestCase,
        menuLightsOut
    };

    HostConfig config;
    std::unique_ptr<PluginAudioEngine> engine;
    std::unique_ptr<MainContent> content;
    juce::KnownPluginList knownPlugins;
    LightsOutManager lightsOut;
#if JUCE_MAC
    std::unique_ptr<juce::PopupMenu> appleMenu;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};
