#include "MainWindow.h"
#include "AboutDialog.h"
#include "AddPluginDialog.h"
#include "AuPluginScanner.h"
#include "HardwareAudioSetupDialog.h"
#include "HardwareVuMeters.h"
#include "HostLog.h"
#include "HostPreferences.h"
#include "MidiEndpointInfo.h"
#include "MidiSetupDialog.h"
#include "OfflineCapture.h"
#include "SessionSnap.h"
#include "SettingsDialog.h"
#include "Utf8.h"
#include "sysex/SysexDeviceRegistry.h"

#if JUCE_MAC
 #include "LightsOutManager_mac.h"
#endif

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

    juce::String keywordFromDescription (const juce::String& description)
    {
        const auto slug = HostConfig::slugify (description);
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

        const float cx = (float) getWidth() - 11.0f;
        const float cy = (float) getHeight() * 0.5f;
        juce::Path chevron;
        chevron.startNewSubPath (cx - 3.5f, cy - 2.0f);
        chevron.lineTo (cx, cy + 2.0f);
        chevron.lineTo (cx + 3.5f, cy - 2.0f);
        g.setColour (findColour (juce::ComboBox::arrowColourId).withAlpha (enabled ? 0.9f : 0.3f));
        g.strokePath (chevron, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));

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

        const float cx = (float) getWidth() - 11.0f;
        const float cy = (float) getHeight() * 0.5f;
        juce::Path chevron;
        chevron.startNewSubPath (cx - 3.5f, cy - 2.0f);
        chevron.lineTo (cx, cy + 2.0f);
        chevron.lineTo (cx + 3.5f, cy - 2.0f);
        g.setColour (findColour (juce::ComboBox::arrowColourId).withAlpha (enabled ? 0.9f : 0.3f));
        g.strokePath (chevron, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));

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

