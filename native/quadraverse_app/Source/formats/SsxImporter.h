#pragma once

#include "../domain/QuadraverbProgram.h"

namespace qverse
{

struct SsxImporter
{
    /**
     * Validate .ssx (raw Quadraverb bank sysex), decode, and write a .syx
     * (optionally as individual program messages) into destSyx.
     */
    static bool importToSyx (const juce::File& ssxFile,
                             const juce::File& destSyx,
                             bool emitIndividualPrograms,
                             juce::String& error,
                             int* outProgramCount = nullptr);
};

} // namespace qverse
