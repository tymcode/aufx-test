#include "PluginAudioEngine.h"
#include "AUpresetLoader.h"
#include "AUpresetSaver.h"
#include "HostAudioHelpers.h"

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
    : hostClock (formatManager, deviceSampleRate),
      midiServices (deviceManager, hostClock),
      hardwareLoop (formatManager, processLock, sendGain, deviceSampleRate, deviceBlockSize)
{
    formatManager.registerBasicFormats();
}

PluginAudioEngine::~PluginAudioEngine()
{
    stopFixture();
    midiServices.clearMidiInput();
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
    plugin->fillInPluginDescription (lastPluginDescription);
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
    lastPluginDescription = description;
    return true;
}

juce::String PluginAudioEngine::getCurrentPluginName() const
{
    if (plugin == nullptr)
        return {};

    return plugin->getName();
}

bool PluginAudioEngine::applyPluginState (const juce::MemoryBlock& state, juce::String& error)
{
    if (plugin == nullptr)
    {
        error = "No plugin loaded";
        return false;
    }

    if (state.isEmpty())
    {
        error = "Empty plugin state";
        return false;
    }

    // Hold processBlock out via flag — do not hold processLock during
    // setStateInformation (can stall larger AU restores on the message thread).
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

bool PluginAudioEngine::reloadCurrentPlugin (juce::String& error)
{
    if (lastPluginDescription.name.isEmpty()
        && lastPluginDescription.fileOrIdentifier.isEmpty())
    {
        error = "No plugin description to reload";
        return false;
    }

    const auto description = lastPluginDescription;
    const auto fixture = currentFixtureFile;
    const bool wasPlaying = playing.load();
    const bool wasLooping = looping.load();
    const bool hwMode = isHardwareMode();
    const bool clockOn = isHostClockEnabled();
    const double bpm = getHostClockBpm();
    const bool clickOn = isMetronomeClickEnabled();
    const float mix = getMixAmount();
    const float sendDb = getSendLevelDb();

    if (! loadPlugin (description, error))
        return false;

    setLooping (wasLooping);
    setMixAmount (mix);
    setSendLevelDb (sendDb);
    setHostClockBpm (bpm);
    setHostClockEnabled (clockOn);
    setMetronomeClickEnabled (clickOn);
    setHardwareMode (hwMode);

    if (fixture.existsAsFile())
    {
        juce::String fixtureError;
        if (! loadFixture (fixture, fixtureError))
        {
            error = fixtureError;
            // Plugin reloaded; fixture failure is non-fatal for preset-list refresh.
        }
    }

    juce::String deviceError;
    if (! startAudioDevice (deviceError))
    {
        error = deviceError;
        return false;
    }

    setPluginProcessingSuspended (false);

    if (wasPlaying && fixture.existsAsFile())
        playFixture();

    return true;
}

double PluginAudioEngine::getFixturePositionSeconds() const
{
    if (fixtureBuffer.getNumSamples() <= 0 || fixtureSampleRate <= 0.0)
        return 0.0;
    return fixtureReadPosition / fixtureSampleRate;
}

double PluginAudioEngine::getFixtureLengthSeconds() const
{
    if (fixtureBuffer.getNumSamples() <= 0 || fixtureSampleRate <= 0.0)
        return 0.0;
    return (double) fixtureBuffer.getNumSamples() / fixtureSampleRate;
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

    return applyPluginState (state, error);
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

bool PluginAudioEngine::loadFixture (const juce::File& audioFile, juce::String& error)
{
    stopFixture();

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (audioFile));
    if (reader == nullptr)
    {
        error = "Could not read source clip: " + audioFile.getFullPathName();
        return false;
    }

    fixtureBuffer.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (fixtureBuffer.getArrayOfWritePointers(), (int) reader->numChannels, 0, (int) reader->lengthInSamples);
    fixtureSampleRate = reader->sampleRate;
    fixtureReadPosition = 0.0;
    currentFixtureFile = audioFile;
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

/**
 * Open either the plain default stereo output (no hardware loop configured)
 * or the configured loop interface with *all* of its channels enabled.
 * Split out of startAudioDevice() so capture flows can re-open the device
 * after an offline render and surface a real error message instead of
 * silently hanging (an earlier version deadlocked the capture UI here).
 */
bool PluginAudioEngine::openConfiguredAudioDevice (juce::String& error)
{
    const auto& hardwareSettings = hardwareLoop.getHardwareLoopSettingsRef();

    if (! hardwareSettings.isConfigured())
    {
        const juce::String result = deviceManager.initialiseWithDefaultDevices (0, 2);
        if (result.isNotEmpty())
        {
            error = result;
            return false;
        }
        return true;
    }

    // Ensure device types are registered.
    if (deviceManager.getAvailableDeviceTypes().isEmpty()
        || deviceManager.getCurrentAudioDevice() == nullptr)
    {
        const juce::String initError = deviceManager.initialise (2, 2, nullptr, true);
        if (deviceManager.getAvailableDeviceTypes().isEmpty() && initError.isNotEmpty())
        {
            error = initError;
            return false;
        }
    }

    auto setup = deviceManager.getAudioDeviceSetup();
    setup.outputDeviceName = hardwareSettings.deviceName;
    setup.inputDeviceName = hardwareSettings.deviceName;
    setup.sampleRate = 0.0; // keep device default / last
    setup.bufferSize = hardwareSettings.bufferSize > 0 ? hardwareSettings.bufferSize : 512;

    // Probe the full channel count so we enable Apollo ins 5-6, etc. — not just
    // CoreAudio's default first stereo pair.
    int numIns = 2;
    int numOuts = 2;
    for (auto* type : deviceManager.getAvailableDeviceTypes())
    {
        if (type == nullptr)
            continue;

        type->scanForDevices();
        std::unique_ptr<juce::AudioIODevice> probe (
            type->createDevice (hardwareSettings.deviceName, hardwareSettings.deviceName));
        if (probe == nullptr)
            continue;

        numIns = juce::jmax (2, probe->getInputChannelNames().size());
        numOuts = juce::jmax (2, probe->getOutputChannelNames().size());
        break;
    }

    setup.useDefaultInputChannels = false;
    setup.useDefaultOutputChannels = false;
    setup.inputChannels.clear();
    setup.outputChannels.clear();
    setup.inputChannels.setRange (0, numIns, true);
    setup.outputChannels.setRange (0, numOuts, true);

    // Also ensure the specifically configured pairs are enabled.
    setup.inputChannels.setBit (hardwareSettings.returnChannelL);
    setup.inputChannels.setBit (hardwareSettings.returnChannelR);
    setup.outputChannels.setBit (hardwareSettings.sendChannelL);
    setup.outputChannels.setBit (hardwareSettings.sendChannelR);
    if (! hardwareSettings.usesSeparateMonitorOutput())
    {
        setup.outputChannels.setBit (hardwareSettings.monitorChannelL);
        setup.outputChannels.setBit (hardwareSettings.monitorChannelR);
    }

    deviceManager.setAudioDeviceSetup (setup, true);

    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        // If the open device still has fewer channels than requested (driver
        // quirk), widen the masks from the live channel-name lists and re-apply.
        const int liveIns = juce::jmax (numIns, device->getInputChannelNames().size());
        const int liveOuts = juce::jmax (numOuts, device->getOutputChannelNames().size());
        if (liveIns > numIns || liveOuts > numOuts
            || device->getActiveInputChannels().countNumberOfSetBits() < liveIns
            || device->getActiveOutputChannels().countNumberOfSetBits() < liveOuts)
        {
            setup = deviceManager.getAudioDeviceSetup();
            setup.useDefaultInputChannels = false;
            setup.useDefaultOutputChannels = false;
            setup.inputChannels.clear();
            setup.outputChannels.clear();
            setup.inputChannels.setRange (0, liveIns, true);
            setup.outputChannels.setRange (0, liveOuts, true);
            deviceManager.setAudioDeviceSetup (setup, true);
        }
    }

    if (deviceManager.getCurrentAudioDevice() == nullptr)
    {
        error = "Failed to open audio device: " + hardwareSettings.deviceName;
        return false;
    }

    return true;
}

bool PluginAudioEngine::startAudioDevice (juce::String& error)
{
    stopAudioDevice();

    if (! openConfiguredAudioDevice (error))
        return false;

    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        deviceSampleRate = device->getCurrentSampleRate();
        deviceBlockSize = device->getCurrentBufferSizeSamples();
    }

    const auto& hardwareSettings = hardwareLoop.getHardwareLoopSettingsRef();
    hardwareLoop.ensureLatencyBufferSize (2, juce::jmax (hardwareSettings.latencySamples + deviceBlockSize * 4,
                                                         deviceBlockSize * 8));

    if (plugin != nullptr)
    {
        plugin->prepareToPlay (deviceSampleRate, deviceBlockSize);
        plugin->suspendProcessing (false);
    }

    if (hardwareSettings.usesSeparateMonitorOutput())
    {
        if (! monitorOutput.startMonitorOutput (hardwareSettings, deviceSampleRate, deviceBlockSize, error))
            return false;
    }

    deviceManager.addAudioCallback (this);
    midiServices.applyMidiInputSelection();

    if (hostClock.isHostClockEnabled())
        hostClock.requestTransport (HostClockMetronome::PendingTransport::start);

    return true;
}

