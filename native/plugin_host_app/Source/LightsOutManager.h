#pragma once

#include <JuceHeader.h>

/** macOS presentation mode: black out other displays/apps behind the host window. */
class LightsOutManager
{
public:
    LightsOutManager();
    ~LightsOutManager();

    void setHostWindow (juce::Component* hostWindowIn) { hostWindow = hostWindowIn; }

    void setEnabled (bool shouldEnable);
    bool isEnabled() const { return enabled; }

    /** Idempotent — restores menu bar, dock, and overlays. */
    void release();

private:
    juce::Component* hostWindow { nullptr };
    bool enabled { false };
};
