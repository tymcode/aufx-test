#pragma once

#include <JuceHeader.h>

/**
 * Modal AU plugin scan UI + worker thread.
 * Cache/skip/retry API lives in AuPluginScanner; this module only runs the scan dialog.
 */
namespace AuPluginScanDialog
{
    /**
     * Show the progress dialog and scan Audio Units out-of-process into ``list``,
     * writing the plugin cache on success. Returns false if the scan failed or
     * was cancelled (``error`` set). Does not fall back to a previous cache —
     * callers that want that behavior wrap this (see AuPluginScanner::ensureCache).
     */
    bool runModalScan (const juce::File& dataRoot,
                       juce::KnownPluginList& list,
                       juce::Component* parentForDialog,
                       juce::String& error);
}
