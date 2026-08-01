#pragma once

#include <JuceHeader.h>
#include <cmath>

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

    /** Drop samples before the first frame whose peak is >= threshold.
        Fully-silent buffers are left unchanged. */
    inline void trimLeadingSilence (juce::AudioBuffer<float>& buffer, float threshold)
    {
        const int channels = buffer.getNumChannels();
        const int samples = buffer.getNumSamples();
        if (channels <= 0 || samples <= 0)
            return;

        int start = 0;
        for (; start < samples; ++start)
        {
            float peak = 0.0f;
            for (int ch = 0; ch < channels; ++ch)
                peak = juce::jmax (peak, std::abs (buffer.getSample (ch, start)));
            if (peak >= threshold)
                break;
        }

        if (start <= 0 || start >= samples)
            return;

        const int newLength = samples - start;
        juce::AudioBuffer<float> trimmed (channels, newLength);
        for (int ch = 0; ch < channels; ++ch)
            trimmed.copyFrom (ch, 0, buffer, ch, start, newLength);
        buffer = std::move (trimmed);
    }
}
