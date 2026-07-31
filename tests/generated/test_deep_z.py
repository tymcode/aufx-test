"""Auto-generated tests from session 'DEEP/Z exploration'.

Regenerate with:
  aufx-test session export DEEP/Z exploration -o tests/generated/test_deep_z.py
"""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from aufx_test import Waveform, compare_waveforms
from aufx_test.comparison import ComparisonThresholds

ROOT = Path(__file__).resolve().parents[2]


def _resolve(path: str | None):
    if not path:
        return None
    p = Path(path).expanduser()
    if not p.is_absolute():
        p = ROOT / p
    return p


SETUPS = json.loads(
    """
[
    {
        "name": "long_tail",
        "input_audio": "sessions/deep_z_exploration/artifacts/long_67dc49d2_input.wav",
        "reference_output": "sessions/deep_z_exploration/artifacts/long_67dc49d2_output.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "sessions/deep_z_exploration/artifacts/long_67dc49d2.aupreset",
        "notes": "Captured from plugin_host_app",
        "source_snapshot_id": "67dc49d2",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": false
    },
    {
        "name": "nonlin_diffus",
        "input_audio": "sessions/deep_z_exploration/artifacts/nonlin_98009644_input.wav",
        "reference_output": "sessions/deep_z_exploration/artifacts/nonlin_98009644_output_bkn.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "sessions/deep_z_exploration/artifacts/nonlin_98009644.aupreset",
        "notes": "Captured from plugin_host_app",
        "source_snapshot_id": "98009644",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": false
    },
    {
        "name": "ddl_flange",
        "input_audio": "sessions/deep_z_exploration/artifacts/ddl_1cc07100_input.wav",
        "reference_output": "sessions/deep_z_exploration/artifacts/ddl_1cc07100_output_bkn.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "sessions/deep_z_exploration/artifacts/ddl_1cc07100.aupreset",
        "notes": "Captured from plugin_host_app",
        "source_snapshot_id": "1cc07100",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": false
    }
]
"""
)


@pytest.mark.parametrize("setup", SETUPS, ids=[s["name"] for s in SETUPS])
def test_session_setup(setup, plugin_host):
    """Replay a promoted manual exploration snapshot."""
    host = plugin_host
    input_path = _resolve(setup.get("input_audio"))
    reference_path = _resolve(setup.get("reference_output"))
    preset_path = _resolve(setup.get("preset_file"))

    missing = [
        label
        for label, path in (
            ("input_audio", input_path),
            ("reference_output", reference_path),
            ("preset_file", preset_path),
        )
        if path is not None and not path.is_file()
    ]
    if missing:
        pytest.skip("Missing local artifacts: " + ", ".join(missing))

    if setup.get("plugin_path"):
        host.load_plugin(setup["plugin_path"])
    if preset_path is not None:
        host.load_preset(str(preset_path))
    if setup.get("parameters"):
        host.set_parameters(setup["parameters"])

    input_wav = Waveform.from_file(str(input_path))
    reference = Waveform.from_file(str(reference_path))
    actual = host.process(input_wav)

    thresholds = ComparisonThresholds(**setup.get("thresholds", {}))
    result = compare_waveforms(actual, reference, thresholds=thresholds)
    if setup.get("expect_match", True):
        assert result.passed, result.summary()
    else:
        assert not result.passed, (
            "Negative case: output still matches the broken reference\n"
            + result.summary()
        )
