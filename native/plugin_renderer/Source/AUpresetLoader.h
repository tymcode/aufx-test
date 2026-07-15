#pragma once

#include <JuceHeader.h>

class AUpresetLoader
{
public:
    static bool loadStateBytes (const juce::File& presetFile,
                                juce::MemoryBlock& outState,
                                juce::String& error);
};
