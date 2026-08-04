#pragma once

#include <JuceHeader.h>
#include "../domain/PatchContextManager.h"

namespace qverse
{

struct ProjectState
{
    PatchContextManager contexts;
    juce::File patchSaveDirectory;
    bool hardwareMode = false;
    juce::String windowState;
    juce::var randomizationSettings; // reserved for Randomizer phase
};

struct ProjectFile
{
    static bool save (const juce::File& file, const ProjectState& state, juce::String& error);
    static bool load (const juce::File& file, ProjectState& state, juce::String& error);
};

} // namespace qverse
