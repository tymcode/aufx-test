#pragma once

#include "../domain/QuadraverbProgram.h"

namespace qverse
{

struct Qdv1StateIO
{
    /** Build QDV1 XML-in-binary state blob (no MIDI_MAP). */
    static bool toStateBlob (const QuadraverbProgram& program, juce::MemoryBlock& out, juce::String& error);

    static bool fromStateBlob (const juce::MemoryBlock& blob, QuadraverbProgram& out, juce::String& error);

    static bool loadAupreset (const juce::File& file, QuadraverbProgram& out, juce::String& error);
    static bool saveAupreset (const juce::File& file, const QuadraverbProgram& program, juce::String& error);

    /** Write user preset JSON into QDV-1's preset library. */
    static bool saveUserPreset (const juce::String& name,
                                const QuadraverbProgram& program,
                                juce::String& outId,
                                juce::String& error);

    static juce::File presetLibraryDir();
    static juce::Array<juce::File> listUserPresetFiles();
    static bool loadUserPresetFile (const juce::File& jsonFile, QuadraverbProgram& out, juce::String& error);
};

} // namespace qverse
