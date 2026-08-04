#include "PluginHostPanel.h"
#include "Utf8.h"
#include "HostPreferences.h"
#include "MidiEndpointInfo.h"

namespace qverse
{

PluginHostPanel::PluginHostPanel (PluginAudioEngine& engineIn, HostConfig& configIn)
    : engine (engineIn),
      config (configIn)
{
    setOpaque (true);
    setWantsKeyboardFocus (true);

    pluginLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (pluginLabel);
    addAndMakeVisible (pluginField);
    pluginField.onSelect = [this] (int index) { switchToPlugin (index); };

    fixtureLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (fixtureLabel);
    addAndMakeVisible (fixtureBox);
    fixtureBox.addListener (this);

    transportButton.addListener (this);
    transportButton.setWantsKeyboardFocus (false);
    addAndMakeVisible (transportButton);

    loopToggle.setToggleState (engine.isLooping(), juce::dontSendNotification);
    loopToggle.addListener (this);
    loopToggle.setWantsKeyboardFocus (false);
    addAndMakeVisible (loopToggle);

    hostClockToggle.setToggleState (engine.isHostClockEnabled(), juce::dontSendNotification);
    hostClockToggle.addListener (this);
    hostClockToggle.setWantsKeyboardFocus (false);
    addAndMakeVisible (hostClockToggle);

    bpmEditor.setInputRestrictions (6, "0123456789.");
    bpmEditor.setText (juce::String (engine.getHostClockBpm(), 1), juce::dontSendNotification);
    bpmEditor.addListener (this);
    addAndMakeVisible (bpmEditor);
    addAndMakeVisible (bpmLabel);

    bypassButton.setClickingTogglesState (true);
    bypassButton.setToggleState (engine.isBypassed(), juce::dontSendNotification);
    bypassButton.addListener (this);
    bypassButton.setWantsKeyboardFocus (false);
    addAndMakeVisible (bypassButton);

    positionLabel.setJustificationType (juce::Justification::centredLeft);
    positionLabel.setColour (juce::Label::textColourId, juce::Colour (0xffaaaaaa));
    positionLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (positionLabel);

    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xffaaaaaa));
    statusLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (statusLabel);

    addAndMakeVisible (editorViewport);
    editorViewport.setViewedComponent (&editorPlaceholder, false);

    hardwareMeterPanel = std::make_unique<HardwareLoopMeterPanel> (engine);
    addChildComponent (*hardwareMeterPanel);

    populatePlugins();
    populateFixtures();

    if (engine.getPlugin() == nullptr && ! config.plugins.isEmpty())
    {
        int def = 0;
        if (config.defaultPluginId.isNotEmpty())
            for (int i = 0; i < config.plugins.size(); ++i)
                if (config.plugins.getReference (i).id == config.defaultPluginId)
                {
                    def = i;
                    break;
                }
        switchToPlugin (def);
    }
    else if (auto* def = config.defaultPlugin())
    {
        for (int i = 0; i < config.plugins.size(); ++i)
            if (config.plugins.getReference (i).id == def->id)
                currentPluginIndex = i;
        populatePlugins();
    }

    refreshHardwareModeUi();
    startTimerHz (20);
}

PluginHostPanel::~PluginHostPanel()
{
    stopTimer();
    destroyPluginEditor();
}

void PluginHostPanel::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void PluginHostPanel::setStatus (const juce::String& text, bool isError)
{
    statusLabel.setColour (juce::Label::textColourId,
                           isError ? juce::Colours::orange : juce::Colour (0xffaaaaaa));
    statusLabel.setText (text, juce::dontSendNotification);
}

void PluginHostPanel::populatePlugins()
{
    pluginField.setPlugins (config.plugins, currentPluginIndex);
}

void PluginHostPanel::populateFixtures()
{
    sourceClips.rescan (config.fixturesDir);
    const auto current = engine.getCurrentFixtureFile();
    int preferred = 0;
    if (current.existsAsFile())
    {
        const int idx = sourceClips.indexOfFile (current);
        if (idx >= 0)
            preferred = idx + 1;
    }
    sourceClips.rebuildComboBox (fixtureBox, preferred);
}

