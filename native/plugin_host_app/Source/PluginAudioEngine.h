#pragma once

#include <JuceHeader.h>
#include <atomic>

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

    /** Available hardware/virtual MIDI inputs (Audio MIDI Setup style names). */
    juce::Array<juce::MidiDeviceInfo> getMidiInputDevices() const;
    juce::StringArray getSelectedMidiInputIdentifiers() const { return selectedMidiIdentifiers; }
    juce::StringArray getSelectedMidiInputNames() const;
    /** Enable these MIDI inputs and merge their messages into the plugin. Empty clears selection. */
    void setMidiInputDevices (const juce::StringArray& identifiers);
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
    static PendingTransport classifyTransportMessage (const juce::MidiMessage& message);

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
};
