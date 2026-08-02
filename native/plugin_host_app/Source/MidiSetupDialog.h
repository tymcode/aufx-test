#pragma once

#include <JuceHeader.h>
#include "HostConfig.h"
#include "PluginAudioEngine.h"

bool showMidiSetupDialog (PluginAudioEngine& engine,
                          HostConfig& config,
                          juce::Component* centreAround);
