#pragma once

void lightsOutShowOverlays();
void lightsOutHideOverlays();
void lightsOutSetPresentationMode (bool enable);
void lightsOutSyncMenuItem (bool isTicked);

/** Set key equivalent + optional checkmark on a macOS main-menu item by title. */
void nativeSyncMenuItem (const char* titleUtf8,
                         const char* keyEquivalentUtf8,
                         bool command,
                         bool shift,
                         bool isTicked,
                         bool applyTick);
