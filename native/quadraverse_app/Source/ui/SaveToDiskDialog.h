#pragma once

#include <JuceHeader.h>
#include "../domain/QuadraverbProgram.h"

namespace qverse
{

struct SaveToDiskResult
{
    bool ok = false;
    bool wroteQdv1 = false;
    bool wroteSyx = false;
    bool wroteAupreset = false;
    juce::String message;
};

SaveToDiskResult runSaveToDiskDialog (const QuadraverbProgram& program,
                                      const juce::File& patchDir,
                                      juce::Component* parent);

} // namespace qverse