void PluginHostPanel::switchToPlugin (int index)
{
    if (! juce::isPositiveAndBelow (index, config.plugins.size()))
        return;

    if (index == currentPluginIndex && engine.getPlugin() != nullptr)
    {
        populatePlugins();
        return;
    }

    if (! config.plugins.getReference (index).installed)
    {
        setStatus (utf8 ("Plugin not installed"), true);
        populatePlugins();
        return;
    }

    engine.stopFixture();
    destroyPluginEditor();
    currentPluginIndex = index;

    juce::String error;
    if (! engine.loadPlugin (config.plugins.getReference (index).toPluginDescription(), error))
    {
        setStatus (utf8 ("Failed to load plugin: ") + error, true);
        populatePlugins();
        return;
    }

    populatePlugins();

    if (! engine.isHardwareMode())
        recreatePluginEditor();

    if (! engine.startAudioDevice (error))
    {
        setStatus (utf8 ("Audio device error: ") + error, true);
        return;
    }

    engine.setPluginProcessingSuspended (false);
    setStatus (utf8 ("Loaded ") + config.plugins.getReference (index).displayLabel());
}

void PluginHostPanel::selectFixture (int comboId)
{
    if (comboId == SourceClipLibrary::selectOtherItemId)
    {
        auto chooser = std::make_shared<juce::FileChooser> (
            utf8 ("Select source clip"),
            config.fixturesDir,
            "*.wav;*.aif;*.aiff;*.flac;*.mp3");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, chooser] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();
                                  if (! file.existsAsFile())
                                      return;
                                  const int id = sourceClips.selectOrAddTemporaryTopLevel (fixtureBox, file);
                                  if (id > 0)
                                      selectFixture (id);
                              });
        return;
    }

    const auto file = sourceClips.getFileForId (comboId);
    if (! file.existsAsFile())
        return;

    juce::String error;
    if (! engine.loadFixture (file, error))
    {
        setStatus (error, true);
        return;
    }

    setStatus (utf8 ("Loaded clip ") + file.getFileName());
}

void PluginHostPanel::startPlayback()
{
    if (engine.getCurrentFixtureFile() == juce::File())
    {
        setStatus (utf8 ("No source clip loaded"), true);
        return;
    }
    engine.playFixture();
    transportButton.setPlaying (true);
}

void PluginHostPanel::stopPlayback()
{
    engine.stopFixture();
    transportButton.setPlaying (false);
}

void PluginHostPanel::togglePlayback()
{
    if (engine.isPlaying())
        stopPlayback();
    else
        startPlayback();
}

void PluginHostPanel::applyBpmFromEditor()
{
    const double bpm = bpmEditor.getText().getDoubleValue();
    if (bpm >= 20.0 && bpm <= 400.0)
        engine.setHostClockBpm (bpm);
    else
        bpmEditor.setText (juce::String (engine.getHostClockBpm(), 1), juce::dontSendNotification);
}

void PluginHostPanel::destroyPluginEditor()
{
    engine.destroyEditor (pluginEditor);
    editorPlaceholder.removeAllChildren();
}

void PluginHostPanel::recreatePluginEditor()
{
    destroyPluginEditor();
    if (engine.isHardwareMode() || engine.getPlugin() == nullptr)
        return;

    pluginEditor = engine.createEditor();
    if (pluginEditor == nullptr)
    {
        setStatus (utf8 ("Plugin has no editor"), true);
        return;
    }

    editorPlaceholder.addAndMakeVisible (*pluginEditor);
    layoutEditor();

    if (engine.getDeviceSampleRate() > 0)
        engine.setPluginProcessingSuspended (false);
}

void PluginHostPanel::layoutEditor()
{
    if (pluginEditor == nullptr)
    {
        editorPlaceholder.setSize (editorViewport.getWidth(), editorViewport.getHeight());
        return;
    }

    const int width = juce::jmax (pluginEditor->getWidth(), editorViewport.getWidth());
    const int height = juce::jmax (pluginEditor->getHeight(), editorViewport.getHeight());
    pluginEditor->setBounds (0, 0, pluginEditor->getWidth(), pluginEditor->getHeight());
    editorPlaceholder.setSize (width, height);
}

void PluginHostPanel::showHardwareMetersBody()
{
    destroyPluginEditor();
    editorViewport.setVisible (false);
    if (hardwareMeterPanel != nullptr)
    {
        hardwareMeterPanel->setVisible (true);
        hardwareMeterPanel->toFront (false);
    }
    bypassButton.setEnabled (false);
    resized();
}

void PluginHostPanel::showPluginEditorBody()
{
    if (hardwareMeterPanel != nullptr)
        hardwareMeterPanel->setVisible (false);

    editorViewport.setVisible (true);
    bypassButton.setEnabled (true);

    if (engine.getPlugin() != nullptr && pluginEditor == nullptr)
        recreatePluginEditor();
    else
        layoutEditor();

    resized();
}

