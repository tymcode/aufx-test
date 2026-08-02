#pragma once

#include <JuceHeader.h>
#include "HostConfig.h"
#include "PluginAudioEngine.h"
#include "SourceClipLibrary.h"
#include "TestCaseLoader.h"

/**
 * Folder picker + restore of a capture stem under session artifacts/.
 * Returns true when a folder was chosen and restore succeeded.
 */
bool showRestoreTestcaseStateDialog (HostConfig& config,
                                     PluginAudioEngine& engine,
                                     TestCaseLoader& loader,
                                     SourceClipLibrary& sourceClips,
                                     juce::ComboBox& sourceClipBox,
                                     const juce::File& startDirectory,
                                     juce::Component* centreAround);
