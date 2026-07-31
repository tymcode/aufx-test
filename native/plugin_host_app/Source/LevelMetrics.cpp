#include "LevelMetrics.h"
#include <cmath>
#include <vector>

namespace
{
    /** Bilinear-transformed ITU-R BS.1770 K-weighting stage (pre-filter + RLB). */
    struct Biquad
    {
        double b0 { 1.0 }, b1 { 0.0 }, b2 { 0.0 }, a1 { 0.0 }, a2 { 0.0 };
        double z1 { 0.0 }, z2 { 0.0 };

        void reset() { z1 = z2 = 0.0; }

        float process (float x)
        {
            const double y = b0 * (double) x + z1;
            z1 = b1 * (double) x - a1 * y + z2;
            z2 = b2 * (double) x - a2 * y;
            return (float) y;
        }
    };

    Biquad makePreFilter (double sampleRate)
    {
        // ITU-R BS.1770-4 pre-filter (high shelf), bilinear at sampleRate.
        const double f0 = 1681.974450955533;
        const double G = 3.999843853973347;
        const double Q = 0.7071752369554196;
        const double K = std::tan (juce::MathConstants<double>::pi * f0 / sampleRate);
        const double Vh = std::pow (10.0, G / 20.0);
        const double Vb = std::pow (Vh, 0.4996667741545416);
        const double a0_ = 1.0 + K / Q + K * K;
        Biquad f;
        f.b0 = (Vh + Vb * K / Q + K * K) / a0_;
        f.b1 = 2.0 * (K * K - Vh) / a0_;
        f.b2 = (Vh - Vb * K / Q + K * K) / a0_;
        f.a1 = 2.0 * (K * K - 1.0) / a0_;
        f.a2 = (1.0 - K / Q + K * K) / a0_;
        return f;
    }

    Biquad makeRlbFilter (double sampleRate)
    {
        // ITU-R BS.1770-4 RLB high-pass.
        const double f0 = 38.13547087602444;
        const double Q = 0.5003270373238773;
        const double K = std::tan (juce::MathConstants<double>::pi * f0 / sampleRate);
        const double a0_ = 1.0 + K / Q + K * K;
        Biquad f;
        f.b0 = 1.0;
        f.b1 = -2.0;
        f.b2 = 1.0;
        f.a1 = 2.0 * (K * K - 1.0) / a0_;
        f.a2 = (1.0 - K / Q + K * K) / a0_;
        f.b0 /= a0_;
        f.b1 /= a0_;
        f.b2 /= a0_;
        return f;
    }

    double meanSquareToLufs (double meanSquare)
    {
        if (! (meanSquare > 1.0e-20))
            return -120.0;
        return -0.691 + 10.0 * std::log10 (meanSquare);
    }

    double integrateLufs (const juce::AudioBuffer<float>& buffer,
                          int start,
                          int end,
                          double sampleRate)
    {
        if (sampleRate <= 1.0 || end <= start)
            return -120.0;

        const int numCh = juce::jmin (2, buffer.getNumChannels());
        if (numCh <= 0)
            return -120.0;

        std::vector<Biquad> pre ( (size_t) numCh);
        std::vector<Biquad> rlb ( (size_t) numCh);
        for (int ch = 0; ch < numCh; ++ch)
        {
            pre[(size_t) ch] = makePreFilter (sampleRate);
            rlb[(size_t) ch] = makeRlbFilter (sampleRate);
        }

        // 400 ms blocks for gating (BS.1770), 75% overlap.
        const int blockSize = juce::jmax (1, (int) std::llround (0.4 * sampleRate));
        const int hop = juce::jmax (1, blockSize / 4);
        const int total = end - start;
        if (total < blockSize)
        {
            // Short window: ungated K-weighted mean square over the whole region.
            double sum = 0.0;
            int count = 0;
            for (int i = start; i < end; ++i)
            {
                for (int ch = 0; ch < numCh; ++ch)
                {
                    float y = pre[(size_t) ch].process (buffer.getSample (ch, i));
                    y = rlb[(size_t) ch].process (y);
                    sum += (double) y * (double) y;
                    ++count;
                }
            }
            return meanSquareToLufs (count > 0 ? sum / (double) count : 0.0);
        }

        // Reset filters and re-run for block gating (filters must be continuous).
        for (int ch = 0; ch < numCh; ++ch)
        {
            pre[(size_t) ch].reset();
            rlb[(size_t) ch].reset();
        }

        juce::Array<double> blockPowers;
        std::vector<double> samplePower ( (size_t) total, 0.0);
        for (int i = 0; i < total; ++i)
        {
            double p = 0.0;
            for (int ch = 0; ch < numCh; ++ch)
            {
                float y = pre[(size_t) ch].process (buffer.getSample (ch, start + i));
                y = rlb[(size_t) ch].process (y);
                p += (double) y * (double) y;
            }
            samplePower[(size_t) i] = p / (double) numCh;
        }

        for (int blockStart = 0; blockStart + blockSize <= total; blockStart += hop)
        {
            double sum = 0.0;
            for (int i = 0; i < blockSize; ++i)
                sum += samplePower[(size_t) (blockStart + i)];
            blockPowers.add (sum / (double) blockSize);
        }

        if (blockPowers.isEmpty())
            return -120.0;

        // Absolute gate: keep blocks above -70 LUFS.
        juce::Array<double> absGated;
        for (auto z : blockPowers)
            if (meanSquareToLufs (z) > -70.0)
                absGated.add (z);

        if (absGated.isEmpty())
            return -120.0;

        double absMean = 0.0;
        for (auto z : absGated)
            absMean += z;
        absMean /= (double) absGated.size();
        const double relThresh = meanSquareToLufs (absMean) - 10.0;

        juce::Array<double> relGated;
        for (auto z : absGated)
            if (meanSquareToLufs (z) > relThresh)
                relGated.add (z);

        if (relGated.isEmpty())
            return meanSquareToLufs (absMean);

        double relMean = 0.0;
        for (auto z : relGated)
            relMean += z;
        relMean /= (double) relGated.size();
        return meanSquareToLufs (relMean);
    }
}

LevelMetrics::Levels LevelMetrics::analyse (const juce::AudioBuffer<float>& buffer,
                                            int startSample,
                                            int numSamples,
                                            double sampleRate)
{
    Levels out;
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
    out.clipped = peak >= 1.0f;
    if (count > 0)
        out.rmsDb = (double) juce::Decibels::gainToDecibels ((float) std::sqrt (sumSq / (double) count),
                                                             -120.0f);
    out.lufs = integrateLufs (buffer, start, end, sampleRate);
    return out;
}

LevelMetrics::Levels LevelMetrics::analyseSteadyState (const juce::AudioBuffer<float>& buffer,
                                                       double sampleRate,
                                                       double settleSeconds,
                                                       double windowSeconds)
{
    Levels out;
    if (buffer.getNumSamples() <= 0 || sampleRate <= 1.0)
        return out;

    const int skip = juce::jmax (0, (int) std::llround (settleSeconds * sampleRate));
    const int want = juce::jmax (1, (int) std::llround (windowSeconds * sampleRate));
    const int available = buffer.getNumSamples();

    int start = skip;
    int count = want;
    if (start + count > available)
    {
        if (available > want)
            start = juce::jmax (0, available - want);
        else
        {
            start = 0;
            count = available;
        }
    }

    return analyse (buffer, start, count, sampleRate);
}
