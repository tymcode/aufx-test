#pragma once

#include <JuceHeader.h>
#include <vector>

namespace qverse
{

struct SysexPatchPickResult
{
    bool ok = false;
    juce::Array<int> selectedIndices;
};

/**
 * Modal multi-select list of patch names from a sysex bank.
 * Used by Load Patch → From Sysex Dump and File → Import Sysex Bank.
 */
SysexPatchPickResult runSysexPatchPicker (const juce::StringArray& patchNames,
                                          juce::Component* parent,
                                          bool selectAllByDefault = true);

} // namespace qverse
