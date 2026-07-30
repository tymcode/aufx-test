"""Interactive manual exploration workflow."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from .aupreset import AUpresetError, validate_aupreset
from .params import parse_param_value
from .session import ExperimentSession, StateSnapshot, slugify


def _prompt(text: str, default: str = "") -> str:
    suffix = f" [{default}]" if default else ""
    value = input(f"{text}{suffix}: ").strip()
    return value or default


def _prompt_path(text: str, *, required: bool = False) -> Path | None:
    while True:
        raw = _prompt(text)
        if not raw:
            if required:
                print("  (required)")
                continue
            return None
        path = Path(raw).expanduser()
        if path.exists():
            return path
        print(f"  File not found: {path}")


def _prompt_params() -> dict[str, Any]:
    print("Enter parameters one per line as name=value. Blank line to finish.")
    params: dict[str, Any] = {}
    while True:
        raw = input("  param> ").strip()
        if not raw:
            break
        if "=" not in raw:
            print("  Use name=value format")
            continue
        key, value = raw.split("=", 1)
        key = key.strip()
        value = value.strip()
        params[key] = parse_param_value(value)
    return params


def _load_params_file(path: Path) -> dict[str, Any]:
    data = json.loads(path.read_text())
    if not isinstance(data, dict):
        raise ValueError("Parameter file must contain a JSON object")
    return data


def run_explore(
    session: ExperimentSession,
    *,
    params_file: Path | None = None,
) -> None:
    """Interactive REPL for capturing manual plugin states."""
    print(session.summary())
    print()
    print("Manual exploration mode")
    print("  1. Tweak your plugin in the DAW or standalone host")
    print("  2. Bounce/render output to a WAV file")
    print("  3. Record the state here (parameters, input, output, notes)")
    print("Commands: snap, list, promote, save, quit")
    print()

    default_params: dict[str, Any] = _load_params_file(params_file) if params_file else {}

    while True:
        try:
            cmd = input("explore> ").strip().lower()
        except (EOFError, KeyboardInterrupt):
            print()
            session.save()
            print(f"Saved {session.session_file}")
            break

        if cmd in {"quit", "q", "exit"}:
            session.save()
            print(f"Saved {session.session_file}")
            break
        if cmd in {"list", "ls"}:
            print(session.summary())
            continue
        if cmd in {"save", "s"}:
            path = session.save()
            print(f"Saved {path}")
            continue
        if cmd.startswith("promote"):
            parts = cmd.split(maxsplit=2)
            if len(parts) < 2:
                snap_id = _prompt("Snapshot id or name")
            else:
                snap_id = parts[1]
            test_name = parts[2] if len(parts) > 2 else _prompt("Test name", default=slugify(snap_id))
            try:
                setup = session.promote_snapshot(snap_id, test_name=test_name)
                session.save()
                print(f"Promoted → test {setup.name!r}")
            except (KeyError, ValueError) as exc:
                print(f"  Error: {exc}")
            continue
        if cmd in {"snap", "capture", "c", ""}:
            _capture_interactive(session, default_params=default_params)
            session.save()
            print(f"Saved snapshot to {session.session_file}")
            continue
        print("Unknown command. Use: snap, list, promote, save, quit")


def _capture_interactive(
    session: ExperimentSession,
    *,
    default_params: dict[str, Any],
) -> StateSnapshot:
    name = _prompt("Snapshot name", default=f"snapshot_{len(session.snapshots) + 1}")
    print("Parameters (Enter keeps previous defaults, 'file' to load JSON, or enter values)")
    use_defaults = _prompt("Use default params?", default="y" if default_params else "n").lower()
    if use_defaults in {"y", "yes"} and default_params:
        params = dict(default_params)
        print(f"  Using {len(params)} default parameters")
    elif use_defaults == "file":
        params_path = _prompt_path("Parameter JSON file", required=True)
        assert params_path is not None
        params = _load_params_file(params_path)
    else:
        params = _prompt_params()

    input_path = _prompt_path("Input WAV used for this render")
    output_path = _prompt_path("Bounced output WAV from your DAW/host", required=True)
    print("Save plugin state as .aupreset from your DAW (Logic: Save As AU Preset).")
    preset_path = _prompt_path(".aupreset file (recommended)", required=True)
    if preset_path is not None:
        try:
            info = validate_aupreset(preset_path)
            print(f"  Valid .aupreset ({info.format}, {info.state_bytes} bytes)")
        except AUpresetError as exc:
            print(f"  Warning: {exc}")
    notes = _prompt("Notes")
    tags_raw = _prompt("Tags (comma-separated)")
    tags = [t.strip() for t in tags_raw.split(",") if t.strip()]

    snapshot = StateSnapshot(
        name=name,
        parameters=params,
        source_clip_name=input_path.stem if input_path is not None else None,
        notes=notes,
        tags=tags,
    )
    session.add_snapshot(
        snapshot,
        copy_input=input_path,
        copy_output=output_path,
        copy_preset=preset_path,
    )
    print(f"Captured {snapshot.id} {snapshot.name!r}")
    return snapshot


def capture_snapshot_from_cli(
    session: ExperimentSession,
    *,
    name: str,
    parameters: dict[str, Any] | None = None,
    params_file: Path | None = None,
    input_audio: Path | None = None,
    output_audio: Path | None = None,
    preset_file: Path | None = None,
    notes: str = "",
    tags: list[str] | None = None,
) -> StateSnapshot:
    """Non-interactive snapshot capture for scripting."""
    params: dict[str, Any] = {}
    if params_file is not None:
        params.update(_load_params_file(params_file))
    if parameters:
        params.update(parameters)

    if preset_file is not None and preset_file.suffix.lower() == ".aupreset":
        validate_aupreset(preset_file)

    snapshot = StateSnapshot(
        name=name,
        parameters=params,
        source_clip_name=input_audio.stem if input_audio is not None else None,
        notes=notes,
        tags=tags or [],
    )
    session.add_snapshot(
        snapshot,
        copy_input=input_audio,
        copy_output=output_audio,
        copy_preset=preset_file,
    )
    session.save()
    return snapshot
