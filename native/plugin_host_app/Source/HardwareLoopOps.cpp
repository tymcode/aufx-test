#include "HardwareLoopOps.h"
#include "HostAudioHelpers.h"

HardwareLoopOps::HardwareLoopOps (juce::AudioFormatManager& formatManagerIn,
                                  juce::CriticalSection& processLockIn,
                                  std::atomic<float>& sendGainIn,
                                  double& deviceSampleRateIn,
                                  int& deviceBlockSizeIn)
    : formatManager (formatManagerIn),
      processLock (processLockIn),
      sendGain (sendGainIn),
      deviceSampleRate (deviceSampleRateIn),
      deviceBlockSize (deviceBlockSizeIn)
{
}

void HardwareLoopOps::setHardwareLoopSettings (const HardwareLoopSettings& settings)
{
    const juce::ScopedLock lock (processLock);
    hardwareSettings = settings;
    ensureLatencyBufferSize (2, juce::jmax (settings.latencySamples + deviceBlockSize * 4,
                                            deviceBlockSize * 8));
}

void HardwareLoopOps::setHardwareMode (bool shouldUseHardware)
{
    if (shouldUseHardware && ! hardwareSettings.isConfigured())
        return;

    hardwareMode.store (shouldUseHardware);
}

void HardwareLoopOps::ensureLatencyBufferSize (int numChannels, int capacity)
{
    capacity = juce::jmax (capacity, 1);
    if (latencyCapacity >= capacity && latencyBuffer.getNumChannels() >= numChannels)
        return;

    latencyBuffer.setSize (numChannels, capacity, false, true, false);
    latencyBuffer.clear();
    latencyWritePos = 0;
    latencyCapacity = capacity;
}

/**
 * Audio thread: copy the hardware return pair into the circular latency
 * buffer and update the return meters in the same pass (one loop instead of
 * two over the input). Mono-safe: a missing right channel mirrors the left.
 */
void HardwareLoopOps::pushReturnToLatencyBuffer (const float* const* inputChannelData,
                                                  int numInputChannels,
                                                  int numSamples)
{
    if (latencyCapacity <= 0 || inputChannelData == nullptr)
        return;

    const int retL = hardwareSettings.returnChannelL;
    const int retR = hardwareSettings.returnChannelR;
    const float* inL = (retL >= 0 && retL < numInputChannels) ? inputChannelData[retL] : nullptr;
    const float* inR = (retR >= 0 && retR < numInputChannels) ? inputChannelData[retR] : nullptr;

    float peakL = 0.0f;
    float peakR = 0.0f;
    double sumSq = 0.0;
    int meterSamples = 0;

    for (int i = 0; i < numSamples; ++i)
    {
        const float l = inL != nullptr ? inL[i] : 0.0f;
        const float r = inR != nullptr ? inR[i] : l;
        latencyBuffer.setSample (0, latencyWritePos, l);
        if (latencyBuffer.getNumChannels() > 1)
            latencyBuffer.setSample (1, latencyWritePos, r);

        peakL = juce::jmax (peakL, std::abs (l));
        peakR = juce::jmax (peakR, std::abs (r));
        sumSq += (double) l * (double) l + (double) r * (double) r;
        meterSamples += 2;

        latencyWritePos = (latencyWritePos + 1) % latencyCapacity;
    }

    returnPeakL.store (peakL);
    returnPeakR.store (peakR);
    if (meterSamples > 0)
        returnRms.store ((float) std::sqrt (sumSq / (double) meterSamples));
}

/**
 * Audio thread: read the return signal latencySamples behind the write head,
 * i.e. time-aligned with the software path so A/B'ing hardware vs plugin
 * doesn't smear transients.
 */
void HardwareLoopOps::readDelayedReturn (juce::AudioBuffer<float>& dest, int numSamples)
{
    dest.clear();
    if (latencyCapacity <= 0)
        return;

    const int delay = juce::jlimit (0, latencyCapacity - 1, hardwareSettings.latencySamples);
    int readPos = latencyWritePos - delay - numSamples;
    while (readPos < 0)
        readPos += latencyCapacity;

    const int channels = juce::jmin (2, dest.getNumChannels());
    for (int i = 0; i < numSamples; ++i)
    {
        for (int ch = 0; ch < channels; ++ch)
            dest.setSample (ch, i, latencyBuffer.getSample (ch % latencyBuffer.getNumChannels(), readPos));

        readPos = (readPos + 1) % latencyCapacity;
    }
}

