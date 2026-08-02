#pragma once

#include <JuceHeader.h>
#include "HostConfig.h"

/**
 * Audio Unit hosting preferences: instrument input, scan timeout, and
 * skipped-plugin retries. Formerly part of Settings.
 */
bool showAudioUnitSettingsDialog (HostConfig& config,
                                  juce::KnownPluginList* knownPlugins,
                                  juce::Component* centreAround);
