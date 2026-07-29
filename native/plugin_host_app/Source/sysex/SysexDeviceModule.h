#pragma once

#include <JuceHeader.h>

/**
 * Pluggable MIDI sysex dump/restore for a hardware effects device.
 *
 * One subclass per device family (currently only the Alesis Quadraverb).
 * The host uses a module to (a) snapshot the box's program alongside audio
 * captures, (b) restore a saved .syx to the box, and (c) optionally force a
 * "dry thru" state for latency calibration on devices whose bypass is an
 * analog relay rather than DSP-through.
 *
 * Modules are registered in SysexDeviceRegistry's constructor; to support a
 * new device, subclass this and add one registerModule() line there.
 */
class SysexDeviceModule
{
public:
    virtual ~SysexDeviceModule() = default;

    virtual juce::String getDisplayName() const = 0;

    /** Match Audio MIDI Setup / CoreMIDI manufacturer, model, and device name. */
    virtual bool matches (const juce::String& manufacturer,
                          const juce::String& model,
                          const juce::String& deviceName) const = 0;

    virtual juce::MidiMessage buildDumpRequest() const = 0;
    virtual bool isDumpResponse (const juce::MidiMessage& message) const = 0;
    virtual bool validateDump (const juce::MidiMessage& message) const = 0;

    /** Messages to send to restore a previously captured dump (usually verbatim). */
    virtual juce::Array<juce::MidiMessage> restoreDump (const juce::MidiMessage& dump) const = 0;

    /** Optional: messages that force DSP-through "dry thru" monitoring for calibration/capture. */
    virtual juce::Array<juce::MidiMessage> buildDryThruMessages() const { return {}; }
};
