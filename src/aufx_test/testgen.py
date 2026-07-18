"""Generate pytest modules from promoted test setups."""

from __future__ import annotations

import json
from pathlib import Path

from .session import ExperimentSession, TestSetup


def export_test_module(
    session: ExperimentSession,
    output_path: str | Path,
    *,
    host_fixture: str = "plugin_host",
) -> Path:
    """Write a pytest module that replays promoted setups from a session."""
    setups = session.promoted_setups()
    if not setups:
        raise ValueError(f"No promoted snapshots in session {session.name!r}")

    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    setups_json = json.dumps([s.to_dict() for s in setups], indent=4)

    content = f'''\
"""Auto-generated tests from session {session.name!r}.

Regenerate with:
  aufx-test session export {session.name} -o {output_path}
"""

from __future__ import annotations

import json

import pytest

from aufx_test import Waveform
from aufx_test.comparison import ComparisonThresholds
from aufx_test.reporting import assert_setup_comparison

SETUPS = json.loads(
    """
{setups_json}
"""
)


@pytest.mark.parametrize("setup", SETUPS, ids=[s["name"] for s in SETUPS])
def test_session_setup(setup, {host_fixture}):
    """Replay a promoted manual exploration snapshot."""
    host = {host_fixture}
    if setup.get("plugin_path"):
        host.load_plugin(setup["plugin_path"])
    if setup.get("preset_file"):
        host.load_preset(setup["preset_file"])
    if setup.get("parameters"):
        host.set_parameters(setup["parameters"])

    input_wav = Waveform.from_file(setup["input_audio"])
    reference = Waveform.from_file(setup["reference_output"])
    actual = host.process(input_wav)

    thresholds = ComparisonThresholds(**setup.get("thresholds", {{}}))
    assert_setup_comparison(
        actual,
        reference,
        setup=setup,
        thresholds=thresholds,
        input_audio=input_wav,
        allow_extra_actual_tail=True,
    )
'''

    output_path.write_text(content)
    return output_path


def export_setups_json(session: ExperimentSession, output_path: str | Path) -> Path:
    """Export promoted setups as a standalone JSON file."""
    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "session": session.name,
        "plugin_path": session.plugin_path,
        "setups": [s.to_dict() for s in session.promoted_setups()],
    }
    output_path.write_text(json.dumps(payload, indent=2) + "\n")
    return output_path
