#pragma once

#include <functional>

/** macOS local key monitor so Space reaches playback even when an AU NSView has focus. */
struct SpacePlaybackMonitor
{
    /** Install once; callback runs on the main thread. Return true to consume the event. */
    static void install (std::function<bool()> onSpace);
    static void uninstall();
};
