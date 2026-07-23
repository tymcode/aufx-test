#include "MainWindow.h"
#include "HostLog.h"
#include "OfflineCapture.h"

namespace
{
    juce::Array<juce::File> collectFiles (const juce::File& root, const juce::String& extension, bool recursive)
    {
        juce::Array<juce::File> results;
        juce::Array<juce::File> stack;
        stack.add (root);

        while (! stack.isEmpty())
        {
            const auto dir = stack.removeAndReturn (0);
            for (const auto& entry : dir.findChildFiles (juce::File::findFilesAndDirectories, false))
            {
                if (entry.isDirectory())
                {
                    if (recursive)
                        stack.add (entry);
                }
                else if (entry.hasFileExtension (extension))
                {
                    results.add (entry);
                }
            }
        }

        struct FileComparator
        {
            static int compareElements (const juce::File& a, const juce::File& b)
            {
                return a.getFileName().compareIgnoreCase (b.getFileName());
            }
        };

        FileComparator comparator;
        results.sort (comparator);
        return results;
    }

    juce::String slugify (juce::String value)
    {
        value = value.trim().toLowerCase();
        juce::String out;
        bool lastUnderscore = false;

        for (auto ch : value)
        {
            if (juce::CharacterFunctions::isLetterOrDigit (ch))
            {
                out << ch;
                lastUnderscore = false;
            }
            else if (! lastUnderscore)
            {
                out << '_';
                lastUnderscore = true;
            }
        }

        return out.trimCharactersAtEnd ("_");
    }

    juce::String keywordFromDescription (const juce::String& description)
    {
        const auto slug = slugify (description);
        if (slug.isEmpty())
            return {};

        return slug.upToFirstOccurrenceOf ("_", false, false);
    }

}

class MidiActivityLed : public juce::Component
{
public:
    void setActive (bool shouldBeActive)
    {
        if (active == shouldBeActive)
            return;
        active = shouldBeActive;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (2.0f);
        const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
        auto led = juce::Rectangle<float> (size, size).withCentre (bounds.getCentre());
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillEllipse (led.expanded (1.0f));
        g.setColour (active ? juce::Colour (0xff3dde6a) : juce::Colour (0xff3a3a3a));
        g.fillEllipse (led);
    }

private:
    bool active { false };
};

/** TR-808-style step switch; embeds the quarter-note tempo LED. */
class ClickToggleButton : public juce::Button
{
public:
    ClickToggleButton()
        : juce::Button ("Click")
    {
        setClickingTogglesState (true);
        setTooltip ("Metronome click (LED always flashes on quarter notes)");
    }

    void setLedActive (bool shouldBeActive)
    {
        if (ledActive == shouldBeActive)
            return;
        ledActive = shouldBeActive;
        repaint();
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const bool on = getToggleState();
        const bool depressed = on || down;

        auto outer = getLocalBounds().toFloat().reduced (1.0f);
        const float corner = 3.0f;

        // Chassis recess behind the key
        g.setColour (juce::Colour (0xff1a1a1a));
        g.fillRoundedRectangle (outer, corner);

        // Cap sits slightly proud when up, flush when depressed
        auto cap = depressed ? outer.reduced (2.0f, 2.0f).translated (0.0f, 1.0f)
                             : outer.reduced (1.5f, 1.5f).translated (0.0f, -0.5f);

        // 808 step keys: olive-grey plastic
        juce::Colour face = on ? juce::Colour (0xff4a5a3e) : juce::Colour (0xff3d3d38);
        if (highlighted && ! depressed)
            face = face.brighter (0.08f);

        g.setColour (face);
        g.fillRoundedRectangle (cap, 2.5f);

        // Bevel: light from top-left (raised) or inverted when pressed
        const float bevel = 1.4f;
        if (! depressed)
        {
            g.setColour (face.brighter (0.35f));
            g.fillRoundedRectangle (cap.getX(), cap.getY(), cap.getWidth(), bevel + 0.5f, 2.0f);
            g.fillRoundedRectangle (cap.getX(), cap.getY(), bevel + 0.5f, cap.getHeight(), 2.0f);

            g.setColour (face.darker (0.45f));
            g.fillRoundedRectangle (cap.getX(), cap.getBottom() - bevel, cap.getWidth(), bevel, 2.0f);
            g.fillRoundedRectangle (cap.getRight() - bevel, cap.getY(), bevel, cap.getHeight(), 2.0f);
        }
        else
        {
            g.setColour (face.darker (0.4f));
            g.fillRoundedRectangle (cap.getX(), cap.getY(), cap.getWidth(), bevel, 2.0f);
            g.fillRoundedRectangle (cap.getX(), cap.getY(), bevel, cap.getHeight(), 2.0f);

            g.setColour (face.brighter (0.12f));
            g.fillRoundedRectangle (cap.getX(), cap.getBottom() - bevel * 0.7f, cap.getWidth(), bevel * 0.7f, 2.0f);
            g.fillRoundedRectangle (cap.getRight() - bevel * 0.7f, cap.getY(), bevel * 0.7f, cap.getHeight(), 2.0f);
        }

        // Soft face sheen
        g.setColour (juce::Colours::white.withAlpha (depressed ? 0.04f : 0.08f));
        g.fillRoundedRectangle (cap.reduced (2.5f).removeFromTop (cap.getHeight() * 0.35f), 1.5f);

        // Centered tempo LED
        const float ledSize = juce::jmin (10.0f, cap.getWidth() * 0.38f);
        auto led = juce::Rectangle<float> (ledSize, ledSize).withCentre (cap.getCentre());
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillEllipse (led.expanded (1.2f));
        g.setColour (ledActive ? juce::Colour (0xffff6a2a) // 808-ish orange-red step lamp
                               : juce::Colour (0xff2a2218));
        g.fillEllipse (led);
        if (ledActive)
        {
            g.setColour (juce::Colour (0xffffc090).withAlpha (0.55f));
            g.fillEllipse (led.reduced (ledSize * 0.28f));
        }
    }

private:
    bool ledActive { false };
};