void PluginAudioEngine::setHardwareLoopSettings (const HardwareLoopSettings& settings)
{
    hardwareLoop.setHardwareLoopSettings (settings);
}

HardwareLoopSettings PluginAudioEngine::getHardwareLoopSettings() const
{
    return hardwareLoop.getHardwareLoopSettings();
}

bool PluginAudioEngine::hasHardwareLoopConfigured() const
{
    return hardwareLoop.hasHardwareLoopConfigured();
}

void PluginAudioEngine::setHardwareMode (bool shouldUseHardware)
{
    hardwareLoop.setHardwareMode (shouldUseHardware);
}

bool PluginAudioEngine::autoDetectLatency (const juce::File& impulseFile,
                                           int& outLatencySamples,
                                           float& outLoopGainDb,
                                           juce::String& error,
                                           std::function<void (int current, int total)> onProgress)
{
    return hardwareLoop.autoDetectLatency (impulseFile, outLatencySamples, outLoopGainDb,
                                           error, std::move (onProgress));
}

bool PluginAudioEngine::measureHardwareNoiseFloor (double listenSeconds,
                                                   float& outPeakDb,
                                                   float& outRmsDb,
                                                   float& outDcOffsetL,
                                                   float& outDcOffsetR,
                                                   juce::String& error)
{
    return hardwareLoop.measureReturnNoiseFloor (listenSeconds, outPeakDb, outRmsDb,
                                                 outDcOffsetL, outDcOffsetR, error);
}

