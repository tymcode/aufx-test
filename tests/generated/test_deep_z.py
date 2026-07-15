"""Auto-generated tests from session 'DEEP/Z exploration'.

Regenerate with:
  aufx-test session export DEEP/Z exploration -o tests/generated/test_deep_z.py
"""

from __future__ import annotations

import json

import pytest

from aufx_test import Waveform, compare_waveforms
from aufx_test.comparison import ComparisonThresholds

SETUPS = json.loads(
    """
[
    {
        "name": "long_tail",
        "input_audio": "/Users/mikejennings/dev/aufx-test/sessions/deep_z_exploration/artifacts/long_67dc49d2_input.wav",
        "reference_output": "/Users/mikejennings/dev/aufx-test/sessions/deep_z_exploration/artifacts/long_67dc49d2_output.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "/Users/mikejennings/dev/aufx-test/sessions/deep_z_exploration/artifacts/long_67dc49d2.aupreset",
        "notes": "Captured from plugin_host_app",
        "source_snapshot_id": "67dc49d2",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": true
    },
    {
        "name": "nonlin_diffus",
        "input_audio": "/Users/mikejennings/dev/aufx-test/sessions/deep_z_exploration/artifacts/nonlin_98009644_input.wav",
        "reference_output": "/Users/mikejennings/dev/aufx-test/sessions/deep_z_exploration/artifacts/nonlin_98009644_output.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "/Users/mikejennings/dev/aufx-test/sessions/deep_z_exploration/artifacts/nonlin_98009644.aupreset",
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
        "input_audio": "/Users/mikejennings/dev/aufx-test/sessions/deep_z_exploration/artifacts/ddl_1cc07100_input.wav",
        "reference_output": "/Users/mikejennings/dev/aufx-test/sessions/deep_z_exploration/artifacts/ddl_1cc07100_output_bkn.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "/Users/mikejennings/dev/aufx-test/sessions/deep_z_exploration/artifacts/ddl_1cc07100.aupreset",
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
    if setup.get("plugin_path"):
        host.load_plugin(setup["plugin_path"])
    if setup.get("preset_file"):
        host.load_preset(setup["preset_file"])
    if setup.get("parameters"):
        host.set_parameters(setup["parameters"])

    input_wav = Waveform.from_file(setup["input_audio"])
    reference = Waveform.from_file(setup["reference_output"])
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
