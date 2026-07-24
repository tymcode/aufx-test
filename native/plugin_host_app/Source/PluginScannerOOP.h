#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>

/** Shared command-line UID for ChildProcessCoordinator / Worker (no spaces). */
inline constexpr const char* kAuPluginScannerProcessUID = "aueffectsexplorerscan";

/** Default per-plugin budget before the child worker is torn down as hung. */
inline constexpr int kAuPluginScanTimeoutMs = 15000;

/**
 * If this process was launched as an out-of-process AU scanner worker, connect
 * and take over. Returns true when the app should stay in worker mode (no UI).
 */
bool tryInitialiseAsPluginScannerWorker (const juce::String& commandLine);

#if JUCE_MAC
/** Sets a distinct process name for the OOP scanner worker (Activity Monitor / logs). */
void aufxSetScannerWorkerProcessName();
#endif

/** Out-of-process CustomScanner (AudioPluginHost pattern). */
class OutOfProcessPluginScanner final : public juce::KnownPluginList::CustomScanner
{
public:
    explicit OutOfProcessPluginScanner (std::atomic<bool>* cancelFlag = nullptr,
                                        int pluginTimeoutMs = kAuPluginScanTimeoutMs);
    ~OutOfProcessPluginScanner() override;

    bool findPluginTypesFor (juce::AudioPluginFormat& format,
                             juce::OwnedArray<juce::PluginDescription>& result,
                             const juce::String& fileOrIdentifier) override;

    void scanFinished() override;

private:
    class Superprocess;

    bool addPluginDescriptions (const juce::String& formatName,
                                const juce::String& fileOrIdentifier,
                                juce::OwnedArray<juce::PluginDescription>& result);

    std::unique_ptr<Superprocess> superprocess;
    std::atomic<bool>* cancelFlag { nullptr };
    int pluginTimeoutMs { kAuPluginScanTimeoutMs };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutOfProcessPluginScanner)
};
