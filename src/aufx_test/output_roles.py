"""Output WAV role flags (golden / suspect / broken)."""

from __future__ import annotations

from pathlib import Path

# Output WAV role flags: ``{stem}_output_{role}.wav`` / ``{stem}_output_hw_{role}.wav``
OUTPUT_ROLES = ("gld", "sus", "bkn")
_OUTPUT_ROLE_LABELS = {"gld": "golden", "sus": "suspect", "bkn": "broken"}
# For now, suspect and broken are negative cases (fail if output still matches).
_NEGATIVE_OUTPUT_ROLES = frozenset({"sus", "bkn"})


def parse_output_role(path: str | Path | None) -> str | None:
    """Return ``gld`` / ``sus`` / ``bkn`` from software or hardware output filenames.

    Recognizes ``*_output_<role>.wav`` and ``*_output_hw_<role>.wav``
    (mirrors ``SessionArtifactSchema::parseOutputRole``).
    """
    if path is None:
        return None
    stem = Path(path).stem
    for role in OUTPUT_ROLES:
        if stem.endswith(f"_output_hw_{role}") or stem.endswith(f"_output_{role}"):
            return role
    return None


def output_artifact_filename(stem: str, role: str | None = None, *, ext: str = ".wav") -> str:
    """Build a software output basename: ``{stem}_output.wav`` or ``{stem}_output_{role}.wav``."""
    if role in OUTPUT_ROLES:
        return f"{stem}_output_{role}{ext}"
    return f"{stem}_output{ext}"


def hardware_output_artifact_filename(stem: str, role: str | None = None, *, ext: str = ".wav") -> str:
    """Build a hardware output basename: ``{stem}_output_hw.wav`` or ``{stem}_output_hw_{role}.wav``."""
    if role in OUTPUT_ROLES:
        return f"{stem}_output_hw_{role}{ext}"
    return f"{stem}_output_hw{ext}"


def expect_match_for_output_role(role: str | None) -> bool | None:
    """Map an output role flag to ``expect_match``, or ``None`` if unknown/absent."""
    if role is None:
        return None
    if role == "gld":
        return True
    if role in _NEGATIVE_OUTPUT_ROLES:
        return False
    return None


def _base_stem_from_output(path: str | Path) -> str:
    """Strip ``_output`` / ``_output_{role}`` / ``_output_hw_{role}`` from an artifact stem."""
    stem = Path(path).stem
    for role in OUTPUT_ROLES:
        hw_suffix = f"_output_hw_{role}"
        if stem.endswith(hw_suffix):
            return stem[: -len(hw_suffix)]
        suffix = f"_output_{role}"
        if stem.endswith(suffix):
            return stem[: -len(suffix)]
    if stem.endswith("_output_hw"):
        return stem[: -len("_output_hw")]
    if stem.endswith("_output"):
        return stem[: -len("_output")]
    return stem