/**
 * Equal-power (sin/cos) crossfade between the software plugin output and the
 * latency-compensated hardware return, ramped over ~8 ms so toggling "Use
 * Hardware" never clicks. Same shape as applyBypassCrossfade.
 */
void HardwareLoopOps::applyMonitorCrossfade (juce::AudioBuffer<float>& softwareBuffer,
                                             const juce::AudioBuffer<float>& hardwareBuffer,
                                             int numSamples)
{
    // Hardware Audio Setup mutes the software effect so Test hears the loop
    // return only. Force the monitor crossfade to hardware in that case —
    // otherwise software-mode (fade target 0) leaves the monitor silent even
    // though send/return metering still moves.
    const float target = (softwareEffectMuted.load() || hardwareMode.load()) ? 1.0f : 0.0f;
    HostAudioHelpers::applyEqualPowerCrossfade (softwareBuffer,
                                                 hardwareBuffer,
                                                 hardwareFade,
                                                 target,
                                                 hardwareFadeLengthSamples,
                                                 numSamples,
                                                 true);
}

/**
 * Route the final stereo monitor mix to wherever the user wants to hear it:
 * either a channel pair on the loop device itself, or (for screen-recording
 * setups) the FIFO feeding the separate monitor output device.
 */
void HardwareLoopOps::writeMonitorSamples (const juce::AudioBuffer<float>& stereoMonitor,
                                           int numSamples,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           MonitorOutputBridge& monitorOutput)
{
    if (hardwareSettings.usesSeparateMonitorOutput())
    {
        monitorOutput.pushMonitorOutput (stereoMonitor.getReadPointer (0),
                                         stereoMonitor.getReadPointer (1),
                                         numSamples);
        return;
    }

    const int monL = hardwareSettings.monitorChannelL;
    const int monR = hardwareSettings.monitorChannelR;

    if (monL >= 0 && monL < numOutputChannels && outputChannelData[monL] != nullptr)
        juce::FloatVectorOperations::copy (outputChannelData[monL],
                                           stereoMonitor.getReadPointer (0), numSamples);
    if (monR >= 0 && monR < numOutputChannels && outputChannelData[monR] != nullptr)
        juce::FloatVectorOperations::copy (outputChannelData[monR],
                                           stereoMonitor.getReadPointer (1), numSamples);
}

void HardwareLoopOps::storeSendMeters (float peakL, float peakR, float rms)
{
    sendPeakL.store (peakL);
    sendPeakR.store (peakR);
    sendRms.store (rms);
}

void HardwareLoopOps::prepareForAudioDevice (int fadeLengthSamples)
{
    hardwareFadeLengthSamples = fadeLengthSamples;
    hardwareFade = hardwareMode.load() ? 1.0f : 0.0f;
    ensureLatencyBufferSize (2, juce::jmax (hardwareSettings.latencySamples + deviceBlockSize * 4,
                                            deviceBlockSize * 8));
}

