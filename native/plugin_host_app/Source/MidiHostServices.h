#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include "HostClockMetronome.h"

/**
 * MIDI I/O, sysex collection, and DAW-surface transport classification.
 * Implements MidiInputCallback so PluginAudioEngine can register this object
 * with juce::AudioDeviceManager.
 */
class MidiHostServices : public juce::MidiInputCallback
{
public:
    MidiHostServices (juce::AudioDeviceManager& deviceManager, HostClockMetronome& hostClock);

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

    void clearMidiInput();
    void applyMidiInputSelection();

    /** Swap pending inbound MIDI into dest (audio thread). */
    void swapPendingMidi (juce::MidiBuffer& dest);

    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override;

private:
    static HostClockMetronome::PendingTransport classifyTransportMessage (const juce::MidiMessage& message);

    juce::AudioDeviceManager& deviceManager;
    HostClockMetronome& hostClock;

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
};
