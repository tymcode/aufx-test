#pragma once

#include <JuceHeader.h>
#include "PluginAudioEngine.h"
#include "SessionSnap.h"
#include "SourceClipLibrary.h"

class PresetHardwareState;

/**
 * Apply a captured snapshot's artifacts into the live host.
 *
 * Load Testcase UI will call apply() after the user picks a snapshot.
 * Mechanical pieces (source-clip selection, preset load, sysex send) are
 * factored here so Capture / Compare / Load share the same restore path.
 */
class TestCaseLoader
{
public:
    TestCaseLoader (PluginAudioEngine& audioEngine,
                    SourceClipLibrary& sourceClips,
                    PresetHardwareState& presetHardware);

    /**
     * Load input audio as the current source clip (via SourceClipLibrary
     * external/"Loaded" entry), restore the .aupreset into the plugin, and
     * optionally send .syx to hardware. Missing optional artifacts are skipped.
     */
    bool apply (const SessionSnapshotRef& snapshot,
                juce::ComboBox& sourceClipBox,
                juce::String& error,
                juce::String* statusMessage = nullptr);

    bool loadSourceClip (const juce::File& inputAudio,
                         juce::ComboBox& sourceClipBox,
                         juce::String& error);

    bool loadPreset (const juce::File& presetFile, juce::String& error);

    bool sendSysex (const juce::File& sysexFile, juce::String& error);

private:
    PluginAudioEngine& engine;
    SourceClipLibrary& sourceClips;
    PresetHardwareState& presetHardware;
};