bool HardwareLoopOps::processLoopOpInCallback (const float* const* inputChannelData,
                                               int numInputChannels,
                                               float* const* outputChannelData,
                                               int numOutputChannels,
                                               int numSamples,
                                               MonitorOutputBridge& monitorOutput)
{
    const bool loopConfigured = hardwareSettings.isConfigured();
    const auto op = loopOp.load();
    if (op == LoopOp::idle || ! loopConfigured)
        return false;

    const int sendL = hardwareSettings.sendChannelL;
    const int sendR = hardwareSettings.sendChannelR;

    double sendSumSq = 0.0;
    int sendCount = 0;
    float peakL = 0.0f;
    float peakR = 0.0f;
    juce::AudioBuffer<float> loopMonitor (2, numSamples);
    loopMonitor.clear();

    for (int i = 0; i < numSamples; ++i)
    {
        float l = 0.0f, r = 0.0f;
        if (loopPlayPosition < loopPlayBuffer.getNumSamples())
        {
            l = loopPlayBuffer.getSample (0, loopPlayPosition);
            r = loopPlayBuffer.getNumChannels() > 1
                    ? loopPlayBuffer.getSample (1, loopPlayPosition)
                    : l;
            ++loopPlayPosition;
        }

        if (sendL >= 0 && sendL < numOutputChannels && outputChannelData[sendL] != nullptr)
            outputChannelData[sendL][i] = l;
        if (sendR >= 0 && sendR < numOutputChannels && outputChannelData[sendR] != nullptr)
            outputChannelData[sendR][i] = r;

        loopMonitor.setSample (0, i, l);
        loopMonitor.setSample (1, i, r);

        peakL = juce::jmax (peakL, std::abs (l));
        peakR = juce::jmax (peakR, std::abs (r));
        sendSumSq += (double) l * (double) l + (double) r * (double) r;
        sendCount += 2;

        if (loopRecordPosition < loopRecordCapacity)
        {
            const int retL = hardwareSettings.returnChannelL;
            const int retR = hardwareSettings.returnChannelR;
            const float rl = (retL >= 0 && retL < numInputChannels && inputChannelData != nullptr
                              && inputChannelData[retL] != nullptr)
                                 ? inputChannelData[retL][i] : 0.0f;
            const float rr = (retR >= 0 && retR < numInputChannels && inputChannelData != nullptr
                              && inputChannelData[retR] != nullptr)
                                 ? inputChannelData[retR][i] : rl;
            loopRecordBuffer.setSample (0, loopRecordPosition, rl);
            if (loopRecordBuffer.getNumChannels() > 1)
                loopRecordBuffer.setSample (1, loopRecordPosition, rr);
            ++loopRecordPosition;
        }
    }

    writeMonitorSamples (loopMonitor, numSamples, outputChannelData, numOutputChannels, monitorOutput);

    if (sendCount > 0)
        sendRms.store ((float) std::sqrt (sendSumSq / (double) sendCount));
    sendPeakL.store (peakL);
    sendPeakR.store (peakR);

    // Auto-finish logic. Capture ops are normally ended by the message
    // thread (silence tail / target duration in captureHardwareToFile);
    // this is the audio-side backstop: play finished plus 2 s of extra
    // return, or the record buffer filled.
    const bool playDone = loopPlayPosition >= loopPlayBuffer.getNumSamples();
    const bool recordFull = loopRecordPosition >= loopRecordCapacity;
    if (playDone && (op == LoopOp::calibrate || recordFull
                     || loopRecordPosition >= loopPlayBuffer.getNumSamples()
                                                + (int) (deviceSampleRate * 2.0)))
    {
        // For calibrate, keep recording a bit after the impulse ends.
        if (op == LoopOp::calibrate && ! recordFull
            && loopRecordPosition < loopPlayBuffer.getNumSamples()
                                        + (int) (deviceSampleRate * 1.5))
        {
            // keep going
        }
        else
        {
            loopOp.store (LoopOp::idle);
            loopOpFinished.store (true);
        }
    }

    return true;
}

int HardwareLoopOps::findCorrelationPeakLatency (const juce::AudioBuffer<float>& impulseMono,
                                                 const juce::AudioBuffer<float>& recorded,
                                                 int recordedSamples,
                                                 int impulseSamples)
{
    // Cross-correlate mono impulse against the mono-summed return, brute-force
    // time domain. O(impulse × lag) is fine here: the impulse is ~2 s and this
    // runs once per calibration click, not per block.
    // TODO: switch to FFT-based correlation if calibration ever feels slow.
    // Calibration Boost will call this once per impulse and combine results.
    double bestCorr = -1.0e300;
    int bestLag = 0;
    const int maxLag = recordedSamples - impulseSamples;

    for (int lag = 0; lag <= maxLag; ++lag)
    {
        double corr = 0.0;
        for (int i = 0; i < impulseSamples; ++i)
        {
            const float ret = 0.5f * (recorded.getSample (0, lag + i)
                                      + recorded.getSample (1, lag + i));
            corr += (double) impulseMono.getSample (0, i) * (double) ret;
        }

        if (corr > bestCorr)
        {
            bestCorr = corr;
            bestLag = lag;
        }
    }

    return bestLag;
}

