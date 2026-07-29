#pragma once

#include <JuceHeader.h>

/** Extra CoreMIDI metadata for matching sysex device modules. */
struct MidiEndpointInfo
{
    juce::String identifier;
    juce::String name;
    juce::String manufacturer;
    juce::String model;
    bool isInput { false };
    bool isOutput { false };
};

juce::Array<MidiEndpointInfo> getMidiInputEndpointInfos();
juce::Array<MidiEndpointInfo> getMidiOutputEndpointInfos();
MidiEndpointInfo findMidiEndpointInfo (const juce::String& identifier, bool preferOutput);
