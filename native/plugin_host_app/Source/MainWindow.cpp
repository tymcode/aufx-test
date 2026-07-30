/**
 * MainWindow.cpp — window chrome, native menu bar, Lights Out, and global shortcuts.
 */
#include "MainWindow.h"
#include "MainContent.h"

#if JUCE_MAC
 #include "LightsOutManager_mac.h"
#endif

MainWindow::MainWindow (HostConfig hostConfig)
    : DocumentWindow ("AU Effects Explorer",
                      juce::Desktop::getInstance().getDefaultLookAndFeel()
                          .findColour (juce::ResizableWindow::backgroundColourId),
                      DocumentWindow::allButtons),
      config (std::move (hostConfig))
{
    engine = std::make_unique<PluginAudioEngine>();
    content = std::make_unique<MainContent> (*engine, config, knownPlugins,
                                             [this] (bool shouldEnable) { setLightsOutEnabled (shouldEnable); });
    auto* mainContent = content.get();
    setContentOwned (content.release(), true);
    setUsingNativeTitleBar (true);
    setResizable (true, true);
    centreWithSize (1100, 780);

#if JUCE_MAC
    appleMenu = std::make_unique<juce::PopupMenu>();
    appleMenu->addItem (menuAbout, "About AU Effects Explorer");
    appleMenu->addSeparator();
    appleMenu->addItem (menuSettings, "Settings...");
    juce::MenuBarModel::setMacMainMenu (this, appleMenu.get());
#else
    setMenuBar (this);
#endif

    setVisible (true);
    lightsOut.setHostWindow (this);
    addKeyListener (this);

#if JUCE_MAC
    juce::Timer::callAfterDelay (0, [safe = juce::Component::SafePointer<MainWindow> (this)]
    {
        if (safe != nullptr)
            safe->syncNativeMenuShortcuts();
    });
#endif

    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainContent> (mainContent)]
                                     {
                                         if (safe != nullptr)
                                             safe->showPluginEditor();
                                     });
}

MainWindow::~MainWindow()
{
    lightsOut.release();

#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (nullptr);
#else
    setMenuBar (nullptr);
#endif

    clearContentComponent();
    engine.reset();
}

