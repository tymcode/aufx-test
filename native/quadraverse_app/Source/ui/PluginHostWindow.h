#pragma once

#include <JuceHeader.h>
#include <functional>
#include "HostConfig.h"
#include "PluginAudioEngine.h"
#include "PluginHostPanel.h"

namespace qverse
{

class PluginHostWindow : public juce::DocumentWindow
{
public:
    using ClosedFn = std::function<void()>;

    PluginHostWindow (PluginAudioEngine& engine,
                      HostConfig& config,
                      ClosedFn onClosed);
    ~PluginHostWindow() override;

    void closeButtonPressed() override;

    PluginHostPanel* getPanel() const { return panel; }

private:
    PluginHostPanel* panel { nullptr };
    ClosedFn onClosed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginHostWindow)
};

} // namespace qverse
