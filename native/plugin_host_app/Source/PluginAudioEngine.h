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

    juce::File getCurrentFixtureFile() const { return currentFixtureFile; }

    bool startAudioDevice (juce::String& error);
    void stopAudioDevice();

    /** Available hardware/virtual MIDI inputs (Audio MIDI Setup style names). */
    juce::Array<juce::MidiDeviceInfo> getMidiInputDevices() const;
    juce::String getSelectedMidiInputIdentifier() const { return selectedMidiIdentifier; }
    juce::String getSelectedMidiInputName() const;
    /** Enable only this MIDI input and route it into the plugin. Empty clears selection. */
    void setMidiInputDevice (const juce::String& identifier);
    /** True if MIDI arrived since the previous call (for the activity LED). */
    bool consumeMidiActivity();

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

    double deviceSampleRate { 44100.0 };
    int deviceBlockSize { 512 };
    juce::CriticalSection processLock;
    std::atomic<bool> restoringState { false };

    juce::String selectedMidiIdentifier;
    juce::CriticalSection midiLock;
    juce::MidiBuffer pendingMidi;
    std::atomic<bool> midiActivity { false };

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
};
