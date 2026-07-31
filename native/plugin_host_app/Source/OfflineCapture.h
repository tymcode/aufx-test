#pragma once

#include <JuceHeader.h>

struct OfflineCaptureOptions
{
    double sampleRate { 0.0 };
    int blockSize { 512 };
    double tailSilenceSeconds { 1.0 };
    double silenceThresholdDb { -60.0 };
    double maxTailSeconds { 120.0 };
    /**
     * Digital gain applied to the (resampled) input before processing.
     * Used by level-sweep calibration to probe transfer curves.
     */
    float inputGainDb { 0.0f };
    /**
     * When true, copy dry input through without calling processBlock
     * (host bypass / dry-thru). Skips the silence-tail loop.
     */
    bool bypassPlugin { false };
    /** Dry/wet mix after processBlock (0 = dry, 1 = wet). Ignored when bypassPlugin. */
    float mixAmount { 1.0f };
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
