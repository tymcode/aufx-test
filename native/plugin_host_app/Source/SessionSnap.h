#pragma once

#include <JuceHeader.h>

/** Native port of `aufx-test session snap` — updates session.json without Python. */
struct SessionSnapRequest
{
    juce::File sessionsRoot;
    juce::String sessionName;
    juce::String snapshotName;
    juce::File inputFile;
    juce::File outputFile;
    juce::File hardwareOutputFile; // optional latency-corrected hardware capture
    juce::File presetFile;
    juce::File sysexFile; // optional hardware state dump
    juce::String pluginPath;
    juce::String notes { "Captured from AU Effects Explorer" };
};

struct SessionSnap
{
    static bool registerSnapshot (const SessionSnapRequest& request, juce::String& error);
};
