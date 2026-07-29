#include "QuadraverbSysex.h"

namespace
{
    // F0 00 00 0E 02 03 64 F7 — edit buffer dump request (original Quadraverb / Plus)
    constexpr uint8_t dumpRequestBytes[] = { 0xf0, 0x00, 0x00, 0x0e, 0x02, 0x03, 0x64, 0xf7 };

    juce::MidiMessage buildParameterEdit (uint8_t functionGroup, uint8_t page, uint16_t value)
    {
        // QV/QV+ "MIDI Editing" packed value format for command 0x01.
        const uint8_t value1 = (uint8_t) ((value >> 7) & 0x7f);
        const uint8_t value2 = (uint8_t) (((value & 0x7f) << 1) & 0x7f);
        constexpr uint8_t value3 = 0x00;
        const uint8_t data[] = { 0x00, 0x00, 0x0e, 0x02, 0x01, functionGroup, page, value1, value2, value3 };
        return juce::MidiMessage::createSysExMessage (data, (int) sizeof (data));
    }
}

bool QuadraverbSysex::matches (const juce::String& manufacturer,
                               const juce::String& model,
                               const juce::String& deviceName) const
{
    const auto haystack = (manufacturer + " " + model + " " + deviceName).toLowerCase();

    if (haystack.contains ("quadraverb"))
        return true;

    // CoreMIDI sometimes only exposes the USB/MIDI interface name.
    if (haystack.contains ("alesis") && (haystack.contains ("qv") || haystack.contains ("quad")))
        return true;

    return false;
}

juce::MidiMessage QuadraverbSysex::buildDumpRequest() const
{
    return juce::MidiMessage::createSysExMessage (dumpRequestBytes + 1, (int) sizeof (dumpRequestBytes) - 2);
}

bool QuadraverbSysex::looksLikeAlesisHeader (const juce::MidiMessage& message)
{
    if (! message.isSysEx())
        return false;

    const auto* data = message.getSysExData();
    const int n = message.getSysExDataSize();
    // JUCE strips F0/F7: data starts at manufacturer ID
    return n >= 5
        && (uint8_t) data[0] == 0x00
        && (uint8_t) data[1] == 0x00
        && (uint8_t) data[2] == 0x0e
        && (uint8_t) data[3] == 0x02;
}

bool QuadraverbSysex::isDumpResponse (const juce::MidiMessage& message) const
{
    if (! looksLikeAlesisHeader (message))
        return false;

    const auto* data = message.getSysExData();
    // Opcode 0x01 = program dump (response to request 0x03) on original Quadraverb
    return message.getSysExDataSize() >= 5 && ((uint8_t) data[4] == 0x01 || (uint8_t) data[4] == 0x02);
}

bool QuadraverbSysex::validateDump (const juce::MidiMessage& message) const
{
    return isDumpResponse (message) && message.getSysExDataSize() > 8;
}

juce::Array<juce::MidiMessage> QuadraverbSysex::restoreDump (const juce::MidiMessage& dump) const
{
    juce::Array<juce::MidiMessage> messages;
    if (dump.isSysEx())
        messages.add (dump);
    return messages;
}

juce::Array<juce::MidiMessage> QuadraverbSysex::buildDryThruMessages() const
{
    // Mix function group (8), config 0 mapping:
    // page 0 direct source type (0 = PRE-EQ),
    // page 1 direct level (0..99),
    // page 2 master effects level (0..99),
    // pages 3/4/5 pitch/delay/reverb levels (0..99).
    juce::Array<juce::MidiMessage> messages;
    messages.add (buildParameterEdit (0x08, 0x00, 0));  // Direct source: PRE-EQ
    messages.add (buildParameterEdit (0x08, 0x01, 99)); // Direct level: full
    messages.add (buildParameterEdit (0x08, 0x02, 0));  // Master FX: off
    messages.add (buildParameterEdit (0x08, 0x03, 0));  // Pitch: off
    messages.add (buildParameterEdit (0x08, 0x04, 0));  // Delay: off
    messages.add (buildParameterEdit (0x08, 0x05, 0));  // Reverb: off
    return messages;
}
