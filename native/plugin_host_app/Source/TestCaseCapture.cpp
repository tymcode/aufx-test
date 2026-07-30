#include "TestCaseCapture.h"
#include "HostPreferences.h"
#include "SessionSnap.h"
#include "Utf8.h"

TestCaseCapture::TestCaseCapture (PluginAudioEngine& audioEngine,
                                  HostConfig& hostConfig,
                                  StatusFn statusFn,
                                  GetPluginEntryFn getPluginEntry,
                                  CapturePipeline::RefreshUiFn refreshHardwareUiIn,
                                  PopulateHardwareStatesFn populateHardwareStatesIn,
                                  SetLightsOutFn setLightsOutIn)
    : engine (audioEngine),
      config (hostConfig),
      setStatus (std::move (statusFn)),
      getCurrentPlugin (std::move (getPluginEntry)),
      populateHardwareStates (std::move (populateHardwareStatesIn)),
      setLightsOut (std::move (setLightsOutIn)),
      pipeline (audioEngine, hostConfig, std::move (refreshHardwareUiIn))
{
}

void TestCaseCapture::prompt (juce::Component* parent,
                              SourceClipLibrary& sourceClips,
                              juce::ComboBox& fixtureBox)
{
    const auto selected = sourceClips.getSelectedFile (fixtureBox);
    if (! selected.existsAsFile())
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
                                      parent);

    aw->addTextEditor ("description",
                       lastCaptureDescription.isNotEmpty() ? lastCaptureDescription : "snapshot",
                       "Description");
    aw->addComboBox ("role", { "golden", "suspect", "broken" }, "Type");
    aw->addComboBox ("source", { "Rendered plugin", "Hardware", "Both" }, "Capture");

    if (auto* roleBox = aw->getComboBoxComponent ("role"))
        roleBox->setSelectedItemIndex (juce::jlimit (0, 2, lastCaptureRoleIndex),
                                       juce::dontSendNotification);

    auto* calibrateToggle = new juce::ToggleButton();
    // AlertWindow paints getName() as a heading above custom components — keep
    // the name empty and put the label only on the checkbox itself.
    calibrateToggle->setName ({});
    calibrateToggle->setButtonText ("Calibrate");
    const bool storedCalibrate = HostPreferences::get().getHardwareCaptureCalibrate();
    calibrateToggle->setTooltip (
        utf8 ("Measure the hardware noise floor before recording so silence detection "
              "can end reverb tails. Turn off to reuse the last gate if levels are unchanged."));
    calibrateToggle->setSize (280, 24);
    aw->addCustomComponent (calibrateToggle);

    // Heap so the modal callback and combo onChange share the same remembered state.
    auto calibrateState = std::make_shared<bool> (storedCalibrate);

    auto syncCalibrateForSource = [calibrateToggle, calibrateState] (int sourceIndex)
    {
        const bool hardwareCapture = sourceIndex == 1 || sourceIndex == 2;
        if (hardwareCapture)
        {
            calibrateToggle->setEnabled (true);
            calibrateToggle->setAlpha (1.0f);
            calibrateToggle->setToggleState (*calibrateState, juce::dontSendNotification);
        }
        else
        {
            if (calibrateToggle->isEnabled())
                *calibrateState = calibrateToggle->getToggleState();
            calibrateToggle->setEnabled (false);
            calibrateToggle->setAlpha (0.45f);
            calibrateToggle->setToggleState (false, juce::dontSendNotification);
        }
    };

    calibrateToggle->onClick = [calibrateToggle, calibrateState]
    {
        if (calibrateToggle->isEnabled())
            *calibrateState = calibrateToggle->getToggleState();
    };

    if (auto* sourceBox = aw->getComboBoxComponent ("source"))
    {
        sourceBox->setSelectedItemIndex (juce::jlimit (0, 2, lastCaptureSourceIndex),
                                         juce::dontSendNotification);
        sourceBox->onChange = [sourceBox, syncCalibrateForSource]
        {
            syncCalibrateForSource (juce::jmax (0, sourceBox->getSelectedItemIndex()));
        };
        syncCalibrateForSource (juce::jmax (0, sourceBox->getSelectedItemIndex()));
    }
    else
    {
        syncCalibrateForSource (lastCaptureSourceIndex);
    }

    aw->addButton ("Capture", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    aw->enterModalState (true,
                         juce::ModalCallbackFunction::create (
                             [safeParent = juce::Component::SafePointer<juce::Component> (parent),
                              capture = this, aw, calibrateToggle, calibrateState,
                              &sourceClips, &fixtureBox] (int result)
                             {
                                 std::unique_ptr<juce::AlertWindow> dialog (aw);

                                 if (safeParent == nullptr || result != 1)
                                     return;

                                 const auto description = dialog->getTextEditorContents ("description").trim();
                                 int roleIndex = 2;
                                 if (auto* roleBox = dialog->getComboBoxComponent ("role"))
                                     roleIndex = juce::jmax (0, roleBox->getSelectedItemIndex());

                                 int sourceIndex = 0;
                                 if (auto* sourceBox = dialog->getComboBoxComponent ("source"))
                                     sourceIndex = juce::jmax (0, sourceBox->getSelectedItemIndex());

                                 const bool hardwareCapture = sourceIndex == 1 || sourceIndex == 2;
                                 bool calibrate = false;
                                 if (hardwareCapture)
                                 {
                                     calibrate = calibrateToggle->isEnabled()
                                                     ? calibrateToggle->getToggleState()
                                                     : *calibrateState;
                                     *calibrateState = calibrate;
                                     HostPreferences::get().setHardwareCaptureCalibrate (calibrate);
                                 }

                                 capture->lastCaptureDescription = description;
                                 capture->lastCaptureRoleIndex = roleIndex;
                                 capture->lastCaptureSourceIndex = sourceIndex;
                                 dialog.reset();

                                 const auto fixtureFile = sourceClips.getSelectedFile (fixtureBox);
                                 capture->capture (description, roleIndex, sourceIndex, calibrate,
                                                   fixtureFile, safeParent.getComponent());
                             }),
                         true);
}

