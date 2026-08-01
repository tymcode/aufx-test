#include "TestCaseCapture.h"
#include "HostPreferences.h"
#include "SessionSnap.h"
#include "Utf8.h"

namespace
{
    /** Two-column options row for Capture Test Case. */
    class CaptureOptionsPanel : public juce::Component
    {
    public:
        CaptureOptionsPanel()
        {
            calibrateToggle.setButtonText ("Calibrate");
            calibrateToggle.setTooltip (
                utf8 ("Before recording: measure the hardware noise floor (for silence detection) "
                      "and subtract DC offset from the capture. Turn off to reuse the last silence "
                      "gate if levels are unchanged (DC removal is skipped when off)."));
            addAndMakeVisible (calibrateToggle);

            reportToggle.setButtonText ("Generate report");
            reportToggle.setTooltip (
                "After a successful capture, run aufx-test compare with --write-report "
                "for this snapshot (JSON, plots, and HTML in the stem folder).");
            addAndMakeVisible (reportToggle);

            settingsHeading.setText ("Capture settings:", juce::dontSendNotification);
            settingsHeading.setJustificationType (juce::Justification::centredLeft);
            settingsHeading.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.9f));
            addAndMakeVisible (settingsHeading);

            softwareSettingsToggle.setButtonText ("Software");
            softwareSettingsToggle.setTooltip (
                "Save a .aupreset from the plugin's current settings "
                "(not the most recently loaded preset file).");
            addAndMakeVisible (softwareSettingsToggle);

            hardwareSettingsToggle.setButtonText ("Hardware");
            hardwareSettingsToggle.setTooltip (
                "Request a MIDI sysex patch dump from the hardware device "
                "(requires MIDI out in MIDI Setup).");
            addAndMakeVisible (hardwareSettingsToggle);

