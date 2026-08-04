#include "PluginHostWindow.h"
#include "Utf8.h"

namespace qverse
{

PluginHostWindow::PluginHostWindow (PluginAudioEngine& engine,
                                    HostConfig& config,
                                    ClosedFn onClosedIn)
    : DocumentWindow (utf8 ("Target View"),
                      juce::Desktop::getInstance().getDefaultLookAndFeel()
                          .findColour (juce::ResizableWindow::backgroundColourId),
                      DocumentWindow::allButtons),
      onClosed (std::move (onClosedIn))
{
    setUsingNativeTitleBar (true);
    setResizable (true, true);

    auto* content = new PluginHostPanel (engine, config);
    panel = content;
    setContentOwned (content, true);
    centreWithSize (980, 720);
    setVisible (true);
    toFront (true);

    addKeyListener (&spaceKeys);
    content->addKeyListener (&spaceKeys);

    // Editor after NSWindow is on-screen (AU Cocoa / WebView rule).
    content->showEditorWhenReady();
}

PluginHostWindow::~PluginHostWindow()
{
    if (auto* c = getContentComponent())
        c->removeKeyListener (&spaceKeys);
    removeKeyListener (&spaceKeys);
    panel = nullptr;
}

void PluginHostWindow::closeButtonPressed()
{
    setVisible (false);
    // Defer owner reset so DocumentWindow teardown is not nested in this call.
    juce::MessageManager::callAsync ([fn = onClosed]()
                                     {
                                         if (fn)
                                             fn();
                                     });
}

bool PluginHostWindow::isEditableFieldFocused (juce::Component* eventSource)
{
    auto isEditable = [] (juce::Component* c) -> bool
    {
        if (c == nullptr)
            return false;
        if (dynamic_cast<juce::TextEditor*> (c) != nullptr)
            return true;
        if (c->findParentComponentOfClass<juce::TextEditor>() != nullptr)
            return true;
        if (dynamic_cast<juce::TextInputTarget*> (c) != nullptr)
            return true;
        if (auto* combo = c->findParentComponentOfClass<juce::ComboBox>())
            if (combo->isTextEditable())
                return true;
        return false;
    };

    if (isEditable (juce::Component::getCurrentlyFocusedComponent()))
        return true;
    return isEditable (eventSource);
}

bool PluginHostWindow::SpaceKeys::keyPressed (const juce::KeyPress& key, juce::Component* source)
{
    if (! key.isKeyCode (juce::KeyPress::spaceKey))
        return false;
    if (isEditableFieldFocused (source))
        return false;
    if (owner.panel == nullptr)
        return false;
    owner.panel->togglePlayback();
    return true;
}

} // namespace qverse
