#include "NoiseFloorCalibration.h"
#include "PluginAudioEngine.h"
#include <cmath>

double NoiseFloorCalibration::thresholdFromPeakDb (double peakDb, double marginDb)
{
    if (! std::isfinite (peakDb))
        return defaultSilenceThresholdDb;

    const double gated = peakDb + marginDb;
    // Keep the gate above absolute digital silence but below typical program
    // material so tails can still cross it.
    return juce::jlimit (-100.0, -35.0, gated);
}

NoiseFloorCalibration::Result NoiseFloorCalibration::analyse (const juce::AudioBuffer<float>& buffer,
                                                              int startSample,
                                                              int numSamples,
                                                              double marginDb)
{
    Result out;
    out.marginDb = marginDb;

    if (buffer.getNumChannels() <= 0 || buffer.getNumSamples() <= 0 || numSamples <= 0)
        return out;

    const int start = juce::jlimit (0, buffer.getNumSamples() - 1, startSample);
    const int end = juce::jlimit (start, buffer.getNumSamples(), start + numSamples);
    if (end <= start)
        return out;

    float peak = 0.0f;
    double sumSq = 0.0;
    int count = 0;

    for (int i = start; i < end; ++i)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const float s = std::abs (buffer.getSample (ch, i));
            peak = juce::jmax (peak, s);
            sumSq += (double) s * (double) s;
            ++count;
        }
    }

    out.peakDb = (double) juce::Decibels::gainToDecibels (peak, -120.0f);
    if (count > 0)
        out.rmsDb = (double) juce::Decibels::gainToDecibels ((float) std::sqrt (sumSq / (double) count),
                                                            -120.0f);
    out.recommendedSilenceThresholdDb = thresholdFromPeakDb (out.peakDb, marginDb);
    return out;
}

bool NoiseFloorCalibration::measureHardwareReturn (PluginAudioEngine& engine,
                                                   Result& out,
                                                   juce::String& error,
                                                   double listenSeconds,
                                                   double marginDb)
{
    out = {};
    out.listenSeconds = listenSeconds;
    out.marginDb = marginDb;

    float peakDb = -120.0f;
    float rmsDb = -120.0f;
    float dcL = 0.0f;
    float dcR = 0.0f;
    if (! engine.measureHardwareNoiseFloor (listenSeconds, peakDb, rmsDb, dcL, dcR, error))
        return false;

    out.peakDb = (double) peakDb;
    out.rmsDb = (double) rmsDb;
    out.dcOffsetL = dcL;
    out.dcOffsetR = dcR;
    out.recommendedSilenceThresholdDb = thresholdFromPeakDb (out.peakDb, marginDb);
    return true;
}
