#include "OfflineCapture.h"

namespace
{
    float blockPeak (const juce::AudioBuffer<float>& buffer)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));

        return peak;
    }

    void appendBlock (juce::AudioBuffer<float>& dst, const juce::AudioBuffer<float>& src)
    {
        const int oldSize = dst.getNumSamples();
        dst.setSize (dst.getNumChannels(), oldSize + src.getNumSamples(), true, false, true);

        for (int ch = 0; ch < dst.getNumChannels(); ++ch)
            dst.copyFrom (ch, oldSize, src, ch, 0, src.getNumSamples());
    }
}

bool OfflineCapture::renderPluginToFile (juce::AudioPluginInstance& plugin,
                                       const juce::File& inputFile,
                                       const juce::File& outputFile,
                                       const OfflineCaptureOptions& options,
                                       juce::String& error)
{
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (inputFile));
    if (reader == nullptr)
    {
        error = "Could not read input WAV: " + inputFile.getFullPathName();
        return false;
    }

    const auto numChannels = juce::jmax (1, (int) reader->numChannels);
    const auto numSamples = (int) reader->lengthInSamples;
    const auto effectiveRate = options.sampleRate > 0.0 ? options.sampleRate : reader->sampleRate;
    const auto blockSize = options.blockSize;
    const float silenceThreshold = juce::Decibels::decibelsToGain ((float) options.silenceThresholdDb);
    const int tailSilenceSamples = (int) std::ceil (options.tailSilenceSeconds * effectiveRate);
    const int maxTailSamples = (int) std::ceil (options.maxTailSeconds * effectiveRate);

    plugin.prepareToPlay (effectiveRate, blockSize);

    juce::AudioBuffer<float> inputBuffer ((int) reader->numChannels, numSamples);
    reader->read (inputBuffer.getArrayOfWritePointers(), (int) reader->numChannels, 0, numSamples);

    if (inputBuffer.getNumChannels() < numChannels)
        inputBuffer.setSize (numChannels, numSamples, true, true, true);

    juce::AudioBuffer<float> outputBuffer (numChannels, 0);
    juce::MidiBuffer midi;

    for (int offset = 0; offset < numSamples; offset += blockSize)
    {
        const auto currentBlock = juce::jmin (blockSize, numSamples - offset);
        juce::AudioBuffer<float> block (numChannels, currentBlock);

        for (int ch = 0; ch < numChannels; ++ch)
            block.copyFrom (ch, 0, inputBuffer, ch, offset, currentBlock);

        plugin.processBlock (block, midi);
        appendBlock (outputBuffer, block);
    }

    int consecutiveSilentSamples = 0;
    int tailSamplesRendered = 0;
    bool tailSilenceReached = false;

    while (tailSamplesRendered < maxTailSamples)
    {
        juce::AudioBuffer<float> block (numChannels, blockSize);
        block.clear();
        plugin.processBlock (block, midi);
        appendBlock (outputBuffer, block);

        tailSamplesRendered += blockSize;

        if (blockPeak (block) < silenceThreshold)
            consecutiveSilentSamples += blockSize;
        else
            consecutiveSilentSamples = 0;

        if (consecutiveSilentSamples >= tailSilenceSamples)
        {
            tailSilenceReached = true;
            break;
        }
    }

    if (tailSilenceReached && consecutiveSilentSamples > 0)
    {
        const int newLength = juce::jmax (0, outputBuffer.getNumSamples() - consecutiveSilentSamples);
        outputBuffer.setSize (numChannels, newLength, true, true, true);
    }

    plugin.releaseResources();

    if (outputBuffer.getNumSamples() == 0)
    {
        error = "Render produced no output samples";
        return false;
    }

    outputFile.getParentDirectory().createDirectory();
    std::unique_ptr<juce::OutputStream> stream (outputFile.createOutputStream());
    if (stream == nullptr)
    {
        error = "Could not open output file: " + outputFile.getFullPathName();
        return false;
    }

    juce::WavAudioFormat wavFormat;
    using Opts = juce::AudioFormatWriterOptions;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wavFormat.createWriterFor (stream,
                                   Opts{}
                                       .withSampleRate (effectiveRate)
                                       .withNumChannels (numChannels)
                                       .withBitsPerSample (24)));

    if (writer == nullptr)
    {
        error = "Could not create WAV writer for: " + outputFile.getFullPathName();
        return false;
    }

    writer->writeFromFloatArrays (outputBuffer.getArrayOfReadPointers(),
                                  numChannels,
                                  outputBuffer.getNumSamples());
    return true;
}
