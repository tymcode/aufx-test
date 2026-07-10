"""Tests for silence analysis."""

from juce_plugin_test import distance_from_silence, measure_silence_regions


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
