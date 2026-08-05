#pragma once

#include "DpProParameters.h"
#include "SysexDeviceModule.h"

/**
 * Ensoniq DP/Pro (dual-ESP effects processor, 1997).
 *
 * Ensoniq never published the DP/Pro sysex spec — the reference manual
 * (docs/EnsoniqDP-ProReferenceManual.pdf) points at a mail-order "DP/Pro
 * MIDI System Exclusive Specification" that never surfaced publicly, so
 * unlike the Quadraverb this module cannot poll the box. What *is*
 * documented lives in DpProParameters.h (System/MIDI parameter table,
 * Bank Select / Program Change layout, bypass / tweak controller map,
 * dump-type enum) so a future preset editor has a structured source.
 *
 * Dump / restore behaviour:
 *  - Dumps are started on the device: System/MIDI parameter 19
 *    ("ENTER to Send SysEx Dump"), with the dump type on the lower
 *    display line (DumpType::selectedEffect snapshots the current
 *    program). Any Ensoniq (manufacturer ID 0x0F) sysex received while
 *    waiting is accepted.
 *  - Restore sends the captured dump back verbatim. The DP/Pro reloads
 *    it provided the embedded Device ID matches parameter 6 and Accept
 *    SysEx Msgs (parameter 11) is Yes.
 *
 * Dry-thru uses the documented MIDI bypass (manual p.16): a controller
 * value >= 64 on the system MIDI channel bypasses an ESP. This module's
 * convention is CC 80 = ESP-A and CC 81 = ESP-B on channel 1
 * (dppro::kBypassEspAController / kBypassEspBController). One-time
 * device setup required:
 *   * Byp A Cntlr (parameter 62) = #80, Byp B Cntlr (parameter 63) = #81
 *   * ESP-A/B Bypass Btn (parameters 64/65) = BypassMode::bypass — the
 *     mode that keeps dry audio flowing through the converters and DSP,
 *     which is what latency calibration needs to measure.
 */
class DpProSysex final : public SysexDeviceModule
{
public:
    juce::String getDisplayName() const override { return "Ensoniq DP/Pro"; }

    bool matches (const juce::String& manufacturer,
                  const juce::String& model,
                  const juce::String& deviceName) const override;

    bool canRequestDump() const override { return false; }
    juce::String manualDumpInstructions() const override;
    int dumpTimeoutMs() const override { return 45000; }

    juce::MidiMessage buildDumpRequest() const override { return {}; }
    bool isDumpResponse (const juce::MidiMessage& message) const override;
    bool validateDump (const juce::MidiMessage& message) const override;
    juce::Array<juce::MidiMessage> restoreDump (const juce::MidiMessage& dump) const override;
    juce::Array<juce::MidiMessage> buildDryThruMessages() const override;

    /** MIDI messages that select an effect (Bank Select MSB/LSB + Program Change)
        on the system MIDI channel. Mirrors Chapter 3 "Selecting Effects Via MIDI". */
    static juce::Array<juce::MidiMessage> buildSelectEffectMessages (int bank, int program,
                                                                     int systemChannel = dppro::kDefaultSystemChannel);

    /** Engage or release ESP bypass via the module's assignable-controller convention. */
    static juce::Array<juce::MidiMessage> buildBypassMessages (bool engage,
                                                               int systemChannel = dppro::kDefaultSystemChannel);
};
