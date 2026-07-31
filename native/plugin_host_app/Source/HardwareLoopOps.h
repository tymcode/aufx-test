#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include "HardwareLoopSettings.h"
#include "MonitorOutputBridge.h"

/**
 * Hardware insert loop: latency delay line, monitor crossfade, and
 * message-thread loop ops (calibrate / capture).
 *
 * Calibration Boost: interactive Level Meters (View menu) owns named
 * level-sweep linearity. Latency auto-detect averages several impulse
 * trials. Capture Calibrate measures noise floor + DC offset. Still
 * deferred here: L/R balance, LUFS gain, and a calibrate→render→compare
 * loop. Keep correlation and peak-based loop gain as private helpers.
 */
class HardwareLoopOps
{
public:
    HardwareLoopOps (juce::AudioFormatManager& formatManager,
                     juce::CriticalSection& processLock,
                     std::atomic<float>& sendGain,
                     double& deviceSampleRate,
                     int& deviceBlockSize);

    void setHardwareLoopSettings (const HardwareLoopSettings& settings);
    HardwareLoopSettings getHardwareLoopSettings() const { return hardwareSettings; }
    const HardwareLoopSettings& getHardwareLoopSettingsRef() const { return hardwareSettings; }
    bool hasHardwareLoopConfigured() const { return hardwareSettings.isConfigured(); }

    /** Crossfaded A/B between plugin monitor and latency-compensated hardware return. */
    void setHardwareMode (bool shouldUseHardware);
    bool isHardwareMode() const { return hardwareMode.load(); }

    float getReturnPeakLevel() const { return juce::jmax (returnPeakL.load(), returnPeakR.load()); }
    float getReturnPeakL() const { return returnPeakL.load(); }
    float getReturnPeakR() const { return returnPeakR.load(); }
    float getReturnRmsLevel() const { return returnRms.load(); }
    float getSendRmsLevel() const { return sendRms.load(); }
    float getSendPeakL() const { return sendPeakL.load(); }
    float getSendPeakR() const { return sendPeakR.load(); }

    /**
     * When true, fixture audio is still sent to the hardware loop but is not
     * fed into the software plugin (used while Hardware Audio Setup is open).
     */
    void setSoftwareEffectMuted (bool shouldMute) { softwareEffectMuted.store (shouldMute); }
    bool isSoftwareEffectMuted() const { return softwareEffectMuted.load(); }

    /**
     * Play impulseFile through the send pair several times, average the
     * correlation-peak latencies (and peak loop gains), and update
     * latencySamples. Sets measuredLoopGainDb from the averaged gain.
     * Optional onProgress is called on the message thread before each trial
     * with 1-based current and total trial counts.
     */
    bool autoDetectLatency (const juce::File& impulseFile,
                            int& outLatencySamples,
                            float& outLoopGainDb,
                            juce::String& error,
                            std::function<void (int current, int total)> onProgress = {});

    /**
     * Send silence through the send pair and measure return peak/RMS (dBFS)
     * after latency settle, plus per-channel mean DC offset. Peak/RMS are
     * computed after subtracting the measured DC. Used by NoiseFloorCalibration
     * before hardware capture.
     */
    bool measureReturnNoiseFloor (double listenSeconds,
                                  float& outPeakDb,
                                  float& outRmsDb,
                                  float& outDcOffsetL,
                                  float& outDcOffsetR,
                                  juce::String& error);

    /**
     * Record the return pair while playing fixtureFile through the send pair.
     * Trims configured latency from the head and extends through silence tail.
     * Optional dcOffsetL/R (from Calibrate) are subtracted before writing.
     *
     * BLOCKING KLUDGE: this runs on the message thread and pumps the dispatch
     * loop in 10 ms slices while the realtime callback does the actual play/
     * record (LoopOp::capture). A worker thread would be cleaner, but the
     * capture UI is modal anyway and the message pump keeps the progress
     * dialog and its Stop / Cancel buttons responsive with far less machinery.
     * TODO: move to a juce::ThreadWithProgressWindow if this ever needs to be
     * non-modal.
     *
     * End-of-capture detection, in priority order:
     *  1. abortRequested — discard the take (returns false),
     *  2. stopRequested — stop recording and save what we have (returns false
     *     only if nothing usable was recorded yet),
     *  3. targetDurationSeconds > 0 — stop shortly after that much audio is
     *     usable. Used by "Capture Both": the offline software render tells us
     *     how long the hardware take should be. Needed because analog return
     *     paths have a noise floor that can defeat silence detection. Do not
     *     fall back to the dry fixture length — hardware-only takes must wait
     *     for silence or a manual Stop so reverb tails are not cut short,
     *  4. silence tail — classic "quiet for N seconds after the clip ended",
     *  5. stall detection — if the record position stops advancing for ~30 s
     *     the device is wedged; bail instead of spinning forever.
     */
    bool captureHardwareToFile (const juce::File& fixtureFile,
                                const juce::File& outputFile,
                                double tailSilenceSeconds,
                                double silenceThresholdDb,
                                double maxTailSeconds,
                                juce::String& error,
                                const std::atomic<bool>* stopRequested = nullptr,
                                const std::atomic<bool>* abortRequested = nullptr,
                                double targetDurationSeconds = 0.0,
                                float dcOffsetL = 0.0f,
                                float dcOffsetR = 0.0f);

