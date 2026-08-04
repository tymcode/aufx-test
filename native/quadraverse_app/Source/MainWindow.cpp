#include "MainWindow.h"
#include "Utf8.h"
#if JUCE_MAC
#include "LightsOutManager_mac.h"
#endif

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
    addKeyListener (&globalKeys);
    if (auto* c = getContentComponent())
        c->addKeyListener (&globalKeys);

#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (this);
#else
    setMenuBar (this);
#endif
    syncNativeMenuShortcuts();
}

QuadraverseMainWindow::~QuadraverseMainWindow()
{
#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (nullptr);
#else
    setMenuBar (nullptr);
#endif
    removeKeyListener (&globalKeys);
    if (auto* c = getContentComponent())
        c->removeKeyListener (&globalKeys);
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

void QuadraverseMainWindow::syncNativeMenuShortcuts()
{
#if JUCE_MAC
    auto* c = content();
    const bool hwTicked = c != nullptr && c->isHardwareMode();
    juce::Timer::callAfterDelay (0, [hwTicked]
    {
        nativeSyncMenuItem ("Send Patch", "p", true, false, false, false);
        nativeSyncMenuItem ("Use Hardware", "u", true, false, hwTicked, true);
        nativeSyncMenuItem ("Level Meters", "m", true, false, false, false);
        nativeSyncMenuItem ("Target View", "t", true, false, false, false);
    });
#endif
}

bool QuadraverseMainWindow::GlobalKeys::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    auto* c = owner.content();
    if (c == nullptr)
        return false;

    if (key.isKeyCode (juce::KeyPress::spaceKey))
    {
        if (QuadraverseMainContent::isEditableFieldFocused())
            return false;
        c->toggleTargetPlayback();
        return true;
    }
    if (key == juce::KeyPress ('p', juce::ModifierKeys::commandModifier, 0))
    {
        c->sendPatch();
        return true;
    }
    if (key == juce::KeyPress ('u', juce::ModifierKeys::commandModifier, 0))
    {
        c->toggleHardwareMode();
        owner.menuItemsChanged();
        owner.syncNativeMenuShortcuts();
        return true;
    }
    if (key == juce::KeyPress ('m', juce::ModifierKeys::commandModifier, 0))
    {
        c->openLevelMeters();
        return true;
    }
    if (key == juce::KeyPress ('t', juce::ModifierKeys::commandModifier, 0))
    {
        c->openTargetView();
        return true;
    }
    return false;
}

juce::StringArray QuadraverseMainWindow::getMenuBarNames()
{
#if JUCE_MAC
    return { utf8 ("File"), utf8 ("Patch"), utf8 ("Session"), utf8 ("View") };
#else
    return { utf8 ("Quadraverse"), utf8 ("File"), utf8 ("Patch"), utf8 ("Session"), utf8 ("View") };
#endif
}

