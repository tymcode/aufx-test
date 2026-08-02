#include "HostPreferences.h"

HostPreferences& HostPreferences::get()
{
    static HostPreferences instance;
    return instance;
}

void HostPreferences::initialise()
{
    if (initialised)
        return;

    juce::PropertiesFile::Options options;
    options.applicationName = appName;
    options.filenameSuffix = "settings";
    options.osxLibrarySubFolder = "Application Support";
    options.folderName = appName;
    options.commonToAllUsers = false;
    options.ignoreCaseOfKeyNames = true;
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    properties.setStorageParameters (options);
    initialised = true;
}

juce::PropertiesFile* HostPreferences::settings()
{
    initialise();
    return properties.getUserSettings();
}

juce::File HostPreferences::defaultExplorationDataRoot() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile (appName);
}

juce::File HostPreferences::bundledResourcesDir() const
{
#if JUCE_MAC
    const auto app = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
    const auto resources = app.getChildFile ("Contents").getChildFile ("Resources");
    if (resources.isDirectory())
        return resources;
#endif

    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    auto candidate = exe.getSiblingFile ("Resources");
    if (candidate.isDirectory())
        return candidate;

    candidate = exe.getParentDirectory().getChildFile ("Resources");
    if (candidate.isDirectory())
        return candidate;

    return {};
}

juce::File HostPreferences::bundledConfigFile() const
{
    const auto resources = bundledResourcesDir();
    if (resources != juce::File())
    {
        const auto config = resources.getChildFile ("host.config.json");
        if (config.existsAsFile())
            return config;
    }
    return {};
}

juce::File HostPreferences::bundledFixturesDir() const
{
    const auto resources = bundledResourcesDir();
    if (resources != juce::File())
    {
        const auto fixtures = resources.getChildFile ("fixtures");
        if (fixtures.isDirectory())
            return fixtures;
    }
    return {};
}

juce::String HostPreferences::getExplorationDataRootPref() const
{
    if (auto* s = const_cast<HostPreferences*> (this)->settings())
        return s->getValue (keyExplorationDataRoot);
    return {};
}

juce::String HostPreferences::getConfigPathPref() const
{
    if (auto* s = const_cast<HostPreferences*> (this)->settings())
        return s->getValue (keyConfigPath);
    return {};
}

bool HostPreferences::getAllowInstrumentAudioInput() const
{
    if (auto* s = const_cast<HostPreferences*> (this)->settings())
        return s->getBoolValue (keyAllowInstrumentAudioInput, false);
    return false;
}

int HostPreferences::getPluginScanTimeoutMs() const
{
    if (auto* s = const_cast<HostPreferences*> (this)->settings())
    {
        const int ms = s->getIntValue (keyPluginScanTimeoutMs, defaultPluginScanTimeoutMs);
        return juce::jlimit (minPluginScanTimeoutMs, maxPluginScanTimeoutMs, ms);
    }

    return defaultPluginScanTimeoutMs;
}

void HostPreferences::setExplorationDataRootPref (const juce::String& path)
{
    if (auto* s = settings())
    {
        if (path.isEmpty())
            s->removeValue (keyExplorationDataRoot);
        else
            s->setValue (keyExplorationDataRoot, path);
        s->saveIfNeeded();
    }
}

void HostPreferences::setConfigPathPref (const juce::String& path)
{
    if (auto* s = settings())
    {
        if (path.isEmpty())
            s->removeValue (keyConfigPath);
        else
            s->setValue (keyConfigPath, path);
        s->saveIfNeeded();
    }
}

void HostPreferences::setAllowInstrumentAudioInput (bool allow)
{
    if (auto* s = settings())
    {
        s->setValue (keyAllowInstrumentAudioInput, allow);
        s->saveIfNeeded();
    }
}

void HostPreferences::setPluginScanTimeoutMs (int timeoutMs)
{
    if (auto* s = settings())
    {
        s->setValue (keyPluginScanTimeoutMs,
                     juce::jlimit (minPluginScanTimeoutMs, maxPluginScanTimeoutMs, timeoutMs));
        s->saveIfNeeded();
    }
}

