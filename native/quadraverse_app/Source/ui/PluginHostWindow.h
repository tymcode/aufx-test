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
    struct SpaceKeys : public juce::KeyListener
    {
        explicit SpaceKeys (PluginHostWindow& o) : owner (o) {}
        bool keyPressed (const juce::KeyPress& key, juce::Component*) override;
        PluginHostWindow& owner;
    };

    static bool isEditableFieldFocused (juce::Component* eventSource = nullptr);

    PluginHostPanel* panel { nullptr };
    ClosedFn onClosed;
    SpaceKeys spaceKeys { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginHostWindow)
};

} // namespace qverse
