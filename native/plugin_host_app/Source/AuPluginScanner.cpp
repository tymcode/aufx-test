#include "AuPluginScanner.h"
#include "PluginScannerOOP.h"
#include "Utf8.h"

#include <thread>
#include <vector>

namespace
{
    juce::StringArray readLinesFile (const juce::File& file)
    {
        juce::StringArray lines;
        if (file.existsAsFile())
            file.readLines (lines);
        lines.removeEmptyStrings();
        return lines;
    }

    void writeLinesFile (const juce::File& file, const juce::StringArray& lines)
    {
        file.getParentDirectory().createDirectory();
        file.replaceWithText (lines.joinIntoString ("\n") + (lines.isEmpty() ? "" : "\n"));
    }

    juce::String displayNameForPluginId (juce::AudioPluginFormat& format, const juce::String& pluginId)
    {
        const auto name = format.getNameOfPluginFromIdentifier (pluginId);
        return name.isNotEmpty() ? name : pluginId;
    }

    /** Shared state between the scan worker thread and the modal UI. */
    struct ScanSharedState
    {
        std::atomic<bool> cancelRequested { false };
        std::atomic<bool> finished { false };
        std::atomic<bool> ok { false };
        std::atomic<double> progress { 0.0 };

        juce::CriticalSection lock;
        juce::String statusMessage;
        juce::String failedLog;
        juce::String error;
        juce::String currentPluginId;
    };

    class AuScanDialog : public juce::Component,
                         private juce::Timer,
                         private juce::Button::Listener
    {
    public:
        AuScanDialog (ScanSharedState& sharedIn)
            : shared (sharedIn)
        {
            titleLabel.setText ("Scanning Audio Units...", juce::dontSendNotification);
            titleLabel.setFont (juce::FontOptions (16.0f, juce::Font::bold));
            addAndMakeVisible (titleLabel);

            onceLabel.setText ("This scan only needs to be performed once. Results are cached and reused on later launches.",
                               juce::dontSendNotification);
            onceLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            onceLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
            onceLabel.setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (onceLabel);

            hintLabel.setText (utf8 ("Plugins are scanned in a separate process; crashes and hangs are skipped automatically. "
                                     "Use Plugins → Rescan Audio Units… only after installing or updating plugins."),
                               juce::dontSendNotification);
            hintLabel.setFont (juce::FontOptions (12.0f));
            hintLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
            hintLabel.setJustificationType (juce::Justification::topLeft);
            addAndMakeVisible (hintLabel);

            statusLabel.setText ("Preparing...", juce::dontSendNotification);
            statusLabel.setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (statusLabel);

            addAndMakeVisible (progressBar);

            failedHeading.setText ("Failed / skipped plugins", juce::dontSendNotification);
            addAndMakeVisible (failedHeading);

            failedEditor.setMultiLine (true);
            failedEditor.setReadOnly (true);
            failedEditor.setScrollbarsShown (true);
            failedEditor.setCaretVisible (false);
            failedEditor.setTextToShowWhenEmpty (utf8 ("None yet. If a plugin hangs, quit the app — it will be skipped on the next scan."),
                                                 juce::Colours::grey);
            addAndMakeVisible (failedEditor);

            cancelButton.setButtonText ("Cancel");
            cancelButton.addListener (this);
            addAndMakeVisible (cancelButton);

            setSize (560, 400);
            startTimerHz (10);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (14);
            titleLabel.setBounds (area.removeFromTop (24));
            area.removeFromTop (6);
            onceLabel.setBounds (area.removeFromTop (22));
            area.removeFromTop (4);
            hintLabel.setBounds (area.removeFromTop (40));
            area.removeFromTop (8);
            statusLabel.setBounds (area.removeFromTop (22));
            area.removeFromTop (6);
            progressBar.setBounds (area.removeFromTop (18));
            area.removeFromTop (12);
            failedHeading.setBounds (area.removeFromTop (20));
            auto bottom = area.removeFromBottom (36);
            cancelButton.setBounds (bottom.removeFromRight (100).reduced (0, 4));
            area.removeFromBottom (8);
            failedEditor.setBounds (area);
        }

        void timerCallback() override
        {
            progressValue = shared.progress.load();

            juce::String status, failed;
            {
                const juce::ScopedLock sl (shared.lock);
                status = shared.statusMessage;
                failed = shared.failedLog;
            }

            statusLabel.setText (status, juce::dontSendNotification);
            if (failed != failedEditor.getText())
                failedEditor.setText (failed, juce::dontSendNotification);

            if (shared.finished.load())
            {
                stopTimer();
                if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                    dw->exitModalState (shared.ok.load() ? 1 : 0);
            }
        }

        void buttonClicked (juce::Button* button) override
        {
            if (button == &cancelButton)
            {
                shared.cancelRequested = true;
                cancelButton.setEnabled (false);
                statusLabel.setText ("Cancelling...", juce::dontSendNotification);
            }
        }

    private:
        ScanSharedState& shared;
        double progressValue { 0.0 };
        juce::Label titleLabel;
        juce::Label onceLabel;
        juce::Label hintLabel;
        juce::Label statusLabel;
        juce::ProgressBar progressBar { progressValue };
        juce::Label failedHeading;
        juce::TextEditor failedEditor;
        juce::TextButton cancelButton;
    };

