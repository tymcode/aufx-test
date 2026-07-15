#pragma once

#include <JuceHeader.h>

namespace ParameterHelpers
{
    void applyOverrides (juce::AudioPluginInstance& plugin,
                         const std::vector<std::pair<juce::String, juce::String>>& overrides,
                         juce::String& error);

    juce::String dumpParametersJson (juce::AudioPluginInstance& plugin);
}
