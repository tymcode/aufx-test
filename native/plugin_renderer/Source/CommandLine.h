#pragma once

#include <JuceHeader.h>
#include <vector>

/**
 * CLI options for plugin_renderer, the headless render tool the Python
 * aufx-test package shells out to (see src/aufx_test/subprocess_host.py).
 * Two modes: offline render (--input/--output/--preset) and parameter dump
 * (--dump-parameters).
 */
struct CommandLineOptions
{
    // A juce::String, NOT a juce::File: --plugin accepts either a bundle path
    // (/Library/.../Foo.component) or a JUCE plugin identifier such as
    // "AudioUnit:Effects/aufx,QDV1,TDSP". Storing it as a File used to mangle
    // identifiers into nonexistent absolute paths.
    juce::String pluginRef;
    juce::File inputPath;
    juce::File outputPath;
    juce::File presetPath;
    juce::File savePresetPath;
    int programIndex = -1; // -1 = leave unchanged
    std::vector<std::pair<juce::String, juce::String>> paramOverrides;
    double sampleRate = 0.0;
    int blockSize = 512;
    double tailSilenceSeconds = 1.0;
    double silenceThresholdDb = -60.0;
    double maxTailSeconds = 120.0;
    bool dumpParameters = false;
    bool listPrograms = false;
    bool jsonOutput = false;

    bool parse (const juce::StringArray& args, juce::String& error);
    bool validateForRender (juce::String& error) const;
    bool validateForDump (juce::String& error) const;
    bool validateForSavePreset (juce::String& error) const;
};
