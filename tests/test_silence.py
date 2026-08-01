"""Tests for silence analysis."""

from aufx_test import distance_from_silence, measure_silence_regions
from aufx_test.audio import Waveform
from aufx_test.silence import leading_silence_samples, trim_leading_silence

import numpy as np


def test_distance_from_silence_returns_channels(sine_stereo):
    result = distance_from_silence(sine_stereo, window_samples=1024, hop_samples=512)
    assert "times" in result
    assert "channel_0" in result
    assert "channel_1" in result
    assert len(result["times"]) == len(result["channel_0"])
    assert result["channel_0"].min() > 0  # sine is above silence threshold


def test_measure_silence_regions_per_channel(stereo_with_silence, sample_rate):
    regions = measure_silence_regions(
        stereo_with_silence,
        threshold_db=-60.0,
        min_duration_samples=int(0.05 * sample_rate),
    )
    assert 0 in regions
    assert 1 in regions
    assert len(regions[0]) >= 1  # ch0 has a silent gap
    assert all(r.length_samples > 0 for r in regions[0])


def test_silence_region_duration(stereo_with_silence, sample_rate):
    regions = measure_silence_regions(
        stereo_with_silence,
        min_duration_samples=int(0.05 * sample_rate),
    )
    r = regions[0][0]
    assert r.duration_seconds(sample_rate) > 0.1


def test_trim_leading_silence_removes_prefix(sample_rate):
    silence = Waveform.silence(0.25, sample_rate=sample_rate, channels=2)
    # Cosine starts at full scale so onset is unambiguous (sine starts at 0).
    tone = Waveform.sine(
        440.0,
        duration_seconds=0.1,
        sample_rate=sample_rate,
        channels=2,
        phase_rad=0.5 * np.pi,
    )
    combined = tone.with_data(np.vstack([silence.data, tone.data]))
    assert leading_silence_samples(combined) == silence.num_samples
    trimmed = trim_leading_silence(combined)
    assert trimmed.num_samples == tone.num_samples
    assert np.allclose(trimmed.data, tone.data)


def test_trim_leading_silence_leaves_onset_aligned(sample_rate):
    tone = Waveform.sine(
        440.0,
        duration_seconds=0.1,
        sample_rate=sample_rate,
        channels=1,
        phase_rad=0.5 * np.pi,
    )
    assert leading_silence_samples(tone) == 0
    assert trim_leading_silence(tone).num_samples == tone.num_samples


def test_trim_leading_silence_leaves_all_silent_unchanged(sample_rate):
    silence = Waveform.silence(0.1, sample_rate=sample_rate, channels=1)
    trimmed = trim_leading_silence(silence)
    assert trimmed.num_samples == silence.num_samples
