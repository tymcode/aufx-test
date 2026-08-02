#pragma once

#include <JuceHeader.h>
#include "HostConfig.h"

class PluginAudioEngine;

/**
 * Two-column Install New Source Clips dialog.
 * Left: browse/select WAV files (with loop preview). Right: target folder under fixtures.
 * Returns true when files were installed (and fixtures may have been relocated).
 */
bool showInstallSourceClipsDialog (HostConfig& config,
                                   PluginAudioEngine& engine,
                                   juce::Component* centreAround,
                                   bool* outFixturesRelocated = nullptr);
