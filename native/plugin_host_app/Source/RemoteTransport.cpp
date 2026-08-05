#include "RemoteTransport.h"

bool RemoteTransport::load (const juce::String& fileOrIdentifier,
                            double sampleRate,
                            int blockSize,
                            juce::String& error)
{
    unload();

    juce::AudioPluginFormatManager formats;
    juce::addDefaultFormatsToManager (formats);

    juce::PluginDescription description;
    description.fileOrIdentifier = fileOrIdentifier.isNotEmpty() ? fileOrIdentifier
                                                                 : juce::String (defaultPluginIdentifier);

    if (description.fileOrIdentifier.startsWithIgnoreCase ("AudioUnit:"))
        description.pluginFormatName = "AudioUnit";
    else if (description.fileOrIdentifier.endsWithIgnoreCase (".vst3"))
        description.pluginFormatName = "VST3";
    else if (description.fileOrIdentifier.endsWithIgnoreCase (".component"))
        description.pluginFormatName = "AudioUnit";
    else
    {
        error = "Unsupported remote transport plugin identifier: " + description.fileOrIdentifier;
        return false;
    }

    juce::String loadError;
    auto instance = formats.createPluginInstance (description, sampleRate, blockSize, loadError);
    if (instance == nullptr)
    {
        error = loadError.isNotEmpty() ? loadError
                                       : juce::String ("Failed to create remote transport plugin");
        return false;
    }

    // Stereo in/out; fall back to whatever the plugin insists on.
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::stereo());
    layout.outputBuses.add (juce::AudioChannelSet::stereo());
    if (! instance->setBusesLayout (layout))
        instance->enableAllBuses();

    lastSampleRate = sampleRate;
    lastBlockSize = blockSize;

    const int channels = juce::jmax (2,
                                     instance->getTotalNumInputChannels(),
                                     instance->getTotalNumOutputChannels());
    scratchBuffer.setSize (channels, juce::jmax (blockSize, 16));

    instance->prepareToPlay (sampleRate, blockSize);

    {
        const juce::ScopedLock lock (processLock);
        plugin = std::move (instance);
        prepared = true;
    }

    muteDryMonitor();
    loaded.store (true);
    return true;
}

void RemoteTransport::unload()
{
    loaded.store (false);

    std::unique_ptr<juce::AudioPluginInstance> dying;
    {
        const juce::ScopedLock lock (processLock);
        dying = std::move (plugin);
        prepared = false;
    }

    if (dying != nullptr)
        dying->releaseResources();
}

juce::String RemoteTransport::getPluginName() const
{
    const juce::ScopedLock lock (processLock);
    return plugin != nullptr ? plugin->getName() : juce::String();
}

void RemoteTransport::prepare (double sampleRate, int blockSize)
{
    const juce::ScopedLock lock (processLock);
    lastSampleRate = sampleRate;
    lastBlockSize = blockSize;

    if (plugin == nullptr)
        return;

    const int channels = juce::jmax (2,
                                     plugin->getTotalNumInputChannels(),
                                     plugin->getTotalNumOutputChannels());
    scratchBuffer.setSize (channels, juce::jmax (blockSize, 16));

    plugin->prepareToPlay (sampleRate, blockSize);
    prepared = true;
}

void RemoteTransport::release()
{
    const juce::ScopedLock lock (processLock);
    if (plugin != nullptr && prepared)
        plugin->releaseResources();
    prepared = false;
}

void RemoteTransport::processBlock (juce::AudioBuffer<float>& stereoBuffer, juce::MidiBuffer& midi)
{
    if (plugin == nullptr || ! prepared || plugin->isSuspended())
        return;

    const int numSamples = stereoBuffer.getNumSamples();

    // Preallocated in prepare; only grows on the rare device block-size bump.
    if (scratchBuffer.getNumSamples() < numSamples)
        scratchBuffer.setSize (scratchBuffer.getNumChannels(), numSamples, false, false, true);

    const int channels = scratchBuffer.getNumChannels();
    for (int ch = 0; ch < channels; ++ch)
    {
        if (ch < stereoBuffer.getNumChannels())
            scratchBuffer.copyFrom (ch, 0, stereoBuffer, ch, 0, numSamples);
        else
            scratchBuffer.clear (ch, 0, numSamples);
    }

    juce::AudioBuffer<float> view (scratchBuffer.getArrayOfWritePointers(), channels, numSamples);
    plugin->processBlock (view, midi);

    for (int ch = 0; ch < stereoBuffer.getNumChannels(); ++ch)
        stereoBuffer.copyFrom (ch, 0, scratchBuffer, juce::jmin (ch, channels - 1), 0, numSamples);
}

void RemoteTransport::swapPendingMidi (juce::MidiBuffer& dest)
{
    dest.clear();
    const juce::ScopedTryLock lock (midiLock);
    if (lock.isLocked() && ! pendingMidi.isEmpty())
    {
        dest.swapWith (pendingMidi);
        pendingMidi.clear();
    }
}

void RemoteTransport::queueMidiMessage (const juce::MidiMessage& message)
{
    const juce::ScopedLock lock (midiLock);
    pendingMidi.addEvent (message, 0);
}

void RemoteTransport::queueMidiMessages (const juce::Array<juce::MidiMessage>& messages)
{
    const juce::ScopedLock lock (midiLock);
    for (const auto& message : messages)
        pendingMidi.addEvent (message, 0);
}

juce::AudioProcessorEditor* RemoteTransport::createEditor()
{
    const juce::ScopedLock lock (processLock);
    if (plugin == nullptr)
        return nullptr;

    return plugin->createEditorIfNeeded();
}

bool RemoteTransport::getState (juce::MemoryBlock& out) const
{
    const juce::ScopedLock lock (processLock);
    if (plugin == nullptr)
        return false;

    out.reset();
    plugin->getStateInformation (out);
    return out.getSize() > 0;
}

bool RemoteTransport::applyState (const juce::MemoryBlock& state)
{
    const juce::ScopedLock lock (processLock);
    if (plugin == nullptr || state.isEmpty())
        return false;

    plugin->setStateInformation (state.getData(), (int) state.getSize());
    return true;
}

void RemoteTransport::muteDryMonitor()
{
    const juce::ScopedLock lock (processLock);
    if (plugin == nullptr)
        return;

    for (auto* parameter : plugin->getParameters())
    {
        if (parameter != nullptr && parameter->getName (64).equalsIgnoreCase ("Dry Level"))
        {
            parameter->setValueNotifyingHost (0.0f);
            break;
        }
    }
}
