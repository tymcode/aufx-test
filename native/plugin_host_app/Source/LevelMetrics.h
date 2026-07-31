#pragma once

#include <JuceHeader.h>

/**
 * Sample-true peak, RMS, and a BS.1770-style integrated LUFS estimate over a
 * buffer region. LUFS uses K-weighting + absolute/relative gating; intended
 * for calibration windows, not a full EBU R128 program meter.
 */
namespace LevelMetrics
{
    struct Levels
    {
        double peakDb { -120.0 };
        double rmsDb { -120.0 };
        double lufs { -120.0 };
        bool clipped { false }; // true sample peak >= 0 dBFS
    };

    /** Peak / RMS / LUFS over [startSample, startSample + numSamples). */
    Levels analyse (const juce::AudioBuffer<float>& buffer,
                    int startSample,
                    int numSamples,
                    double sampleRate);

    /**
     * Skip settleSeconds from the start, then analyse windowSeconds
     * (or whatever remains). Falls back to the whole buffer if short.
     */
    Levels analyseSteadyState (const juce::AudioBuffer<float>& buffer,
                               double sampleRate,
                               double settleSeconds,
                               double windowSeconds);
}
