#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include "HardwareLoopSettings.h"
#include "HardwareLoopOps.h"
#include "HostClockMetronome.h"
#include "MidiHostServices.h"
#include "MonitorOutputBridge.h"

/**
 * The audio core of AU Effects Explorer. One instance owns:
 *
 *  - the hosted plugin (AU/VST3) and its lifecycle (load, preset save/restore,
 *    editor creation with the suspend-during-UI-init workaround),
 *  - the CoreAudio device (via juce::AudioDeviceManager) and the realtime
 *    render callback that feeds the fixture clip through the plugin,
 *  - collaborator units for the hardware insert loop, optional monitor output,
 *    MIDI I/O / sysex, and host clock / metronome.
 *
 * Threading model: the UI (message thread) calls the setters, which are all
 * atomics or take processLock; audioDeviceIOCallbackWithContext runs on the
 * CoreAudio realtime thread and only reads those atomics. Blocking operations
 * (latency detect, hardware capture) run on the message thread and pump the
 * dispatch loop while the audio callback records — see captureHardwareToFile.
 */
class PluginAudioEngine : public juce::AudioIODeviceCallback,
                          private juce::AudioPlayHead
{
public:
    PluginAudioEngine();
    ~PluginAudioEngine() override;

    bool loadPlugin (const juce::File& pluginFile, juce::String& error);
    bool loadPlugin (const juce::PluginDescription& description, juce::String& error);
    bool loadPreset (const juce::File& presetFile, juce::String& error);
    bool saveCurrentPreset (const juce::File& presetFile, juce::String& error) const;

    /**
     * Apply an in-memory state blob (e.g. QDV-1 XML-in-binary) with the same
     * suspend/reset/updateHostDisplay sequence as loadPreset.
     */
    bool applyPluginState (const juce::MemoryBlock& state, juce::String& error);

    /**
     * Unload and reload the current plugin description so plugin-side stores
     * (e.g. QDV-1 preset library) rescans disk. Preserves fixture path, loop,
     * BPM, host-clock, hardware mode, and restarts the audio device.
     * Caller must destroy any owned plugin editor before calling.
     */
    bool reloadCurrentPlugin (juce::String& error);

    /** Last successfully loaded plugin description (empty if none). */
    juce::PluginDescription getCurrentPluginDescription() const { return lastPluginDescription; }

    /** Fixture read position in seconds (0 if no fixture). */
    double getFixturePositionSeconds() const;
    /** Fixture length in seconds (0 if no fixture). */
    double getFixtureLengthSeconds() const;

    juce::AudioPluginInstance* getPlugin() const { return plugin.get(); }
    juce::AudioProcessorEditor* createEditor();
    void destroyEditor (juce::AudioProcessorEditor*& editor);
    juce::String getCurrentPluginName() const;

    bool loadFixture (const juce::File& audioFile, juce::String& error);
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

    /** Currently loaded source-clip file (library fixture or external/Loaded). */
    juce::File getCurrentFixtureFile() const { return currentFixtureFile; }

    bool startAudioDevice (juce::String& error);
    void stopAudioDevice();

    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }
    const juce::AudioDeviceManager& getDeviceManager() const { return deviceManager; }
    double getDeviceSampleRate() const { return deviceSampleRate; }
    int getDeviceBlockSize() const { return deviceBlockSize; }

    // --- Hardware insert loop (forwarded to HardwareLoopOps) ---
    void setHardwareLoopSettings (const HardwareLoopSettings& settings);
    HardwareLoopSettings getHardwareLoopSettings() const;
    bool hasHardwareLoopConfigured() const;

    /** Crossfaded A/B between plugin monitor and latency-compensated hardware return. */
    void setHardwareMode (bool shouldUseHardware);
    bool isHardwareMode() const { return hardwareLoop.isHardwareMode(); }

    float getReturnPeakLevel() const { return hardwareLoop.getReturnPeakLevel(); }
    float getReturnPeakL() const { return hardwareLoop.getReturnPeakL(); }
    float getReturnPeakR() const { return hardwareLoop.getReturnPeakR(); }
    float getReturnRmsLevel() const { return hardwareLoop.getReturnRmsLevel(); }
    float getSendRmsLevel() const { return hardwareLoop.getSendRmsLevel(); }
    float getSendPeakL() const { return hardwareLoop.getSendPeakL(); }
    float getSendPeakR() const { return hardwareLoop.getSendPeakR(); }

    /** Pre-plugin fixture peaks (after send gain) for software-path metering. */
    float getSoftwareSendPeakL() const { return softwareSendPeakL.load(); }
    float getSoftwareSendPeakR() const { return softwareSendPeakR.load(); }
    /** Post-plugin / post-mix-bypass peaks for software-path metering. */
    float getSoftwareReturnPeakL() const { return softwareReturnPeakL.load(); }
    float getSoftwareReturnPeakR() const { return softwareReturnPeakR.load(); }

    /**
     * When true, fixture audio is still sent to the hardware loop but is not
     * fed into the software plugin (used while Hardware Audio Setup is open).
     */
    void setSoftwareEffectMuted (bool shouldMute) { hardwareLoop.setSoftwareEffectMuted (shouldMute); }
    bool isSoftwareEffectMuted() const { return hardwareLoop.isSoftwareEffectMuted(); }

    /**
     * Play impulseFile through the send pair several times, average the
     * correlation-peak latencies, and update latencySamples.
     * Sets measuredLoopGainDb from the averaged peak loop gain.
     * Optional onProgress is called before each trial (1-based current / total).
     */
    bool autoDetectLatency (const juce::File& impulseFile,
                            int& outLatencySamples,
                            float& outLoopGainDb,
                            juce::String& error,
                            std::function<void (int current, int total)> onProgress = {});

    /**
     * Send silence through the hardware send pair and measure return noise
     * floor (peak/RMS dBFS after DC removal) plus per-channel DC offset.
     * See NoiseFloorCalibration.
     */
    bool measureHardwareNoiseFloor (double listenSeconds,
                                    float& outPeakDb,
                                    float& outRmsDb,
                                    float& outDcOffsetL,
                                    float& outDcOffsetR,
                                    juce::String& error);

    /**
     * Record the return pair while playing fixtureFile through the send pair.
     * Trims configured latency from the head and extends through silence tail.
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

    /** Available hardware/virtual MIDI inputs (Audio MIDI Setup style names). */
    juce::Array<juce::MidiDeviceInfo> getMidiInputDevices() const;
    juce::Array<juce::MidiDeviceInfo> getMidiOutputDevices() const;
    juce::StringArray getSelectedMidiInputIdentifiers() const;
    juce::StringArray getSelectedMidiInputNames() const;
    /** Enable these MIDI inputs and merge their messages into the plugin. Empty clears selection. */
    void setMidiInputDevices (const juce::StringArray& identifiers);

    bool setMidiOutputDevice (const juce::String& identifier, juce::String& error);
    juce::String getMidiOutputIdentifier() const;
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
    bool isHostClockEnabled() const { return hostClock.isHostClockEnabled(); }
    void setHostClockBpm (double bpm);
    double getHostClockBpm() const { return hostClock.getHostClockBpm(); }
    /** True once per emitted quarter note while the host clock is running. */
    bool consumeHostClockQuarterPulse();
    /** Load fixtures/impulse.wav (uses the impulse peak as a one-shot click). */
    bool loadMetronomeClick (const juce::File& impulseFile, juce::String& error);
    void setMetronomeClickEnabled (bool enabled);
    bool isMetronomeClickEnabled() const { return hostClock.isMetronomeClickEnabled(); }

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
    void fillFixtureBlock (juce::AudioBuffer<float>& buffer, int numSamples);
    void applyBypassCrossfade (juce::AudioBuffer<float>& wetBuffer,
                               const juce::AudioBuffer<float>& dryBuffer,
                               int numSamples);
    void clearOutputChannels (float* const* outputChannelData, int numOutputChannels, int numSamples);
    bool openConfiguredAudioDevice (juce::String& error);

    // AudioPlayHead: how a DAW communicates tempo/transport to the plugin.
    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override;
    bool canControlTransport() override { return true; }

    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::PluginDescription lastPluginDescription;
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
    std::atomic<float> softwareSendPeakL { 0.0f };
    std::atomic<float> softwareSendPeakR { 0.0f };
    std::atomic<float> softwareReturnPeakL { 0.0f };
    std::atomic<float> softwareReturnPeakR { 0.0f };

    double deviceSampleRate { 44100.0 };
    int deviceBlockSize { 512 };
    juce::CriticalSection processLock;
    std::atomic<bool> restoringState { false };

    std::atomic<bool> allowInstrumentAudioInput { false };

    // Collaborators (declaration order = construction order; later units
    // take references to earlier members).
    MonitorOutputBridge monitorOutput;
    HostClockMetronome hostClock;
    MidiHostServices midiServices;
    HardwareLoopOps hardwareLoop;
};