    class AuScanThread : public juce::Thread
    {
    public:
        AuScanThread (const juce::File& root,
                      juce::KnownPluginList& targetList,
                      ScanSharedState& sharedIn)
            : juce::Thread ("AU Plugin Scan"),
              dataRoot (root),
              list (targetList),
              shared (sharedIn)
        {
        }

        void run() override
        {
            juce::String error;
            const bool success = runScan (error);

            {
                const juce::ScopedLock sl (shared.lock);
                shared.error = error;
            }

            shared.ok = success;
            shared.finished = true;
        }

    private:
        void setStatus (const juce::String& text)
        {
            const juce::ScopedLock sl (shared.lock);
            shared.statusMessage = text;
        }

        void appendFailed (const juce::String& line)
        {
            const juce::ScopedLock sl (shared.lock);
            if (shared.failedLog.isNotEmpty())
                shared.failedLog += "\n";
            shared.failedLog += line;
        }

        bool runScan (juce::String& error)
        {
            juce::AudioPluginFormatManager formatManager;
            juce::addDefaultFormatsToManager (formatManager);

            juce::AudioPluginFormat* auFormat = nullptr;
            for (auto* format : formatManager.getFormats())
            {
                if (format != nullptr && format->getName().containsIgnoreCase ("AudioUnit"))
                {
                    auFormat = format;
                    break;
                }
            }

            if (auFormat == nullptr)
            {
                error = "AudioUnit format is not available in this build";
                return false;
            }

            // Recover from a previous hang: stamp left behind => permanent skip.
            const auto stampFile = AuPluginScanner::inProgressStampFile (dataRoot);
            auto skipList = AuPluginScanner::loadSkipList (dataRoot);

            if (stampFile.existsAsFile())
            {
                for (const auto& hungId : readLinesFile (stampFile))
                {
                    if (hungId.isEmpty())
                        continue;

                    if (! skipList.contains (hungId))
                    {
                        skipList.add (hungId);
                        AuPluginScanner::saveSkipList (dataRoot, skipList);
                    }
                    appendFailed ("Skipped (hung last time): "
                                  + displayNameForPluginId (*auFormat, hungId));
                }
                stampFile.deleteFile();
            }

            // Also treat dead-man's-pedal leftovers as permanent skips.
            for (const auto& crashed : readLinesFile (AuPluginScanner::deadMansPedalFile (dataRoot)))
            {
                if (crashed.isNotEmpty() && ! skipList.contains (crashed))
                {
                    skipList.add (crashed);
                    appendFailed ("Skipped (previous crash): "
                                  + displayNameForPluginId (*auFormat, crashed));
                }
            }
            AuPluginScanner::saveSkipList (dataRoot, skipList);

            juce::FileSearchPath paths;
            paths.add (juce::File ("/Library/Audio/Plug-Ins/Components"));
            paths.add (juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                           .getChildFile ("Library/Audio/Plug-Ins/Components"));

            auto files = auFormat->searchPathsForPlugins (paths, true, false);

            // Remove skipped plugins entirely so they never block the scan.
            juce::StringArray toScan;
            for (const auto& file : files)
            {
                if (skipList.contains (file))
                {
                    appendFailed ("Skipped: " + displayNameForPluginId (*auFormat, file));
                    continue;
                }
                toScan.add (file);
            }

            list.clear();

            // Parallel out-of-process scan: each worker owns its own child process.
            const int workerCount = juce::jlimit (
                1,
                6,
                juce::jmax (1, (int) std::thread::hardware_concurrency()));

            std::atomic<int> nextIndex { 0 };
            std::atomic<int> completedCount { 0 };
            std::mutex listMutex;
            std::mutex skipMutex;
            std::mutex stampMutex;
            juce::StringArray inFlight;

            const auto pedal = AuPluginScanner::deadMansPedalFile (dataRoot);
            const int total = juce::jmax (1, toScan.size());

            auto rewriteStamp = [&]()
            {
                if (inFlight.isEmpty())
                    stampFile.deleteFile();
                else
                    stampFile.replaceWithText (inFlight.joinIntoString ("\n") + "\n");
            };

            auto markInFlight = [&] (const juce::String& file)
            {
                const std::scoped_lock lock (stampMutex);
                inFlight.addIfNotAlreadyThere (file);
                rewriteStamp();
            };

            auto clearInFlight = [&] (const juce::String& file)
            {
                const std::scoped_lock lock (stampMutex);
                inFlight.removeString (file);
                rewriteStamp();
            };

            auto appendFailedLocked = [&] (const juce::String& line)
            {
                appendFailed (line);
            };

            setStatus ("Scanning with " + juce::String (workerCount) + " workers...");

            std::vector<std::thread> workers;
            workers.reserve ((size_t) workerCount);

            for (int w = 0; w < workerCount; ++w)
            {
                workers.emplace_back ([&, w]
                {
                    juce::ignoreUnused (w);

                    juce::AudioPluginFormatManager workerFormats;
                    juce::addDefaultFormatsToManager (workerFormats);

                    juce::AudioPluginFormat* workerAu = nullptr;
                    for (auto* format : workerFormats.getFormats())
                    {
                        if (format != nullptr && format->getName().containsIgnoreCase ("AudioUnit"))
                        {
                            workerAu = format;
                            break;
                        }
                    }

                    if (workerAu == nullptr)
                        return;

                    OutOfProcessPluginScanner scanner (&shared.cancelRequested, kAuPluginScanTimeoutMs);

                    for (;;)
                    {
                        if (shared.cancelRequested.load() || threadShouldExit())
                            break;

                        const int index = nextIndex.fetch_add (1);
                        if (index >= toScan.size())
                            break;

                        const auto file = toScan[index];
                        const auto niceName = displayNameForPluginId (*workerAu, file);

                        {
                            const juce::ScopedLock sl (shared.lock);
                            shared.currentPluginId = file;
                            shared.statusMessage = "Scanning (" + juce::String (completedCount.load())
                                                   + "/" + juce::String (toScan.size()) + "): " + niceName;
                        }

                        markInFlight (file);

                        {
                            const std::scoped_lock lock (listMutex);
                            auto crashedPlugins = readLinesFile (pedal);
                            crashedPlugins.removeString (file);
                            crashedPlugins.add (file);
                            writeLinesFile (pedal, crashedPlugins);
                        }

                        juce::OwnedArray<juce::PluginDescription> typesFound;
                        const bool workerOk = scanner.findPluginTypesFor (*workerAu, typesFound, file);

                        {
                            const std::scoped_lock lock (listMutex);
                            auto crashedPlugins = readLinesFile (pedal);
                            crashedPlugins.removeString (file);
                            writeLinesFile (pedal, crashedPlugins);

                            if (typesFound.isEmpty())
                            {
                                if (! list.getBlacklistedFiles().contains (file))
                                    list.addToBlacklist (file);
                            }
                            else
                            {
                                for (auto* desc : typesFound)
                                    if (desc != nullptr)
                                        list.addType (*desc);
                            }
                        }

                        clearInFlight (file);

                        if (typesFound.isEmpty())
                        {
                            {
                                const std::scoped_lock lock (skipMutex);
                                if (! skipList.contains (file))
                                {
                                    skipList.add (file);
                                    AuPluginScanner::saveSkipList (dataRoot, skipList);
                                }
                            }

                            if (! workerOk)
                                appendFailedLocked ("Crashed or timed out (subprocess): " + niceName);
                            else
                                appendFailedLocked ("Failed: " + niceName);
                        }

                        const int done = ++completedCount;
                        shared.progress = (double) done / (double) total;
                    }

                    scanner.scanFinished();
                });
            }

            for (auto& worker : workers)
                worker.join();

            stampFile.deleteFile();

            if (shared.cancelRequested.load() || threadShouldExit())
            {
                error = "Scan cancelled";
                return false;
            }

            shared.progress = 1.0;
            setStatus ("Saving plugin cache...");

            if (! AuPluginScanner::saveCache (dataRoot, list, error))
                return false;

            setStatus ("Scan complete (" + juce::String (workerCount) + " workers, out-of-process)");
            return true;
        }

