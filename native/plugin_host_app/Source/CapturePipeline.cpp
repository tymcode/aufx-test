#include "CapturePipeline.h"
#include "HostFileUtils.h"
#include "HostLog.h"
#include "HostPreferences.h"
#include "MidiEndpointInfo.h"
#include "NoiseFloorCalibration.h"
#include "OfflineCapture.h"
#include "PresetHardwareState.h"
#include "SessionArtifactSchema.h"
#include "Utf8.h"
#include "sysex/SysexDeviceModule.h"

CapturePipeline::CapturePipeline (PluginAudioEngine& audioEngine,
                                  HostConfig& hostConfig,
                                  RefreshUiFn refreshHardwareUiIn)
    : engine (audioEngine),
      config (hostConfig),
      refreshHardwareUi (std::move (refreshHardwareUiIn))
{
}

CaptureArtifactPaths CapturePipeline::makeArtifactPaths (const HostPluginEntry& plugin,
                                                         const juce::String& description,
                                                         int roleIndex,
                                                         const juce::String& sessionNameOverride) const
{
    CaptureArtifactPaths paths;

    const auto keyword = HostFileUtils::keywordFromDescription (description);
    const auto token = juce::Uuid().toString().substring (0, 8);
    paths.stem = keyword.isNotEmpty() ? keyword + "_" + token : token;

    // One folder per capture stem so growing artifact sets stay grouped:
    // sessions/<slug>/artifacts/<stem>/{preset,wavs,sysex,…}
    const auto sessionName = sessionNameOverride.isNotEmpty() ? sessionNameOverride
                                                              : plugin.sessionName;
    paths.captureDir = config.sessionsRoot
                           .getChildFile (HostConfig::slugify (sessionName))
                           .getChildFile ("artifacts")
                           .getChildFile (paths.stem);
    paths.captureDir.createDirectory();

    const auto roleSuffix = SessionArtifactSchema::roleCodeFromIndex (roleIndex);
    paths.presetFile = paths.captureDir.getChildFile (
        SessionArtifactSchema::ensureAupresetExtension (paths.stem));
    paths.softwareOutput = paths.captureDir.getChildFile (
        paths.stem + SessionArtifactSchema::softwareOutputSuffix (roleSuffix) + ".wav");
    paths.hardwareOutput = paths.captureDir.getChildFile (
        paths.stem + SessionArtifactSchema::hardwareOutputSuffix (roleSuffix) + ".wav");
    paths.sysexFile = paths.captureDir.getChildFile (paths.stem + ".syx");
    return paths;
}

double CapturePipeline::wavDurationSeconds (const juce::File& wavFile)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    if (auto reader = std::unique_ptr<juce::AudioFormatReader> (fm.createReaderFor (wavFile)))
        if (reader->sampleRate > 1.0)
            return (double) reader->lengthInSamples / reader->sampleRate;
    return 0.0;
}

bool CapturePipeline::saveSoftwarePreset (const juce::File& presetOut, juce::String& error)
{
    if (engine.getPlugin() == nullptr)
    {
        error = "No plugin loaded";
        return false;
    }

    return engine.saveCurrentPreset (presetOut, error);
}

bool CapturePipeline::renderSoftware (const juce::File& fixtureFile,
                                      const juce::File& softwareOut,
                                      double& outDurationSeconds,
                                      juce::String& error)
{
    outDurationSeconds = 0.0;

    if (engine.getPlugin() == nullptr)
    {
        error = "No plugin loaded";
        return false;
    }

    engine.stopFixture();
    engine.stopAudioDevice();

    // Render at the device rate (with fixture resampled inside OfflineCapture)
    // so software and hardware takes share sample rate and wall-clock length —
    // rendering at the fixture file rate made 44.1k software files look ~0.5s
    // short next to 48k hardware when compared by sample index.
    OfflineCaptureOptions renderOptions;
    renderOptions.sampleRate = engine.getDeviceSampleRate() > 1.0 ? engine.getDeviceSampleRate()
                                                                  : 48000.0;
    renderOptions.blockSize = juce::jmax (32, engine.getDeviceBlockSize());

    if (! OfflineCapture::renderPluginToFile (*engine.getPlugin(), fixtureFile, softwareOut, renderOptions, error))
    {
        juce::String restartError;
        if (! engine.startAudioDevice (restartError))
            HostLog::error ("Failed to restart audio after offline capture failure: " + restartError);
        return false;
    }

    if (! engine.startAudioDevice (error))
        return false;

    outDurationSeconds = wavDurationSeconds (softwareOut);
    return true;
}