void PluginHostPanel::refreshHardwareModeUi()
{
    const bool hw = engine.isHardwareMode();
    pluginField.setEnabled (! hw);
    if (hw)
    {
        juce::String device;
        const auto outId = HostPreferences::get().getMidiOutIdentifier();
        if (outId.isNotEmpty())
            device = findMidiEndpointInfo (outId, true).name;
        if (device.isEmpty())
            device = HostPreferences::get().getMidiSysexModule();
        if (device.isEmpty())
            device = utf8 ("Quadraverb");
        pluginField.setForcedDisplayText (utf8 ("Using ") + device);
    }
    else
    {
        pluginField.clearForcedDisplayText();
    }

    if (hw)
        showHardwareMetersBody();
    else
        showPluginEditorBody();
}

void PluginHostPanel::showEditorWhenReady()
{
    editorCreatePending = true;
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PluginHostPanel> (this)]
                                     {
                                         if (safe == nullptr)
                                             return;
                                         safe->editorCreatePending = false;
                                         safe->refreshHardwareModeUi();
                                         juce::String error;
                                         if (! safe->engine.startAudioDevice (error) && error.isNotEmpty())
                                             safe->setStatus (error, true);
                                         else
                                             safe->engine.setPluginProcessingSuspended (false);
                                     });
}

void PluginHostPanel::recreateEditorAfterPluginReload()
{
    if (engine.isHardwareMode())
        return;
    recreatePluginEditor();
}

void PluginHostPanel::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto strip = r.removeFromTop (32);
    const int gap = 6;

    pluginLabel.setBounds (strip.removeFromLeft (48));
    strip.removeFromLeft (gap);
    pluginField.setBounds (strip.removeFromLeft (220));
    strip.removeFromLeft (gap);

    fixtureLabel.setBounds (strip.removeFromLeft (72));
    strip.removeFromLeft (gap);
    fixtureBox.setBounds (strip.removeFromLeft (180));
    strip.removeFromLeft (gap);

    transportButton.setBounds (strip.removeFromLeft (36).withSizeKeepingCentre (36, 28));
    strip.removeFromLeft (gap);
    loopToggle.setBounds (strip.removeFromLeft (28).withSizeKeepingCentre (26, 26));
    strip.removeFromLeft (gap);

    hostClockToggle.setBounds (strip.removeFromLeft (90));
    strip.removeFromLeft (gap);
    bpmLabel.setBounds (strip.removeFromLeft (32));
    bpmEditor.setBounds (strip.removeFromLeft (56));
    strip.removeFromLeft (gap);
    bypassButton.setBounds (strip.removeFromLeft (70));
    strip.removeFromLeft (gap);
    positionLabel.setBounds (strip);

    auto status = r.removeFromBottom (22);
    statusLabel.setBounds (status);
    r.removeFromBottom (4);

    editorViewport.setBounds (r);
    if (hardwareMeterPanel != nullptr)
        hardwareMeterPanel->setBounds (r);

    layoutEditor();
}

void PluginHostPanel::timerCallback()
{
    transportButton.setPlaying (engine.isPlaying());

    // DAW surface Play/Stop (MIDI Start/Stop, Mackie/HUI notes 94/93, MMC).
    if (engine.consumeTransportPlayRequest())
        startPlayback();
    if (engine.consumeTransportStopRequest())
        stopPlayback();

    const double pos = engine.getFixturePositionSeconds();
    const double len = engine.getFixtureLengthSeconds();
    if (len > 0.0)
        positionLabel.setText (juce::String (pos, 1) + " / " + juce::String (len, 1) + " s",
                               juce::dontSendNotification);
    else
        positionLabel.setText ({}, juce::dontSendNotification);
}

void PluginHostPanel::comboBoxChanged (juce::ComboBox* box)
{
    if (box == &fixtureBox)
        selectFixture (fixtureBox.getSelectedId());
}

void PluginHostPanel::buttonClicked (juce::Button* button)
{
    if (button == &transportButton)
    {
        if (engine.isPlaying())
            stopPlayback();
        else
            startPlayback();
    }
    else if (button == &loopToggle)
    {
        engine.setLooping (loopToggle.getToggleState());
    }
    else if (button == &hostClockToggle)
    {
        engine.setHostClockEnabled (hostClockToggle.getToggleState());
    }
    else if (button == &bypassButton)
    {
        engine.setBypassed (bypassButton.getToggleState());
    }
}

void PluginHostPanel::textEditorReturnKeyPressed (juce::TextEditor&)
{
    applyBpmFromEditor();
}

void PluginHostPanel::textEditorFocusLost (juce::TextEditor&)
{
    applyBpmFromEditor();
}

} // namespace qverse