HardwareLoopSettings HostPreferences::getHardwareLoopSettings() const
{
    HardwareLoopSettings hw;
    if (auto* s = const_cast<HostPreferences*> (this)->settings())
    {
        hw.deviceName = s->getValue (keyHwDeviceName);
        hw.sendChannelL = s->getIntValue (keyHwSendL, 2);
        hw.sendChannelR = s->getIntValue (keyHwSendR, 3);
        hw.returnChannelL = s->getIntValue (keyHwReturnL, 0);
        hw.returnChannelR = s->getIntValue (keyHwReturnR, 1);
        hw.monitorChannelL = s->getIntValue (keyHwMonitorL, 0);
        hw.monitorChannelR = s->getIntValue (keyHwMonitorR, 1);
        hw.monitorOutputDeviceName = s->getValue (keyHwMonitorOutputDevice);
        hw.bufferSize = s->getIntValue (keyHwBufferSize, 512);
        hw.latencySamples = s->getIntValue (keyHwLatencySamples, 0);
    }
    return hw;
}

void HostPreferences::setHardwareLoopSettings (const HardwareLoopSettings& settings)
{
    if (auto* s = this->settings())
    {
        // "None Selected" / unconfigured: drop the whole hardware-loop block so
        // Use Hardware stays disabled and nothing stale survives a relaunch.
        if (! settings.isConfigured())
        {
            s->removeValue (keyHwDeviceName);
            s->removeValue (keyHwSendL);
            s->removeValue (keyHwSendR);
            s->removeValue (keyHwReturnL);
            s->removeValue (keyHwReturnR);
            s->removeValue (keyHwMonitorL);
            s->removeValue (keyHwMonitorR);
            s->removeValue (keyHwMonitorOutputDevice);
            s->removeValue (keyHwBufferSize);
            s->removeValue (keyHwLatencySamples);
            s->saveIfNeeded();
            return;
        }

        s->setValue (keyHwDeviceName, settings.deviceName);
        s->setValue (keyHwSendL, settings.sendChannelL);
        s->setValue (keyHwSendR, settings.sendChannelR);
        s->setValue (keyHwReturnL, settings.returnChannelL);
        s->setValue (keyHwReturnR, settings.returnChannelR);
        s->setValue (keyHwMonitorL, settings.monitorChannelL);
        s->setValue (keyHwMonitorR, settings.monitorChannelR);
        if (settings.monitorOutputDeviceName.isEmpty())
            s->removeValue (keyHwMonitorOutputDevice);
        else
            s->setValue (keyHwMonitorOutputDevice, settings.monitorOutputDeviceName);
        s->setValue (keyHwBufferSize, settings.bufferSize);
        s->setValue (keyHwLatencySamples, settings.latencySamples);
        s->saveIfNeeded();
    }
}

juce::String HostPreferences::getMidiOutIdentifier() const
{
    if (auto* s = const_cast<HostPreferences*> (this)->settings())
        return s->getValue (keyMidiOutIdentifier);
    return {};
}

juce::String HostPreferences::getMidiDumpInIdentifier() const
{
    if (auto* s = const_cast<HostPreferences*> (this)->settings())
        return s->getValue (keyMidiDumpInIdentifier);
    return {};
}

juce::String HostPreferences::getMidiSysexModule() const
{
    if (auto* s = const_cast<HostPreferences*> (this)->settings())
        return s->getValue (keyMidiSysexModule);
    return {};
}

bool HostPreferences::getHardwareCaptureCalibrate() const
{
    if (auto* s = const_cast<HostPreferences*> (this)->settings())
        return s->getBoolValue (keyHardwareCaptureCalibrate, true);
    return true;
}

bool HostPreferences::getCaptureGenerateReport() const
{
    if (auto* s = const_cast<HostPreferences*> (this)->settings())
        return s->getBoolValue (keyCaptureGenerateReport, true);
    return true;
}

bool HostPreferences::getCaptureSoftwareSettings() const
{
    if (auto* s = const_cast<HostPreferences*> (this)->settings())
        return s->getBoolValue (keyCaptureSoftwareSettings, true);
    return true;
}

bool HostPreferences::getCaptureHardwareSettings() const
{
    if (auto* s = const_cast<HostPreferences*> (this)->settings())
        return s->getBoolValue (keyCaptureHardwareSettings, true);
    return true;
}

double HostPreferences::getHardwareCaptureSilenceThresholdDb (double defaultThresholdDb) const
{
    if (auto* s = const_cast<HostPreferences*> (this)->settings())
        if (s->containsKey (keyHardwareCaptureSilenceThresholdDb))
            return s->getDoubleValue (keyHardwareCaptureSilenceThresholdDb, defaultThresholdDb);
    return defaultThresholdDb;
}