bool CapturePipeline::recordHardware (const juce::File& fixtureFile,
                                      const juce::File& hardwareOut,
                                      double targetDurationSeconds,
                                      double silenceThresholdDb,
                                      juce::Component* progressParent,
                                      juce::String& error,
                                      float dcOffsetL,
                                      float dcOffsetR)
{
    if (! engine.hasExternalLoopConfigured())
    {
        error = "Configure Hardware Audio Setup (or Remote Setup) before capturing hardware";
        return false;
    }

    engine.stopFixture();

    std::atomic<bool> stopRequested { false };
    std::atomic<bool> abortRequested { false };
    const juce::String progressText = targetDurationSeconds > 0.0
        ? (utf8 ("Recording hardware return (~")
           + juce::String (targetDurationSeconds, 1)
           + utf8 ("s, then auto-stops). Stop saves early; Cancel aborts."))
        : (utf8 ("Recording hardware return. Auto stops after silence (gate ")
           + juce::String (silenceThresholdDb, 1)
           + utf8 (" dBFS)."));

    struct HardwareCaptureProgressPanel final : public juce::Component,
                                                private juce::Timer
    {
        HardwareCaptureProgressPanel (const juce::String& messageText,
                                      std::atomic<bool>& stopFlag,
                                      std::atomic<bool>& abortFlag)
            : stopRequested (stopFlag),
              abortRequested (abortFlag)
        {
            message.setText (messageText, juce::dontSendNotification);
            message.setJustificationType (juce::Justification::centred);
            message.setMinimumHorizontalScale (1.0f);
            addAndMakeVisible (message);

            stopwatch.setJustificationType (juce::Justification::centred);
            stopwatch.setFont (juce::FontOptions (22.0f, juce::Font::bold));
            stopwatch.setText ("0:00.0", juce::dontSendNotification);
            addAndMakeVisible (stopwatch);

            stopButton.setButtonText ("Stop");
            cancelButton.setButtonText ("Cancel");
            stopButton.addShortcut (juce::KeyPress (juce::KeyPress::returnKey));
            cancelButton.addShortcut (juce::KeyPress (juce::KeyPress::escapeKey));
            stopButton.onClick = [this] { stopRequested.store (true); };
            cancelButton.onClick = [this] { abortRequested.store (true); };
            addAndMakeVisible (stopButton);
            addAndMakeVisible (cancelButton);

            setSize (440, 150);
            startedAtMs = juce::Time::getMillisecondCounterHiRes();
            startTimerHz (10);
        }

        ~HardwareCaptureProgressPanel() override { stopTimer(); }

        void stop() { stopTimer(); }

        void resized() override
        {
            auto area = getLocalBounds();
            message.setBounds (area.removeFromTop (56));
            stopwatch.setBounds (area.removeFromTop (36));
            area.removeFromTop (10);

            constexpr int buttonWidth = 110;
            constexpr int buttonHeight = 32;
            constexpr int gap = 16;
            auto buttonRow = area.removeFromTop (buttonHeight);
            const int totalWidth = buttonWidth * 2 + gap;
            auto centred = buttonRow.withSizeKeepingCentre (totalWidth, buttonHeight);
            stopButton.setBounds (centred.removeFromLeft (buttonWidth));
            centred.removeFromLeft (gap);
            cancelButton.setBounds (centred.removeFromLeft (buttonWidth));
        }

        void timerCallback() override
        {
            const double elapsed = juce::jmax (0.0,
                (juce::Time::getMillisecondCounterHiRes() - startedAtMs) * 0.001);
            const int totalTenths = (int) std::llround (elapsed * 10.0);
            const int minutes = totalTenths / 600;
            const int seconds = (totalTenths / 10) % 60;
            const int tenths = totalTenths % 10;
            stopwatch.setText (juce::String (minutes) + ":"
                                   + juce::String (seconds).paddedLeft ('0', 2)
                                   + "." + juce::String (tenths),
                               juce::dontSendNotification);
        }

        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key == juce::KeyPress::escapeKey)
            {
                abortRequested.store (true);
                return true;
            }
            if (key == juce::KeyPress::returnKey)
            {
                stopRequested.store (true);
                return true;
            }
            return false;
        }

        juce::Label message;
        juce::Label stopwatch;
        juce::TextButton stopButton;
        juce::TextButton cancelButton;
        std::atomic<bool>& stopRequested;
        std::atomic<bool>& abortRequested;
        double startedAtMs { 0.0 };
    };

    juce::AlertWindow progress ("Capturing Hardware",
                                {},
                                juce::MessageBoxIconType::NoIcon,
                                progressParent);
    HardwareCaptureProgressPanel panel (progressText, stopRequested, abortRequested);
    progress.addCustomComponent (&panel);
    // Escape / Return still work via button shortcuts and the panel key handler.
    progress.setAlwaysOnTop (true);
    progress.setWantsKeyboardFocus (true);
    panel.setWantsKeyboardFocus (true);
    progress.enterModalState (true, nullptr, false);
    if (progressParent != nullptr)
        progress.centreAroundComponent (progressParent, progress.getWidth(), progress.getHeight());
    else
        progress.setCentreRelative (0.5f, 0.4f);
    panel.grabKeyboardFocus();

    const bool ok = engine.captureHardwareToFile (fixtureFile, hardwareOut, 1.0, silenceThresholdDb, 120.0, error,
                                                  &stopRequested, &abortRequested, targetDurationSeconds,
                                                  dcOffsetL, dcOffsetR);
    panel.stop();
    panel.message.setText (utf8 ("Finishing…"), juce::dontSendNotification);
    progress.exitModalState (0);
    progress.setVisible (false);
    return ok;
}

