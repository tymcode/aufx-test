#include "SpacePlaybackMonitor.h"
#include <functional>

#if defined (__APPLE__)
#import <AppKit/AppKit.h>

namespace
{
    id spaceMonitorToken = nil;
    std::function<bool()>* spaceCallback = nullptr;
}

void SpacePlaybackMonitor::install (std::function<bool()> onSpace)
{
    uninstall();

    spaceCallback = new std::function<bool()> (std::move (onSpace));

    spaceMonitorToken = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
                                                              handler:^NSEvent* (NSEvent* event)
    {
        // keyCode 49 = Space. Ignore when Command/Option/Control/Shift held.
        if ([event keyCode] != 49)
            return event;

        const NSEventModifierFlags mods = [event modifierFlags]
            & (NSEventModifierFlagDeviceIndependentFlagsMask);
        if ((mods & (NSEventModifierFlagCommand | NSEventModifierFlagOption
                     | NSEventModifierFlagControl | NSEventModifierFlagShift)) != 0)
            return event;

        // Let Cocoa text fields keep Space (typing).
        NSResponder* first = [[NSApp keyWindow] firstResponder];
        if ([first isKindOfClass:[NSText class]]
            || [first isKindOfClass:[NSTextView class]]
            || [first isKindOfClass:[NSTextField class]])
            return event;

        if (spaceCallback != nullptr && (*spaceCallback)())
            return nil; // consume

        return event;
    }];
}

void SpacePlaybackMonitor::uninstall()
{
    if (spaceMonitorToken != nil)
    {
        [NSEvent removeMonitor:spaceMonitorToken];
        spaceMonitorToken = nil;
    }
    delete spaceCallback;
    spaceCallback = nullptr;
}

#else

void SpacePlaybackMonitor::install (std::function<bool()>) {}
void SpacePlaybackMonitor::uninstall() {}

#endif
