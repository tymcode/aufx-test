"""Tests for aufx-test calibrate-plot."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from aufx_test.cli.calibrate_plot import _cmd_calibrate_plot


def test_calibrate_plot_writes_png(tmp_path: Path) -> None:
    payload = {
        "plot_name": "studio_insert",
        "path": "hardware",
        "device_name": "Test Device",
        "bypassed": False,
        "mix_amount": 1.0,
        "sample_rate": 48000,
        "buffer_size": 512,
        "latency_samples": 100,
        "max_abs_measured_minus_ideal_peak_db": 0.4,
        "points": [
            {
                "send_db": -12.0,
                "measured_rms_db": -16.0,
                "measured_peak_db": -12.5,
                "measured_lufs": -16.2,
                "clipped": False,
            },
            {
                "send_db": 0.0,
                "measured_rms_db": -4.0,
                "measured_peak_db": -0.3,
                "measured_lufs": -4.1,
                "clipped": False,
            },
        ],
        "any_clipped": False,
        "analyse_skip_seconds": 0.75,
        "analyse_window_seconds": 2.5,
    }
    json_path = tmp_path / "studio_insert.json"
    json_path.write_text(json.dumps(payload))
    png_path = tmp_path / "studio_insert.png"

    args = argparse.Namespace(json_path=json_path, output=png_path)
    assert _cmd_calibrate_plot(args) == 0
    assert png_path.exists()
    assert png_path.stat().st_size > 0


def test_calibrate_plot_legacy_dual_path_json(tmp_path: Path) -> None:
    payload = {
        "device_name": "Legacy",
        "points": [
            {
                "send_db": -6.0,
                "hw_rms_db": -10.0,
                "sw_rms_db": -9.0,
                "hw_peak_db": -6.2,
                "sw_peak_db": -6.0,
            }
        ],
    }
    json_path = tmp_path / "legacy.json"
    json_path.write_text(json.dumps(payload))
    png_path = tmp_path / "legacy.png"
    assert _cmd_calibrate_plot(argparse.Namespace(json_path=json_path, output=png_path)) == 0
    assert png_path.exists()
