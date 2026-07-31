#pragma once

#include <JuceHeader.h>
#include <functional>
#include "PluginAudioEngine.h"

/**
 * Floating Level Meters window: live Send/Return for the active path, plus
 * a named tone level-sweep that writes calibration/<slug>.{json,png}.
 *
 * Space and host command shortcuts are forwarded via keyHandler so playback
 * and menus keep working while this window is focused.
 */
class LevelMetersWindow : public juce::DocumentWindow
{
public:
    using ClosedFn = std::function<void()>;
    /** Return true if the key was handled. */
    using KeyHandlerFn = std::function<bool (const juce::KeyPress&)>;

    LevelMetersWindow (PluginAudioEngine& engine,
                       juce::File fixturesDir,
                       juce::File calibrationDir,
                       juce::File pythonCli,
                       ClosedFn onClosed,
                       KeyHandlerFn keyHandler);
    ~LevelMetersWindow() override;

    void closeButtonPressed() override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    ClosedFn onClosed;
    KeyHandlerFn keyHandler;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelMetersWindow)
};
