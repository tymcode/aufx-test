"""Filename/slug helpers for session artifacts."""

from __future__ import annotations

import re
from datetime import datetime, timezone
from pathlib import Path


def _utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def slugify(value: str) -> str:
    """Lowercase alphanumeric slug; non-alnum runs become ``_``."""
    slug = re.sub(r"[^a-z0-9]+", "_", value.strip().lower())
    return slug.strip("_") or "snapshot"


def _keyword_from_description(description: str) -> str:
    """Return the first slug token from a description for artifact filenames."""
    slug = re.sub(r"[^a-z0-9]+", "_", description.strip().lower()).strip("_")
    if not slug:
        return ""
    return slug.split("_", 1)[0]


def artifact_stem(description: str, snapshot_id: str) -> str:
    """Filename stem for session artifacts: ``{keyword}_{id}`` or ``{id}``."""
    keyword = _keyword_from_description(description)
    return f"{keyword}_{snapshot_id}" if keyword else snapshot_id


def _artifact_subdir(artifacts_dir: Path, stem: str) -> Path:
    """Per-capture folder: ``artifacts/<stem>/``."""
    return artifacts_dir / stem


def relative_artifact_path(stem: str, file_name: str) -> str:
    """Session-relative path: ``artifacts/<stem>/<file_name>``."""
    return f"artifacts/{stem}/{file_name}"
