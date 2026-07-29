#pragma once

#include <JuceHeader.h>
#include <atomic>
#include "HardwareLoopSettings.h"

/**
 * The audio core of AU Effects Explorer. One instance owns:
 *
 *  - the hosted plugin (AU/VST3) and its lifecycle (load, preset save/restore,
 *    editor creation with the suspend-during-UI-init workaround),
 *  - the CoreAudio device (via juce::AudioDeviceManager) and the realtime
 *    render callback that feeds the fixture clip through the plugin,
 *  - the optional *hardware insert loop*: a send stereo pair (to an external
 *    effects box), a return stereo pair (back from the box), a delay line for
 *    latency compensation, and a crossfaded software/hardware monitor switch,
 *  - an optional *second* output device ("monitor output") so playthrough can
 *    be routed to a macOS Multi-Output Device (e.g. interface + BlackHole)
 *    for screen recording — see the monitorDeviceManager notes below,
 *  - MIDI in/out for control surfaces, host clock generation, and sysex
 *    dump/restore of external hardware state.
 *
 * Threading model: the UI (message thread) calls the setters, which are all
 * atomics or take processLock; audioDeviceIOCallbackWithContext runs on the
 * CoreAudio realtime thread and only reads those atomics. Blocking operations
 * (latency detect, hardware capture) run on the message thread and pump the
 * dispatch loop while the audio callback records — see captureHardwareToFile.
 */
