"""Manual experiment sessions and automatable test setups.

Owns ``ExperimentSession``; re-exports naming, output-role, golden-import, and
model helpers previously defined in this module for backward-compatible imports.
"""

from __future__ import annotations

import json
import shutil
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any

from .aupreset import import_aupreset, validate_aupreset
from .comparison import ComparisonThresholds
from .golden_import import GoldenTriplet, discover_golden_triplets
from .naming import (
    _artifact_subdir,
    _keyword_from_description,
    _utc_now,
    artifact_stem,
    relative_artifact_path,
    slugify,
)
from .output_roles import (
    _OUTPUT_ROLE_LABELS,
    OUTPUT_ROLES,
    _base_stem_from_output,
    expect_match_for_output_role,
    hardware_output_artifact_filename,
    output_artifact_filename,
    parse_output_role,
)
from .session_models import StateSnapshot, TestSetup

__all__ = [
    "ExperimentSession",
    "GoldenTriplet",
    "OUTPUT_ROLES",
    "StateSnapshot",
    "TestSetup",
    "_artifact_subdir",
    "_base_stem_from_output",
    "_keyword_from_description",
    "_utc_now",
    "artifact_stem",
    "discover_golden_triplets",
    "expect_match_for_output_role",
    "hardware_output_artifact_filename",
    "output_artifact_filename",
    "parse_output_role",
    "relative_artifact_path",
    "slugify",
]


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
        return self.root_dir / slugify(self.name)

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
        """Add a snapshot, optionally staging audio/preset files into the session.

        Artifacts are stored under ``artifacts/<stem>/`` so each capture keeps
        its files together as the set grows (input, software/hardware outputs,
        preset, sysex, future reports).
        """
        self.artifacts_dir.mkdir(parents=True, exist_ok=True)
        stem = artifact_stem(snapshot.name, snapshot.id)
        stem_dir = _artifact_subdir(self.artifacts_dir, stem)
        stem_dir.mkdir(parents=True, exist_ok=True)

        if copy_input is not None:
            if not snapshot.source_clip_name:
                snapshot.source_clip_name = Path(copy_input).stem
            dest = stem_dir / f"{stem}_input{Path(copy_input).suffix or '.wav'}"
            staged = self._stage_artifact(Path(copy_input), dest)
            snapshot.input_audio = str(staged.relative_to(self.session_dir))

        if copy_output is not None:
            output_src = Path(copy_output)
            role = snapshot.reference_kind or parse_output_role(output_src)
            if role in OUTPUT_ROLES:
                snapshot.reference_kind = role
            dest = stem_dir / output_artifact_filename(
                stem, role, ext=output_src.suffix or ".wav"
            )
            staged = self._stage_artifact(output_src, dest)
            snapshot.output_audio = str(staged.relative_to(self.session_dir))

        if copy_preset is not None:
            preset = Path(copy_preset)
            if preset.suffix.lower() == ".aupreset":
                dest = stem_dir / f"{stem}.aupreset"
                staged = self._stage_artifact(preset, dest, aupreset=True)
            else:
                dest = stem_dir / f"{stem}_preset{preset.suffix}"
                staged = self._stage_artifact(preset, dest)
            snapshot.preset_file = str(staged.relative_to(self.session_dir))

        self.snapshots.append(snapshot)
        self.updated_at = _utc_now()
        return snapshot

    def get_snapshot(self, snapshot_id: str) -> StateSnapshot:
        key = snapshot_id.strip()
        for snap in self.snapshots:
            if snap.id == key or slugify(snap.name) == slugify(key):
                return snap
            # Accept artifact stems like ``long_67dc49d2`` (keyword + id).
            if key.endswith("_" + snap.id) or key == artifact_stem(snap.name, snap.id):
                return snap
            if snap.preset_file and Path(snap.preset_file).stem == key:
                return snap
            if snap.output_audio and _base_stem_from_output(snap.output_audio) == key:
                return snap
            if snap.output_audio_hw and _base_stem_from_output(snap.output_audio_hw) == key:
                return snap
        raise KeyError(f"Snapshot not found: {snapshot_id!r}")

    def promote_snapshot(
        self,
        snapshot_id: str,
        *,
        test_name: str | None = None,
        thresholds: ComparisonThresholds | None = None,
        expect_match: bool | None = None,
    ) -> TestSetup:
        """Mark a snapshot as ready for automation and return its test setup.

        ``expect_match=False`` marks a negative case: the reference is a known-broken
        capture, and the test passes only when the plugin output no longer matches it.
        """
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
        if test_name is not None:
            snap.test_name = test_name
        elif not snap.test_name:
            snap.test_name = slugify(snap.name)
        if thresholds is not None:
            snap.thresholds = asdict(thresholds)

        role = snap.reference_kind or parse_output_role(snap.output_audio)
        if role in OUTPUT_ROLES:
            snap.reference_kind = role
        if expect_match is not None:
            snap.expect_match = expect_match
        else:
            inferred = expect_match_for_output_role(role)
            if inferred is not None:
                snap.expect_match = inferred

        self.updated_at = _utc_now()
        return self.snapshot_to_test_setup(snap)

    def snapshot_to_test_setup(self, snap: StateSnapshot) -> TestSetup:
        thresholds = ComparisonThresholds(**snap.thresholds) if snap.thresholds else ComparisonThresholds()
        input_path = str(self.resolve_path(snap.input_audio))
        output_path = str(self.resolve_path(snap.output_audio))
        preset_path = str(self.resolve_path(snap.preset_file)) if snap.preset_file else None
        return TestSetup(
            name=snap.test_name or slugify(snap.name),
            plugin_path=self.plugin_path,
            input_audio=input_path,
            reference_output=output_path,
            parameters=dict(snap.parameters),
            preset_file=preset_path,
            notes=snap.notes,
            source_snapshot_id=snap.id,
            thresholds=thresholds,
            expect_match=snap.expect_match,
        )

    def promoted_setups(self) -> list[TestSetup]:
        return [self.snapshot_to_test_setup(s) for s in self.snapshots if s.promoted]

    def import_goldens(
        self,
        directory: str | Path,
        *,
        promote: bool = True,
        thresholds: ComparisonThresholds | None = None,
    ) -> tuple[list[StateSnapshot], list[str]]:
        """Import external golden triplets into this session.

        Expects files named (flat or under a ``{stem}/`` subfolder)::

            {stem}.aupreset
            {stem}_input.wav
            {stem}_output_gld.wav   # or _output_bkn / _output_sus / bare _output.wav

        Bare ``{stem}_output.wav`` is treated as a golden (``gld``) reference.
        Imported files are staged into ``artifacts/<stem>/`` in this session.

        Already-present snapshot names are skipped. When ``promote`` is true,
        new snapshots are marked ready for automation.
        """
        triplets, warnings = discover_golden_triplets(directory)
        if not triplets and not warnings:
            warnings.append(
                f"No *_output.wav or *_output_{{gld,sus,bkn}}.wav files found in {directory}"
            )

        existing_names = {slugify(s.name) for s in self.snapshots}
        imported: list[StateSnapshot] = []

        for triplet in triplets:
            if slugify(triplet.name) in existing_names:
                warnings.append(f"Skipped existing snapshot {triplet.name!r}")
                continue

            validate_aupreset(triplet.preset)
            snap = StateSnapshot(
                name=triplet.name,
                reference_kind=triplet.role,
                notes=f"Imported from {Path(directory).resolve()}",
                tags=["imported", _OUTPUT_ROLE_LABELS.get(triplet.role, triplet.role)],
            )
            self.add_snapshot(
                snap,
                copy_input=triplet.input_audio,
                copy_output=triplet.output_audio,
                copy_preset=triplet.preset,
            )
            if promote:
                self.promote_snapshot(snap.id, thresholds=thresholds)
            existing_names.add(slugify(triplet.name))
            imported.append(snap)

        if imported:
            self.save()
        return imported, warnings

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
            flags: list[str] = []
            if snap.promoted:
                flags.append("promoted")
            if snap.promoted and not snap.expect_match:
                flags.append("negative")
            role = snap.reference_kind or parse_output_role(snap.output_audio)
            if role in _OUTPUT_ROLE_LABELS:
                flags.append(_OUTPUT_ROLE_LABELS[role])
            flag = f" [{', '.join(flags)}]" if flags else ""
            lines.append(f"  - {snap.id} {snap.name!r}{flag}")
            if snap.parameters:
                params = ", ".join(f"{k}={v}" for k, v in snap.parameters.items())
                lines.append(f"      params: {params}")
            if snap.input_audio:
                lines.append(f"      input: {snap.input_audio}")
            if snap.source_clip_name:
                lines.append(f"      source clip: {snap.source_clip_name}")
            if snap.output_audio:
                lines.append(f"      output: {snap.output_audio}")
            if snap.output_audio_hw:
                lines.append(f"      output_hw: {snap.output_audio_hw}")
            if snap.preset_file:
                lines.append(f"      preset: {snap.preset_file}")
            if snap.sysex_file:
                lines.append(f"      sysex: {snap.sysex_file}")
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
                "output_audio_hw": snap.output_audio_hw,
                "sysex_file": snap.sysex_file,
            }
            if snap.preset_file:
                source = self.resolve_path(snap.preset_file)
                info = validate_aupreset(source)
                dest = presets_dir / f"{snap.id}_{slugify(snap.name)}.aupreset"
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
