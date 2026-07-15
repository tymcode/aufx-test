#pragma once

#include <JuceHeader.h>

/** Append-only file logger for the plugin host. */
class HostLog
{
public:
    static void open (const juce::File& logFile);
    static void close();

    static void info (const juce::String& message);
    static void error (const juce::String& message);

    static juce::File getLogFile();

private:
    static void write (const juce::String& level, const juce::String& message);
};