            setSize (440, 78);
        }

        void resized() override
        {
            auto area = getLocalBounds();
            auto left = area.removeFromLeft (area.getWidth() / 2).reduced (0, 2);
            auto right = area.reduced (8, 2);

            calibrateToggle.setBounds (left.removeFromTop (24));
            left.removeFromTop (4);
            reportToggle.setBounds (left.removeFromTop (24));

            settingsHeading.setBounds (right.removeFromTop (20));
            softwareSettingsToggle.setBounds (right.removeFromTop (24));
            right.removeFromTop (2);
            hardwareSettingsToggle.setBounds (right.removeFromTop (24));
        }

        void setHardwareSettingsAvailable (bool available, bool preferredOn)
        {
            hardwareSettingsToggle.setEnabled (available);
            hardwareSettingsToggle.setAlpha (available ? 1.0f : 0.45f);
            hardwareSettingsToggle.setToggleState (available && preferredOn,
                                                   juce::dontSendNotification);
        }

        juce::ToggleButton calibrateToggle;
        juce::ToggleButton reportToggle;
        juce::Label settingsHeading;
        juce::ToggleButton softwareSettingsToggle;
        juce::ToggleButton hardwareSettingsToggle;
    };
}

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

    auto* options = new CaptureOptionsPanel();
    // AlertWindow paints getName() as a heading above custom components.
    options->setName ({});
    aw->addCustomComponent (options);

    const bool hardwareConfigured = engine.hasHardwareLoopConfigured();
    const bool storedCalibrate = HostPreferences::get().getHardwareCaptureCalibrate();
    const bool storedGenerateReport = HostPreferences::get().getCaptureGenerateReport();
    const bool storedSoftwareSettings = HostPreferences::get().getCaptureSoftwareSettings();
    const bool storedHardwareSettings = HostPreferences::get().getCaptureHardwareSettings();
    const bool midiConfigured = HostPreferences::get().getMidiOutIdentifier().isNotEmpty();
    // Sysex dump only makes sense with a configured insert loop + MIDI out.
    const bool hardwareSettingsAvailable = hardwareConfigured && midiConfigured;

    options->reportToggle.setToggleState (storedGenerateReport, juce::dontSendNotification);
    options->softwareSettingsToggle.setToggleState (storedSoftwareSettings, juce::dontSendNotification);
    options->setHardwareSettingsAvailable (hardwareSettingsAvailable, storedHardwareSettings);

    // Heap so the modal callback and combo onChange share the same remembered state.
    auto calibrateState = std::make_shared<bool> (storedCalibrate);
    auto hardwareSettingsState = std::make_shared<bool> (storedHardwareSettings && hardwareSettingsAvailable);

    auto syncCalibrateForSource = [options, calibrateState] (int sourceIndex)
    {
        const bool hardwareCapture = sourceIndex == 1 || sourceIndex == 2;
        if (hardwareCapture)
        {
            options->calibrateToggle.setEnabled (true);
            options->calibrateToggle.setAlpha (1.0f);
            options->calibrateToggle.setToggleState (*calibrateState, juce::dontSendNotification);
        }
        else
        {
            if (options->calibrateToggle.isEnabled())
                *calibrateState = options->calibrateToggle.getToggleState();
            options->calibrateToggle.setEnabled (false);
            options->calibrateToggle.setAlpha (0.45f);
            options->calibrateToggle.setToggleState (false, juce::dontSendNotification);
        }
    };

    options->calibrateToggle.onClick = [options, calibrateState]
    {
        if (options->calibrateToggle.isEnabled())
            *calibrateState = options->calibrateToggle.getToggleState();
    };

    options->hardwareSettingsToggle.onClick = [options, hardwareSettingsState]
    {
        if (options->hardwareSettingsToggle.isEnabled())
            *hardwareSettingsState = options->hardwareSettingsToggle.getToggleState();
    };

    if (auto* sourceBox = aw->getComboBoxComponent ("source"))
    {
        // AlertWindow addComboBox uses 1-based item IDs: 1 software, 2 Hardware, 3 Both.
        sourceBox->setItemEnabled (2, hardwareConfigured);
        sourceBox->setItemEnabled (3, hardwareConfigured);

        int sourceIndex = juce::jlimit (0, 2, lastCaptureSourceIndex);
        if (! hardwareConfigured && sourceIndex != 0)
            sourceIndex = 0;

        sourceBox->setSelectedItemIndex (sourceIndex, juce::dontSendNotification);
        sourceBox->onChange = [sourceBox, syncCalibrateForSource]
        {
            syncCalibrateForSource (juce::jmax (0, sourceBox->getSelectedItemIndex()));
        };
        syncCalibrateForSource (sourceIndex);
    }
    else
    {
        syncCalibrateForSource (hardwareConfigured ? lastCaptureSourceIndex : 0);
    }

    aw->addButton ("Capture", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    aw->enterModalState (true,
                         juce::ModalCallbackFunction::create (
                             [safeParent = juce::Component::SafePointer<juce::Component> (parent),
                              capture = this, aw, options, calibrateState, hardwareSettingsState,
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
                                     calibrate = options->calibrateToggle.isEnabled()
                                                     ? options->calibrateToggle.getToggleState()
                                                     : *calibrateState;
                                     *calibrateState = calibrate;
                                     HostPreferences::get().setHardwareCaptureCalibrate (calibrate);
                                 }

                                 const bool generateReport = options->reportToggle.getToggleState();
                                 HostPreferences::get().setCaptureGenerateReport (generateReport);

                                 const bool captureSoftwareSettings =
                                     options->softwareSettingsToggle.getToggleState();
                                 HostPreferences::get().setCaptureSoftwareSettings (captureSoftwareSettings);

                                 bool captureHardwareSettings = false;
                                 if (options->hardwareSettingsToggle.isEnabled())
                                 {
                                     captureHardwareSettings = options->hardwareSettingsToggle.getToggleState();
                                     *hardwareSettingsState = captureHardwareSettings;
                                 }
                                 HostPreferences::get().setCaptureHardwareSettings (
                                     options->hardwareSettingsToggle.isEnabled()
                                         ? captureHardwareSettings
                                         : *hardwareSettingsState);

                                 capture->lastCaptureDescription = description;
                                 capture->lastCaptureRoleIndex = roleIndex;
                                 capture->lastCaptureSourceIndex = sourceIndex;
                                 dialog.reset();

                                 const auto fixtureFile = sourceClips.getSelectedFile (fixtureBox);
                                 capture->capture (description, roleIndex, sourceIndex, calibrate,
                                                   generateReport, captureSoftwareSettings,
                                                   captureHardwareSettings, fixtureFile,
                                                   safeParent.getComponent());
                             }),
                         true);
}

void TestCaseCapture::capture (const juce::String& snapshotName, int roleIndex, int sourceIndex,
                               bool calibrateNoiseFloor,
                               bool generateReport,
                               bool captureSoftwareSettings,
                               bool captureHardwareSettings,
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

    if ((sourceIndex == 1 || sourceIndex == 2) && ! engine.hasHardwareLoopConfigured())
    {
        setStatus ("Configure Hardware Audio Setup before capturing hardware", true);
        return;
    }

    CapturePipelineRequest request;
    request.description = snapshotName;
    request.roleIndex = roleIndex;
    request.source = static_cast<CaptureSource> (juce::jlimit (0, 2, sourceIndex));
    request.fixtureFile = fixtureFile;
    request.progressParent = progressParent;
    request.calibrateNoiseFloor = calibrateNoiseFloor;
    request.captureSoftwareSettings = captureSoftwareSettings;
    request.captureHardwareSettings = captureHardwareSettings;

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
        snap.outputFile = result.paths.softwareOutput;
    if (result.paths.presetFile.existsAsFile())
        snap.presetFile = result.paths.presetFile;
    if (result.capturedHardware)
        snap.hardwareOutputFile = result.paths.hardwareOutput;
    if (result.capturedSysex)
        snap.sysexFile = result.paths.sysexFile;
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
    {
        status += " (silence gate " + juce::String (result.hardwareSilenceThresholdDb, 1) + " dBFS";
        if (result.dcOffsetL != 0.0f || result.dcOffsetR != 0.0f)
            status += ", DC removed";
        status += ")";
    }
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
