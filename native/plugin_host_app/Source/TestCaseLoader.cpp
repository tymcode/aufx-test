#include "TestCaseLoader.h"
#include "PresetHardwareState.h"

TestCaseLoader::TestCaseLoader (PluginAudioEngine& audioEngine,
                                SourceClipLibrary& sourceClipsIn,
                                PresetHardwareState& presetHardwareIn)
    : engine (audioEngine),
      sourceClips (sourceClipsIn),
      presetHardware (presetHardwareIn)
{
}

bool TestCaseLoader::loadSourceClip (const juce::File& inputAudio,
                                     juce::ComboBox& sourceClipBox,
                                     juce::String& error)
{
    if (! SourceClipLibrary::isSupportedAudioFile (inputAudio))
    {
        error = "Unsupported or missing input audio: " + inputAudio.getFullPathName();
        return false;
    }

    const int id = sourceClips.selectOrAddExternal (sourceClipBox, inputAudio);
    if (id <= 0)
    {
        error = "Failed to add source clip: " + inputAudio.getFileName();
        return false;
    }

    if (! engine.loadFixture (inputAudio, error))
    {
        error = "Failed to load source clip: " + error;
        return false;
    }

    return true;
}

bool TestCaseLoader::loadSourceClipTemporaryTopLevel (const juce::File& inputAudio,
                                                      juce::ComboBox& sourceClipBox,
                                                      juce::String& error)
{
    if (! SourceClipLibrary::isSupportedAudioFile (inputAudio))
    {
        error = "Unsupported or missing input audio: " + inputAudio.getFullPathName();
        return false;
    }

    // Temporary top-level entries are WAV-only in the library; fall back to Loaded.
    const int id = SourceClipLibrary::isWavFile (inputAudio)
                       ? sourceClips.selectOrAddTemporaryTopLevel (sourceClipBox, inputAudio)
                       : sourceClips.selectOrAddExternal (sourceClipBox, inputAudio);
    if (id <= 0)
    {
        error = "Failed to add source clip: " + inputAudio.getFileName();
        return false;
    }

    if (! engine.loadFixture (inputAudio, error))
    {
        error = "Failed to load source clip: " + error;
        return false;
    }

    return true;
}

bool TestCaseLoader::loadPreset (const juce::File& presetFile, juce::String& error)
{
    return presetHardware.loadPresetFile (presetFile, error);
}

bool TestCaseLoader::sendSysex (const juce::File& sysexFile, juce::String& error)
{
    return presetHardware.sendHardwareStateFile (sysexFile, error);
}

bool TestCaseLoader::apply (const SessionSnapshotRef& snapshot,
                            juce::ComboBox& sourceClipBox,
                            juce::String& error,
                            juce::String* statusMessage)
{
    juce::StringArray loaded;

    if (snapshot.inputAudio.existsAsFile())
    {
        if (! loadSourceClip (snapshot.inputAudio, sourceClipBox, error))
            return false;
        loaded.add ("clip");
    }
    else if (snapshot.inputAudio != juce::File())
    {
        error = "Snapshot input audio missing: " + snapshot.inputAudio.getFullPathName();
        return false;
    }

    if (snapshot.presetFile.existsAsFile())
    {
        if (! loadPreset (snapshot.presetFile, error))
            return false;
        loaded.add ("preset");
    }

    if (snapshot.sysexFile.existsAsFile())
    {
        if (! sendSysex (snapshot.sysexFile, error))
            return false;
        loaded.add ("sysex");
    }

    if (loaded.isEmpty())
    {
        error = "Snapshot has no loadable artifacts (input_audio / preset_file / sysex_file)";
        return false;
    }

    if (statusMessage != nullptr)
        *statusMessage = "Loaded test case \"" + snapshot.name + "\" (" + loaded.joinIntoString (", ") + ")";

    return true;
}
