#pragma once

#include <JuceHeader.h>

class PluginAudioEngine;

/**
 * Read the hosted plugin's current state (QDV-1 XML-in-binary), map it to a
 * Quadraverb Plus edit-buffer Load Program sysex, and send it to the
 * configured MIDI output.
 */
struct SendPluginSettingsToDevice
{
    /** MIDI Out port name from MIDI Setup, or empty if none configured. */
    static juce::String resolveTargetDeviceName();

    /**
     * @return true on success. On failure, error is set; on success, error
     *         holds a short status message (success text).
     */
    static bool send (PluginAudioEngine& engine, juce::String& errorOrStatus);
};