void MainWindow::closeButtonPressed()
{
    lightsOut.release();
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

void MainWindow::toggleLightsOut()
{
    lightsOut.setHostWindow (this);
    lightsOut.setEnabled (! lightsOut.isEnabled());
    menuItemsChanged();
    syncNativeMenuShortcuts();
}

void MainWindow::toggleLightsOutFromMenu()
{
    juce::Timer::callAfterDelay (150, [safe = juce::Component::SafePointer<MainWindow> (this)]
                                  {
                                      if (safe != nullptr)
                                          safe->toggleLightsOut();
                                  });
}

void MainWindow::toggleHardwareMode()
{
    if (engine == nullptr || ! engine->hasHardwareLoopConfigured())
        return;

    engine->setHardwareMode (! engine->isHardwareMode());
    refreshHardwareUi();
    menuItemsChanged();
    syncNativeMenuShortcuts();
}

void MainWindow::toggleHardwareModeFromMenu()
{
    juce::Timer::callAfterDelay (150, [safe = juce::Component::SafePointer<MainWindow> (this)]
                                  {
                                      if (safe != nullptr)
                                          safe->toggleHardwareMode();
                                  });
}

void MainWindow::openHardwareAudioSetup()
{
    if (auto* mainContent = dynamic_cast<MainContent*> (getContentComponent()))
        mainContent->openHardwareAudioSetup();
}

void MainWindow::openMidiSetup()
{
    if (auto* mainContent = dynamic_cast<MainContent*> (getContentComponent()))
        mainContent->openMidiSetup();
}

void MainWindow::refreshHardwareUi()
{
    if (auto* mainContent = dynamic_cast<MainContent*> (getContentComponent()))
        mainContent->refreshHardwareModeUi();
}

void MainWindow::setLightsOutEnabled (bool shouldEnable)
{
    lightsOut.setHostWindow (this);
    if (lightsOut.isEnabled() == shouldEnable)
        return;

    lightsOut.setEnabled (shouldEnable);
    menuItemsChanged();
    syncNativeMenuShortcuts();
}

void MainWindow::syncNativeMenuShortcuts()
{
#if JUCE_MAC
    const bool lightsTicked = lightsOut.isEnabled();
    const bool hwTicked = engine != nullptr && engine->isHardwareMode();
    juce::Timer::callAfterDelay (0, [lightsTicked, hwTicked]
    {
        lightsOutSyncMenuItem (lightsTicked);
        nativeSyncMenuItem ("Use Hardware", "u", true, false, hwTicked, true);
    });
#endif
}

bool MainWindow::keyPressed (const juce::KeyPress& key, juce::Component* originatingComponent)
{
    juce::ignoreUnused (originatingComponent);

    if (key == juce::KeyPress ('l', juce::ModifierKeys::commandModifier, 0))
    {
        toggleLightsOut();
        return true;
    }

    if (key == juce::KeyPress ('u', juce::ModifierKeys::commandModifier, 0))
    {
        toggleHardwareMode();
        return true;
    }

    return false;
}

juce::StringArray MainWindow::getMenuBarNames()
{
#if JUCE_MAC
    return { "Session", "Plugins" };
#else
    return { "AU Effects Explorer", "Session", "Plugins" };
#endif
}

juce::PopupMenu MainWindow::getMenuForIndex (int topLevelMenuIndex, const juce::String& menuName)
{
    juce::ignoreUnused (menuName);
    juce::PopupMenu menu;

    auto addSessionItems = [this, &menu]()
    {
        menu.addItem (menuCaptureTestCase, "Capture Test Case...");
        menu.addSeparator();
        menu.addItem (menuHardwareAudioSetup, "Hardware Audio Setup...");
        menu.addItem (menuMidiSetup, "MIDI Setup...");
        {
            juce::PopupMenu::Item item;
            item.itemID = menuUseHardware;
            item.text = "Use Hardware";
            item.isTicked = engine != nullptr && engine->isHardwareMode();
            item.isEnabled = engine != nullptr && engine->hasHardwareLoopConfigured();
            item.shortcutKeyDescription = "Cmd+U";
            menu.addItem (std::move (item));
        }
        menu.addSeparator();
        {
            juce::PopupMenu::Item item;
            item.itemID = menuLightsOut;
            item.text = "Lights Out";
            item.isTicked = lightsOut.isEnabled();
            item.shortcutKeyDescription = "Cmd+L";
            menu.addItem (std::move (item));
        }
    };

#if JUCE_MAC
    if (topLevelMenuIndex == 0)
        addSessionItems();
    else if (topLevelMenuIndex == 1)
    {
        menu.addItem (menuAddPlugin, "Add Plugin...");
        menu.addItem (menuRescanPlugins, "Rescan Audio Units...");
        menu.addSeparator();
        menu.addItem (menuRescanSourceClips, "Rescan Source Clips");
    }
#else
    if (topLevelMenuIndex == 0)
    {
        menu.addItem (menuAbout, "About AU Effects Explorer");
        menu.addSeparator();
        menu.addItem (menuSettings, "Settings...");
    }
    else if (topLevelMenuIndex == 1)
        addSessionItems();
    else if (topLevelMenuIndex == 2)
    {
        menu.addItem (menuAddPlugin, "Add Plugin...");
        menu.addItem (menuRescanPlugins, "Rescan Audio Units...");
        menu.addSeparator();
        menu.addItem (menuRescanSourceClips, "Rescan Source Clips");
    }
#endif

    return menu;
}

void MainWindow::menuItemSelected (int menuItemID, int topLevelMenuIndex)
{
    juce::ignoreUnused (topLevelMenuIndex);

    juce::MessageManager::callAsync ([safeWindow = juce::Component::SafePointer<MainWindow> (this),
                                      menuItemID]
                                     {
                                         auto* window = safeWindow.getComponent();
                                         if (window == nullptr)
                                             return;

                                         auto* mainContent = dynamic_cast<MainContent*> (window->getContentComponent());
                                         if (mainContent == nullptr)
                                             return;

                                         switch (menuItemID)
                                         {
                                             case menuAbout:               mainContent->openAbout(); break;
                                             case menuSettings:            mainContent->openSettings(); break;
                                             case menuCaptureTestCase:     mainContent->openCaptureTestCase(); break;
                                             case menuLightsOut:           window->toggleLightsOutFromMenu(); break;
                                             case menuHardwareAudioSetup:  window->openHardwareAudioSetup(); break;
                                             case menuMidiSetup:           window->openMidiSetup(); break;
                                             case menuUseHardware:         window->toggleHardwareModeFromMenu(); break;
                                             case menuAddPlugin:           mainContent->openAddPlugin(); break;
                                             case menuRescanPlugins:       mainContent->rescanPlugins(); break;
                                             case menuRescanSourceClips:   mainContent->rescanSourceClips(); break;
                                             default: break;
                                         }
                                     });
}
