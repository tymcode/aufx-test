#include "HostClockMetronome.h"

HostClockMetronome::HostClockMetronome (juce::AudioFormatManager& formatManagerIn,
                                        double& deviceSampleRateIn)
    : formatManager (formatManagerIn),
      deviceSampleRate (deviceSampleRateIn)
{
}

void HostClockMetronome::setHostClockEnabled (bool enabled)
{
    const bool wasEnabled = hostClockEnabled.exchange (enabled);

    if (enabled && ! wasEnabled)
        pendingTransport.store (PendingTransport::start);
    else if (! enabled && wasEnabled)
        pendingTransport.store (PendingTransport::stop);
}

void HostClockMetronome::setHostClockBpm (double bpm)
{
    hostClockBpm.store (juce::jlimit (20.0, 999.0, bpm));
}

bool HostClockMetronome::consumeHostClockQuarterPulse()
{
    return quarterNotePulse.exchange (false);
}

bool HostClockMetronome::loadMetronomeClick (const juce::File& impulseFile, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (impulseFile));
    if (reader == nullptr)
    {
        error = "Could not read metronome click: " + impulseFile.getFullPathName();
        return false;
    }

    juce::AudioBuffer<float> full ((int) reader->numChannels, (int) reader->lengthInSamples);
    if (! reader->read (&full, 0, (int) reader->lengthInSamples, 0, true, true))
    {
        error = "Failed to decode metronome click: " + impulseFile.getFullPathName();
        return false;
    }

    // fixtures/impulse.wav is a 2s IR with silence then a unit spike — use the peak
    // (the actual impulse) rather than sample 0, which is silent.
    int peakIndex = 0;
    float peakAbs = 0.0f;
    for (int ch = 0; ch < full.getNumChannels(); ++ch)
    {
        const float* data = full.getReadPointer (ch);
        for (int i = 0; i < full.getNumSamples(); ++i)
        {
            const float a = std::abs (data[i]);
            if (a > peakAbs)
            {
                peakAbs = a;
                peakIndex = i;
            }
        }
    }

    if (peakAbs < 1.0e-6f)
    {
        error = "Metronome click file has no impulse energy: " + impulseFile.getFullPathName();
        return false;
    }

    const int clickLength = juce::jmin (256, full.getNumSamples() - peakIndex);
    metronomeClickBuffer.setSize (1, clickLength);
    metronomeClickBuffer.clear();

    for (int i = 0; i < clickLength; ++i)
    {
        float sample = 0.0f;
        for (int ch = 0; ch < full.getNumChannels(); ++ch)
            sample += full.getSample (ch, peakIndex + i);
        sample /= (float) full.getNumChannels();
        metronomeClickBuffer.setSample (0, i, sample);
    }

    metronomeClickPosition = -1;
    pendingClickOffset = -1;
    return true;
}

void HostClockMetronome::setMetronomeClickEnabled (bool enabled)
{
    metronomeClickEnabled.store (enabled);
    if (! enabled)
    {
        metronomeClickPosition = -1;
        pendingClickOffset = -1;
    }
}

void HostClockMetronome::resetHostClockTiming()
{
    clockSampleCounter = 0.0;
    clockTicksSinceQuarter = 0;
    playHeadSamples.store (0);
}

void HostClockMetronome::stopHostClockPlayback()
{
    hostClockPlaying.store (false);
    resetHostClockTiming();
}

juce::Optional<juce::AudioPlayHead::PositionInfo> HostClockMetronome::getPosition() const
{
    juce::AudioPlayHead::PositionInfo info;

    const double bpm = hostClockBpm.load();
    const bool transportPlaying = hostClockEnabled.load() && hostClockPlaying.load();
    const juce::int64 samples = playHeadSamples.load();
    const double sampleRate = deviceSampleRate > 0.0 ? deviceSampleRate : 44100.0;
    const double seconds = (double) samples / sampleRate;
    const double ppq = seconds * bpm / 60.0;

    info.setBpm (bpm);
    info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
    info.setTimeInSamples (samples);
    info.setTimeInSeconds (seconds);
    info.setPpqPosition (ppq);
    info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
    info.setIsPlaying (transportPlaying);
    info.setIsRecording (false);
    info.setIsLooping (false);

    return info;
}

void HostClockMetronome::applyPendingHostClockTransport (juce::MidiBuffer& midi)
{
    const auto transport = pendingTransport.exchange (PendingTransport::none);
    switch (transport)
    {
        case PendingTransport::none:
            break;
        case PendingTransport::start:
            hostClockPlaying.store (true);
            resetHostClockTiming();
            midi.addEvent (juce::MidiMessage::midiStart(), 0);
            break;
        case PendingTransport::stop:
            hostClockPlaying.store (false);
            metronomeClickPosition = -1;
            pendingClickOffset = -1;
            midi.addEvent (juce::MidiMessage::midiStop(), 0);
            break;
        case PendingTransport::continue_:
            hostClockPlaying.store (true);
            midi.addEvent (juce::MidiMessage::midiContinue(), 0);
            break;
    }
}

void HostClockMetronome::generateHostClockMidi (juce::MidiBuffer& midi, int numSamples)
{
    const double bpm = hostClockBpm.load();
    if (bpm <= 0.0)
        return;

    const double samplesPerTick = deviceSampleRate * 60.0 / (bpm * 24.0);
    if (samplesPerTick <= 0.0)
        return;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        clockSampleCounter += 1.0;

        while (clockSampleCounter >= samplesPerTick)
        {
            clockSampleCounter -= samplesPerTick;
            midi.addEvent (juce::MidiMessage::midiClock(), sample);

            if (++clockTicksSinceQuarter >= 24)
            {
                clockTicksSinceQuarter = 0;
                quarterNotePulse.store (true);

                if (metronomeClickEnabled.load() && metronomeClickBuffer.getNumSamples() > 0)
                    pendingClickOffset = sample;
            }
        }
    }
}

void HostClockMetronome::mixMetronomeClick (juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (metronomeClickBuffer.getNumSamples() <= 0)
        return;

    int startInBlock = 0;
    if (pendingClickOffset >= 0)
    {
        metronomeClickPosition = 0;
        startInBlock = pendingClickOffset;
        pendingClickOffset = -1;
    }

    if (metronomeClickPosition < 0 || startInBlock >= numSamples)
        return;

    const int clickLength = metronomeClickBuffer.getNumSamples();

    for (int i = startInBlock; i < numSamples; ++i)
    {
        if (metronomeClickPosition >= clickLength)
        {
            metronomeClickPosition = -1;
            break;
        }

        const float click = metronomeClickBuffer.getSample (0, metronomeClickPosition++);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.addSample (ch, i, click);
    }

    if (metronomeClickPosition >= clickLength)
        metronomeClickPosition = -1;
}
