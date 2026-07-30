#include "OfflineCapture.h"
#include "AudioBufferUtils.h"

namespace
{
    /** Linear-resample `source` (at sourceRate) into `destRate` stereo (or N-ch) buffer. */
    juce::AudioBuffer<float> resampleToRate (const juce::AudioBuffer<float>& source,
                                             double sourceRate,
                                             double destRate,
                                             int destChannels)
    {
        const int sourceSamples = source.getNumSamples();
        const int sourceChannels = juce::jmax (1, source.getNumChannels());
        destChannels = juce::jmax (1, destChannels);

        if (sourceSamples <= 0 || sourceRate <= 1.0 || destRate <= 1.0)
            return juce::AudioBuffer<float> (destChannels, 0);

        const int destSamples = juce::jmax (1, (int) std::llround ((double) sourceSamples * destRate / sourceRate));
        juce::AudioBuffer<float> dest (destChannels, destSamples);

        for (int i = 0; i < destSamples; ++i)
        {
            const double srcPos = (double) i * sourceRate / destRate;
            const int src0 = juce::jlimit (0, sourceSamples - 1, (int) std::floor (srcPos));
            const int src1 = juce::jmin (sourceSamples - 1, src0 + 1);
            const float frac = (float) juce::jlimit (0.0, 1.0, srcPos - (double) src0);

            for (int ch = 0; ch < destChannels; ++ch)
            {
                const int srcCh = juce::jmin (ch, sourceChannels - 1);
                const float a = source.getSample (srcCh, src0);
                const float b = source.getSample (srcCh, src1);
                dest.setSample (ch, i, a + (b - a) * frac);
            }
        }

        return dest;
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

    const int sourceSamples = (int) reader->lengthInSamples;
    if (sourceSamples <= 0)
    {
        error = "Input has no audio samples: " + inputFile.getFullPathName();
        return false;
    }

    const double sourceRate = reader->sampleRate > 1.0 ? reader->sampleRate : 44100.0;
    const double effectiveRate = options.sampleRate > 0.0 ? options.sampleRate : sourceRate;
    const int blockSize = juce::jmax (32, options.blockSize);
    const float silenceThreshold = juce::Decibels::decibelsToGain ((float) options.silenceThresholdDb);
    const int tailSilenceSamples = (int) std::ceil (options.tailSilenceSeconds * effectiveRate);
    const int maxTailSamples = (int) std::ceil (options.maxTailSeconds * effectiveRate);

    // Match the realtime host: process at the wider of plugin IO / stereo so
    // multi-bus AUs get allocated channel pointers (null channels in
    // renderGetInput are a common AU crash when the buffer is too narrow).
    const int processChannels = juce::jmax (2,
                                            plugin.getTotalNumInputChannels(),
                                            plugin.getTotalNumOutputChannels(),
                                            (int) reader->numChannels);

    juce::AudioBuffer<float> sourceBuffer (juce::jmax (1, (int) reader->numChannels), sourceSamples);
    reader->read (sourceBuffer.getArrayOfWritePointers(),
                  sourceBuffer.getNumChannels(),
                  0,
                  sourceSamples);

    auto inputBuffer = resampleToRate (sourceBuffer, sourceRate, effectiveRate, processChannels);
    const int numSamples = inputBuffer.getNumSamples();
    if (numSamples <= 0)
    {
        error = "Resampled input is empty";
        return false;
    }

    // Offline on a live plugin instance that also serves the device callback:
    // never releaseResources here (the engine owns that), always mark
    // non-realtime, and keep a fixed block size — variable last-block sizes
    // and a second releaseResources() have crashed AUs (e.g. QDV1) on the
    // next capture.
    const bool wasNonRealtime = plugin.isNonRealtime();
    const bool wasSuspended = plugin.isSuspended();
    plugin.setNonRealtime (true);
    plugin.suspendProcessing (false);
    plugin.prepareToPlay (effectiveRate, blockSize);

    juce::AudioBuffer<float> outputBuffer (processChannels, 0);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> block (processChannels, blockSize);

    for (int offset = 0; offset < numSamples; offset += blockSize)
    {
        const int currentBlock = juce::jmin (blockSize, numSamples - offset);
        block.clear();

        for (int ch = 0; ch < processChannels; ++ch)
            block.copyFrom (ch, 0, inputBuffer, ch, offset, currentBlock);

        midi.clear();
        plugin.processBlock (block, midi);

        if (currentBlock == blockSize)
        {
            AudioBufferUtils::appendBlock (outputBuffer, block);
        }
        else
        {
            juce::AudioBuffer<float> clipped (processChannels, currentBlock);
            for (int ch = 0; ch < processChannels; ++ch)
                clipped.copyFrom (ch, 0, block, ch, 0, currentBlock);
            AudioBufferUtils::appendBlock (outputBuffer, clipped);
        }
    }

    int consecutiveSilentSamples = 0;
    int tailSamplesRendered = 0;
    bool tailSilenceReached = false;

    while (tailSamplesRendered < maxTailSamples)
    {
        block.clear();
        midi.clear();
        plugin.processBlock (block, midi);
        AudioBufferUtils::appendBlock (outputBuffer, block);

        tailSamplesRendered += blockSize;

        if (AudioBufferUtils::blockPeak (block) < silenceThreshold)
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
        outputBuffer.setSize (processChannels, newLength, true, true, true);
    }

    plugin.suspendProcessing (wasSuspended);
    plugin.setNonRealtime (wasNonRealtime);

    if (outputBuffer.getNumSamples() == 0)
    {
        error = "Render produced no output samples";
        return false;
    }

    // Hardware captures are stereo; write the first two process channels so
    // A/B files share channel layout and sample rate.
    const int writeChannels = juce::jmin (2, outputBuffer.getNumChannels());

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
                                       .withNumChannels (writeChannels)
                                       .withBitsPerSample (24)));

    if (writer == nullptr)
    {
        error = "Could not create WAV writer for: " + outputFile.getFullPathName();
        return false;
    }

    writer->writeFromFloatArrays (outputBuffer.getArrayOfReadPointers(),
                                  writeChannels,
                                  outputBuffer.getNumSamples());
    return true;
}