float HardwareLoopOps::measurePeakLoopGainDb (const juce::AudioBuffer<float>& impulseMono,
                                              const juce::AudioBuffer<float>& recorded,
                                              int recordedSamples,
                                              int impulseSamples,
                                              int bestLag)
{
    // Peak amplitude ratio near the detected lag.
    // Calibration Boost will replace this with BS.1770 integrated LUFS median.
    float impulsePeak = 0.0f, returnPeakLocal = 0.0f;
    for (int i = 0; i < impulseSamples; ++i)
    {
        impulsePeak = juce::jmax (impulsePeak, std::abs (impulseMono.getSample (0, i)));
        if (bestLag + i < recordedSamples)
        {
            const float ret = 0.5f * (recorded.getSample (0, bestLag + i)
                                      + recorded.getSample (1, bestLag + i));
            returnPeakLocal = juce::jmax (returnPeakLocal, std::abs (ret));
        }
    }

    return (impulsePeak > 1.0e-8f)
               ? juce::Decibels::gainToDecibels (returnPeakLocal / impulsePeak)
               : -120.0f;
}

bool HardwareLoopOps::autoDetectLatency (const juce::File& impulseFile,
                                         int& outLatencySamples,
                                         float& outLoopGainDb,
                                         juce::String& error)
{
    if (! hardwareSettings.isConfigured())
    {
        error = "Configure a hardware audio device first";
        return false;
    }

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (impulseFile));
    if (reader == nullptr)
    {
        error = "Failed to read impulse file: " + impulseFile.getFullPathName();
        return false;
    }

    const int impulseSamples = (int) reader->lengthInSamples;
    loopPlayBuffer.setSize (2, impulseSamples, false, true, false);
    reader->read (&loopPlayBuffer, 0, impulseSamples, 0, true, true);

    const int recordSamples = impulseSamples + (int) (deviceSampleRate * 2.0) + deviceBlockSize * 4;
    loopRecordBuffer.setSize (2, recordSamples, false, true, false);
    loopRecordBuffer.clear();
    loopPlayPosition = 0;
    loopRecordPosition = 0;
    loopRecordCapacity = recordSamples;
    loopOpFinished.store (false);
    loopOp.store (LoopOp::calibrate);

    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 8000.0;
    while (! loopOpFinished.load() && juce::Time::getMillisecondCounterHiRes() < deadline)
        HostAudioHelpers::pumpMessageThreadMs (10);

    loopOp.store (LoopOp::idle);

    if (! loopOpFinished.load())
    {
        error = "Latency detection timed out";
        return false;
    }

    const int recorded = juce::jmin (loopRecordPosition, loopRecordCapacity);
    if (recorded < impulseSamples)
    {
        error = "Not enough return audio recorded";
        return false;
    }

    juce::AudioBuffer<float> impulseMono (1, impulseSamples);
    for (int i = 0; i < impulseSamples; ++i)
    {
        const float l = loopPlayBuffer.getSample (0, i);
        const float r = loopPlayBuffer.getNumChannels() > 1 ? loopPlayBuffer.getSample (1, i) : l;
        impulseMono.setSample (0, i, 0.5f * (l + r));
    }

    const int bestLag = findCorrelationPeakLatency (impulseMono, loopRecordBuffer,
                                                    recorded, impulseSamples);
    outLatencySamples = bestLag;
    outLoopGainDb = measurePeakLoopGainDb (impulseMono, loopRecordBuffer,
                                           recorded, impulseSamples, bestLag);

    hardwareSettings.latencySamples = bestLag;
    ensureLatencyBufferSize (2, juce::jmax (bestLag + deviceBlockSize * 4, deviceBlockSize * 8));
    return true;
}

