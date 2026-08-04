#pragma once

#include <JuceHeader.h>
#include "../domain/PatchContextManager.h"
#include "PluginAudioEngine.h"
#include "HostConfig.h"

namespace qverse
{

/** Capture selected patch contexts to software and/or hardware, then run compare + spectrograms. */
void runComparisonReportDialog (PatchContextManager& contexts,
                                PluginAudioEngine& engine,
                                HostConfig& config,
                                juce::Component* parent);

} // namespace qverse
