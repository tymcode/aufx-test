#pragma once

#include <JuceHeader.h>

/**
 * Shared modal AlertWindow shell for custom-panel dialogs.
 * Returns the button result (1 = primary, 0 = cancel / dismissed).
 */
namespace HostDialog
{
    inline int runCustomPanelModal (const juce::String& title,
                                    const juce::String& message,
                                    juce::Component& panel,
                                    juce::Component* centreAround,
                                    const juce::String& primaryLabel = "Save",
                                    bool includeCancel = true)
    {
        juce::AlertWindow window (title,
                                  message,
                                  juce::MessageBoxIconType::NoIcon,
                                  centreAround);
        window.addCustomComponent (&panel);
        window.addButton (primaryLabel, 1, juce::KeyPress (juce::KeyPress::returnKey));
        if (includeCancel)
            window.addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        return window.runModalLoop();
    }
}
