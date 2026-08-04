#pragma once

#include "../domain/QuadraverbProgram.h"
#include "../domain/DeviceProfile.h"
#include <vector>

namespace qverse
{

enum class TranslationClass
{
    mapsCleanly,
    approximated,
    noEquivalent
};

struct TranslationItem
{
    ParamAddress sourceAddress;
    ParamAddress targetAddress;
    juce::String name;
    juce::String section;
    int sourceValue = 0;
    int proposedValue = 0;
    int targetDefault = 0;
    TranslationClass classification = TranslationClass::mapsCleanly;
    bool accepted = true;
};

struct TranslationReport
{
    DeviceModel sourceModel {};
    DeviceModel targetModel {};
    std::vector<TranslationItem> items;
    bool isClean() const;
};

struct PatchTranslator
{
    static TranslationReport translate (const QuadraverbProgram& source,
                                        DeviceModel targetModel,
                                        QuadraverbProgram& outProgram);
};

} // namespace qverse