bool PluginAudioEngine::captureHardwareToFile (const juce::File& fixtureFile,
                                               const juce::File& outputFile,
                                               double tailSilenceSeconds,
                                               double silenceThresholdDb,
                                               double maxTailSeconds,
                                               juce::String& error,
                                               const std::atomic<bool>* stopRequested,
                                               const std::atomic<bool>* abortRequested,
                                               double targetDurationSeconds,
                                               float dcOffsetL,
                                               float dcOffsetR)
{
    return hardwareLoop.captureHardwareToFile (fixtureFile, outputFile,
                                               tailSilenceSeconds, silenceThresholdDb, maxTailSeconds,
                                               error, stopRequested, abortRequested, targetDurationSeconds,
                                               dcOffsetL, dcOffsetR);
}

void PluginAudioEngine::clearOutputChannels (float* const* outputChannelData, int numOutputChannels, int numSamples)
{
    for (int ch = 0; ch < numOutputChannels; ++ch)
        if (outputChannelData[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);
}

void PluginAudioEngine::stopAudioDevice()
{
    stopFixture();
    midiServices.clearMidiInput();
    hostClock.stopHostClockPlayback();
    deviceManager.removeAudioCallback (this);
    monitorOutput.stopMonitorOutput();
    deviceManager.closeAudioDevice();

    if (plugin != nullptr)
        plugin->releaseResources();
}

juce::Array<juce::MidiDeviceInfo> PluginAudioEngine::getMidiInputDevices() const
{
    return midiServices.getMidiInputDevices();
}

juce::Array<juce::MidiDeviceInfo> PluginAudioEngine::getMidiOutputDevices() const
{
    return midiServices.getMidiOutputDevices();
}

juce::StringArray PluginAudioEngine::getSelectedMidiInputIdentifiers() const
{
    return midiServices.getSelectedMidiInputIdentifiers();
}

juce::StringArray PluginAudioEngine::getSelectedMidiInputNames() const
{
    return midiServices.getSelectedMidiInputNames();
}

void PluginAudioEngine::setMidiInputDevices (const juce::StringArray& identifiers)
{
    midiServices.setMidiInputDevices (identifiers);
}

bool PluginAudioEngine::consumeMidiActivity()
{
    return midiServices.consumeMidiActivity();
}

bool PluginAudioEngine::consumeTransportPlayRequest()
{
    return midiServices.consumeTransportPlayRequest();
}

bool PluginAudioEngine::consumeTransportStopRequest()
{
    return midiServices.consumeTransportStopRequest();
}

void PluginAudioEngine::setHostClockEnabled (bool enabled)
{
    hostClock.setHostClockEnabled (enabled);
}

void PluginAudioEngine::setHostClockBpm (double bpm)
{
    hostClock.setHostClockBpm (bpm);
}

bool PluginAudioEngine::consumeHostClockQuarterPulse()
{
    return hostClock.consumeHostClockQuarterPulse();
}

bool PluginAudioEngine::loadMetronomeClick (const juce::File& impulseFile, juce::String& error)
{
    return hostClock.loadMetronomeClick (impulseFile, error);
}

void PluginAudioEngine::setMetronomeClickEnabled (bool enabled)
{
    hostClock.setMetronomeClickEnabled (enabled);
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

void PluginAudioEngine::setPluginProcessingSuspended (bool shouldSuspend)
{
    const juce::ScopedLock lock (processLock);
    if (plugin != nullptr)
        plugin->suspendProcessing (shouldSuspend);
}

juce::Optional<juce::AudioPlayHead::PositionInfo> PluginAudioEngine::getPosition() const
{
    return hostClock.getPosition();
}

bool PluginAudioEngine::setMidiOutputDevice (const juce::String& identifier, juce::String& error)
{
    return midiServices.setMidiOutputDevice (identifier, error);
}

juce::String PluginAudioEngine::getMidiOutputIdentifier() const
{
    return midiServices.getMidiOutputIdentifier();
}

bool PluginAudioEngine::sendMidiMessage (const juce::MidiMessage& message)
{
    return midiServices.sendMidiMessage (message);
}

bool PluginAudioEngine::sendMidiMessages (const juce::Array<juce::MidiMessage>& messages)
{
    return midiServices.sendMidiMessages (messages);
}

bool PluginAudioEngine::waitForSysexDump (std::function<bool (const juce::MidiMessage&)> isAcceptable,
                                          juce::MidiMessage& outMessage,
                                          int timeoutMs,
                                          juce::String& error)
{
    return midiServices.waitForSysexDump (std::move (isAcceptable), outMessage, timeoutMs, error);
}

void PluginAudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    deviceSampleRate = device->getCurrentSampleRate();
    deviceBlockSize = device->getCurrentBufferSizeSamples();
    bypassFadeLengthSamples = juce::jmax (1, (int) std::lround (deviceSampleRate * bypassFadeSeconds));
    bypassFade = bypassed.load() ? 1.0f : 0.0f;
    hardwareLoop.prepareForAudioDevice (bypassFadeLengthSamples);

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
    HostAudioHelpers::applyEqualPowerCrossfade (wetBuffer,
                                                 dryBuffer,
                                                 bypassFade,
                                                 bypassed.load() ? 1.0f : 0.0f,
                                                 bypassFadeLengthSamples,
                                                 numSamples,
                                                 true);
}

/**
 * The realtime render callback. Signal flow per block:
 *
 *   fixture clip --> send gain --+--> plugin ---+--> software monitor
 *                                |              |
 *                                |         (hardware mode / setup Test only)
 *                                +--> [send pair] --> external FX box
 *                                                         |
 *                                                     [return pair]
 *                                                         |
 *                                                   latency delay line
 *                                                         v
 *                                     hardware crossfade (sw <-> hw)
 *                                               |
 *                                     monitor pair / monitor FIFO
 *
 * In Use Software mode the send pair stays silent so the hardware box is not
 * driven. Hardware mode (and Hardware Audio Setup Test, which mutes the
 * software effect) drive the send pair with the pre-plugin fixture.
 * Two special "loop ops" (latency calibration and hardware capture) hijack
 * the whole callback: they stream a pre-rendered buffer straight to the send
 * pair and record the raw return, skipping the plugin entirely, so captured
 * audio is exactly what the hardware produced.
 */
void PluginAudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                                          int numInputChannels,
                                                          float* const* outputChannelData,
                                                          int numOutputChannels,
                                                          int numSamples,
                                                          const juce::AudioIODeviceCallbackContext& context)
{
    juce::ignoreUnused (context);

    clearOutputChannels (outputChannelData, numOutputChannels, numSamples);

    const auto& hardwareSettings = hardwareLoop.getHardwareLoopSettingsRef();
    const bool loopConfigured = hardwareSettings.isConfigured();
    if (loopConfigured)
        hardwareLoop.pushReturnToLatencyBuffer (inputChannelData, numInputChannels, numSamples);

    // Latency / capture special ops: drive send from loopPlayBuffer, record return.
    if (hardwareLoop.processLoopOpInCallback (inputChannelData, numInputChannels,
                                              outputChannelData, numOutputChannels,
                                              numSamples, monitorOutput))
        return;

    // Normal path. Process at the wider of device/plugin channel counts so
    // multi-bus plugins get all their channels; only the first stereo pair is
    // monitored.
    int processChannels = juce::jmax (1, loopConfigured ? 2 : numOutputChannels);

    {
        // processLock on the audio thread is deliberate: it is only contested
        // during plugin load/unload and state restore, which already stop or
        // suspend audio. It guards against processBlock racing plugin.reset().
        const juce::ScopedLock lock (processLock);

        if (plugin != nullptr)
            processChannels = juce::jmax (processChannels,
                                          plugin->getTotalNumInputChannels(),
                                          plugin->getTotalNumOutputChannels());

        juce::AudioBuffer<float> buffer (processChannels, numSamples);
        buffer.clear();

        juce::AudioBuffer<float> sendBuffer (2, numSamples);
        sendBuffer.clear();

        if (! restoringState.load() && plugin != nullptr && ! plugin->isSuspended())
        {
            const bool mutePlugin = hardwareLoop.isSoftwareEffectMuted();

            if (playing
                && (! plugin->getPluginDescription().isInstrument
                    || allowInstrumentAudioInput.load()))
                fillFixtureBlock (buffer, numSamples);

            const float send = sendGain.load();
            if (send != 1.0f)
                buffer.applyGain (send);

            // Capture what we'll send to hardware (pre-plugin fixture).
            for (int ch = 0; ch < 2; ++ch)
            {
                const int src = juce::jmin (ch, buffer.getNumChannels() - 1);
                if (src >= 0)
                    sendBuffer.copyFrom (ch, 0, buffer, src, 0, numSamples);
            }

            juce::MidiBuffer midi;
            midiServices.swapPendingMidi (midi);

            const bool clockEnabled = hostClock.isHostClockEnabled();
            if (clockEnabled)
            {
                hostClock.applyPendingHostClockTransport (midi);

                if (hostClock.isHostClockPlaying())
                    hostClock.generateHostClockMidi (midi, numSamples);
            }

            if (mutePlugin)
            {
                // Keep hardware send alive; do not feed the software effect.
                buffer.clear();
            }
            else
            {
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
            }

            if (clockEnabled && hostClock.isHostClockPlaying())
                hostClock.advancePlayHead (numSamples);

            if (! mutePlugin)
                hostClock.mixMetronomeClick (buffer, numSamples);
        }
        else if (! restoringState.load())
        {
            if (playing)
            {
                fillFixtureBlock (buffer, numSamples);
                for (int ch = 0; ch < 2; ++ch)
                {
                    const int src = juce::jmin (ch, buffer.getNumChannels() - 1);
                    if (src >= 0)
                        sendBuffer.copyFrom (ch, 0, buffer, src, 0, numSamples);
                }
            }
            hostClock.mixMetronomeClick (buffer, numSamples);
        }

        {
            float peakL = 0.0f;
            float peakR = 0.0f;
            double sumSq = 0.0;
            for (int i = 0; i < numSamples; ++i)
            {
                const float l = sendBuffer.getSample (0, i);
                const float r = sendBuffer.getSample (1, i);
                peakL = juce::jmax (peakL, std::abs (l));
                peakR = juce::jmax (peakR, std::abs (r));
                sumSq += (double) l * (double) l + (double) r * (double) r;
            }
            hardwareLoop.storeSendMeters (peakL, peakR,
                                          (float) std::sqrt (sumSq / (double) juce::jmax (1, numSamples * 2)));
            softwareSendPeakL.store (peakL);
            softwareSendPeakR.store (peakR);
        }

        juce::AudioBuffer<float> monitorBuffer (2, numSamples);
        for (int ch = 0; ch < 2; ++ch)
        {
            const int src = juce::jmin (ch, buffer.getNumChannels() - 1);
            if (src >= 0)
                monitorBuffer.copyFrom (ch, 0, buffer, src, 0, numSamples);
            else
                monitorBuffer.clear (ch, 0, numSamples);
        }

        {
            float retL = 0.0f;
            float retR = 0.0f;
            for (int i = 0; i < numSamples; ++i)
            {
                retL = juce::jmax (retL, std::abs (monitorBuffer.getSample (0, i)));
                retR = juce::jmax (retR, std::abs (monitorBuffer.getSample (1, i)));
            }
            softwareReturnPeakL.store (retL);
            softwareReturnPeakR.store (retR);
        }

        if (loopConfigured)
        {
            juce::AudioBuffer<float> hardwareMonitor (2, numSamples);
            hardwareLoop.readDelayedReturn (hardwareMonitor, numSamples);
            hardwareLoop.applyMonitorCrossfade (monitorBuffer, hardwareMonitor, numSamples);

            // Only drive the analog send when monitoring hardware (or when
            // Hardware Audio Setup mutes the plugin for Test). Software mode
            // must keep the send pair silent so the box is not fed dry audio.
            const bool driveHardwareSend = hardwareLoop.isHardwareMode()
                                           || hardwareLoop.isSoftwareEffectMuted();
            if (driveHardwareSend)
            {
                const int sendL = hardwareSettings.sendChannelL;
                const int sendR = hardwareSettings.sendChannelR;

                if (sendL >= 0 && sendL < numOutputChannels && outputChannelData[sendL] != nullptr)
                    juce::FloatVectorOperations::copy (outputChannelData[sendL],
                                                       sendBuffer.getReadPointer (0), numSamples);
                if (sendR >= 0 && sendR < numOutputChannels && outputChannelData[sendR] != nullptr)
                    juce::FloatVectorOperations::copy (outputChannelData[sendR],
                                                       sendBuffer.getReadPointer (1), numSamples);
            }
            else
            {
                hardwareLoop.storeSendMeters (0.0f, 0.0f, 0.0f);
            }

            hardwareLoop.writeMonitorSamples (monitorBuffer, numSamples,
                                              outputChannelData, numOutputChannels, monitorOutput);
        }
        else
        {
            for (int ch = 0; ch < numOutputChannels; ++ch)
            {
                if (outputChannelData[ch] == nullptr)
                    continue;

                const int srcCh = ch % monitorBuffer.getNumChannels();
                juce::FloatVectorOperations::copy (outputChannelData[ch],
                                                   monitorBuffer.getReadPointer (srcCh),
                                                   numSamples);
            }
        }
    }
}
