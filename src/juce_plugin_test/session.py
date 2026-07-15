"""Manual experiment sessions and automatable test setups."""

from __future__ import annotations

import json
import re
import shutil
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any
from uuid import uuid4

from .comparison import ComparisonThresholds
from .aupreset import import_aupreset, validate_aupreset


def _utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def _slug(value: str) -> str:
    slug = re.sub(r"[^a-z0-9]+", "_", value.strip().lower())
    return slug.strip("_") or "snapshot"


def _keyword_from_description(description: str) -> str:
    """Return the first slug token from a description for artifact filenames."""
    slug = re.sub(r"[^a-z0-9]+", "_", description.strip().lower()).strip("_")
    if not slug:
        return ""
    return slug.split("_", 1)[0]


def _artifact_stem(description: str, snapshot_id: str) -> str:
    """Filename stem for session artifacts: ``{keyword}_{id}`` or ``{id}``."""
    keyword = _keyword_from_description(description)
    return f"{keyword}_{snapshot_id}" if keyword else snapshot_id


@dataclass
class StateSnapshot:
    """One captured plugin state from manual exploration."""

    name: str
    parameters: dict[str, float | int | bool | str] = field(default_factory=dict)
    input_audio: str | None = None
    output_audio: str | None = None
    preset_file: str | None = None
    notes: str = ""
    tags: list[str] = field(default_factory=list)
    id: str = field(default_factory=lambda: uuid4().hex[:8])
    created_at: str = field(default_factory=_utc_now)
    promoted: bool = False
    test_name: str | None = None
    thresholds: dict[str, float] | None = None

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

    def to_dict(self) -> dict[str, Any]:
        data = asdict(self)
        data["thresholds"] = asdict(self.thresholds)
        return data

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> TestSetup:
        thresholds_data = data.pop("thresholds", {})
        thresholds = ComparisonThresholds(**thresholds_data) if thresholds_data else ComparisonThresholds()
        allowed = {k: v for k, v in data.items() if k in cls.__dataclass_fields__ and k != "thresholds"}
        return cls(thresholds=thresholds, **allowed)


