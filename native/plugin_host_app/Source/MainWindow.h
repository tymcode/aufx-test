#pragma once

#include <JuceHeader.h>
#include "HostConfig.h"
#include "PluginAudioEngine.h"

class MainWindow : public juce::DocumentWindow
{
public:
    explicit MainWindow (HostConfig config);
    ~MainWindow() override;

    void closeButtonPressed() override;

private:
    class MainContent;

    HostConfig config;
    std::unique_ptr<PluginAudioEngine> engine;
    std::unique_ptr<MainContent> content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};
