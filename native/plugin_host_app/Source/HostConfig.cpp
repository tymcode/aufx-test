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

    juce::String pathForJson (const juce::File& file, const juce::File& projectRoot)
    {
        if (file == juce::File())
            return {};

        const auto full = file.getFullPathName();
        const auto root = projectRoot.getFullPathName();
        if (root.isNotEmpty() && full.startsWith (root))
        {
            auto rel = full.substring (root.length());
            while (rel.startsWithChar ('/') || rel.startsWithChar ('\\'))
                rel = rel.substring (1);
            if (rel.isNotEmpty())
                return rel;
        }
        return full;
    }
}

const HostPluginEntry* HostConfig::findPluginById (const juce::String& id) const
{
    for (const auto& plugin : plugins)
        if (plugin.id == id)
            return &plugin;
    return nullptr;
}

const HostPluginEntry* HostConfig::configuredDefaultPlugin() const
{
    if (auto* plugin = findPluginById (defaultPluginId))
        if (plugin->installed)
            return plugin;
    return nullptr;
}

const HostPluginEntry* HostConfig::firstInstalledPlugin() const
{
    for (const auto& plugin : plugins)
        if (plugin.installed)
            return &plugin;
    return nullptr;
}

const HostPluginEntry* HostConfig::defaultPlugin() const
{
    // Prefer the configured default when it is installed. Only then fall back
    // to the first installed entry (often Apple AUMatrixReverb in this repo).
    if (auto* preferred = configuredDefaultPlugin())
        return preferred;

    return firstInstalledPlugin();
}

juce::String HostConfig::slugify (juce::String value)
{
    value = value.trim().toLowerCase();
    juce::String out;
    bool lastUnderscore = false;

    for (auto ch : value)
    {
        if (juce::CharacterFunctions::isLetterOrDigit (ch))
        {
            out << ch;
            lastUnderscore = false;
        }
        else if (! lastUnderscore)
        {
            out << '_';
            lastUnderscore = true;
        }
    }

    return out.trimCharactersAtEnd ("_");
}

juce::String HostConfig::makeUniquePluginId (const juce::String& name,
                                             const juce::Array<HostPluginEntry>& existing)
{
    auto base = slugify (name);
    if (base.isEmpty())
        base = "plugin";

    auto candidate = base;
    int suffix = 2;
    for (;;)
    {
        bool taken = false;
        for (const auto& plugin : existing)
        {
            if (plugin.id == candidate)
            {
                taken = true;
                break;
            }
        }
        if (! taken)
            return candidate;
        candidate = base + "_" + juce::String (suffix++);
    }
}