    // Realtime helpers called from PluginAudioEngine::audioDeviceIOCallbackWithContext:
    void ensureLatencyBufferSize (int numChannels, int capacity);
    void pushReturnToLatencyBuffer (const float* const* inputChannelData, int numInputChannels, int numSamples);
    void readDelayedReturn (juce::AudioBuffer<float>& dest, int numSamples);
    void applyMonitorCrossfade (juce::AudioBuffer<float>& softwareBuffer,
                                const juce::AudioBuffer<float>& hardwareBuffer,
                                int numSamples);

    /**
     * Drive send/record for LoopOp::calibrate or LoopOp::capture.
     * @return true if a loop op consumed this block (caller should return).
     */
    bool processLoopOpInCallback (const float* const* inputChannelData,
                                  int numInputChannels,
                                  float* const* outputChannelData,
                                  int numOutputChannels,
                                  int numSamples,
                                  MonitorOutputBridge& monitorOutput);

    void writeMonitorSamples (const juce::AudioBuffer<float>& stereoMonitor,
                              int numSamples,
                              float* const* outputChannelData,
                              int numOutputChannels,
                              MonitorOutputBridge& monitorOutput);

    void storeSendMeters (float peakL, float peakR, float rms);
    void prepareForAudioDevice (int fadeLengthSamples);

private:
    /** Correlation peak for one impulse trial. */
    static int findCorrelationPeakLatency (const juce::AudioBuffer<float>& impulseMono,
                                           const juce::AudioBuffer<float>& recorded,
                                           int recordedSamples,
                                           int impulseSamples);

    /** Peak-based send/return gain. Calibration Boost will replace with LUFS. */
    static float measurePeakLoopGainDb (const juce::AudioBuffer<float>& impulseMono,
                                        const juce::AudioBuffer<float>& recorded,
                                        int recordedSamples,
                                        int impulseSamples,
                                        int bestLag);

    enum class LoopOp { idle, calibrate, capture };

    juce::AudioFormatManager& formatManager;
    juce::CriticalSection& processLock;
    std::atomic<float>& sendGain;
    double& deviceSampleRate;
    int& deviceBlockSize;

    HardwareLoopSettings hardwareSettings;
    std::atomic<bool> hardwareMode { false };
    // Equal-power crossfade position between software monitor (0) and
    // hardware return (1); ~8 ms ramp, same feel as the bypass button.
    float hardwareFade { 0.0f };
    int hardwareFadeLengthSamples { 441 };
    // Circular delay line holding the hardware return so it can be read back
    // latencySamples late, time-aligning it with the software path.
    // Only touched on the audio thread.
    juce::AudioBuffer<float> latencyBuffer;
    int latencyWritePos { 0 };
    int latencyCapacity { 0 };
    std::atomic<float> returnPeakL { 0.0f };
    std::atomic<float> returnPeakR { 0.0f };
    std::atomic<float> returnRms { 0.0f };
    std::atomic<float> sendRms { 0.0f };
    std::atomic<float> sendPeakL { 0.0f };
    std::atomic<float> sendPeakR { 0.0f };
    std::atomic<bool> softwareEffectMuted { false };

    // Special playback/record modes used by latency detect and hardware
    // capture. While loopOp != idle the audio callback ignores the normal
    // fixture/plugin path and instead streams loopPlayBuffer out of the send
    // pair while recording the return pair into loopRecordBuffer. Buffers are
    // pre-allocated to worst case on the message thread before the op starts,
    // so the audio thread never allocates.
    std::atomic<LoopOp> loopOp { LoopOp::idle };
    juce::AudioBuffer<float> loopPlayBuffer;
    int loopPlayPosition { 0 };
    juce::AudioBuffer<float> loopRecordBuffer;
    int loopRecordPosition { 0 };
    int loopRecordCapacity { 0 };
    std::atomic<bool> loopOpFinished { false };
};