class MainWindow::MainContent : public juce::Component,
                                public juce::FileDragAndDropTarget,
                                private juce::Button::Listener,
                                private juce::ComboBox::Listener,
                                private juce::TextEditor::Listener,
                                private juce::KeyListener,
                                private juce::Timer
{
public:
    MainContent (PluginAudioEngine& audioEngine, HostConfig& hostConfig, juce::KnownPluginList& knownList)
        : engine (audioEngine), config (hostConfig), knownPlugins (knownList)
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

        pluginField.setTooltip (utf8 ("Configured plugins — use More plugins… to add more"));
        pluginField.onSelect = [this] (int index) { switchToPlugin (index); };
        pluginField.onRemove = [this] (int index) { confirmRemovePlugin (index); };
        pluginField.onMorePlugins = [this] { openAddPlugin(); };
        addAndMakeVisible (pluginField);

        setStatus (config.plugins.isEmpty() ? "Add a plugin from the Plugins menu"
                                            : "Loading plugin...");
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

        transportButton.addListener (this);
        addAndMakeVisible (transportButton);

        bypassButton.setButtonText ("Bypass");
        bypassButton.setClickingTogglesState (true);
        bypassButton.setTooltip ("Pass the source clip through unprocessed (A/B the plugin)");
        bypassButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffc47a1e));
        bypassButton.addListener (this);
        addAndMakeVisible (bypassButton);

        loopToggle.setToggleState (true, juce::dontSendNotification);
        loopToggle.addListener (this);
        addAndMakeVisible (loopToggle);
        engine.setLooping (true);

        sendLabel.setText ("Send", juce::dontSendNotification);
        sendLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (sendLabel);
        addAndMakeVisible (sendSlider);
        sendSlider.onValueChange = [this]
        {
            engine.setSendLevelDb ((float) sendSlider.getValue());
        };
        engine.setSendLevelDb (0.0f);

        mixLabel.setText ("Mix", juce::dontSendNotification);
        mixLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (mixLabel);
        addAndMakeVisible (mixSlider);
        mixSlider.onValueChange = [this]
        {
            engine.setMixAmount ((float) mixSlider.getValue() / 100.0f);
        };
        engine.setMixAmount (1.0f);
        engine.setAllowInstrumentAudioInput (HostPreferences::get().getAllowInstrumentAudioInput());

        savePresetNameLabel.setText ("Save as", juce::dontSendNotification);
        savePresetNameLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (savePresetNameLabel);
        savePresetNameEditor.setText ("Untitled", juce::dontSendNotification);
        savePresetNameEditor.setInputRestrictions (64);
        savePresetNameEditor.setJustification (juce::Justification::centredLeft);
        addAndMakeVisible (savePresetNameEditor);

        midiLabel.setText ("MIDI Sources", juce::dontSendNotification);
        midiLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (midiLabel);
        midiField.setTooltip (utf8 ("MIDI inputs from Audio MIDI Setup — check one or more to merge"));
        midiField.onChange = [this] (const juce::StringArray& identifiers)
        {
            engine.setMidiInputDevices (identifiers);
        };
        addAndMakeVisible (midiField);
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

        hardwareMeterPanel = std::make_unique<HardwareLoopMeterPanel> (engine);
        addChildComponent (*hardwareMeterPanel);

        populatePluginBox();
        populatePresets();
        populateFixtures();
        populateMidiInputs();

        {
            juce::String clickError;
            if (! engine.loadMetronomeClick (config.fixturesDir.getChildFile ("impulse.wav"), clickError))
                HostLog::error (clickError);
        }

        {
            const auto hw = HostPreferences::get().getHardwareLoopSettings();
            engine.setHardwareLoopSettings (hw);

            juce::String midiError;
            engine.setMidiOutputDevice (HostPreferences::get().getMidiOutIdentifier(), midiError);
            if (midiError.isNotEmpty())
                HostLog::error (midiError);

            const auto dumpIn = HostPreferences::get().getMidiDumpInIdentifier();
            if (dumpIn.isNotEmpty())
            {
                auto ids = engine.getSelectedMidiInputIdentifiers();
                if (! ids.contains (dumpIn))
                    ids.add (dumpIn);
                engine.setMidiInputDevices (ids);
            }
        }

        loadPluginWithoutEditor();
        startTimerHz (30);
        refreshHardwareModeUi();
    }

    ~MainContent() override
    {
        hardwareMeterPanel.reset();
        engine.stopFixture();
        engine.stopAudioDevice();
        destroyPluginEditor();

        if (keyListenerOwner != nullptr)
            keyListenerOwner->removeKeyListener (this);
    }

    void openAbout()
    {
        showAboutDialog (this);
    }

    void openHardwareAudioSetup()
    {
        if (showHardwareAudioSetupDialog (engine, config.fixturesDir, this))
            refreshHardwareModeUi();
    }

    void openMidiSetup()
    {
        showMidiSetupDialog (engine, this);
    }

    void refreshHardwareModeUi()
    {
        const bool hw = engine.isHardwareMode();

        if (hw)
        {
            presetLabel.setText ("HW State", juce::dontSendNotification);
            loadPresetButton.setButtonText ("Send");
            populateHardwareStates();
            showHardwareMeters();
        }
        else
        {
            presetLabel.setText ("Preset", juce::dontSendNotification);
            loadPresetButton.setButtonText ("Load");
            populatePresets();
            showPluginEditorArea();
        }

        savePresetButton.setEnabled (! hw);
        savePresetNameEditor.setEnabled (! hw);
        savePresetNameLabel.setEnabled (! hw);
        bypassButton.setEnabled (! hw);
    }

    void showHardwareMeters()
    {
        // Tear down the editor view only — plugin instance and its state stay loaded.
        destroyPluginEditor();
        editorViewport.setVisible (false);

        if (hardwareMeterPanel != nullptr)
        {
            hardwareMeterPanel->setVisible (true);
            hardwareMeterPanel->toFront (false);
            resized();
        }
    }

    void showPluginEditorArea()
    {
        if (hardwareMeterPanel != nullptr)
            hardwareMeterPanel->setVisible (false);

        editorViewport.setVisible (true);

        if (engine.getPlugin() != nullptr && pluginEditor == nullptr)
            recreatePluginEditor();
        else
            layoutEditor();
    }

    void populateHardwareStates()
    {
        presetBox.clear (juce::dontSendNotification);
        hardwareStateFiles.clear();

        if (config.plugins.isEmpty())
            return;

        const auto sessionDir = config.sessionsRoot.getChildFile (HostConfig::slugify (currentPlugin().sessionName));
        const auto artifactsDir = sessionDir.getChildFile ("artifacts");
        if (artifactsDir.isDirectory())
        {
            for (const auto& file : artifactsDir.findChildFiles (juce::File::findFiles, false, "*.syx"))
                hardwareStateFiles.add (file);
        }

        // Also scan sibling session artifact folders for convenience.
        if (config.sessionsRoot.isDirectory())
        {
            for (const auto& session : config.sessionsRoot.findChildFiles (juce::File::findDirectories, false))
            {
                const auto art = session.getChildFile ("artifacts");
                if (! art.isDirectory())
                    continue;
                for (const auto& file : art.findChildFiles (juce::File::findFiles, false, "*.syx"))
                    if (! hardwareStateFiles.contains (file))
                        hardwareStateFiles.add (file);
            }
        }

        struct FileComparator
        {
            static int compareElements (const juce::File& a, const juce::File& b)
            {
                return a.getFileName().compareNatural (b.getFileName());
            }
        } comparator;
        hardwareStateFiles.sort (comparator);

        for (int i = 0; i < hardwareStateFiles.size(); ++i)
            presetBox.addItem (hardwareStateFiles[i].getFileNameWithoutExtension(), i + 1);

        if (presetBox.getNumItems() > 0)
            presetBox.setSelectedItemIndex (0, juce::dontSendNotification);
    }

    void sendSelectedHardwareState()
    {
        const int index = presetBox.getSelectedId() - 1;
        if (! juce::isPositiveAndBelow (index, hardwareStateFiles.size()))
        {
            setStatus ("No hardware state selected", true);
            return;
        }

        const auto file = hardwareStateFiles[index];
        juce::MemoryBlock data;
        if (! file.loadFileAsData (data) || data.getSize() < 4)
        {
            setStatus ("Failed to read " + file.getFileName(), true);
            return;
        }

        const auto* bytes = static_cast<const uint8_t*> (data.getData());
        int offset = 0;
        int length = (int) data.getSize();
        if (bytes[0] == 0xf0)
        {
            offset = 1;
            length -= 1;
            if (length > 0 && bytes[data.getSize() - 1] == 0xf7)
                --length;
        }

        const auto message = juce::MidiMessage::createSysExMessage (bytes + offset, length);

        const auto outId = HostPreferences::get().getMidiOutIdentifier();
        const auto info = findMidiEndpointInfo (outId, true);
        const auto* module = SysexDeviceRegistry::get().findModule (info.manufacturer, info.model, info.name);

        juce::Array<juce::MidiMessage> messages;
        if (module != nullptr)
            messages = module->restoreDump (message);
        else
            messages.add (message);

        if (! engine.sendMidiMessages (messages))
        {
            setStatus ("MIDI output not configured — open MIDI Setup", true);
            return;
        }

        setStatus ("Sent " + file.getFileName() + " to hardware");
    }

    bool captureHardwareSysex (const juce::File& sysexOut, juce::String& error)
    {
        const auto outId = HostPreferences::get().getMidiOutIdentifier();
        if (outId.isEmpty())
        {
            error = "No MIDI out configured";
            return false;
        }

        const auto info = findMidiEndpointInfo (outId, true);
        const auto* module = SysexDeviceRegistry::get().findModule (info.manufacturer, info.model, info.name);
        if (module == nullptr)
        {
            error = "No sysex module for " + info.name;
            return false;
        }

        juce::String openError;
        if (! engine.setMidiOutputDevice (outId, openError))
        {
            error = openError;
            return false;
        }

        const auto dumpIn = HostPreferences::get().getMidiDumpInIdentifier();
        if (dumpIn.isNotEmpty())
        {
            auto ids = engine.getSelectedMidiInputIdentifiers();
            if (! ids.contains (dumpIn))
            {
                ids.add (dumpIn);
                engine.setMidiInputDevices (ids);
            }
        }

        if (! engine.sendMidiMessage (module->buildDumpRequest()))
        {
            error = "Failed to send dump request";
            return false;
        }

        juce::MidiMessage dump;
        if (! engine.waitForSysexDump (
                [module] (const juce::MidiMessage& m) { return module->isDumpResponse (m); },
                dump, 5000, error))
            return false;

        if (! module->validateDump (dump))
        {
            error = "Received sysex failed validation";
            return false;
        }

        // Write raw .syx including F0/F7 for interchange.
        juce::MemoryBlock block;
        block.append (dump.getRawData(), (size_t) dump.getRawDataSize());
        if (! sysexOut.replaceWithData (block.getData(), block.getSize()))
        {
            error = "Failed to write " + sysexOut.getFullPathName();
            return false;
        }

        return true;
    }

    void openSettings()
    {
        if (showSettingsDialog (config, &knownPlugins, this))
        {
            engine.setAllowInstrumentAudioInput (
                HostPreferences::get().getAllowInstrumentAudioInput());

            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::InfoIcon,
                "Settings saved",
                "Instrument audio-input preference applies immediately (reload the plugin if an "
                "instrument was already open).\n\n"
                "Relaunch AU Effects Explorer for exploration folder / config override changes to take effect.\n\n"
                "If you changed the exploration folder, its data (including the AU plugin cache) was moved to the new location.");
        }
    }

    void openAddPlugin()
    {
        HostLog::info ("Add Plugin: ensuring AU cache...");
        // First use (or missing cache) triggers the AU scan; later opens just load the cache.
        ensurePluginCache (false);
        HostLog::info ("Add Plugin: opening picker (" + juce::String (knownPlugins.getTypes().size())
                       + " cached types)");

        juce::Array<int> added;
        if (! showAddPluginDialog (config, knownPlugins, this, added))
        {
            HostLog::info ("Add Plugin: picker cancelled or nothing selected");
            return;
        }

        juce::String addedNames;
        for (const int index : added)
        {
            if (juce::isPositiveAndBelow (index, config.plugins.size()))
            {
                if (addedNames.isNotEmpty())
                    addedNames += ", ";
                addedNames += config.plugins.getReference (index).displayLabel();
            }
        }

        HostLog::info ("Add Plugin: added " + juce::String (added.size()) + " plugin(s)"
                       + (addedNames.isNotEmpty() ? ": " + addedNames : juce::String()));

        populatePluginBox();
        if (! added.isEmpty())
        {
            const int index = added.getLast();
            // Defer switch so the Add Plugin modal is fully torn down before
            // heavy AU/WebView editor construction begins.
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainContent> (this), index]
                                             {
                                                 if (safe == nullptr)
                                                     return;
                                                 safe->currentPluginIndex = -1;
                                                 safe->switchToPlugin (index);
                                                 safe->setStatus ("Added plugin(s) to the list");
                                             });
            return;
        }
        setStatus ("Added plugin(s) to the list");
    }

    void rescanPlugins()
    {
        HostLog::info ("Rescan Audio Units requested");
        juce::String error;
        if (! AuPluginScanner::ensureCache (config.projectRoot, knownPlugins, this, error, true))
        {
            HostLog::error ("AU rescan failed: " + error);
            setStatus ("AU rescan failed: " + error, true);
            return;
        }

        HostLog::info ("AU rescan finished (" + juce::String (knownPlugins.getTypes().size()) + " cached types)");
        setStatus ("Rescanned Audio Units (" + juce::String (knownPlugins.getTypes().size()) + " found)");
    }

    void openCaptureTestCase()
    {
        promptCaptureTestCase();
    }

    /** Call after the host window is on-screen so AU Cocoa UIs can attach to an NSWindow. */
    void showPluginEditor()
    {
        // Do not AU-scan here — scanning starts the first time Add Plugin / More plugins is used.
        if (config.plugins.isEmpty())
        {
            setStatus (utf8 ("No plugins configured — use More plugins… to add one"));
            return;
        }

        // Editor first (while DSP is still suspended), then start audio.
        recreatePluginEditor();

        juce::String error;
        if (! engine.startAudioDevice (error))
        {
            setStatus ("Audio device error: " + error, true);
            return;
        }

        if (loadDefaultOrFirstPreset())
            return;

        if (! fixtureFiles.isEmpty())
        {
            const int selected = fixtureBox.getSelectedId() - 1;
            selectFixture (juce::isPositiveAndBelow (selected, fixtureFiles.size()) ? selected : 0);
            setStatus ("Ready - " + currentPlugin().displayLabel());
        }
        else
        {
            setStatus ("Ready - " + currentPlugin().displayLabel());
        }
    }

    /** Prefer config default_preset when present; otherwise the first preset in the list. */
    bool loadDefaultOrFirstPreset()
    {
        if (! juce::isPositiveAndBelow (currentPluginIndex, config.plugins.size()))
            return false;

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

        // Right column: host clock on row1.
        clickToggle.setBounds (row1.removeFromRight (26).withSizeKeepingCentre (26, 26));
        row1.removeFromRight (gap);
        bpmLabel.setBounds (row1.removeFromRight (30).withSizeKeepingCentre (30, ctrlH));
        row1.removeFromRight (gap);
        bpmEditor.setBounds (row1.removeFromRight (40).withSizeKeepingCentre (40, ctrlH));
        row1.removeFromRight (gap);
        hostClockToggle.setBounds (row1.removeFromRight (100).withSizeKeepingCentre (100, ctrlH));
        row1.removeFromRight (groupGap);

        // Left columns — shared field width + identical action buttons
        auto left0 = row0.removeFromLeft (leftColW);
        auto left1 = row1.removeFromLeft (leftColW);
        auto left2 = row2.removeFromLeft (leftColW);

        pluginLabel.setBounds (place (left0, leftLabelW));
        left0.removeFromLeft (gap);
        pluginField.setBounds (place (left0, leftDropW));
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

        constexpr int bypassButtonW = 64;
        bypassButton.setBounds (row0.removeFromRight (bypassButtonW).withSizeKeepingCentre (bypassButtonW, ctrlH));
        row0.removeFromRight (gap);

        // Status LCD spans the rest of row0 to the right edge
        statusDisplay.setBounds (row0.withSizeKeepingCentre (row0.getWidth(), ctrlH));

        // MIDI + Source Clip share label width; clip dropdown is half-width so Send can grow.
        constexpr int midLabelW = leftLabelW;
        const int mid1W = juce::jmax (180, row1.getWidth());

        {
            auto mid1 = row1.removeFromLeft (mid1W);
            midiLabel.setBounds (place (mid1, midLabelW));
            mid1.removeFromLeft (gap);
            midiLed.setBounds (mid1.removeFromRight (16).withSizeKeepingCentre (16, 16));
            mid1.removeFromRight (gap);
            midiField.setBounds (place (mid1, mid1.getWidth()));
        }

        constexpr int beginButtonW = 72;
        const int clipControlsW = 26 + gap + beginButtonW + gap;
        const int fixtureDropFull = juce::jmax (120, mid1W - midLabelW - gap - clipControlsW);
        const int fixtureDropW = juce::jmax (60, fixtureDropFull / 2);
        const int mid2W = midLabelW + gap + fixtureDropW + gap + clipControlsW;

        {
            auto mid2 = row2.removeFromLeft (mid2W);
            fixtureLabel.setBounds (place (mid2, midLabelW));
            mid2.removeFromLeft (gap);
            loopToggle.setBounds (mid2.removeFromRight (26).withSizeKeepingCentre (26, 26));
            mid2.removeFromRight (gap);
            transportButton.setBounds (mid2.removeFromRight (beginButtonW).withSizeKeepingCentre (beginButtonW, ctrlH));
            mid2.removeFromRight (gap);
            fixtureBox.setBounds (place (mid2, fixtureDropW));
        }

        row2.removeFromLeft (gap);
        sendLabel.setBounds (place (row2, 32));
        row2.removeFromLeft (gap);

        constexpr int mixLabelW = 28;
        const int mixSliderW = juce::jmax (72, row2.getWidth() / 5);
        const int mixSectionW = mixLabelW + gap + mixSliderW;
        auto mixSection = row2.removeFromRight (mixSectionW);
        mixLabel.setBounds (place (mixSection, mixLabelW));
        mixSection.removeFromLeft (gap);
        mixSlider.setBounds (mixSection.withSizeKeepingCentre (mixSection.getWidth(), ctrlH));

        sendSlider.setBounds (row2.withSizeKeepingCentre (juce::jmax (80, row2.getWidth()), ctrlH));

        bounds.removeFromTop (8);
        controlStripDivider = juce::Rectangle<int> (bounds.getX(), bounds.getY(), bounds.getWidth(), 1);
        bounds.removeFromTop (4);

        editorViewport.setBounds (bounds);
        if (hardwareMeterPanel != nullptr)
            hardwareMeterPanel->setBounds (bounds);
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
        jassert (juce::isPositiveAndBelow (currentPluginIndex, config.plugins.size()));
        return config.plugins.getReference (currentPluginIndex);
    }

    void ensurePluginCache (bool forceRescan)
    {
        juce::String error;
        if (! AuPluginScanner::ensureCache (config.projectRoot, knownPlugins, this, error, forceRescan))
        {
            if (error.isNotEmpty())
                HostLog::error ("AU plugin cache: " + error);
            setStatus ("AU scan issue: " + error, true);
        }
        else if (forceRescan)
        {
            setStatus ("Rescanned Audio Units (" + juce::String (knownPlugins.getTypes().size()) + " found)");
        }
    }

    void populatePluginBox()
    {
        if (! juce::isPositiveAndBelow (currentPluginIndex, config.plugins.size()))
        {
            currentPluginIndex = 0;
            for (int i = 0; i < config.plugins.size(); ++i)
            {
                if (config.plugins.getReference (i).installed)
                {
                    currentPluginIndex = i;
                    break;
                }
            }
        }

        pluginField.setPlugins (config.plugins, currentPluginIndex);
    }

    void confirmRemovePlugin (int index)
    {
        if (! juce::isPositiveAndBelow (index, config.plugins.size()))
            return;

        const auto label = config.plugins.getReference (index).displayLabel();
        auto options = juce::MessageBoxOptions()
                           .withIconType (juce::MessageBoxIconType::QuestionIcon)
                           .withTitle ("Remove Plugin")
                           .withMessage ("Remove \"" + label + "\" from the plugin list?")
                           .withButton ("Remove")
                           .withButton ("Cancel");

        juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer<MainContent> (this), index] (int result)
                                      {
                                          if (safe == nullptr || result != 1)
                                              return;
                                          safe->removePluginAt (index);
                                      });
    }

    void removePluginAt (int index)
    {
        if (! juce::isPositiveAndBelow (index, config.plugins.size()))
            return;

        const auto removedId = config.plugins.getReference (index).id;
        config.plugins.remove (index);

        if (config.defaultPluginId == removedId)
            config.defaultPluginId = config.plugins.isEmpty() ? juce::String()
                                                             : config.plugins.getReference (0).id;

        juce::String error;
        if (! config.saveToFile (error))
        {
            setStatus ("Failed to save config: " + error, true);
            return;
        }

        engine.stopFixture();
        destroyPluginEditor();

        if (config.plugins.isEmpty())
        {
            currentPluginIndex = 0;
            populatePluginBox();
            setStatus ("No plugins configured - use Plugins -> Add Plugin...");
            return;
        }

        if (currentPluginIndex >= config.plugins.size())
            currentPluginIndex = config.plugins.size() - 1;
        else if (index < currentPluginIndex)
            --currentPluginIndex;
        else if (index == currentPluginIndex)
            currentPluginIndex = juce::jlimit (0, config.plugins.size() - 1, currentPluginIndex);

        // Force reload if we removed the active plugin.
        const int next = currentPluginIndex;
        currentPluginIndex = -1;
        populatePluginBox();
        switchToPlugin (next);
        setStatus ("Removed plugin from list");
    }

    void populatePresets()
    {
        presetBox.clear();
        presetFiles.clearQuick();

        if (! juce::isPositiveAndBelow (currentPluginIndex, config.plugins.size()))
        {
            presetBox.addItem ("(no plugin)", 1);
            return;
        }

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

    static void collectAupresetFiles (const juce::File& file, juce::Array<juce::File>& out)
    {
        if (file.isDirectory())
        {
            for (const auto& child : collectFiles (file, ".aupreset", true))
                out.addIfNotAlreadyThere (child);
            return;
        }

        if (file.existsAsFile() && file.hasFileExtension (".aupreset"))
            out.addIfNotAlreadyThere (file);
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        if (! juce::isPositiveAndBelow (currentPluginIndex, config.plugins.size()))
            return false;

        if (currentPlugin().presetsDir == juce::File())
            return false;

        for (const auto& path : files)
        {
            const juce::File file (path);
            if (file.hasFileExtension (".aupreset"))
                return true;
            if (file.isDirectory())
                return true; // may contain .aupreset; validated on drop
        }

        return false;
    }

    void filesDropped (const juce::StringArray& files, int, int) override
    {
        importDroppedAupresets (files);
    }

    void importDroppedAupresets (const juce::StringArray& files)
    {
        if (! juce::isPositiveAndBelow (currentPluginIndex, config.plugins.size()))
        {
            setStatus ("Load a plugin before importing presets", true);
            return;
        }

        auto presetsDir = currentPlugin().presetsDir;
        if (presetsDir == juce::File())
        {
            setStatus ("This plugin has no presets folder", true);
            return;
        }

        presetsDir.createDirectory();
        if (! presetsDir.isDirectory())
        {
            setStatus ("Could not create presets folder: " + presetsDir.getFullPathName(), true);
            return;
        }

        juce::Array<juce::File> sources;
        for (const auto& path : files)
            collectAupresetFiles (juce::File (path), sources);

        if (sources.isEmpty())
        {
            setStatus ("No .aupreset files in drop", true);
            return;
        }

        enum class ConflictPolicy { ask, replaceAll, skipAll };
        auto policy = ConflictPolicy::ask;

        juce::Array<juce::File> imported;
        int skipped = 0;
        int copyFailures = 0;
        bool cancelled = false;

        for (int i = 0; i < sources.size(); ++i)
        {
            const auto& src = sources.getReference (i);
            const auto dest = presetsDir.getChildFile (src.getFileName());

            if (src.getFullPathName() == dest.getFullPathName())
            {
                imported.add (dest);
                continue;
            }

            if (dest.existsAsFile())
            {
                bool replace = false;

                if (policy == ConflictPolicy::replaceAll)
                {
                    replace = true;
                }
                else if (policy == ConflictPolicy::skipAll)
                {
                    ++skipped;
                    continue;
                }
                else
                {
                    const bool moreConflictsAhead = [&]()
                    {
                        for (int j = i + 1; j < sources.size(); ++j)
                        {
                            const auto& later = sources.getReference (j);
                            const auto laterDest = presetsDir.getChildFile (later.getFileName());
                            if (later.getFullPathName() != laterDest.getFullPathName()
                                && laterDest.existsAsFile())
                                return true;
                        }
                        return false;
                    }();

                    juce::ToggleButton applyToAll ("Apply to all");
                    applyToAll.setSize (280, 24);
                    applyToAll.setVisible (moreConflictsAhead);

                    juce::AlertWindow dialog (
                        "Preset already exists",
                        "\"" + dest.getFileName() + "\" already exists in the presets folder.",
                        juce::MessageBoxIconType::QuestionIcon,
                        this);

                    if (moreConflictsAhead)
                        dialog.addCustomComponent (&applyToAll);

                    dialog.addButton ("Replace", 1, juce::KeyPress (juce::KeyPress::returnKey));
                    dialog.addButton ("Skip", 2);
                    dialog.addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

                    const int result = dialog.runModalLoop();
                    if (result == 0)
                    {
                        cancelled = true;
                        break;
                    }

                    replace = (result == 1);

                    if (moreConflictsAhead && applyToAll.getToggleState())
                        policy = replace ? ConflictPolicy::replaceAll : ConflictPolicy::skipAll;

                    if (! replace)
                    {
                        ++skipped;
                        continue;
                    }
                }

                if (! replace)
                    continue;

                if (! dest.deleteFile())
                {
                    ++copyFailures;
                    HostLog::error ("Failed to replace preset " + dest.getFullPathName());
                    continue;
                }
            }

            if (! src.copyFileTo (dest))
            {
                ++copyFailures;
                HostLog::error ("Failed to copy preset " + src.getFullPathName()
                                + " -> " + dest.getFullPathName());
                continue;
            }

            imported.add (dest);
        }

        if (imported.isEmpty())
        {
            if (cancelled)
                setStatus ("Import cancelled");
            else if (skipped > 0 && copyFailures == 0)
                setStatus ("Skipped " + juce::String (skipped) + " existing preset"
                           + (skipped == 1 ? "" : "s"));
            else
                setStatus ("Failed to import preset(s)", true);
            return;
        }

        populatePresets();
        const auto& first = imported.getReference (0);
        selectPresetInDropdown (first);

        juce::String error;
        if (! engine.loadPreset (first, error))
        {
            setStatus ("Imported " + juce::String (imported.size())
                           + " preset(s) but load failed: " + error,
                       true);
            return;
        }

        if (pluginEditor != nullptr)
            pluginEditor->repaint();

        savePresetNameEditor.setText (first.getFileNameWithoutExtension(),
                                      juce::dontSendNotification);

        juce::String status = (cancelled ? "Import cancelled after copying " : "Imported ")
                              + juce::String (imported.size()) + " preset"
                              + (imported.size() == 1 ? "" : "s")
                              + "; loaded " + first.getFileNameWithoutExtension();
        if (skipped > 0)
            status += " (" + juce::String (skipped) + " skipped)";
        if (copyFailures > 0)
            status += " (" + juce::String (copyFailures) + " copy failed)";
        setStatus (status);
    }

    void switchToPlugin (int pluginIndex)
    {
        if (! juce::isPositiveAndBelow (pluginIndex, config.plugins.size())
            || pluginIndex == currentPluginIndex)
            return;

        if (! config.plugins.getReference (pluginIndex).installed)
        {
            setStatus ("Plugin not installed: " + config.plugins.getReference (pluginIndex).displayLabel(), true);
            populatePluginBox();
            return;
        }

        engine.stopFixture();
        destroyPluginEditor();

        currentPluginIndex = pluginIndex;
        const auto& plugin = currentPlugin();
        plugin.presetsDir.createDirectory();

        juce::String error;
        if (! engine.loadPlugin (plugin.toPluginDescription(), error))
        {
            setStatus ("Failed to load plugin: " + error, true);
            return;
        }

        populatePresets();
        savePresetNameEditor.setText ("Untitled", juce::dontSendNotification);
        // Build the editor while audio is still stopped / plugin suspended.
        // WebView AUs (e.g. Lunacy BEAM) crash if processBlock runs during UI init.
        recreatePluginEditor();

        if (! engine.startAudioDevice (error))
        {
            setStatus ("Audio device error: " + error, true);
            return;
        }

        populatePluginBox();

        if (! loadDefaultOrFirstPreset())
        {
            setStatus ("Ready - " + plugin.displayLabel()
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

        // createEditor() intentionally suspends DSP during UI construction.
        // When toggling back from hardware meters, audio is already running, so
        // resume processing immediately to avoid a dry/no-input path.
        if (engine.getDeviceManager().getCurrentAudioDevice() != nullptr)
            engine.setPluginProcessingSuspended (false);
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
        fixtureBox.clear();
        fixtureFiles.clearQuick();

        int impulseId = 0;

        // Clips directly in fixtures/ sit at the top level of the menu.
        for (const auto& file : collectFiles (config.fixturesDir, ".wav", false))
        {
            fixtureFiles.add (file);
            fixtureBox.addItem (file.getFileNameWithoutExtension(), fixtureFiles.size());
            if (file.getFileName().equalsIgnoreCase ("impulse.wav"))
                impulseId = fixtureFiles.size();
        }

        // Each subfolder of fixtures/ becomes a submenu of its .wav files.
        auto folders = config.fixturesDir.findChildFiles (juce::File::findDirectories, false);
        struct FolderComparator
        {
            static int compareElements (const juce::File& a, const juce::File& b)
            {
                return a.getFileName().compareIgnoreCase (b.getFileName());
            }
        };
        FolderComparator folderComparator;
        folders.sort (folderComparator);

        for (const auto& folder : folders)
        {
            const auto clips = collectFiles (folder, ".wav", true);
            if (clips.isEmpty())
                continue;

            juce::PopupMenu subMenu;
            for (const auto& file : clips)
            {
                fixtureFiles.add (file);
                auto display = file.getRelativePathFrom (folder);
                if (display.endsWithIgnoreCase (".wav"))
                    display = display.dropLastCharacters (4);
                subMenu.addItem (fixtureFiles.size(), display);
            }

            fixtureBox.getRootMenu()->addSubMenu (folder.getFileName(), subMenu);
        }

        if (impulseId > 0)
            fixtureBox.setSelectedId (impulseId, juce::dontSendNotification);
        else if (! fixtureFiles.isEmpty())
            fixtureBox.setSelectedId (1, juce::dontSendNotification);
    }

    void populateMidiInputs()
    {
        midiDevices = engine.getMidiInputDevices();

        juce::StringArray selectedIds;
        for (const auto& wantedName : config.defaultMidiInputs)
        {
            for (const auto& device : midiDevices)
            {
                // Match exact or substring so "Oxygen Pro 49" also selects
                // "Oxygen Pro 49 Mackie/HUI" (where DAW transport usually lives).
                if (device.name.equalsIgnoreCase (wantedName)
                    || device.name.containsIgnoreCase (wantedName))
                {
                    selectedIds.addIfNotAlreadyThere (device.identifier);
                }
            }
        }

        midiField.setDevices (midiDevices, selectedIds);
        engine.setMidiInputDevices (midiField.getSelectedIdentifiers());
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

        // Bypass resets in the engine whenever a plugin (re)loads.
        if (bypassButton.getToggleState() != engine.isBypassed())
            bypassButton.setToggleState (engine.isBypassed(), juce::dontSendNotification);

        // DAW surface Play/Stop (MIDI Start/Stop or Mackie notes 94/93).
        if (engine.consumeTransportPlayRequest())
            startPlayback();
        if (engine.consumeTransportStopRequest())
            stopPlayback();
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
            setStatus (utf8 ("No installed plugin selected — choose one from the list"));
            return;
        }

        const auto& plugin = currentPlugin();
        plugin.presetsDir.createDirectory();

        juce::String error;
        if (! engine.loadPlugin (plugin.toPluginDescription(), error))
        {
            setStatus ("Failed to load plugin: " + error, true);
            return;
        }

        populatePresets();
        setStatus ("Loaded " + plugin.displayLabel() + " - opening UI...");
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
        if (! engine.loadPlugin (plugin.toPluginDescription(), error))
        {
            setStatus ("Failed to reset plugin: " + error, true);
            return;
        }

        savePresetNameEditor.setText ("Untitled", juce::dontSendNotification);
        recreatePluginEditor();

        if (! engine.startAudioDevice (error))
        {
            setStatus ("Audio device error: " + error, true);
            return;
        }

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
            if (engine.isHardwareMode())
                sendSelectedHardwareState();
            else
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

        if (button == &bypassButton)
        {
            engine.setBypassed (bypassButton.getToggleState());
            setStatus (bypassButton.getToggleState() ? "Plugin bypassed" : "Plugin active");
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
    }

    void comboBoxChanged (juce::ComboBox* box) override
    {
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

    void promptCaptureTestCase()
    {
        const int fixtureIndex = fixtureBox.getSelectedId() - 1;
        if (! juce::isPositiveAndBelow (fixtureIndex, fixtureFiles.size()))
        {
            setStatus ("Select a source clip before capturing", true);
            return;
        }

        if (engine.getPlugin() == nullptr)
        {
            setStatus ("No plugin loaded", true);
            return;
        }

        auto* aw = new juce::AlertWindow ("Capture Test Case",
                                          "Describe the snapshot and choose how to label the output.",
                                          juce::MessageBoxIconType::QuestionIcon,
                                          this);

        aw->addTextEditor ("description",
                           lastCaptureDescription.isNotEmpty() ? lastCaptureDescription : "snapshot",
                           "Description");
        aw->addComboBox ("role", { "golden", "suspect", "broken" }, "Type");
        aw->addComboBox ("source", { "Rendered plugin", "Hardware", "Both" }, "Capture");

        if (auto* roleBox = aw->getComboBoxComponent ("role"))
            roleBox->setSelectedItemIndex (juce::jlimit (0, 2, lastCaptureRoleIndex),
                                           juce::dontSendNotification);
        if (auto* sourceBox = aw->getComboBoxComponent ("source"))
            sourceBox->setSelectedItemIndex (juce::jlimit (0, 2, lastCaptureSourceIndex),
                                             juce::dontSendNotification);

        aw->addButton ("Capture", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        aw->enterModalState (true,
                             juce::ModalCallbackFunction::create (
                                 [safe = juce::Component::SafePointer<MainContent> (this), aw] (int result)
                                 {
                                     std::unique_ptr<juce::AlertWindow> dialog (aw);

                                     if (safe == nullptr || result != 1)
                                         return;

                                     const auto description = dialog->getTextEditorContents ("description").trim();
                                     int roleIndex = 2; // broken
                                     if (auto* roleBox = dialog->getComboBoxComponent ("role"))
                                         roleIndex = juce::jmax (0, roleBox->getSelectedItemIndex());

                                     int sourceIndex = 0;
                                     if (auto* sourceBox = dialog->getComboBoxComponent ("source"))
                                         sourceIndex = juce::jmax (0, sourceBox->getSelectedItemIndex());

                                     safe->lastCaptureDescription = description;
                                     safe->lastCaptureRoleIndex = roleIndex;
                                     safe->lastCaptureSourceIndex = sourceIndex;
                                     dialog.reset();
                                     safe->captureTestCase (description, roleIndex, sourceIndex);
                                 }),
                             true);
    }

    void captureTestCase (const juce::String& snapshotName, int roleIndex, int sourceIndex)
    {
        if (snapshotName.isEmpty())
        {
            setStatus ("Description is required", true);
            return;
        }

        const int fixtureIndex = fixtureBox.getSelectedId() - 1;
        if (! juce::isPositiveAndBelow (fixtureIndex, fixtureFiles.size()))
        {
            setStatus ("Select a source clip before capturing", true);
            return;
        }

        const bool wantPlugin = (sourceIndex == 0 || sourceIndex == 2);
        const bool wantHardware = (sourceIndex == 1 || sourceIndex == 2);

        if (wantPlugin && engine.getPlugin() == nullptr)
        {
            setStatus ("No plugin loaded", true);
            return;
        }

        if (wantHardware && ! engine.hasHardwareLoopConfigured())
        {
            setStatus ("Configure Hardware Audio Setup before capturing hardware", true);
            return;
        }

        const auto fixtureFile = fixtureFiles[fixtureIndex];
        const auto captureDir = config.sessionsRoot
                                    .getChildFile (HostConfig::slugify (currentPlugin().sessionName))
                                    .getChildFile ("artifacts");
        captureDir.createDirectory();

        const auto keyword = keywordFromDescription (snapshotName);
        const auto token = juce::Uuid().toString().substring (0, 8);
        const auto stem = keyword.isNotEmpty() ? keyword + "_" + token : token;
        const auto presetOut = captureDir.getChildFile (stem + ".aupreset");
        const auto roleSuffix = artifactRoleCode (roleIndex);
        const auto outputOut = captureDir.getChildFile (stem + "_output_" + roleSuffix + ".wav");
        const auto hwOutputOut = captureDir.getChildFile (stem + "_output_hw_" + roleSuffix + ".wav");
        const auto sysexOut = captureDir.getChildFile (stem + ".syx");

        juce::String error;
        const bool originalHardwareMode = engine.isHardwareMode();

        struct HardwareModeRestore
        {
            PluginAudioEngine& engineRef;
            MainContent& owner;
            bool restoreHardwareMode;
            ~HardwareModeRestore()
            {
                engineRef.setHardwareMode (restoreHardwareMode);
                owner.refreshHardwareModeUi();
            }
        } restoreMode { engine, *this, originalHardwareMode };

        auto showCaptureMode = [this] (bool hardwareMode)
        {
            if (engine.isHardwareMode() != hardwareMode)
                engine.setHardwareMode (hardwareMode);
            refreshHardwareModeUi();
        };

        if (wantPlugin)
        {
            showCaptureMode (false);

            if (! engine.saveCurrentPreset (presetOut, error))
            {
                setStatus ("Failed to save preset: " + error, true);
                return;
            }
        }

        engine.stopFixture();

        if (wantPlugin)
        {
            engine.stopAudioDevice();

            OfflineCaptureOptions renderOptions;
            if (! OfflineCapture::renderPluginToFile (*engine.getPlugin(), fixtureFile, outputOut, renderOptions, error))
            {
                juce::String restartError;
                if (! engine.startAudioDevice (restartError))
                    HostLog::error ("Failed to restart audio after offline capture failure: " + restartError);
                setStatus ("Offline capture failed: " + error, true);
                return;
            }

            if (! engine.startAudioDevice (error))
            {
                setStatus ("Capture rendered but audio restart failed: " + error, true);
                return;
            }
        }

        if (wantHardware)
        {
            showCaptureMode (true);

            std::atomic<bool> cancelRequested { false };
            juce::AlertWindow progress ("Capturing Hardware",
                                        "Recording hardware return... press Cancel to stop.",
                                        juce::MessageBoxIconType::NoIcon,
                                        this);
            progress.addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            if (auto* cancelButton = progress.getButton ("Cancel"))
                cancelButton->onClick = [&cancelRequested] { cancelRequested.store (true); };
            progress.setAlwaysOnTop (true);
            progress.centreAroundComponent (this, 460, 190);
            progress.enterModalState (true, nullptr, false);

            if (! engine.captureHardwareToFile (fixtureFile, hwOutputOut, 1.0, -60.0, 120.0, error, &cancelRequested))
            {
                progress.exitModalState (0);
                setStatus ("Hardware capture failed: " + error, true);
                return;
            }
            progress.exitModalState (0);

            juce::String sysexError;
            if (! captureHardwareSysex (sysexOut, sysexError))
                HostLog::info ("Sysex dump skipped: " + sysexError);
        }

        SessionSnapRequest request;
        request.sessionsRoot = config.sessionsRoot;
        request.sessionName = currentPlugin().sessionName;
        request.snapshotName = snapshotName;
        request.inputFile = fixtureFile;
        if (wantPlugin)
        {
            request.outputFile = outputOut;
            request.presetFile = presetOut;
        }
        if (wantHardware)
        {
            request.hardwareOutputFile = hwOutputOut;
            if (sysexOut.existsAsFile())
                request.sysexFile = sysexOut;
        }
        request.pluginPath = currentPlugin().identifierForLoad();
        request.notes = "Captured from AU Effects Explorer";

        if (! SessionSnap::registerSnapshot (request, error))
        {
            setStatus ("Capture saved to disk but session update failed: " + error, true);
            return;
        }

        if (engine.isHardwareMode())
            populateHardwareStates();

        if (auto* window = findParentComponentOfClass<MainWindow>())
            window->setLightsOutEnabled (false);
        captureDir.revealToUser();

        setStatus ("Captured test case: " + snapshotName);
    }

    static juce::String artifactRoleCode (int roleIndex)
    {
        switch (roleIndex)
        {
            case 0:  return "gld";
            case 1:  return "sus";
            default: return "bkn";
        }
    }

    bool registerSnapshot (const juce::String& snapshotName,
                           const juce::File& inputFile,
                           const juce::File& outputFile,
                           const juce::File& presetFile,
                           juce::String& error)
    {
        SessionSnapRequest request;
        request.sessionsRoot = config.sessionsRoot;
        request.sessionName = currentPlugin().sessionName;
        request.snapshotName = snapshotName;
        request.inputFile = inputFile;
        request.outputFile = outputFile;
        request.presetFile = presetFile;
        request.pluginPath = currentPlugin().identifierForLoad();
        request.notes = "Captured from AU Effects Explorer";
        return SessionSnap::registerSnapshot (request, error);
    }

    PluginAudioEngine& engine;
    HostConfig& config;
    juce::KnownPluginList& knownPlugins;
    int currentPluginIndex { 0 };

    juce::Label pluginLabel;
    PluginPickerField pluginField;
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
    juce::TextButton bypassButton;
    LoopToggleButton loopToggle;
    juce::Label sendLabel;
    SendSlider sendSlider;
    juce::Label mixLabel;
    MixSlider mixSlider;
    juce::Label midiLabel;
    MidiSourceField midiField;
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
    std::unique_ptr<HardwareLoopMeterPanel> hardwareMeterPanel;
    juce::AudioProcessorEditor* pluginEditor { nullptr };
    juce::Array<juce::File> presetFiles;
    juce::Array<juce::File> hardwareStateFiles;
    juce::Array<juce::File> fixtureFiles;
    juce::Component* keyListenerOwner { nullptr };
    juce::ScopedMessageBox replacePresetDialog;
    juce::String lastCaptureDescription { "snapshot" };
    int lastCaptureRoleIndex { 2 }; // broken
    int lastCaptureSourceIndex { 0 }; // rendered plugin

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
    content = std::make_unique<MainContent> (*engine, config, knownPlugins);
    auto* mainContent = content.get();
    setContentOwned (content.release(), true);
    setUsingNativeTitleBar (true);
    setResizable (true, true);
    centreWithSize (1100, 780);

#if JUCE_MAC
    appleMenu = std::make_unique<juce::PopupMenu>();
    appleMenu->addItem (menuAbout, "About AU Effects Explorer");
    appleMenu->addSeparator();
    appleMenu->addItem (menuSettings, "Settings...");
    juce::MenuBarModel::setMacMainMenu (this, appleMenu.get());
#else
    setMenuBar (this);
#endif

    setVisible (true);
    lightsOut.setHostWindow (this);
    addKeyListener (this);

#if JUCE_MAC
    juce::Timer::callAfterDelay (0, [safe = juce::Component::SafePointer<MainWindow> (this)]
    {
        if (safe != nullptr)
            safe->syncNativeMenuShortcuts();
    });
#endif

    // AU Cocoa editors need a real NSWindow parent; create after the host is shown.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainContent> (mainContent)]
                                     {
                                         if (safe != nullptr)
                                             safe->showPluginEditor();
                                     });
}

MainWindow::~MainWindow()
{
    lightsOut.release();

#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (nullptr);
#else
    setMenuBar (nullptr);
#endif

    // MainContent holds PluginAudioEngine&; DocumentWindow owns content and
    // would destroy it after our members. Clear it first so ~MainContent
    // still sees a live engine (otherwise quit crashes in destroyEditor).
    clearContentComponent();
    engine.reset();
}

void MainWindow::closeButtonPressed()
{
    lightsOut.release();
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

void MainWindow::toggleLightsOut()
{
    lightsOut.setHostWindow (this);
    lightsOut.setEnabled (! lightsOut.isEnabled());
    menuItemsChanged();
    syncNativeMenuShortcuts();
}

void MainWindow::toggleLightsOutFromMenu()
{
    juce::Timer::callAfterDelay (150, [safe = juce::Component::SafePointer<MainWindow> (this)]
                                  {
                                      if (safe != nullptr)
                                          safe->toggleLightsOut();
                                  });
}

void MainWindow::toggleHardwareMode()
{
    if (engine == nullptr || ! engine->hasHardwareLoopConfigured())
        return;

    engine->setHardwareMode (! engine->isHardwareMode());
    refreshHardwareUi();
    menuItemsChanged();
    syncNativeMenuShortcuts();
}

void MainWindow::toggleHardwareModeFromMenu()
{
    juce::Timer::callAfterDelay (150, [safe = juce::Component::SafePointer<MainWindow> (this)]
                                  {
                                      if (safe != nullptr)
                                          safe->toggleHardwareMode();
                                  });
}

void MainWindow::openHardwareAudioSetup()
{
    if (auto* mainContent = dynamic_cast<MainContent*> (getContentComponent()))
        mainContent->openHardwareAudioSetup();
}

void MainWindow::openMidiSetup()
{
    if (auto* mainContent = dynamic_cast<MainContent*> (getContentComponent()))
        mainContent->openMidiSetup();
}

void MainWindow::refreshHardwareUi()
{
    if (auto* mainContent = dynamic_cast<MainContent*> (getContentComponent()))
        mainContent->refreshHardwareModeUi();
}

void MainWindow::setLightsOutEnabled (bool shouldEnable)
{
    lightsOut.setHostWindow (this);
    if (lightsOut.isEnabled() == shouldEnable)
        return;

    lightsOut.setEnabled (shouldEnable);
    menuItemsChanged();
    syncNativeMenuShortcuts();
}

void MainWindow::syncNativeMenuShortcuts()
{
#if JUCE_MAC
    const bool lightsTicked = lightsOut.isEnabled();
    const bool hwTicked = engine != nullptr && engine->isHardwareMode();
    juce::Timer::callAfterDelay (0, [lightsTicked, hwTicked]
    {
        lightsOutSyncMenuItem (lightsTicked);
        nativeSyncMenuItem ("Use Hardware", "u", true, false, hwTicked, true);
    });
#endif
}

bool MainWindow::keyPressed (const juce::KeyPress& key, juce::Component* originatingComponent)
{
    juce::ignoreUnused (originatingComponent);

    if (key == juce::KeyPress ('l', juce::ModifierKeys::commandModifier, 0))
    {
        toggleLightsOut();
        return true;
    }

    if (key == juce::KeyPress ('u', juce::ModifierKeys::commandModifier, 0))
    {
        toggleHardwareMode();
        return true;
    }

    return false;
}

juce::StringArray MainWindow::getMenuBarNames()
{
#if JUCE_MAC
    return { "Session", "Plugins" };
#else
    return { "AU Effects Explorer", "Session", "Plugins" };
#endif
}

juce::PopupMenu MainWindow::getMenuForIndex (int topLevelMenuIndex, const juce::String& menuName)
{
    juce::ignoreUnused (menuName);
    juce::PopupMenu menu;

    auto addSessionItems = [this, &menu]()
    {
        menu.addItem (menuCaptureTestCase, "Capture Test Case...");
        menu.addSeparator();
        menu.addItem (menuHardwareAudioSetup, "Hardware Audio Setup...");
        menu.addItem (menuMidiSetup, "MIDI Setup...");
        {
            juce::PopupMenu::Item item;
            item.itemID = menuUseHardware;
            item.text = "Use Hardware";
            item.isTicked = engine != nullptr && engine->isHardwareMode();
            item.isEnabled = engine != nullptr && engine->hasHardwareLoopConfigured();
            item.shortcutKeyDescription = "Cmd+U";
            menu.addItem (std::move (item));
        }
        menu.addSeparator();
        {
            juce::PopupMenu::Item item;
            item.itemID = menuLightsOut;
            item.text = "Lights Out";
            item.isTicked = lightsOut.isEnabled();
            item.shortcutKeyDescription = "Cmd+L";
            menu.addItem (std::move (item));
        }
    };

#if JUCE_MAC
    if (topLevelMenuIndex == 0)
        addSessionItems();
    else if (topLevelMenuIndex == 1)
    {
        menu.addItem (menuAddPlugin, "Add Plugin...");
        menu.addItem (menuRescanPlugins, "Rescan Audio Units...");
    }
#else
    if (topLevelMenuIndex == 0)
    {
        menu.addItem (menuAbout, "About AU Effects Explorer");
        menu.addSeparator();
        menu.addItem (menuSettings, "Settings...");
    }
    else if (topLevelMenuIndex == 1)
        addSessionItems();
    else if (topLevelMenuIndex == 2)
    {
        menu.addItem (menuAddPlugin, "Add Plugin...");
        menu.addItem (menuRescanPlugins, "Rescan Audio Units...");
    }
#endif

    return menu;
}

void MainWindow::menuItemSelected (int menuItemID, int topLevelMenuIndex)
{
    juce::ignoreUnused (topLevelMenuIndex);

    // Never run modal dialogs synchronously from a native menu callback.
    // On macOS that can abort the process immediately (often with no crash report)
    // because AppKit is still inside menu tracking.
    juce::MessageManager::callAsync ([safeWindow = juce::Component::SafePointer<MainWindow> (this),
                                      menuItemID]
                                     {
                                         auto* window = safeWindow.getComponent();
                                         if (window == nullptr)
                                             return;

                                         auto* mainContent = dynamic_cast<MainContent*> (window->getContentComponent());
                                         if (mainContent == nullptr)
                                             return;

                                         switch (menuItemID)
                                         {
                                             case menuAbout:               mainContent->openAbout(); break;
                                             case menuSettings:            mainContent->openSettings(); break;
                                             case menuCaptureTestCase:     mainContent->openCaptureTestCase(); break;
                                             case menuLightsOut:           window->toggleLightsOutFromMenu(); break;
                                             case menuHardwareAudioSetup:  window->openHardwareAudioSetup(); break;
                                             case menuMidiSetup:           window->openMidiSetup(); break;
                                             case menuUseHardware:         window->toggleHardwareModeFromMenu(); break;
                                             case menuAddPlugin:           mainContent->openAddPlugin(); break;
                                             case menuRescanPlugins:       mainContent->rescanPlugins(); break;
                                             default: break;
                                         }
                                     });
}
