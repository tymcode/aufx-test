#include "LevelMetersWindow.h"
#include "HardwareVuMeters.h"
#include "LevelSweepCalibration.h"
#include "Utf8.h"

namespace
{
    class LevelMetersPanel : public juce::Component,
                             private juce::Button::Listener,
                             private juce::KeyListener
    {
    public:
        LevelMetersPanel (PluginAudioEngine& engineIn,
                          juce::File fixturesDirIn,
                          juce::File calibrationDirIn,
                          juce::File pythonCliIn,
                          LevelMetersWindow::KeyHandlerFn keyHandlerIn)
            : engine (engineIn),
              fixturesDir (std::move (fixturesDirIn)),
              calibrationDir (std::move (calibrationDirIn)),
              pythonCli (std::move (pythonCliIn)),
              meters (engineIn),
              keyHandler (std::move (keyHandlerIn))
        {
            addAndMakeVisible (meters);

            sweepButton.setButtonText (utf8 ("Level sweep…"));
            sweepButton.setTooltip (
                "Play a 0 dBFS sine at stepped send gains through the active path "
                "(hardware insert or software plugin with current bypass/mix), "
                "then write a named plot under calibration/.");
            sweepButton.addListener (this);
            addAndMakeVisible (sweepButton);

            statusLabel.setJustificationType (juce::Justification::centredLeft);
            statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xffaaaaaa));
            statusLabel.setFont (juce::FontOptions (12.0f));
            addAndMakeVisible (statusLabel);

