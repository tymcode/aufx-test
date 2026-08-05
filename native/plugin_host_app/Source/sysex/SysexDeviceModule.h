#pragma once

#include <JuceHeader.h>

/**
 * Pluggable MIDI sysex dump/restore for a hardware effects device.
 *
 * One subclass per device family (Alesis Quadraverb, Ensoniq DP/Pro).
 * The host uses a module to (a) snapshot the box's program alongside audio
 * captures, (b) restore a saved .syx to the box, and (c) optionally force a
 * "dry thru" state for latency calibration on devices whose bypass is an
 * analog relay rather than DSP-through.
 *
 * Modules are registered in SysexDeviceRegistry's constructor; to support a
 * new device, subclass this and add one registerModule() line there, plus
 * the .cpp to CMakeLists.txt.
 *
 * Devices whose dump protocol is not publicly documented (the DP/Pro) can
 * still participate: return false from canRequestDump() and the capture
 * flow prompts the user to start the dump from the device's front panel
 * instead of polling, waiting dumpTimeoutMs() for it to arrive.
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

    /** False when the device cannot be polled and the user must start the
        dump from its front panel; buildDumpRequest() is not called then. */
    virtual bool canRequestDump() const { return true; }

    /** One-line front-panel instruction shown while waiting for a manual dump. */
    virtual juce::String manualDumpInstructions() const { return {}; }

    /** Wait budget for the dump response; manual dumps need human time. */
    virtual int dumpTimeoutMs() const { return 5000; }

    virtual juce::MidiMessage buildDumpRequest() const = 0;
    /** Request all user programs (bank). Empty message if unsupported. */
    virtual juce::MidiMessage buildBulkDumpRequest() const { return {}; }
    virtual bool isDumpResponse (const juce::MidiMessage& message) const = 0;
    /** True when message is a full all-programs bank dump. */
    virtual bool isBulkDumpResponse (const juce::MidiMessage&) const { return false; }
    virtual bool validateDump (const juce::MidiMessage& message) const = 0;

    /** Messages to send to restore a previously captured dump (usually verbatim). */
    virtual juce::Array<juce::MidiMessage> restoreDump (const juce::MidiMessage& dump) const = 0;

    /** Optional: messages that force DSP-through "dry thru" monitoring for calibration/capture. */
    virtual juce::Array<juce::MidiMessage> buildDryThruMessages() const { return {}; }
};
