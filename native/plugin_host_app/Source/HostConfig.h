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

    juce::String displayLabel() const
    {
        if (manufacturer.isNotEmpty() && name.isNotEmpty())
            return manufacturer + " — " + name;
        if (name.isNotEmpty())
            return name;
        return path.getFileNameWithoutExtension();
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