            setSize (380, 480);
            setWantsKeyboardFocus (true);
            addKeyListener (this);
        }

        ~LevelMetersPanel() override
        {
            removeKeyListener (this);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (8);
            auto bottom = area.removeFromBottom (70);
            statusLabel.setBounds (bottom.removeFromBottom (28));
            bottom.removeFromBottom (6);
            sweepButton.setBounds (bottom.removeFromLeft (140).withHeight (28));
            meters.setBounds (area);
        }

    private:
        using juce::Component::keyPressed;

        bool keyPressed (const juce::KeyPress& key, juce::Component*) override
        {
            return keyHandler && keyHandler (key);
        }

        void buttonClicked (juce::Button* button) override
        {
            if (button == &sweepButton)
                promptAndRunSweep();
        }

        void promptAndRunSweep()
        {
            auto* aw = new juce::AlertWindow ("Level sweep",
                                              "Name this calibration plot. Files are written under calibration/.",
                                              juce::MessageBoxIconType::QuestionIcon,
                                              this);
            const auto defaultName = "level_sweep_"
                                     + juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S");
            aw->addTextEditor ("name", defaultName, "Plot name");
            aw->addButton ("Run", 1, juce::KeyPress (juce::KeyPress::returnKey));
            aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

            aw->enterModalState (true,
                                 juce::ModalCallbackFunction::create (
                                     [safe = juce::Component::SafePointer<LevelMetersPanel> (this), aw] (int result)
                                     {
                                         std::unique_ptr<juce::AlertWindow> dialog (aw);
                                         if (safe == nullptr || result != 1)
                                             return;

                                         const auto name = dialog->getTextEditorContents ("name").trim();
                                         dialog.reset();
                                         if (name.isEmpty())
                                         {
                                             safe->statusLabel.setText ("Plot name is required",
                                                                        juce::dontSendNotification);
                                             return;
                                         }
                                         safe->runSweep (name);
                                     }),
                                 true);
        }

        void runSweep (const juce::String& plotName)
        {
            const auto sine = fixturesDir.getChildFile ("synth_waves")
                                  .getChildFile ("sine_0db_1ch_5s_48k.wav");
            if (! sine.existsAsFile())
            {
                statusLabel.setText ("sine_0db_1ch_5s_48k.wav not found under fixtures/synth_waves",
                                     juce::dontSendNotification);
                return;
            }

            const auto path = engine.isHardwareMode() ? LevelSweepCalibration::Path::hardware
                                                       : LevelSweepCalibration::Path::software;

            if (path == LevelSweepCalibration::Path::hardware)
            {
                if (! engine.hasHardwareLoopConfigured())
                {
                    statusLabel.setText ("Configure Hardware Audio Setup before a hardware sweep",
                                         juce::dontSendNotification);
                    return;
                }

                juce::String error;
                if (engine.getDeviceSampleRate() <= 1.0)
                {
                    auto settings = engine.getHardwareLoopSettings();
                    engine.setHardwareLoopSettings (settings);
                    if (! engine.startAudioDevice (error))
                    {
                        statusLabel.setText ("Could not open device: " + error, juce::dontSendNotification);
                        return;
                    }
                }
            }

            sweepButton.setEnabled (false);
            LevelSweepCalibration::Result result;
            juce::String sweepError;
            const bool ok = LevelSweepCalibration::run (
                engine,
                sine,
                calibrationDir,
                pythonCli,
                path,
                plotName,
                result,
                sweepError,
                [this] (int index, int total, float sendDb)
                {
                    statusLabel.setText ("Level sweep " + juce::String (index) + "/" + juce::String (total)
                                             + " (" + juce::String (sendDb, 1) + " dB)"
                                             + utf8 ("…"),
                                         juce::dontSendNotification);
                    if (auto* peer = getPeer())
                        peer->performAnyPendingRepaintsNow();
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
                });

            sweepButton.setEnabled (true);

            if (! ok)
            {
                statusLabel.setText ("Level sweep failed: " + sweepError, juce::dontSendNotification);
                return;
            }

            juce::String summary = "Saved " + result.jsonFile.getFileName()
                                   + " " + utf8 ("—") + " max |peak"
                                   + utf8 ("−") + "send| "
                                   + juce::String (result.maxAbsMeasuredMinusIdealPeak, 2) + " dB";
            if (result.anyClipped)
                summary += " CLIP";
            if (! result.wrotePng)
                summary += " (PNG skipped: " + sweepError + ")";
            statusLabel.setText (summary, juce::dontSendNotification);

            if (result.wrotePng && result.pngFile.existsAsFile())
                result.pngFile.revealToUser();
            else if (result.jsonFile.existsAsFile())
                result.jsonFile.revealToUser();
        }

        PluginAudioEngine& engine;
        juce::File fixturesDir;
        juce::File calibrationDir;
        juce::File pythonCli;
        ActivePathMeterPanel meters;
        juce::TextButton sweepButton;
        juce::Label statusLabel;
        LevelMetersWindow::KeyHandlerFn keyHandler;
    };
}

LevelMetersWindow::LevelMetersWindow (PluginAudioEngine& engine,
                                      juce::File fixturesDir,
                                      juce::File calibrationDir,
                                      juce::File pythonCli,
                                      ClosedFn onClosedIn,
                                      KeyHandlerFn keyHandlerIn)
    : DocumentWindow ("Level Meters",
                      juce::Desktop::getInstance().getDefaultLookAndFeel()
                          .findColour (juce::ResizableWindow::backgroundColourId),
                      DocumentWindow::closeButton),
      onClosed (std::move (onClosedIn)),
      keyHandler (std::move (keyHandlerIn))
{
    setUsingNativeTitleBar (true);
    setResizable (true, true);
    setWantsKeyboardFocus (true);
    setContentOwned (new LevelMetersPanel (engine,
                                           std::move (fixturesDir),
                                           std::move (calibrationDir),
                                           std::move (pythonCli),
                                           keyHandler),
                     true);
    centreWithSize (400, 500);
    setVisible (true);
    setAlwaysOnTop (false);
    toFront (true);
}

LevelMetersWindow::~LevelMetersWindow()
{
    clearContentComponent();
}

void LevelMetersWindow::closeButtonPressed()
{
    setVisible (false);
    if (onClosed)
        onClosed();
}

bool LevelMetersWindow::keyPressed (const juce::KeyPress& key)
{
    if (keyHandler && keyHandler (key))
        return true;
    return DocumentWindow::keyPressed (key);
}
