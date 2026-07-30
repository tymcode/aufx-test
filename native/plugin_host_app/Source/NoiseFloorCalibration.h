#pragma once

#include <JuceHeader.h>

class PluginAudioEngine;

/**
 * Measure an audio path's noise floor and recommend a silence-detection
 * threshold just above it.
 *
 * Used before hardware capture so reverb-tail auto-stop works when the analog
 * return floor is far below the generic -60 dB default (or above a too-low
 * fixed gate). The analyse() helper is buffer-agnostic for reuse elsewhere.
 */
namespace NoiseFloorCalibration
{
    /** How far above the measured peak floor the silence gate sits. */
    constexpr double defaultMarginDb = 8.0;

    /** How long to listen after send/return settle when measuring hardware. */
    constexpr double defaultListenSeconds = 0.75;

    /** Fallback when no measurement is available. */
    constexpr double defaultSilenceThresholdDb = -60.0;

    struct Result
    {
        double peakDb { -120.0 };
        double rmsDb { -120.0 };
        double recommendedSilenceThresholdDb { defaultSilenceThresholdDb };
        double listenSeconds { 0.0 };
        double marginDb { defaultMarginDb };
    };

    /** peakDb + marginDb, clamped to a practical silence-gate range. */
    double thresholdFromPeakDb (double peakDb, double marginDb = defaultMarginDb);

    /**
     * Analyse a recorded buffer region (linear amplitude) and fill Result.
     * startSample / numSamples select the window; both channels are included.
     */
    Result analyse (const juce::AudioBuffer<float>& buffer,
                    int startSample,
                    int numSamples,
                    double marginDb = defaultMarginDb);

    /**
     * Send silence through the hardware insert loop, record the return, and
     * measure the noise floor after latency settle.
     *
     * BLOCKING: pumps the message thread while the audio callback records
     * (same pattern as hardware capture / latency detect).
     */
    bool measureHardwareReturn (PluginAudioEngine& engine,
                                Result& out,
                                juce::String& error,
                                double listenSeconds = defaultListenSeconds,
                                double marginDb = defaultMarginDb);
}
