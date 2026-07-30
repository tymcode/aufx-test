#pragma once

#include <JuceHeader.h>

/** Small AudioBuffer helpers shared by OfflineCapture and the headless renderer. */
namespace AudioBufferUtils
{
    inline float blockPeak (const juce::AudioBuffer<float>& buffer)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));

        return peak;
    }

    inline void appendBlock (juce::AudioBuffer<float>& dst, const juce::AudioBuffer<float>& src)
    {
        const int oldSize = dst.getNumSamples();
        dst.setSize (dst.getNumChannels(), oldSize + src.getNumSamples(), true, false, true);

        for (int ch = 0; ch < dst.getNumChannels(); ++ch)
            dst.copyFrom (ch, oldSize, src, ch, 0, src.getNumSamples());
    }
}
