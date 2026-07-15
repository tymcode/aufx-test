#include "AUpresetSaver.h"

bool AUpresetSaver::saveStateBytes (const juce::File& presetFile,
                                    const juce::MemoryBlock& state,
                                    juce::String& error)
{
    juce::ignoreUnused (presetFile, state, error);
    error = "Saving .aupreset is only supported on macOS";
    return false;
}
