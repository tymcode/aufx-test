#include "TestCaseCapture.h"
#include "SessionSnap.h"

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
    if (auto* sourceBox = aw->getComboBoxComponent ("source"))
        sourceBox->setSelectedItemIndex (juce::jlimit (0, 2, lastCaptureSourceIndex),
                                         juce::dontSendNotification);

    aw->addButton ("Capture", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    aw->enterModalState (true,
                         juce::ModalCallbackFunction::create (
                             [safeParent = juce::Component::SafePointer<juce::Component> (parent),
                              capture = this, aw, &sourceClips, &fixtureBox] (int result)
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

                                 capture->lastCaptureDescription = description;
                                 capture->lastCaptureRoleIndex = roleIndex;
                                 capture->lastCaptureSourceIndex = sourceIndex;
                                 dialog.reset();

                                 const auto fixtureFile = sourceClips.getSelectedFile (fixtureBox);
                                 capture->capture (description, roleIndex, sourceIndex,
                                                   fixtureFile, safeParent.getComponent());
                             }),
                         true);
}

void TestCaseCapture::capture (const juce::String& snapshotName, int roleIndex, int sourceIndex,
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

    setStatus ("Captured test case: " + snapshotName, false);
}
