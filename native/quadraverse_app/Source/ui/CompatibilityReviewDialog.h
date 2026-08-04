#pragma once

#include <JuceHeader.h>
#include "../domain/PatchTranslator.h"

namespace qverse
{

/** Modal review when cross-device translation is not clean. Returns true if accepted. */
bool runCompatibilityReview (TranslationReport& report, juce::Component* parent);

bool translateIfNeeded (const QuadraverbProgram& source,
                        DeviceModel target,
                        QuadraverbProgram& out,
                        juce::Component* parent);

} // namespace qverse
