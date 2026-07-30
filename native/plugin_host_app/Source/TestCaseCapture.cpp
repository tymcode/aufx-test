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

    auto* reportToggle = new juce::ToggleButton();
    reportToggle->setName ({});
    reportToggle->setButtonText ("Generate report");
    const bool storedGenerateReport = HostPreferences::get().getCaptureGenerateReport();
    reportToggle->setToggleState (storedGenerateReport, juce::dontSendNotification);
    reportToggle->setTooltip (
        "After a successful capture, run aufx-test compare with --write-report "
        "for this snapshot (JSON, plots, and HTML in the stem folder).");
    reportToggle->setSize (280, 24);
    aw->addCustomComponent (reportToggle);

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
                              capture = this, aw, calibrateToggle, calibrateState, reportToggle,
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

                                 const bool generateReport = reportToggle->getToggleState();
                                 HostPreferences::get().setCaptureGenerateReport (generateReport);

                                 capture->lastCaptureDescription = description;
                                 capture->lastCaptureRoleIndex = roleIndex;
                                 capture->lastCaptureSourceIndex = sourceIndex;
                                 dialog.reset();

                                 const auto fixtureFile = sourceClips.getSelectedFile (fixtureBox);
                                 capture->capture (description, roleIndex, sourceIndex, calibrate,
                                                   generateReport, fixtureFile, safeParent.getComponent());
                             }),
                         true);
}

void TestCaseCapture::capture (const juce::String& snapshotName, int roleIndex, int sourceIndex,
                               bool calibrateNoiseFloor,
                               bool generateReport,
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

    juce::String snapshotId;
    if (! SessionSnap::registerSnapshot (snap, error, &snapshotId))
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

    if (generateReport)
        runCompareReport (snap.sessionName, snapshotId, snapshotName);
}

void TestCaseCapture::runCompareReport (const juce::String& sessionName,
                                        const juce::String& snapshotId,
                                        const juce::String& snapshotName)
{
    if (config.pythonCli == juce::File() || ! config.pythonCli.existsAsFile())
    {
        setStatus ("Capture succeeded, but Generate report needs python_cli in host.config.json", true);
        return;
    }

    if (snapshotId.isEmpty())
    {
        setStatus ("Capture succeeded, but snapshot id is missing for report generation", true);
        return;
    }

    setStatus ("Generating compare report for " + snapshotName + utf8 ("…"), false);

    const auto pythonCli = config.pythonCli;
    const auto sessionsRoot = config.sessionsRoot;
    const auto setStatusFn = setStatus;

    juce::Thread::launch ([pythonCli, sessionsRoot, sessionName, snapshotId, snapshotName, setStatusFn]
    {
        juce::StringArray args;
        args.add (pythonCli.getFullPathName());
        args.add ("compare");
        args.add ("--root");
        args.add (sessionsRoot.getFullPathName());
        args.add (sessionName);
        args.add (snapshotId);
        args.add ("--write-report");

        juce::ChildProcess process;
        juce::String detail;
        bool ok = false;

        if (! process.start (args))
        {
            detail = "failed to start " + pythonCli.getFullPathName();
        }
        else
        {
            // Blocks this worker until the CLI exits. Exit 1 can mean gated
            // compare FAILED while --write-report still wrote artifacts.
            const auto output = process.readAllProcessOutput();
            const auto exitCode = process.getExitCode();

            const auto sessionDir = sessionsRoot.getChildFile (HostConfig::slugify (sessionName));
            juce::String findError;
            SessionSnapshotRef ref;
            if (SessionSnap::findSnapshot (sessionDir, snapshotId, ref, findError))
            {
                juce::File stemDir;
                if (ref.outputAudio.existsAsFile())
                    stemDir = ref.outputAudio.getParentDirectory();
                else if (ref.hardwareOutputAudio.existsAsFile())
                    stemDir = ref.hardwareOutputAudio.getParentDirectory();

                ok = stemDir != juce::File()
                     && stemDir.getChildFile ("compare_report.html").existsAsFile();
            }

            if (! ok)
            {
                if (output.trim().isNotEmpty())
                    detail = output.trim().upToFirstOccurrenceOf ("\n", false, false);
                else
                    detail = "aufx-test compare exited with code " + juce::String (exitCode);
            }
        }

        juce::MessageManager::callAsync ([setStatusFn, snapshotName, ok, detail]
        {
            if (ok)
                setStatusFn ("Captured test case: " + snapshotName + " (report written)", false);
            else
                setStatusFn ("Capture succeeded, but report failed: " + detail, true);
        });
    });
}