bool HostConfig::loadFromFile (const juce::File& configFileIn,
                               const juce::File& projectRootIn,
                               HostConfig& out,
                               juce::String& error,
                               const juce::File& resourcesDir)
{
    out = {};
    out.configFile = configFileIn;
    out.resourcesDir = resourcesDir;
    out.projectRoot = projectRootIn;
    if (out.projectRoot == juce::File())
        out.projectRoot = configFileIn.getParentDirectory();

    const auto rootVar = readJsonFile (configFileIn, error);
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

    // Prefer bundled fixtures when the relative/default fixtures folder is missing.
    if (! out.fixturesDir.isDirectory() && resourcesDir.isDirectory())
    {
        const auto bundledFixtures = resourcesDir.getChildFile ("fixtures");
        if (bundledFixtures.isDirectory())
            out.fixturesDir = bundledFixtures;
    }

    out.sessionsRoot = resolvePath (root->getProperty ("sessions_root").toString(), out.projectRoot);
    if (out.sessionsRoot == juce::File())
        out.sessionsRoot = out.projectRoot.getChildFile ("sessions");

    out.pythonCli = resolvePath (root->getProperty ("python_cli").toString(), out.projectRoot);
    out.logFile = resolvePath (root->getProperty ("log_file").toString(), out.projectRoot);
    if (out.logFile == juce::File())
        out.logFile = out.projectRoot.getChildFile ("sessions").getChildFile ("plugin_host.log");
    out.defaultPluginId = root->getProperty ("default_plugin").toString();

    out.defaultMidiInputs.clear();
    const auto midiDefaults = root->getProperty ("default_midi_input");
    if (midiDefaults.isArray())
    {
        for (const auto& item : *midiDefaults.getArray())
        {
            const auto name = item.toString().trim();
            if (name.isNotEmpty())
                out.defaultMidiInputs.add (name);
        }
    }
    else
    {
        const auto name = midiDefaults.toString().trim();
        if (name.isNotEmpty())
            out.defaultMidiInputs.add (name);
    }

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
        entry.pluginFormatName = obj->getProperty ("format").toString();
        entry.sessionName = obj->getProperty ("session").toString();
        entry.presetsDir = resolvePath (obj->getProperty ("presets_dir").toString(), out.projectRoot);
        entry.defaultPreset = resolvePath (obj->getProperty ("default_preset").toString(), out.projectRoot);

        const auto pathRaw = obj->getProperty ("path").toString().trim().unquoted();
        if (pathRaw.startsWithIgnoreCase ("AudioUnit:"))
        {
            entry.fileOrIdentifier = pathRaw;
            entry.path = juce::File();
            if (entry.pluginFormatName.isEmpty())
                entry.pluginFormatName = "AudioUnit";
            entry.installed = true;
        }
        else
        {
            entry.path = resolvePath (pathRaw, out.projectRoot);
            entry.fileOrIdentifier = entry.path.getFullPathName();
            entry.installed = entry.path.exists();

            if (entry.pluginFormatName.isEmpty())
            {
                if (entry.path.hasFileExtension (".vst3"))
                    entry.pluginFormatName = "VST3";
                else if (entry.path.hasFileExtension (".component") || entry.path.hasFileExtension (".appex"))
                    entry.pluginFormatName = "AudioUnit";
            }
        }

        if (entry.id.isEmpty())
        {
            if (entry.path != juce::File())
                entry.id = entry.path.getFileNameWithoutExtension().toLowerCase();
            else
                entry.id = slugify (entry.name);
        }
        if (entry.name.isEmpty())
        {
            if (entry.path != juce::File())
                entry.name = entry.path.getFileNameWithoutExtension();
            else
                entry.name = entry.fileOrIdentifier;
        }
        if (entry.sessionName.isEmpty())
            entry.sessionName = entry.name + " exploration";
        if (entry.presetsDir == juce::File() && entry.manufacturer.isNotEmpty())
        {
            entry.presetsDir = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                   .getChildFile ("Library/Audio/Presets")
                                   .getChildFile (entry.manufacturer)
                                   .getChildFile (entry.name);
        }

        if (entry.defaultPreset != juce::File() && ! entry.defaultPreset.existsAsFile())
            entry.defaultPreset = juce::File();

        out.plugins.add (std::move (entry));
    }

    if (out.defaultPluginId.isEmpty() && ! out.plugins.isEmpty())
    {
        if (auto* firstInstalled = out.defaultPlugin())
            out.defaultPluginId = firstInstalled->id;
        else
            out.defaultPluginId = out.plugins.getReference (0).id;
    }

    if (out.defaultPluginId.isNotEmpty() && out.findPluginById (out.defaultPluginId) == nullptr)
    {
        error = "default_plugin \"" + out.defaultPluginId + "\" not found in plugins list";
        return false;
    }

    if (! out.fixturesDir.isDirectory())
    {
        error = "Fixtures directory not found: " + out.fixturesDir.getFullPathName();
        return false;
    }

    // python_cli is optional (native session snap does not require it).
    if (out.pythonCli != juce::File() && ! out.pythonCli.existsAsFile())
        out.pythonCli = juce::File();

    out.sessionsRoot.createDirectory();
    out.sessionHash = juce::Uuid().toString().replace ("-", "").substring (0, 8);
    out.resolveSessionLogFile();
    out.ensureSessions();
    return true;
}

