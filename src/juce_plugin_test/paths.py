"""Path helpers for render outputs."""

from __future__ import annotations

from pathlib import Path
from uuid import uuid4


def unique_output_path(
    *,
    directory: str | Path = "tmp",
    prefix: str = "render",
    suffix: str = ".wav",
) -> Path:
    """Return a unique output path under ``directory`` with a short guid suffix.

    Example: ``tmp/render_a1b2c3d4.wav``
    """
    out_dir = Path(directory)
    out_dir.mkdir(parents=True, exist_ok=True)
    token = uuid4().hex[:8]
    return out_dir / f"{prefix}_{token}{suffix}"