bool HardwareLoopOps::captureHardwareToFile (const juce::File& fixtureFile,
                                             const juce::File& outputFile,
                                             double tailSilenceSeconds,
                                             double silenceThresholdDb,
                                             double maxTailSeconds,
                                             juce::String& error,
                                             const std::atomic<bool>* cancelRequested,
                                             double targetDurationSeconds)
{
    if (! hardwareSettings.isConfigured())
    {
        error = "Configure a hardware audio device first";
        return false;
    }

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (fixtureFile));
    if (reader == nullptr)
    {
        error = "Failed to read fixture: " + fixtureFile.getFullPathName();
        return false;
    }

    const int sourceSamples = (int) reader->lengthInSamples;
    if (sourceSamples <= 0)
    {
        error = "Fixture has no audio samples: " + fixtureFile.getFullPathName();
        return false;
    }

    juce::AudioBuffer<float> sourceBuffer (juce::jmax (1, (int) reader->numChannels), sourceSamples);
    reader->read (sourceBuffer.getArrayOfWritePointers(),
                  sourceBuffer.getNumChannels(),
                  0,
                  sourceSamples);

    // Resample the fixture to the *device* rate with linear interpolation.
    // The loop-op playback path streams loopPlayBuffer 1:1 at device rate, so
    // skipping this played 44.1k fixtures at 48k speed (an early bug heard as
    // "very slow playback"). Linear interpolation is adequate: the hardware
    // D/A -> analog FX -> A/D round trip dominates any interpolation error.
    const double sourceRate = reader->sampleRate > 1.0 ? reader->sampleRate : deviceSampleRate;
    const int fixtureSamples = juce::jmax (1, (int) std::llround ((double) sourceSamples * deviceSampleRate / sourceRate));
    loopPlayBuffer.setSize (2, fixtureSamples, false, true, false);

    for (int i = 0; i < fixtureSamples; ++i)
    {
        const double srcPos = (double) i * sourceRate / deviceSampleRate;
        const int src0 = juce::jlimit (0, sourceSamples - 1, (int) std::floor (srcPos));
        const int src1 = juce::jmin (sourceSamples - 1, src0 + 1);
        const float frac = (float) juce::jlimit (0.0, 1.0, srcPos - (double) src0);

        for (int ch = 0; ch < 2; ++ch)
        {
            const int srcCh = juce::jmin (ch, sourceBuffer.getNumChannels() - 1);
            const float a = sourceBuffer.getSample (srcCh, src0);
            const float b = sourceBuffer.getSample (srcCh, src1);
            loopPlayBuffer.setSample (ch, i, a + (b - a) * frac);
        }
    }

    const float send = sendGain.load();
    if (send != 1.0f)
        loopPlayBuffer.applyGain (send);

    const int maxRecord = fixtureSamples
                          + (int) (deviceSampleRate * maxTailSeconds)
                          + hardwareSettings.latencySamples
                          + deviceBlockSize * 4;
    loopRecordBuffer.setSize (2, maxRecord, false, true, false);
    loopRecordBuffer.clear();
    loopPlayPosition = 0;
    loopRecordPosition = 0;
    loopRecordCapacity = maxRecord;
    loopOpFinished.store (false);
    loopOp.store (LoopOp::capture);

    const float silenceThresh = juce::Decibels::decibelsToGain ((float) silenceThresholdDb);
    const int silenceHold = juce::jmax (1, (int) (deviceSampleRate * tailSilenceSeconds));
    // When the caller knows how long the take should be (the "Capture Both"
    // flow passes the software render's length), stop on duration instead of
    // relying on silence detection — analog return noise floors can sit above
    // the silence threshold forever. A quarter-second of slack keeps the
    // hardware file at least as long as the software one.
    const int targetUsableSamples = targetDurationSeconds > 0.0
                                        ? (int) std::llround (targetDurationSeconds * deviceSampleRate)
                                        : 0;
    const int targetSlackSamples = targetUsableSamples > 0
                                        ? juce::jmax (deviceBlockSize * 2, (int) (deviceSampleRate * 0.25))
                                        : 0;
    int silentSamples = 0;
    bool pastFixture = false;
    int lastRecorded = -1;
    int stagnantIterations = 0;
    bool stopRequested = false;

    const auto deadline = juce::Time::getMillisecondCounterHiRes()
                          + (maxTailSeconds + (double) fixtureSamples / deviceSampleRate + 5.0) * 1000.0;

    while (juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        // Cancel means "stop recording, keep what we have" — the user hit
        // Cancel to end the take, not to throw it away. We only fail below if
        // nothing usable was recorded yet.
        if (cancelRequested != nullptr && cancelRequested->load())
        {
            loopOp.store (LoopOp::idle);
            stopRequested = true;
            break;
        }

        HostAudioHelpers::pumpMessageThreadMs (10);

        const int recorded = loopRecordPosition;
        if (recorded == lastRecorded)
            ++stagnantIterations;
        else
            stagnantIterations = 0;

        lastRecorded = recorded;

        // Stall detection: if the audio callback stops advancing the record
        // position (device unplugged, CoreAudio wedged after a device
        // restart), bail with a diagnosable error instead of spinning until
        // the deadline. The threshold is generous (~30 s) because a slow
        // device restart can legitimately pause the callback for a while.
        if (stagnantIterations > 1500) // about 30s with sleep+dispatch cadence
        {
            loopOp.store (LoopOp::idle);
            error = "Hardware capture stalled (audio device not advancing)";
            return false;
        }

        if (loopPlayPosition >= fixtureSamples)
            pastFixture = true;

        const int usableNow = juce::jmax (0, recorded - hardwareSettings.latencySamples);
        if (pastFixture && targetUsableSamples > 0
            && usableNow >= targetUsableSamples + targetSlackSamples)
        {
            loopOp.store (LoopOp::idle);
            loopOpFinished.store (true);
            break;
        }

        if (pastFixture && recorded > hardwareSettings.latencySamples + fixtureSamples)
        {
            const int checkFrom = juce::jmax (0, recorded - deviceBlockSize);
            float peak = 0.0f;
            for (int i = checkFrom; i < recorded; ++i)
                peak = juce::jmax (peak,
                                   std::abs (loopRecordBuffer.getSample (0, i)),
                                   std::abs (loopRecordBuffer.getSample (1, i)));

            if (peak < silenceThresh)
                silentSamples += deviceBlockSize;
            else
                silentSamples = 0;

            if (silentSamples >= silenceHold || recorded >= maxRecord)
            {
                loopOp.store (LoopOp::idle);
                loopOpFinished.store (true);
                break;
            }
        }
    }

    loopOp.store (LoopOp::idle);

    // Trim the measured loop latency off the head so the file starts where
    // the fixture actually started — this keeps hardware captures sample-
    // aligned with offline software renders for A/B comparison.
    const int recorded = juce::jmin (loopRecordPosition, loopRecordCapacity);
    const int latency = juce::jlimit (0, recorded, hardwareSettings.latencySamples);
    const int usable = recorded - latency;
    if (stopRequested && usable <= 0)
    {
        error = "Capture cancelled before usable audio was recorded";
        return false;
    }

    if (usable <= 0)
    {
        error = "Hardware capture produced no usable audio (check latency / routing)";
        return false;
    }

    juce::AudioBuffer<float> trimmed (2, usable);
    for (int ch = 0; ch < 2; ++ch)
        trimmed.copyFrom (ch, 0, loopRecordBuffer, ch, latency, usable);

    outputFile.deleteFile();
    outputFile.getParentDirectory().createDirectory();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream (outputFile.createOutputStream());
    if (stream == nullptr)
    {
        error = "Failed to create output file: " + outputFile.getFullPathName();
        return false;
    }

    using Opts = juce::AudioFormatWriterOptions;
    std::unique_ptr<juce::OutputStream> ownedStream (stream.release());
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (ownedStream,
                             Opts{}
                                 .withSampleRate (deviceSampleRate)
                                 .withNumChannels (2)
                                 .withBitsPerSample (24)));
    if (writer == nullptr)
    {
        error = "Failed to create WAV writer";
        return false;
    }

    if (! writer->writeFromAudioSampleBuffer (trimmed, 0, usable))
    {
        error = "Failed to write hardware capture WAV";
        return false;
    }

    return true;
}
