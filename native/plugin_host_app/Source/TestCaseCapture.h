#pragma once

#include <JuceHeader.h>
#include <functional>
#include "CapturePipeline.h"
#include "HostConfig.h"
#include "PluginAudioEngine.h"
#include "SourceClipLibrary.h"

/**
 * Capture Test Case UI: prompt for description/role/source, run CapturePipeline,
 * then register a session snapshot.
 *
 * Source clip selection goes through SourceClipLibrary so Load Testcase can
 * inject arbitrary snapshot input audio via the same path.
 */
class TestCaseCapture
{
public:
    using StatusFn = std::function<void (const juce::String&, bool)>;
    using GetPluginEntryFn = std::function<const HostPluginEntry&()>;
    using PopulateHardwareStatesFn = std::function<void()>;
    using SetLightsOutFn = std::function<void (bool)>;

    TestCaseCapture (PluginAudioEngine& audioEngine,
                     HostConfig& hostConfig,
                     StatusFn statusFn,
                     GetPluginEntryFn getPluginEntry,
                     CapturePipeline::RefreshUiFn refreshHardwareUi,
                     PopulateHardwareStatesFn populateHardwareStates,
                     SetLightsOutFn setLightsOut);

    void prompt (juce::Component* parent,
                 SourceClipLibrary& sourceClips,
                 juce::ComboBox& fixtureBox);

    void capture (const juce::String& snapshotName, int roleIndex, int sourceIndex,
                  bool calibrateNoiseFloor,
                  bool generateReport,
                  bool captureSoftwareSettings,
                  bool captureHardwareSettings,
                  const juce::File& fixtureFile,
                  juce::Component* progressParent);

    CapturePipeline& getPipeline() { return pipeline; }

private:
    /** Run `aufx-test compare --root … --write-report` for a registered snapshot. */
    void runCompareReport (const juce::String& sessionName,
                           const juce::String& snapshotId,
                           const juce::String& snapshotName);

    PluginAudioEngine& engine;
    HostConfig& config;
    StatusFn setStatus;
    GetPluginEntryFn getCurrentPlugin;
    PopulateHardwareStatesFn populateHardwareStates;
    SetLightsOutFn setLightsOut;
    CapturePipeline pipeline;

    juce::String lastCaptureDescription { "snapshot" };
    int lastCaptureRoleIndex { 2 };
    int lastCaptureSourceIndex { 0 };
};