        juce::File dataRoot;
        juce::KnownPluginList& list;
        ScanSharedState& shared;
    };
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

bool AuPluginScanner::saveCache (const juce::File& dataRoot, const juce::KnownPluginList& list, juce::String& error)
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

    if (! xml->writeTo (cacheFile (dataRoot)))
    {
        error = "Failed to write plugin cache";
        return false;
    }

    return true;
}

juce::StringArray AuPluginScanner::loadSkipList (const juce::File& dataRoot)
{
    return readLinesFile (skipListFile (dataRoot));
}

void AuPluginScanner::saveSkipList (const juce::File& dataRoot, const juce::StringArray& skips)
{
    writeLinesFile (skipListFile (dataRoot), skips);
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

    auto crashed = readLinesFile (deadMansPedalFile (dataRoot));
    if (crashed.contains (pluginId))
    {
        crashed.removeString (pluginId);
        writeLinesFile (deadMansPedalFile (dataRoot), crashed);
    }

    auto inProgress = readLinesFile (inProgressStampFile (dataRoot));
    if (inProgress.contains (pluginId))
    {
        inProgress.removeString (pluginId);
        if (inProgress.isEmpty())
            inProgressStampFile (dataRoot).deleteFile();
        else
            writeLinesFile (inProgressStampFile (dataRoot), inProgress);
    }
}

