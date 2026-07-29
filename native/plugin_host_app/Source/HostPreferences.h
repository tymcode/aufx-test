#pragma once

#include <JuceHeader.h>
#include "HostConfig.h"
#include "HardwareLoopSettings.h"

/** User/system preferences and launch-time path resolution for the standalone app. */
class HostPreferences
{
public:
    static constexpr const char* appName = "AU Effects Explorer";
    static constexpr const char* bundleId = "com.aufxtest.AUEffectsExplorer";
    static constexpr const char* keyExplorationDataRoot = "explorationDataRoot";
    static constexpr const char* keyConfigPath = "configPath";
    static constexpr const char* keyAllowInstrumentAudioInput = "allowInstrumentAudioInput";
    static constexpr const char* keyPluginScanTimeoutMs = "pluginScanTimeoutMs";
    static constexpr const char* keyHwDeviceName = "hwLoopDeviceName";
    static constexpr const char* keyHwSendL = "hwLoopSendL";
    static constexpr const char* keyHwSendR = "hwLoopSendR";
    static constexpr const char* keyHwReturnL = "hwLoopReturnL";
    static constexpr const char* keyHwReturnR = "hwLoopReturnR";
    static constexpr const char* keyHwMonitorL = "hwLoopMonitorL";
    static constexpr const char* keyHwMonitorR = "hwLoopMonitorR";
    static constexpr const char* keyHwMonitorOutputDevice = "hwLoopMonitorOutputDevice";
    static constexpr const char* keyHwBufferSize = "hwLoopBufferSize";
    static constexpr const char* keyHwLatencySamples = "hwLoopLatencySamples";
    static constexpr const char* keyMidiOutIdentifier = "midiOutIdentifier";
    static constexpr const char* keyMidiDumpInIdentifier = "midiDumpInIdentifier";
    static constexpr const char* keyMidiSysexModule = "midiSysexModule";
    static constexpr int defaultPluginScanTimeoutMs = 15000;
    static constexpr int minPluginScanTimeoutMs = 5000;
    static constexpr int maxPluginScanTimeoutMs = 300000;
    static constexpr const char* systemKeyExplorationDataRoot = "ExplorationDataRoot";
    static constexpr const char* systemKeyConfigPath = "ConfigPath";

    static HostPreferences& get();

    void initialise();

    juce::PropertiesFile* settings();

    juce::File defaultExplorationDataRoot() const;
    juce::File bundledResourcesDir() const;
    juce::File bundledConfigFile() const;
    juce::File bundledFixturesDir() const;

    juce::String getExplorationDataRootPref() const;
    juce::String getConfigPathPref() const;
    bool getAllowInstrumentAudioInput() const;
    int getPluginScanTimeoutMs() const;
    HardwareLoopSettings getHardwareLoopSettings() const;
    juce::String getMidiOutIdentifier() const;
    juce::String getMidiDumpInIdentifier() const;
    juce::String getMidiSysexModule() const;
    void setExplorationDataRootPref (const juce::String& path);
    void setConfigPathPref (const juce::String& path);
    void setAllowInstrumentAudioInput (bool allow);
    void setPluginScanTimeoutMs (int timeoutMs);
    void setHardwareLoopSettings (const HardwareLoopSettings& settings);
    void setMidiOutIdentifier (const juce::String& identifier);
    void setMidiDumpInIdentifier (const juce::String& identifier);
    void setMidiSysexModule (const juce::String& moduleName);
    void clearPrefs();

    /** Read org-level defaults from /Library/Preferences (macOS). */
    juce::String readSystemPreference (const juce::String& key) const;

    /**
     * Resolve config + data root:
     * config: CLI --config > user configPath > system ConfigPath > dataRoot/host.config.json > bundled
     * data:  CLI --project-root/--data-root > user explorationDataRoot > system ExplorationDataRoot > Application Support
     */
    bool resolveLaunchPaths (const HostCommandLineOptions& cli,
                             juce::File& outConfigFile,
                             juce::File& outDataRoot,
                             juce::File& outResourcesDir,
                             juce::String& error);

    /** Seed writable host.config.json under dataRoot from bundled template when missing. */
    bool ensureUserConfigSeeded (const juce::File& dataRoot,
                                 const juce::File& bundledConfig,
                                 juce::File& outConfigFile,
                                 juce::String& error);

    /**
     * Move exploration contents (config, plugin cache/skip lists, sessions, etc.)
     * from one data root to another. Skips app preference files (*.settings).
     * Items that already exist at the destination are left alone.
     * Returns false only on hard failure; sets error/warning text in `message`.
     */
    bool relocateExplorationData (const juce::File& from,
                                  const juce::File& to,
                                  juce::String& message);

private:
    HostPreferences() = default;

    juce::ApplicationProperties properties;
    bool initialised { false };
};
