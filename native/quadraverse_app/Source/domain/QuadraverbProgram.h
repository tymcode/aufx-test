#pragma once

#include "DeviceProfile.h"
#include <array>
#include <map>
#include <string>

namespace qverse
{

/** Per-parameter UI/randomizer metadata (Randomizer phase prepared here). */
struct ParamRuntimeMeta
{
    bool protectFromAdjustment = false;
    bool protectFromRandomization = false;
    int randomMin = 0;
    int randomMax = 99;
    bool hasCustomRandomRange = false;
};

/**
 * Editable Quadraverb program.
 * Canonical edit model is (function, page) -> value, matching QDV-1 and
 * Change Parameter sysex. Raw 128-byte dump kept for Load Program / .syx I/O.
 */
struct QuadraverbProgram
{
    static constexpr int kMaxFunctions = 10;
    static constexpr int kMaxPages = 32;
    static constexpr int kProgramBytes = 128;
    static constexpr int kNameLength = 14;

    DeviceModel model = DeviceModel::quadraverbPlus;
    int programNumber = 0;
    int config = 4;
    int inLevel = 50;
    int outLevel = 84;
    juce::String name;

    std::array<uint8_t, kProgramBytes> bytes {};
    bool hasValidBytes = false;

    int value[kMaxFunctions][kMaxPages] {};
    bool known[kMaxFunctions][kMaxPages] {};

    std::map<std::string, ParamRuntimeMeta> paramMeta;

    void clearValues();
    void setParam (int function, int page, int v);
    int getParam (int function, int page, int fallback = 0) const;
    bool isKnown (int function, int page) const;

    void setName (const juce::String& n);
    void syncNameFromBytes();
    void syncNameToBytes();
    void syncConfigFromBytes();
    void syncConfigToBytes();

    /** Pull known layout fields from bytes into value[] (QV1/Plus). */
    void hydrateValuesFromBytes();
    /** Write known value[] fields back into bytes. */
    void flushValuesToBytes();

    ParamRuntimeMeta& metaFor (const ParamAddress& addr);
    const ParamRuntimeMeta* findMeta (const ParamAddress& addr) const;

    juce::var toVar() const;
    static QuadraverbProgram fromVar (const juce::var& v);
};

} // namespace qverse
