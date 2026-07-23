#include "MainWindow.h"
#include "HostConfig.h"
#include "HostLog.h"
#include "HostLookAndFeel.h"

class PluginHostApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "AU Effects Explorer"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String& commandLine) override
    {
        juce::ignoreUnused (commandLine);
        const juce::StringArray args = juce::JUCEApplicationBase::getCommandLineParameterArray();

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

        HostConfig config;
        if (! HostConfig::loadFromFile (cli.configFile, cli.projectRoot, config, error))
        {
            juce::Logger::writeToLog ("Error: " + error);
            quit();
            return;
        }

        HostLog::open (config.sessionLogFile);
        HostLog::info ("Config loaded from " + cli.configFile.getFullPathName());
        HostLog::info ("Session hash " + config.sessionHash);
        HostLog::info ("Logging to " + config.sessionLogFile.getFullPathName());

        lookAndFeel = std::make_unique<HostLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());

        mainWindow = std::make_unique<MainWindow> (config);
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
            "Usage: plugin_host_app [--config PATH] [--project-root PATH]\n"
            "\n"
            "Defaults:\n"
            "  --project-root  current working directory\n"
            "  --config        <project-root>/host.config.json");
    }

    std::unique_ptr<HostLookAndFeel> lookAndFeel;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (PluginHostApplication)
