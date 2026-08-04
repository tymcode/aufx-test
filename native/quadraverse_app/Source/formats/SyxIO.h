#pragma once

#include "../domain/QuadraverbProgram.h"
#include <vector>

namespace qverse
{

struct SyxIO
{
    static bool loadFile (const juce::File& file,
                          std::vector<QuadraverbProgram>& outPrograms,
                          juce::String& error);

    static bool loadFromMemory (const void* data,
                                size_t size,
                                std::vector<QuadraverbProgram>& outPrograms,
                                juce::String& error);

    static bool saveSingle (const juce::File& file,
                            const QuadraverbProgram& program,
                            uint8_t programSlot, // 0x64 edit, or 0-99
                            juce::String& error);

    /** Concatenate one Load Program message per program (names already in bytes). */
    static bool savePrograms (const juce::File& file,
                              const std::vector<QuadraverbProgram>& programs,
                              juce::String& error);

    static bool saveBank (const juce::File& file,
                          const std::vector<QuadraverbProgram>& programs,
                          juce::String& error);
};

} // namespace qverse
