#include "PluginAudioEngine.h"
#include "AUpresetLoader.h"
#include "AUpresetSaver.h"

namespace
{
    constexpr double bypassFadeSeconds = 0.008;
    juce::String formatNameForPluginFile (const juce::File& pluginFile)
    {
        if (pluginFile.hasFileExtension (".vst3"))
            return "VST3";
        if (pluginFile.hasFileExtension (".component") || pluginFile.hasFileExtension (".appex"))
            return "AudioUnit";
        return {};
    }

    void configureDefaultBuses (juce::AudioPluginInstance& instance, bool allowInstrumentAudioInput)
    {
        // setBusesLayout requires the same number of buses as the plugin declares.
        // Instruments / samplers (DecentSampler, ASR-V) often expose an optional
        // audio input for sampling; enabling it with no host capture leaves them
        // in a broken state (null deref in the editor / IO setup). Disable those
        // inputs by default; enable when the user opts in via Settings.
        const bool instrument = instance.getPluginDescription().isInstrument;
        juce::AudioProcessor::BusesLayout layout;

        for (int i = 0; i < instance.getBusCount (true); ++i)
        {
            if (instrument && ! allowInstrumentAudioInput)
            {
                layout.inputBuses.add (juce::AudioChannelSet::disabled());
                continue;
            }

            if (auto* bus = instance.getBus (true, i))
            {
                auto channels = bus->getDefaultLayout();
                if (channels.isDisabled())
                    channels = juce::AudioChannelSet::stereo();
                layout.inputBuses.add (channels);
            }
            else
            {
                layout.inputBuses.add (juce::AudioChannelSet::stereo());
            }
        }

        for (int i = 0; i < instance.getBusCount (false); ++i)
        {
            if (auto* bus = instance.getBus (false, i))
            {
                auto channels = bus->isEnabled() ? bus->getCurrentLayout()
                                                 : bus->getDefaultLayout();
                if (channels.isDisabled())
                    channels = juce::AudioChannelSet::stereo();
                layout.outputBuses.add (channels);
            }
            else
            {
                layout.outputBuses.add (juce::AudioChannelSet::stereo());
            }
        }

        if (layout.outputBuses.isEmpty())
            layout.outputBuses.add (juce::AudioChannelSet::stereo());

        if (! instrument && layout.inputBuses.isEmpty())
            layout.inputBuses.add (juce::AudioChannelSet::stereo());

        if (! instance.setBusesLayout (layout))
            instance.enableAllBuses();
    }

