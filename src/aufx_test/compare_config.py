"""Load compare.config.json settings for waveform and band comparison."""

from __future__ import annotations

import json
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path
from typing import Any

from .comparison import ComparisonThresholds
from .paths import project_root


@dataclass(frozen=True)
class CompareConfig:
    """Resolved compare settings from compare.config.json."""

    num_of_bands: int = 7
    band_low_hz: float = 20.0
    band_high_hz: float = 20000.0
    window_samples: int = 4096
    thresholds: ComparisonThresholds = ComparisonThresholds()

    def hop_samples(self) -> int:
        return max(1, self.window_samples // 2)


_DEFAULTS = CompareConfig()


def default_config_path(root: Path | None = None) -> Path:
    return (root or project_root()) / "compare.config.json"


def _parse_thresholds(raw: Any) -> ComparisonThresholds:
    base = _DEFAULTS.thresholds
    if not isinstance(raw, dict):
        return base
    return ComparisonThresholds(
        correlation_min=float(raw.get("correlation_min", base.correlation_min)),
        rms_error_max=float(raw.get("rms_error_max", base.rms_error_max)),
        spectral_distance_max=float(raw.get("spectral_distance_max", base.spectral_distance_max)),
    )


def _parse_compare_config(data: dict[str, Any]) -> CompareConfig:
    num_raw = data.get("num_of_bands", _DEFAULTS.num_of_bands)
    try:
        num_of_bands = int(num_raw)
    except (TypeError, ValueError) as exc:
        raise ValueError(f"compare.config.json num_of_bands must be an integer, got {num_raw!r}") from exc
    if num_of_bands < 1:
        raise ValueError(f"compare.config.json num_of_bands must be >= 1, got {num_of_bands}")

    window_samples = int(data.get("window_samples", _DEFAULTS.window_samples))
    if window_samples < 16:
        raise ValueError(f"compare.config.json window_samples must be >= 16, got {window_samples}")

    band_low_hz = float(data.get("band_low_hz", _DEFAULTS.band_low_hz))
    band_high_hz = float(data.get("band_high_hz", _DEFAULTS.band_high_hz))
    if band_low_hz <= 0 or band_high_hz <= band_low_hz:
        raise ValueError(
            f"compare.config.json band range invalid: [{band_low_hz}, {band_high_hz})"
        )

    return CompareConfig(
        num_of_bands=num_of_bands,
        band_low_hz=band_low_hz,
        band_high_hz=band_high_hz,
        window_samples=window_samples,
        thresholds=_parse_thresholds(data.get("thresholds")),
    )


def load_compare_config(path: str | Path | None = None) -> CompareConfig:
    """Load compare settings. Missing file yields defaults."""
    config_path = Path(path) if path is not None else default_config_path()
    if not config_path.is_file():
        return _DEFAULTS

    data = json.loads(config_path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError(f"Compare config root must be a JSON object: {config_path}")
    return _parse_compare_config(data)


def num_of_bands(path: str | Path | None = None) -> int:
    """Return how many frequency bands to use when comparing."""
    return load_compare_config(path).num_of_bands


@lru_cache(maxsize=4)
def cached_compare_config(path: str | None = None) -> CompareConfig:
    """Cached lookup of compare.config.json (``None`` → project-root default)."""
    return load_compare_config(path)


def clear_compare_config_cache() -> None:
    cached_compare_config.cache_clear()
