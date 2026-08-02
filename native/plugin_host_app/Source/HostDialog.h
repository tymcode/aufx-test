#pragma once

#include <JuceHeader.h>

/**
 * Shared modal AlertWindow shell for custom-panel dialogs.
 * Returns the button result (1 = primary, 0 = cancel / dismissed).
 * Cancel is always the leftmost button when present.
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
        if (includeCancel)
        {
            window.addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            // Leftmost Cancel must not steal Return when focused.
            if (auto* cancel = window.getButton ("Cancel"))
                cancel->setWantsKeyboardFocus (false);
        }
        window.addButton (primaryLabel, 1, juce::KeyPress (juce::KeyPress::returnKey));

        return window.runModalLoop();
    }
}