void TestCaseCapture::capture (const juce::String& snapshotName, int roleIndex, int sourceIndex,
                               bool calibrateNoiseFloor,
                               const juce::File& fixtureFile,
                               juce::Component* progressParent)
{
    if (snapshotName.isEmpty())
    {
        setStatus ("Description is required", true);
        return;
    }

    if (! fixtureFile.existsAsFile())
    {
        setStatus ("Select a source clip before capturing", true);
        return;
    }

    CapturePipelineRequest request;
    request.description = snapshotName;
    request.roleIndex = roleIndex;
    request.source = static_cast<CaptureSource> (juce::jlimit (0, 2, sourceIndex));
    request.fixtureFile = fixtureFile;
    request.progressParent = progressParent;
    request.calibrateNoiseFloor = calibrateNoiseFloor;

    CapturePipelineResult result;
    juce::String error;
    if (! pipeline.run (request, getCurrentPlugin(), result, error))
    {
        setStatus (error, true);
        return;
    }

    SessionSnapRequest snap;
    snap.sessionsRoot = config.sessionsRoot;
    snap.sessionName = getCurrentPlugin().sessionName;
    snap.snapshotName = snapshotName;
    snap.sourceClipName = request.fixtureFile.getFileNameWithoutExtension();
    snap.inputFile = request.fixtureFile;
    if (result.capturedPlugin)
    {
        snap.outputFile = result.paths.softwareOutput;
        snap.presetFile = result.paths.presetFile;
    }
    if (result.capturedHardware)
    {
        snap.hardwareOutputFile = result.paths.hardwareOutput;
        if (result.capturedSysex)
            snap.sysexFile = result.paths.sysexFile;
    }
    snap.pluginPath = getCurrentPlugin().identifierForLoad();
    snap.notes = "Captured from AU Effects Explorer";

    if (! SessionSnap::registerSnapshot (snap, error))
    {
        setStatus ("Capture saved to disk but session update failed: " + error, true);
        return;
    }

    if (engine.isHardwareMode())
        populateHardwareStates();

    setLightsOut (false);
    result.paths.captureDir.revealToUser();

    juce::String status = "Captured test case: " + snapshotName;
    if (result.calibratedNoiseFloor)
        status += " (silence gate " + juce::String (result.hardwareSilenceThresholdDb, 1) + " dBFS)";
    setStatus (status, false);
}
