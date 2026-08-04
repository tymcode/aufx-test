#include "PluginHostWindow.h"
#include "Utf8.h"

namespace qverse
{

PluginHostWindow::PluginHostWindow (PluginAudioEngine& engine,
                                    HostConfig& config,
                                    ClosedFn onClosedIn)
    : DocumentWindow (utf8 ("Plugin Host"),
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

    // Editor after NSWindow is on-screen (AU Cocoa / WebView rule).
    content->showEditorWhenReady();
}

PluginHostWindow::~PluginHostWindow()
{
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

} // namespace qverse
