#pragma once

#include <JuceHeader.h>
#include <functional>

class PluginAudioEngine;

/**
 * Measure transfer curves by playing a 0 dBFS sine at stepped digital send
 * gains through the active path (hardware insert or software plugin with
 * current bypass/mix — not dry-thru unless the host is bypassed).
 *
 * Each step records true sample peak, RMS, and BS.1770-style LUFS over a long
 * enough window to catch pulsing / modulated effects.
 *
 * Writes <slug>.json and optionally <slug>.png via aufx-test calibrate-plot.
 */
namespace LevelSweepCalibration
{
    enum class Path
    {
        hardware,
        software
    };

    /** Commanded send levels (dBFS digital gain on the 0 dBFS sine fixture). */
    inline constexpr float sendLevelsDb[] = {
        -24.0f, -18.0f, -12.0f, -9.0f, -6.0f, -3.0f, -1.5f, 0.0f
    };
    inline constexpr int sendLevelCount = (int) (sizeof (sendLevelsDb) / sizeof (sendLevelsDb[0]));

    /** Discard settle time so filters / modulation can start cycling. */
    constexpr double analyseSkipSeconds = 0.75;
    /** Analysis window — long enough for typical LFO / pulse rates. */
    constexpr double analyseWindowSeconds = 2.5;
    /** Hardware record length after latency trim (covers skip + window). */
    constexpr double hardwareTargetSeconds = 4.0;

    struct Point
    {
        double sendDb { 0.0 };
        double measuredPeakDb { -120.0 };
        double measuredRmsDb { -120.0 };
        double measuredLufs { -120.0 };
        bool clipped { false };
        // Legacy aliases for older plot JSON / dual-path runs.
        double hwRmsDb { -120.0 };
        double hwPeakDb { -120.0 };
        double swRmsDb { -120.0 };
        double swPeakDb { -120.0 };
        double hwMinusSwDb { 0.0 };
    };

    struct Result
    {
        juce::Array<Point> points;
        Path path { Path::hardware };
        juce::String plotName;
        juce::String deviceName;
        bool bypassed { false };
        float mixAmount { 1.0f };
        double sampleRate { 0.0 };
        int bufferSize { 0 };
        int latencySamples { 0 };
        double maxAbsMeasuredMinusIdealPeak { 0.0 };
        bool anyClipped { false };
        juce::File jsonFile;
        juce::File pngFile;
        bool wrotePng { false };
    };

    using ProgressFn = std::function<void (int index, int total, float sendDb)>;

    bool writeJson (const Result& result, const juce::File& jsonFile, juce::String& error);

    /**
     * Run a single-path level sweep. Writes JSON/PNG as outputDir/<slug>.*
     * Restores the engine send level when finished.
     */
    bool run (PluginAudioEngine& engine,
              const juce::File& sineFixture,
              const juce::File& outputDir,
              const juce::File& pythonCli,
              Path path,
              const juce::String& plotName,
              Result& out,
              juce::String& error,
              ProgressFn progress = nullptr);
}
