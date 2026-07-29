#include "PluginAudioEngine.h"
#include "AUpresetLoader.h"
#include "AUpresetSaver.h"

namespace
{
    constexpr double bypassFadeSeconds = 0.008;
    constexpr int monitorRingCapacity = 32768;

    /**
     * Turn the persisted monitor-output preference into a concrete CoreAudio
     * device name. The sentinel "<System Default Output>" is stored instead of
     * a resolved name so the preference keeps following the OS default when
     * the user changes it in Sound settings; we resolve it lazily here.
     */
    juce::String resolveMonitorOutputDeviceName (juce::AudioDeviceManager& deviceManager,
                                                 const juce::String& configuredName)
    {
        if (configuredName.isNotEmpty()
            && configuredName != HardwareLoopSettings::systemDefaultMonitorOutputName)
            return configuredName;

        for (auto* type : deviceManager.getAvailableDeviceTypes())
        {
            if (type == nullptr)
                continue;

            type->scanForDevices();
            const auto names = type->getDeviceNames (false);
            const int defaultIndex = type->getDefaultDeviceIndex (false);
            if (defaultIndex >= 0 && defaultIndex < names.size())
                return names[defaultIndex];
        }

        return configuredName;
    }

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

/**
 * Callback for the *second* output device (the separate monitor output).
 * It only drains the monitor FIFO that the main loop-device callback fills.
 * A nested struct rather than making the engine itself the callback for both
 * devices — juce::AudioDeviceManager identifies callbacks by pointer, so the
 * two devices need distinct callback objects.
 */
struct PluginAudioEngine::MonitorOutputHandler : juce::AudioIODeviceCallback
{
    explicit MonitorOutputHandler (PluginAudioEngine& ownerIn) : owner (ownerIn) {}

    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override
    {
        juce::ignoreUnused (inputChannelData, numInputChannels, context);
        owner.pullMonitorOutput (outputChannelData, numOutputChannels, numSamples);
    }

    void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}

    PluginAudioEngine& owner;
};

