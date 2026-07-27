#pragma once

#include <JuceHeader.h>

/** Append-only scan diagnostics under the exploration data root + worker log mirror. */
class PluginScanLog
{
public:
    explicit PluginScanLog (const juce::File& dataRoot);

    static juce::File failuresLogFile (const juce::File& dataRoot);
    static juce::File lastRunFile (const juce::File& dataRoot);

    void logScanStart (int workerCount,
                       int timeoutMs,
                       int totalDiscovered,
                       int toScan,
                       int preSkipped);

    void logPluginResult (int workerIndex,
                          const juce::String& pluginId,
                          const juce::String& displayName,
                          const juce::String& outcome,
                          int durationMs,
                          int typesFound = 0);

    void logScanEnd (bool success,
                     bool cancelled,
                     int succeeded,
                     int failed,
                     int preSkipped,
                     juce::int64 durationMs,
                     const juce::String& failedLogText);

    void logRetry (const juce::String& pluginId,
                   const juce::String& displayName,
                   const juce::String& outcome,
                   int durationMs,
                   int typesFound = 0);

    /** Same file as fatal-signal worker crashes (Application Support). */
    static void appendWorkerLogLine (const juce::String& line);

private:
    juce::File dataRoot;
    juce::File failuresFile;

    void appendFailuresLine (const juce::String& event, const juce::String& details);
    static juce::String timestamp();
    static juce::String scanHeaderDetails();
};