void HostPreferences::setMidiOutIdentifier (const juce::String& identifier)
{
    if (auto* s = settings())
    {
        if (identifier.isEmpty())
            s->removeValue (keyMidiOutIdentifier);
        else
            s->setValue (keyMidiOutIdentifier, identifier);
        s->saveIfNeeded();
    }
}

void HostPreferences::setMidiDumpInIdentifier (const juce::String& identifier)
{
    if (auto* s = settings())
    {
        if (identifier.isEmpty())
            s->removeValue (keyMidiDumpInIdentifier);
        else
            s->setValue (keyMidiDumpInIdentifier, identifier);
        s->saveIfNeeded();
    }
}

void HostPreferences::setMidiSysexModule (const juce::String& moduleName)
{
    if (auto* s = settings())
    {
        if (moduleName.isEmpty())
            s->removeValue (keyMidiSysexModule);
        else
            s->setValue (keyMidiSysexModule, moduleName);
        s->saveIfNeeded();
    }
}

void HostPreferences::setHardwareCaptureCalibrate (bool shouldCalibrate)
{
    if (auto* s = settings())
    {
        s->setValue (keyHardwareCaptureCalibrate, shouldCalibrate);
        s->saveIfNeeded();
    }
}

void HostPreferences::setCaptureGenerateReport (bool shouldGenerate)
{
    if (auto* s = settings())
    {
        s->setValue (keyCaptureGenerateReport, shouldGenerate);
        s->saveIfNeeded();
    }
}

void HostPreferences::setCaptureSoftwareSettings (bool shouldCapture)
{
    if (auto* s = settings())
    {
        s->setValue (keyCaptureSoftwareSettings, shouldCapture);
        s->saveIfNeeded();
    }
}

void HostPreferences::setCaptureHardwareSettings (bool shouldCapture)
{
    if (auto* s = settings())
    {
        s->setValue (keyCaptureHardwareSettings, shouldCapture);
        s->saveIfNeeded();
    }
}

void HostPreferences::setHardwareCaptureSilenceThresholdDb (double thresholdDb)
{
    if (auto* s = settings())
    {
        s->setValue (keyHardwareCaptureSilenceThresholdDb, thresholdDb);
        s->saveIfNeeded();
    }
}

void HostPreferences::clearPrefs()
{
    if (auto* s = settings())
    {
        s->removeValue (keyExplorationDataRoot);
        s->removeValue (keyConfigPath);
        s->removeValue (keyAllowInstrumentAudioInput);
        s->removeValue (keyPluginScanTimeoutMs);
        s->removeValue (keyHwDeviceName);
        s->removeValue (keyHwSendL);
        s->removeValue (keyHwSendR);
        s->removeValue (keyHwReturnL);
        s->removeValue (keyHwReturnR);
        s->removeValue (keyHwMonitorL);
        s->removeValue (keyHwMonitorR);
        s->removeValue (keyHwMonitorOutputDevice);
        s->removeValue (keyHwBufferSize);
        s->removeValue (keyHwLatencySamples);
        s->removeValue (keyMidiOutIdentifier);
        s->removeValue (keyMidiDumpInIdentifier);
        s->removeValue (keyMidiSysexModule);
        s->removeValue (keyHardwareCaptureCalibrate);
        s->removeValue (keyHardwareCaptureSilenceThresholdDb);
        s->removeValue (keyCaptureGenerateReport);
        s->removeValue (keyCaptureSoftwareSettings);
        s->removeValue (keyCaptureHardwareSettings);
        s->saveIfNeeded();
    }
}

#if ! JUCE_MAC
juce::String HostPreferences::readSystemPreference (const juce::String& key) const
{
    juce::ignoreUnused (key);
    return {};
}
#endif

