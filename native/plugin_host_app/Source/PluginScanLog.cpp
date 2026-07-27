#include "PluginScanLog.h"

#include "AppVersion.h"

namespace
{
    juce::File scannerWorkerLogFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("AU Effects Explorer")
            .getChildFile ("plugin-scan-worker-crashes.log");
    }
}

PluginScanLog::PluginScanLog (const juce::File& dataRootIn)
    : dataRoot (dataRootIn),
      failuresFile (failuresLogFile (dataRootIn))
{
    dataRoot.createDirectory();
    failuresFile.getParentDirectory().createDirectory();
}

juce::File PluginScanLog::failuresLogFile (const juce::File& dataRoot)
{
    return dataRoot.getChildFile ("plugin-scan-failures.log");
}

juce::File PluginScanLog::lastRunFile (const juce::File& dataRoot)
{
    return dataRoot.getChildFile ("plugin-scan-last-run.txt");
}

juce::String PluginScanLog::timestamp()
{
    return juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S");
}

juce::String PluginScanLog::scanHeaderDetails()
{
    return "app=" + juce::String (aufx_version::versionString())
           + " os=" + juce::SystemStats::getOperatingSystemName();
}

void PluginScanLog::appendFailuresLine (const juce::String& event, const juce::String& details)
{
    const auto line = timestamp() + "  " + event
                      + (details.isNotEmpty() ? "  " + details : juce::String())
                      + "\n";
    failuresFile.appendText (line, false, false, nullptr);
}

void PluginScanLog::appendWorkerLogLine (const juce::String& line)
{
    auto file = scannerWorkerLogFile();
    file.getParentDirectory().createDirectory();
    file.appendText (timestamp() + "  " + line.trim() + "\n", false, false, nullptr);
}

void PluginScanLog::logScanStart (int workerCount,
                                  int timeoutMs,
                                  int totalDiscovered,
                                  int toScan,
                                  int preSkipped)
{
    appendFailuresLine ("SCAN_START",
                        scanHeaderDetails()
                            + " data_root=" + dataRoot.getFullPathName()
                            + " workers=" + juce::String (workerCount)
                            + " timeout_ms=" + juce::String (timeoutMs)
                            + " discovered=" + juce::String (totalDiscovered)
                            + " to_scan=" + juce::String (toScan)
                            + " pre_skipped=" + juce::String (preSkipped));
}

void PluginScanLog::logPluginResult (int workerIndex,
                                     const juce::String& pluginId,
                                     const juce::String& displayName,
                                     const juce::String& outcome,
                                     int durationMs,
                                     int typesFound)
{
    appendFailuresLine ("PLUGIN",
                        "worker=" + juce::String (workerIndex)
                            + " outcome=" + outcome
                            + " duration_ms=" + juce::String (durationMs)
                            + " types=" + juce::String (typesFound)
                            + " name=\"" + displayName + "\""
                            + " id=" + pluginId);

    if (outcome == "timeout" || outcome == "subprocess_lost" || outcome == "send_failed")
    {
        appendWorkerLogLine ("scan " + outcome + " (" + juce::String (durationMs)
                             + " ms) while scanning: " + pluginId);
    }
}

void PluginScanLog::logScanEnd (bool success,
                                bool cancelled,
                                int succeeded,
                                int failed,
                                int preSkipped,
                                juce::int64 durationMs,
                                const juce::String& failedLogText)
{
    appendFailuresLine ("SCAN_END",
                        juce::String (success ? "ok=true" : "ok=false")
                            + " cancelled=" + juce::String (cancelled ? "true" : "false")
                            + " succeeded=" + juce::String (succeeded)
                            + " failed=" + juce::String (failed)
                            + " pre_skipped=" + juce::String (preSkipped)
                            + " duration_ms=" + juce::String (durationMs));

    auto lastRun = lastRunFile (dataRoot);
    juce::String body;
    body << "AU plugin scan summary (" << timestamp() << ")\n";
    body << scanHeaderDetails() << "\n";
    body << "Data root: " << dataRoot.getFullPathName() << "\n";
    body << "Result: " << (cancelled ? "cancelled" : (success ? "success" : "failed")) << "\n";
    body << "Succeeded: " << succeeded << ", failed: " << failed << ", pre-skipped: " << preSkipped << "\n";
    body << "Duration: " << durationMs << " ms\n\n";
    body << "Failed / skipped plugins:\n";
    body << (failedLogText.isNotEmpty() ? failedLogText : juce::String ("(none)"));
    body << "\n";
    lastRun.replaceWithText (body);
}

void PluginScanLog::logRetry (const juce::String& pluginId,
                              const juce::String& displayName,
                              const juce::String& outcome,
                              int durationMs,
                              int typesFound)
{
    appendFailuresLine ("RETRY",
                        "outcome=" + outcome
                            + " duration_ms=" + juce::String (durationMs)
                            + " types=" + juce::String (typesFound)
                            + " name=\"" + displayName + "\""
                            + " id=" + pluginId);

    if (outcome != "ok" && outcome != "ok_slow")
    {
        appendWorkerLogLine ("retry " + outcome + " (" + juce::String (durationMs)
                             + " ms) for: " + pluginId);
    }
}
