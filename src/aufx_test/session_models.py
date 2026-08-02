"""Dataclass models for manual exploration sessions."""

from __future__ import annotations

from dataclasses import asdict, dataclass, field
from typing import Any
from uuid import uuid4

from .comparison import ComparisonThresholds, thresholds_from_dict
from .naming import _utc_now


@dataclass
class StateSnapshot:
    """One captured plugin state from manual exploration."""

    name: str
    parameters: dict[str, float | int | bool | str] = field(default_factory=dict)
    source_clip_name: str | None = None
    input_audio: str | None = None
    output_audio: str | None = None
    # Hardware-insert capture (written by native SessionSnap); optional.
    output_audio_hw: str | None = None
    preset_file: str | None = None
    # Optional MIDI sysex dump staged beside the capture artifacts.
    sysex_file: str | None = None
    notes: str = ""
    tags: list[str] = field(default_factory=list)
    id: str = field(default_factory=lambda: uuid4().hex[:8])
    created_at: str = field(default_factory=_utc_now)
    promoted: bool = False
    test_name: str | None = None
    thresholds: dict[str, float] | None = None
    # True: output should match reference. False: negative case — fail if it matches
    # (reference is a known-broken capture; pass only when the bug is fixed).
    expect_match: bool = True
    # Output artifact role: ``gld`` / ``sus`` / ``bkn`` (from filename flag).
    reference_kind: str | None = None

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> StateSnapshot:
        return cls(**{k: v for k, v in data.items() if k in cls.__dataclass_fields__})


@dataclass
class TestSetup:
    """An automatable test derived from a promoted snapshot."""

    name: str
    input_audio: str
    reference_output: str
    parameters: dict[str, float | int | bool | str] = field(default_factory=dict)
    plugin_path: str | None = None
    preset_file: str | None = None
    notes: str = ""
    source_snapshot_id: str | None = None
    thresholds: ComparisonThresholds = field(default_factory=ComparisonThresholds)
    expect_match: bool = True

    def to_dict(self) -> dict[str, Any]:
        data = asdict(self)
        data["thresholds"] = asdict(self.thresholds)
        return data

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> TestSetup:
        thresholds_data = data.pop("thresholds", {})
        thresholds = thresholds_from_dict(thresholds_data)
        allowed = {k: v for k, v in data.items() if k in cls.__dataclass_fields__ and k != "thresholds"}
        return cls(thresholds=thresholds, **allowed)