bool HostPreferences::ensureUserConfigSeeded (const juce::File& dataRoot,
                                              const juce::File& bundledConfig,
                                              juce::File& outConfigFile,
                                              juce::String& error)
{
    dataRoot.createDirectory();
    const auto userConfig = dataRoot.getChildFile ("host.config.json");

    if (userConfig.existsAsFile())
    {
        outConfigFile = userConfig;
        return true;
    }

    if (bundledConfig.existsAsFile())
    {
        if (! bundledConfig.copyFileTo (userConfig))
        {
            error = "Failed to seed host.config.json into " + userConfig.getFullPathName();
            return false;
        }
        outConfigFile = userConfig;
        return true;
    }

    const auto minimal = juce::String (R"({
  "fixtures_dir": "fixtures",
  "sessions_root": "sessions",
  "log_file": "sessions/plugin_host.log",
  "default_midi_input": [],
  "default_plugin": "matrix_reverb",
  "plugins": [
    {
      "id": "matrix_reverb",
      "name": "AUMatrixReverb",
      "manufacturer": "Apple",
      "path": "AudioUnit:Effects/aufx,mrev,appl",
      "format": "AudioUnit",
      "session": "AUMatrixReverb exploration"
    }
  ]
}
)");
    if (! userConfig.replaceWithText (minimal))
    {
        error = "Failed to create host.config.json at " + userConfig.getFullPathName();
        return false;
    }

    outConfigFile = userConfig;
    return true;
}

namespace
{
    bool shouldSkipExplorationRelocateItem (const juce::File& file)
    {
        const auto name = file.getFileName();
        if (name == ".DS_Store")
            return true;
        // JUCE ApplicationProperties live alongside the default data root.
        if (name.endsWithIgnoreCase (".settings"))
            return true;
        return false;
    }

    bool relocateChild (const juce::File& source, const juce::File& dest, juce::String& error)
    {
        if (dest.exists())
            return true; // keep destination

        dest.getParentDirectory().createDirectory();

        if (source.isDirectory())
        {
            if (! source.copyDirectoryTo (dest))
            {
                error = "Failed to copy folder: " + source.getFullPathName();
                return false;
            }

            if (! source.deleteRecursively())
            {
                // Copy succeeded; leave a note but don't fail the relocate.
                error = "Copied " + source.getFileName()
                        + " but could not remove the original at " + source.getFullPathName();
            }
            return true;
        }

        if (! source.moveFileTo (dest))
        {
            // Cross-volume move can fail — fall back to copy + delete.
            if (! source.copyFileTo (dest))
            {
                error = "Failed to move file: " + source.getFullPathName();
                return false;
            }
            if (! source.deleteFile())
            {
                error = "Copied " + source.getFileName()
                        + " but could not remove the original at " + source.getFullPathName();
            }
        }
        return true;
    }
}

bool HostPreferences::relocateExplorationData (const juce::File& from,
                                               const juce::File& to,
                                               juce::String& message)
{
    message.clear();

    if (from == juce::File() || to == juce::File())
        return true;

    if (from.getFullPathName() == to.getFullPathName())
        return true;

    if (! from.isDirectory())
        return true;

    if (! to.createDirectory())
    {
        message = "Could not create exploration data folder: " + to.getFullPathName();
        return false;
    }

    int moved = 0;
    int skippedExisting = 0;
    juce::StringArray warnings;

    for (const auto& child : from.findChildFiles (juce::File::findFilesAndDirectories, false))
    {
        if (shouldSkipExplorationRelocateItem (child))
            continue;

        const auto dest = to.getChildFile (child.getFileName());
        if (dest.exists())
        {
            ++skippedExisting;
            continue;
        }

        juce::String itemError;
        if (! relocateChild (child, dest, itemError))
        {
            message = itemError;
            return false;
        }

        ++moved;
        if (itemError.isNotEmpty())
            warnings.add (itemError);
    }

    if (moved == 0 && skippedExisting == 0)
    {
        message = "No exploration data found to move from " + from.getFullPathName();
    }
    else
    {
        message = "Moved exploration data to " + to.getFullPathName()
                  + " (" + juce::String (moved) + " item"
                  + (moved == 1 ? "" : "s");
        if (skippedExisting > 0)
            message += ", " + juce::String (skippedExisting) + " already present at destination";
        message += ").";
    }

    if (! warnings.isEmpty())
        message += "\n\n" + warnings.joinIntoString ("\n");

    return true;
}

