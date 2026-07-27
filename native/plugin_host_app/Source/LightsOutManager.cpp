#include "LightsOutManager.h"

#if JUCE_MAC
 #include "LightsOutManager_mac.h"
#endif

#include "HostLog.h"

LightsOutManager::LightsOutManager() = default;

LightsOutManager::~LightsOutManager()
{
    release();
}

void LightsOutManager::setEnabled (bool shouldEnable)
{
    if (enabled == shouldEnable)
        return;

#if JUCE_MAC
    if (shouldEnable)
    {
        lightsOutShowOverlays();

        if (hostWindow != nullptr)
        {
            hostWindow->setAlwaysOnTop (true);
            hostWindow->toFront (true);
        }

        lightsOutSetPresentationMode (true);
        lightsOutSyncMenuItem (true);
        HostLog::info ("Lights Out enabled");
        enabled = true;
        return;
    }

    enabled = false;
    lightsOutSetPresentationMode (false);
    lightsOutSyncMenuItem (false);

    if (hostWindow != nullptr)
        hostWindow->setAlwaysOnTop (false);

    // Defer overlay teardown so AppKit finishes menu tracking / the key event
    // before the windows are ordered out and released.
    juce::MessageManager::callAsync ([]
    {
        lightsOutHideOverlays();
        HostLog::info ("Lights Out disabled");
    });
#else
    enabled = shouldEnable;
#endif
}

void LightsOutManager::release()
{
#if JUCE_MAC
    if (! enabled)
        return;

    enabled = false;
    lightsOutSetPresentationMode (false);
    lightsOutHideOverlays();

    if (hostWindow != nullptr)
        hostWindow->setAlwaysOnTop (false);

    HostLog::info ("Lights Out released");
#endif
}