void HostConfig::ensureSessions() const
{
    for (const auto& plugin : plugins)
    {
        const auto sessionDir = sessionsRoot.getChildFile (slugify (plugin.sessionName));
        sessionDir.createDirectory();
        sessionDir.getChildFile ("artifacts").createDirectory();

        const auto sessionFile = sessionDir.getChildFile ("session.json");
        if (sessionFile.existsAsFile())
            continue;

        auto* obj = new juce::DynamicObject();
        obj->setProperty ("name", plugin.sessionName);
        obj->setProperty ("plugin_path", plugin.identifierForLoad());
        obj->setProperty ("description", "");
        const auto now = juce::Time::getCurrentTime().toISO8601 (true);
        obj->setProperty ("created_at", now);
        obj->setProperty ("updated_at", now);
        obj->setProperty ("snapshots", juce::var (juce::Array<juce::var>{}));
        sessionFile.replaceWithText (juce::JSON::toString (juce::var (obj), true) + "\n");
    }
}

bool HostConfig::saveToFile (juce::String& error, const juce::File& dest) const
{
    const auto target = dest != juce::File() ? dest : configFile;
    if (target == juce::File())
    {
        error = "No config file path to save";
        return false;
    }

    auto* root = new juce::DynamicObject();
    root->setProperty ("fixtures_dir", pathForJson (fixturesDir, projectRoot));
    root->setProperty ("sessions_root", pathForJson (sessionsRoot, projectRoot));
    if (pythonCli != juce::File())
        root->setProperty ("python_cli", pathForJson (pythonCli, projectRoot));
    root->setProperty ("log_file", pathForJson (logFile, projectRoot));
    if (defaultPluginId.isNotEmpty())
        root->setProperty ("default_plugin", defaultPluginId);

    juce::Array<juce::var> midiArr;
    for (const auto& name : defaultMidiInputs)
        midiArr.add (name);
    root->setProperty ("default_midi_input", juce::var (midiArr));

    juce::Array<juce::var> pluginsArr;
    for (const auto& plugin : plugins)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("id", plugin.id);
        obj->setProperty ("name", plugin.name);
        if (plugin.manufacturer.isNotEmpty())
            obj->setProperty ("manufacturer", plugin.manufacturer);
        obj->setProperty ("path", plugin.identifierForLoad());
        if (plugin.pluginFormatName.isNotEmpty())
            obj->setProperty ("format", plugin.pluginFormatName);
        if (plugin.presetsDir != juce::File())
            obj->setProperty ("presets_dir", plugin.presetsDir.getFullPathName());
        if (plugin.defaultPreset != juce::File())
            obj->setProperty ("default_preset", plugin.defaultPreset.getFullPathName());
        obj->setProperty ("session", plugin.sessionName);
        pluginsArr.add (juce::var (obj));
    }
    root->setProperty ("plugins", juce::var (pluginsArr));

    const auto text = juce::JSON::toString (juce::var (root), true) + "\n";
    if (! target.replaceWithText (text))
    {
        error = "Failed to write config: " + target.getFullPathName();
        return false;
    }

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
    const auto ext = logFile.getFileExtension();
    const auto hashedName = stem + "_" + sessionHash + ext;
    sessionLogFile = parent.getChildFile (hashedName);
}

bool HostCommandLineOptions::parse (const juce::StringArray& args, juce::String& error)
{
    configFileExplicit = false;
    projectRootExplicit = false;

    for (int i = 0; i < args.size(); ++i)
    {
        const auto& arg = args[i];

        if ((arg == "--config") && i + 1 < args.size())
        {
            configFile = juce::File (cleanPathArg (args[++i]));
            configFileExplicit = true;
            continue;
        }

        if ((arg == "--project-root" || arg == "--data-root") && i + 1 < args.size())
        {
            projectRoot = juce::File (cleanPathArg (args[++i]));
            projectRootExplicit = true;
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

#if JUCE_MAC
    {
        const auto appFile = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
        launchedAsStandaloneBundle = appFile.hasFileExtension ("app")
                                     && ! configFileExplicit
                                     && ! projectRootExplicit;
    }
#else
    launchedAsStandaloneBundle = false;
#endif

    if (! projectRootExplicit)
    {
        if (! launchedAsStandaloneBundle)
            projectRoot = juce::File::getCurrentWorkingDirectory();
        // else: HostPreferences picks Application Support
    }

    if (! configFileExplicit && ! launchedAsStandaloneBundle && projectRoot != juce::File())
        configFile = projectRoot.getChildFile ("host.config.json");

    // Existence is validated after HostPreferences::resolveLaunchPaths.
    return true;
}
