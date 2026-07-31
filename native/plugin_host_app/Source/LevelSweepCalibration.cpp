#include "LevelSweepCalibration.h"
#include "HostConfig.h"
#include "LevelMetrics.h"
#include "OfflineCapture.h"
#include "PluginAudioEngine.h"
#include <cmath>

namespace
{
    bool loadWavBuffer (const juce::File& file,
                        juce::AudioBuffer<float>& out,
                        double& outSampleRate,
                        juce::String& error)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
        if (reader == nullptr)
        {
            error = "Could not read WAV: " + file.getFullPathName();
            return false;
        }

        const int n = (int) reader->lengthInSamples;
        if (n <= 0)
        {
            error = "WAV has no samples: " + file.getFullPathName();
            return false;
        }

        out.setSize ((int) reader->numChannels, n, false, true, false);
        reader->read (out.getArrayOfWritePointers(), out.getNumChannels(), 0, n);
        outSampleRate = reader->sampleRate > 1.0 ? reader->sampleRate : 48000.0;
        return true;
    }

    juce::String pathLabel (LevelSweepCalibration::Path path)
    {
        return path == LevelSweepCalibration::Path::hardware ? "hardware" : "software";
    }

    void fillPointFromLevels (LevelSweepCalibration::Point& point,
                              const LevelMetrics::Levels& levels,
                              LevelSweepCalibration::Path path)
    {
        point.measuredPeakDb = levels.peakDb;
        point.measuredRmsDb = levels.rmsDb;
        point.measuredLufs = levels.lufs;
        point.clipped = levels.clipped;
        if (path == LevelSweepCalibration::Path::hardware)
        {
            point.hwPeakDb = levels.peakDb;
            point.hwRmsDb = levels.rmsDb;
        }
        else
        {
            point.swPeakDb = levels.peakDb;
            point.swRmsDb = levels.rmsDb;
        }
    }

    bool writeLevelSweepJson (const LevelSweepCalibration::Result& result,
                              const juce::File& jsonFile,
                              juce::String& error)
    {
        auto* root = new juce::DynamicObject();
        root->setProperty ("version", 3);
        root->setProperty ("created_at", juce::Time::getCurrentTime().toISO8601 (true));
        root->setProperty ("plot_name", result.plotName);
        root->setProperty ("path", pathLabel (result.path));
        root->setProperty ("device_name", result.deviceName);
        root->setProperty ("bypassed", result.bypassed);
        root->setProperty ("mix_amount", result.mixAmount);
        root->setProperty ("sample_rate", result.sampleRate);
        root->setProperty ("buffer_size", result.bufferSize);
        root->setProperty ("latency_samples", result.latencySamples);
        root->setProperty ("stimulus", "sine_0db_1ch_5s_48k.wav");
        root->setProperty ("analyse_skip_seconds", LevelSweepCalibration::analyseSkipSeconds);
        root->setProperty ("analyse_window_seconds", LevelSweepCalibration::analyseWindowSeconds);
        root->setProperty ("max_abs_measured_minus_ideal_peak_db", result.maxAbsMeasuredMinusIdealPeak);
        root->setProperty ("any_clipped", result.anyClipped);

        juce::Array<juce::var> points;
        for (const auto& p : result.points)
        {
            auto* obj = new juce::DynamicObject();
            obj->setProperty ("send_db", p.sendDb);
            obj->setProperty ("measured_peak_db", p.measuredPeakDb);
            obj->setProperty ("measured_rms_db", p.measuredRmsDb);
            obj->setProperty ("measured_lufs", p.measuredLufs);
            obj->setProperty ("clipped", p.clipped);
            obj->setProperty ("hw_rms_db", p.hwRmsDb);
            obj->setProperty ("hw_peak_db", p.hwPeakDb);
            obj->setProperty ("sw_rms_db", p.swRmsDb);
            obj->setProperty ("sw_peak_db", p.swPeakDb);
            obj->setProperty ("hw_minus_sw_db", p.hwMinusSwDb);
            points.add (juce::var (obj));
        }
        root->setProperty ("points", juce::var (points));

        jsonFile.getParentDirectory().createDirectory();
        if (! jsonFile.replaceWithText (juce::JSON::toString (juce::var (root), true) + "\n"))
        {
            error = "Failed to write " + jsonFile.getFullPathName();
            return false;
        }
        return true;
    }

    bool runCalibratePlot (const juce::File& pythonCli,
                           const juce::File& jsonFile,
                           const juce::File& pngFile,
                           juce::String& error)
    {
        if (pythonCli == juce::File() || ! pythonCli.existsAsFile())
        {
            error = "python_cli not configured; JSON written but PNG not generated";
            return false;
        }

        juce::StringArray args;
        args.add (pythonCli.getFullPathName());
        args.add ("calibrate-plot");
        args.add (jsonFile.getFullPathName());
        args.add ("-o");
        args.add (pngFile.getFullPathName());

        juce::ChildProcess process;
        if (! process.start (args))
        {
            error = "Failed to start " + pythonCli.getFullPathName();
            return false;
        }

        const auto output = process.readAllProcessOutput();
        const auto exitCode = process.getExitCode();
        if (exitCode != 0 || ! pngFile.existsAsFile())
        {
            error = output.trim().isNotEmpty()
                        ? output.trim().upToFirstOccurrenceOf ("\n", false, false)
                        : ("calibrate-plot exited with code " + juce::String ((int) exitCode));
            return false;
        }
        return true;
    }
}