PluginAudioEngine::PluginAudioEngine()
{
    formatManager.registerBasicFormats();
    monitorRingBuffer.setSize (2, monitorRingCapacity, false, true, true);
    monitorOutputHandler = std::make_unique<MonitorOutputHandler> (*this);
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

/**
 * Open either the plain default stereo output (no hardware loop configured)
 * or the configured loop interface with *all* of its channels enabled.
 * Split out of startAudioDevice() so capture flows can re-open the device
 * after an offline render and surface a real error message instead of
 * silently hanging (an earlier version deadlocked the capture UI here).
 */
bool PluginAudioEngine::openConfiguredAudioDevice (juce::String& error)
{
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

    ensureLatencyBufferSize (2, juce::jmax (hardwareSettings.latencySamples + deviceBlockSize * 4,
                                            deviceBlockSize * 8));

    if (plugin != nullptr)
    {
        plugin->prepareToPlay (deviceSampleRate, deviceBlockSize);
        plugin->suspendProcessing (false);
    }

    if (hardwareSettings.usesSeparateMonitorOutput())
    {
        if (! startMonitorOutput (error))
            return false;
    }

    deviceManager.addAudioCallback (this);
    applyMidiInputSelection();

    if (hostClockEnabled.load())
        pendingTransport.store (PendingTransport::start);

    return true;
}

void PluginAudioEngine::setHardwareLoopSettings (const HardwareLoopSettings& settings)
{
    const juce::ScopedLock lock (processLock);
    hardwareSettings = settings;
    ensureLatencyBufferSize (2, juce::jmax (settings.latencySamples + deviceBlockSize * 4,
                                            deviceBlockSize * 8));
}

HardwareLoopSettings PluginAudioEngine::getHardwareLoopSettings() const
{
    return hardwareSettings;
}

bool PluginAudioEngine::hasHardwareLoopConfigured() const
{
    return hardwareSettings.isConfigured();
}

void PluginAudioEngine::setHardwareMode (bool shouldUseHardware)
{
    if (shouldUseHardware && ! hardwareSettings.isConfigured())
        return;

    hardwareMode.store (shouldUseHardware);
}

void PluginAudioEngine::ensureLatencyBufferSize (int numChannels, int capacity)
{
    capacity = juce::jmax (capacity, 1);
    if (latencyCapacity >= capacity && latencyBuffer.getNumChannels() >= numChannels)
        return;

    latencyBuffer.setSize (numChannels, capacity, false, true, false);
    latencyBuffer.clear();
    latencyWritePos = 0;
    latencyCapacity = capacity;
}

/**
 * Audio thread: copy the hardware return pair into the circular latency
 * buffer and update the return meters in the same pass (one loop instead of
 * two over the input). Mono-safe: a missing right channel mirrors the left.
 */
void PluginAudioEngine::pushReturnToLatencyBuffer (const float* const* inputChannelData,
                                                    int numInputChannels,
                                                    int numSamples)
{
    if (latencyCapacity <= 0 || inputChannelData == nullptr)
        return;

    const int retL = hardwareSettings.returnChannelL;
    const int retR = hardwareSettings.returnChannelR;
    const float* inL = (retL >= 0 && retL < numInputChannels) ? inputChannelData[retL] : nullptr;
    const float* inR = (retR >= 0 && retR < numInputChannels) ? inputChannelData[retR] : nullptr;

        float peakL = 0.0f;
        float peakR = 0.0f;
        double sumSq = 0.0;
        int meterSamples = 0;

        for (int i = 0; i < numSamples; ++i)
        {
            const float l = inL != nullptr ? inL[i] : 0.0f;
            const float r = inR != nullptr ? inR[i] : l;
            latencyBuffer.setSample (0, latencyWritePos, l);
            if (latencyBuffer.getNumChannels() > 1)
                latencyBuffer.setSample (1, latencyWritePos, r);

            peakL = juce::jmax (peakL, std::abs (l));
            peakR = juce::jmax (peakR, std::abs (r));
            sumSq += (double) l * (double) l + (double) r * (double) r;
            meterSamples += 2;

            latencyWritePos = (latencyWritePos + 1) % latencyCapacity;
        }

        returnPeakL.store (peakL);
        returnPeakR.store (peakR);
        if (meterSamples > 0)
            returnRms.store ((float) std::sqrt (sumSq / (double) meterSamples));
    }

/**
 * Audio thread: read the return signal latencySamples behind the write head,
 * i.e. time-aligned with the software path so A/B'ing hardware vs plugin
 * doesn't smear transients.
 */
void PluginAudioEngine::readDelayedReturn (juce::AudioBuffer<float>& dest, int numSamples)
{
    dest.clear();
    if (latencyCapacity <= 0)
        return;

    const int delay = juce::jlimit (0, latencyCapacity - 1, hardwareSettings.latencySamples);
    int readPos = latencyWritePos - delay - numSamples;
    while (readPos < 0)
        readPos += latencyCapacity;

    const int channels = juce::jmin (2, dest.getNumChannels());
    for (int i = 0; i < numSamples; ++i)
    {
        for (int ch = 0; ch < channels; ++ch)
            dest.setSample (ch, i, latencyBuffer.getSample (ch % latencyBuffer.getNumChannels(), readPos));

        readPos = (readPos + 1) % latencyCapacity;
    }
}

void PluginAudioEngine::clearOutputChannels (float* const* outputChannelData, int numOutputChannels, int numSamples)
{
    for (int ch = 0; ch < numOutputChannels; ++ch)
        if (outputChannelData[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);
}

/**
 * Route the final stereo monitor mix to wherever the user wants to hear it:
 * either a channel pair on the loop device itself, or (for screen-recording
 * setups) the FIFO feeding the separate monitor output device.
 */
void PluginAudioEngine::writeMonitorSamples (const juce::AudioBuffer<float>& stereoMonitor,
                                             int numSamples,
                                             float* const* outputChannelData,
                                             int numOutputChannels)
{
    if (hardwareSettings.usesSeparateMonitorOutput())
    {
        pushMonitorOutput (stereoMonitor.getReadPointer (0),
                           stereoMonitor.getReadPointer (1),
                           numSamples);
        return;
    }

    const int monL = hardwareSettings.monitorChannelL;
    const int monR = hardwareSettings.monitorChannelR;

    if (monL >= 0 && monL < numOutputChannels && outputChannelData[monL] != nullptr)
        juce::FloatVectorOperations::copy (outputChannelData[monL],
                                           stereoMonitor.getReadPointer (0), numSamples);
    if (monR >= 0 && monR < numOutputChannels && outputChannelData[monR] != nullptr)
        juce::FloatVectorOperations::copy (outputChannelData[monR],
                                           stereoMonitor.getReadPointer (1), numSamples);
}

void PluginAudioEngine::pushMonitorOutput (const float* left, const float* right, int numSamples)
{
    if (! monitorOutputActive.load() || left == nullptr || right == nullptr || numSamples <= 0)
        return;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    monitorFifo.prepareToWrite (numSamples, start1, size1, start2, size2);

    const int written = size1 + size2;
    if (written <= 0)
        return;

    auto writeBlock = [&] (int ringStart, int count, const float*& srcL, const float*& srcR)
    {
        monitorRingBuffer.copyFrom (0, ringStart, srcL, count);
        monitorRingBuffer.copyFrom (1, ringStart, srcR, count);
        srcL += count;
        srcR += count;
    };

    writeBlock (start1, size1, left, right);
    writeBlock (start2, size2, left, right);
    monitorFifo.finishedWrite (written);
}

void PluginAudioEngine::pullMonitorOutput (float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples)
{
    for (int ch = 0; ch < numOutputChannels; ++ch)
        if (outputChannelData[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    monitorFifo.prepareToRead (numSamples, start1, size1, start2, size2);

    const int available = size1 + size2;
    if (available <= 0)
        return;

    int outPos = 0;
    auto readBlock = [&] (int ringStart, int count)
    {
        if (outputChannelData[0] != nullptr)
            juce::FloatVectorOperations::copy (outputChannelData[0] + outPos,
                                               monitorRingBuffer.getReadPointer (0, ringStart),
                                               count);
        if (numOutputChannels > 1 && outputChannelData[1] != nullptr)
            juce::FloatVectorOperations::copy (outputChannelData[1] + outPos,
                                               monitorRingBuffer.getReadPointer (1, ringStart),
                                               count);
        else if (outputChannelData[0] != nullptr)
            juce::FloatVectorOperations::copy (outputChannelData[0] + outPos,
                                               monitorRingBuffer.getReadPointer (1, ringStart),
                                               count);

        outPos += count;
    };

    readBlock (start1, size1);
    readBlock (start2, size2);
    monitorFifo.finishedRead (available);
}

bool PluginAudioEngine::startMonitorOutput (juce::String& error)
{
    stopMonitorOutput();

    if (! hardwareSettings.usesSeparateMonitorOutput())
        return true;

    if (monitorDeviceManager.getAvailableDeviceTypes().isEmpty())
    {
        const juce::String initError = monitorDeviceManager.initialise (0, 2, nullptr, true);
        if (initError.isNotEmpty())
        {
            error = "Monitor output: " + initError;
            return false;
        }
    }

    const auto deviceName = resolveMonitorOutputDeviceName (monitorDeviceManager,
                                                            hardwareSettings.monitorOutputDeviceName);
    if (deviceName.isEmpty())
    {
        error = "Monitor output: no output device available";
        return false;
    }

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.outputDeviceName = deviceName;
    setup.inputDeviceName.clear();
    setup.sampleRate = deviceSampleRate > 0.0 ? deviceSampleRate : 0.0;
    setup.bufferSize = deviceBlockSize > 0 ? deviceBlockSize : hardwareSettings.bufferSize;
    setup.useDefaultInputChannels = false;
    setup.useDefaultOutputChannels = true;

    const juce::String setupError = monitorDeviceManager.setAudioDeviceSetup (setup, true);
    if (setupError.isNotEmpty())
    {
        error = "Monitor output (\"" + deviceName + "\"): " + setupError;
        return false;
    }

    monitorFifo.reset();
    monitorRingBuffer.clear();
    monitorDeviceManager.addAudioCallback (monitorOutputHandler.get());
    monitorOutputActive.store (true);
    return true;
}

void PluginAudioEngine::stopMonitorOutput()
{
    monitorOutputActive.store (false);

    if (monitorOutputHandler != nullptr)
        monitorDeviceManager.removeAudioCallback (monitorOutputHandler.get());

    monitorDeviceManager.closeAudioDevice();
    monitorFifo.reset();
    monitorRingBuffer.clear();
}

/**
 * Equal-power (sin/cos) crossfade between the software plugin output and the
 * latency-compensated hardware return, ramped over ~8 ms so toggling "Use
 * Hardware" never clicks. Same shape as applyBypassCrossfade; kept as two
 * functions because each carries its own fade state and buffer roles.
 */
void PluginAudioEngine::applyHardwareMonitorCrossfade (juce::AudioBuffer<float>& softwareBuffer,
                                                       const juce::AudioBuffer<float>& hardwareBuffer,
                                                       int numSamples)
{
    const float target = hardwareMode.load() ? 1.0f : 0.0f;

    // Fully on the software side and staying there: nothing to mix.
    if (hardwareFade <= 0.0f && target <= 0.0f)
        return;

    const float fadeStart = hardwareFade;
    float fadeEnd = fadeStart;

    if (std::abs (fadeStart - target) > 1.0e-6f)
    {
        const float step = (float) numSamples / (float) hardwareFadeLengthSamples;
        fadeEnd = target > fadeStart ? juce::jmin (target, fadeStart + step)
                                     : juce::jmax (target, fadeStart - step);
    }

    const float fadeStep = (fadeEnd - fadeStart) / (float) juce::jmax (1, numSamples);
    constexpr float halfPi = juce::MathConstants<float>::halfPi;
    const int channels = juce::jmin (softwareBuffer.getNumChannels(), hardwareBuffer.getNumChannels());

    for (int ch = 0; ch < channels; ++ch)
    {
        auto* soft = softwareBuffer.getWritePointer (ch);
        const auto* hard = hardwareBuffer.getReadPointer (ch);
        float fade = fadeStart;

        for (int i = 0; i < numSamples; ++i)
        {
            const float hwGain = std::sin (fade * halfPi);
            const float swGain = std::cos (fade * halfPi);
            soft[i] = soft[i] * swGain + hard[i] * hwGain;
            fade += fadeStep;
        }
    }

    hardwareFade = fadeEnd;
}

void PluginAudioEngine::stopAudioDevice()
{
    stopFixture();
    clearMidiInput();
    hostClockPlaying.store (false);
    resetHostClockTiming();
    deviceManager.removeAudioCallback (this);
    stopMonitorOutput();
    deviceManager.closeAudioDevice();

    if (plugin != nullptr)
        plugin->releaseResources();
}

juce::Array<juce::MidiDeviceInfo> PluginAudioEngine::getMidiInputDevices() const
{
    return juce::MidiInput::getAvailableDevices();
}

juce::Array<juce::MidiDeviceInfo> PluginAudioEngine::getMidiOutputDevices() const
{
    return juce::MidiOutput::getAvailableDevices();
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

void PluginAudioEngine::setPluginProcessingSuspended (bool shouldSuspend)
{
    const juce::ScopedLock lock (processLock);
    if (plugin != nullptr)
        plugin->suspendProcessing (shouldSuspend);
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
    if (message.isSysEx() && collectSysex.load())
    {
        const juce::ScopedLock lock (sysexLock);
        pendingSysex.add (message);
    }

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
    hardwareFadeLengthSamples = bypassFadeLengthSamples;
    bypassFade = bypassed.load() ? 1.0f : 0.0f;
    hardwareFade = hardwareMode.load() ? 1.0f : 0.0f;
    ensureLatencyBufferSize (2, juce::jmax (hardwareSettings.latencySamples + deviceBlockSize * 4,
                                            deviceBlockSize * 8));

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

/**
 * The realtime render callback. Signal flow per block:
 *
 *   fixture clip --> send gain --+--> [send pair] --> external FX box
 *                                |                        |
 *                                +--> plugin ---+     [return pair]
 *                                               |         |
 *                                               |   latency delay line
 *                                               v         v
 *                                     hardware crossfade (sw <-> hw)
 *                                               |
 *                                     monitor pair / monitor FIFO
 *
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

    const bool loopConfigured = hardwareSettings.isConfigured();
    if (loopConfigured)
        pushReturnToLatencyBuffer (inputChannelData, numInputChannels, numSamples);

    // Latency / capture special ops: drive send from loopPlayBuffer, record return.
    const auto op = loopOp.load();
    if (op != LoopOp::idle && loopConfigured)
    {
        const int sendL = hardwareSettings.sendChannelL;
        const int sendR = hardwareSettings.sendChannelR;

        double sendSumSq = 0.0;
        int sendCount = 0;
        float peakL = 0.0f;
        float peakR = 0.0f;
        juce::AudioBuffer<float> loopMonitor (2, numSamples);
        loopMonitor.clear();

        for (int i = 0; i < numSamples; ++i)
        {
            float l = 0.0f, r = 0.0f;
            if (loopPlayPosition < loopPlayBuffer.getNumSamples())
            {
                l = loopPlayBuffer.getSample (0, loopPlayPosition);
                r = loopPlayBuffer.getNumChannels() > 1
                        ? loopPlayBuffer.getSample (1, loopPlayPosition)
                        : l;
                ++loopPlayPosition;
            }

            if (sendL >= 0 && sendL < numOutputChannels && outputChannelData[sendL] != nullptr)
                outputChannelData[sendL][i] = l;
            if (sendR >= 0 && sendR < numOutputChannels && outputChannelData[sendR] != nullptr)
                outputChannelData[sendR][i] = r;

            loopMonitor.setSample (0, i, l);
            loopMonitor.setSample (1, i, r);

            peakL = juce::jmax (peakL, std::abs (l));
            peakR = juce::jmax (peakR, std::abs (r));
            sendSumSq += (double) l * (double) l + (double) r * (double) r;
            sendCount += 2;

            if (loopRecordPosition < loopRecordCapacity)
            {
                const int retL = hardwareSettings.returnChannelL;
                const int retR = hardwareSettings.returnChannelR;
                const float rl = (retL >= 0 && retL < numInputChannels && inputChannelData != nullptr
                                  && inputChannelData[retL] != nullptr)
                                     ? inputChannelData[retL][i] : 0.0f;
                const float rr = (retR >= 0 && retR < numInputChannels && inputChannelData != nullptr
                                  && inputChannelData[retR] != nullptr)
                                     ? inputChannelData[retR][i] : rl;
                loopRecordBuffer.setSample (0, loopRecordPosition, rl);
                if (loopRecordBuffer.getNumChannels() > 1)
                    loopRecordBuffer.setSample (1, loopRecordPosition, rr);
                ++loopRecordPosition;
            }
        }

        writeMonitorSamples (loopMonitor, numSamples, outputChannelData, numOutputChannels);

        if (sendCount > 0)
            sendRms.store ((float) std::sqrt (sendSumSq / (double) sendCount));
        sendPeakL.store (peakL);
        sendPeakR.store (peakR);

        // Auto-finish logic. Capture ops are normally ended by the message
        // thread (silence tail / target duration in captureHardwareToFile);
        // this is the audio-side backstop: play finished plus 2 s of extra
        // return, or the record buffer filled.
        const bool playDone = loopPlayPosition >= loopPlayBuffer.getNumSamples();
        const bool recordFull = loopRecordPosition >= loopRecordCapacity;
        if (playDone && (op == LoopOp::calibrate || recordFull
                         || loopRecordPosition >= loopPlayBuffer.getNumSamples()
                                                    + (int) (deviceSampleRate * 2.0)))
        {
            // For calibrate, keep recording a bit after the impulse ends.
            if (op == LoopOp::calibrate && ! recordFull
                && loopRecordPosition < loopPlayBuffer.getNumSamples()
                                            + (int) (deviceSampleRate * 1.5))
            {
                // keep going
            }
            else
            {
                loopOp.store (LoopOp::idle);
                loopOpFinished.store (true);
            }
        }

        return;
    }

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
            const bool mutePlugin = softwareEffectMuted.load();

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

            if (clockEnabled && hostClockPlaying.load())
                playHeadSamples.fetch_add (numSamples);

            if (! mutePlugin)
                mixMetronomeClick (buffer, numSamples);
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
            mixMetronomeClick (buffer, numSamples);
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
            sendPeakL.store (peakL);
            sendPeakR.store (peakR);
            sendRms.store ((float) std::sqrt (sumSq / (double) juce::jmax (1, numSamples * 2)));
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

        if (loopConfigured)
        {
            juce::AudioBuffer<float> hardwareMonitor (2, numSamples);
            readDelayedReturn (hardwareMonitor, numSamples);
            applyHardwareMonitorCrossfade (monitorBuffer, hardwareMonitor, numSamples);

            const int sendL = hardwareSettings.sendChannelL;
            const int sendR = hardwareSettings.sendChannelR;

            if (sendL >= 0 && sendL < numOutputChannels && outputChannelData[sendL] != nullptr)
                juce::FloatVectorOperations::copy (outputChannelData[sendL],
                                                   sendBuffer.getReadPointer (0), numSamples);
            if (sendR >= 0 && sendR < numOutputChannels && outputChannelData[sendR] != nullptr)
                juce::FloatVectorOperations::copy (outputChannelData[sendR],
                                                   sendBuffer.getReadPointer (1), numSamples);

            writeMonitorSamples (monitorBuffer, numSamples, outputChannelData, numOutputChannels);
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

bool PluginAudioEngine::setMidiOutputDevice (const juce::String& identifier, juce::String& error)
{
    midiOutput.reset();
    midiOutputIdentifier.clear();

    if (identifier.isEmpty())
        return true;

    midiOutput = juce::MidiOutput::openDevice (identifier);
    if (midiOutput == nullptr)
    {
        error = "Failed to open MIDI output device";
        return false;
    }

    midiOutputIdentifier = identifier;
    return true;
}

bool PluginAudioEngine::sendMidiMessage (const juce::MidiMessage& message)
{
    if (midiOutput == nullptr)
        return false;

    midiOutput->sendMessageNow (message);
    return true;
}

bool PluginAudioEngine::sendMidiMessages (const juce::Array<juce::MidiMessage>& messages)
{
    if (midiOutput == nullptr)
        return false;

    for (const auto& message : messages)
        midiOutput->sendMessageNow (message);

    return true;
}

/**
 * Blocking wait (message thread) for a sysex dump that satisfies the caller's
 * predicate — used for "dump current program" round-trips with external
 * hardware. Pumps the dispatch loop while waiting so MIDI callbacks and the
 * UI stay alive; same pattern as autoDetectLatency / captureHardwareToFile.
 */
bool PluginAudioEngine::waitForSysexDump (std::function<bool (const juce::MidiMessage&)> isAcceptable,
                                          juce::MidiMessage& outMessage,
                                          int timeoutMs,
                                          juce::String& error)
{
    {
        const juce::ScopedLock lock (sysexLock);
        pendingSysex.clear();
    }

    collectSysex.store (true);
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + (double) timeoutMs;

    while (juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        {
            const juce::ScopedLock lock (sysexLock);
            for (int i = 0; i < pendingSysex.size(); ++i)
            {
                if (isAcceptable (pendingSysex.getReference (i)))
                {
                    outMessage = pendingSysex.getReference (i);
                    pendingSysex.clear();
                    collectSysex.store (false);
                    return true;
                }
            }
        }

        juce::Thread::sleep (10);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    }

    collectSysex.store (false);
    error = "Timed out waiting for sysex dump";
    return false;
}

bool PluginAudioEngine::autoDetectLatency (const juce::File& impulseFile,
                                           int& outLatencySamples,
                                           float& outLoopGainDb,
                                           juce::String& error)
{
    if (! hardwareSettings.isConfigured())
    {
        error = "Configure a hardware audio device first";
        return false;
    }

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (impulseFile));
    if (reader == nullptr)
    {
        error = "Failed to read impulse file: " + impulseFile.getFullPathName();
        return false;
    }

    const int impulseSamples = (int) reader->lengthInSamples;
    loopPlayBuffer.setSize (2, impulseSamples, false, true, false);
    reader->read (&loopPlayBuffer, 0, impulseSamples, 0, true, true);

    const int recordSamples = impulseSamples + (int) (deviceSampleRate * 2.0) + deviceBlockSize * 4;
    loopRecordBuffer.setSize (2, recordSamples, false, true, false);
    loopRecordBuffer.clear();
    loopPlayPosition = 0;
    loopRecordPosition = 0;
    loopRecordCapacity = recordSamples;
    loopOpFinished.store (false);
    loopOp.store (LoopOp::calibrate);

    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 8000.0;
    while (! loopOpFinished.load() && juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        juce::Thread::sleep (10);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    }

    loopOp.store (LoopOp::idle);

    if (! loopOpFinished.load())
    {
        error = "Latency detection timed out";
        return false;
    }

    const int recorded = juce::jmin (loopRecordPosition, loopRecordCapacity);
    if (recorded < impulseSamples)
    {
        error = "Not enough return audio recorded";
        return false;
    }

    // Cross-correlate mono impulse against the mono-summed return, brute-force
    // time domain. O(impulse × lag) is fine here: the impulse is ~2 s and this
    // runs once per calibration click, not per block.
    // TODO: switch to FFT-based correlation if calibration ever feels slow.
    juce::AudioBuffer<float> impulseMono (1, impulseSamples);
    for (int i = 0; i < impulseSamples; ++i)
    {
        const float l = loopPlayBuffer.getSample (0, i);
        const float r = loopPlayBuffer.getNumChannels() > 1 ? loopPlayBuffer.getSample (1, i) : l;
        impulseMono.setSample (0, i, 0.5f * (l + r));
    }

    double bestCorr = -1.0e300;
    int bestLag = 0;
    const int maxLag = recorded - impulseSamples;

    for (int lag = 0; lag <= maxLag; ++lag)
    {
        double corr = 0.0;
        for (int i = 0; i < impulseSamples; ++i)
        {
            const float ret = 0.5f * (loopRecordBuffer.getSample (0, lag + i)
                                      + loopRecordBuffer.getSample (1, lag + i));
            corr += (double) impulseMono.getSample (0, i) * (double) ret;
        }

        if (corr > bestCorr)
        {
            bestCorr = corr;
            bestLag = lag;
        }
    }

    // Peak amplitude ratio near the detected lag.
    float impulsePeak = 0.0f, returnPeakLocal = 0.0f;
    for (int i = 0; i < impulseSamples; ++i)
    {
        impulsePeak = juce::jmax (impulsePeak, std::abs (impulseMono.getSample (0, i)));
        if (bestLag + i < recorded)
        {
            const float ret = 0.5f * (loopRecordBuffer.getSample (0, bestLag + i)
                                      + loopRecordBuffer.getSample (1, bestLag + i));
            returnPeakLocal = juce::jmax (returnPeakLocal, std::abs (ret));
        }
    }

    outLatencySamples = bestLag;
    outLoopGainDb = (impulsePeak > 1.0e-8f)
                        ? juce::Decibels::gainToDecibels (returnPeakLocal / impulsePeak)
                        : -120.0f;

    hardwareSettings.latencySamples = bestLag;
    ensureLatencyBufferSize (2, juce::jmax (bestLag + deviceBlockSize * 4, deviceBlockSize * 8));
    return true;
}

bool PluginAudioEngine::captureHardwareToFile (const juce::File& fixtureFile,
                                               const juce::File& outputFile,
                                               double tailSilenceSeconds,
                                               double silenceThresholdDb,
                                               double maxTailSeconds,
                                               juce::String& error,
                                               const std::atomic<bool>* cancelRequested,
                                               double targetDurationSeconds)
{
    if (! hardwareSettings.isConfigured())
    {
        error = "Configure a hardware audio device first";
        return false;
    }

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (fixtureFile));
    if (reader == nullptr)
    {
        error = "Failed to read fixture: " + fixtureFile.getFullPathName();
        return false;
    }

    const int sourceSamples = (int) reader->lengthInSamples;
    if (sourceSamples <= 0)
    {
        error = "Fixture has no audio samples: " + fixtureFile.getFullPathName();
        return false;
    }

    juce::AudioBuffer<float> sourceBuffer (juce::jmax (1, (int) reader->numChannels), sourceSamples);
    reader->read (sourceBuffer.getArrayOfWritePointers(),
                  sourceBuffer.getNumChannels(),
                  0,
                  sourceSamples);

    // Resample the fixture to the *device* rate with linear interpolation.
    // The loop-op playback path streams loopPlayBuffer 1:1 at device rate, so
    // skipping this played 44.1k fixtures at 48k speed (an early bug heard as
    // "very slow playback"). Linear interpolation is adequate: the hardware
    // D/A -> analog FX -> A/D round trip dominates any interpolation error.
    const double sourceRate = reader->sampleRate > 1.0 ? reader->sampleRate : deviceSampleRate;
    const int fixtureSamples = juce::jmax (1, (int) std::llround ((double) sourceSamples * deviceSampleRate / sourceRate));
    loopPlayBuffer.setSize (2, fixtureSamples, false, true, false);

    for (int i = 0; i < fixtureSamples; ++i)
    {
        const double srcPos = (double) i * sourceRate / deviceSampleRate;
        const int src0 = juce::jlimit (0, sourceSamples - 1, (int) std::floor (srcPos));
        const int src1 = juce::jmin (sourceSamples - 1, src0 + 1);
        const float frac = (float) juce::jlimit (0.0, 1.0, srcPos - (double) src0);

        for (int ch = 0; ch < 2; ++ch)
        {
            const int srcCh = juce::jmin (ch, sourceBuffer.getNumChannels() - 1);
            const float a = sourceBuffer.getSample (srcCh, src0);
            const float b = sourceBuffer.getSample (srcCh, src1);
            loopPlayBuffer.setSample (ch, i, a + (b - a) * frac);
        }
    }

    const float send = sendGain.load();
    if (send != 1.0f)
        loopPlayBuffer.applyGain (send);

    const int maxRecord = fixtureSamples
                          + (int) (deviceSampleRate * maxTailSeconds)
                          + hardwareSettings.latencySamples
                          + deviceBlockSize * 4;
    loopRecordBuffer.setSize (2, maxRecord, false, true, false);
    loopRecordBuffer.clear();
    loopPlayPosition = 0;
    loopRecordPosition = 0;
    loopRecordCapacity = maxRecord;
    loopOpFinished.store (false);
    loopOp.store (LoopOp::capture);

    const float silenceThresh = juce::Decibels::decibelsToGain ((float) silenceThresholdDb);
    const int silenceHold = juce::jmax (1, (int) (deviceSampleRate * tailSilenceSeconds));
    // When the caller knows how long the take should be (the "Capture Both"
    // flow passes the software render's length), stop on duration instead of
    // relying on silence detection — analog return noise floors can sit above
    // the silence threshold forever. A quarter-second of slack keeps the
    // hardware file at least as long as the software one.
    const int targetUsableSamples = targetDurationSeconds > 0.0
                                        ? (int) std::llround (targetDurationSeconds * deviceSampleRate)
                                        : 0;
    const int targetSlackSamples = targetUsableSamples > 0
                                        ? juce::jmax (deviceBlockSize * 2, (int) (deviceSampleRate * 0.25))
                                        : 0;
    int silentSamples = 0;
    bool pastFixture = false;
    int lastRecorded = -1;
    int stagnantIterations = 0;
    bool stopRequested = false;

    const auto deadline = juce::Time::getMillisecondCounterHiRes()
                          + (maxTailSeconds + (double) fixtureSamples / deviceSampleRate + 5.0) * 1000.0;

    while (juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        // Cancel means "stop recording, keep what we have" — the user hit
        // Cancel to end the take, not to throw it away. We only fail below if
        // nothing usable was recorded yet.
        if (cancelRequested != nullptr && cancelRequested->load())
        {
            loopOp.store (LoopOp::idle);
            stopRequested = true;
            break;
        }

        juce::Thread::sleep (10);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

        const int recorded = loopRecordPosition;
        if (recorded == lastRecorded)
            ++stagnantIterations;
        else
            stagnantIterations = 0;

        lastRecorded = recorded;

        // Stall detection: if the audio callback stops advancing the record
        // position (device unplugged, CoreAudio wedged after a device
        // restart), bail with a diagnosable error instead of spinning until
        // the deadline. The threshold is generous (~30 s) because a slow
        // device restart can legitimately pause the callback for a while.
        if (stagnantIterations > 1500) // about 30s with sleep+dispatch cadence
        {
            loopOp.store (LoopOp::idle);
            error = "Hardware capture stalled (audio device not advancing)";
            return false;
        }

        if (loopPlayPosition >= fixtureSamples)
            pastFixture = true;

        const int usableNow = juce::jmax (0, recorded - hardwareSettings.latencySamples);
        if (pastFixture && targetUsableSamples > 0
            && usableNow >= targetUsableSamples + targetSlackSamples)
        {
            loopOp.store (LoopOp::idle);
            loopOpFinished.store (true);
            break;
        }

        if (pastFixture && recorded > hardwareSettings.latencySamples + fixtureSamples)
        {
            const int checkFrom = juce::jmax (0, recorded - deviceBlockSize);
            float peak = 0.0f;
            for (int i = checkFrom; i < recorded; ++i)
                peak = juce::jmax (peak,
                                   std::abs (loopRecordBuffer.getSample (0, i)),
                                   std::abs (loopRecordBuffer.getSample (1, i)));

            if (peak < silenceThresh)
                silentSamples += deviceBlockSize;
            else
                silentSamples = 0;

            if (silentSamples >= silenceHold || recorded >= maxRecord)
            {
                loopOp.store (LoopOp::idle);
                loopOpFinished.store (true);
                break;
            }
        }
    }

    loopOp.store (LoopOp::idle);

    // Trim the measured loop latency off the head so the file starts where
    // the fixture actually started — this keeps hardware captures sample-
    // aligned with offline software renders for A/B comparison.
    const int recorded = juce::jmin (loopRecordPosition, loopRecordCapacity);
    const int latency = juce::jlimit (0, recorded, hardwareSettings.latencySamples);
    const int usable = recorded - latency;
    if (stopRequested && usable <= 0)
    {
        error = "Capture cancelled before usable audio was recorded";
        return false;
    }

    if (usable <= 0)
    {
        error = "Hardware capture produced no usable audio (check latency / routing)";
        return false;
    }

    juce::AudioBuffer<float> trimmed (2, usable);
    for (int ch = 0; ch < 2; ++ch)
        trimmed.copyFrom (ch, 0, loopRecordBuffer, ch, latency, usable);

    outputFile.deleteFile();
    outputFile.getParentDirectory().createDirectory();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream (outputFile.createOutputStream());
    if (stream == nullptr)
    {
        error = "Failed to create output file: " + outputFile.getFullPathName();
        return false;
    }

    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.get(), deviceSampleRate, 2, 24, {}, 0));
    if (writer == nullptr)
    {
        error = "Failed to create WAV writer";
        return false;
    }

    stream.release();
    if (! writer->writeFromAudioSampleBuffer (trimmed, 0, usable))
    {
        error = "Failed to write hardware capture WAV";
        return false;
    }

    return true;
}
