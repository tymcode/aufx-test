#pragma once

#include <JuceHeader.h>
#include <atomic>

/**
 * Hosts the network audio transport plugin (SonoBus) that carries send/return
 * audio (and, with the MIDI-enabled SonoBus fork, MIDI) to a remote hardware
 * rig. PluginAudioEngine owns one instance and treats it as the "remote"
 * transport of the external insert loop:
 *
 *   sendBuffer -> processBlock (transport plugin) -> return pushed into
 *   HardwareLoopOps' latency delay line, exactly where the CoreAudio
 *   hardware return would land.
 *
 * The plugin keeps its own network threads alive, so processBlock must run
 * every callback while loaded (fed silence when the send is not driven) or
 * the peer stream stalls.
 *
 * Threading: load/unload/applyState run on the message thread and take the
 * engine's processLock; processBlock/swapPendingMidi run on the audio thread
 * with the caller already holding processLock.
 */
class RemoteTransport
{
public:
    /** SonoBus AU as registered by the local build (aumf = music effect, accepts MIDI). */
    static constexpr const char* defaultPluginIdentifier = "AudioUnit:Effects/aumf,NBus,Sono";

    explicit RemoteTransport (juce::CriticalSection& processLockIn) : processLock (processLockIn) {}

    bool load (const juce::String& fileOrIdentifier, double sampleRate, int blockSize, juce::String& error);
    void unload();
    bool isLoaded() const { return loaded.load(); }
    juce::String getPluginName() const;

    /** Call when the audio device (re)starts; safe if not loaded. */
    void prepare (double sampleRate, int blockSize);
    /** Call when the audio device stops; safe if not loaded. */
    void release();

    /**
     * Audio thread (caller holds processLock): run the stereo block through
     * the transport plugin in place. Input = send to the remote peer,
     * output = the remote peer's return. The plugin may use more channels
     * internally; an internal scratch buffer handles the width mismatch.
     */
    void processBlock (juce::AudioBuffer<float>& stereoBuffer, juce::MidiBuffer& midi);

    /** Audio thread: move queued outbound MIDI into dest (cleared first). */
    void swapPendingMidi (juce::MidiBuffer& dest);

    /** Message thread: queue MIDI for the next processBlock (Phase 3 remote MIDI). */
    void queueMidiMessage (const juce::MidiMessage& message);
    void queueMidiMessages (const juce::Array<juce::MidiMessage>& messages);

    /** Message thread. Editor is owned by the caller (dialog); destroy before unload. */
    juce::AudioProcessorEditor* createEditor();
    juce::AudioPluginInstance* getPlugin() const { return plugin.get(); }

    bool getState (juce::MemoryBlock& out) const;
    bool applyState (const juce::MemoryBlock& state);

private:
    /**
     * Fresh instances monitor their own input ("Dry Level" = 1), which would
     * feed the dry send straight back into the return. Zero it so the return
     * is the remote peer only, matching hardware-insert semantics.
     */
    void muteDryMonitor();

    juce::CriticalSection& processLock;
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    std::atomic<bool> loaded { false };
    bool prepared { false };
    double lastSampleRate { 44100.0 };
    int lastBlockSize { 512 };

    juce::AudioBuffer<float> scratchBuffer;
    juce::CriticalSection midiLock;
    juce::MidiBuffer pendingMidi;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RemoteTransport)
};