bool HostPreferences::relocateDirectoryContents (const juce::File& from,
                                                 const juce::File& to,
                                                 juce::String& message)
{
    message.clear();

    if (from == juce::File() || to == juce::File())
        return true;

    if (from.getFullPathName() == to.getFullPathName())
        return true;

    if (! from.isDirectory())
    {
        message = "Source folder does not exist: " + from.getFullPathName();
        return false;
    }

    if (! to.createDirectory())
    {
        message = "Could not create folder: " + to.getFullPathName();
        return false;
    }

    int moved = 0;
    int skippedExisting = 0;
    juce::StringArray warnings;

    for (const auto& child : from.findChildFiles (juce::File::findFilesAndDirectories, false))
    {
        if (child.getFileName() == ".DS_Store")
            continue;

        const auto dest = to.getChildFile (child.getFileName());
        if (dest.exists())
        {
            ++skippedExisting;
            continue;
        }

        juce::String itemError;
        if (! relocateChild (child, dest, itemError))
        {
            message = itemError;
            return false;
        }

        ++moved;
        if (itemError.isNotEmpty())
            warnings.add (itemError);
    }

    message = "Moved " + juce::String (moved) + " item" + (moved == 1 ? "" : "s")
              + " to " + to.getFullPathName();
    if (skippedExisting > 0)
        message += " (" + juce::String (skippedExisting) + " already present)";
    message += ".";
    if (! warnings.isEmpty())
        message += "\n\n" + warnings.joinIntoString ("\n");
    return true;
}

bool HostPreferences::copyDirectoryContents (const juce::File& from,
                                             const juce::File& to,
                                             juce::String& message)
{
    message.clear();

    if (from == juce::File() || to == juce::File())
        return true;

    if (! from.isDirectory())
    {
        message = "Source folder does not exist: " + from.getFullPathName();
        return false;
    }

    if (! to.createDirectory())
    {
        message = "Could not create folder: " + to.getFullPathName();
        return false;
    }

    int copied = 0;
    int skippedExisting = 0;

    for (const auto& child : from.findChildFiles (juce::File::findFilesAndDirectories, false))
    {
        if (child.getFileName() == ".DS_Store")
            continue;

        const auto dest = to.getChildFile (child.getFileName());
        if (dest.exists())
        {
            ++skippedExisting;
            continue;
        }

        if (child.isDirectory())
        {
            if (! child.copyDirectoryTo (dest))
            {
                message = "Failed to copy folder: " + child.getFullPathName();
                return false;
            }
        }
        else if (! child.copyFileTo (dest))
        {
            message = "Failed to copy file: " + child.getFullPathName();
            return false;
        }

        ++copied;
    }

    message = "Copied " + juce::String (copied) + " item" + (copied == 1 ? "" : "s")
              + " to " + to.getFullPathName();
    if (skippedExisting > 0)
        message += " (" + juce::String (skippedExisting) + " already present)";
    message += ".";
    return true;
}

bool HostPreferences::resolveLaunchPaths (const HostCommandLineOptions& cli,
                                          juce::File& outConfigFile,
                                          juce::File& outDataRoot,
                                          juce::File& outResourcesDir,
                                          juce::String& error)
{
    initialise();
    outResourcesDir = bundledResourcesDir();

    const bool cliHasDataRoot = cli.projectRootExplicit;
    const bool cliHasConfig = cli.configFileExplicit;

    if (cliHasDataRoot)
    {
        outDataRoot = cli.projectRoot;
    }
    else
    {
        const auto userRoot = getExplorationDataRootPref().trim();
        if (userRoot.isNotEmpty())
            outDataRoot = juce::File (userRoot);
        else
        {
            const auto systemRoot = readSystemPreference (systemKeyExplorationDataRoot);
            if (systemRoot.isNotEmpty())
                outDataRoot = juce::File (systemRoot);
            else if (cli.projectRoot != juce::File() && ! cli.launchedAsStandaloneBundle)
                outDataRoot = cli.projectRoot;
            else
                outDataRoot = defaultExplorationDataRoot();
        }
    }

    if (! outDataRoot.createDirectory())
    {
        error = "Could not create exploration data folder: " + outDataRoot.getFullPathName();
        return false;
    }

    if (cliHasConfig)
    {
        outConfigFile = cli.configFile;
    }
    else
    {
        const auto userConfig = getConfigPathPref().trim();
        if (userConfig.isNotEmpty())
            outConfigFile = juce::File (userConfig);
        else
        {
            const auto systemConfig = readSystemPreference (systemKeyConfigPath);
            if (systemConfig.isNotEmpty())
                outConfigFile = juce::File (systemConfig);
            else
            {
                const auto bundled = bundledConfigFile();
                if (! ensureUserConfigSeeded (outDataRoot, bundled, outConfigFile, error))
                    return false;
            }
        }
    }

    if (! outConfigFile.existsAsFile())
    {
        error = "Config file not found: " + outConfigFile.getFullPathName();
        return false;
    }

    return true;
}
