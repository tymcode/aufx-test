#pragma once

#include <JuceHeader.h>

class AUpresetSaver
{
public:
    static bool saveStateBytes (const juce::File& presetFile,
                                const juce::MemoryBlock& state,
                                juce::String& error);
};
