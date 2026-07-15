"""Utilities for working with Audio Unit .aupreset preset files."""

from __future__ import annotations

import plistlib
import shutil
from dataclasses import dataclass
from pathlib import Path
from typing import Any


class AUpresetError(ValueError):
    """Raised when an .aupreset file is missing or malformed."""


@dataclass(frozen=True)
class AUpresetInfo:
    """Summary metadata extracted from an .aupreset file."""

    path: Path
    name: str | None
    manufacturer: int | str | None
    subtype: int | str | None
    plugin_type: int | str | None
    state_bytes: int
    format: str

    def as_dict(self) -> dict[str, Any]:
        return {
            "path": str(self.path),
            "name": self.name,
            "manufacturer": self.manufacturer,
            "subtype": self.subtype,
            "plugin_type": self.plugin_type,
            "state_bytes": self.state_bytes,
            "format": self.format,
        }


def _read_plist(path: Path) -> dict[str, Any]:
    if not path.exists():
        raise AUpresetError(f"Preset file not found: {path}")
    if path.suffix.lower() != ".aupreset":
        raise AUpresetError(f"Expected .aupreset extension, got {path.suffix!r}")

    try:
        with path.open("rb") as handle:
            payload = plistlib.load(handle)
    except plistlib.InvalidFileException as exc:
        raise AUpresetError(f"Invalid plist in {path}: {exc}") from exc
    except Exception as exc:
        raise AUpresetError(f"Could not read {path}: {exc}") from exc

    if not isinstance(payload, dict):
        raise AUpresetError(f"Expected plist dictionary in {path}, got {type(payload).__name__}")
    return payload


def _state_size(payload: dict[str, Any]) -> int:
    for key in ("jucePluginState", "data", "state"):
        value = payload.get(key)
        if isinstance(value, (bytes, bytearray)):
            size = len(value)
            if size > 0:
                return size
        if isinstance(value, str):
            size = len(value.encode("utf-8"))
            if size > 0:
                return size
    return 0


def _extract_state_bytes(payload: dict[str, Any]) -> bytes | None:
    for key in ("jucePluginState", "data", "state"):
        value = payload.get(key)
        if isinstance(value, (bytes, bytearray)) and len(value) > 0:
            return bytes(value)
        if isinstance(value, str) and value:
            return value.encode("utf-8")
    return None


def _detect_format(payload: dict[str, Any]) -> str:
    for key, fmt in (("jucePluginState", "juce-au"), ("data", "au-classic"), ("state", "generic-state")):
        value = payload.get(key)
        if isinstance(value, (bytes, bytearray)) and len(value) > 0:
            return fmt
        if isinstance(value, str) and value:
            return fmt
    return "unknown"


def validate_aupreset(path: str | Path) -> AUpresetInfo:
    """Validate an .aupreset and return extracted metadata."""
    preset_path = Path(path).expanduser().resolve()
    payload = _read_plist(preset_path)
    state_bytes = _state_size(payload)
    if state_bytes == 0:
        raise AUpresetError(
            f"No plugin state blob found in {preset_path}. "
            "Expected a 'data', 'jucePluginState', or 'state' key."
        )
    return AUpresetInfo(
        path=preset_path,
        name=payload.get("name") if isinstance(payload.get("name"), str) else None,
        manufacturer=payload.get("manufacturer"),
        subtype=payload.get("subtype"),
        plugin_type=payload.get("type"),
        state_bytes=state_bytes,
        format=_detect_format(payload),
    )


def import_aupreset(source: str | Path, dest: str | Path) -> Path:
    """Validate and copy an .aupreset to a destination path."""
    source_path = Path(source).expanduser()
    dest_path = Path(dest)
    if dest_path.suffix.lower() != ".aupreset":
        dest_path = dest_path.with_suffix(".aupreset")
    validate_aupreset(source_path)
    dest_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source_path, dest_path)
    return dest_path


def export_aupreset_for_sharing(
    source: str | Path,
    dest_dir: str | Path,
    *,
    name: str | None = None,
) -> Path:
    """Copy an .aupreset into a share folder with a readable filename."""
    info = validate_aupreset(source)
    dest_directory = Path(dest_dir)
    dest_directory.mkdir(parents=True, exist_ok=True)
    label = name or info.name or Path(source).stem
    safe = "".join(ch if ch.isalnum() or ch in "-_" else "_" for ch in label).strip("_") or "preset"
    dest_path = dest_directory / f"{safe}.aupreset"
    shutil.copy2(Path(source).expanduser(), dest_path)
    return dest_path
