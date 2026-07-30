#include "CapturePipeline.h"
#include "HostFileUtils.h"
#include "HostLog.h"
#include "HostPreferences.h"
#include "MidiEndpointInfo.h"
#include "OfflineCapture.h"
#include "PresetHardwareState.h"
#include "SessionArtifactSchema.h"
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
                                                         int roleIndex) const
{
    CaptureArtifactPaths paths;

    const auto keyword = HostFileUtils::keywordFromDescription (description);
    const auto token = juce::Uuid().toString().substring (0, 8);
    paths.stem = keyword.isNotEmpty() ? keyword + "_" + token : token;

    // One folder per capture stem so growing artifact sets stay grouped:
    // sessions/<slug>/artifacts/<stem>/{preset,wavs,sysex,…}
    paths.captureDir = config.sessionsRoot
                           .getChildFile (HostConfig::slugify (plugin.sessionName))
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

bool CapturePipeline::renderSoftware (const juce::File& fixtureFile,
                                      const juce::File& presetOut,
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

    if (! engine.saveCurrentPreset (presetOut, error))
        return false;

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
                                      juce::Component* progressParent,
                                      juce::String& error)
{
    if (! engine.hasHardwareLoopConfigured())
    {
        error = "Configure Hardware Audio Setup before capturing hardware";
        return false;
    }

    engine.stopFixture();

    std::atomic<bool> cancelRequested { false };
    const juce::String progressText = targetDurationSeconds > 0.0
        ? ("Recording hardware return (~"
           + juce::String (targetDurationSeconds, 1)
           + "s, then auto-stops). Cancel saves early.")
        : "Recording hardware return… waits for silence after the clip. Cancel saves early.";

    juce::AlertWindow progress ("Capturing Hardware",
                                progressText,
                                juce::MessageBoxIconType::NoIcon,
                                progressParent);
    progress.addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    if (auto* cancelButton = progress.getButton ("Cancel"))
        cancelButton->onClick = [&cancelRequested] { cancelRequested.store (true); };
    progress.setAlwaysOnTop (true);
    progress.setSize (420, 160);
    progress.setCentreRelative (0.5f, 0.4f);
    progress.enterModalState (true, nullptr, false);

    const bool ok = engine.captureHardwareToFile (fixtureFile, hardwareOut, 1.0, -60.0, 120.0, error,
                                                  &cancelRequested, targetDurationSeconds);
    progress.setMessage ("Finishing…");
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

    if (! engine.sendMidiMessage (module->buildDumpRequest()))
    {
        error = "Failed to send dump request";
        return false;
    }

    juce::MidiMessage dump;
    if (! engine.waitForSysexDump (
            [module] (const juce::MidiMessage& m) { return module->isDumpResponse (m); },
            dump, 5000, error))
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

    if (wantHardware && ! engine.hasHardwareLoopConfigured())
    {
        error = "Configure Hardware Audio Setup before capturing hardware";
        return false;
    }

    if (! request.fixtureFile.existsAsFile())
    {
        error = "Select a source clip before capturing";
        return false;
    }

    outResult.paths = makeArtifactPaths (plugin, request.description, request.roleIndex);

    HardwareModeGuard modeGuard (engine, refreshHardwareUi);

    if (wantPlugin)
    {
        modeGuard.showMode (false);
        if (! renderSoftware (request.fixtureFile,
                              outResult.paths.presetFile,
                              outResult.paths.softwareOutput,
                              outResult.softwareDurationSeconds,
                              error))
            return false;
        outResult.capturedPlugin = true;
    }

    if (wantHardware)
    {
        modeGuard.showMode (true);

        // Prefer the software take's duration so both files match; if plugin
        // capture was skipped or duration could not be read, fall back to the
        // fixture length so we do not sit forever on a noisy analog floor.
        double hardwareTargetSeconds = outResult.softwareDurationSeconds;
        if (hardwareTargetSeconds <= 0.0)
            hardwareTargetSeconds = wavDurationSeconds (request.fixtureFile);

        if (! recordHardware (request.fixtureFile,
                              outResult.paths.hardwareOutput,
                              hardwareTargetSeconds,
                              request.progressParent,
                              error))
            return false;
        outResult.capturedHardware = true;

        juce::String sysexError;
        if (dumpHardwareSysex (outResult.paths.sysexFile, sysexError))
            outResult.capturedSysex = true;
        else
            HostLog::info ("Sysex dump skipped: " + sysexError);
    }

    return true;
}
