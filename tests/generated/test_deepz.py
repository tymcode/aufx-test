"""Auto-generated tests from session 'DEEPZ exploration'.

Regenerate with:
  aufx-test session export DEEPZ exploration -o tests/generated/test_deepz.py
"""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from aufx_test import Waveform
from aufx_test.comparison import ComparisonThresholds
from aufx_test.reporting import assert_setup_comparison

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
        "name": "vocals_antimatter_delays",
        "input_audio": "sessions/deepz_exploration/artifacts/vocals_21310df9_input.wav",
        "reference_output": "sessions/deepz_exploration/artifacts/vocals_21310df9_output_gld.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "sessions/deepz_exploration/artifacts/vocals_21310df9.aupreset",
        "notes": "Imported from goldens/deepz",
        "source_snapshot_id": "21310df9",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": true
    },
    {
        "name": "vocals_apollo_13",
        "input_audio": "sessions/deepz_exploration/artifacts/vocals_c791a054_input.wav",
        "reference_output": "sessions/deepz_exploration/artifacts/vocals_c791a054_output_gld.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "sessions/deepz_exploration/artifacts/vocals_c791a054.aupreset",
        "notes": "Imported from goldens/deepz",
        "source_snapshot_id": "c791a054",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": true
    },
    {
        "name": "vocals_bloom_star",
        "input_audio": "sessions/deepz_exploration/artifacts/vocals_c17f82f4_input.wav",
        "reference_output": "sessions/deepz_exploration/artifacts/vocals_c17f82f4_output_gld.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "sessions/deepz_exploration/artifacts/vocals_c17f82f4.aupreset",
        "notes": "Imported from goldens/deepz",
        "source_snapshot_id": "c17f82f4",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": true
    },
    {
        "name": "vocals_chordal_harmony",
        "input_audio": "sessions/deepz_exploration/artifacts/vocals_79038ef4_input.wav",
        "reference_output": "sessions/deepz_exploration/artifacts/vocals_79038ef4_output_gld.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "sessions/deepz_exploration/artifacts/vocals_79038ef4.aupreset",
        "notes": "Imported from goldens/deepz",
        "source_snapshot_id": "79038ef4",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": true
    },
    {
        "name": "vocals_deep_sea_echo",
        "input_audio": "sessions/deepz_exploration/artifacts/vocals_646700e2_input.wav",
        "reference_output": "sessions/deepz_exploration/artifacts/vocals_646700e2_output_gld.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "sessions/deepz_exploration/artifacts/vocals_646700e2.aupreset",
        "notes": "Imported from goldens/deepz",
        "source_snapshot_id": "646700e2",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": true
    },
    {
        "name": "vocals_depth_of_space",
        "input_audio": "sessions/deepz_exploration/artifacts/vocals_309fb233_input.wav",
        "reference_output": "sessions/deepz_exploration/artifacts/vocals_309fb233_output_gld.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "sessions/deepz_exploration/artifacts/vocals_309fb233.aupreset",
        "notes": "Imported from goldens/deepz",
        "source_snapshot_id": "309fb233",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": true
    },
    {
        "name": "vocals_diamond_chorus",
        "input_audio": "sessions/deepz_exploration/artifacts/vocals_43c42a87_input.wav",
        "reference_output": "sessions/deepz_exploration/artifacts/vocals_43c42a87_output_gld.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "sessions/deepz_exploration/artifacts/vocals_43c42a87.aupreset",
        "notes": "Imported from goldens/deepz",
        "source_snapshot_id": "43c42a87",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": true
    },
    {
        "name": "vocals_evil_scary_harmo",
        "input_audio": "sessions/deepz_exploration/artifacts/vocals_190257c8_input.wav",
        "reference_output": "sessions/deepz_exploration/artifacts/vocals_190257c8_output_gld.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "sessions/deepz_exploration/artifacts/vocals_190257c8.aupreset",
        "notes": "Imported from goldens/deepz",
        "source_snapshot_id": "190257c8",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": true
    },
    {
        "name": "vocals_famous_cathedral",
        "input_audio": "sessions/deepz_exploration/artifacts/vocals_9a26b65d_input.wav",
        "reference_output": "sessions/deepz_exploration/artifacts/vocals_9a26b65d_output_gld.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "sessions/deepz_exploration/artifacts/vocals_9a26b65d.aupreset",
        "notes": "Imported from goldens/deepz",
        "source_snapshot_id": "9a26b65d",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": true
    },
    {
        "name": "vocals_lead_vocal_plate",
        "input_audio": "sessions/deepz_exploration/artifacts/vocals_902da561_input.wav",
        "reference_output": "sessions/deepz_exploration/artifacts/vocals_902da561_output_gld.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "sessions/deepz_exploration/artifacts/vocals_902da561.aupreset",
        "notes": "Imported from goldens/deepz",
        "source_snapshot_id": "902da561",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": true
    },
    {
        "name": "vocals_liquid_phasor",
        "input_audio": "sessions/deepz_exploration/artifacts/vocals_0498891e_input.wav",
        "reference_output": "sessions/deepz_exploration/artifacts/vocals_0498891e_output_gld.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "sessions/deepz_exploration/artifacts/vocals_0498891e.aupreset",
        "notes": "Imported from goldens/deepz",
        "source_snapshot_id": "0498891e",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": true
    },
    {
        "name": "vocals_lost_souls_harmo",
        "input_audio": "sessions/deepz_exploration/artifacts/vocals_a460c592_input.wav",
        "reference_output": "sessions/deepz_exploration/artifacts/vocals_a460c592_output_gld.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "sessions/deepz_exploration/artifacts/vocals_a460c592.aupreset",
        "notes": "Imported from goldens/deepz",
        "source_snapshot_id": "a460c592",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": true
    },
    {
        "name": "vocals_luscious_delays",
        "input_audio": "sessions/deepz_exploration/artifacts/vocals_9e5bf05d_input.wav",
        "reference_output": "sessions/deepz_exploration/artifacts/vocals_9e5bf05d_output_gld.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "sessions/deepz_exploration/artifacts/vocals_9e5bf05d.aupreset",
        "notes": "Imported from goldens/deepz",
        "source_snapshot_id": "9e5bf05d",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": true
    },
    {
        "name": "vocals_luscious_plate",
        "input_audio": "sessions/deepz_exploration/artifacts/vocals_35fecc40_input.wav",
        "reference_output": "sessions/deepz_exploration/artifacts/vocals_35fecc40_output_gld.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "sessions/deepz_exploration/artifacts/vocals_35fecc40.aupreset",
        "notes": "Imported from goldens/deepz",
        "source_snapshot_id": "35fecc40",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": true
    },
    {
        "name": "vocals_lush_multi_chorus",
        "input_audio": "sessions/deepz_exploration/artifacts/vocals_63f4362f_input.wav",
        "reference_output": "sessions/deepz_exploration/artifacts/vocals_63f4362f_output_gld.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "sessions/deepz_exploration/artifacts/vocals_63f4362f.aupreset",
        "notes": "Imported from goldens/deepz",
        "source_snapshot_id": "63f4362f",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": true
    },
    {
        "name": "vocals_majestic_hall",
        "input_audio": "sessions/deepz_exploration/artifacts/vocals_6bb0cf77_input.wav",
        "reference_output": "sessions/deepz_exploration/artifacts/vocals_6bb0cf77_output_gld.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "sessions/deepz_exploration/artifacts/vocals_6bb0cf77.aupreset",
        "notes": "Imported from goldens/deepz",
        "source_snapshot_id": "6bb0cf77",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": true
    },
    {
        "name": "vocals_opening_nite",
        "input_audio": "sessions/deepz_exploration/artifacts/vocals_a663d592_input.wav",
        "reference_output": "sessions/deepz_exploration/artifacts/vocals_a663d592_output_gld.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "sessions/deepz_exploration/artifacts/vocals_a663d592.aupreset",
        "notes": "Imported from goldens/deepz",
        "source_snapshot_id": "a663d592",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": true
    },
    {
        "name": "vocals_rand_o_tap",
        "input_audio": "sessions/deepz_exploration/artifacts/vocals_487240f8_input.wav",
        "reference_output": "sessions/deepz_exploration/artifacts/vocals_487240f8_output_gld.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "sessions/deepz_exploration/artifacts/vocals_487240f8.aupreset",
        "notes": "Imported from goldens/deepz",
        "source_snapshot_id": "487240f8",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": true
    },
    {
        "name": "vocals_rotary_in_space",
        "input_audio": "sessions/deepz_exploration/artifacts/vocals_5f1afd7a_input.wav",
        "reference_output": "sessions/deepz_exploration/artifacts/vocals_5f1afd7a_output_gld.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "sessions/deepz_exploration/artifacts/vocals_5f1afd7a.aupreset",
        "notes": "Imported from goldens/deepz",
        "source_snapshot_id": "5f1afd7a",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": true
    },
    {
        "name": "vocals_shifted_chorus",
        "input_audio": "sessions/deepz_exploration/artifacts/vocals_268c7e06_input.wav",
        "reference_output": "sessions/deepz_exploration/artifacts/vocals_268c7e06_output_gld.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "sessions/deepz_exploration/artifacts/vocals_268c7e06.aupreset",
        "notes": "Imported from goldens/deepz",
        "source_snapshot_id": "268c7e06",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": true
    },
    {
        "name": "vocals_surreal_vocals_2",
        "input_audio": "sessions/deepz_exploration/artifacts/vocals_df49d336_input.wav",
        "reference_output": "sessions/deepz_exploration/artifacts/vocals_df49d336_output_gld.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "sessions/deepz_exploration/artifacts/vocals_df49d336.aupreset",
        "notes": "Imported from goldens/deepz",
        "source_snapshot_id": "df49d336",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": true
    },
    {
        "name": "vocals_tap_time_reverb",
        "input_audio": "sessions/deepz_exploration/artifacts/vocals_c7c05d37_input.wav",
        "reference_output": "sessions/deepz_exploration/artifacts/vocals_c7c05d37_output_gld.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "sessions/deepz_exploration/artifacts/vocals_c7c05d37.aupreset",
        "notes": "Imported from goldens/deepz",
        "source_snapshot_id": "c7c05d37",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": true
    },
    {
        "name": "vocals_time_modulator",
        "input_audio": "sessions/deepz_exploration/artifacts/vocals_e0772791_input.wav",
        "reference_output": "sessions/deepz_exploration/artifacts/vocals_e0772791_output_gld.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "sessions/deepz_exploration/artifacts/vocals_e0772791.aupreset",
        "notes": "Imported from goldens/deepz",
        "source_snapshot_id": "e0772791",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": true
    },
    {
        "name": "vocals_train_station",
        "input_audio": "sessions/deepz_exploration/artifacts/vocals_9b27d576_input.wav",
        "reference_output": "sessions/deepz_exploration/artifacts/vocals_9b27d576_output_gld.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "sessions/deepz_exploration/artifacts/vocals_9b27d576.aupreset",
        "notes": "Imported from goldens/deepz",
        "source_snapshot_id": "9b27d576",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": true
    },
    {
        "name": "vocals_tweaky_taps",
        "input_audio": "sessions/deepz_exploration/artifacts/vocals_524279f3_input.wav",
        "reference_output": "sessions/deepz_exploration/artifacts/vocals_524279f3_output_gld.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "sessions/deepz_exploration/artifacts/vocals_524279f3.aupreset",
        "notes": "Imported from goldens/deepz",
        "source_snapshot_id": "524279f3",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": true
    },
    {
        "name": "vocals_unique_plate",
        "input_audio": "sessions/deepz_exploration/artifacts/vocals_26284a7a_input.wav",
        "reference_output": "sessions/deepz_exploration/artifacts/vocals_26284a7a_output_gld.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "sessions/deepz_exploration/artifacts/vocals_26284a7a.aupreset",
        "notes": "Imported from goldens/deepz",
        "source_snapshot_id": "26284a7a",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": true
    },
    {
        "name": "vocals_vocal_hall_1",
        "input_audio": "sessions/deepz_exploration/artifacts/vocals_920add5c_input.wav",
        "reference_output": "sessions/deepz_exploration/artifacts/vocals_920add5c_output_gld.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "sessions/deepz_exploration/artifacts/vocals_920add5c.aupreset",
        "notes": "Imported from goldens/deepz",
        "source_snapshot_id": "920add5c",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": true
    },
    {
        "name": "vocals_widening_taps",
        "input_audio": "sessions/deepz_exploration/artifacts/vocals_582591d4_input.wav",
        "reference_output": "sessions/deepz_exploration/artifacts/vocals_582591d4_output_gld.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "sessions/deepz_exploration/artifacts/vocals_582591d4.aupreset",
        "notes": "Imported from goldens/deepz",
        "source_snapshot_id": "582591d4",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": true
    },
    {
        "name": "vocals_pretty_harmonizer",
        "input_audio": "sessions/deepz_exploration/artifacts/vocals_56dee3db_input.wav",
        "reference_output": "sessions/deepz_exploration/artifacts/vocals_56dee3db_output_gld.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "sessions/deepz_exploration/artifacts/vocals_56dee3db.aupreset",
        "notes": "Imported from goldens/deepz",
        "source_snapshot_id": "56dee3db",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": true
    },
    {
        "name": "vocals_tap_tempo_bounce",
        "input_audio": "sessions/deepz_exploration/artifacts/vocals_9e434d45_input.wav",
        "reference_output": "sessions/deepz_exploration/artifacts/vocals_9e434d45_output_gld.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "sessions/deepz_exploration/artifacts/vocals_9e434d45.aupreset",
        "notes": "Imported from goldens/deepz",
        "source_snapshot_id": "9e434d45",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": true
    },
    {
        "name": "vocals_init_serial",
        "input_audio": "sessions/deepz_exploration/artifacts/vocals_2c335819_input.wav",
        "reference_output": "sessions/deepz_exploration/artifacts/vocals_2c335819_output_gld.wav",
        "parameters": {},
        "plugin_path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
        "preset_file": "sessions/deepz_exploration/artifacts/vocals_2c335819.aupreset",
        "notes": "Imported from goldens/deepz",
        "source_snapshot_id": "2c335819",
        "thresholds": {
            "snr_db_min": 30.0,
            "correlation_min": 0.95,
            "rms_error_max": 0.05,
            "spectral_distance_max": 0.15
        },
        "expect_match": true
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
    assert_setup_comparison(
        actual,
        reference,
        setup=setup,
        thresholds=thresholds,
        input_audio=input_wav,
        allow_extra_actual_tail=True,
    )
