"""Tests for frequency-band analysis."""

from aufx_test import band_amplitude_over_time, num_of_bands
from aufx_test.spectrum import FrequencyBand, analysis_bands, log_spaced_bands


def test_band_amplitude_over_time(sine_mono):
    bands = (
        FrequencyBand("low", 200, 500),
        FrequencyBand("target", 400, 480),
    )
    result = band_amplitude_over_time(
        sine_mono,
        bands=bands,
        window_samples=2048,
        hop_samples=1024,
    )
    assert "target" in result
    assert "times" in result["target"]
    target_amp = result["target"]["channel_0"]
    low_amp = result["low"]["channel_0"]
    assert target_amp.mean() > low_amp.mean() * 0.5


def test_default_bands_cover_spectrum(sine_mono):
    result = band_amplitude_over_time(sine_mono, window_samples=4096, hop_samples=2048)
    assert len(result) == num_of_bands()
    for band_data in result.values():
        assert len(band_data["times"]) > 0


def test_analysis_bands_respects_count():
    assert len(analysis_bands(7)) == 7
    assert len(log_spaced_bands(4)) == 4
    assert analysis_bands(7)[0].name == "sub"
