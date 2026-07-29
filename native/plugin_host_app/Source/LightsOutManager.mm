/**
 * macOS side of "Lights Out": black out every attached display (for critical
 * listening / clean screen recordings) while keeping the host window on top.
 *
 * Implemented as raw borderless NSWindows rather than JUCE components
 * because the overlays must cover *other* screens, float across Spaces, and
 * never take key focus from the host — all direct NSWindow behaviors.
 * Everything here runs on the main thread (AppKit requirement).
 */
#include "LightsOutManager_mac.h"
#import <AppKit/AppKit.h>

namespace
{
    // Strong references keep the windows alive; ARC releases them when the
    // array is emptied. releasedWhenClosed is explicitly NO so that hiding a
    // window never triggers an extra release (the classic teardown segfault).
    NSMutableArray<NSWindow*>* overlayWindows()
    {
        static NSMutableArray<NSWindow*>* windows = [[NSMutableArray alloc] init];
        return windows;
    }

    NSApplicationPresentationOptions savedPresentation { NSApplicationPresentationDefault };
    bool presentationOptionsActive { false };
    uint64_t presentationGeneration { 0 };

    void applyPresentationOptions()
    {
        @try
        {
            savedPresentation = [NSApp presentationOptions];
            [NSApp setPresentationOptions: NSApplicationPresentationAutoHideMenuBar
                                           | NSApplicationPresentationHideDock];
            presentationOptionsActive = true;
        }
        @catch (NSException* e)
        {
            NSLog (@"Lights Out: presentation options failed: %@", e);
        }
    }

    void restorePresentationOptions()
    {
        if (! presentationOptionsActive)
            return;

        @try
        {
            [NSApp setPresentationOptions: savedPresentation];
        }
        @catch (NSException* e)
        {
            NSLog (@"Lights Out: restore presentation options failed: %@", e);
        }

        presentationOptionsActive = false;
    }
}

void lightsOutShowOverlays()
{
    lightsOutHideOverlays();

    for (NSScreen* screen in [NSScreen screens])
    {
        NSWindow* window = [[NSWindow alloc] initWithContentRect: [screen frame]
                                                       styleMask: NSWindowStyleMaskBorderless
                                                         backing: NSBackingStoreBuffered
                                                           defer: NO
                                                          screen: screen];

        [window setReleasedWhenClosed: NO];
        [window setBackgroundColor: [NSColor blackColor]];
        [window setOpaque: YES];
        [window setHasShadow: NO];
        [window setIgnoresMouseEvents: NO];
        [window setLevel: NSNormalWindowLevel + 1];
        [window setCollectionBehavior: NSWindowCollectionBehaviorCanJoinAllSpaces
                                       | NSWindowCollectionBehaviorStationary
                                       | NSWindowCollectionBehaviorFullScreenAuxiliary];
        [window orderFrontRegardless];

        [overlayWindows() addObject: window];
    }
}

void lightsOutHideOverlays()
{
    NSMutableArray<NSWindow*>* windows = overlayWindows();

    for (NSWindow* window in windows)
        [window orderOut: nil];

    // ARC releases the windows here; because releasedWhenClosed is NO and we
    // never call close, this is the only release and cannot double-free.
    [windows removeAllObjects];
}

void lightsOutSetPresentationMode (bool enable)
{
    if (enable)
    {
        // Hiding the menu bar/dock is deferred 150 ms: doing it while AppKit
        // is still tracking the menu click that triggered Lights Out leaves
        // the menu bar in a corrupt half-open state. The generation counter
        // cancels the pending apply if the user toggles off again within the
        // delay window — otherwise a rapid on/off would *end* with the menu
        // bar hidden but Lights Out logically off.
        const uint64_t generation = ++presentationGeneration;

        dispatch_after (dispatch_time (DISPATCH_TIME_NOW, (int64_t) (0.15 * NSEC_PER_SEC)),
                        dispatch_get_main_queue(), ^{
            if (generation != presentationGeneration)
                return;

            applyPresentationOptions();
        });

        return;
    }

    // Disable path bumps the generation (invalidating any pending apply) and
    // restores synchronously — restoring is safe mid-menu-tracking.
    ++presentationGeneration;
    restorePresentationOptions();
}

void lightsOutSyncMenuItem (bool isTicked)
{
    nativeSyncMenuItem ("Lights Out", "l", true, false, isTicked, true);
}

/**
 * HACK: patch a native NSMenuItem (shortcut glyph + checkmark) by searching
 * the main menu for its title. JUCE rebuilds the mac main menu from its
 * MenuBarModel but does not reliably re-apply tick state or key equivalents
 * on that rebuild, so MainWindow::syncNativeMenuShortcuts() calls this after
 * every toggle. Title-based lookup is fragile by design-tradeoff: it avoids
 * keeping NSMenuItem references across JUCE's menu rebuilds (which would
 * dangle). If a menu item is renamed, its sync silently stops — keep titles
 * here and in getMenuForIndex() in lockstep.
 */
void nativeSyncMenuItem (const char* titleUtf8,
                         const char* keyEquivalentUtf8,
                         bool command,
                         bool shift,
                         bool isTicked,
                         bool applyTick)
{
    NSString* const title = [NSString stringWithUTF8String: titleUtf8];
    NSString* const key = [NSString stringWithUTF8String: keyEquivalentUtf8 != nullptr ? keyEquivalentUtf8 : ""];
    const bool tick = isTicked;
    const bool doTick = applyTick;
    const bool useCommand = command;
    const bool useShift = shift;

    dispatch_async (dispatch_get_main_queue(), ^{
        NSMenu* const mainMenu = [NSApp mainMenu];
        if (mainMenu == nil)
            return;

        for (NSMenuItem* topItem in [mainMenu itemArray])
        {
            NSMenu* const subMenu = [topItem submenu];
            if (subMenu == nil)
                continue;

            for (NSMenuItem* item in [subMenu itemArray])
            {
                if (! [[item title] isEqualToString: title])
                    continue;

                if ([key length] > 0)
                {
                    [item setKeyEquivalent: key];
                    NSEventModifierFlags mask = 0;
                    if (useCommand)
                        mask |= NSEventModifierFlagCommand;
                    if (useShift)
                        mask |= NSEventModifierFlagShift;
                    [item setKeyEquivalentModifierMask: mask];
                }

                if (doTick)
                    [item setState: tick ? NSControlStateValueOn : NSControlStateValueOff];
                return;
            }
        }
    });
}
