"""Generate pytest modules from promoted test setups."""

from __future__ import annotations

import json
from pathlib import Path

from .session import ExperimentSession


def export_test_module(
    session: ExperimentSession,
    output_path: str | Path,
    *,
    host_fixture: str = "plugin_host",
) -> Path:
    """Write a pytest module that replays promoted setups from a session."""
    setups = session.promoted_setups(portable=True)
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
from pathlib import Path

import pytest

from aufx_test import Waveform
from aufx_test.comparison import thresholds_from_dict
from aufx_test.reporting import assert_setup_comparison

ROOT = Path(__file__).resolve().parents[2]

SETUPS = json.loads(
    """
{setups_json}
"""
)


def _resolve(path: str | None) -> Path | None:
    if not path:
        return None
    p = Path(path).expanduser()
    if not p.is_absolute():
        p = ROOT / p
    return p


@pytest.mark.parametrize("setup", SETUPS, ids=[s["name"] for s in SETUPS])
def test_session_setup(setup, {host_fixture}):
    """Replay a promoted manual exploration snapshot."""
    host = {host_fixture}
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

    thresholds = thresholds_from_dict(setup.get("thresholds", {{}}))
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
        "setups": [s.to_dict() for s in session.promoted_setups(portable=True)],
    }
    output_path.write_text(json.dumps(payload, indent=2) + "\n")
    return output_path
