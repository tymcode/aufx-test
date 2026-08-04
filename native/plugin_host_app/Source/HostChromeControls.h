#pragma once

#include <JuceHeader.h>
#include "HostConfig.h"
#include "Utf8.h"

inline void paintComboChevron (juce::Graphics& g, juce::Rectangle<int> bounds, juce::Colour arrowColour)
{
    const float cx = (float) bounds.getRight() - 11.0f;
    const float cy = (float) bounds.getCentreY();
    juce::Path chevron;
    chevron.startNewSubPath (cx - 3.5f, cy - 2.0f);
    chevron.lineTo (cx, cy + 2.0f);
    chevron.lineTo (cx + 3.5f, cy - 2.0f);
    g.setColour (arrowColour);
    g.strokePath (chevron, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
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

/** Combined begin/stop control for the source clip. */
class TransportButton : public juce::Button
{
public:
    TransportButton() : juce::Button ("Transport")
    {
        setTooltip ("Begin / stop the source clip (Space)");
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
        auto area = getLocalBounds().toFloat().reduced (0.5f);
        constexpr float corner = 3.0f;

        const auto face = playing ? juce::Colour (0xff5a2a1a)
                                  : juce::Colour (0xffff6a2a);
        const auto facePaint = down ? face.darker (0.12f)
                                    : (highlighted ? face.brighter (0.08f) : face);

        g.setColour (facePaint);
        g.fillRoundedRectangle (area, corner);
        g.setColour (playing ? juce::Colour (0xffff8a5c).withAlpha (0.7f)
                             : juce::Colours::white.withAlpha (0.25f));
        g.drawRoundedRectangle (area, corner, 1.0f);

        g.setColour (playing ? juce::Colour (0xffffc8a8) : juce::Colours::black.withAlpha (0.9f));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (playing ? "Stop" : "Begin",
                    getLocalBounds(),
                    juce::Justification::centred,
                    false);
    }

private:
    bool playing { false };
};

/** Plugin dropdown with a remove (x) control on each menu row. */
class PluginPickerField : public juce::Component,
                          public juce::SettableTooltipClient
{
public:
    std::function<void(int)> onSelect;
    std::function<void(int)> onRemove;
    std::function<void()> onMorePlugins;

    void setPlugins (const juce::Array<HostPluginEntry>& plugins, int selectedIndex)
    {
        entries = plugins;
        currentIndex = selectedIndex;
        updateDisplayText();
        repaint();
    }

    /** When non-empty, paint this text instead of the selected plugin label. */
    void setForcedDisplayText (const juce::String& text)
    {
        forcedText = text;
        updateDisplayText();
        repaint();
    }

    void clearForcedDisplayText()
    {
        forcedText = {};
        updateDisplayText();
        repaint();
    }

    int getSelectedIndex() const { return currentIndex; }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat().reduced (0.5f);
        constexpr float corner = 3.0f;
        const bool enabled = isEnabled();

        g.setColour (findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (area, corner);
        g.setColour (findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (area, corner, 1.0f);

        paintComboChevron (g, getLocalBounds(),
                           findColour (juce::ComboBox::arrowColourId).withAlpha (enabled ? 0.9f : 0.3f));

        g.setColour (findColour (juce::ComboBox::textColourId).withAlpha (enabled ? 1.0f : 0.4f));
        g.setFont (juce::FontOptions (12.5f));
        g.drawText (displayText,
                    getLocalBounds().reduced (8, 0).withTrimmedRight (18),
                    juce::Justification::centredLeft,
                    true);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (isEnabled())
            showMenu();
    }

private:
    void updateDisplayText()
    {
        if (forcedText.isNotEmpty())
        {
            displayText = forcedText;
            return;
        }
        if (entries.isEmpty())
            displayText = utf8 ("(no plugins — choose More plugins…)");
        else if (juce::isPositiveAndBelow (currentIndex, entries.size()))
            displayText = entries.getReference (currentIndex).displayLabel();
        else
            displayText = "(select a plugin)";
    }

    void showMenu()
    {
        struct MenuList final : public juce::Component,
                                private juce::ListBoxModel
        {
            MenuList (PluginPickerField& ownerIn, const juce::Array<HostPluginEntry>& items)
                : owner (ownerIn), entries (items)
            {
                list.setModel (this);
                list.setRowHeight (26);
                addAndMakeVisible (list);
                const int rows = items.size() + 1; // trailing "More plugins…"
                setSize (340, juce::jlimit (80, 360, rows * 26 + 8));
            }

            void resized() override { list.setBounds (getLocalBounds().reduced (4)); }

            int getNumRows() override { return entries.size() + 1; }

            bool isMorePluginsRow (int row) const { return row == entries.size(); }

            void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
            {
                if (isMorePluginsRow (row))
                {
                    if (selected)
                        g.fillAll (juce::Colours::dodgerblue.withAlpha (0.3f));

                    if (! entries.isEmpty())
                    {
                        g.setColour (juce::Colours::white.withAlpha (0.18f));
                        g.fillRect (8, 0, width - 16, 1);
                    }

                    g.setColour (juce::Colours::white.withAlpha (0.92f));
                    g.setFont (juce::Font (juce::FontOptions (13.0f)).boldened());
                    g.drawText (utf8 ("More plugins…"),
                                8, 0, width - 16, height,
                                juce::Justification::centredLeft, true);
                    return;
                }

                if (! juce::isPositiveAndBelow (row, entries.size()))
                    return;

                if (selected || row == owner.currentIndex)
                    g.fillAll (juce::Colours::dodgerblue.withAlpha (0.3f));

                const auto& entry = entries.getReference (row);
                const bool enabled = entry.installed;
                g.setColour (juce::Colours::white.withAlpha (enabled ? 0.92f : 0.4f));
                g.setFont (13.0f);
                g.drawText (entry.displayLabel(),
                            8, 0, width - 34, height,
                            juce::Justification::centredLeft, true);

                auto xArea = juce::Rectangle<int> (width - 28, 0, 24, height).reduced (4);
                g.setColour (juce::Colours::white.withAlpha (0.55f));
                g.setFont (juce::FontOptions (15.0f));
                g.drawText ("x", xArea, juce::Justification::centred, false);
            }

            void listBoxItemClicked (int row, const juce::MouseEvent& e) override
            {
                if (isMorePluginsRow (row))
                {
                    if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
                        box->dismiss();
                    if (owner.onMorePlugins)
                        juce::MessageManager::callAsync (owner.onMorePlugins);
                    return;
                }

                if (! juce::isPositiveAndBelow (row, entries.size()))
                    return;

                const int width = list.getWidth();
                if (e.x >= width - 28)
                {
                    if (owner.onRemove)
                        owner.onRemove (row);
                    if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
                        box->dismiss();
                    return;
                }

                if (! entries.getReference (row).installed)
                    return;

                owner.currentIndex = row;
                owner.updateDisplayText();
                owner.repaint();
                if (owner.onSelect)
                    owner.onSelect (row);
                if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
                    box->dismiss();
            }

            PluginPickerField& owner;
            juce::Array<HostPluginEntry> entries;
            juce::ListBox list { "pluginPicker", this };
        };

        auto content = std::make_unique<MenuList> (*this, entries);
        juce::CallOutBox::launchAsynchronously (std::move (content), getScreenBounds(), nullptr);
    }

    juce::Array<HostPluginEntry> entries;
    int currentIndex { -1 };
    juce::String displayText;
    juce::String forcedText;
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

/** Horizontal slider matching the host chrome (dark recess + orange fill). */
class HostChromeSlider : public juce::Slider
{
public:
    HostChromeSlider()
    {
        setSliderStyle (juce::Slider::LinearHorizontal);
        setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    }

    void paint (juce::Graphics& g) override
    {
        const float proportion = (float) valueToProportionOfLength (getValue());
        const auto bounds = getLocalBounds();
        const auto fillBounds = paintChromeTrack (g, bounds, proportion);
        paintInvertedValueText (g, bounds, fillBounds, formatValueText());
    }

protected:
    virtual juce::String formatValueText() const { return {}; }

    static juce::Rectangle<float> paintChromeTrack (juce::Graphics& g, juce::Rectangle<int> area, float proportion)
    {
        auto bounds = area.toFloat().reduced (0.5f);
        constexpr float corner = 3.0f;

        g.setColour (juce::Colour (0xff101510));
        g.fillRoundedRectangle (bounds, corner);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.drawRoundedRectangle (bounds, corner, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 0.5f);

        auto fill = bounds.reduced (2.0f);
        fill.setWidth (fill.getWidth() * proportion);
        const auto fillForText = fill;

        if (fill.getWidth() > 2.0f)
        {
            g.setColour (juce::Colour (0xffff6a2a).withAlpha (0.85f));
            g.fillRoundedRectangle (fill, 2.0f);
            g.setColour (juce::Colour (0xffff8a5c).withAlpha (0.35f));
            g.fillRoundedRectangle (fill.removeFromTop (fill.getHeight() * 0.45f), 2.0f);
        }

        const float thumbW = 5.0f;
        const float travel = juce::jmax (0.0f, bounds.getWidth() - 4.0f - thumbW);
        const float thumbX = bounds.getX() + 2.0f + travel * proportion;
        auto thumb = juce::Rectangle<float> (thumbX, bounds.getY() + 3.0f, thumbW, bounds.getHeight() - 6.0f);

        g.setColour (juce::Colour (0xffffc8a8));
        g.fillRoundedRectangle (thumb, 1.5f);
        g.setColour (juce::Colour (0xffff6a2a));
        g.drawRoundedRectangle (thumb, 1.5f, 0.8f);

        return fillForText;
    }

    void paintInvertedValueText (juce::Graphics& g,
                                 juce::Rectangle<int> area,
                                 juce::Rectangle<float> fill,
                                 const juce::String& text) const
    {
        if (text.isEmpty())
            return;

        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        const auto lightText = findColour (juce::Label::textColourId);
        const auto inner = area.reduced (2);
        const auto fillI = fill.toNearestInt().getIntersection (inner);

        if (! fillI.isEmpty())
        {
            g.saveState();
            g.reduceClipRegion (fillI);
            g.setColour (juce::Colours::black);
            g.drawText (text, area, juce::Justification::centred, false);
            g.restoreState();
        }

        if (fillI.getX() > inner.getX())
        {
            g.saveState();
            g.reduceClipRegion (inner.getX(), inner.getY(),
                                fillI.getX() - inner.getX(), inner.getHeight());
            g.setColour (lightText);
            g.drawText (text, area, juce::Justification::centred, false);
            g.restoreState();
        }

        if (fillI.getRight() < inner.getRight())
        {
            g.saveState();
            g.reduceClipRegion (fillI.getRight(), inner.getY(),
                                inner.getRight() - fillI.getRight(), inner.getHeight());
            g.setColour (lightText);
            g.drawText (text, area, juce::Justification::centred, false);
            g.restoreState();
        }
    }
};

class MixSlider : public HostChromeSlider
{
public:
    MixSlider()
    {
        setRange (0.0, 100.0, 1.0);
        setValue (100.0, juce::dontSendNotification);
        setTooltip (utf8 ("Dry/wet mix — 0% is source only, 100% is fully processed"));
    }

protected:
    juce::String formatValueText() const override
    {
        return formatMixPercent (juce::roundToInt (getValue()));
    }
};

/** Send level: mute .. 0 dB (log taper) .. +6 dB (0 dB 60 px from the right edge). */
class SendSlider : public HostChromeSlider
{
public:
    static constexpr double muteDb = -120.0;
    static constexpr double maxPositiveDb = 6.0;
    static constexpr double positiveTravelPx = 60.0;
    static constexpr double positiveExp = 2.8;
    static constexpr double negativeSkew = 0.16;

    SendSlider()
    {
        setRange (muteDb, maxPositiveDb, 0.01);
        setValue (0.0, juce::dontSendNotification);
        setTooltip (utf8 ("Send level — mute at minimum, 0 dB unity, +6 dB max"));
    }

protected:
    juce::String formatValueText() const override
    {
        return formatSendLevelDb (getValue(), muteDb);
    }

    double valueToProportionOfLength (double value) override
    {
        const auto W = travelWidthPx();
        const auto negTravel = juce::jmax (1.0, W - positiveTravelPx);

        if (value >= 0.0)
        {
            const auto p = std::pow (juce::jlimit (0.0, 1.0, value / maxPositiveDb),
                                     1.0 / positiveExp);
            const auto x = negTravel + p * positiveTravelPx;
            return juce::jlimit (0.0, 1.0, x / W);
        }

        if (value <= muteDb + 0.05)
            return 0.0;

        const auto normalized = (value - muteDb) / (0.0 - muteDb);
        const auto t = std::pow (juce::jlimit (0.0, 1.0, normalized), 1.0 / negativeSkew);
        return juce::jlimit (0.0, 1.0, (t * negTravel) / W);
    }

    double proportionOfLengthToValue (double proportion) override
    {
        const auto W = travelWidthPx();
        const auto negTravel = juce::jmax (1.0, W - positiveTravelPx);
        const auto x = juce::jlimit (0.0, W, proportion * W);

        if (x >= negTravel - 0.5)
        {
            const auto p = juce::jlimit (0.0, 1.0, (x - negTravel) / positiveTravelPx);
            const auto db = maxPositiveDb * std::pow (p, positiveExp);
            return juce::jlimit (0.0, maxPositiveDb, db);
        }

        if (x <= 0.5)
            return muteDb;

        const auto t = x / negTravel;
        const auto normalized = std::pow (t, negativeSkew);
        return muteDb + normalized * (0.0 - muteDb);
    }

private:
    double travelWidthPx() const
    {
        return juce::jmax (positiveTravelPx + 1.0, (double) getLocalBounds().reduced (2).getWidth());
    }
};

/** Combo-styled multi-select for MIDI inputs; closed label lists selected names shortest-first. */
class MidiSourceField : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    std::function<void (const juce::StringArray&)> onChange;

    void setDevices (const juce::Array<juce::MidiDeviceInfo>& devices,
                     const juce::StringArray& initiallySelectedIds)
    {
        midiDevices = devices;
        selectedIds.clear();

        for (const auto& id : initiallySelectedIds)
            if (id.isNotEmpty())
                selectedIds.addIfNotAlreadyThere (id);

        setEnabled (! midiDevices.isEmpty());
        updateDisplayText();
        repaint();
    }

    juce::StringArray getSelectedIdentifiers() const { return selectedIds; }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat().reduced (0.5f);
        constexpr float corner = 3.0f;
        const bool enabled = isEnabled();

        g.setColour (findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (area, corner);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (area.getX() + 1.0f, area.getY() + 1.0f,
                                area.getWidth() - 2.0f, 1.5f, corner - 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRoundedRectangle (area.getX() + 1.0f, area.getBottom() - 1.5f,
                                area.getWidth() - 2.0f, 1.0f, corner - 1.0f);
        g.setColour (findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (area, corner, 1.0f);

        paintComboChevron (g, getLocalBounds(),
                           findColour (juce::ComboBox::arrowColourId).withAlpha (enabled ? 0.9f : 0.3f));

        g.setColour (findColour (juce::ComboBox::textColourId).withAlpha (enabled ? 1.0f : 0.4f));
        g.setFont (juce::FontOptions (12.5f));
        g.drawText (displayText,
                    getLocalBounds().reduced (8, 0).withTrimmedRight (18),
                    juce::Justification::centredLeft,
                    true);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (isEnabled() && ! midiDevices.isEmpty())
            showChecklist();
    }

private:
    void updateDisplayText()
    {
        if (midiDevices.isEmpty())
        {
            displayText = "No MIDI inputs";
            return;
        }

        juce::StringArray names;
        for (const auto& device : midiDevices)
            if (selectedIds.contains (device.identifier))
                names.add (device.name);

        if (names.isEmpty())
        {
            displayText = "(none)";
            return;
        }

        for (int i = 0; i < names.size() - 1; ++i)
            for (int j = i + 1; j < names.size(); ++j)
                if (names[j].length() < names[i].length()
                    || (names[j].length() == names[i].length()
                        && names[j].compareIgnoreCase (names[i]) < 0))
                {
                    const auto tmp = names[i];
                    names.set (i, names[j]);
                    names.set (j, tmp);
                }

        displayText = names.joinIntoString (", ");
    }

    void setDeviceSelected (const juce::String& identifier, bool shouldBeSelected)
    {
        if (shouldBeSelected)
            selectedIds.addIfNotAlreadyThere (identifier);
        else
            selectedIds.removeString (identifier);

        updateDisplayText();
        repaint();

        if (onChange)
            onChange (selectedIds);
    }

    void showChecklist()
    {
        struct Checklist final : public juce::Component,
                                 private juce::Button::Listener
        {
            Checklist (MidiSourceField& ownerIn)
                : owner (ownerIn)
            {
                constexpr int rowH = 24;
                int y = 4;

                for (int i = 0; i < owner.midiDevices.size(); ++i)
                {
                    const auto& device = owner.midiDevices.getReference (i);
                    auto* toggle = toggles.add (new juce::ToggleButton (device.name));
                    toggle->setToggleState (owner.selectedIds.contains (device.identifier),
                                           juce::dontSendNotification);
                    toggle->addListener (this);
                    toggle->setBounds (6, y, 260, rowH);
                    addAndMakeVisible (toggle);
                    identifiers.add (device.identifier);
                    y += rowH;
                }

                setSize (272, y + 4);
            }

            void buttonClicked (juce::Button* button) override
            {
                for (int i = 0; i < toggles.size(); ++i)
                {
                    if (button == toggles.getUnchecked (i))
                    {
                        owner.setDeviceSelected (identifiers.getReference (i),
                                                 toggles.getUnchecked (i)->getToggleState());
                        break;
                    }
                }
            }

            MidiSourceField& owner;
            juce::OwnedArray<juce::ToggleButton> toggles;
            juce::StringArray identifiers;
        };

        juce::CallOutBox::launchAsynchronously (std::make_unique<Checklist> (*this),
                                                localAreaToGlobal (getLocalBounds()),
                                                nullptr);
    }

    juce::Array<juce::MidiDeviceInfo> midiDevices;
    juce::StringArray selectedIds;
    juce::String displayText { "(none)" };
};
