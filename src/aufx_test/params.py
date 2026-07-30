"""Shared CLI / explore parameter parsing."""

from __future__ import annotations

from typing import Any


def parse_param_value(raw: str) -> float | int | bool | str:
    """Parse a single ``name=value`` right-hand side into bool/int/float/str."""
    lower = raw.lower()
    if lower in {"true", "false"}:
        return lower == "true"
    try:
        if "." in raw:
            return float(raw)
        return int(raw)
    except ValueError:
        return raw


def parse_param_args(values: list[str]) -> dict[str, Any]:
    """Parse ``name=value`` tokens from the CLI into a parameter dict."""
    params: dict[str, Any] = {}
    for raw in values:
        if "=" not in raw:
            raise ValueError(f"Expected name=value, got {raw!r}")
        key, value = raw.split("=", 1)
        params[key.strip()] = parse_param_value(value.strip())
    return params
