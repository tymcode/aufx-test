#include "AboutDialog.h"
#include "AppVersion.h"
#include "BinaryData.h"
#include "Utf8.h"

namespace
{
    class AboutPanel : public juce::Component
    {
    public:
        AboutPanel()
        {
            titleLabel.setText (AUFX_APP_NAME, juce::dontSendNotification);
            titleLabel.setFont (juce::FontOptions (22.0f, juce::Font::bold));
            titleLabel.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (titleLabel);

            versionLabel.setText ("Version " + juce::String (AUFX_VERSION_STRING), juce::dontSendNotification);
            versionLabel.setFont (juce::FontOptions (14.0f));
            versionLabel.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (versionLabel);

            copyrightLabel.setText (utf8 ("©") + juce::String (AUFX_BUILD_YEAR) + " by Mike Jennings",
                                    juce::dontSendNotification);
            copyrightLabel.setFont (juce::FontOptions (13.0f));
            copyrightLabel.setJustificationType (juce::Justification::centred);
            copyrightLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
            addAndMakeVisible (copyrightLabel);

            attributionLink.setButtonText ("Some source clips by kind courtesy of VintageDigital.com.au");
            attributionLink.setURL (juce::URL ("https://vintagedigital.com.au"));
            attributionLink.setFont (juce::FontOptions (12.0f), false);
            attributionLink.setJustificationType (juce::Justification::centred);
            attributionLink.setColour (juce::HyperlinkButton::textColourId, juce::Colour (0xff6699cc));
            addAndMakeVisible (attributionLink);

            auto image = juce::ImageFileFormat::loadFrom (BinaryData::app_icon_png,
                                                          BinaryData::app_icon_pngSize);
            if (image.isValid())
                icon = image.rescaled (256, 256, juce::Graphics::highResamplingQuality);

            setSize (340, 420);
        }

        void paint (juce::Graphics& g) override
        {
            if (icon.isValid())
            {
                const int x = (getWidth() - 256) / 2;
                g.drawImageAt (icon, x, 12);
            }
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (16);
            area.removeFromTop (256 + 16);
            titleLabel.setBounds (area.removeFromTop (28));
            area.removeFromTop (6);
            versionLabel.setBounds (area.removeFromTop (22));
            area.removeFromTop (10);
            copyrightLabel.setBounds (area.removeFromTop (26));
            area.removeFromTop (8);
            attributionLink.setBounds (area.removeFromTop (36));
        }

    private:
        juce::Image icon;
        juce::Label titleLabel;
        juce::Label versionLabel;
        juce::Label copyrightLabel;
        juce::HyperlinkButton attributionLink { {}, juce::URL ("https://vintagedigital.com.au") };
    };
}

void showAboutDialog (juce::Component* centreAround)
{
    AboutPanel panel;

    juce::AlertWindow window ("About AU Effects Explorer",
                              {},
                              juce::MessageBoxIconType::NoIcon,
                              centreAround);
    window.addCustomComponent (&panel);
    window.addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window.runModalLoop();
}