@dataclass
class ExperimentSession:
    """A collection of manual captures that can be promoted into automated tests."""

    name: str
    plugin_path: str | None = None
    description: str = ""
    snapshots: list[StateSnapshot] = field(default_factory=list)
    created_at: str = field(default_factory=_utc_now)
    updated_at: str = field(default_factory=_utc_now)
    root_dir: Path = field(default_factory=lambda: Path("sessions"))

    @property
    def session_dir(self) -> Path:
        return self.root_dir / _slug(self.name)

    @property
    def artifacts_dir(self) -> Path:
        return self.session_dir / "artifacts"

    @property
    def session_file(self) -> Path:
        return self.session_dir / "session.json"

    def _stage_artifact(
        self,
        source: Path,
        dest: Path,
        *,
        aupreset: bool = False,
    ) -> Path:
        """Place ``source`` at ``dest`` under artifacts.

        Files already inside this session's artifacts dir are reused (renamed
        when needed) so host capture + ``session snap`` do not create duplicates.
        External files are still copied in.
        """
        source_path = Path(source).expanduser().resolve()
        dest_path = Path(dest)
        if aupreset and dest_path.suffix.lower() != ".aupreset":
            dest_path = dest_path.with_suffix(".aupreset")
        dest_path.parent.mkdir(parents=True, exist_ok=True)

        artifacts_root = self.artifacts_dir.resolve()
        already_staged = source_path == artifacts_root or artifacts_root in source_path.parents

        if already_staged:
            if aupreset:
                validate_aupreset(source_path)
            if source_path != dest_path.resolve():
                if dest_path.exists():
                    dest_path.unlink()
                source_path.replace(dest_path)
            return dest_path

        if aupreset:
            return import_aupreset(source_path, dest_path)

        shutil.copy2(source_path, dest_path)
        return dest_path

    def add_snapshot(
        self,
        snapshot: StateSnapshot,
        *,
        copy_input: Path | None = None,
        copy_output: Path | None = None,
        copy_preset: Path | None = None,
    ) -> StateSnapshot:
        """Add a snapshot, optionally staging audio/preset files into the session."""
        self.artifacts_dir.mkdir(parents=True, exist_ok=True)
        stem = _artifact_stem(snapshot.name, snapshot.id)

        if copy_input is not None:
            dest = self.artifacts_dir / f"{stem}_input{Path(copy_input).suffix or '.wav'}"
            staged = self._stage_artifact(Path(copy_input), dest)
            snapshot.input_audio = str(staged.relative_to(self.session_dir))

        if copy_output is not None:
            dest = self.artifacts_dir / f"{stem}_output{Path(copy_output).suffix or '.wav'}"
            staged = self._stage_artifact(Path(copy_output), dest)
            snapshot.output_audio = str(staged.relative_to(self.session_dir))

        if copy_preset is not None:
            preset = Path(copy_preset)
            if preset.suffix.lower() == ".aupreset":
                dest = self.artifacts_dir / f"{stem}.aupreset"
                staged = self._stage_artifact(preset, dest, aupreset=True)
            else:
                dest = self.artifacts_dir / f"{stem}_preset{preset.suffix}"
                staged = self._stage_artifact(preset, dest)
            snapshot.preset_file = str(staged.relative_to(self.session_dir))

        self.snapshots.append(snapshot)
        self.updated_at = _utc_now()
        return snapshot

    def get_snapshot(self, snapshot_id: str) -> StateSnapshot:
        for snap in self.snapshots:
            if snap.id == snapshot_id or _slug(snap.name) == _slug(snapshot_id):
                return snap
        raise KeyError(f"Snapshot not found: {snapshot_id!r}")

    def promote_snapshot(
        self,
        snapshot_id: str,
        *,
        test_name: str | None = None,
        thresholds: ComparisonThresholds | None = None,
    ) -> TestSetup:
        """Mark a snapshot as ready for automation and return its test setup."""
        snap = self.get_snapshot(snapshot_id)
        if not snap.output_audio:
            raise ValueError(f"Snapshot {snap.name!r} has no output_audio — bounce audio before promoting.")
        if not snap.input_audio:
            raise ValueError(f"Snapshot {snap.name!r} has no input_audio — record the input used.")
        if not snap.preset_file:
            raise ValueError(
                f"Snapshot {snap.name!r} has no .aupreset — save AU state before promoting."
            )

        snap.promoted = True
        snap.test_name = test_name or _slug(snap.name)
        if thresholds is not None:
            snap.thresholds = asdict(thresholds)
        self.updated_at = _utc_now()
        return self.snapshot_to_test_setup(snap)

    def snapshot_to_test_setup(self, snap: StateSnapshot) -> TestSetup:
        thresholds = ComparisonThresholds(**snap.thresholds) if snap.thresholds else ComparisonThresholds()
        input_path = str(self.resolve_path(snap.input_audio))
        output_path = str(self.resolve_path(snap.output_audio))
        preset_path = str(self.resolve_path(snap.preset_file)) if snap.preset_file else None
        return TestSetup(
            name=snap.test_name or _slug(snap.name),
            plugin_path=self.plugin_path,
            input_audio=input_path,
            reference_output=output_path,
            parameters=dict(snap.parameters),
            preset_file=preset_path,
            notes=snap.notes,
            source_snapshot_id=snap.id,
            thresholds=thresholds,
        )

    def promoted_setups(self) -> list[TestSetup]:
        return [self.snapshot_to_test_setup(s) for s in self.snapshots if s.promoted]

    def resolve_path(self, path: str | None) -> Path:
        if path is None:
            raise ValueError("Path is required")
        p = Path(path)
        if p.is_absolute():
            return p
        return (self.session_dir / p).resolve()

    def save(self) -> Path:
        self.session_dir.mkdir(parents=True, exist_ok=True)
        payload = {
            "name": self.name,
            "plugin_path": self.plugin_path,
            "description": self.description,
            "created_at": self.created_at,
            "updated_at": self.updated_at,
            "snapshots": [s.to_dict() for s in self.snapshots],
        }
        self.session_file.write_text(json.dumps(payload, indent=2) + "\n")
        return self.session_file

    @classmethod
    def load(cls, path: str | Path, *, root_dir: Path | None = None) -> ExperimentSession:
        session_file = Path(path)
        if session_file.is_dir():
            session_file = session_file / "session.json"
        data = json.loads(session_file.read_text())
        root = root_dir or session_file.parent.parent
        session = cls(
            name=data["name"],
            plugin_path=data.get("plugin_path"),
            description=data.get("description", ""),
            created_at=data.get("created_at", _utc_now()),
            updated_at=data.get("updated_at", _utc_now()),
            root_dir=root,
        )
        session.snapshots = [StateSnapshot.from_dict(s) for s in data.get("snapshots", [])]
        return session

    @classmethod
    def create(
        cls,
        name: str,
        *,
        plugin_path: str | None = None,
        description: str = "",
        root_dir: Path | str = "sessions",
    ) -> ExperimentSession:
        session = cls(name=name, plugin_path=plugin_path, description=description, root_dir=Path(root_dir))
        session.save()
        return session

    def summary(self) -> str:
        lines = [
            f"Session: {self.name}",
            f"  plugin: {self.plugin_path or '(not set)'}",
            f"  snapshots: {len(self.snapshots)} ({sum(s.promoted for s in self.snapshots)} promoted)",
            f"  file: {self.session_file}",
        ]
        for snap in self.snapshots:
            flag = " [promoted]" if snap.promoted else ""
            lines.append(f"  - {snap.id} {snap.name!r}{flag}")
            if snap.parameters:
                params = ", ".join(f"{k}={v}" for k, v in snap.parameters.items())
                lines.append(f"      params: {params}")
            if snap.input_audio:
                lines.append(f"      input: {snap.input_audio}")
            if snap.output_audio:
                lines.append(f"      output: {snap.output_audio}")
            if snap.preset_file:
                lines.append(f"      preset: {snap.preset_file}")
            if snap.notes:
                lines.append(f"      notes: {snap.notes}")
        return "\n".join(lines)

    def export_developer_presets(self, output_dir: str | Path) -> Path:
        """Export .aupreset files and a manifest for sharing with the plugin developer."""
        out = Path(output_dir)
        presets_dir = out / "presets"
        presets_dir.mkdir(parents=True, exist_ok=True)
        manifest_snapshots: list[dict[str, Any]] = []

        for snap in self.snapshots:
            entry: dict[str, Any] = {
                "id": snap.id,
                "name": snap.name,
                "notes": snap.notes,
                "tags": snap.tags,
                "promoted": snap.promoted,
                "input_audio": snap.input_audio,
                "output_audio": snap.output_audio,
            }
            if snap.preset_file:
                source = self.resolve_path(snap.preset_file)
                info = validate_aupreset(source)
                dest = presets_dir / f"{snap.id}_{_slug(snap.name)}.aupreset"
                import_aupreset(source, dest)
                entry["preset_file"] = str(dest.relative_to(out))
                entry["preset_info"] = info.as_dict()
            manifest_snapshots.append(entry)

        manifest = {
            "session": self.name,
            "plugin_path": self.plugin_path,
            "description": self.description,
            "snapshots": manifest_snapshots,
        }
        manifest_path = out / "manifest.json"
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
        return manifest_path
