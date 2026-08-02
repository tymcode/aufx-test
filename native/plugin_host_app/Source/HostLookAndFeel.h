#pragma once

#include <JuceHeader.h>

/**
    App-wide look and feel: dark charcoal chassis with subtly 3D controls.

    Buttons read as slightly raised keys (light from the top-left) and recess
    when pressed; combo boxes and text editors read as inset wells. Matches
    the TR-808-style metronome key at a lower intensity.
*/
class HostLookAndFeel : public juce::LookAndFeel_V4
{
public:
    HostLookAndFeel();

    // Palette shared with custom-painted components.
    static constexpr juce::uint32 windowBackground = 0xff2b2b2b;
    static constexpr juce::uint32 controlFace      = 0xff3d3d3d;
    static constexpr juce::uint32 wellBackground   = 0xff232323;
    static constexpr juce::uint32 outlineDark      = 0xff191919;
    static constexpr juce::uint32 textBright       = 0xffe6e6e6;
    static constexpr juce::uint32 textDim          = 0xffb8b8b8;
    static constexpr juce::uint32 accentOrange     = 0xffff6a2a;

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

    juce::Font getLabelFont (juce::Label&) override;

    void drawTickBox (juce::Graphics&, juce::Component&,
                      float x, float y, float w, float h,
                      bool ticked, bool isEnabled,
                      bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    /** Cancel is leftmost; primary/confirm stays rightmost with Return. */
    juce::AlertWindow* createAlertWindow (const juce::String& title,
                                          const juce::String& message,
                                          const juce::String& button1,
                                          const juce::String& button2,
                                          const juce::String& button3,
                                          juce::MessageBoxIconType iconType,
                                          int numButtons,
                                          juce::Component* associatedComponent) override;

private:
    static void drawRaisedBevel (juce::Graphics&, juce::Rectangle<float> area,
                                 juce::Colour face, float cornerRadius, bool recessed);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HostLookAndFeel)
};
