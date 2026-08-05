#pragma once

#include <JuceHeader.h>
#include "PluginAudioEngine.h"

/**
 * Remote Transport Setup: loads the SonoBus transport plugin, embeds its
 * editor for connect/group configuration, and measures the network loop
 * latency. Returns true if the user saved (transport enabled + persisted).
 */
bool showRemoteSetupDialog (PluginAudioEngine& engine,
                            const juce::File& fixturesDir,
                            juce::Component* centreAround);
