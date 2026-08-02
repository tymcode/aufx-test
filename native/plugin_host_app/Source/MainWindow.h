#pragma once

#include <JuceHeader.h>
#include "HostConfig.h"
#include "LightsOutManager.h"
#include "PluginAudioEngine.h"

class MainContent;
class LevelMetersWindow;

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
    void toggleLightsOutFromMenu();
    void toggleHardwareMode();
    void toggleHardwareModeFromMenu();
    void toggleLevelMeters();
    void openHardwareAudioSetup();
    void openMidiSetup();
    void refreshHardwareUi();
    void setLightsOutEnabled (bool shouldEnable);

private:
    using juce::Component::keyPressed;

    bool keyPressed (const juce::KeyPress& key, juce::Component* originatingComponent) override;
    /** Shared by the main window and Level Meters (space + command shortcuts). */
    bool handleGlobalKeyPress (const juce::KeyPress& key);
    void syncNativeMenuShortcuts();
    /** Keep Level Meters above Lights Out overlays when that mode is on. */
    void syncLevelMetersForLightsOut();

    enum MenuIds
    {
        menuAbout = 1,
        menuSettings,
        menuAddPlugin,
        menuAudioUnitSettings,
        menuRescanPlugins,
        menuRescanSourceClips,
        menuInstallSourceClips,
        menuCaptureTestCase,
        menuRestoreTestcaseState,
        menuLightsOut,
        menuHardwareAudioSetup,
        menuMidiSetup,
        menuUseHardware,
        menuLevelMeters
    };

    HostConfig config;
    std::unique_ptr<PluginAudioEngine> engine;
    std::unique_ptr<MainContent> content;
    std::unique_ptr<LevelMetersWindow> levelMetersWindow;
    juce::KnownPluginList knownPlugins;
    LightsOutManager lightsOut;
#if JUCE_MAC
    std::unique_ptr<juce::PopupMenu> appleMenu;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};
