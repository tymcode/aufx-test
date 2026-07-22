#include "HostConfig.h"

namespace
{
    juce::String cleanPathArg (const juce::String& raw)
    {
        return raw.trim().unquoted();
    }

    juce::File resolvePath (const juce::String& raw, const juce::File& projectRoot)
    {
        auto text = raw.trim().unquoted();
        if (text.isEmpty())
            return {};

        if (text.startsWithChar ('~'))
        {
            const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName();
            text = home + text.substring (1);
        }

        if (juce::File::isAbsolutePath (text))
            return juce::File (text);

        return projectRoot.getChildFile (text);
    }

    juce::var readJsonFile (const juce::File& file, juce::String& error)
    {
        if (! file.existsAsFile())
        {
            error = "Config file not found: " + file.getFullPathName();
            return {};
        }

        const auto text = file.loadFileAsString();
        if (text.isEmpty())
        {
            error = "Config file is empty: " + file.getFullPathName();
            return {};
        }

        const auto parsed = juce::JSON::parse (text);
        if (parsed.isVoid())
        {
            error = "Failed to parse JSON config: " + file.getFullPathName();
            return {};
        }

        return parsed;
    }
}

const HostPluginEntry* HostConfig::findPluginById (const juce::String& id) const
{
    for (const auto& plugin : plugins)
        if (plugin.id == id)
            return &plugin;
    return nullptr;
}

const HostPluginEntry* HostConfig::defaultPlugin() const
{
    if (auto* plugin = findPluginById (defaultPluginId))
        if (plugin->installed)
            return plugin;

    for (const auto& plugin : plugins)
        if (plugin.installed)
            return &plugin;

    if (! plugins.isEmpty())
        return &plugins.getReference (0);
    return nullptr;
}

bool HostConfig::loadFromFile (const juce::File& configFile,
                               const juce::File& projectRootIn,
                               HostConfig& out,
                               juce::String& error)
{
    out = {};
    out.projectRoot = projectRootIn;
    if (out.projectRoot == juce::File())
        out.projectRoot = configFile.getParentDirectory();

    const auto rootVar = readJsonFile (configFile, error);
    if (rootVar.isVoid())
        return false;

    auto* root = rootVar.getDynamicObject();
    if (root == nullptr)
    {
        error = "Config root must be a JSON object";
        return false;
    }

    out.fixturesDir = resolvePath (root->getProperty ("fixtures_dir").toString(), out.projectRoot);
    if (out.fixturesDir == juce::File())
        out.fixturesDir = out.projectRoot.getChildFile ("fixtures");

    out.sessionsRoot = resolvePath (root->getProperty ("sessions_root").toString(), out.projectRoot);
    if (out.sessionsRoot == juce::File())
        out.sessionsRoot = out.projectRoot.getChildFile ("sessions");

    out.pythonCli = resolvePath (root->getProperty ("python_cli").toString(), out.projectRoot);
    out.logFile = resolvePath (root->getProperty ("log_file").toString(), out.projectRoot);
    if (out.logFile == juce::File())
        out.logFile = out.projectRoot.getChildFile ("sessions").getChildFile ("plugin_host.log");
    out.defaultPluginId = root->getProperty ("default_plugin").toString();
    out.defaultMidiInput = root->getProperty ("default_midi_input").toString().trim();

    const auto pluginsVar = root->getProperty ("plugins");
    if (! pluginsVar.isArray())
    {
        error = "Config must contain a \"plugins\" array";
        return false;
    }

    for (const auto& item : *pluginsVar.getArray())
    {
        auto* obj = item.getDynamicObject();
        if (obj == nullptr)
            continue;

        HostPluginEntry entry;
        entry.id = obj->getProperty ("id").toString();
        entry.name = obj->getProperty ("name").toString();
        entry.manufacturer = obj->getProperty ("manufacturer").toString();
        entry.path = resolvePath (obj->getProperty ("path").toString(), out.projectRoot);
        entry.presetsDir = resolvePath (obj->getProperty ("presets_dir").toString(), out.projectRoot);
        entry.defaultPreset = resolvePath (obj->getProperty ("default_preset").toString(), out.projectRoot);
        entry.sessionName = obj->getProperty ("session").toString();

        if (entry.id.isEmpty())
            entry.id = entry.path.getFileNameWithoutExtension().toLowerCase();
        if (entry.name.isEmpty())
            entry.name = entry.path.getFileNameWithoutExtension();
        if (entry.sessionName.isEmpty())
            entry.sessionName = entry.name + " exploration";
        if (entry.presetsDir == juce::File() && entry.manufacturer.isNotEmpty())
        {
            entry.presetsDir = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                   .getChildFile ("Library/Audio/Presets")
                                   .getChildFile (entry.manufacturer)
                                   .getChildFile (entry.name);
        }

        entry.installed = entry.path.exists();

        // default_preset is optional; ignore missing files rather than failing startup.
        if (entry.defaultPreset != juce::File() && ! entry.defaultPreset.existsAsFile())
            entry.defaultPreset = juce::File();

        out.plugins.add (std::move (entry));
    }

    if (out.plugins.isEmpty())
    {
        error = "Config \"plugins\" array is empty";
        return false;
    }

    if (out.defaultPluginId.isEmpty())
    {
        if (auto* firstInstalled = out.defaultPlugin())
            out.defaultPluginId = firstInstalled->id;
        else
            out.defaultPluginId = out.plugins.getReference (0).id;
    }

    if (out.findPluginById (out.defaultPluginId) == nullptr)
    {
        error = "default_plugin \"" + out.defaultPluginId + "\" not found in plugins list";
        return false;
    }

    if (! out.fixturesDir.isDirectory())
    {
        error = "Fixtures directory not found: " + out.fixturesDir.getFullPathName();
        return false;
    }

    if (out.pythonCli != juce::File() && ! out.pythonCli.existsAsFile())
    {
        error = "python_cli not found: " + out.pythonCli.getFullPathName();
        return false;
    }

    out.sessionsRoot.createDirectory();
    out.sessionHash = juce::Uuid().toString().replace ("-", "").substring (0, 8);
    out.resolveSessionLogFile();
    return true;
}

void HostConfig::resolveSessionLogFile()
{
    if (logFile.getFullPathName().isEmpty())
    {
        sessionLogFile = juce::File();
        return;
    }

    const auto parent = logFile.getParentDirectory();
    const auto stem = logFile.getFileNameWithoutExtension();
    const auto ext = logFile.getFileExtension(); // includes leading '.'
    const auto hashedName = stem + "_" + sessionHash + ext;
    sessionLogFile = parent.getChildFile (hashedName);
}

bool HostCommandLineOptions::parse (const juce::StringArray& args, juce::String& error)
{
    for (int i = 0; i < args.size(); ++i)
    {
        const auto& arg = args[i];

        if (arg == "--config" && i + 1 < args.size())
        {
            configFile = juce::File (cleanPathArg (args[++i]));
            continue;
        }

        if (arg == "--project-root" && i + 1 < args.size())
        {
            projectRoot = juce::File (cleanPathArg (args[++i]));
            continue;
        }

        if (arg == "--help" || arg == "-h")
        {
            error = "help";
            return false;
        }

        error = "Unknown argument: " + arg;
        return false;
    }

    if (projectRoot == juce::File())
        projectRoot = juce::File::getCurrentWorkingDirectory();

    if (configFile == juce::File())
        configFile = projectRoot.getChildFile ("host.config.json");

    if (! configFile.existsAsFile())
    {
        error = "Config file not found: " + configFile.getFullPathName();
        return false;
    }

    return true;
}