class PluginAudioEngine : public juce::AudioIODeviceCallback,
                          private juce::MidiInputCallback,
                          private juce::AudioPlayHead
{
public:
    PluginAudioEngine();
    ~PluginAudioEngine() override;

    bool loadPlugin (const juce::File& pluginFile, juce::String& error);
    bool loadPlugin (const juce::PluginDescription& description, juce::String& error);
    bool loadPreset (const juce::File& presetFile, juce::String& error);
    bool saveCurrentPreset (const juce::File& presetFile, juce::String& error) const;

    juce::AudioPluginInstance* getPlugin() const { return plugin.get(); }
    juce::AudioProcessorEditor* createEditor();
    void destroyEditor (juce::AudioProcessorEditor*& editor);
    juce::String getCurrentPluginName() const;

    bool loadFixture (const juce::File& fixtureFile, juce::String& error);
    void playFixture();
    void stopFixture();
    bool isPlaying() const { return playing.load(); }

    /** Loop the source clip (default) or play it once as a one-shot. */
    void setLooping (bool shouldLoop) { looping.store (shouldLoop); }
    bool isLooping() const { return looping.load(); }

    /**
     * Host bypass: crossfades between dry (pre-plugin) and wet (post-plugin) over
     * a few milliseconds to avoid clicks. Reset to off whenever a plugin is reloaded.
     */
    void setBypassed (bool shouldBypass);
    bool isBypassed() const { return bypassed.load(); }

    /** Dry/wet mix: 0 = source clip only, 1 = fully processed (default). */
    void setMixAmount (float amount);
    float getMixAmount() const { return mixAmount.load(); }

    /** Input send level in dB (-inf mute .. +6 dB, default 0 dB). */
    void setSendLevelDb (float decibels);
    float getSendLevelDb() const { return sendLevelDb.load(); }

    juce::File getCurrentFixtureFile() const { return currentFixtureFile; }

    bool startAudioDevice (juce::String& error);
    void stopAudioDevice();

    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }
    const juce::AudioDeviceManager& getDeviceManager() const { return deviceManager; }
    double getDeviceSampleRate() const { return deviceSampleRate; }
    int getDeviceBlockSize() const { return deviceBlockSize; }

    // --- Hardware insert loop ---
    void setHardwareLoopSettings (const HardwareLoopSettings& settings);
    HardwareLoopSettings getHardwareLoopSettings() const;
    bool hasHardwareLoopConfigured() const;

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
     * Play impulseFile once through the send pair, record the return, find the
     * correlation peak, and update latencySamples. Sets measuredLoopGainDb.
     */
    bool autoDetectLatency (const juce::File& impulseFile,
                            int& outLatencySamples,
                            float& outLoopGainDb,
                            juce::String& error);

    /**
     * Record the return pair while playing fixtureFile through the send pair.
     * Trims configured latency from the head and extends through silence tail.
     *
     * BLOCKING KLUDGE: this runs on the message thread and pumps the dispatch
     * loop in 10 ms slices while the realtime callback does the actual play/
     * record (LoopOp::capture). A worker thread would be cleaner, but the
     * capture UI is modal anyway and the message pump keeps the progress
     * dialog and its Cancel button responsive with far less machinery.
     * TODO: move to a juce::ThreadWithProgressWindow if this ever needs to be
     * non-modal.
     *
     * End-of-capture detection, in priority order:
     *  1. cancelRequested — treated as "stop and save what we have" (returns
     *     false only if nothing usable was recorded yet),
     *  2. targetDurationSeconds > 0 — stop shortly after that much audio is
     *     usable. Used by "Capture Both": the offline software render tells us
     *     how long the hardware take should be. Needed because analog return
     *     paths have a noise floor that can defeat silence detection,
     *  3. silence tail — classic "quiet for N seconds after the clip ended",
     *  4. stall detection — if the record position stops advancing for ~30 s
     *     the device is wedged; bail instead of spinning forever.
     */
    bool captureHardwareToFile (const juce::File& fixtureFile,
                                const juce::File& outputFile,
                                double tailSilenceSeconds,
                                double silenceThresholdDb,
                                double maxTailSeconds,
                                juce::String& error,
                                const std::atomic<bool>* cancelRequested = nullptr,
                                double targetDurationSeconds = 0.0);

    /** Available hardware/virtual MIDI inputs (Audio MIDI Setup style names). */
    juce::Array<juce::MidiDeviceInfo> getMidiInputDevices() const;
    juce::Array<juce::MidiDeviceInfo> getMidiOutputDevices() const;
    juce::StringArray getSelectedMidiInputIdentifiers() const { return selectedMidiIdentifiers; }
    juce::StringArray getSelectedMidiInputNames() const;
    /** Enable these MIDI inputs and merge their messages into the plugin. Empty clears selection. */
    void setMidiInputDevices (const juce::StringArray& identifiers);

    bool setMidiOutputDevice (const juce::String& identifier, juce::String& error);
    juce::String getMidiOutputIdentifier() const { return midiOutputIdentifier; }
    bool sendMidiMessage (const juce::MidiMessage& message);
    bool sendMidiMessages (const juce::Array<juce::MidiMessage>& messages);

    /** Collect the next sysex dump matching isAcceptable (timeout in ms). */
    bool waitForSysexDump (std::function<bool (const juce::MidiMessage&)> isAcceptable,
                           juce::MidiMessage& outMessage,
                           int timeoutMs,
                           juce::String& error);

    /** True if MIDI arrived since the previous call (for the activity LED). */
    bool consumeMidiActivity();

    /** DAW-surface Play/Stop requests for source-clip playback (polled on the message thread). */
    bool consumeTransportPlayRequest();
    bool consumeTransportStopRequest();

    /** When enabled, generates MIDI clock + transport like a DAW in playback. */
    void setHostClockEnabled (bool enabled);
    bool isHostClockEnabled() const { return hostClockEnabled.load(); }
    void setHostClockBpm (double bpm);
    double getHostClockBpm() const { return hostClockBpm.load(); }
    /** True once per emitted quarter note while the host clock is running. */
    bool consumeHostClockQuarterPulse();
    /** Load fixtures/impulse.wav (uses the impulse peak as a one-shot click). */
    bool loadMetronomeClick (const juce::File& impulseFile, juce::String& error);
    void setMetronomeClickEnabled (bool enabled);
    bool isMetronomeClickEnabled() const { return metronomeClickEnabled.load(); }

    /**
     * When true, instruments/samplers get audio input buses enabled and the
     * source clip is fed into those inputs (for sampling). Default false —
     * enabling input without a feed crashes some AUs (DecentSampler, ASR-V).
     */
    void setAllowInstrumentAudioInput (bool allow);
    bool getAllowInstrumentAudioInput() const { return allowInstrumentAudioInput.load(); }

    /**
     * Explicit suspend/resume for the hosted plugin (under processLock).
     * Exists because createEditor() intentionally leaves DSP suspended while
     * heavyweight (WebView/Cocoa) editors initialise; callers that rebuild an
     * editor while audio is already running must resume afterwards or the
     * plugin stays silent — see MainWindow::recreatePluginEditor().
     */
    void setPluginProcessingSuspended (bool shouldSuspend);

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;

