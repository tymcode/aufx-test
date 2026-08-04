#pragma once

#include <JuceHeader.h>
#include "HostLookAndFeel.h"
#include "PluginAudioEngine.h"
#include "Utf8.h"

/**
 * Horizontal stereo VU pair (L/R).
 *
 * Bars show smoothed per-block sample peaks (ballistics for readability).
 * A sticky peak-hold mark tracks the highest peak since last clear. If any
 * sample peak reaches 0 dBFS, a CLIP badge latches until clicked.
 */
class StereoVuMeter : public juce::Component
{
public:
    explicit StereoVuMeter (juce::String titleIn)
        : title (std::move (titleIn))
    {
        setOpaque (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void setLevels (float peakL, float peakR)
    {
        const float dbL = juce::Decibels::gainToDecibels (peakL, -100.0f);
        const float dbR = juce::Decibels::gainToDecibels (peakR, -100.0f);
        levelL = smoothToward (levelL, dbL);
        levelR = smoothToward (levelR, dbR);

        // Sticky holds — only rise until the user clicks to clear.
        holdL = juce::jmax (holdL, dbL);
        holdR = juce::jmax (holdR, dbR);

        if (peakL >= 1.0f || peakR >= 1.0f)
            clipped = true;

        repaint();
    }

    void clearPeakHold()
    {
        holdL = levelL;
        holdR = levelR;
        clipped = false;
        repaint();
    }

    bool isClipped() const { return clipped; }

    void mouseDown (const juce::MouseEvent&) override
    {
        clearPeakHold();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (HostLookAndFeel::wellBackground));

        auto bounds = getLocalBounds().reduced (8);
        auto titleRow = bounds.removeFromTop (20);
        g.setColour (juce::Colour (HostLookAndFeel::textBright));
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (title, titleRow.removeFromLeft (titleRow.getWidth() - 56),
                    juce::Justification::centredLeft);

        if (clipped)
        {
            auto badge = titleRow.withSizeKeepingCentre (52, 18).toFloat();
            g.setColour (juce::Colours::red.darker (0.15f));
            g.fillRoundedRectangle (badge, 3.0f);
            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText ("CLIP", badge.toNearestInt(), juce::Justification::centred);
        }

        g.setColour (juce::Colour (HostLookAndFeel::textDim));
        g.setFont (juce::FontOptions (11.0f));
        auto readout = bounds.removeFromTop (18);
        g.drawText ("L " + formatLevelDbfs (levelL)
                        + "  pk " + formatLevelDbfs (holdL),
                    readout.removeFromLeft (readout.getWidth() / 2),
                    juce::Justification::centredLeft);
        g.drawText ("R " + formatLevelDbfs (levelR)
                        + "  pk " + formatLevelDbfs (holdR),
                    readout, juce::Justification::centredLeft);

        g.setColour (juce::Colour (HostLookAndFeel::textDim).withAlpha (0.75f));
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("click to clear peak / CLIP",
                    bounds.removeFromBottom (14), juce::Justification::centredLeft);

        bounds.removeFromTop (4);
        paintBar (g, bounds.removeFromTop (18), levelL, holdL, clipped);
        bounds.removeFromTop (6);
        paintBar (g, bounds.removeFromTop (18), levelR, holdR, clipped);
    }

private:
    // Asymmetric ballistics: fast attack so transients register, slower
    // release so the bar is readable — coefficients tuned by eye at the
    // panel's 30 Hz refresh, not derived from any VU standard.
    static float smoothToward (float current, float target)
    {
        constexpr float attack = 0.55f;
        constexpr float release = 0.18f;
        return target > current ? current + (target - current) * attack
                                : current + (target - current) * release;
    }

    static void paintBar (juce::Graphics& g, juce::Rectangle<int> area,
                          float db, float holdDb, bool isClipped)
    {
        auto r = area.toFloat();
        g.setColour (juce::Colour (HostLookAndFeel::outlineDark));
        g.fillRoundedRectangle (r, 3.0f);

        const float norm = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        const float holdNorm = juce::jlimit (0.0f, 1.0f, (holdDb + 60.0f) / 60.0f);
        auto fill = r.reduced (1.5f);
        fill.setWidth (fill.getWidth() * norm);

        juce::ColourGradient grad (juce::Colour (0xff2ecc71), fill.getX(), 0.0f,
                                   juce::Colour (HostLookAndFeel::accentOrange), fill.getRight(), 0.0f, false);
        if (db > -6.0f || isClipped)
            grad = juce::ColourGradient (juce::Colour (0xff2ecc71), fill.getX(), 0.0f,
                                         juce::Colours::red, fill.getRight(), 0.0f, false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (fill, 2.0f);

        const float holdX = r.getX() + 1.5f + (r.getWidth() - 3.0f) * holdNorm;
        g.setColour (isClipped ? juce::Colours::red : juce::Colours::white.withAlpha (0.9f));
        g.fillRect (holdX - 1.0f, r.getY() + 1.5f, 2.0f, r.getHeight() - 3.0f);
    }

    juce::String title;
    float levelL { -100.0f };
    float levelR { -100.0f };
    float holdL { -100.0f };
    float holdR { -100.0f };
    bool clipped { false };
};

/** Send + Return VU panel driven from PluginAudioEngine metering. */
class HardwareLoopMeterPanel : public juce::Component,
                             private juce::Timer
{
public:
    explicit HardwareLoopMeterPanel (PluginAudioEngine& engineIn)
        : engine (engineIn)
    {
        addAndMakeVisible (sendMeter);
        addAndMakeVisible (returnMeter);
        startTimerHz (30);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);
        const int gap = 12;
        const int half = (area.getHeight() - gap) / 2;
        sendMeter.setBounds (area.removeFromTop (half));
        area.removeFromTop (gap);
        returnMeter.setBounds (area);
    }

private:
    // 30 Hz UI-thread poll of the engine's atomic peak values — no audio
    // thread involvement, so a stalled UI can never glitch audio.
    void timerCallback() override
    {
        sendMeter.setLevels (engine.getSendPeakL(), engine.getSendPeakR());
        returnMeter.setLevels (engine.getReturnPeakL(), engine.getReturnPeakR());
    }

    PluginAudioEngine& engine;
    StereoVuMeter sendMeter { "Send" };
    StereoVuMeter returnMeter { "Return" };
};

/**
 * Send + Return VU for the active path: hardware insert peaks when Use
 * Hardware is on, otherwise software pre/post-plugin peaks.
 */
class ActivePathMeterPanel : public juce::Component,
                             private juce::Timer
{
public:
    explicit ActivePathMeterPanel (PluginAudioEngine& engineIn)
        : engine (engineIn)
    {
        addAndMakeVisible (modeLabel);
        modeLabel.setJustificationType (juce::Justification::centredLeft);
        modeLabel.setColour (juce::Label::textColourId, juce::Colour (HostLookAndFeel::textDim));
        modeLabel.setFont (juce::FontOptions (13.0f));
        addAndMakeVisible (sendMeter);
        addAndMakeVisible (returnMeter);
        startTimerHz (30);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);
        modeLabel.setBounds (area.removeFromTop (22));
        area.removeFromTop (8);
        const int gap = 12;
        const int half = (area.getHeight() - gap) / 2;
        sendMeter.setBounds (area.removeFromTop (half));
        area.removeFromTop (gap);
        returnMeter.setBounds (area);
    }

private:
    void timerCallback() override
    {
        const bool hw = engine.isHardwareMode();
        modeLabel.setText (hw ? "Path: Hardware (sample peak)" : "Path: Software (sample peak)",
                           juce::dontSendNotification);

        if (hw)
        {
            sendMeter.setLevels (engine.getSendPeakL(), engine.getSendPeakR());
            returnMeter.setLevels (engine.getReturnPeakL(), engine.getReturnPeakR());
        }
        else
        {
            sendMeter.setLevels (engine.getSoftwareSendPeakL(), engine.getSoftwareSendPeakR());
            returnMeter.setLevels (engine.getSoftwareReturnPeakL(), engine.getSoftwareReturnPeakR());
        }
    }

    PluginAudioEngine& engine;
    juce::Label modeLabel;
    StereoVuMeter sendMeter { "Send" };
    StereoVuMeter returnMeter { "Return" };
};