/** LCD-style status readout consistent with the host chrome. */
class StatusDisplay : public juce::Component
{
public:
    StatusDisplay()
    {
        label.setJustificationType (juce::Justification::centredLeft);
        label.setColour (juce::Label::textColourId, juce::Colour (0xff8dff9e));
        label.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        label.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.5f, juce::Font::plain)));
        addAndMakeVisible (label);
    }

    void setMessage (const juce::String& text, bool isError)
    {
        label.setColour (juce::Label::textColourId,
                         isError ? juce::Colour (0xffff8a5c) : juce::Colour (0xff8dff9e));
        label.setText (text, juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat().reduced (0.5f);

        g.setColour (juce::Colour (0xff101510));
        g.fillRoundedRectangle (area, 3.0f);

        // Inner top shadow — recessed well
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (area.getX() + 1.0f, area.getY() + 1.0f,
                                area.getWidth() - 2.0f, 2.0f, 2.0f);
        g.setColour (juce::Colour (0xff1a2a1a).withAlpha (0.5f));
        g.fillRoundedRectangle (area.reduced (2.0f), 2.0f);

        g.setColour (juce::Colour (0xff191919));
        g.drawRoundedRectangle (area, 3.0f, 1.0f);
    }

    void resized() override
    {
        label.setBounds (getLocalBounds().reduced (8, 2));
    }

private:
    juce::Label label;
};

/** Combined play/stop key: play triangle when stopped, stop square when playing. */
class TransportButton : public juce::Button
{
public:
    TransportButton() : juce::Button ("Transport")
    {
        setTooltip ("Play / stop the source clip (Space)");
    }

    void setPlaying (bool shouldBePlaying)
    {
        if (playing == shouldBePlaying)
            return;
        playing = shouldBePlaying;
        repaint();
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        getLookAndFeel().drawButtonBackground (g, *this,
                                               findColour (juce::TextButton::buttonColourId),
                                               highlighted, down || playing);

        auto area = getLocalBounds().toFloat();
        const float glyph = juce::jmin (area.getWidth(), area.getHeight()) * 0.42f;
        auto centre = area.getCentre();

        if (playing)
        {
            auto sq = juce::Rectangle<float> (glyph, glyph).withCentre (centre);
            g.setColour (juce::Colour (0xffff6a2a));
            g.fillRoundedRectangle (sq, 1.5f);
        }
        else
        {
            juce::Path tri;
            const float half = glyph * 0.5f;
            tri.addTriangle (centre.x - half * 0.8f, centre.y - half,
                             centre.x - half * 0.8f, centre.y + half,
                             centre.x + half,        centre.y);
            g.setColour (juce::Colour (0xffe6e6e6));
            g.fillPath (tri);
        }
    }

private:
    bool playing { false };
};

/** Loop toggle drawn as chasing arrows; on = loop, off = one-shot. */
class LoopToggleButton : public juce::Button
{
public:
    LoopToggleButton() : juce::Button ("Loop")
    {
        setClickingTogglesState (true);
        setTooltip ("Loop the clip, or play once (one-shot) when off");
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        getLookAndFeel().drawButtonBackground (g, *this,
                                               findColour (juce::TextButton::buttonColourId),
                                               highlighted, down);

        const bool on = getToggleState();
        auto area = getLocalBounds().toFloat().reduced ((float) getWidth() * 0.24f, (float) getHeight() * 0.24f);
        const float radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
        auto centre = area.getCentre();
        const float thickness = juce::jmax (1.6f, radius * 0.32f);

        auto arc = [&] (float startRad, float endRad)
        {
            juce::Path p;
            p.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startRad, endRad, true);
            g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::butt));
        };

        auto head = [&] (float angleRad, float dir)
        {
            const float hx = centre.x + std::sin (angleRad) * radius;
            const float hy = centre.y - std::cos (angleRad) * radius;
            // Tangent direction of the circle at this angle
            const float tx = std::cos (angleRad) * dir;
            const float ty = std::sin (angleRad) * dir;
            const float s = thickness * 1.7f;
            juce::Path tri;
            tri.addTriangle (hx + tx * s,            hy + ty * s,
                             hx - ty * s * 0.6f - tx * s * 0.2f, hy + tx * s * 0.6f - ty * s * 0.2f,
                             hx + ty * s * 0.6f - tx * s * 0.2f, hy - tx * s * 0.6f - ty * s * 0.2f);
            g.fillPath (tri);
        };

        g.setColour (on ? juce::Colour (0xffff6a2a) : juce::Colour (0xff777777));

        // Two opposing arcs with a gap, forming chasing arrows
        const float pi = juce::MathConstants<float>::pi;
        arc (0.35f, pi - 0.15f);
        arc (pi + 0.35f, pi * 2.0f - 0.15f);
        head (pi - 0.15f, 1.0f);
        head (pi * 2.0f - 0.15f, 1.0f);
    }
};

