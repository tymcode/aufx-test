#pragma once

#include <JuceHeader.h>

struct OfflineCaptureOptions
{
    double sampleRate { 0.0 };
    int blockSize { 512 };
    double tailSilenceSeconds { 1.0 };
    double silenceThresholdDb { -60.0 };
    double maxTailSeconds { 120.0 };
};

class OfflineCapture
{
public:
    static bool renderPluginToFile (juce::AudioPluginInstance& plugin,
                                    const juce::File& inputFile,
                                    const juce::File& outputFile,
                                    const OfflineCaptureOptions& options,
                                    juce::String& error);
};