bool LevelSweepCalibration::writeJson (const Result& result, const juce::File& jsonFile, juce::String& error)
{
    return writeLevelSweepJson (result, jsonFile, error);
}

bool LevelSweepCalibration::run (PluginAudioEngine& engine,
                                 const juce::File& sineFixture,
                                 const juce::File& outputDir,
                                 const juce::File& pythonCli,
                                 Path path,
                                 const juce::String& plotName,
                                 Result& out,
                                 juce::String& error,
                                 ProgressFn progress)
{
    out = {};
    error.clear();

    const auto trimmedName = plotName.trim();
    if (trimmedName.isEmpty())
    {
        error = "Plot name is required";
        return false;
    }

    if (! sineFixture.existsAsFile())
    {
        error = "Sine fixture not found: " + sineFixture.getFullPathName();
        return false;
    }

    auto* plugin = engine.getPlugin();
    if (plugin == nullptr)
    {
        error = "Load a plugin before running the level sweep";
        return false;
    }

    if (path == Path::hardware && ! engine.hasHardwareLoopConfigured())
    {
        error = "Configure a hardware audio device first";
        return false;
    }

    const auto hwSettings = engine.getHardwareLoopSettings();
    out.path = path;
    out.plotName = trimmedName;
    out.deviceName = hwSettings.deviceName;
    out.bypassed = engine.isBypassed();
    out.mixAmount = engine.getMixAmount();
    out.sampleRate = engine.getDeviceSampleRate();
    out.bufferSize = hwSettings.bufferSize > 0 ? hwSettings.bufferSize : engine.getDeviceBlockSize();
    out.latencySamples = hwSettings.latencySamples;

    if (out.sampleRate <= 1.0)
        out.sampleRate = 48000.0;

    const auto slug = HostConfig::slugify (trimmedName);
    if (slug.isEmpty())
    {
        error = "Plot name produced an empty file slug";
        return false;
    }

    outputDir.createDirectory();
    const auto workDir = outputDir.getChildFile ("tmp_level_sweep");
    workDir.deleteRecursively();
    workDir.createDirectory();

    const float savedSendDb = engine.getSendLevelDb();
    struct RestoreSend
    {
        PluginAudioEngine& e;
        float db;
        ~RestoreSend() { e.setSendLevelDb (db); }
    } restoreSend { engine, savedSendDb };

    out.points.resize (sendLevelCount);
    for (int i = 0; i < sendLevelCount; ++i)
    {
        out.points.getReference (i) = {};
        out.points.getReference (i).sendDb = (double) sendLevelsDb[i];
    }

    if (path == Path::hardware)
    {
        if (engine.getDeviceSampleRate() <= 1.0)
        {
            error = "Audio device is not running";
            workDir.deleteRecursively();
            return false;
        }

        for (int i = 0; i < sendLevelCount; ++i)
        {
            const float sendDb = sendLevelsDb[i];
            if (progress)
                progress (i + 1, sendLevelCount, sendDb);

            engine.setSendLevelDb (sendDb);
            const auto wav = workDir.getChildFile ("hw_" + juce::String (i) + ".wav");
            juce::String hwError;
            // maxTail must cover targetDuration; keep a little slack beyond stop.
            if (! engine.captureHardwareToFile (sineFixture,
                                               wav,
                                               0.25,
                                               -20.0,
                                               hardwareTargetSeconds + 2.0,
                                               hwError,
                                               nullptr,
                                               nullptr,
                                               hardwareTargetSeconds))
            {
                error = "Hardware capture at " + juce::String (sendDb, 1) + " dB failed: " + hwError;
                workDir.deleteRecursively();
                return false;
            }

            juce::AudioBuffer<float> buf;
            double rate = out.sampleRate;
            if (! loadWavBuffer (wav, buf, rate, error))
            {
                workDir.deleteRecursively();
                return false;
            }

            const auto levels = LevelMetrics::analyseSteadyState (buf, rate,
                                                                  analyseSkipSeconds,
                                                                  analyseWindowSeconds);
            fillPointFromLevels (out.points.getReference (i), levels, path);
        }

        engine.setSendLevelDb (savedSendDb);
    }
    else
    {
        const double renderRate = juce::jmax (1.0, engine.getDeviceSampleRate() > 1.0
                                                       ? engine.getDeviceSampleRate()
                                                       : out.sampleRate);
        out.sampleRate = renderRate;
        const bool deviceWasRunning = engine.getDeviceSampleRate() > 1.0;
        if (deviceWasRunning)
            engine.stopAudioDevice();

        for (int i = 0; i < sendLevelCount; ++i)
        {
            const float sendDb = sendLevelsDb[i];
            if (progress)
                progress (i + 1, sendLevelCount, sendDb);

            const auto wav = workDir.getChildFile ("sw_" + juce::String (i) + ".wav");
            OfflineCaptureOptions opts;
            opts.sampleRate = renderRate;
            opts.blockSize = juce::jmax (32, out.bufferSize > 0 ? out.bufferSize : 512);
            opts.inputGainDb = sendDb;
            opts.bypassPlugin = out.bypassed;
            opts.mixAmount = out.mixAmount;
            // Keep the full 5 s sine so the 0.75+2.5 s analysis window fits.
            opts.maxTailSeconds = out.bypassed ? 0.0 : 4.0;
            opts.tailSilenceSeconds = out.bypassed ? 0.0 : 0.35;

            juce::String swError;
            if (! OfflineCapture::renderPluginToFile (*plugin, sineFixture, wav, opts, swError))
            {
                if (deviceWasRunning)
                {
                    juce::String restartError;
                    engine.startAudioDevice (restartError);
                }
                error = "Software render at " + juce::String (sendDb, 1) + " dB failed: " + swError;
                workDir.deleteRecursively();
                return false;
            }

            juce::AudioBuffer<float> buf;
            double rate = renderRate;
            if (! loadWavBuffer (wav, buf, rate, error))
            {
                if (deviceWasRunning)
                {
                    juce::String restartError;
                    engine.startAudioDevice (restartError);
                }
                workDir.deleteRecursively();
                return false;
            }

            const auto levels = LevelMetrics::analyseSteadyState (buf, rate,
                                                                  analyseSkipSeconds,
                                                                  analyseWindowSeconds);
            fillPointFromLevels (out.points.getReference (i), levels, path);
        }

        if (deviceWasRunning)
        {
            juce::String restartError;
            if (! engine.startAudioDevice (restartError))
            {
                error = "Could not reopen audio device after software renders: " + restartError;
                workDir.deleteRecursively();
                return false;
            }
        }
    }

    double maxIdeal = 0.0;
    bool anyClip = false;
    for (const auto& point : out.points)
    {
        maxIdeal = juce::jmax (maxIdeal, std::abs (point.measuredPeakDb - point.sendDb));
        anyClip = anyClip || point.clipped;
    }
    out.maxAbsMeasuredMinusIdealPeak = maxIdeal;
    out.anyClipped = anyClip;

    out.jsonFile = outputDir.getChildFile (slug + ".json");
    out.pngFile = outputDir.getChildFile (slug + ".png");

    if (! writeLevelSweepJson (out, out.jsonFile, error))
    {
        workDir.deleteRecursively();
        return false;
    }

    juce::String plotError;
    out.wrotePng = runCalibratePlot (pythonCli, out.jsonFile, out.pngFile, plotError);
    if (! out.wrotePng)
        error = plotError;

    workDir.deleteRecursively();
    return true;
}
