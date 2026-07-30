"""Discover external golden/broken/suspect triplets on disk."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from .output_roles import OUTPUT_ROLES, _base_stem_from_output, parse_output_role


@dataclass(frozen=True)
class GoldenTriplet:
    """One external golden/broken/suspect case discovered on disk."""

    name: str
    stem: str
    role: str
    preset: Path
    input_audio: Path
    output_audio: Path


def _try_complete_triplet(
    root: Path,
    output: Path,
    role: str,
    *,
    seen_stems: set[str],
    complete: list[GoldenTriplet],
    warnings: list[str],
) -> None:
    """Validate and append one output WAV as a golden triplet, or record a warning."""
    stem = _base_stem_from_output(output)
    if stem in seen_stems:
        warnings.append(f"Duplicate stem {stem!r}; keeping first match ({output.name})")
        return

    preset = root / f"{stem}.aupreset"
    input_audio = root / f"{stem}_input.wav"
    missing: list[str] = []
    if not preset.is_file():
        missing.append(preset.name)
    if not input_audio.is_file():
        missing.append(input_audio.name)
    if missing:
        warnings.append(f"Incomplete set for {stem!r}: missing {', '.join(missing)}")
        return

    seen_stems.add(stem)
    complete.append(
        GoldenTriplet(
            name=stem,
            stem=stem,
            role=role,
            preset=preset,
            input_audio=input_audio,
            output_audio=output,
        )
    )


def discover_golden_triplets(directory: str | Path) -> tuple[list[GoldenTriplet], list[str]]:
    """Find ``{stem}.aupreset`` + ``{stem}_input.wav`` + output WAV sets.

    Accepts flat layouts and per-stem subfolders::

        {stem}.aupreset / {stem}_input.wav / {stem}_output_{role}.wav
        {stem}/{stem}.aupreset / …   (session artifacts layout)

    Bare ``{stem}_output.wav`` is treated as golden / ``gld``.

    Returns ``(complete, warnings)``. Incomplete sets are reported in ``warnings``.
    """
    root = Path(directory)
    if not root.is_dir():
        raise FileNotFoundError(f"Golden directory not found: {root}")

    complete: list[GoldenTriplet] = []
    warnings: list[str] = []
    seen_stems: set[str] = set()

    def _scan_dir(search_root: Path) -> None:
        for role in OUTPUT_ROLES:
            for output in sorted(search_root.glob(f"*_output_{role}.wav")):
                _try_complete_triplet(
                    search_root,
                    output,
                    role,
                    seen_stems=seen_stems,
                    complete=complete,
                    warnings=warnings,
                )

        for output in sorted(search_root.glob("*_output.wav")):
            if parse_output_role(output) is not None:
                continue
            _try_complete_triplet(
                search_root,
                output,
                "gld",
                seen_stems=seen_stems,
                complete=complete,
                warnings=warnings,
            )

    _scan_dir(root)
    # Also accept session-style ``artifacts/<stem>/`` trees when importing from
    # an existing session artifacts directory.
    for child in sorted(p for p in root.iterdir() if p.is_dir()):
        _scan_dir(child)

    complete.sort(key=lambda t: t.name.lower())
    return complete, warnings
