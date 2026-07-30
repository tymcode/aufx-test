#include "AuPluginScanner.h"
#include "AuPluginScanDialog.h"
#include "PluginScannerOOP.h"
#include "PluginScanLog.h"
#include "HostFileUtils.h"
#include "HostLog.h"
#include "HostPreferences.h"
#include "AppVersion.h"

namespace
{
    int scanTimeoutMs()
    {
        return HostPreferences::get().getPluginScanTimeoutMs();
    }
}

juce::File AuPluginScanner::cacheFile (const juce::File& dataRoot)
{
    return dataRoot.getChildFile ("plugin-cache.xml");
}

juce::File AuPluginScanner::deadMansPedalFile (const juce::File& dataRoot)
{
    return dataRoot.getChildFile ("plugin-scan-crashes.txt");
}

juce::File AuPluginScanner::skipListFile (const juce::File& dataRoot)
{
    return dataRoot.getChildFile ("plugin-scan-skip.txt");
}

juce::File AuPluginScanner::inProgressStampFile (const juce::File& dataRoot)
{
    return dataRoot.getChildFile ("plugin-scan-in-progress.txt");
}

bool AuPluginScanner::cacheExists (const juce::File& dataRoot)
{
    return cacheFile (dataRoot).existsAsFile();
}

bool AuPluginScanner::loadCache (const juce::File& dataRoot, juce::KnownPluginList& list, juce::String& error)
{
    const auto file = cacheFile (dataRoot);
    if (! file.existsAsFile())
    {
        error = "Plugin cache not found";
        return false;
    }

    std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (file));
    if (xml == nullptr)
    {
        error = "Failed to parse plugin cache";
        return false;
    }

    list.clear();
    list.recreateFromXml (*xml);
    return true;
}

bool AuPluginScanner::saveCache (const juce::File& dataRoot,
                                 const juce::KnownPluginList& list,
                                 juce::String& error,
                                 const ScanRunStats* stats)
{
    dataRoot.createDirectory();
    auto xml = list.createXml();
    if (xml == nullptr)
    {
        error = "Failed to serialise plugin cache";
        return false;
    }

    xml->setAttribute ("scanTime", juce::Time::getCurrentTime().toISO8601 (true));
    xml->setAttribute ("os", juce::SystemStats::getOperatingSystemName());
    xml->setAttribute ("hostVersion", aufx_version::versionString());

    if (stats != nullptr)
    {
        xml->setAttribute ("scanWorkerCount", stats->workerCount);
        xml->setAttribute ("scanTimeoutMs", stats->timeoutMs);
        xml->setAttribute ("scanDiscovered", stats->discovered);
        xml->setAttribute ("scanPreSkipped", stats->preSkipped);
        xml->setAttribute ("scanAttempted", stats->scanned);
        xml->setAttribute ("scanSucceeded", stats->succeeded);
        xml->setAttribute ("scanFailed", stats->failed);
        xml->setAttribute ("scanDurationMs", (int) stats->durationMs);
    }

    if (! xml->writeTo (cacheFile (dataRoot)))
    {
        error = "Failed to write plugin cache";
        return false;
    }

    return true;
}

juce::StringArray AuPluginScanner::loadSkipList (const juce::File& dataRoot)
{
    return HostFileUtils::readLinesFile (skipListFile (dataRoot));
}

void AuPluginScanner::saveSkipList (const juce::File& dataRoot, const juce::StringArray& skips)
{
    HostFileUtils::writeLinesFile (skipListFile (dataRoot), skips);
}

void AuPluginScanner::addToSkipList (const juce::File& dataRoot, const juce::String& pluginId)
{
    if (pluginId.isEmpty())
        return;

    auto skips = loadSkipList (dataRoot);
    if (! skips.contains (pluginId))
    {
        skips.add (pluginId);
        saveSkipList (dataRoot, skips);
    }
}

void AuPluginScanner::removeFromSkipList (const juce::File& dataRoot, const juce::String& pluginId)
{
    if (pluginId.isEmpty())
        return;

    auto skips = loadSkipList (dataRoot);
    if (skips.contains (pluginId))
    {
        skips.removeString (pluginId);
        saveSkipList (dataRoot, skips);
    }

    auto crashed = HostFileUtils::readLinesFile (deadMansPedalFile (dataRoot));
    if (crashed.contains (pluginId))
    {
        crashed.removeString (pluginId);
        HostFileUtils::writeLinesFile (deadMansPedalFile (dataRoot), crashed);
    }

    auto inProgress = HostFileUtils::readLinesFile (inProgressStampFile (dataRoot));
    if (inProgress.contains (pluginId))
    {
        inProgress.removeString (pluginId);
        if (inProgress.isEmpty())
            inProgressStampFile (dataRoot).deleteFile();
        else
            HostFileUtils::writeLinesFile (inProgressStampFile (dataRoot), inProgress);
    }
}