bool CapturePipeline::dumpHardwareSysex (const juce::File& sysexOut, juce::String& error)
{
    const auto outId = HostPreferences::get().getMidiOutIdentifier();
    if (outId.isEmpty())
    {
        error = "No MIDI out configured";
        return false;
    }

    const auto info = findMidiEndpointInfo (outId, true);
    const auto* module = resolveSelectedSysexModule (info);
    if (module == nullptr)
    {
        error = "No sysex module for " + info.name;
        return false;
    }

    juce::String openError;
    if (! engine.setMidiOutputDevice (outId, openError))
    {
        error = openError;
        return false;
    }

    const auto dumpIn = HostPreferences::get().getMidiDumpInIdentifier();
    if (dumpIn.isNotEmpty())
    {
        auto ids = engine.getSelectedMidiInputIdentifiers();
        if (! ids.contains (dumpIn))
        {
            ids.add (dumpIn);
            engine.setMidiInputDevices (ids);
        }
    }

    // Devices without a public dump-request protocol (DP/Pro) need the user
    // to start the dump from the front panel; show the module's instructions
    // while we listen. waitForSysexDump pumps the message thread, so the
    // non-modal window paints and MIDI keeps flowing.
    std::unique_ptr<juce::AlertWindow> manualPrompt;

    if (module->canRequestDump())
    {
        if (! engine.sendMidiMessage (module->buildDumpRequest()))
        {
            error = "Failed to send dump request";
            return false;
        }
    }
    else
    {
        manualPrompt = std::make_unique<juce::AlertWindow> (
            module->getDisplayName() + utf8 (" — waiting for sysex dump"),
            module->manualDumpInstructions(),
            juce::MessageBoxIconType::InfoIcon);
        manualPrompt->centreWithSize (juce::jmax (420, manualPrompt->getWidth()),
                                      juce::jmax (140, manualPrompt->getHeight()));
        manualPrompt->setVisible (true);
        manualPrompt->toFront (true);
    }

    juce::MidiMessage dump;
    const bool received = engine.waitForSysexDump (
        [module] (const juce::MidiMessage& m) { return module->isDumpResponse (m); },
        dump, module->dumpTimeoutMs(), error);

    manualPrompt.reset();

    if (! received)
        return false;

    if (! module->validateDump (dump))
    {
        error = "Received sysex failed validation";
        return false;
    }

    juce::MemoryBlock block;
    block.append (dump.getRawData(), (size_t) dump.getRawDataSize());
    if (! sysexOut.replaceWithData (block.getData(), block.getSize()))
    {
        error = "Failed to write " + sysexOut.getFullPathName();
        return false;
    }

    return true;
}