juce::String AuPluginScanner::displayNameForPluginId (const juce::String& pluginId)
{
    juce::AudioPluginFormatManager formatManager;
    juce::addDefaultFormatsToManager (formatManager);

    for (auto* format : formatManager.getFormats())
    {
        if (format == nullptr || ! format->getName().containsIgnoreCase ("AudioUnit"))
            continue;

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

    juce::AudioPluginFormat* auFormat = nullptr;
    for (auto* format : formatManager.getFormats())
    {
        if (format != nullptr && format->getName().containsIgnoreCase ("AudioUnit"))
        {
            auFormat = format;
            break;
        }
    }

    if (auFormat == nullptr)
    {
        error = "AudioUnit format is not available in this build";
        addToSkipList (dataRoot, pluginId);
        return false;
    }

    const auto niceName = displayNameForPluginId (pluginId);
    const auto pedal = deadMansPedalFile (dataRoot);

    {
        auto crashedPlugins = readLinesFile (pedal);
        crashedPlugins.removeString (pluginId);
        crashedPlugins.add (pluginId);
        writeLinesFile (pedal, crashedPlugins);
    }

    inProgressStampFile (dataRoot).replaceWithText (pluginId + "\n");

    OutOfProcessPluginScanner scanner (nullptr, kAuPluginScanTimeoutMs);
    juce::OwnedArray<juce::PluginDescription> typesFound;
    const bool workerOk = scanner.findPluginTypesFor (*auFormat, typesFound, pluginId);
    scanner.scanFinished();

    inProgressStampFile (dataRoot).deleteFile();

    {
        auto crashedPlugins = readLinesFile (pedal);
        crashedPlugins.removeString (pluginId);
        writeLinesFile (pedal, crashedPlugins);
    }

    if (! workerOk || typesFound.isEmpty())
    {
        addToSkipList (dataRoot, pluginId);
        if (typesFound.isEmpty())
            list.addToBlacklist (pluginId);

        juce::String ignore;
        saveCache (dataRoot, list, ignore);

        error = "Retry failed for " + niceName
                + (workerOk ? " (no types found)" : " (scan worker crashed or timed out)");
        return false;
    }

    for (auto* desc : typesFound)
        if (desc != nullptr)
            list.addType (*desc);

    juce::String saveError;
    if (! saveCache (dataRoot, list, saveError))
    {
        error = saveError;
        return false;
    }

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
            return true;
    }

    dataRoot.createDirectory();

    ScanSharedState shared;
    AuScanDialog dialog (shared);
    AuScanThread thread (dataRoot, list, shared);
    thread.startThread (juce::Thread::Priority::normal);

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Scanning Audio Units";
    options.dialogBackgroundColour = parentForDialog != nullptr
                                         ? parentForDialog->findColour (juce::ResizableWindow::backgroundColourId)
                                         : juce::Colour (0xff2b2b2b);
    options.content.setNonOwned (&dialog);
    options.componentToCentreAround = parentForDialog;
    options.escapeKeyTriggersCloseButton = false;
    options.useNativeTitleBar = true;
    options.resizable = true;

    const int result = options.runModal();
    juce::ignoreUnused (result);
    shared.cancelRequested = true;
    // Don't force-kill if stuck inside a plugin — leave stamp for next-launch skip.
    thread.waitForThreadToExit (3000);

    if (! shared.ok.load())
    {
        const juce::ScopedLock sl (shared.lock);
        error = shared.error.isNotEmpty() ? shared.error : "AU scan failed or was cancelled";
        // Still try to load whatever was cached if a previous cache exists.
        juce::String loadError;
        if (cacheExists (dataRoot) && loadCache (dataRoot, list, loadError))
            return true;
        return false;
    }

    return true;
}
