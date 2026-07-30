#pragma once

#include <JuceHeader.h>

/** Small audio-host utilities shared by PluginAudioEngine collaborators. */
namespace HostAudioHelpers
{
    /**
     * Pump the message thread for `ms` milliseconds. Used by blocking
     * message-thread waits (sysex dump, latency detect, hardware capture)
     * so MIDI callbacks and modal UI stay alive.
     */
    inline void pumpMessageThreadMs (int ms)
    {
        juce::Thread::sleep (ms);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }

    /**
     * Equal-power (sin/cos) crossfade between two buffers over fadeLength samples.
     * fade=0 → all A, fade=1 → all B. Updates fade to the end-of-block position.
     * When writeIntoFirst is true (the only mode used today), the mix is written
     * into outOrA; outOrA is also the A input.
     */
    inline void applyEqualPowerCrossfade (juce::AudioBuffer<float>& outOrA,
                                          const juce::AudioBuffer<float>& b,
                                          float& fade,
                                          float target,
                                          int fadeLength,
                                          int numSamples,
                                          bool writeIntoFirst)
    {
        if (! writeIntoFirst)
            return;

        if (fade <= 0.0f && target <= 0.0f)
            return;

        const float fadeStart = fade;
        float fadeEnd = fadeStart;

        if (std::abs (fadeStart - target) > 1.0e-6f)
        {
            const float step = (float) numSamples / (float) juce::jmax (1, fadeLength);
            fadeEnd = target > fadeStart ? juce::jmin (target, fadeStart + step)
                                         : juce::jmax (target, fadeStart - step);
        }

        const float fadeStep = (fadeEnd - fadeStart) / (float) juce::jmax (1, numSamples);
        constexpr float halfPi = juce::MathConstants<float>::halfPi;
        const int channels = juce::jmin (outOrA.getNumChannels(), b.getNumChannels());

        for (int ch = 0; ch < channels; ++ch)
        {
            auto* a = outOrA.getWritePointer (ch);
            const auto* bPtr = b.getReadPointer (ch);
            float f = fadeStart;

            for (int i = 0; i < numSamples; ++i)
            {
                const float bGain = std::sin (f * halfPi);
                const float aGain = std::cos (f * halfPi);
                a[i] = a[i] * aGain + bPtr[i] * bGain;
                f += fadeStep;
            }
        }

        fade = fadeEnd;
    }
}
