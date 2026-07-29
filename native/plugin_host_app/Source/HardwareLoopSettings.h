#pragma once

#include <JuceHeader.h>

/** CoreAudio hardware-insert loop: send → external FX → return, plus monitor outs. */
struct HardwareLoopSettings
{
    /** Stored in prefs when monitorOutputDeviceName is the live system default. */
    static constexpr const char* systemDefaultMonitorOutputName = "<System Default Output>";

    juce::String deviceName;
    int sendChannelL { 2 };
    int sendChannelR { 3 };
    int returnChannelL { 0 };
    int returnChannelR { 1 };
    int monitorChannelL { 0 };
    int monitorChannelR { 1 };
    /**
     * When empty, playthrough is written to monitorChannelL/R on the loop device.
     * When set (including systemDefaultMonitorOutputName), playthrough opens a
     * separate CoreAudio output — e.g. a Multi-Output Device for screen capture.
     */
    juce::String monitorOutputDeviceName;
    int bufferSize { 512 };
    int latencySamples { 0 };

    bool isConfigured() const { return deviceName.isNotEmpty(); }
    bool usesSeparateMonitorOutput() const { return monitorOutputDeviceName.isNotEmpty(); }

    static juce::String channelPairLabel (int left, int right)
    {
        return juce::String (left + 1) + "-" + juce::String (right + 1);
    }
};
