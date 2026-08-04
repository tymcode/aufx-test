"""Multi-patch HW/SW visual gallery for Quadraverse comparison reports."""

from __future__ import annotations

import argparse
from pathlib import Path

from ..audio import Waveform
from ..graphing import plot_comparison, plot_spectrogram_pair
from ..output_roles import _base_stem_from_output
from ..reporting import write_compare_gallery_html
from ..session import ExperimentSession, StateSnapshot
from .session_loader import _load_session


def _load_wave(session: ExperimentSession, relative: str | None) -> Waveform | None:
    if not relative:
        return None
    path = session.resolve_path(relative)
    if not path.exists():
        return None
    return Waveform.from_file(path)


def _stem_of(snap: StateSnapshot) -> str:
    for rel in (snap.output_audio, snap.output_audio_hw, snap.input_audio):
        if rel:
            return _base_stem_from_output(rel)
    return snap.id or "patch"


def _cmd_compare_gallery(args: argparse.Namespace) -> int:
    session = _load_session(args.session, args.root)
    snaps = list(session.snapshots)
    if args.stems:
        wanted = {s.strip() for s in args.stems.split(",") if s.strip()}
        snaps = [s for s in snaps if s.id in wanted or _stem_of(s) in wanted or s.name in wanted]
    if args.limit is not None:
        snaps = snaps[: max(0, int(args.limit))]
    if not snaps:
        print("No snapshots found for gallery report")
        return 1

    out_dir = Path(args.out).expanduser().resolve() if args.out else session.session_dir
    out_dir.mkdir(parents=True, exist_ok=True)
    assets = out_dir / "gallery_assets"
    assets.mkdir(parents=True, exist_ok=True)

    patches: list[dict] = []
    for snap in snaps:
        dry = _load_wave(session, snap.input_audio)
        sw = _load_wave(session, snap.output_audio)
        hw = _load_wave(session, snap.output_audio_hw)
        if sw is None and hw is None:
            continue

        stem = _stem_of(snap)
        targets: list[dict] = []

        def add_target(key: str, label: str, wet: Waveform | None) -> None:
            if wet is None:
                return
            prefix = f"{stem}_{key}"
            wave_path = assets / f"{prefix}_waveform.png"
            spec_path = assets / f"{prefix}_spectrogram.png"

            if dry is not None:
                plot_comparison(
                    dry,
                    wet,
                    labels=["Dry", label],
                    spectrogram_background=wet,
                    title=f"{snap.name} — {label}",
                    save_path=wave_path,
                )
                plot_spectrogram_pair(
                    dry,
                    wet,
                    labels=("Dry", label),
                    title=f"{snap.name} — spectrograms",
                    save_path=spec_path,
                )
            else:
                plot_comparison(
                    wet,
                    wet,
                    labels=[label, label],
                    spectrogram_background=wet,
                    title=f"{snap.name} — {label}",
                    save_path=wave_path,
                )
                plot_spectrogram_pair(
                    wet,
                    wet,
                    labels=(label, label),
                    title=f"{snap.name} — spectrogram",
                    save_path=spec_path,
                )

            targets.append(
                {
                    "key": key,
                    "label": label,
                    "waveform_plot": wave_path,
                    "spectrogram_plot": spec_path,
                }
            )

        add_target("software", "Software", sw)
        add_target("hardware", "Hardware", hw)

        if not targets:
            continue

        patches.append(
            {
                "key": stem,
                "label": snap.name or stem,
                "targets": targets,
            }
        )

    if not patches:
        print("No usable audio found for gallery report")
        return 1

    html_path = out_dir / "compare_gallery.html"
    write_compare_gallery_html(
        html_path,
        session_name=session.name,
        patches=patches,
    )
    print(f"Wrote {html_path}")
    return 0


def add_compare_gallery_parser(sub: argparse._SubParsersAction) -> None:
    p = sub.add_parser(
        "compare-gallery",
        help="Write a single HTML gallery for multi-patch HW/SW visual comparison",
    )
    p.add_argument("session", help="Session name (e.g. quadraverse_compare)")
    p.add_argument(
        "--root",
        type=Path,
        default=Path("sessions"),
        help="Sessions root directory",
    )
    p.add_argument(
        "--out",
        type=Path,
        default=None,
        help="Output directory for compare_gallery.html (default: session folder)",
    )
    p.add_argument(
        "--stems",
        default=None,
        help="Optional comma-separated snapshot ids/stems/names to include",
    )
    p.add_argument(
        "--limit",
        type=int,
        default=4,
        help="Maximum number of patches to include (default: 4)",
    )
    p.set_defaults(func=_cmd_compare_gallery)