juce::String AuPluginScanner::displayNameForPluginId (const juce::String& pluginId)
{
    juce::AudioPluginFormatManager formatManager;
    juce::addDefaultFormatsToManager (formatManager);

    if (auto* format = HostFileUtils::findAudioUnitFormat (formatManager))
    {
        const auto name = format->getNameOfPluginFromIdentifier (pluginId);
        if (name.isNotEmpty())
            return name;
    }

    return pluginId;
}

bool AuPluginScanner::retrySkippedPlugin (const juce::File& dataRoot,
                                          const juce::String& pluginId,
                                          juce::KnownPluginList& list,
                                          juce::String& error)
{
    if (pluginId.isEmpty())
    {
        error = "No plugin selected";
        return false;
    }

    dataRoot.createDirectory();

    // Start from the on-disk cache so we don't wipe other types.
    if (cacheExists (dataRoot))
    {
        juce::String loadError;
        if (! loadCache (dataRoot, list, loadError))
        {
            error = loadError;
            return false;
        }
    }

    removeFromSkipList (dataRoot, pluginId);
    list.removeFromBlacklist (pluginId);

    juce::AudioPluginFormatManager formatManager;
    juce::addDefaultFormatsToManager (formatManager);

    auto* auFormat = HostFileUtils::findAudioUnitFormat (formatManager);
    if (auFormat == nullptr)
    {
        error = "AudioUnit format is not available in this build";
        addToSkipList (dataRoot, pluginId);
        return false;
    }

    const auto niceName = displayNameForPluginId (pluginId);
    const auto pedal = deadMansPedalFile (dataRoot);

    {
        auto crashedPlugins = HostFileUtils::readLinesFile (pedal);
        crashedPlugins.removeString (pluginId);
        crashedPlugins.add (pluginId);
        HostFileUtils::writeLinesFile (pedal, crashedPlugins);
    }

    inProgressStampFile (dataRoot).replaceWithText (pluginId + "\n");

    const int timeoutMs = scanTimeoutMs();
    HostLog::info ("AU scan retry starting for " + niceName + " timeout_ms=" + juce::String (timeoutMs));

    OutOfProcessPluginScanner scanner (nullptr, timeoutMs);
    juce::OwnedArray<juce::PluginDescription> typesFound;
    const bool workerOk = scanner.findPluginTypesFor (*auFormat, typesFound, pluginId);
    const int durationMs = scanner.getLastScanDurationMs();
    const auto failureReason = scanner.getLastFailureReason();
    scanner.scanFinished();

    inProgressStampFile (dataRoot).deleteFile();

    {
        auto crashedPlugins = HostFileUtils::readLinesFile (pedal);
        crashedPlugins.removeString (pluginId);
        HostFileUtils::writeLinesFile (pedal, crashedPlugins);
    }

    PluginScanLog scanLog (dataRoot);

    if (! workerOk || typesFound.isEmpty())
    {
        const juce::String outcome = workerOk ? "no_types"
                                              : oopScanFailureReasonString (failureReason);
        scanLog.logRetry (pluginId, niceName, outcome, durationMs, typesFound.size());

        addToSkipList (dataRoot, pluginId);
        if (typesFound.isEmpty())
            list.addToBlacklist (pluginId);

        juce::String ignore;
        saveCache (dataRoot, list, ignore);

        error = "Retry failed for " + niceName
                + (workerOk ? " (no types found)" : " (" + outcome + ")");
        HostLog::error ("AU scan retry: " + error + " duration_ms=" + juce::String (durationMs));
        return false;
    }

    scanLog.logRetry (pluginId, niceName, "ok", durationMs, typesFound.size());

    for (auto* desc : typesFound)
        if (desc != nullptr)
            list.addType (*desc);

    juce::String saveError;
    if (! saveCache (dataRoot, list, saveError))
    {
        error = saveError;
        return false;
    }

    HostLog::info ("AU scan retry succeeded for " + niceName + " (types="
                   + juce::String (typesFound.size()) + " duration_ms=" + juce::String (durationMs) + ")");
    return true;
}

bool AuPluginScanner::ensureCache (const juce::File& dataRoot,
                                   juce::KnownPluginList& list,
                                   juce::Component* parentForDialog,
                                   juce::String& error,
                                   bool forceRescan)
{
    if (! forceRescan && cacheExists (dataRoot))
    {
        if (loadCache (dataRoot, list, error))
        {
            HostLog::info ("AU plugin cache loaded (" + juce::String (list.getTypes().size()) + " types)");
            return true;
        }

        HostLog::error ("AU plugin cache load failed: " + error);
    }

    if (! AuPluginScanDialog::runModalScan (dataRoot, list, parentForDialog, error))
    {
        // Still try to load whatever was cached if a previous cache exists.
        juce::String loadError;
        if (cacheExists (dataRoot) && loadCache (dataRoot, list, loadError))
        {
            HostLog::info ("AU plugin cache loaded from previous scan (" + juce::String (list.getTypes().size())
                           + " types)");
            return true;
        }
        return false;
    }

    return true;
}
