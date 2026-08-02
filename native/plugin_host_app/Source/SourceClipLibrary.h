#pragma once

#include <JuceHeader.h>

/**
 * Source Clip model for the host UI.
 *
 * Library clips come from fixtures/ (top-level + per-subfolder menus).
 * External clips are arbitrary audio files loaded outside that tree — used by
 * Load Testcase to replay a snapshot's staged input_audio on any machine.
 * Browse picks from "Select Other…" are temporary top-level entries.
 *
 * ComboBox item IDs are 1-based indices into clips[].
 * selectOtherItemId is reserved for the browse action (not a clip index).
 */
struct SourceClipEntry
{
    juce::File file;
    juce::String menuLabel;
    juce::String groupName; // empty = top-level; folder name; "Loaded" for external
    bool fromLibrary { true };
};

class SourceClipLibrary
{
public:
    static constexpr const char* externalGroupName = "Loaded";
    static constexpr int selectOtherItemId = 0x0ffe0002;

    /** Rescan fixturesDir; preserves previously added external / temporary clips. */
    void rescan (const juce::File& fixturesDir);

    /** Rebuild combo contents from clips[]. Optionally keep a preferred selection. */
    void rebuildComboBox (juce::ComboBox& box, int preferredSelectId = 0) const;

    const juce::Array<SourceClipEntry>& getClips() const { return clips; }
    int getNumClips() const { return clips.size(); }

    juce::File getFileForId (int comboId) const;
    juce::File getSelectedFile (const juce::ComboBox& box) const;
    int indexOfFile (const juce::File& file) const;

    /**
     * Ensure file is in the list (as an external/"Loaded" entry if needed)
     * and select it in the combo. Returns the combo item id, or 0 on failure.
     */
    int selectOrAddExternal (juce::ComboBox& box, const juce::File& file);

    /**
     * Ensure file is in the list as a temporary top-level entry and select it.
     * Returns the combo item id, or 0 on failure.
     */
    int selectOrAddTemporaryTopLevel (juce::ComboBox& box, const juce::File& file);

    /** True if the path looks like audio the engine can load (by extension). */
    static bool isSupportedAudioFile (const juce::File& file);

    /** True if the path is an existing .wav file. */
    static bool isWavFile (const juce::File& file);

private:
    void addLibraryFile (const juce::File& file, const juce::String& groupName, const juce::String& menuLabel);
    int addExternalEntry (const juce::File& file);
    int addTemporaryTopLevelEntry (const juce::File& file);

    juce::Array<SourceClipEntry> clips;
    juce::File libraryRoot;
};
