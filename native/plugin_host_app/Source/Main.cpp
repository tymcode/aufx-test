#include "MainWindow.h"
#include "HostConfig.h"
#include "HostLog.h"
#include "HostLookAndFeel.h"
#include "HostPreferences.h"
#include "AuPluginScanner.h"
#include "PluginScannerOOP.h"
#include "AppVersion.h"

class PluginHostApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return AUFX_APP_NAME; }
    const juce::String getApplicationVersion() override { return AUFX_VERSION_STRING; }

    bool moreThanOneInstanceAllowed() override
    {
        // Scanner worker is a second process of the same executable.
        const auto cmd = juce::JUCEApplicationBase::getCommandLineParameterArray().joinIntoString (" ");
        return cmd.contains ("--" + juce::String (kAuPluginScannerProcessUID) + ":");
    }

    void initialise (const juce::String& commandLine) override
    {
        if (tryInitialiseAsPluginScannerWorker (commandLine))
        {
            // Running as out-of-process AU scanner — no UI.
            return;
        }

        const juce::StringArray args = juce::JUCEApplicationBase::getCommandLineParameterArray();

        HostPreferences::get().initialise();

        HostCommandLineOptions cli;
        juce::String error;

        if (! cli.parse (args, error))
        {
            if (error == "help")
                printUsage();
            else
                juce::Logger::writeToLog ("Error: " + error);

            quit();
            return;
        }

        juce::File configFile, dataRoot, resourcesDir;
        if (! HostPreferences::get().resolveLaunchPaths (cli, configFile, dataRoot, resourcesDir, error))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    "AU Effects Explorer",
                                                    error);
            quit();
            return;
        }

        HostConfig config;
        if (! HostConfig::loadFromFile (configFile, dataRoot, config, error, resourcesDir))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    "AU Effects Explorer",
                                                    error);
            quit();
            return;
        }

        HostLog::open (config.sessionLogFile);
        HostLog::info ("Config loaded from " + configFile.getFullPathName());
        HostLog::info ("Exploration data root " + dataRoot.getFullPathName());
        HostLog::info ("Session hash " + config.sessionHash);
        HostLog::info ("Logging to " + config.sessionLogFile.getFullPathName());

        lookAndFeel = std::make_unique<HostLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());

        mainWindow = std::make_unique<MainWindow> (std::move (config));
    }

    void shutdown() override
    {
        mainWindow.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        lookAndFeel.reset();
        HostLog::close();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    static void printUsage()
    {
        juce::Logger::writeToLog (
            "Usage: \"AU Effects Explorer\" [--config PATH] [--project-root PATH | --data-root PATH]\n"
            "\n"
            "When launched as a Mac app bundle with no arguments, exploration data defaults to\n"
            "~/Library/Application Support/AU Effects Explorer/ and config is seeded from the bundle.\n"
            "\n"
            "Path resolution:\n"
            "  data root:  CLI > Settings > system plist > Application Support (bundle) or cwd (dev)\n"
            "  config:     CLI > Settings > system plist > <data-root>/host.config.json > bundled");
    }

    std::unique_ptr<HostLookAndFeel> lookAndFeel;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (PluginHostApplication)
