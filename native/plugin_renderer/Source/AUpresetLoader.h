#pragma once

#include <JuceHeader.h>

class AUpresetLoader
{
public:
    /** Extract the plugin's raw state blob from an .aupreset (AU ClassInfo plist). */
    static bool loadStateBytes (const juce::File& presetFile,
                                juce::MemoryBlock& outState,
                                juce::String& error);

    /**
     * Unwrap host getStateInformation() output into the processor's raw state bytes.
     * Accepts either a bare JUCE copyXmlToBinary blob, or an AU ClassInfo plist
     * (what AudioUnitPluginFormat returns) containing jucePluginState/data/state.
     */
    static bool extractStateBytes (const juce::MemoryBlock& hostOrPluginState,
                                   juce::MemoryBlock& outState,
                                   juce::String& error);
};
