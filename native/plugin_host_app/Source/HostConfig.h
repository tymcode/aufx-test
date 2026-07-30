#pragma once

#include <JuceHeader.h>

struct HostPluginEntry
{
    juce::String id;
    juce::String name;
    juce::String manufacturer;
    /** Filesystem path when known (.component / .vst3). */
    juce::File path;
    /**
     * Identifier passed to the plugin format (may be a path or an AU registry id
     * such as "AudioUnit:Effects/..."). Preferred over path when non-empty.
     */
    juce::String fileOrIdentifier;
    juce::String pluginFormatName; // e.g. "AudioUnit", "VST3"
    juce::File presetsDir;
    juce::File defaultPreset; // optional .aupreset loaded on plugin open
    juce::String sessionName;
    bool installed { true }; // false when path/identifier is missing on this machine

    juce::String identifierForLoad() const
    {
        if (fileOrIdentifier.isNotEmpty())
            return fileOrIdentifier;
        return path.getFullPathName();
    }

    juce::PluginDescription toPluginDescription() const
    {
        juce::PluginDescription description;
        description.name = name;
        description.manufacturerName = manufacturer;
        description.fileOrIdentifier = identifierForLoad();
        description.pluginFormatName = pluginFormatName;

        if (description.pluginFormatName.isEmpty())
        {
            if (description.fileOrIdentifier.startsWithIgnoreCase ("AudioUnit:"))
                description.pluginFormatName = "AudioUnit";
            else if (path.hasFileExtension (".vst3"))
                description.pluginFormatName = "VST3";
            else if (path.hasFileExtension (".component") || path.hasFileExtension (".appex"))
                description.pluginFormatName = "AudioUnit";
        }

        return description;
    }

    juce::String displayLabel() const
    {
        juce::String label;
        if (manufacturer.isNotEmpty() && name.isNotEmpty())
            label = manufacturer + " - " + name;
        else if (name.isNotEmpty())
            label = name;
        else if (path != juce::File())
            label = path.getFileNameWithoutExtension();
        else
            label = fileOrIdentifier;

        if (! installed)
            label += " (not installed)";
        return label;
    }
};

struct HostConfig
{
    juce::File projectRoot;   // exploration data root (sessions, user config, cache)
    juce::File configFile;    // writable (or read) host.config.json
    juce::File resourcesDir;  // Contents/Resources when bundled
    juce::File fixturesDir;
    juce::File sessionsRoot;
    juce::File pythonCli;     // optional; unused when native session snap is available
    juce::File logFile;          // config path template (before session hash)
    juce::File sessionLogFile;   // logFile with session hash inserted in the stem
    juce::String sessionHash;
    juce::String defaultPluginId;
    /** Preferred MIDI input device names (Audio MIDI Setup). Empty = none preselected. */
    juce::StringArray defaultMidiInputs;
    juce::Array<HostPluginEntry> plugins;

    const HostPluginEntry* findPluginById (const juce::String& id) const;
    /** Configured default_plugin when installed; otherwise nullptr. */
    const HostPluginEntry* configuredDefaultPlugin() const;
    /** First installed plugin in the list (fallback). */
    const HostPluginEntry* firstInstalledPlugin() const;
    /** configuredDefaultPlugin() if set, else firstInstalledPlugin(). */
    const HostPluginEntry* defaultPlugin() const;

    /** Build sessionLogFile from logFile + sessionHash (e.g. plugin_host.log → plugin_host_a1b2c3d4.log). */
    void resolveSessionLogFile();

    /** Create missing session.json folders for each configured plugin. */
    void ensureSessions() const;

    /** Persist this config to configFile (or dest if provided). */
    bool saveToFile (juce::String& error, const juce::File& dest = {}) const;

    static juce::String slugify (juce::String value);
    static juce::String makeUniquePluginId (const juce::String& name,
                                            const juce::Array<HostPluginEntry>& existing);

    static bool loadFromFile (const juce::File& configFile,
                              const juce::File& projectRoot,
                              HostConfig& out,
                              juce::String& error,
                              const juce::File& resourcesDir = {});
};

struct HostCommandLineOptions
{
    juce::File configFile;
    juce::File projectRoot;
    bool configFileExplicit { false };
    bool projectRootExplicit { false };
    /** True when the app was double-clicked as a .app with no path overrides. */
    bool launchedAsStandaloneBundle { false };

    bool parse (const juce::StringArray& args, juce::String& error);
};
