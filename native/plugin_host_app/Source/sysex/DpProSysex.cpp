#include "DpProSysex.h"

bool DpProSysex::matches (const juce::String& manufacturer,
                          const juce::String& model,
                          const juce::String& deviceName) const
{
    const auto haystack = (manufacturer + " " + model + " " + deviceName).toLowerCase();

    if (haystack.contains ("dp/pro") || haystack.contains ("dp pro") || haystack.contains ("dppro"))
        return true;

    // CoreMIDI sometimes only exposes the USB/MIDI interface name.
    if (haystack.contains ("ensoniq") && haystack.contains ("pro"))
        return true;

    return false;
}

juce::String DpProSysex::manualDumpInstructions() const
{
    // Parameter 19 wording from the reference manual; dump types match DumpType.
    return "On the DP/Pro: press System/MIDI, select parameter 19 "
           "(ENTER to Send SysEx Dump), choose the dump type on the lower "
           "line (Selected Effect = current program), then press ENTER.";
}

bool DpProSysex::isDumpResponse (const juce::MidiMessage& message) const
{
    if (! message.isSysEx())
        return false;

    // No public spec for the bytes after the manufacturer ID, so accept any
    // Ensoniq sysex — the capture flow only listens while a dump is expected.
    const auto* data = message.getSysExData();
    return message.getSysExDataSize() >= 4
        && (uint8_t) data[0] == dppro::kManufacturerId;
}

bool DpProSysex::validateDump (const juce::MidiMessage& message) const
{
    // A real program dump carries at least the header plus parameter data;
    // reject obvious runts (e.g. a stray handshake) without a byte-level spec.
    return isDumpResponse (message) && message.getSysExDataSize() > 16;
}

juce::Array<juce::MidiMessage> DpProSysex::restoreDump (const juce::MidiMessage& dump) const
{
    // The DP/Pro reloads its own dumps verbatim (reference manual, parameter
    // 19 notes): embedded Device ID must match parameter 6 and Accept SysEx
    // Msgs (parameter 11) must be Yes.
    juce::Array<juce::MidiMessage> messages;
    if (dump.isSysEx())
        messages.add (dump);
    return messages;
}

juce::Array<juce::MidiMessage> DpProSysex::buildDryThruMessages() const
{
    return buildBypassMessages (true);
}

juce::Array<juce::MidiMessage> DpProSysex::buildSelectEffectMessages (int bank, int program,
                                                                      int systemChannel)
{
    juce::Array<juce::MidiMessage> messages;
    if (bank < 0 || bank >= (int) dppro::kNumEffectBanks
        || program < 0 || program >= (int) dppro::kProgramsPerBank)
        return messages;

    const int ch = juce::jlimit (1, 16, systemChannel);
    messages.add (juce::MidiMessage::controllerEvent (ch, dppro::kBankSelectMsbCc,
                                                      dppro::kBankSelectMsbValue));
    messages.add (juce::MidiMessage::controllerEvent (ch, dppro::kBankSelectLsbCc, bank));
    messages.add (juce::MidiMessage::programChange (ch, program));
    return messages;
}

juce::Array<juce::MidiMessage> DpProSysex::buildBypassMessages (bool engage, int systemChannel)
{
    // Bypass both ESPs via their assignable bypass controllers. With
    // ESP-A/B Bypass Btn set to BypassMode::bypass the audio path stays
    // through the converters and DSP with processing off — the state
    // latency calibration needs. Requires the one-time device setup in
    // the header (Byp A/B Cntlr = #80/#81).
    const int ch = juce::jlimit (1, 16, systemChannel);
    const int value = engage ? dppro::kBypassOnValue : dppro::kBypassOffValue;

    juce::Array<juce::MidiMessage> messages;
    messages.add (juce::MidiMessage::controllerEvent (ch, dppro::kBypassEspAController, value));
    messages.add (juce::MidiMessage::controllerEvent (ch, dppro::kBypassEspBController, value));
    return messages;
}
