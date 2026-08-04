#pragma once

#include <JuceHeader.h>
#include <cstdint>
#include <vector>

namespace qverse
{

/** Alesis continuous MSB-first 8↔7-bit packing (QV1/Plus program dumps). */
struct AlesisCodec
{
    static std::vector<uint8_t> decode (const uint8_t* encoded, int encodedLen);
    static std::vector<uint8_t> encode (const uint8_t* raw, int rawLen);

    static juce::MidiMessage buildChangeParameter (uint8_t productId,
                                                   uint8_t function,
                                                   uint8_t page,
                                                   uint16_t value);

    static juce::MidiMessage buildLoadProgram (uint8_t productId,
                                              uint8_t programOrEdit,
                                              const uint8_t* raw128);

    static constexpr uint8_t kProductQuadraverb = 0x02;
    static constexpr uint8_t kEditBuffer = 0x64;
    static constexpr uint8_t kAllPrograms = 0x65;
    static constexpr int kEncodedProgramSize = 147;
    static constexpr int kSingleProgramSysexSize = 155; // F0..F7 inclusive
};

} // namespace qverse
