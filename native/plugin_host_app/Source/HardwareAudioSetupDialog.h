#pragma once

#include <JuceHeader.h>
#include "HardwareLoopSettings.h"
#include "PluginAudioEngine.h"

/** Returns true if the user saved new settings (applied to engine + prefs). */
bool showHardwareAudioSetupDialog (PluginAudioEngine& engine,
                                   const juce::File& fixturesDir,
                                   juce::Component* centreAround);
