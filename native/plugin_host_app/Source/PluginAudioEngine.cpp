#include "PluginAudioEngine.h"
#include "AUpresetLoader.h"
#include "AUpresetSaver.h"

namespace
{
    juce::String formatNameForPluginFile (const juce::File& pluginFile)
    {
        if (pluginFile.hasFileExtension (".vst3"))
            return "VST3";
        if (pluginFile.hasFileExtension (".component") || pluginFile.hasFileExtension (".appex"))
            return "AudioUnit";
        return {};
    }

    std::unique_ptr<juce::AudioPluginInstance> createPluginInstance (const juce::File& pluginFile,
                                                                     double sampleRate,
                                                                     int blockSize,
                                                                     juce::String& error)
    {
        juce::AudioPluginFormatManager formatManager;
        juce::addDefaultFormatsToManager (formatManager);

        // Create once from the bundle path. Avoid findAllTypesForFile first — that
        // temporary instantiate/teardown can leave some AUs unable to provide their
        // Cocoa UI on the next instance (host falls back to AUGenericView).
        juce::PluginDescription description;
        description.fileOrIdentifier = pluginFile.getFullPathName();
        description.pluginFormatName = formatNameForPluginFile (pluginFile);

        if (description.pluginFormatName.isEmpty())
        {
            error = "Unsupported plugin type: " + pluginFile.getFullPathName();
            return {};
        }

        juce::String loadError;
        auto instance = formatManager.createPluginInstance (description, sampleRate, blockSize, loadError);

        if (instance == nullptr)
        {
            error = loadError.isNotEmpty() ? loadError : juce::String ("Failed to create plugin instance");
            return {};
        }

        return instance;
    }
}

PluginAudioEngine::PluginAudioEngine()
{
    formatManager.registerBasicFormats();
}

PluginAudioEngine::~PluginAudioEngine()
{
    stopFixture();
    clearMidiInput();
    stopAudioDevice();
}

bool PluginAudioEngine::loadPlugin (const juce::File& pluginFile, juce::String& error)
{
    stopFixture();
    stopAudioDevice();

    const juce::ScopedLock lock (processLock);
    plugin.reset();

    auto instance = createPluginInstance (pluginFile, deviceSampleRate, deviceBlockSize, error);
    if (instance == nullptr)
        return false;

    plugin = std::move (instance);
    plugin->setPlayHead (this);
    return true;
}

bool PluginAudioEngine::loadPlugin (const juce::PluginDescription& description, juce::String& error)
{
    stopFixture();
    stopAudioDevice();

    juce::AudioPluginFormatManager pluginFormats;
    juce::addDefaultFormatsToManager (pluginFormats);

    juce::String loadError;
    auto instance = pluginFormats.createPluginInstance (description, deviceSampleRate, deviceBlockSize, loadError);

    if (instance == nullptr)
    {
        error = loadError.isNotEmpty() ? loadError : juce::String ("Failed to create plugin instance");
        return false;
    }

    const juce::ScopedLock lock (processLock);
    plugin = std::move (instance);
    plugin->setPlayHead (this);
    return true;
}

juce::String PluginAudioEngine::getCurrentPluginName() const
{
    if (plugin == nullptr)
        return {};

    return plugin->getName();
}

bool PluginAudioEngine::loadPreset (const juce::File& presetFile, juce::String& error)
{
    if (plugin == nullptr)
    {
        error = "No plugin loaded";
        return false;
    }

    juce::MemoryBlock state;
    if (! AUpresetLoader::loadStateBytes (presetFile, state, error))
        return false;

    // Logic-exported .aupresets (often the "initial" list entry) need a clean
    // restore after a host-saved getStateInformation dump. Hold processBlock
    // out via flag — do not hold processLock during setStateInformation, which
    // can stall larger AU state restores on the message thread.
    restoringState.store (true);
    plugin->suspendProcessing (true);
    plugin->reset();
    plugin->setStateInformation (state.getData(), (int) state.getSize());
    plugin->updateHostDisplay (juce::AudioProcessorListener::ChangeDetails()
                                   .withProgramChanged (true)
                                   .withParameterInfoChanged (true)
                                   .withNonParameterStateChanged (true));
    plugin->suspendProcessing (false);
    restoringState.store (false);
    return true;
}

bool PluginAudioEngine::saveCurrentPreset (const juce::File& presetFile, juce::String& error) const
{
    if (plugin == nullptr)
    {
        error = "No plugin loaded";
        return false;
    }

    juce::MemoryBlock state;
    plugin->getStateInformation (state);
    return AUpresetSaver::saveStateBytes (presetFile, state, error);
}

