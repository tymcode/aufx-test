"""Unit tests for compares/ batch discovery."""

from __future__ import annotations

from pathlib import Path

import pytest

from aufx_test.audio import Waveform
from aufx_test.compare_batch import (
    discover_compare_cases,
    find_preset_for_number,
    parse_golden_name,
    render_with_settle,
)


def test_parse_golden_name_prefers_longest_dry_stem() -> None:
    sources = ["Guitars", "Guitar-Solo", "Vocals", "Drums-Band"]
    assert parse_golden_name("Guitar-Solo-38-Lunar-13", sources) == (
        "Guitar-Solo",
        "38",
        "Lunar-13",
    )
    assert parse_golden_name("Guitars-01-Majesty-Hall", sources) == (
        "Guitars",
        "01",
        "Majesty-Hall",
    )
    assert parse_golden_name("Drums-Band-04-Intimacy-Hall", sources) == (
        "Drums-Band",
        "04",
        "Intimacy-Hall",
    )


def test_find_preset_for_number(tmp_path: Path) -> None:
    presets = tmp_path / "presets"
    presets.mkdir()
    (presets / "38-Lunar 13.aupreset").write_bytes(b"x")
    (presets / "05-Premiere Nite.aupreset").write_bytes(b"x")
    assert find_preset_for_number(presets, "38").name == "38-Lunar 13.aupreset"
    assert find_preset_for_number(presets, "05").name == "05-Premiere Nite.aupreset"
    assert find_preset_for_number(presets, "99") is None


def test_discover_compare_cases_skips_missing_preset(tmp_path: Path) -> None:
    root = tmp_path / "deepz"
    dry = root / "dry"
    presets = root / "presets" / "au"
    goldens = root / "goldens"
    dry.mkdir(parents=True)
    presets.mkdir(parents=True)
    goldens.mkdir(parents=True)

    (dry / "Vocals.wav").write_bytes(b"RIFF")
    (presets / "00-Depthness of Space.aupreset").write_bytes(b"preset")
    (goldens / "Vocals-00-Depthness-Of-Space.wav").write_bytes(b"RIFF")
    (goldens / "Vocals-04-Intimacy-Hall.wav").write_bytes(b"RIFF")

    cases, warnings = discover_compare_cases(root)
    assert len(cases) == 1
    assert cases[0].source == "Vocals"
    assert cases[0].preset.name == "00-Depthness of Space.aupreset"
    assert any("no preset 04" in w for w in warnings)


@pytest.mark.parametrize(
    "stem,expected",
    [
        ("Vocals-00-Depthness-Of-Space", ("Vocals", "00", "Depthness-Of-Space")),
        ("Strings-36-Random-o-Tap", ("Strings", "36", "Random-o-Tap")),
    ],
)
def test_parse_golden_name_deepz_examples(stem: str, expected: tuple[str, str, str]) -> None:
    sources = ["Vocals", "Strings", "Drums-Band", "Guitars", "Guitar-Solo"]
    assert parse_golden_name(stem, sources) == expected


def test_render_with_settle_trims_lead_in(tmp_path: Path) -> None:
    class FakeHost:
        def __init__(self) -> None:
            self.last_input: Waveform | None = None

        def process(self, input_waveform: Waveform, *, output_path=None) -> Waveform:
            self.last_input = input_waveform
            # Echo input as output (settle trim should remove the lead-in).
            return input_waveform

    host = FakeHost()
    dry = Waveform.sine(
        440.0,
        duration_seconds=0.5,
        sample_rate=48000,
        channels=2,
        phase_rad=0.5 * 3.141592653589793,
    )
    out = render_with_settle(host, dry, settle_seconds=1.5)  # type: ignore[arg-type]
    assert host.last_input is not None
    assert host.last_input.num_samples == dry.num_samples + int(1.5 * 48000)
    assert out.num_samples == dry.num_samples
    import numpy as np

    assert np.allclose(out.data, dry.data)
