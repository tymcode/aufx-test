#pragma once

#include <JuceHeader.h>
#include <atomic>

/**
 * Host MIDI clock generation, AudioPlayHead position, and metronome click.
 */
class HostClockMetronome
{
public:
    enum class PendingTransport { none, start, stop, continue_ };

    HostClockMetronome (juce::AudioFormatManager& formatManager, double& deviceSampleRate);

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

    void resetHostClockTiming();
    void applyPendingHostClockTransport (juce::MidiBuffer& midi);
    void generateHostClockMidi (juce::MidiBuffer& midi, int numSamples);
    void mixMetronomeClick (juce::AudioBuffer<float>& buffer, int numSamples);

    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const;

    void requestTransport (PendingTransport transport) { pendingTransport.store (transport); }
    bool isHostClockPlaying() const { return hostClockPlaying.load(); }
    void stopHostClockPlayback();
    void advancePlayHead (int numSamples) { playHeadSamples.fetch_add (numSamples); }

private:
    juce::AudioFormatManager& formatManager;
    double& deviceSampleRate;

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
