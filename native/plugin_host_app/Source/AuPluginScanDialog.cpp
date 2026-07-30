#include "AuPluginScanDialog.h"
#include "AuPluginScanner.h"
#include "PluginScannerOOP.h"
#include "PluginScanLog.h"
#include "HostFileUtils.h"
#include "HostLog.h"
#include "HostPreferences.h"
#include "Utf8.h"

#include <thread>
#include <vector>
#include <chrono>

namespace
{
    int scanTimeoutMs()
    {
        return HostPreferences::get().getPluginScanTimeoutMs();
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
            const auto scanStarted = std::chrono::steady_clock::now();
            PluginScanLog scanLog (dataRoot);

            juce::AudioPluginFormatManager formatManager;
            juce::addDefaultFormatsToManager (formatManager);

            juce::AudioPluginFormat* auFormat = HostFileUtils::findAudioUnitFormat (formatManager);

            if (auFormat == nullptr)
            {
                error = "AudioUnit format is not available in this build";
                return false;
            }

            // Recover from a previous hang: stamp left behind => permanent skip.
            const auto stampFile = AuPluginScanner::inProgressStampFile (dataRoot);
            auto skipList = AuPluginScanner::loadSkipList (dataRoot);
            int preSkippedCount = 0;

            if (stampFile.existsAsFile())
            {
                for (const auto& hungId : HostFileUtils::readLinesFile (stampFile))
                {
                    if (hungId.isEmpty())
                        continue;

                    if (! skipList.contains (hungId))
                    {
                        skipList.add (hungId);
                        AuPluginScanner::saveSkipList (dataRoot, skipList);
                    }
                    ++preSkippedCount;
                    appendFailed ("Skipped (hung last time): "
                                  + displayNameForPluginId (*auFormat, hungId));
                }
                stampFile.deleteFile();
            }

            // Also treat dead-man's-pedal leftovers as permanent skips.
            for (const auto& crashed : HostFileUtils::readLinesFile (AuPluginScanner::deadMansPedalFile (dataRoot)))
            {
                if (crashed.isNotEmpty() && ! skipList.contains (crashed))
                {
                    skipList.add (crashed);
                    ++preSkippedCount;
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
                    ++preSkippedCount;
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

            const int timeoutMs = scanTimeoutMs();

            scanLog.logScanStart (workerCount,
                                  timeoutMs,
                                  files.size(),
                                  toScan.size(),
                                  preSkippedCount);

            HostLog::info ("AU scan started: workers=" + juce::String (workerCount)
                           + " timeout_ms=" + juce::String (timeoutMs)
                           + " discovered=" + juce::String (files.size())
                           + " to_scan=" + juce::String (toScan.size())
                           + " pre_skipped=" + juce::String (preSkippedCount));

            std::atomic<int> nextIndex { 0 };
            std::atomic<int> completedCount { 0 };
            std::atomic<int> succeededCount { 0 };
            std::atomic<int> failedCount { 0 };
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

                    juce::AudioPluginFormat* workerAu = HostFileUtils::findAudioUnitFormat (workerFormats);

                    if (workerAu == nullptr)
                        return;

                    OutOfProcessPluginScanner scanner (&shared.cancelRequested, timeoutMs);

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
                            auto crashedPlugins = HostFileUtils::readLinesFile (pedal);
                            crashedPlugins.removeString (file);
                            crashedPlugins.add (file);
                            HostFileUtils::writeLinesFile (pedal, crashedPlugins);
                        }

                        juce::OwnedArray<juce::PluginDescription> typesFound;
                        const bool workerOk = scanner.findPluginTypesFor (*workerAu, typesFound, file);
                        const int durationMs = scanner.getLastScanDurationMs();
                        const auto failureReason = scanner.getLastFailureReason();

                        {
                            const std::scoped_lock lock (listMutex);
                            auto crashedPlugins = HostFileUtils::readLinesFile (pedal);
                            crashedPlugins.removeString (file);
                            HostFileUtils::writeLinesFile (pedal, crashedPlugins);

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
                            ++failedCount;

                            juce::String outcome = workerOk ? "no_types"
                                                            : oopScanFailureReasonString (failureReason);
                            scanLog.logPluginResult (w, file, niceName, outcome, durationMs, 0);

                            {
                                const std::scoped_lock lock (skipMutex);
                                if (! skipList.contains (file))
                                {
                                    skipList.add (file);
                                    AuPluginScanner::saveSkipList (dataRoot, skipList);
                                }
                            }

                            if (! workerOk)
                            {
                                if (failureReason == OopScanFailureReason::timeout)
                                    appendFailedLocked ("Timed out (subprocess): " + niceName);
                                else if (failureReason == OopScanFailureReason::connectionLost)
                                    appendFailedLocked ("Subprocess crashed or lost: " + niceName);
                                else if (failureReason == OopScanFailureReason::sendFailed)
                                    appendFailedLocked ("Failed to start scan worker: " + niceName);
                                else
                                    appendFailedLocked ("Crashed or timed out (subprocess): " + niceName);
                            }
                            else
                                appendFailedLocked ("Failed (no types): " + niceName);
                        }
                        else
                        {
                            ++succeededCount;

                            const juce::String outcome = durationMs >= 10000 ? "ok_slow" : "ok";
                            if (durationMs >= 10000)
                                scanLog.logPluginResult (w, file, niceName, outcome, durationMs, typesFound.size());
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

            const auto scanDurationMs = std::chrono::duration_cast<std::chrono::milliseconds> (
                                            std::chrono::steady_clock::now() - scanStarted)
                                            .count();

            juce::String failedLogCopy;
            {
                const juce::ScopedLock sl (shared.lock);
                failedLogCopy = shared.failedLog;
            }

            if (shared.cancelRequested.load() || threadShouldExit())
            {
                scanLog.logScanEnd (false,
                                    true,
                                    succeededCount.load(),
                                    failedCount.load(),
                                    preSkippedCount,
                                    scanDurationMs,
                                    failedLogCopy);
                HostLog::info ("AU scan cancelled after "
                               + juce::String (scanDurationMs) + " ms (succeeded="
                               + juce::String (succeededCount.load()) + " failed="
                               + juce::String (failedCount.load()) + ")");
                error = "Scan cancelled";
                return false;
            }

            shared.progress = 1.0;
            setStatus ("Saving plugin cache...");

            AuPluginScanner::ScanRunStats stats;
            stats.workerCount = workerCount;
            stats.timeoutMs = timeoutMs;
            stats.discovered = files.size();
            stats.preSkipped = preSkippedCount;
            stats.scanned = toScan.size();
            stats.succeeded = succeededCount.load();
            stats.failed = failedCount.load();
            stats.durationMs = scanDurationMs;

            if (! AuPluginScanner::saveCache (dataRoot, list, error, &stats))
            {
                scanLog.logScanEnd (false,
                                    false,
                                    stats.succeeded,
                                    stats.failed,
                                    preSkippedCount,
                                    scanDurationMs,
                                    failedLogCopy);
                HostLog::error ("AU scan cache save failed: " + error);
                return false;
            }

            scanLog.logScanEnd (true,
                                false,
                                stats.succeeded,
                                stats.failed,
                                preSkippedCount,
                                scanDurationMs,
                                failedLogCopy);

            HostLog::info ("AU scan finished: succeeded=" + juce::String (stats.succeeded)
                           + " failed=" + juce::String (stats.failed)
                           + " pre_skipped=" + juce::String (preSkippedCount)
                           + " duration_ms=" + juce::String (stats.durationMs)
                           + " cached_types=" + juce::String (list.getTypes().size()));

            setStatus ("Scan complete (" + juce::String (workerCount) + " workers, out-of-process)");
            return true;
        }

        juce::File dataRoot;
        juce::KnownPluginList& list;
        ScanSharedState& shared;
    };
} // namespace

bool AuPluginScanDialog::runModalScan (const juce::File& dataRoot,
                                       juce::KnownPluginList& list,
                                       juce::Component* parentForDialog,
                                       juce::String& error)
{
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
        HostLog::error ("AU scan did not complete: " + error);
        return false;
    }

    return true;
}
