#pragma once

#include <JuceHeader.h>
#include "HardwareLoopSettings.h"

/**
 * Optional second CoreAudio output device for playthrough monitoring.
 *
 * Why a second AudioDeviceManager: the loop device (e.g. UA Apollo) is
 * opened exclusively for send/return, so writing playthrough to its
 * monitor pair bypasses macOS system audio entirely — screen recorders
 * capturing via a Multi-Output Device (interface + BlackHole) hear
 * nothing from this app. When HardwareLoopSettings names a separate
 * monitor output, playthrough is pushed into a lock-free FIFO on the loop
 * device's callback and pulled by this second device's callback.
 * The two devices free-run on independent clocks; drift is tolerated
 * because monitoring is non-critical (the FIFO under/overruns manifest as
 * an occasional dropout, never as corruption of the captured audio).
 * TODO: add a resampling bridge if long screen-recording sessions drift
 * audibly.
 */
class MonitorOutputBridge
{
public:
    MonitorOutputBridge();
    ~MonitorOutputBridge();

    MonitorOutputBridge (const MonitorOutputBridge&) = delete;
    MonitorOutputBridge& operator= (const MonitorOutputBridge&) = delete;

    void pushMonitorOutput (const float* left, const float* right, int numSamples);
    void pullMonitorOutput (float* const* outputChannelData, int numOutputChannels, int numSamples);

    bool startMonitorOutput (const HardwareLoopSettings& settings,
                             double deviceSampleRate,
                             int deviceBlockSize,
                             juce::String& error);
    void stopMonitorOutput();

    bool isActive() const { return monitorOutputActive.load(); }

private:
    /**
     * Callback for the separate monitor output device. It only drains the
     * monitor FIFO that the main loop-device callback fills. A nested struct
     * rather than making the engine itself the callback for both devices —
     * juce::AudioDeviceManager identifies callbacks by pointer, so the two
     * devices need distinct callback objects.
     */
    struct MonitorOutputHandler : juce::AudioIODeviceCallback
    {
        explicit MonitorOutputHandler (MonitorOutputBridge& ownerIn) : owner (ownerIn) {}

        void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                               int numInputChannels,
                                               float* const* outputChannelData,
                                               int numOutputChannels,
                                               int numSamples,
                                               const juce::AudioIODeviceCallbackContext& context) override;

        void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
        void audioDeviceStopped() override {}

        MonitorOutputBridge& owner;
    };

    std::unique_ptr<MonitorOutputHandler> monitorOutputHandler;
    juce::AudioDeviceManager monitorDeviceManager;
    juce::AudioBuffer<float> monitorRingBuffer;
    juce::AbstractFifo monitorFifo { 32768 };
    std::atomic<bool> monitorOutputActive { false };
};
