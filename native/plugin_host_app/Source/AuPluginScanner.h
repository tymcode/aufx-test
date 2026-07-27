#pragma once

#include <JuceHeader.h>

/** Scans Audio Unit components and caches them as KnownPluginList XML. */
class AuPluginScanner
{
public:
    struct ScanRunStats
    {
        int workerCount = 0;
        int timeoutMs = 0;
        int discovered = 0;
        int preSkipped = 0;
        int scanned = 0;
        int succeeded = 0;
        int failed = 0;
        juce::int64 durationMs = 0;
    };

    static juce::File cacheFile (const juce::File& dataRoot);
    static juce::File deadMansPedalFile (const juce::File& dataRoot);
    static juce::File skipListFile (const juce::File& dataRoot);
    static juce::File inProgressStampFile (const juce::File& dataRoot);

    static bool cacheExists (const juce::File& dataRoot);
    static bool loadCache (const juce::File& dataRoot, juce::KnownPluginList& list, juce::String& error);
    static bool saveCache (const juce::File& dataRoot,
                           const juce::KnownPluginList& list,
                           juce::String& error,
                           const ScanRunStats* stats = nullptr);

    static juce::StringArray loadSkipList (const juce::File& dataRoot);
    static void saveSkipList (const juce::File& dataRoot, const juce::StringArray& skips);
    static void addToSkipList (const juce::File& dataRoot, const juce::String& pluginId);
    static void removeFromSkipList (const juce::File& dataRoot, const juce::String& pluginId);

    /** Best-effort display name for a skip-list / AU identifier. */
    static juce::String displayNameForPluginId (const juce::String& pluginId);

    /**
     * Rescan a single previously skipped plugin out-of-process, merge into the
     * cache, and remove it from the skip list on success. On failure the plugin
     * stays (or is re-added) on the skip list.
     */
    static bool retrySkippedPlugin (const juce::File& dataRoot,
                                    const juce::String& pluginId,
                                    juce::KnownPluginList& list,
                                    juce::String& error);

    /**
     * If cache missing (or forceRescan), run a modal scan dialog with progress +
     * failed-plugin list. Failed / previously hung plugins are skipped and do not
     * halt the scan.
     */
    static bool ensureCache (const juce::File& dataRoot,
                             juce::KnownPluginList& list,
                             juce::Component* parentForDialog,
                             juce::String& error,
                             bool forceRescan = false);
};
