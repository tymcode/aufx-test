#include "MidiHostServices.h"
#include "HostAudioHelpers.h"

MidiHostServices::MidiHostServices (juce::AudioDeviceManager& deviceManagerIn,
                                    HostClockMetronome& hostClockIn)
    : deviceManager (deviceManagerIn),
      hostClock (hostClockIn)
{
}

juce::Array<juce::MidiDeviceInfo> MidiHostServices::getMidiInputDevices() const
{
    return juce::MidiInput::getAvailableDevices();
}

juce::Array<juce::MidiDeviceInfo> MidiHostServices::getMidiOutputDevices() const
{
    return juce::MidiOutput::getAvailableDevices();
}

juce::StringArray MidiHostServices::getSelectedMidiInputNames() const
{
    juce::StringArray names;
    const auto available = juce::MidiInput::getAvailableDevices();

    for (const auto& id : selectedMidiIdentifiers)
        for (const auto& device : available)
            if (device.identifier == id)
                names.add (device.name);

    return names;
}

void MidiHostServices::clearMidiInput()
{
    for (const auto& id : selectedMidiIdentifiers)
    {
        if (id.isEmpty())
            continue;

        deviceManager.removeMidiInputDeviceCallback (id, this);
        deviceManager.setMidiInputDeviceEnabled (id, false);
    }

    const juce::ScopedLock lock (midiLock);
    pendingMidi.clear();
}

void MidiHostServices::applyMidiInputSelection()
{
    for (const auto& id : selectedMidiIdentifiers)
    {
        if (id.isEmpty())
            continue;

        deviceManager.setMidiInputDeviceEnabled (id, true);
        deviceManager.addMidiInputDeviceCallback (id, this);
    }
}

void MidiHostServices::setMidiInputDevices (const juce::StringArray& identifiers)
{
    clearMidiInput();
    selectedMidiIdentifiers.clear();

    for (const auto& id : identifiers)
        if (id.isNotEmpty() && ! selectedMidiIdentifiers.contains (id))
            selectedMidiIdentifiers.add (id);

    applyMidiInputSelection();
}

bool MidiHostServices::consumeMidiActivity()
{
    return midiActivity.exchange (false);
}

bool MidiHostServices::consumeTransportPlayRequest()
{
    return transportPlayRequest.exchange (false);
}

bool MidiHostServices::consumeTransportStopRequest()
{
    return transportStopRequest.exchange (false);
}

void MidiHostServices::swapPendingMidi (juce::MidiBuffer& dest)
{
    const juce::ScopedLock lock (midiLock);
    dest.swapWith (pendingMidi);
}

void MidiHostServices::collectRemoteSysex (const juce::MidiMessage& message)
{
    if (message.isSysEx() && collectSysex.load())
    {
        const juce::ScopedLock lock (sysexLock);
        pendingSysex.add (message);
    }

    midiActivity.store (true);
}

bool MidiHostServices::setMidiOutputDevice (const juce::String& identifier, juce::String& error)
{
    midiOutput.reset();
    midiOutputIdentifier.clear();

    if (identifier.isEmpty())
        return true;

    midiOutput = juce::MidiOutput::openDevice (identifier);
    if (midiOutput == nullptr)
    {
        error = "Failed to open MIDI output device";
        return false;
    }

    midiOutputIdentifier = identifier;
    return true;
}

bool MidiHostServices::sendMidiMessage (const juce::MidiMessage& message)
{
    if (midiOutput == nullptr)
        return false;

    midiOutput->sendMessageNow (message);
    return true;
}

bool MidiHostServices::sendMidiMessages (const juce::Array<juce::MidiMessage>& messages)
{
    if (midiOutput == nullptr)
        return false;

    for (const auto& message : messages)
        midiOutput->sendMessageNow (message);

    return true;
}

/**
 * Blocking wait (message thread) for a sysex dump that satisfies the caller's
 * predicate — used for "dump current program" round-trips with external
 * hardware. Pumps the dispatch loop while waiting so MIDI callbacks and the
 * UI stay alive; same pattern as autoDetectLatency / captureHardwareToFile.
 */
