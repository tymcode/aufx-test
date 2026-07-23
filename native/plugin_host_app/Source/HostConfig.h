#pragma once

#include <JuceHeader.h>

struct HostPluginEntry
{
    juce::String id;
    juce::String name;
    juce::String manufacturer;
    juce::File path;
    juce::File presetsDir;
    juce::File defaultPreset; // optional .aupreset loaded on plugin open
    juce::String sessionName;
    bool installed { true }; // false when path is missing on disk

    juce::String displayLabel() const
    {
        juce::String label;
        if (manufacturer.isNotEmpty() && name.isNotEmpty())
            label = manufacturer + " — " + name;
        else if (name.isNotEmpty())
            label = name;
        else
            label = path.getFileNameWithoutExtension();

        if (! installed)
            label += " (not installed)";
        return label;
    }
};

struct HostConfig
{
    juce::File projectRoot;
    juce::File fixturesDir;
    juce::File sessionsRoot;
    juce::File pythonCli;
    juce::File logFile;          // config path template (before session hash)
    juce::File sessionLogFile;   // logFile with session hash inserted in the stem
    juce::String sessionHash;
    juce::String defaultPluginId;
    /** Preferred MIDI input device names (Audio MIDI Setup). Empty = none preselected. */
    juce::StringArray defaultMidiInputs;
    juce::Array<HostPluginEntry> plugins;

    const HostPluginEntry* findPluginById (const juce::String& id) const;
    const HostPluginEntry* defaultPlugin() const;

    /** Build sessionLogFile from logFile + sessionHash (e.g. plugin_host.log → plugin_host_a1b2c3d4.log). */
    void resolveSessionLogFile();

    static bool loadFromFile (const juce::File& configFile,
                              const juce::File& projectRoot,
                              HostConfig& out,
                              juce::String& error);
};

struct HostCommandLineOptions
{
    juce::File configFile;
    juce::File projectRoot;

    bool parse (const juce::StringArray& args, juce::String& error);
};