juce::PopupMenu QuadraverseMainWindow::getMenuForIndex (int topLevelIndex, const juce::String&)
{
    juce::PopupMenu menu;
#if JUCE_MAC
    const int file = 0, patch = 1, session = 2, view = 3;
#else
    if (topLevelIndex == 0)
    {
        menu.addItem (menuAbout, utf8 ("About Quadraverse"));
        menu.addItem (menuSettings, utf8 ("Settings…"));
        menu.addSeparator();
        menu.addItem (menuQuit, utf8 ("Quit"));
        return menu;
    }
    const int file = 1, patch = 2, session = 3, view = 4;
#endif

    auto* c = content();

    if (topLevelIndex == file)
    {
        menu.addItem (menuNewProject, utf8 ("New Project"));
        menu.addItem (menuOpenProject, utf8 ("Open Project…"));
        menu.addItem (menuSaveProject, utf8 ("Save Project"), true, false);
        menu.addItem (menuSaveProjectAs, utf8 ("Save Project As…"));
        menu.addSeparator();
        menu.addItem (menuImportSysexBank, utf8 ("Import Sysex Bank…"));
        menu.addItem (menuSaveBulkDump, utf8 ("Save Bulk Dump…"));
        menu.addItem (menuConvertSsx, utf8 ("Convert SSX…"));
        menu.addSeparator();
        {
            juce::PopupMenu saveBank;
            saveBank.addItem (menuSaveBankAsSysex, utf8 ("As Sysex…"));
            saveBank.addItem (menuSaveBankAsPresets, utf8 ("As Presets…"));
            menu.addSubMenu (utf8 ("Save Bank"), saveBank);
        }
#if JUCE_MAC
        menu.addSeparator();
        menu.addItem (menuSettings, utf8 ("Settings…"));
#endif
    }
    else if (topLevelIndex == patch)
    {
        {
            juce::PopupMenu::Item item;
            item.itemID = menuSendPatch;
            item.text = utf8 ("Send Patch");
            item.shortcutKeyDescription = "Cmd+P";
            menu.addItem (std::move (item));
        }
        menu.addSeparator();
        menu.addItem (menuNewPatchContext, utf8 ("New Patch Context"));
        menu.addItem (menuDuplicatePatchContext, utf8 ("Duplicate Patch Context"));
        menu.addItem (menuRenamePatchContext, utf8 ("Rename Patch Context…"));
        {
            juce::PopupMenu::Item item;
            item.itemID = menuDeletePatchContext;
            item.text = utf8 ("Delete Patch Context…");
            item.isEnabled = c != nullptr && c->canDeletePatchContext();
            menu.addItem (std::move (item));
        }
        menu.addSeparator();
        {
            juce::PopupMenu load;
            const auto device = c != nullptr ? c->configuredDeviceName() : utf8 ("Quadraverb");
            load.addItem (menuLoadFromDevice, utf8 ("From ") + device);
            load.addItem (menuLoadFromPlugin, utf8 ("From Plugin"));
            load.addItem (menuLoadFromPresetFile, utf8 ("From Preset File"));
            load.addItem (menuLoadFromSysexDump, utf8 ("From Sysex Dump…"));
            menu.addSubMenu (utf8 ("Load Patch…"), load);
        }
        {
            juce::PopupMenu save;
            save.addItem (menuSavePatchAsPreset, utf8 ("As Preset…"));
            save.addItem (menuSavePatchAsSysex, utf8 ("As Sysex…"));
            menu.addSubMenu (utf8 ("Save Patch"), save);
        }
    }
    else if (topLevelIndex == session)
    {
        menu.addItem (menuMidiSetup, utf8 ("MIDI Setup…"));
        menu.addItem (menuHardwareAudio, utf8 ("Hardware Audio Setup…"));
        menu.addSeparator();
        {
            juce::PopupMenu::Item item;
            item.itemID = menuUseHardware;
            item.text = utf8 ("Use Hardware");
            item.isTicked = c != nullptr && c->isHardwareMode();
            item.shortcutKeyDescription = "Cmd+U";
            menu.addItem (std::move (item));
        }
        menu.addSeparator();
        menu.addItem (menuCompareReport, utf8 ("Comparison Report…"));
    }
    else if (topLevelIndex == view)
    {
        {
            juce::PopupMenu::Item item;
            item.itemID = menuTargetView;
            item.text = utf8 ("Target View");
            item.shortcutKeyDescription = "Cmd+T";
            menu.addItem (std::move (item));
        }
        {
            juce::PopupMenu::Item item;
            item.itemID = menuLevelMeters;
            item.text = utf8 ("Level Meters");
            item.shortcutKeyDescription = "Cmd+M";
            menu.addItem (std::move (item));
        }
    }

    syncNativeMenuShortcuts();
    return menu;
}

void QuadraverseMainWindow::menuItemSelected (int menuItemID, int)
{
    auto* c = content();
    if (c == nullptr)
        return;

    switch (menuItemID)
    {
        case menuSettings:              c->openSettings(); break;
        case menuNewProject:            c->newProject(); break;
        case menuOpenProject:           c->openProject(); break;
        case menuSaveProject:           c->saveProject(); break;
        case menuSaveProjectAs:         c->saveProjectAs(); break;
        case menuImportSysexBank:       c->importSysexBank(); break;
        case menuSaveBulkDump:          c->saveBulkDump(); break;
        case menuConvertSsx:            c->convertSsx(); break;
        case menuSaveBankAsSysex:       c->saveBankAsSysex(); break;
        case menuSaveBankAsPresets:     c->saveBankAsPresets(); break;
        case menuMidiSetup:             c->openMidiSetup(); break;
        case menuHardwareAudio:         c->openHardwareAudioSetup(); break;
        case menuUseHardware:
            c->toggleHardwareMode();
            menuItemsChanged();
            syncNativeMenuShortcuts();
            break;
        case menuTargetView:            c->openTargetView(); break;
        case menuLevelMeters:           c->openLevelMeters(); break;
        case menuCompareReport:         c->openComparisonReport(); break;
        case menuSendPatch:             c->sendPatch(); break;
        case menuNewPatchContext:       c->newPatchContext(); break;
        case menuDuplicatePatchContext: c->duplicatePatchContext(); break;
        case menuRenamePatchContext:    c->renamePatchContext(); break;
        case menuDeletePatchContext:    c->deletePatchContext(); break;
        case menuLoadFromDevice:        c->loadPatchFromDevice(); break;
        case menuLoadFromPlugin:        c->loadPatchFromPlugin(); break;
        case menuLoadFromPresetFile:    c->loadPatchFromPresetFile(); break;
        case menuLoadFromSysexDump:     c->loadPatchFromSysexDump(); break;
        case menuSavePatchAsPreset:     c->savePatchAsPreset(); break;
        case menuSavePatchAsSysex:      c->savePatchAsSysex(); break;
        case menuQuit:                  juce::JUCEApplication::getInstance()->systemRequestedQuit(); break;
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
