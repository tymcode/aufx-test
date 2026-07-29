#pragma once

#include <JuceHeader.h>
#include "HostLookAndFeel.h"
#include "PluginAudioEngine.h"
#include "Utf8.h"

/** Horizontal stereo VU pair (L/R) with peak hold and dB readout. */
class StereoVuMeter : public juce::Component
{
public:
    explicit StereoVuMeter (juce::String titleIn)
        : title (std::move (titleIn))
    {
        setOpaque (true);
    }

    void setLevels (float peakL, float peakR)
    {
        const float dbL = juce::Decibels::gainToDecibels (peakL, -100.0f);
        const float dbR = juce::Decibels::gainToDecibels (peakR, -100.0f);
        levelL = smoothToward (levelL, dbL);
        levelR = smoothToward (levelR, dbR);
        holdL = juce::jmax (holdL - holdDecay, dbL);
        holdR = juce::jmax (holdR - holdDecay, dbR);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (HostLookAndFeel::wellBackground));

        auto bounds = getLocalBounds().reduced (8);
        g.setColour (juce::Colour (HostLookAndFeel::textBright));
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (title, bounds.removeFromTop (20), juce::Justification::centredLeft);

        g.setColour (juce::Colour (HostLookAndFeel::textDim));
        g.setFont (juce::FontOptions (12.0f));
        auto readout = bounds.removeFromTop (18);
        g.drawText ("L " + formatLevelDbfs (levelL), readout.removeFromLeft (readout.getWidth() / 2),
                    juce::Justification::centredLeft);
        g.drawText ("R " + formatLevelDbfs (levelR), readout, juce::Justification::centredLeft);

        bounds.removeFromTop (4);
        paintBar (g, bounds.removeFromTop (18), levelL, holdL);
        bounds.removeFromTop (6);
        paintBar (g, bounds.removeFromTop (18), levelR, holdR);
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

    static void paintBar (juce::Graphics& g, juce::Rectangle<int> area, float db, float holdDb)
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
        if (db > -6.0f)
            grad = juce::ColourGradient (juce::Colour (0xff2ecc71), fill.getX(), 0.0f,
                                         juce::Colours::red, fill.getRight(), 0.0f, false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (fill, 2.0f);

        const float holdX = r.getX() + 1.5f + (r.getWidth() - 3.0f) * holdNorm;
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.fillRect (holdX - 1.0f, r.getY() + 1.5f, 2.0f, r.getHeight() - 3.0f);
    }

    juce::String title;
    float levelL { -100.0f };
    float levelR { -100.0f };
    float holdL { -100.0f };
    float holdR { -100.0f };
    static constexpr float holdDecay = 0.8f;
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