void PluginAudioEngine::destroyEditor (juce::AudioProcessorEditor*& editor)
{
    // Delete our owned editor first. Do not touch getActiveEditor() after the
    // plugin instance may already be tearing down.
    if (editor != nullptr)
    {
        delete editor;
        editor = nullptr;
    }

    if (plugin == nullptr)
        return;

    if (auto* orphan = plugin->getActiveEditor())
        delete orphan;
}

juce::AudioProcessorEditor* PluginAudioEngine::createEditor()
{
    if (plugin == nullptr)
        return nullptr;

    // Ensure no stale active editor remains (AU destructors can skip cleanup).
    if (auto* existing = plugin->getActiveEditor())
    {
        juce::AudioProcessorEditor* toDelete = existing;
        destroyEditor (toDelete);
    }

    return plugin->createEditorAndMakeActive();
}

bool PluginAudioEngine::loadFixture (const juce::File& fixtureFile, juce::String& error)
{
    stopFixture();

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (fixtureFile));
    if (reader == nullptr)
    {
        error = "Could not read fixture WAV: " + fixtureFile.getFullPathName();
        return false;
    }

    fixtureBuffer.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (fixtureBuffer.getArrayOfWritePointers(), (int) reader->numChannels, 0, (int) reader->lengthInSamples);
    fixtureSampleRate = reader->sampleRate;
    fixtureReadPosition = 0.0;
    currentFixtureFile = fixtureFile;
    return true;
}

void PluginAudioEngine::playFixture()
{
    if (fixtureBuffer.getNumSamples() == 0)
        return;

    fixtureReadPosition = 0.0;
    playing = true;
}

void PluginAudioEngine::stopFixture()
{
    playing = false;
    fixtureReadPosition = 0.0;
}

bool PluginAudioEngine::startAudioDevice (juce::String& error)
{
    stopAudioDevice();

    const juce::String result = deviceManager.initialiseWithDefaultDevices (0, 2);
    if (result.isNotEmpty())
    {
        error = result;
        return false;
    }

    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        deviceSampleRate = device->getCurrentSampleRate();
        deviceBlockSize = device->getCurrentBufferSizeSamples();
    }

    if (plugin != nullptr)
        plugin->prepareToPlay (deviceSampleRate, deviceBlockSize);

    deviceManager.addAudioCallback (this);
    applyMidiInputSelection();

    if (hostClockEnabled.load())
        pendingTransport.store (PendingTransport::start);

    return true;
}

void PluginAudioEngine::stopAudioDevice()
{
    stopFixture();
    clearMidiInput();
    hostClockPlaying.store (false);
    resetHostClockTiming();
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();

    if (plugin != nullptr)
        plugin->releaseResources();
}

juce::Array<juce::MidiDeviceInfo> PluginAudioEngine::getMidiInputDevices() const
{
    return juce::MidiInput::getAvailableDevices();
}

juce::String PluginAudioEngine::getSelectedMidiInputName() const
{
    if (selectedMidiIdentifier.isEmpty())
        return {};

    for (const auto& device : juce::MidiInput::getAvailableDevices())
        if (device.identifier == selectedMidiIdentifier)
            return device.name;

    return {};
}

void PluginAudioEngine::clearMidiInput()
{
    if (selectedMidiIdentifier.isNotEmpty())
    {
        deviceManager.removeMidiInputDeviceCallback (selectedMidiIdentifier, this);
        deviceManager.setMidiInputDeviceEnabled (selectedMidiIdentifier, false);
    }

    const juce::ScopedLock lock (midiLock);
    pendingMidi.clear();
}

void PluginAudioEngine::applyMidiInputSelection()
{
    if (selectedMidiIdentifier.isEmpty())
        return;

    deviceManager.setMidiInputDeviceEnabled (selectedMidiIdentifier, true);
    deviceManager.addMidiInputDeviceCallback (selectedMidiIdentifier, this);
}

void PluginAudioEngine::setMidiInputDevice (const juce::String& identifier)
{
    clearMidiInput();
    selectedMidiIdentifier = identifier;
    applyMidiInputSelection();
}

bool PluginAudioEngine::consumeMidiActivity()
{
    return midiActivity.exchange (false);
}

void PluginAudioEngine::setHostClockEnabled (bool enabled)
{
    const bool wasEnabled = hostClockEnabled.exchange (enabled);

    if (enabled && ! wasEnabled)
        pendingTransport.store (PendingTransport::start);
    else if (! enabled && wasEnabled)
        pendingTransport.store (PendingTransport::stop);
}

void PluginAudioEngine::setHostClockBpm (double bpm)
{
    hostClockBpm.store (juce::jlimit (20.0, 999.0, bpm));
}

bool PluginAudioEngine::consumeHostClockQuarterPulse()
{
    return quarterNotePulse.exchange (false);
}

