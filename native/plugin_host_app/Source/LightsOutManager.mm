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

    NSMenuItem* findLightsOutMenuItem (NSMenu* menu)
    {
        for (NSMenuItem* item in [menu itemArray])
        {
            if ([[item title] isEqualToString: @"Lights Out"])
                return item;
        }

        return nil;
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
        const uint64_t generation = ++presentationGeneration;

        dispatch_after (dispatch_time (DISPATCH_TIME_NOW, (int64_t) (0.15 * NSEC_PER_SEC)),
                        dispatch_get_main_queue(), ^{
            if (generation != presentationGeneration)
                return;

            applyPresentationOptions();
        });

        return;
    }

    ++presentationGeneration;
    restorePresentationOptions();
}

void lightsOutSyncMenuItem (bool isTicked)
{
    dispatch_async (dispatch_get_main_queue(), ^{
        NSMenu* const mainMenu = [NSApp mainMenu];
        if (mainMenu == nil)
            return;

        for (NSMenuItem* topItem in [mainMenu itemArray])
        {
            NSMenu* const subMenu = [topItem submenu];
            if (subMenu == nil)
                continue;

            if (NSMenuItem* item = findLightsOutMenuItem (subMenu))
            {
                [item setKeyEquivalent: @"l"];
                [item setKeyEquivalentModifierMask: NSEventModifierFlagCommand];
                [item setState: isTicked ? NSControlStateValueOn : NSControlStateValueOff];
                return;
            }
        }
    });
}