private:
    enum class PendingTransport { none, start, stop, continue_ };

    void fillFixtureBlock (juce::AudioBuffer<float>& buffer, int numSamples);
    void clearMidiInput();
    void applyMidiInputSelection();
    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override;
    void applyPendingHostClockTransport (juce::MidiBuffer& midi);
    void generateHostClockMidi (juce::MidiBuffer& midi, int numSamples);
    void resetHostClockTiming();
    void mixMetronomeClick (juce::AudioBuffer<float>& buffer, int numSamples);
    void applyBypassCrossfade (juce::AudioBuffer<float>& wetBuffer,
                               const juce::AudioBuffer<float>& dryBuffer,
                               int numSamples);
    void applyHardwareMonitorCrossfade (juce::AudioBuffer<float>& softwareBuffer,
                                        const juce::AudioBuffer<float>& hardwareBuffer,
                                        int numSamples);
    void ensureLatencyBufferSize (int numChannels, int capacity);
    void pushReturnToLatencyBuffer (const float* const* inputChannelData, int numInputChannels, int numSamples);
    void readDelayedReturn (juce::AudioBuffer<float>& dest, int numSamples);
    void clearOutputChannels (float* const* outputChannelData, int numOutputChannels, int numSamples);
    void writeMonitorSamples (const juce::AudioBuffer<float>& stereoMonitor, int numSamples,
                              float* const* outputChannelData, int numOutputChannels);
    void pushMonitorOutput (const float* left, const float* right, int numSamples);
    void pullMonitorOutput (float* const* outputChannelData, int numOutputChannels, int numSamples);
    bool startMonitorOutput (juce::String& error);
    void stopMonitorOutput();
    bool openConfiguredAudioDevice (juce::String& error);
    static PendingTransport classifyTransportMessage (const juce::MidiMessage& message);

    // --- Separate monitor output device ------------------------------------
    // Why a second AudioDeviceManager: the loop device (e.g. UA Apollo) is
    // opened exclusively for send/return, so writing playthrough to its
    // monitor pair bypasses macOS system audio entirely — screen recorders
    // capturing via a Multi-Output Device (interface + BlackHole) hear
    // nothing from this app. When HardwareLoopSettings names a separate
    // monitor output, playthrough is pushed into a lock-free FIFO on the loop
    // device's callback and pulled by this second device's callback.
    // The two devices free-run on independent clocks; drift is tolerated
    // because monitoring is non-critical (the FIFO under/overruns manifest as
    // an occasional dropout, never as corruption of the captured audio).
    // TODO: add a resampling bridge if long screen-recording sessions drift
    // audibly.
    struct MonitorOutputHandler;
    std::unique_ptr<MonitorOutputHandler> monitorOutputHandler;
    juce::AudioDeviceManager monitorDeviceManager;
    juce::AudioBuffer<float> monitorRingBuffer;
    juce::AbstractFifo monitorFifo { 32768 };
    std::atomic<bool> monitorOutputActive { false };

    // AudioPlayHead: how a DAW communicates tempo/transport to the plugin.
    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override;
    bool canControlTransport() override { return true; }

    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::AudioDeviceManager deviceManager;
    juce::AudioFormatManager formatManager;

    juce::AudioBuffer<float> fixtureBuffer;
    double fixtureSampleRate { 44100.0 };
    double fixtureReadPosition { 0.0 };
    juce::File currentFixtureFile;
    std::atomic<bool> playing { false };
    std::atomic<bool> looping { true };
    std::atomic<bool> bypassed { false };
    float bypassFade { 0.0f };
    int bypassFadeLengthSamples { 441 };
    std::atomic<float> mixAmount { 1.0f };
    std::atomic<float> sendLevelDb { 0.0f };
    std::atomic<float> sendGain { 1.0f };

    double deviceSampleRate { 44100.0 };
    int deviceBlockSize { 512 };
    juce::CriticalSection processLock;
    std::atomic<bool> restoringState { false };

    juce::StringArray selectedMidiIdentifiers;
    juce::CriticalSection midiLock;
    juce::MidiBuffer pendingMidi;
    std::atomic<bool> midiActivity { false };
    std::atomic<bool> transportPlayRequest { false };
    std::atomic<bool> transportStopRequest { false };

    std::unique_ptr<juce::MidiOutput> midiOutput;
    juce::String midiOutputIdentifier;
    juce::CriticalSection sysexLock;
    juce::Array<juce::MidiMessage> pendingSysex;
    std::atomic<bool> collectSysex { false };

    std::atomic<bool> hostClockEnabled { false };
    std::atomic<bool> hostClockPlaying { false };
    std::atomic<double> hostClockBpm { 120.0 };
    std::atomic<PendingTransport> pendingTransport { PendingTransport::none };
    std::atomic<bool> quarterNotePulse { false };
    double clockSampleCounter { 0.0 };
    int clockTicksSinceQuarter { 0 };
    std::atomic<juce::int64> playHeadSamples { 0 };

    std::atomic<bool> metronomeClickEnabled { false };
    juce::AudioBuffer<float> metronomeClickBuffer;
    int metronomeClickPosition { -1 };
    int pendingClickOffset { -1 };

    std::atomic<bool> allowInstrumentAudioInput { false };

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
    enum class LoopOp { idle, calibrate, capture };
    std::atomic<LoopOp> loopOp { LoopOp::idle };
    juce::AudioBuffer<float> loopPlayBuffer;
    int loopPlayPosition { 0 };
    juce::AudioBuffer<float> loopRecordBuffer;
    int loopRecordPosition { 0 };
    int loopRecordCapacity { 0 };
    std::atomic<bool> loopOpFinished { false };
};