class MainWindow::MainContent : public juce::Component,
                                private juce::Button::Listener,
                                private juce::ComboBox::Listener,
                                private juce::TextEditor::Listener,
                                private juce::KeyListener,
                                private juce::Timer
{
public:
    MainContent (PluginAudioEngine& audioEngine, HostConfig hostConfig)
        : engine (audioEngine), config (std::move (hostConfig))
    {
        setOpaque (true);
        setWantsKeyboardFocus (true);

        currentPluginIndex = 0;
        if (auto* preferred = config.defaultPlugin())
        {
            for (int i = 0; i < config.plugins.size(); ++i)
            {
                if (&config.plugins.getReference (i) == preferred)
                {
                    currentPluginIndex = i;
                    break;
                }
            }
        }
        else
        {
            for (int i = 0; i < config.plugins.size(); ++i)
            {
                if (config.plugins.getReference (i).installed)
                {
                    currentPluginIndex = i;
                    break;
                }
            }
        }

        pluginLabel.setText ("Plugin", juce::dontSendNotification);
        pluginLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (pluginLabel);

        pluginBox.setTooltip ("Plugins from host.config.json");
        addAndMakeVisible (pluginBox);
        pluginBox.addListener (this);

        setStatus ("Loading plugin...");
        addAndMakeVisible (statusDisplay);

        presetLabel.setText ("Preset", juce::dontSendNotification);
        presetLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (presetLabel);
        addAndMakeVisible (presetBox);
        presetBox.addListener (this);

        fixtureLabel.setText ("Source Clip", juce::dontSendNotification);
        fixtureLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (fixtureLabel);
        addAndMakeVisible (fixtureBox);
        fixtureBox.addListener (this);

        configureButton (resetButton, "Reset");
        resetButton.setTooltip ("Reload the plugin at its default state");
        configureButton (loadPresetButton, "Load");
        configureButton (savePresetButton, "Save");
        configureButton (captureButton, "Capture Test Case");

        transportButton.addListener (this);
        addAndMakeVisible (transportButton);

        loopToggle.setToggleState (true, juce::dontSendNotification);
        loopToggle.addListener (this);
        addAndMakeVisible (loopToggle);
        engine.setLooping (true);

        savePresetNameLabel.setText ("Save as", juce::dontSendNotification);
        savePresetNameLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (savePresetNameLabel);
        savePresetNameEditor.setText ("Untitled", juce::dontSendNotification);
        savePresetNameEditor.setInputRestrictions (64);
        savePresetNameEditor.setJustification (juce::Justification::centredLeft);
        addAndMakeVisible (savePresetNameEditor);

        descriptionLabel.setText ("Description", juce::dontSendNotification);
        descriptionLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (descriptionLabel);
        snapshotNameEditor.setText ("snapshot", juce::dontSendNotification);
        snapshotNameEditor.setInputRestrictions (64);
        snapshotNameEditor.setJustification (juce::Justification::centredLeft);
        addAndMakeVisible (snapshotNameEditor);

        // Output WAV role flag: golden→_gld, suspect→_sus, broken→_bkn
        artifactRoleBox.addItem ("golden", 1);
        artifactRoleBox.addItem ("suspect", 2);
        artifactRoleBox.addItem ("broken", 3);
        artifactRoleBox.setSelectedId (3); // default: broken
        addAndMakeVisible (artifactRoleBox);

        midiLabel.setText ("MIDI", juce::dontSendNotification);
        midiLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (midiLabel);
        midiBox.setTooltip ("MIDI input from Audio MIDI Setup");
        addAndMakeVisible (midiBox);
        midiBox.addListener (this);
        addAndMakeVisible (midiLed);

        hostClockToggle.setButtonText ("Host Clock");
        hostClockToggle.setToggleState (false, juce::dontSendNotification);
        hostClockToggle.addListener (this);
        addAndMakeVisible (hostClockToggle);

        bpmEditor.setText ("120", juce::dontSendNotification);
        bpmEditor.setInputRestrictions (3, "0123456789");
        bpmEditor.setJustification (juce::Justification::centred);
        bpmEditor.addListener (this);
        addAndMakeVisible (bpmEditor);

        bpmLabel.setText ("BPM", juce::dontSendNotification);
        addAndMakeVisible (bpmLabel);

        clickToggle.setToggleState (false, juce::dontSendNotification);
        clickToggle.addListener (this);
        addAndMakeVisible (clickToggle);

        addAndMakeVisible (editorViewport);
        editorViewport.setViewedComponent (&editorPlaceholder, false);

        populatePluginBox();
        populatePresets();
        populateFixtures();
        populateMidiInputs();

        {
            juce::String clickError;
            if (! engine.loadMetronomeClick (config.fixturesDir.getChildFile ("impulse.wav"), clickError))
                HostLog::error (clickError);
        }

        loadPluginWithoutEditor();
        startTimerHz (30);
    }

    ~MainContent() override
    {
        engine.stopFixture();
        engine.stopAudioDevice();
        destroyPluginEditor();

        if (keyListenerOwner != nullptr)
            keyListenerOwner->removeKeyListener (this);
    }

    /** Call after the host window is on-screen so AU Cocoa UIs can attach to an NSWindow. */
    void showPluginEditor()
    {
        recreatePluginEditor();

        if (loadDefaultOrFirstPreset())
            return;

        if (! fixtureFiles.isEmpty())
        {
            selectFixture (0);
            setStatus ("Ready — " + currentPlugin().displayLabel());
        }
        else
        {
            setStatus ("Ready — " + currentPlugin().displayLabel());
        }
    }

    /** Prefer config default_preset when present; otherwise the first preset in the list. */
    bool loadDefaultOrFirstPreset()
    {
        const auto& defaultPreset = currentPlugin().defaultPreset;
        if (defaultPreset.existsAsFile())
        {
            selectPresetInDropdown (defaultPreset);
            // If the file isn't in the scanned presets folder, select by loading directly.
            if (getSelectedPresetFile() != defaultPreset
                && getSelectedPresetFile().getFullPathName() != defaultPreset.getFullPathName())
            {
                juce::String error;
                if (! engine.loadPreset (defaultPreset, error))
                {
                    setStatus ("Default preset error: " + error, true);
                    return false;
                }

                if (pluginEditor != nullptr)
                    pluginEditor->repaint();

                savePresetNameEditor.setText (defaultPreset.getFileNameWithoutExtension(),
                                              juce::dontSendNotification);
                setStatus ("Loaded default preset: " + defaultPreset.getFileNameWithoutExtension());
                return true;
            }

            loadSelectedPreset();
            return true;
        }

        if (! presetFiles.isEmpty())
        {
            presetBox.setSelectedItemIndex (0, juce::dontSendNotification);
            loadSelectedPreset();
            return true;
        }

        return false;
    }

    void parentHierarchyChanged() override
    {
        if (keyListenerOwner != nullptr)
        {
            keyListenerOwner->removeKeyListener (this);
            keyListenerOwner = nullptr;
        }

        if (auto* top = getTopLevelComponent())
        {
            top->addKeyListener (this);
            keyListenerOwner = top;
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

        // Faint divider under the control strip
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (controlStripDivider);
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.fillRect (controlStripDivider.translated (0, 1));

        for (auto& sep : groupSeparators)
        {
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRect (sep);
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRect (sep.translated (1, 0));
        }
    }

    void resized() override
    {
        constexpr int ctrlH = 24;
        constexpr int rowH = 28;
        constexpr int gap = 6;
        constexpr int groupGap = 14;
        constexpr int leftLabelW = 72;
        constexpr int leftDropW = 240;
        constexpr int leftButtonW = 56;
        constexpr int leftColW = leftLabelW + gap + leftDropW + gap + leftButtonW;

        groupSeparators.clearQuick();

        auto bounds = getLocalBounds().reduced (12);

        auto place = [] (juce::Rectangle<int>& row, int w)
        {
            return row.removeFromLeft (w).withSizeKeepingCentre (w, ctrlH);
        };

        auto row0 = bounds.removeFromTop (rowH);
        bounds.removeFromTop (4);
        auto row1 = bounds.removeFromTop (rowH);
        bounds.removeFromTop (4);
        auto row2 = bounds.removeFromTop (rowH);

        // Right column: description + type on row0; Capture under them on row1;
        // host clock on row2.
        constexpr int nameFieldW = 150;
        constexpr int roleBoxW = 90;
        constexpr int captureW = nameFieldW + gap + roleBoxW;

        artifactRoleBox.setBounds (row0.removeFromRight (roleBoxW).withSizeKeepingCentre (roleBoxW, ctrlH));
        row0.removeFromRight (gap);
        snapshotNameEditor.setBounds (row0.removeFromRight (nameFieldW).withSizeKeepingCentre (nameFieldW, ctrlH));
        row0.removeFromRight (gap);
        descriptionLabel.setBounds (row0.removeFromRight (72));
        row0.removeFromRight (groupGap);

        captureButton.setBounds (row1.removeFromRight (captureW).withSizeKeepingCentre (captureW, ctrlH));
        row1.removeFromRight (groupGap);

        clickToggle.setBounds (row2.removeFromRight (26).withSizeKeepingCentre (26, 26));
        row2.removeFromRight (gap);
        bpmLabel.setBounds (row2.removeFromRight (30).withSizeKeepingCentre (30, ctrlH));
        row2.removeFromRight (gap);
        bpmEditor.setBounds (row2.removeFromRight (40).withSizeKeepingCentre (40, ctrlH));
        row2.removeFromRight (gap);
        hostClockToggle.setBounds (row2.removeFromRight (100).withSizeKeepingCentre (100, ctrlH));
        row2.removeFromRight (groupGap);

        // Left columns — shared field width + identical action buttons
        auto left0 = row0.removeFromLeft (leftColW);
        auto left1 = row1.removeFromLeft (leftColW);
        auto left2 = row2.removeFromLeft (leftColW);

        pluginLabel.setBounds (place (left0, leftLabelW));
        left0.removeFromLeft (gap);
        pluginBox.setBounds (place (left0, leftDropW));
        left0.removeFromLeft (gap);
        resetButton.setBounds (place (left0, leftButtonW));

        presetLabel.setBounds (place (left1, leftLabelW));
        left1.removeFromLeft (gap);
        presetBox.setBounds (place (left1, leftDropW));
        left1.removeFromLeft (gap);
        loadPresetButton.setBounds (place (left1, leftButtonW));

        savePresetNameLabel.setBounds (place (left2, leftLabelW));
        left2.removeFromLeft (gap);
        savePresetNameEditor.setBounds (place (left2, leftDropW));
        left2.removeFromLeft (gap);
        savePresetButton.setBounds (place (left2, leftButtonW));

        // Separator between left and middle
        groupSeparators.add (juce::Rectangle<float> ((float) (row0.getX() - groupGap / 2),
                                                     (float) row0.getY() + 3.0f,
                                                     1.0f,
                                                     (float) (row2.getBottom() - row0.getY() - 6)));

        row0.removeFromLeft (groupGap);
        row1.removeFromLeft (groupGap);
        row2.removeFromLeft (groupGap);

        // Middle width matches across status + MIDI (status display sets the width)
        const int midW = juce::jmax (180, juce::jmin (row0.getWidth(), row1.getWidth()));

        statusDisplay.setBounds (row0.removeFromLeft (midW).withSizeKeepingCentre (midW, ctrlH));

        {
            auto mid1 = row1.removeFromLeft (midW);
            midiLabel.setBounds (place (mid1, 40));
            mid1.removeFromLeft (gap);
            midiLed.setBounds (mid1.removeFromRight (16).withSizeKeepingCentre (16, 16));
            mid1.removeFromRight (gap);
            midiBox.setBounds (place (mid1, mid1.getWidth()));
        }

        {
            auto mid2 = row2.removeFromLeft (midW);
            fixtureLabel.setBounds (place (mid2, leftLabelW));
            mid2.removeFromLeft (gap);
            loopToggle.setBounds (mid2.removeFromRight (26).withSizeKeepingCentre (26, 26));
            mid2.removeFromRight (gap);
            transportButton.setBounds (mid2.removeFromRight (26).withSizeKeepingCentre (26, 26));
            mid2.removeFromRight (gap);
            fixtureBox.setBounds (place (mid2, mid2.getWidth()));
        }

        bounds.removeFromTop (8);
        controlStripDivider = juce::Rectangle<int> (bounds.getX(), bounds.getY(), bounds.getWidth(), 1);
        bounds.removeFromTop (4);

        editorViewport.setBounds (bounds);
        layoutEditor();
    }

private:
    static juce::String stripAupresetExtension (juce::String name)
    {
        if (name.endsWithIgnoreCase (".aupreset"))
            return name.dropLastCharacters (9);
        return name;
    }

    static juce::String presetDisplayPath (const juce::File& file, const juce::File& presetsDir)
    {
        return stripAupresetExtension (file.getRelativePathFrom (presetsDir));
    }

    void configureButton (juce::TextButton& button, const juce::String& text)
    {
        button.setButtonText (text);
        button.addListener (this);
        addAndMakeVisible (button);
    }

    void setStatus (const juce::String& text, bool isError = false)
    {
        statusDisplay.setMessage (text, isError);
        if (isError)
            HostLog::error (text);
    }

    const HostPluginEntry& currentPlugin() const
    {
        return config.plugins.getReference (currentPluginIndex);
    }

    void populatePluginBox()
    {
        pluginBox.clear (juce::dontSendNotification);

        for (int i = 0; i < config.plugins.size(); ++i)
        {
            const auto& entry = config.plugins.getReference (i);
            const int itemId = i + 1;
            pluginBox.addItem (entry.displayLabel(), itemId);
            pluginBox.setItemEnabled (itemId, entry.installed);
        }

        if (juce::isPositiveAndBelow (currentPluginIndex, config.plugins.size())
            && config.plugins.getReference (currentPluginIndex).installed)
        {
            pluginBox.setSelectedItemIndex (currentPluginIndex, juce::dontSendNotification);
        }
        else
        {
            pluginBox.setSelectedId (0, juce::dontSendNotification);
        }
    }

    void populatePresets()
    {
        presetBox.clear();
        presetFiles.clearQuick();

        auto presetsDir = currentPlugin().presetsDir;
        if (presetsDir != juce::File())
            presetsDir.createDirectory();

        if (! presetsDir.isDirectory())
        {
            presetBox.addItem ("(no presets folder)", 1);
            return;
        }

        presetFiles = collectFiles (presetsDir, ".aupreset", true);

        for (int i = 0; i < presetFiles.size(); ++i)
            presetBox.addItem (presetDisplayPath (presetFiles[i], presetsDir), i + 1);

        if (presetFiles.isEmpty())
            presetBox.addItem ("(no presets found)", 1);
    }

    void switchToPlugin (int pluginIndex)
    {
        if (! juce::isPositiveAndBelow (pluginIndex, config.plugins.size())
            || pluginIndex == currentPluginIndex)
            return;

        if (! config.plugins.getReference (pluginIndex).installed)
        {
            setStatus ("Plugin not installed: " + config.plugins.getReference (pluginIndex).displayLabel(), true);
            pluginBox.setSelectedItemIndex (currentPluginIndex, juce::dontSendNotification);
            return;
        }

        engine.stopFixture();
        destroyPluginEditor();

        currentPluginIndex = pluginIndex;
        const auto& plugin = currentPlugin();
        plugin.presetsDir.createDirectory();

        juce::String error;
        if (! engine.loadPlugin (plugin.path, error))
        {
            setStatus ("Failed to load plugin: " + error, true);
            return;
        }

        if (! engine.startAudioDevice (error))
        {
            setStatus ("Audio device error: " + error, true);
            return;
        }

        populatePresets();
        savePresetNameEditor.setText ("Untitled", juce::dontSendNotification);
        recreatePluginEditor();

        if (! loadDefaultOrFirstPreset())
        {
            setStatus ("Ready — " + plugin.displayLabel()
                                     + " (presets: " + plugin.presetsDir.getFullPathName() + ")");
        }
    }

    void selectPresetInDropdown (const juce::File& presetFile)
    {
        for (int i = 0; i < presetFiles.size(); ++i)
        {
            if (presetFiles[i] == presetFile
                || presetFiles[i].getFullPathName() == presetFile.getFullPathName())
            {
                presetBox.setSelectedItemIndex (i, juce::dontSendNotification);
                savePresetNameEditor.setText (presetFile.getFileNameWithoutExtension(),
                                              juce::dontSendNotification);
                return;
            }
        }
    }

    juce::File getSelectedPresetFile() const
    {
        // Trust the selected index. Avoid string equality checks that can reject
        // the first list entry (commonly the Logic "initial" presets).
        const int index = presetBox.getSelectedItemIndex();
        if (index >= 0 && index < presetFiles.size())
            return presetFiles[index];

        // Fallback: match the displayed text to a known preset path/name.
        const auto text = presetBox.getText().trim();
        if (text.isNotEmpty())
        {
            for (const auto& file : presetFiles)
            {
                if (presetDisplayPath (file, currentPlugin().presetsDir) == text
                    || file.getFileName() == text
                    || file.getFileNameWithoutExtension() == text)
                {
                    return file;
                }
            }
        }

        return {};
    }

    void destroyPluginEditor()
    {
        editorPlaceholder.removeAllChildren();
        engine.destroyEditor (pluginEditor);
    }

    void recreatePluginEditor()
    {
        if (engine.getPlugin() == nullptr)
            return;

        destroyPluginEditor();

        pluginEditor = engine.createEditor();
        if (pluginEditor != nullptr)
        {
            editorPlaceholder.addAndMakeVisible (*pluginEditor);
            layoutEditor();
            HostLog::info ("Opened editor for " + currentPlugin().displayLabel()
                           + " (" + juce::String (pluginEditor->getWidth()) + "x"
                           + juce::String (pluginEditor->getHeight()) + ")");
        }
        else
        {
            HostLog::error ("Plugin reported no editor for " + currentPlugin().displayLabel());
        }
    }

    void loadSelectedPreset()
    {
        const auto presetFile = getSelectedPresetFile();
        if (! presetFile.exists())
        {
            setStatus ("Select a preset to load", true);
            return;
        }

        juce::String error;
        if (! engine.loadPreset (presetFile, error))
        {
            setStatus ("Preset error: " + error, true);
            return;
        }

        // Keep the existing Cocoa UI. Destroying/recreating after setStateInformation
        // often falls back to AUGenericView (placard / property list / channel layout).
        if (pluginEditor != nullptr)
            pluginEditor->repaint();

        savePresetNameEditor.setText (presetFile.getFileNameWithoutExtension(),
                                      juce::dontSendNotification);
        setStatus ("Loaded preset: " + presetFile.getFileNameWithoutExtension());
    }

    juce::File presetFileForName (const juce::String& presetName) const
    {
        auto fileName = juce::File::createLegalFileName (presetName.trim());
        if (fileName.isEmpty())
            fileName = "Untitled";

        if (! fileName.endsWithIgnoreCase (".aupreset"))
            fileName << ".aupreset";

        return currentPlugin().presetsDir.getChildFile (fileName);
    }

    void commitPresetSave (const juce::File& dest, bool replacing)
    {
        juce::String error;
        if (! engine.saveCurrentPreset (dest, error))
        {
            setStatus ("Failed to save preset: " + error, true);
            return;
        }

        populatePresets();
        selectPresetInDropdown (dest);
        setStatus ((replacing ? "Replaced preset: " : "Saved preset: ")
                   + dest.getFileNameWithoutExtension());
    }

    void savePresetFromEditor()
    {
        if (engine.getPlugin() == nullptr)
        {
            setStatus ("No plugin loaded", true);
            return;
        }

        const auto presetName = savePresetNameEditor.getText().trim();
        if (presetName.isEmpty())
        {
            setStatus ("Enter a preset name before saving", true);
            return;
        }

        const auto dest = presetFileForName (presetName);
        if (dest.existsAsFile())
        {
            auto options = juce::MessageBoxOptions()
                               .withIconType (juce::MessageBoxIconType::QuestionIcon)
                               .withTitle ("Replace Preset")
                               .withMessage ("File " + dest.getFileName()
                                             + " already exists.\nReplace it?")
                               .withButton ("OK")
                               .withButton ("Cancel")
                               .withAssociatedComponent (this);

            replacePresetDialog = juce::AlertWindow::showScopedAsync (options,
                [safe = juce::Component::SafePointer<MainContent> (this), dest] (int result)
                {
                    if (safe == nullptr || result != 1)
                        return;
                    safe->commitPresetSave (dest, true);
                });
            return;
        }

        commitPresetSave (dest, false);
    }

    void populateFixtures()
    {
        fixtureFiles = collectFiles (config.fixturesDir, ".wav", false);
        fixtureBox.clear();

        for (int i = 0; i < fixtureFiles.size(); ++i)
            fixtureBox.addItem (fixtureFiles[i].getFileName(), i + 1);

        if (! fixtureFiles.isEmpty())
            fixtureBox.setSelectedId (1, juce::dontSendNotification);
    }

    void populateMidiInputs()
    {
        midiDevices = engine.getMidiInputDevices();
        midiBox.clear (juce::dontSendNotification);

        if (midiDevices.isEmpty())
        {
            midiBox.addItem ("No MIDI inputs", 1);
            midiBox.setSelectedId (1, juce::dontSendNotification);
            midiBox.setEnabled (false);
            engine.setMidiInputDevice ({});
            return;
        }

        midiBox.setEnabled (true);
        for (int i = 0; i < midiDevices.size(); ++i)
            midiBox.addItem (midiDevices.getReference (i).name, i + 1);

        int selectedIndex = 0;
        if (config.defaultMidiInput.isNotEmpty())
        {
            for (int i = 0; i < midiDevices.size(); ++i)
            {
                if (midiDevices.getReference (i).name.equalsIgnoreCase (config.defaultMidiInput))
                {
                    selectedIndex = i;
                    break;
                }
            }
        }

        midiBox.setSelectedItemIndex (selectedIndex, juce::dontSendNotification);
        engine.setMidiInputDevice (midiDevices.getReference (selectedIndex).identifier);
    }

    void timerCallback() override
    {
        if (engine.consumeMidiActivity())
            midiLedLitUntil = juce::Time::getMillisecondCounterHiRes() + 150.0;

        midiLed.setActive (juce::Time::getMillisecondCounterHiRes() < midiLedLitUntil);

        if (engine.consumeHostClockQuarterPulse())
            clockLedLitUntil = juce::Time::getMillisecondCounterHiRes() + 150.0;

        clickToggle.setLedActive (juce::Time::getMillisecondCounterHiRes() < clockLedLitUntil);

        // Keep the transport glyph in sync (one-shot clips stop themselves).
        transportButton.setPlaying (engine.isPlaying());
    }

    void applyBpmFromEditor()
    {
        const auto text = bpmEditor.getText().trim();
        const int bpm = text.isEmpty() ? 120 : text.getIntValue();
        const int clamped = juce::jlimit (20, 999, bpm <= 0 ? 120 : bpm);
        engine.setHostClockBpm ((double) clamped);

        if (clamped != bpm || text.isEmpty())
            bpmEditor.setText (juce::String (clamped), juce::dontSendNotification);
    }

    void loadPluginWithoutEditor()
    {
        if (! juce::isPositiveAndBelow (currentPluginIndex, config.plugins.size())
            || ! currentPlugin().installed)
        {
            setStatus ("No installed plugin selected — choose one from the list");
            return;
        }

        const auto& plugin = currentPlugin();
        plugin.presetsDir.createDirectory();

        juce::String error;
        if (! engine.loadPlugin (plugin.path, error))
        {
            setStatus ("Failed to load plugin: " + error, true);
            return;
        }

        if (! engine.startAudioDevice (error))
        {
            setStatus ("Audio device error: " + error, true);
            return;
        }

        populatePresets();
        setStatus ("Loaded " + plugin.displayLabel() + " — opening UI...");
    }

    void layoutEditor()
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

    void selectFixture (int index)
    {
        if (! juce::isPositiveAndBelow (index, fixtureFiles.size()))
            return;

        juce::String error;
        if (! engine.loadFixture (fixtureFiles[index], error))
            setStatus ("Fixture error: " + error, true);
    }

    void startPlayback()
    {
        const int index = fixtureBox.getSelectedId() - 1;
        selectFixture (index);
        engine.playFixture();
        transportButton.setPlaying (true);
        setStatus (engine.isLooping() ? "Looping clip..." : "Playing clip (one-shot)...");
    }

    void stopPlayback()
    {
        engine.stopFixture();
        transportButton.setPlaying (false);
        setStatus ("Stopped");
    }

    void togglePlayback()
    {
        if (engine.isPlaying())
            stopPlayback();
        else
            startPlayback();
    }

    void resetPluginToDefaults()
    {
        if (! juce::isPositiveAndBelow (currentPluginIndex, config.plugins.size())
            || ! currentPlugin().installed)
        {
            setStatus ("No installed plugin to reset", true);
            return;
        }

        engine.stopFixture();
        transportButton.setPlaying (false);
        destroyPluginEditor();

        const auto& plugin = currentPlugin();
        juce::String error;
        if (! engine.loadPlugin (plugin.path, error))
        {
            setStatus ("Failed to reset plugin: " + error, true);
            return;
        }

        if (! engine.startAudioDevice (error))
        {
            setStatus ("Audio device error: " + error, true);
            return;
        }

        // Fresh instance = plugin's own default state; clear any preset selection.
        savePresetNameEditor.setText ("Untitled", juce::dontSendNotification);
        recreatePluginEditor();
        setStatus ("Reset " + plugin.displayLabel() + " to defaults");
    }

    static bool isEditableFieldFocused()
    {
        auto* focused = juce::Component::getCurrentlyFocusedComponent();
        if (focused == nullptr)
            return false;

        if (dynamic_cast<juce::TextEditor*> (focused) != nullptr)
            return true;

        if (focused->findParentComponentOfClass<juce::TextEditor>() != nullptr)
            return true;

        // Covers plugin UIs that implement TextInputTarget without using TextEditor.
        if (dynamic_cast<juce::TextInputTarget*> (focused) != nullptr)
            return true;

        return false;
    }

    using juce::Component::keyPressed;

    bool keyPressed (const juce::KeyPress& key, juce::Component*) override
    {
        if (! key.isKeyCode (juce::KeyPress::spaceKey))
            return false;

        if (isEditableFieldFocused())
            return false;

        togglePlayback();
        return true;
    }

    void buttonClicked (juce::Button* button) override
    {
        if (button == &loadPresetButton)
        {
            loadSelectedPreset();
            return;
        }

        if (button == &savePresetButton)
        {
            savePresetFromEditor();
            return;
        }

        if (button == &resetButton)
        {
            resetPluginToDefaults();
            return;
        }

        if (button == &transportButton)
        {
            togglePlayback();
            return;
        }

        if (button == &loopToggle)
        {
            engine.setLooping (loopToggle.getToggleState());
            return;
        }

        if (button == &hostClockToggle)
        {
            applyBpmFromEditor();
            engine.setHostClockEnabled (hostClockToggle.getToggleState());
            return;
        }

        if (button == &clickToggle)
        {
            engine.setMetronomeClickEnabled (clickToggle.getToggleState());
            return;
        }

        if (button == &captureButton)
            captureTestCase();
    }

    void comboBoxChanged (juce::ComboBox* box) override
    {
        if (box == &pluginBox)
        {
            switchToPlugin (pluginBox.getSelectedItemIndex());
            return;
        }

        if (box == &fixtureBox)
        {
            selectFixture (fixtureBox.getSelectedId() - 1);
            return;
        }


        if (box == &presetBox)
        {
            const auto presetFile = getSelectedPresetFile();
            if (presetFile.exists())
            {
                savePresetNameEditor.setText (presetFile.getFileNameWithoutExtension(),
                                              juce::dontSendNotification);
            }
            return;
        }

        if (box == &midiBox)
        {
            const int index = midiBox.getSelectedItemIndex();
            if (juce::isPositiveAndBelow (index, midiDevices.size()))
                engine.setMidiInputDevice (midiDevices.getReference (index).identifier);
            else
                engine.setMidiInputDevice ({});
        }
    }

    void textEditorTextChanged (juce::TextEditor&) override {}

    void textEditorReturnKeyPressed (juce::TextEditor& editor) override
    {
        if (&editor == &bpmEditor)
            applyBpmFromEditor();
    }

    void textEditorFocusLost (juce::TextEditor& editor) override
    {
        if (&editor == &bpmEditor)
            applyBpmFromEditor();
    }

    void captureTestCase()
    {
        const int fixtureIndex = fixtureBox.getSelectedId() - 1;
        if (! juce::isPositiveAndBelow (fixtureIndex, fixtureFiles.size()))
        {
            setStatus ("Select a fixture WAV before capturing", true);
            return;
        }

        if (engine.getPlugin() == nullptr)
        {
            setStatus ("No plugin loaded", true);
            return;
        }

        const auto snapshotName = snapshotNameEditor.getText().trim();
        if (snapshotName.isEmpty())
        {
            setStatus ("Snapshot name is required", true);
            return;
        }

        const auto fixtureFile = fixtureFiles[fixtureIndex];
        const auto captureDir = config.sessionsRoot
                                    .getChildFile (slugify (currentPlugin().sessionName))
                                    .getChildFile ("artifacts");
        captureDir.createDirectory();

        const auto keyword = keywordFromDescription (snapshotName);
        const auto token = juce::Uuid().toString().substring (0, 8);
        const auto stem = keyword.isNotEmpty() ? keyword + "_" + token : token;
        // Always dump the live plugin state — never copy the selected library
        // .aupreset, which may be stale after UI tweaks.
        const auto presetOut = captureDir.getChildFile (stem + ".aupreset");
        const auto roleSuffix = artifactRoleCode();
        const auto outputOut = captureDir.getChildFile (stem + "_output_" + roleSuffix + ".wav");

        juce::String error;
        if (! engine.saveCurrentPreset (presetOut, error))
        {
            setStatus ("Failed to save preset: " + error, true);
            return;
        }

        engine.stopFixture();
        engine.stopAudioDevice();

        OfflineCaptureOptions renderOptions;
        if (! OfflineCapture::renderPluginToFile (*engine.getPlugin(), fixtureFile, outputOut, renderOptions, error))
        {
            engine.startAudioDevice (error);
            setStatus ("Offline capture failed: " + error, true);
            return;
        }

        engine.startAudioDevice (error);

        if (! registerSnapshotWithPython (snapshotName, fixtureFile, outputOut, presetOut, error))
        {
            setStatus ("Capture saved to disk but session update failed: " + error, true);
            return;
        }

        setStatus ("Captured test case: " + snapshotName);
    }

    juce::String artifactRoleCode() const
    {
        switch (artifactRoleBox.getSelectedId())
        {
            case 1:  return "gld";
            case 2:  return "sus";
            default: return "bkn";
        }
    }

    bool registerSnapshotWithPython (const juce::String& snapshotName,
                                     const juce::File& inputFile,
                                     const juce::File& outputFile,
                                     const juce::File& presetFile,
                                     juce::String& error)
    {
        juce::File cli = config.pythonCli;
        if (! cli.existsAsFile())
            cli = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                      .getParentDirectory()
                      .getChildFile ("aufx-test");

        if (! cli.existsAsFile())
        {
            error = "Could not find aufx-test CLI (set python_cli in host.config.json)";
            return false;
        }

        // --root must precede the snap subcommand (parent-parser option).
        juce::StringArray args;
        args.add (cli.getFullPathName());
        args.add ("session");
        args.add ("--root");
        args.add (config.sessionsRoot.getFullPathName());
        args.add ("snap");
        args.add (currentPlugin().sessionName);
        args.add (snapshotName);
        args.add ("--input");
        args.add (inputFile.getFullPathName());
        args.add ("--output");
        args.add (outputFile.getFullPathName());
        args.add ("--aupreset");
        args.add (presetFile.getFullPathName());
        args.add ("--notes");
        args.add ("Captured from plugin_host_app");

        juce::ChildProcess process;
        if (! process.start (args))
        {
            error = "Failed to start aufx-test CLI";
            return false;
        }

        if (! process.waitForProcessToFinish (120000))
        {
            error = "Timed out waiting for aufx-test session snap";
            return false;
        }

        const auto exitCode = process.getExitCode();
        if (exitCode != 0)
        {
            error = process.readAllProcessOutput().trim();
            if (error.isEmpty())
                error = "aufx-test session snap failed with exit code " + juce::String (exitCode);
            return false;
        }

        return true;
    }

    PluginAudioEngine& engine;
    HostConfig config;
    int currentPluginIndex { 0 };

    juce::Label pluginLabel;
    juce::ComboBox pluginBox;
    juce::TextButton resetButton;
    StatusDisplay statusDisplay;
    juce::Label presetLabel;
    juce::Label fixtureLabel;
    juce::ComboBox presetBox;
    juce::ComboBox fixtureBox;
    juce::TextButton loadPresetButton;
    juce::TextButton savePresetButton;
    juce::Label savePresetNameLabel;
    juce::TextEditor savePresetNameEditor;
    TransportButton transportButton;
    LoopToggleButton loopToggle;
    juce::TextButton captureButton;
    juce::Label descriptionLabel;
    juce::TextEditor snapshotNameEditor;
    juce::ComboBox artifactRoleBox;
    juce::Label midiLabel;
    juce::ComboBox midiBox;
    MidiActivityLed midiLed;
    juce::ToggleButton hostClockToggle;
    juce::TextEditor bpmEditor;
    juce::Label bpmLabel;
    ClickToggleButton clickToggle;
    juce::Array<juce::MidiDeviceInfo> midiDevices;
    double midiLedLitUntil { 0.0 };
    double clockLedLitUntil { 0.0 };
    juce::Array<juce::Rectangle<float>> groupSeparators;
    juce::Rectangle<int> controlStripDivider;
    juce::Viewport editorViewport;
    juce::Component editorPlaceholder;
    juce::AudioProcessorEditor* pluginEditor { nullptr };
    juce::Array<juce::File> presetFiles;
    juce::Array<juce::File> fixtureFiles;
    juce::Component* keyListenerOwner { nullptr };
    juce::ScopedMessageBox replacePresetDialog;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainContent)
};

MainWindow::MainWindow (HostConfig hostConfig)
    : DocumentWindow ("AU Effects Explorer",
                      juce::Desktop::getInstance().getDefaultLookAndFeel()
                          .findColour (juce::ResizableWindow::backgroundColourId),
                      DocumentWindow::allButtons),
      config (std::move (hostConfig))
{
    engine = std::make_unique<PluginAudioEngine>();
    content = std::make_unique<MainContent> (*engine, config);
    auto* mainContent = content.get();
    setContentOwned (content.release(), true);
    setResizable (true, true);
    centreWithSize (1100, 780);
    setVisible (true);

    // AU Cocoa editors need a real NSWindow parent; create after the host is shown.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainContent> (mainContent)]
                                     {
                                         if (safe != nullptr)
                                             safe->showPluginEditor();
                                     });
}

MainWindow::~MainWindow()
{
    // MainContent holds PluginAudioEngine&; DocumentWindow owns content and
    // would destroy it after our members. Clear it first so ~MainContent
    // still sees a live engine (otherwise quit crashes in destroyEditor).
    clearContentComponent();
    engine.reset();
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}
