#include <JuceHeader.h>
#include "Utf8.h"
#include "HostPreferences.h"
#include "HostConfig.h"
#include "HostLog.h"
#include "HostLookAndFeel.h"
#include "MainWindow.h"
#include "AppVersion.h"

class QuadraverseApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return HostPreferences::appName; }
    const juce::String getApplicationVersion() override { return AUFX_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String&) override
    {
        const juce::StringArray args = juce::JUCEApplicationBase::getCommandLineParameterArray();
        HostPreferences::get().initialise();

        HostCommandLineOptions cli;
        juce::String error;
        if (! cli.parse (args, error))
        {
            juce::Logger::writeToLog ("Error: " + error);
            quit();
            return;
        }

        juce::File configFile, dataRoot, resourcesDir;
        if (! HostPreferences::get().resolveLaunchPaths (cli, configFile, dataRoot, resourcesDir, error))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    utf8 ("Quadraverse"),
                                                    error);
            quit();
            return;
        }

        HostConfig config;
        if (! HostConfig::loadFromFile (configFile, dataRoot, config, error, resourcesDir))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    utf8 ("Quadraverse"),
                                                    error);
            quit();
            return;
        }

        HostLog::open (config.sessionLogFile);
        HostLog::info ("Quadraverse config " + configFile.getFullPathName());

        lookAndFeel = std::make_unique<HostLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());
        mainWindow = std::make_unique<QuadraverseMainWindow> (std::move (config));
    }

    void shutdown() override
    {
        mainWindow.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        lookAndFeel.reset();
        HostLog::close();
    }

    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<QuadraverseMainWindow> mainWindow;
    std::unique_ptr<HostLookAndFeel> lookAndFeel;
};

START_JUCE_APPLICATION (QuadraverseApplication)