bool MidiHostServices::waitForSysexDump (std::function<bool (const juce::MidiMessage&)> isAcceptable,
                                         juce::MidiMessage& outMessage,
                                         int timeoutMs,
                                         juce::String& error)
{
    {
        const juce::ScopedLock lock (sysexLock);
        pendingSysex.clear();
    }

    collectSysex.store (true);
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + (double) timeoutMs;

    while (juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        {
            const juce::ScopedLock lock (sysexLock);
            for (int i = 0; i < pendingSysex.size(); ++i)
            {
                if (isAcceptable (pendingSysex.getReference (i)))
                {
                    outMessage = pendingSysex.getReference (i);
                    pendingSysex.clear();
                    collectSysex.store (false);
                    return true;
                }
            }
        }

        HostAudioHelpers::pumpMessageThreadMs (10);
    }

    collectSysex.store (false);
    error = "Timed out waiting for sysex dump";
    return false;
}

void MidiHostServices::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    if (message.isSysEx() && collectSysex.load())
    {
        const juce::ScopedLock lock (sysexLock);
        pendingSysex.add (message);
    }

    bool forward = true;

    // Transport from MIDI realtime (Start/Stop/Continue), MMC, or Mackie/HUI notes.
    // Oxygen Pro DAW mode (Mackie / Mackie/HUI) sends notes 93/94 on the HUI port
    // (e.g. iConnectMIDI "HST" ports), not MIDI Start/Stop.
    const auto transport = classifyTransportMessage (message);
    if (transport != HostClockMetronome::PendingTransport::none)
    {
        if (hostClock.isHostClockEnabled())
        {
            hostClock.requestTransport (transport);
            forward = false;
        }

        // Always drive source-clip play/stop so DAW buttons do something audible
        // even when Host Clock is off.
        if (transport == HostClockMetronome::PendingTransport::start
            || transport == HostClockMetronome::PendingTransport::continue_)
            transportPlayRequest.store (true);
        else if (transport == HostClockMetronome::PendingTransport::stop)
            transportStopRequest.store (true);
    }
    else if (hostClock.isHostClockEnabled() && hostClock.isHostClockPlaying() && message.isMidiClock())
    {
        forward = false;
    }

    if (forward)
    {
        const juce::ScopedLock lock (midiLock);
        pendingMidi.addEvent (message, 0);
    }

    midiActivity.store (true);
}

HostClockMetronome::PendingTransport MidiHostServices::classifyTransportMessage (const juce::MidiMessage& message)
{
    if (message.isMidiStart())
        return HostClockMetronome::PendingTransport::start;
    if (message.isMidiContinue())
        return HostClockMetronome::PendingTransport::continue_;
    if (message.isMidiStop())
        return HostClockMetronome::PendingTransport::stop;

    // Mackie Control / Logic Control transport (ch. 1 notes).
    // Stop = 93, Play = 94. Accept any channel — some surfaces remap.
    if (message.isNoteOn (false) && message.getVelocity() > 0)
    {
        switch (message.getNoteNumber())
        {
            case 94: return HostClockMetronome::PendingTransport::start;
            case 93: return HostClockMetronome::PendingTransport::stop;
            default: break;
        }
    }

    // MMC: F0 7F <dev> 06 <cmd> F7 — Play=02, Deferred Play=03, Continue=04, Stop=01
    if (message.isSysEx())
    {
        const auto* data = message.getSysExData();
        const int n = message.getSysExDataSize();
        if (n >= 4 && (uint8_t) data[0] == 0x7f && (uint8_t) data[2] == 0x06)
        {
            switch ((uint8_t) data[3])
            {
                case 0x01: return HostClockMetronome::PendingTransport::stop;
                case 0x02: return HostClockMetronome::PendingTransport::start;
                case 0x03: return HostClockMetronome::PendingTransport::start;
                case 0x04: return HostClockMetronome::PendingTransport::continue_;
                default: break;
            }
        }
    }

    return HostClockMetronome::PendingTransport::none;
}
