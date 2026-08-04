#include "MainWindow.h"
#include "Utf8.h"

QuadraverseMainWindow::QuadraverseMainWindow (HostConfig cfg)
    : DocumentWindow (utf8 ("Quadraverse"),
                      juce::Desktop::getInstance().getDefaultLookAndFeel()
                          .findColour (juce::ResizableWindow::backgroundColourId),
                      DocumentWindow::allButtons),
      config (std::move (cfg))
{
    setUsingNativeTitleBar (true);
    auto owned = std::make_unique<QuadraverseMainContent> (config);
    setContentOwned (owned.release(), true);
    setResizable (true, true);
    centreWithSize (1100, 760);
    setVisible (true);

#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (this);
#else
    setMenuBar (this);
#endif
}

QuadraverseMainWindow::~QuadraverseMainWindow()
{
#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (nullptr);
#else
    setMenuBar (nullptr);
#endif
    // DocumentWindow owns the content via setContentOwned — do not also
    // unique_ptr::reset it (that double-frees and crashes on quit).
    clearContentComponent();
}

QuadraverseMainContent* QuadraverseMainWindow::content() const
{
    return dynamic_cast<QuadraverseMainContent*> (getContentComponent());
}

void QuadraverseMainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

juce::StringArray QuadraverseMainWindow::getMenuBarNames()
{
#if JUCE_MAC
    return { utf8 ("File"), utf8 ("Session"), utf8 ("View") };
#else
    return { utf8 ("Quadraverse"), utf8 ("File"), utf8 ("Session"), utf8 ("View") };
#endif
}

juce::PopupMenu QuadraverseMainWindow::getMenuForIndex (int topLevelIndex, const juce::String&)
{
    juce::PopupMenu menu;
#if JUCE_MAC
    const int file = 0, session = 1, view = 2;
#else
    if (topLevelIndex == 0)
    {
        menu.addItem (menuAbout, utf8 ("About Quadraverse"));
        menu.addItem (menuSettings, utf8 ("Settings…"));
        menu.addSeparator();
        menu.addItem (menuQuit, utf8 ("Quit"));
        return menu;
    }
    const int file = 1, session = 2, view = 3;
#endif

    if (topLevelIndex == file)
    {
        menu.addItem (menuNewProject, utf8 ("New Project"));
        menu.addItem (menuOpenProject, utf8 ("Open Project…"));
        menu.addItem (menuSaveProject, utf8 ("Save Project"), true, false);
        menu.addItem (menuSaveProjectAs, utf8 ("Save Project As…"));
#if JUCE_MAC
        menu.addSeparator();
        menu.addItem (menuSettings, utf8 ("Settings…"));
#endif
    }
    else if (topLevelIndex == session)
    {
        auto* c = content();
        menu.addItem (menuMidiSetup, utf8 ("MIDI Setup…"));
        menu.addItem (menuHardwareAudio, utf8 ("Hardware Audio Setup…"));
        menu.addSeparator();
        menu.addItem (menuUseHardware, utf8 ("Use Hardware"), true,
                      c != nullptr && c->isHardwareMode());
        menu.addSeparator();
        menu.addItem (menuCompareReport, utf8 ("Comparison Report…"));
    }
    else if (topLevelIndex == view)
    {
        menu.addItem (menuPluginHost, utf8 ("Plugin Host"));
        menu.addItem (menuLevelMeters, utf8 ("Level Meters"));
    }
    return menu;
}

void QuadraverseMainWindow::menuItemSelected (int menuItemID, int)
{
    auto* c = content();
    if (c == nullptr)
        return;

    switch (menuItemID)
    {
        case menuSettings:        c->openSettings(); break;
        case menuNewProject:      c->newProject(); break;
        case menuOpenProject:     c->openProject(); break;
        case menuSaveProject:     c->saveProject(); break;
        case menuSaveProjectAs:   c->saveProjectAs(); break;
        case menuMidiSetup:       c->openMidiSetup(); break;
        case menuHardwareAudio:   c->openHardwareAudioSetup(); break;
        case menuUseHardware:     c->toggleHardwareMode(); menuItemsChanged(); break;
        case menuPluginHost:      c->openPluginHost(); break;
        case menuLevelMeters:     c->openLevelMeters(); break;
        case menuCompareReport:   c->openComparisonReport(); break;
        case menuQuit:            juce::JUCEApplication::getInstance()->systemRequestedQuit(); break;
        case menuAbout:
            juce::AlertWindow::showMessageBoxAsync (
                juce::AlertWindow::InfoIcon,
                utf8 ("About Quadraverse"),
                utf8 ("Quadraverse — Quadraverb patch editor / translator\n"
                      "Hosts QDV-1 and talks SysEx to Quadraverb hardware."));
            break;
        default: break;
    }
}
