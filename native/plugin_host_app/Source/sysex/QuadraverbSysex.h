#pragma once

#include "SysexDeviceModule.h"

/** Original Alesis Quadraverb / Quadraverb Plus edit-buffer dump (product ID 0x02). */
class QuadraverbSysex final : public SysexDeviceModule
{
public:
    juce::String getDisplayName() const override { return "Alesis Quadraverb"; }

    bool matches (const juce::String& manufacturer,
                  const juce::String& model,
                  const juce::String& deviceName) const override;

    juce::MidiMessage buildDumpRequest() const override;
    bool isDumpResponse (const juce::MidiMessage& message) const override;
    bool validateDump (const juce::MidiMessage& message) const override;
    juce::Array<juce::MidiMessage> restoreDump (const juce::MidiMessage& dump) const override;
    juce::Array<juce::MidiMessage> buildDryThruMessages() const override;

private:
    static bool looksLikeAlesisHeader (const juce::MidiMessage& message);
};
