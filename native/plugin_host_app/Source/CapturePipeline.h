#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include "HostConfig.h"
#include "PluginAudioEngine.h"

/**
 * Shared mechanical capture of software (offline plugin render) and/or
 * hardware (latency-trimmed loop record + optional sysex dump).
 *
 * Used today by TestCaseCapture. Planned HW/SW compare + reporting will call
 * the same pipeline, then run analysis on the resulting WAV pair — keep
 * dialog/session-registration concerns out of this class so both entry points
 * can share the render/record path without duplicating it.
 */
struct CaptureArtifactPaths
{
    juce::File captureDir;
    juce::String stem;
    juce::File presetFile;
    juce::File softwareOutput;
    juce::File hardwareOutput;
    juce::File sysexFile;
};

enum class CaptureSource
{
    plugin = 0,
    hardware = 1,
    both = 2
};

struct CapturePipelineRequest
{
    juce::String description;
    int roleIndex { 2 }; // broken
    CaptureSource source { CaptureSource::plugin };
    juce::File fixtureFile;
    juce::Component* progressParent { nullptr };
};

struct CapturePipelineResult
{
    CaptureArtifactPaths paths;
    bool capturedPlugin { false };
    bool capturedHardware { false };
    bool capturedSysex { false };
    double softwareDurationSeconds { 0.0 };
};

/** RAII: restore hardware-mode flag and refresh UI on scope exit. */
class HardwareModeGuard
{
public:
    using RefreshFn = std::function<void()>;

    HardwareModeGuard (PluginAudioEngine& engineIn, RefreshFn refreshIn)
        : engine (engineIn),
          refresh (std::move (refreshIn)),
          restoreHardwareMode (engine.isHardwareMode())
    {
    }

    ~HardwareModeGuard()
    {
        engine.setHardwareMode (restoreHardwareMode);
        if (refresh)
            refresh();
    }

    void showMode (bool hardwareMode)
    {
        if (engine.isHardwareMode() != hardwareMode)
            engine.setHardwareMode (hardwareMode);
        if (refresh)
            refresh();
    }

private:
    PluginAudioEngine& engine;
    RefreshFn refresh;
    bool restoreHardwareMode;
};

class CapturePipeline
{
public:
    using RefreshUiFn = std::function<void()>;

    CapturePipeline (PluginAudioEngine& audioEngine,
                     HostConfig& hostConfig,
                     RefreshUiFn refreshHardwareUi);

    /** Build artifact directory + stem filenames for a capture/compare run. */
    CaptureArtifactPaths makeArtifactPaths (const HostPluginEntry& plugin,
                                            const juce::String& description,
                                            int roleIndex) const;

    /**
     * Run software and/or hardware capture according to request.source.
     * Does not register a session snapshot — callers decide how to persist.
     */
    bool run (const CapturePipelineRequest& request,
              const HostPluginEntry& plugin,
              CapturePipelineResult& outResult,
              juce::String& error);

    bool renderSoftware (const juce::File& fixtureFile,
                         const juce::File& presetOut,
                         const juce::File& softwareOut,
                         double& outDurationSeconds,
                         juce::String& error);

    bool recordHardware (const juce::File& fixtureFile,
                         const juce::File& hardwareOut,
                         double targetDurationSeconds,
                         juce::Component* progressParent,
                         juce::String& error);

    bool dumpHardwareSysex (const juce::File& sysexOut, juce::String& error);

    static double wavDurationSeconds (const juce::File& wavFile);

private:
    PluginAudioEngine& engine;
    HostConfig& config;
    RefreshUiFn refreshHardwareUi;
};
