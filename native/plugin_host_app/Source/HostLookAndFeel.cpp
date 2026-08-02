#include "HostLookAndFeel.h"

namespace
{
    constexpr float kCorner = 3.0f;

    juce::Colour c (juce::uint32 argb) { return juce::Colour (argb); }
}

HostLookAndFeel::HostLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, c (windowBackground));

    setColour (juce::TextButton::buttonColourId,   c (controlFace));
    setColour (juce::TextButton::buttonOnColourId, c (controlFace).brighter (0.15f));
    setColour (juce::TextButton::textColourOffId,  c (textBright));
    setColour (juce::TextButton::textColourOnId,   c (textBright));

    setColour (juce::ComboBox::backgroundColourId, c (wellBackground));
    setColour (juce::ComboBox::textColourId,       c (textBright));
    setColour (juce::ComboBox::outlineColourId,    c (outlineDark));
    setColour (juce::ComboBox::arrowColourId,      c (textDim));

    setColour (juce::TextEditor::backgroundColourId,      c (wellBackground));
    setColour (juce::TextEditor::textColourId,            c (textBright));
    setColour (juce::TextEditor::outlineColourId,         c (outlineDark));
    setColour (juce::TextEditor::focusedOutlineColourId,  c (accentOrange).withAlpha (0.65f));
    setColour (juce::TextEditor::highlightColourId,       c (accentOrange).withAlpha (0.35f));
    setColour (juce::CaretComponent::caretColourId,       c (accentOrange));

    setColour (juce::Label::textColourId, c (textDim));

    setColour (juce::ToggleButton::textColourId, c (textBright));
    setColour (juce::ToggleButton::tickColourId, c (accentOrange));

    setColour (juce::PopupMenu::backgroundColourId,          c (0xff2f2f2f));
    setColour (juce::PopupMenu::textColourId,                c (textBright));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, c (accentOrange).withAlpha (0.85f));
    setColour (juce::PopupMenu::highlightedTextColourId,     juce::Colours::black);

    setColour (juce::ScrollBar::thumbColourId, c (controlFace).brighter (0.3f));
    setColour (juce::TooltipWindow::backgroundColourId, c (0xff1f1f1f));
}

void HostLookAndFeel::drawRaisedBevel (juce::Graphics& g, juce::Rectangle<float> area,
                                       juce::Colour face, float cornerRadius, bool recessed)
{
    // Face with a soft vertical sheen
    juce::ColourGradient grad (face.brighter (recessed ? 0.0f : 0.06f), area.getX(), area.getY(),
                               face.darker (recessed ? 0.02f : 0.10f),  area.getX(), area.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (area, cornerRadius);

    const float edge = 1.0f;
    if (! recessed)
    {
        // Light from top-left, shade at bottom
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.fillRoundedRectangle (area.getX(), area.getY(), area.getWidth(), edge + 0.5f, cornerRadius);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (area.getX(), area.getBottom() - edge, area.getWidth(), edge, cornerRadius);
    }
    else
    {
        // Inner shadow at the top when pressed
        g.setColour (juce::Colours::black.withAlpha (0.30f));
        g.fillRoundedRectangle (area.getX(), area.getY(), area.getWidth(), edge + 1.0f, cornerRadius);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRoundedRectangle (area.getX(), area.getBottom() - edge, area.getWidth(), edge, cornerRadius);
    }
}

juce::Font HostLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::Font (juce::FontOptions (juce::jmin (12.5f, (float) buttonHeight * 0.55f)));
}

void HostLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                            const juce::Colour& backgroundColour,
                                            bool highlighted, bool down)
{
    auto area = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = button.getToggleState();
    const bool recessed = down || on;

    juce::Colour face = backgroundColour;
    if (highlighted && ! down)
        face = face.brighter (0.08f);
    if (recessed)
        face = face.darker (0.15f);

    // Chassis recess outline behind the key
    g.setColour (c (outlineDark));
    g.drawRoundedRectangle (area, kCorner, 1.0f);

    drawRaisedBevel (g, area.reduced (0.5f), face, kCorner, recessed);
}

void HostLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool /*isButtonDown*/,
                                    int /*buttonX*/, int /*buttonY*/, int /*buttonW*/, int /*buttonH*/,
                                    juce::ComboBox& box)
{
    auto area = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);

    // Inset well
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (area, kCorner);

    // Inner shadow (top) + light lip (bottom)
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (area.getX() + 1.0f, area.getY() + 1.0f, area.getWidth() - 2.0f, 1.5f, kCorner - 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillRoundedRectangle (area.getX() + 1.0f, area.getBottom() - 1.5f, area.getWidth() - 2.0f, 1.0f, kCorner - 1.0f);

    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (area, kCorner, 1.0f);

    // Chevron
    const float cx = (float) width - 11.0f;
    const float cy = (float) height * 0.5f;
    juce::Path chevron;
    chevron.startNewSubPath (cx - 3.5f, cy - 2.0f);
    chevron.lineTo (cx, cy + 2.0f);
    chevron.lineTo (cx + 3.5f, cy - 2.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId)
                    .withAlpha (box.isEnabled() ? 0.9f : 0.3f));
    g.strokePath (chevron, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
}

juce::Font HostLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (juce::FontOptions (12.5f));
}

void HostLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 1, box.getWidth() - 24, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
}

void HostLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height,
                                                juce::TextEditor& editor)
{
    auto area = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);

    g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (area, kCorner);

    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (area.getX() + 1.0f, area.getY() + 1.0f, area.getWidth() - 2.0f, 1.5f, kCorner - 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillRoundedRectangle (area.getX() + 1.0f, area.getBottom() - 1.5f, area.getWidth() - 2.0f, 1.0f, kCorner - 1.0f);
}

void HostLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height,
                                             juce::TextEditor& editor)
{
    auto area = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);

    if (editor.isEnabled() && editor.hasKeyboardFocus (true) && ! editor.isReadOnly())
        g.setColour (editor.findColour (juce::TextEditor::focusedOutlineColourId));
    else
        g.setColour (editor.findColour (juce::TextEditor::outlineColourId));

    g.drawRoundedRectangle (area, kCorner, 1.0f);
}

juce::Font HostLookAndFeel::getLabelFont (juce::Label& label)
{
    return label.getFont().withHeight (juce::jmin (12.5f, label.getFont().getHeight()));
}

void HostLookAndFeel::drawTickBox (juce::Graphics& g, juce::Component& component,
                                   float x, float y, float w, float h,
                                   bool ticked, bool isEnabled,
                                   bool /*highlighted*/, bool down)
{
    auto well = juce::Rectangle<float> (x, y, w, h).reduced (1.0f);

    // Inset well, like the text fields
    g.setColour (c (wellBackground).darker (down ? 0.1f : 0.0f));
    g.fillRoundedRectangle (well, 2.5f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (well.getX() + 1.0f, well.getY() + 1.0f, well.getWidth() - 2.0f, 1.2f, 1.5f);
    g.setColour (c (outlineDark));
    g.drawRoundedRectangle (well, 2.5f, 1.0f);

    if (ticked)
    {
        auto tick = well.reduced (well.getWidth() * 0.22f);
        auto colour = component.findColour (juce::ToggleButton::tickColourId)
                          .withAlpha (isEnabled ? 1.0f : 0.4f);

        // Glowing indicator dot with a soft highlight, like the LEDs
        g.setColour (colour);
        g.fillRoundedRectangle (tick, 1.8f);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.fillRoundedRectangle (tick.reduced (tick.getWidth() * 0.28f)
                                     .translated (0.0f, -tick.getHeight() * 0.12f), 1.2f);
    }
}

juce::AlertWindow* HostLookAndFeel::createAlertWindow (const juce::String& title,
                                                       const juce::String& message,
                                                       const juce::String& button1,
                                                       const juce::String& button2,
                                                       const juce::String& button3,
                                                       juce::MessageBoxIconType iconType,
                                                       int numButtons,
                                                       juce::Component* associatedComponent)
{
    auto* aw = new juce::AlertWindow (title, message, iconType, associatedComponent);

    auto disableCancelFocus = [aw]()
    {
        for (int i = 0; i < aw->getNumChildComponents(); ++i)
            if (auto* button = dynamic_cast<juce::TextButton*> (aw->getChildComponent (i)))
                if (button->getButtonText().equalsIgnoreCase ("Cancel")
                    || button->getCommandID() == 0)
                    button->setWantsKeyboardFocus (false);
    };

    if (numButtons == 1)
    {
        aw->addButton (button1, 0,
                       juce::KeyPress (juce::KeyPress::escapeKey),
                       juce::KeyPress (juce::KeyPress::returnKey));
    }
    else if (numButtons == 2)
    {
        // MessageBoxOptions: button1 = confirm (result 1), button2 = cancel (result 0).
        // Add Cancel first so it is leftmost.
        juce::KeyPress button1ShortCut ((int) juce::CharacterFunctions::toLowerCase (button1[0]), 0, 0);
        juce::KeyPress button2ShortCut ((int) juce::CharacterFunctions::toLowerCase (button2[0]), 0, 0);
        if (button1ShortCut == button2ShortCut)
            button2ShortCut = {};

        aw->addButton (button2, 0, juce::KeyPress (juce::KeyPress::escapeKey), button2ShortCut);
        aw->addButton (button1, 1, juce::KeyPress (juce::KeyPress::returnKey), button1ShortCut);
        disableCancelFocus();
    }
    else if (numButtons == 3)
    {
        juce::KeyPress button1ShortCut ((int) juce::CharacterFunctions::toLowerCase (button1[0]), 0, 0);
        juce::KeyPress button2ShortCut ((int) juce::CharacterFunctions::toLowerCase (button2[0]), 0, 0);
        if (button1ShortCut == button2ShortCut)
            button2ShortCut = {};

        // button3 = cancel (0), button2 = secondary (2), button1 = primary (1).
        aw->addButton (button3, 0, juce::KeyPress (juce::KeyPress::escapeKey));
        aw->addButton (button2, 2, button2ShortCut);
        aw->addButton (button1, 1, button1ShortCut);
        disableCancelFocus();
    }

    // Match LookAndFeel_V4 alert padding.
    constexpr int boundsOffset = 50;
    auto bounds = aw->getBounds();
    bounds = bounds.withSizeKeepingCentre (bounds.getWidth() + boundsOffset,
                                           bounds.getHeight() + boundsOffset);
    aw->setBounds (bounds);

    for (auto* child : aw->getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*> (child))
            button->setBounds (button->getBounds() + juce::Point<int> (25, 40));

    return aw;
}