    std::unique_ptr<juce::AudioPluginInstance> createPluginInstance (const juce::File& pluginFile,
                                                                     double sampleRate,
                                                                     int blockSize,
                                                                     bool allowInstrumentAudioInput,
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

        configureDefaultBuses (*instance, allowInstrumentAudioInput);
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
    bypassed.store (false);
    bypassFade = 0.0f;

    {
        const juce::ScopedLock lock (processLock);
        plugin.reset();
    }

    auto instance = createPluginInstance (pluginFile, deviceSampleRate, deviceBlockSize,
                                          allowInstrumentAudioInput.load(), error);
    if (instance == nullptr)
        return false;

    instance->setPlayHead (this);
    instance->suspendProcessing (true);

    const juce::ScopedLock lock (processLock);
    plugin = std::move (instance);
    return true;
}

bool PluginAudioEngine::loadPlugin (const juce::PluginDescription& description, juce::String& error)
{
    stopFixture();
    stopAudioDevice();
    bypassed.store (false);
    bypassFade = 0.0f;

    {
        const juce::ScopedLock lock (processLock);
        plugin.reset();
    }

    juce::AudioPluginFormatManager pluginFormats;
    juce::addDefaultFormatsToManager (pluginFormats);

    juce::String loadError;
    auto instance = pluginFormats.createPluginInstance (description, deviceSampleRate, deviceBlockSize, loadError);

    if (instance == nullptr)
    {
        error = loadError.isNotEmpty() ? loadError : juce::String ("Failed to create plugin instance");
        return false;
    }

    configureDefaultBuses (*instance, allowInstrumentAudioInput.load());
    instance->setPlayHead (this);
    // Stay suspended until the editor is up and the audio device starts — avoids
    // AU/WebView plugins (e.g. Lunacy BEAM) racing processBlock during UI init.
    instance->suspendProcessing (true);

    const juce::ScopedLock lock (processLock);
    plugin = std::move (instance);
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

    // Keep DSP suspended while WebView/Cocoa editors initialise (can take seconds).
    // Always leave suspended here — startAudioDevice() is responsible for resuming.
    plugin->suspendProcessing (true);

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
    playing.store (true);
}

void PluginAudioEngine::stopFixture()
{
    playing.store (false);
    fixtureReadPosition = 0.0;
}

void PluginAudioEngine::setBypassed (bool shouldBypass)
{
    bypassed.store (shouldBypass);
}

void PluginAudioEngine::setMixAmount (float amount)
{
    mixAmount.store (juce::jlimit (0.0f, 1.0f, amount));
}

void PluginAudioEngine::setSendLevelDb (float decibels)
{
    sendLevelDb.store (decibels);

    if (decibels <= -119.9f)
        sendGain.store (0.0f);
    else
        sendGain.store (juce::Decibels::decibelsToGain (decibels));
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
    {
        plugin->prepareToPlay (deviceSampleRate, deviceBlockSize);
        plugin->suspendProcessing (false);
    }

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

juce::StringArray PluginAudioEngine::getSelectedMidiInputNames() const
{
    juce::StringArray names;
    const auto available = juce::MidiInput::getAvailableDevices();

    for (const auto& id : selectedMidiIdentifiers)
        for (const auto& device : available)
            if (device.identifier == id)
                names.add (device.name);

    return names;
}

void PluginAudioEngine::clearMidiInput()
{
    for (const auto& id : selectedMidiIdentifiers)
    {
        if (id.isEmpty())
            continue;

        deviceManager.removeMidiInputDeviceCallback (id, this);
        deviceManager.setMidiInputDeviceEnabled (id, false);
    }

    const juce::ScopedLock lock (midiLock);
    pendingMidi.clear();
}

void PluginAudioEngine::applyMidiInputSelection()
{
    for (const auto& id : selectedMidiIdentifiers)
    {
        if (id.isEmpty())
            continue;

        deviceManager.setMidiInputDeviceEnabled (id, true);
        deviceManager.addMidiInputDeviceCallback (id, this);
    }
}

void PluginAudioEngine::setMidiInputDevices (const juce::StringArray& identifiers)
{
    clearMidiInput();
    selectedMidiIdentifiers.clear();

    for (const auto& id : identifiers)
        if (id.isNotEmpty() && ! selectedMidiIdentifiers.contains (id))
            selectedMidiIdentifiers.add (id);

    applyMidiInputSelection();
}

bool PluginAudioEngine::consumeMidiActivity()
{
    return midiActivity.exchange (false);
}

bool PluginAudioEngine::consumeTransportPlayRequest()
{
    return transportPlayRequest.exchange (false);
}

bool PluginAudioEngine::consumeTransportStopRequest()
{
    return transportStopRequest.exchange (false);
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

void PluginAudioEngine::setAllowInstrumentAudioInput (bool allow)
{
    const bool previous = allowInstrumentAudioInput.exchange (allow);
    if (previous == allow)
        return;

    const juce::ScopedLock lock (processLock);
    if (plugin == nullptr || ! plugin->getPluginDescription().isInstrument)
        return;

    const bool wasSuspended = plugin->isSuspended();
    plugin->suspendProcessing (true);
    configureDefaultBuses (*plugin, allow);
    plugin->prepareToPlay (deviceSampleRate, deviceBlockSize);

    if (! wasSuspended && deviceManager.getCurrentAudioDevice() != nullptr)
        plugin->suspendProcessing (false);
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

    // Transport from MIDI realtime (Start/Stop/Continue), MMC, or Mackie/HUI notes.
    // Oxygen Pro DAW mode (Mackie / Mackie/HUI) sends notes 93/94 on the HUI port
    // (e.g. iConnectMIDI "HST" ports), not MIDI Start/Stop.
    const auto transport = classifyTransportMessage (message);
    if (transport != PendingTransport::none)
    {
        if (hostClockEnabled.load())
        {
            pendingTransport.store (transport);
            forward = false;
        }

        // Always drive source-clip play/stop so DAW buttons do something audible
        // even when Host Clock is off.
        if (transport == PendingTransport::start || transport == PendingTransport::continue_)
            transportPlayRequest.store (true);
        else if (transport == PendingTransport::stop)
            transportStopRequest.store (true);
    }
    else if (hostClockEnabled.load() && hostClockPlaying.load() && message.isMidiClock())
    {
        forward = false;
    }

    if (forward)
    {
        const juce::ScopedLock lock (midiLock);
        pendingMidi.addEvent (message, 0);
    }

    midiActivity.store (true);
}

PluginAudioEngine::PendingTransport PluginAudioEngine::classifyTransportMessage (const juce::MidiMessage& message)
{
    if (message.isMidiStart())
        return PendingTransport::start;
    if (message.isMidiContinue())
        return PendingTransport::continue_;
    if (message.isMidiStop())
        return PendingTransport::stop;

    // Mackie Control / Logic Control transport (ch. 1 notes).
    // Stop = 93, Play = 94. Accept any channel — some surfaces remap.
    if (message.isNoteOn (false) && message.getVelocity() > 0)
    {
        switch (message.getNoteNumber())
        {
            case 94: return PendingTransport::start;
            case 93: return PendingTransport::stop;
            default: break;
        }
    }

    // MMC: F0 7F <dev> 06 <cmd> F7 — Play=02, Deferred Play=03, Continue=04, Stop=01
    if (message.isSysEx())
    {
        const auto* data = message.getSysExData();
        const int n = message.getSysExDataSize();
        if (n >= 4 && (uint8_t) data[0] == 0x7f && (uint8_t) data[2] == 0x06)
        {
            switch ((uint8_t) data[3])
            {
                case 0x01: return PendingTransport::stop;
                case 0x02: return PendingTransport::start;
                case 0x03: return PendingTransport::start;
                case 0x04: return PendingTransport::continue_;
                default: break;
            }
        }
    }

    return PendingTransport::none;
}

void PluginAudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    deviceSampleRate = device->getCurrentSampleRate();
    deviceBlockSize = device->getCurrentBufferSizeSamples();
    bypassFadeLengthSamples = juce::jmax (1, (int) std::lround (deviceSampleRate * bypassFadeSeconds));
    bypassFade = bypassed.load() ? 1.0f : 0.0f;

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
    const bool loop = looping.load();

    for (int i = 0; i < numSamples; ++i)
    {
        if (fixtureReadPosition >= (double) length)
        {
            if (loop)
            {
                while (fixtureReadPosition >= (double) length)
                    fixtureReadPosition -= (double) length;
            }
            else
            {
                // One-shot finished: leave the rest of the block silent and stop.
                playing.store (false);
                break;
            }
        }

        const int index = (int) fixtureReadPosition;
        const float frac = (float) (fixtureReadPosition - (double) index);
        const int index2 = loop ? (index + 1) % length
                                 : juce::jmin (index + 1, length - 1);

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

void PluginAudioEngine::applyBypassCrossfade (juce::AudioBuffer<float>& wetBuffer,
                                              const juce::AudioBuffer<float>& dryBuffer,
                                              int numSamples)
{
    const float target = bypassed.load() ? 1.0f : 0.0f;

    if (bypassFade <= 0.0f && target <= 0.0f)
        return;

    const float fadeStart = bypassFade;
    float fadeEnd = fadeStart;

    if (std::abs (fadeStart - target) > 1.0e-6f)
    {
        const float step = (float) numSamples / (float) bypassFadeLengthSamples;

        if (target > fadeStart)
            fadeEnd = juce::jmin (target, fadeStart + step);
        else
            fadeEnd = juce::jmax (target, fadeStart - step);
    }

    const float fadeStep = (fadeEnd - fadeStart) / (float) juce::jmax (1, numSamples);
    constexpr float halfPi = juce::MathConstants<float>::halfPi;

    for (int ch = 0; ch < wetBuffer.getNumChannels(); ++ch)
    {
        auto* wet = wetBuffer.getWritePointer (ch);
        const auto* dry = dryBuffer.getReadPointer (ch);
        float fade = fadeStart;

        for (int i = 0; i < numSamples; ++i)
        {
            const float dryGain = std::sin (fade * halfPi);
            const float wetGain = std::cos (fade * halfPi);
            wet[i] = dry[i] * dryGain + wet[i] * wetGain;
            fade += fadeStep;
        }
    }

    bypassFade = fadeEnd;
}

void PluginAudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                                          int numInputChannels,
                                                          float* const* outputChannelData,
                                                          int numOutputChannels,
                                                          int numSamples,
                                                          const juce::AudioIODeviceCallbackContext& context)
{
    juce::ignoreUnused (inputChannelData, numInputChannels, context);

    int processChannels = juce::jmax (1, numOutputChannels);

    {
        const juce::ScopedLock lock (processLock);

        if (plugin != nullptr)
            processChannels = juce::jmax (processChannels,
                                          plugin->getTotalNumInputChannels(),
                                          plugin->getTotalNumOutputChannels());

        juce::AudioBuffer<float> buffer (processChannels, numSamples);
        buffer.clear();

        if (! restoringState.load() && plugin != nullptr && ! plugin->isSuspended())
        {
            // Fixture → plugin inputs. Instruments only when Settings allows it.
            if (playing
                && (! plugin->getPluginDescription().isInstrument
                    || allowInstrumentAudioInput.load()))
                fillFixtureBlock (buffer, numSamples);

            const float send = sendGain.load();
            if (send != 1.0f)
                buffer.applyGain (send);

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

            const float wetMix = mixAmount.load();
            const bool blendDry = wetMix < 1.0f;
            juce::AudioBuffer<float> dryBuffer;
            dryBuffer.setSize (buffer.getNumChannels(), numSamples, false, false, true);

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

            plugin->processBlock (buffer, midi);

            if (blendDry)
            {
                const float dryMix = 1.0f - wetMix;
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                {
                    auto* wet = buffer.getWritePointer (ch);
                    const auto* dry = dryBuffer.getReadPointer (ch);
                    for (int i = 0; i < numSamples; ++i)
                        wet[i] = dry[i] * dryMix + wet[i] * wetMix;
                }
            }

            applyBypassCrossfade (buffer, dryBuffer, numSamples);

            if (clockEnabled && hostClockPlaying.load())
                playHeadSamples.fetch_add (numSamples);

            // Host monitoring click — mixed after the plugin, like a DAW metronome.
            mixMetronomeClick (buffer, numSamples);
        }
        else if (! restoringState.load())
        {
            if (playing)
                fillFixtureBlock (buffer, numSamples);
            mixMetronomeClick (buffer, numSamples);
        }

        for (int ch = 0; ch < numOutputChannels; ++ch)
        {
            if (outputChannelData[ch] == nullptr)
                continue;

            const int srcCh = ch % buffer.getNumChannels();
            juce::FloatVectorOperations::copy (outputChannelData[ch], buffer.getReadPointer (srcCh), numSamples);
        }
    }
}
