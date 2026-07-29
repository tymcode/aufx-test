"""Auto-generated tests from session 'QDV1 exploration'.

Regenerate with:
  aufx-test session export QDV1 exploration -o tests/generated/test_qdv1_exploration.py
"""

from __future__ import annotations

import json

import pytest

from aufx_test import Waveform
from aufx_test.comparison import ComparisonThresholds
from aufx_test.reporting import assert_setup_comparison

SETUPS = json.loads(
    """
[
    {
        "name": "kingschamber",
        "input_audio": "/Users/mikejennings/Library/AU Effects Explorer/sessions/qdv1_exploration/artifacts/kingschamber_0784edf0_input.wav",
        "reference_output": "/Users/mikejennings/Library/AU Effects Explorer/sessions/qdv1_exploration/artifacts/kingschamber_0784edf0_output_bkn.wav",
        "parameters": {},
        "plugin_path": "AudioUnit:Effects/aufx,QDV1,TDSP",
        "preset_file": "/Users/mikejennings/Library/AU Effects Explorer/sessions/qdv1_exploration/artifacts/kingschamber_0784edf0.aupreset",
        "notes": "Captured from AU Effects Explorer",
        "source_snapshot_id": "0784edf0",
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
    assert_setup_comparison(
        actual,
        reference,
        setup=setup,
        thresholds=thresholds,
        input_audio=input_wav,
        allow_extra_actual_tail=True,
    )
