#pragma once

#include <JuceHeader.h>
#include <vector>

struct CommandLineOptions
{
    juce::File pluginPath;
    juce::File inputPath;
    juce::File outputPath;
    juce::File presetPath;
    std::vector<std::pair<juce::String, juce::String>> paramOverrides;
    double sampleRate = 0.0;
    int blockSize = 512;
    double tailSilenceSeconds = 1.0;
    double silenceThresholdDb = -60.0;
    double maxTailSeconds = 120.0;
    bool dumpParameters = false;
    bool jsonOutput = false;

    bool parse (const juce::StringArray& args, juce::String& error);
    bool validateForRender (juce::String& error) const;
    bool validateForDump (juce::String& error) const;
};
