#include "PluginRenderer.h"
#include "AUpresetLoader.h"
#include "ParameterHelpers.h"

#include <iostream>

namespace
{
    std::unique_ptr<juce::AudioPluginInstance> loadPlugin (const juce::File& pluginFile,
                                                           double sampleRate,
                                                           int blockSize,
                                                           juce::String& error)
    {
        juce::AudioPluginFormatManager formatManager;
        juce::addDefaultFormatsToManager (formatManager);

        juce::OwnedArray<juce::PluginDescription> descriptions;

        for (auto* format : formatManager.getFormats())
        {
            if (format == nullptr)
                continue;

            format->findAllTypesForFile (descriptions, pluginFile.getFullPathName());
        }

        if (descriptions.isEmpty())
        {
            juce::String formatNames;
            for (auto* format : formatManager.getFormats())
                formatNames << (formatNames.isEmpty() ? "" : ", ") << format->getName();

            error = "No plugin types found in: " + pluginFile.getFullPathName()
                  + (formatNames.isEmpty() ? " (no plugin formats enabled — rebuild with PLUGINHOST_AU)"
                                           : " (scanned formats: " + formatNames + ")");
            return {};
        }

        juce::String loadError;
        auto instance = formatManager.createPluginInstance (*descriptions[0], sampleRate, blockSize, loadError);

        if (instance == nullptr)
        {
            error = loadError.isNotEmpty() ? loadError
                                           : juce::String ("Failed to create plugin instance");
            return {};
        }

        return instance;
    }

    bool loadPresetState (juce::AudioPluginInstance& plugin,
                          const juce::File& presetFile,
                          juce::String& error)
    {
        juce::MemoryBlock state;
        if (! AUpresetLoader::loadStateBytes (presetFile, state, error))
            return false;

        plugin.setStateInformation (state.getData(), (int) state.getSize());
        return true;
    }

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

    bool renderOffline (juce::AudioPluginInstance& plugin,
                        const juce::File& inputFile,
                        const juce::File& outputFile,
                        const CommandLineOptions& options,
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
        else if (! tailSilenceReached && tailSamplesRendered >= maxTailSamples)
        {
            std::cerr << "Warning: max tail length reached (" << options.maxTailSeconds
                      << "s) before output fell silent for " << options.tailSilenceSeconds << "s" << std::endl;
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
}

int PluginRenderer::run (const CommandLineOptions& options)
{
    juce::String error;

    if (options.dumpParameters)
    {
        CommandLineOptions dumpOptions = options;
        if (! dumpOptions.validateForDump (error))
        {
            std::cerr << error << std::endl;
            return 1;
        }

        auto plugin = loadPlugin (options.pluginPath, 44100.0, options.blockSize, error);
        if (plugin == nullptr)
        {
            std::cerr << error << std::endl;
            return 1;
        }

        if (options.presetPath.existsAsFile())
        {
            if (! loadPresetState (*plugin, options.presetPath, error))
            {
                std::cerr << error << std::endl;
                return 1;
            }
        }

        const auto json = ParameterHelpers::dumpParametersJson (*plugin);
        std::cout << json << std::endl;
        return 0;
    }

    if (! options.validateForRender (error))
    {
        std::cerr << error << std::endl;
        return 1;
    }

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> inputReader (formatManager.createReaderFor (options.inputPath));
    if (inputReader == nullptr)
    {
        std::cerr << "Could not read input WAV: " << options.inputPath.getFullPathName() << std::endl;
        return 1;
    }

    const double renderRate = options.sampleRate > 0.0 ? options.sampleRate : inputReader->sampleRate;
    auto plugin = loadPlugin (options.pluginPath, renderRate, options.blockSize, error);
    if (plugin == nullptr)
    {
        std::cerr << error << std::endl;
        return 1;
    }

    if (! loadPresetState (*plugin, options.presetPath, error))
    {
        std::cerr << error << std::endl;
        return 1;
    }

    ParameterHelpers::applyOverrides (*plugin, options.paramOverrides, error);
    if (error.isNotEmpty())
    {
        std::cerr << error << std::endl;
        return 1;
    }

    if (! renderOffline (*plugin, options.inputPath, options.outputPath, options, error))
    {
        std::cerr << error << std::endl;
        return 1;
    }

    return 0;
}