bool PluginAudioEngine::loadMetronomeClick (const juce::File& impulseFile, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (impulseFile));
    if (reader == nullptr)
    {
        error = "Could not read metronome click: " + impulseFile.getFullPathName();
        return false;
    }

    juce::AudioBuffer<float> full ((int) reader->numChannels, (int) reader->lengthInSamples);
    if (! reader->read (&full, 0, (int) reader->lengthInSamples, 0, true, true))
    {
        error = "Failed to decode metronome click: " + impulseFile.getFullPathName();
        return false;
    }

    // fixtures/impulse.wav is a 2s IR with silence then a unit spike — use the peak
    // (the actual impulse) rather than sample 0, which is silent.
    int peakIndex = 0;
    float peakAbs = 0.0f;
    for (int ch = 0; ch < full.getNumChannels(); ++ch)
    {
        const float* data = full.getReadPointer (ch);
        for (int i = 0; i < full.getNumSamples(); ++i)
        {
            const float a = std::abs (data[i]);
            if (a > peakAbs)
            {
                peakAbs = a;
                peakIndex = i;
            }
        }
    }

    if (peakAbs < 1.0e-6f)
    {
        error = "Metronome click file has no impulse energy: " + impulseFile.getFullPathName();
        return false;
    }

    const int clickLength = juce::jmin (256, full.getNumSamples() - peakIndex);
    metronomeClickBuffer.setSize (1, clickLength);
    metronomeClickBuffer.clear();

    for (int i = 0; i < clickLength; ++i)
    {
        float sample = 0.0f;
        for (int ch = 0; ch < full.getNumChannels(); ++ch)
            sample += full.getSample (ch, peakIndex + i);
        sample /= (float) full.getNumChannels();
        metronomeClickBuffer.setSample (0, i, sample);
    }

    metronomeClickPosition = -1;
    pendingClickOffset = -1;
    return true;
}

void PluginAudioEngine::setMetronomeClickEnabled (bool enabled)
{
    metronomeClickEnabled.store (enabled);
    if (! enabled)
    {
        metronomeClickPosition = -1;
        pendingClickOffset = -1;
    }
}

void PluginAudioEngine::resetHostClockTiming()
{
    clockSampleCounter = 0.0;
    clockTicksSinceQuarter = 0;
    playHeadSamples.store (0);
}

juce::Optional<juce::AudioPlayHead::PositionInfo> PluginAudioEngine::getPosition() const
{
    juce::AudioPlayHead::PositionInfo info;

    const double bpm = hostClockBpm.load();
    const bool transportPlaying = hostClockEnabled.load() && hostClockPlaying.load();
    const juce::int64 samples = playHeadSamples.load();
    const double sampleRate = deviceSampleRate > 0.0 ? deviceSampleRate : 44100.0;
    const double seconds = (double) samples / sampleRate;
    const double ppq = seconds * bpm / 60.0;

    info.setBpm (bpm);
    info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
    info.setTimeInSamples (samples);
    info.setTimeInSeconds (seconds);
    info.setPpqPosition (ppq);
    info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
    info.setIsPlaying (transportPlaying);
    info.setIsRecording (false);
    info.setIsLooping (false);

    return info;
}

void PluginAudioEngine::applyPendingHostClockTransport (juce::MidiBuffer& midi)
{
    const auto transport = pendingTransport.exchange (PendingTransport::none);
    switch (transport)
    {
        case PendingTransport::none:
            break;
        case PendingTransport::start:
            hostClockPlaying.store (true);
            resetHostClockTiming();
            midi.addEvent (juce::MidiMessage::midiStart(), 0);
            break;
        case PendingTransport::stop:
            hostClockPlaying.store (false);
            metronomeClickPosition = -1;
            pendingClickOffset = -1;
            midi.addEvent (juce::MidiMessage::midiStop(), 0);
            break;
        case PendingTransport::continue_:
            hostClockPlaying.store (true);
            midi.addEvent (juce::MidiMessage::midiContinue(), 0);
            break;
    }
}

void PluginAudioEngine::generateHostClockMidi (juce::MidiBuffer& midi, int numSamples)
{
    const double bpm = hostClockBpm.load();
    if (bpm <= 0.0)
        return;

    const double samplesPerTick = deviceSampleRate * 60.0 / (bpm * 24.0);
    if (samplesPerTick <= 0.0)
        return;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        clockSampleCounter += 1.0;

        while (clockSampleCounter >= samplesPerTick)
        {
            clockSampleCounter -= samplesPerTick;
            midi.addEvent (juce::MidiMessage::midiClock(), sample);

            if (++clockTicksSinceQuarter >= 24)
            {
                clockTicksSinceQuarter = 0;
                quarterNotePulse.store (true);

                if (metronomeClickEnabled.load() && metronomeClickBuffer.getNumSamples() > 0)
                    pendingClickOffset = sample;
            }
        }
    }
}

