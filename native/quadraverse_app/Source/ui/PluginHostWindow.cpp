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

bool PluginHostWindow::isEditableFieldFocused()
{
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    if (focused == nullptr)
        return false;
    if (dynamic_cast<juce::TextEditor*> (focused) != nullptr)
        return true;
    if (focused->findParentComponentOfClass<juce::TextEditor>() != nullptr)
        return true;
    if (dynamic_cast<juce::TextInputTarget*> (focused) != nullptr)
        return true;
    return false;
}

bool PluginHostWindow::SpaceKeys::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    if (! key.isKeyCode (juce::KeyPress::spaceKey))
        return false;
    if (isEditableFieldFocused())
        return false;
    if (owner.panel == nullptr)
        return false;
    owner.panel->togglePlayback();
    return true;
}

} // namespace qverse
