"""Tests for compare.config.json loading."""

from aufx_test.compare_config import CompareConfig, load_compare_config, num_of_bands
from aufx_test.spectrum import analysis_bands, log_spaced_bands


def test_default_compare_config_num_of_bands():
    assert num_of_bands() == 7


def test_load_compare_config_missing(tmp_path):
    data = load_compare_config(tmp_path / "missing.json")
    assert data.num_of_bands == 7
    assert data.window_samples == 4096
    assert data.thresholds.correlation_min == 0.95


def test_num_of_bands_from_file(tmp_path):
    path = tmp_path / "compare.config.json"
    path.write_text(
        """
        {
          "num_of_bands": 4,
          "band_low_hz": 40,
          "band_high_hz": 10000,
          "window_samples": 2048,
          "thresholds": {
            "correlation_min": 0.9,
            "rms_error_max": 0.1,
            "spectral_distance_max": 0.2
          }
        }
        """,
        encoding="utf-8",
    )
    cfg = load_compare_config(path)
    assert cfg.num_of_bands == 4
    assert cfg.window_samples == 2048
    assert cfg.band_low_hz == 40
    assert cfg.thresholds.correlation_min == 0.9
    assert cfg.thresholds.rms_error_max == 0.1
    assert cfg.thresholds.spectral_distance_max == 0.2
    bands = analysis_bands(config=cfg)
    assert len(bands) == 4
    assert bands[0].low_hz == 40


def test_log_spaced_uses_config_range():
    cfg = CompareConfig(num_of_bands=3, band_low_hz=100, band_high_hz=1000)
    bands = log_spaced_bands(3, low_hz=cfg.band_low_hz, high_hz=cfg.band_high_hz)
    assert bands[0].low_hz == 100
    assert bands[-1].high_hz == 1000
