#include "MonitorOutputBridge.h"

namespace
{
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
}

void MonitorOutputBridge::MonitorOutputHandler::audioDeviceIOCallbackWithContext (
    const float* const* inputChannelData,
    int numInputChannels,
    float* const* outputChannelData,
    int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext& context)
{
    juce::ignoreUnused (inputChannelData, numInputChannels, context);
    owner.pullMonitorOutput (outputChannelData, numOutputChannels, numSamples);
}

MonitorOutputBridge::MonitorOutputBridge()
{
    monitorRingBuffer.setSize (2, monitorRingCapacity, false, true, true);
    monitorOutputHandler = std::make_unique<MonitorOutputHandler> (*this);
}

MonitorOutputBridge::~MonitorOutputBridge()
{
    stopMonitorOutput();
}

void MonitorOutputBridge::pushMonitorOutput (const float* left, const float* right, int numSamples)
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

void MonitorOutputBridge::pullMonitorOutput (float* const* outputChannelData,
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

bool MonitorOutputBridge::startMonitorOutput (const HardwareLoopSettings& settings,
                                              double deviceSampleRate,
                                              int deviceBlockSize,
                                              juce::String& error)
{
    stopMonitorOutput();

    if (! settings.usesSeparateMonitorOutput())
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
                                                            settings.monitorOutputDeviceName);
    if (deviceName.isEmpty())
    {
        error = "Monitor output: no output device available";
        return false;
    }

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.outputDeviceName = deviceName;
    setup.inputDeviceName.clear();
    setup.sampleRate = deviceSampleRate > 0.0 ? deviceSampleRate : 0.0;
    setup.bufferSize = deviceBlockSize > 0 ? deviceBlockSize : settings.bufferSize;
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

void MonitorOutputBridge::stopMonitorOutput()
{
    monitorOutputActive.store (false);

    if (monitorOutputHandler != nullptr)
        monitorDeviceManager.removeAudioCallback (monitorOutputHandler.get());

    monitorDeviceManager.closeAudioDevice();
    monitorFifo.reset();
    monitorRingBuffer.clear();
}