void PluginAudioEngine::mixMetronomeClick (juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (metronomeClickBuffer.getNumSamples() <= 0)
        return;

    int startInBlock = 0;
    if (pendingClickOffset >= 0)
    {
        metronomeClickPosition = 0;
        startInBlock = pendingClickOffset;
        pendingClickOffset = -1;
    }

    if (metronomeClickPosition < 0 || startInBlock >= numSamples)
        return;

    const int clickLength = metronomeClickBuffer.getNumSamples();

    for (int i = startInBlock; i < numSamples; ++i)
    {
        if (metronomeClickPosition >= clickLength)
        {
            metronomeClickPosition = -1;
            break;
        }

        const float click = metronomeClickBuffer.getSample (0, metronomeClickPosition++);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.addSample (ch, i, click);
    }

    if (metronomeClickPosition >= clickLength)
        metronomeClickPosition = -1;
}

void PluginAudioEngine::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    bool forward = true;

    if (hostClockEnabled.load())
    {
        if (message.isMidiStart())
        {
            pendingTransport.store (PendingTransport::start);
            forward = false;
        }
        else if (message.isMidiContinue())
        {
            pendingTransport.store (PendingTransport::continue_);
            forward = false;
        }
        else if (message.isMidiStop())
        {
            pendingTransport.store (PendingTransport::stop);
            forward = false;
        }
        else if (hostClockPlaying.load() && message.isMidiClock())
        {
            forward = false;
        }
    }

    if (forward)
    {
        const juce::ScopedLock lock (midiLock);
        pendingMidi.addEvent (message, 0);
    }

    midiActivity.store (true);
}

void PluginAudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    deviceSampleRate = device->getCurrentSampleRate();
    deviceBlockSize = device->getCurrentBufferSizeSamples();

    const juce::ScopedLock lock (processLock);
    if (plugin != nullptr)
        plugin->prepareToPlay (deviceSampleRate, deviceBlockSize);
}

void PluginAudioEngine::audioDeviceStopped()
{
    const juce::ScopedLock lock (processLock);
    if (plugin != nullptr)
        plugin->releaseResources();
}

void PluginAudioEngine::fillFixtureBlock (juce::AudioBuffer<float>& buffer, int numSamples)
{
    buffer.clear();

    const int length = fixtureBuffer.getNumSamples();
    if (length <= 0)
        return;

    const double ratio = fixtureSampleRate / deviceSampleRate;
    const int channels = fixtureBuffer.getNumChannels();

    for (int i = 0; i < numSamples; ++i)
    {
        while (fixtureReadPosition >= (double) length)
            fixtureReadPosition -= (double) length;

        const int index = (int) fixtureReadPosition;
        const float frac = (float) (fixtureReadPosition - (double) index);
        const int index2 = (index + 1) % length;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const int srcCh = ch % channels;
            const float s0 = fixtureBuffer.getSample (srcCh, index);
            const float s1 = fixtureBuffer.getSample (srcCh, index2);
            buffer.setSample (ch, i, s0 + frac * (s1 - s0));
        }

        fixtureReadPosition += ratio;
    }
}

void PluginAudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                                          int numInputChannels,
                                                          float* const* outputChannelData,
                                                          int numOutputChannels,
                                                          int numSamples,
                                                          const juce::AudioIODeviceCallbackContext& context)
{
    juce::ignoreUnused (inputChannelData, numInputChannels, context);

    juce::AudioBuffer<float> buffer (juce::jmax (1, numOutputChannels), numSamples);
    buffer.clear();

    {
        const juce::ScopedLock lock (processLock);

        if (! restoringState.load())
        {
            if (playing)
                fillFixtureBlock (buffer, numSamples);

            if (plugin != nullptr)
            {
                juce::MidiBuffer midi;
                {
                    const juce::ScopedLock midiScopedLock (midiLock);
                    midi.swapWith (pendingMidi);
                }

                const bool clockEnabled = hostClockEnabled.load();
                if (clockEnabled)
                {
                    applyPendingHostClockTransport (midi);

                    if (hostClockPlaying.load())
                        generateHostClockMidi (midi, numSamples);
                }

                plugin->processBlock (buffer, midi);

                if (clockEnabled && hostClockPlaying.load())
                    playHeadSamples.fetch_add (numSamples);
            }

            // Host monitoring click — mixed after the plugin, like a DAW metronome.
            mixMetronomeClick (buffer, numSamples);
        }
    }

    for (int ch = 0; ch < numOutputChannels; ++ch)
    {
        if (outputChannelData[ch] == nullptr)
            continue;

        const int srcCh = ch % buffer.getNumChannels();
        juce::FloatVectorOperations::copy (outputChannelData[ch], buffer.getReadPointer (srcCh), numSamples);
    }
}