bool CapturePipeline::run (const CapturePipelineRequest& request,
                           const HostPluginEntry& plugin,
                           CapturePipelineResult& outResult,
                           juce::String& error)
{
    outResult = {};

    const bool wantPlugin = (request.source == CaptureSource::plugin
                             || request.source == CaptureSource::both);
    const bool wantHardware = (request.source == CaptureSource::hardware
                               || request.source == CaptureSource::both);

    if (wantPlugin && engine.getPlugin() == nullptr)
    {
        error = "No plugin loaded";
        return false;
    }

    if (wantHardware && ! engine.hasExternalLoopConfigured())
    {
        error = "Configure Hardware Audio Setup (or Remote Setup) before capturing hardware";
        return false;
    }

    if (! request.fixtureFile.existsAsFile())
    {
        error = "Select a source clip before capturing";
        return false;
    }

    outResult.paths = makeArtifactPaths (plugin,
                                         request.description,
                                         request.roleIndex,
                                         request.sessionNameOverride);

    // Flip send/return routing only. Do not refresh the chrome / tear down the
    // Cocoa editor — destroying and recreating it after capture falls back to
    // AUGenericView (empty "placeholder" UI) on many plugins.
    HardwareModeGuard modeGuard (engine, {});

    if (wantPlugin)
    {
        modeGuard.showMode (false);
        if (! renderSoftware (request.fixtureFile,
                              outResult.paths.softwareOutput,
                              outResult.softwareDurationSeconds,
                              error))
            return false;
        outResult.capturedPlugin = true;
    }

    if (request.captureSoftwareSettings)
    {
        if (engine.getPlugin() == nullptr)
        {
            error = "No plugin loaded";
            return false;
        }

        if (! saveSoftwarePreset (outResult.paths.presetFile, error))
            return false;
    }

    if (wantHardware)
    {
        modeGuard.showMode (true);

        // Prefer the software take's duration so Both captures match. With no
        // software reference (hardware-only), leave target at 0 so recording
        // continues until silence, Stop, or the max-tail safety limit — never
        // truncate to the dry fixture length (that cuts reverb tails short).
        const double hardwareTargetSeconds = outResult.softwareDurationSeconds;

        double silenceThresholdDb = HostPreferences::get().getHardwareCaptureSilenceThresholdDb (
            NoiseFloorCalibration::defaultSilenceThresholdDb);
        float dcOffsetL = 0.0f;
        float dcOffsetR = 0.0f;

        if (request.calibrateNoiseFloor)
        {
            juce::AlertWindow calibrating ("Calibrating",
                                           utf8 ("Measuring noise floor and DC offset…"),
                                           juce::MessageBoxIconType::NoIcon,
                                           request.progressParent);
            calibrating.setAlwaysOnTop (true);
            calibrating.setSize (360, 120);
            calibrating.setCentreRelative (0.5f, 0.4f);
            calibrating.enterModalState (true, nullptr, false);

            NoiseFloorCalibration::Result floorResult;
            juce::String floorError;
            const bool ok = NoiseFloorCalibration::measureHardwareReturn (engine, floorResult, floorError);
            calibrating.exitModalState (0);
            calibrating.setVisible (false);

            if (! ok)
            {
                error = floorError;
                return false;
            }

            silenceThresholdDb = floorResult.recommendedSilenceThresholdDb;
            dcOffsetL = floorResult.dcOffsetL;
            dcOffsetR = floorResult.dcOffsetR;
            HostPreferences::get().setHardwareCaptureSilenceThresholdDb (silenceThresholdDb);
            outResult.calibratedNoiseFloor = true;
            outResult.dcOffsetL = dcOffsetL;
            outResult.dcOffsetR = dcOffsetR;
            HostLog::info ("Hardware noise floor "
                           + juce::String (floorResult.peakDb, 1)
                           + " dBFS peak (DC L "
                           + juce::String (dcOffsetL, 5)
                           + " R "
                           + juce::String (dcOffsetR, 5)
                           + ") → silence gate "
                           + juce::String (silenceThresholdDb, 1)
                           + " dBFS");
        }

        outResult.hardwareSilenceThresholdDb = silenceThresholdDb;

        if (! recordHardware (request.fixtureFile,
                              outResult.paths.hardwareOutput,
                              hardwareTargetSeconds,
                              silenceThresholdDb,
                              request.progressParent,
                              error,
                              dcOffsetL,
                              dcOffsetR))
            return false;
        outResult.capturedHardware = true;
    }

    if (request.captureHardwareSettings)
    {
        juce::String sysexError;
        if (dumpHardwareSysex (outResult.paths.sysexFile, sysexError))
            outResult.capturedSysex = true;
        else
            HostLog::info ("Sysex dump skipped: " + sysexError);
    }

    return true;
}
