"""Argument parser wiring for the aufx-test CLI."""

from __future__ import annotations

import argparse
from pathlib import Path

from .. import __version__
from .calibrate_plot import add_calibrate_plot_parser
from .compare import _cmd_compare
from .compare_batch import add_compare_batch_parser
from .compare_gallery import add_compare_gallery_parser
from .explore_host import _cmd_explore, _cmd_host
from .session_cmds import (
    _cmd_session_export,
    _cmd_session_export_presets,
    _cmd_session_import_goldens,
    _cmd_session_new,
    _cmd_session_promote,
    _cmd_session_show,
    _cmd_session_snap,
)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Audio plugin test framework — compare, capture, and automate",
    )
    parser.add_argument("--version", action="version", version=f"%(prog)s {__version__}")
    sub = parser.add_subparsers(dest="command")

    compare = sub.add_parser("compare", help="Compare two WAV files")
    compare.add_argument("actual", help="Actual output WAV, or session name when --root is set")
    compare.add_argument("expected", help="Expected WAV, or snapshot id/name when --root is set")
    compare.add_argument(
        "--root",
        type=Path,
        default=None,
        help=(
            "Sessions root for snapshot compare mode. When set, positional args "
            "are interpreted as <session_name> <snapshot_id>. With both wet "
            "captures, compares output_audio_hw vs output_audio (pass/fail). "
            "With only software or only hardware plus input_audio, writes an "
            "informational dry-vs-wet report (no pass/fail)."
        ),
    )
    compare.add_argument(
        "--corr-min",
        type=float,
        default=None,
        help="Override correlation_min from compare.config.json",
    )
    compare.add_argument(
        "--compare-config",
        type=Path,
        help="Path to compare.config.json (default: <project-root>/compare.config.json)",
    )
    compare.add_argument("--plot", metavar="PATH", help="Save waveform comparison plot")
    compare.add_argument("--metrics-plot", metavar="PATH", help="Save metrics bar chart")
    compare.add_argument(
        "--write-report",
        nargs="?",
        const="auto",
        default=None,
        metavar="[DIR|auto]",
        help=(
            "Write compare.json + compare_waveform.png + compare_metrics.png. "
            "Use --write-report (or auto) to target the snapshot stem folder in --root mode "
            "(otherwise: folder of the actual WAV), or pass DIR explicitly."
        ),
    )
    compare.add_argument(
        "--spectrogram-diff",
        action="store_true",
        help="Also write side-by-side spectrograms and a difference spectrogram into the report folder",
    )
    compare.add_argument("--json", action="store_true", help="Print metrics as JSON")
    compare.set_defaults(func=_cmd_compare)

    add_compare_batch_parser(sub)
    add_compare_gallery_parser(sub)
    add_calibrate_plot_parser(sub)

    explore = sub.add_parser("explore", help="Interactive manual exploration session")
    explore.add_argument("name", nargs="?", help="Existing session name or path")
    explore.add_argument("--new-name", help="Create a new session with this name")
    explore.add_argument("--plugin", help="Path to plugin binary (.vst3 / .component)")
    explore.add_argument("--description", help="Session description")
    explore.add_argument("--params-file", type=Path, help="Default parameter JSON for snapshots")
    explore.add_argument("--root", type=Path, default=Path("sessions"), help="Sessions root directory")
    explore.set_defaults(func=_cmd_explore)

    host = sub.add_parser("host", help="Launch native plugin host GUI for manual exploration")
    host.add_argument(
        "--config",
        type=Path,
        help="Path to host.config.json (default: <project-root>/host.config.json)",
    )
    host.add_argument(
        "--project-root",
        type=Path,
        help="Project root used to resolve relative paths in the config",
    )
    host.add_argument("--host-app", type=Path, help="Path to plugin_host_app binary or .app")
    host.add_argument(
        "--detach",
        action="store_true",
        help="Launch in background and return immediately (default: wait until the app exits)",
    )
    host.set_defaults(func=_cmd_host)

    session = sub.add_parser("session", help="Manage exploration sessions")
    session.add_argument("--root", type=Path, default=Path("sessions"), help="Sessions root directory")
    session_sub = session.add_subparsers(dest="session_command")

    session_new = session_sub.add_parser("new", help="Create a new session")
    session_new.add_argument("name")
    session_new.add_argument("--plugin", help="Path to plugin binary")
    session_new.add_argument("--description", default="")
    session_new.set_defaults(func=_cmd_session_new)

    session_snap = session_sub.add_parser("snap", help="Capture a snapshot non-interactively")
    session_snap.add_argument("name", help="Session name")
    session_snap.add_argument("snapshot_name", help="Label for this capture")
    session_snap.add_argument("--input", type=Path, help="Input WAV path")
    session_snap.add_argument("--output", type=Path, required=True, help="Bounced output WAV")
    session_snap.add_argument("--preset", "--aupreset", type=Path, dest="preset", help=".aupreset state file")
    session_snap.add_argument("--params-file", type=Path, help="Parameter JSON file")
    session_snap.add_argument("--param", action="append", default=[], help="Parameter as name=value")
    session_snap.add_argument("--notes", default="")
    session_snap.add_argument("--tags", help="Comma-separated tags")
    session_snap.set_defaults(func=_cmd_session_snap)

    session_show = session_sub.add_parser(
        "show",
        help="Show session summary, or list exploration folders when name is omitted",
    )
    session_show.add_argument(
        "name",
        nargs="?",
        default=None,
        help="Session name or folder (omit to list valid exploration folders)",
    )
    session_show.set_defaults(func=_cmd_session_show)

    session_promote = session_sub.add_parser("promote", help="Promote snapshot to automatable test")
    session_promote.add_argument("name", help="Session name")
    session_promote.add_argument(
        "snapshot",
        nargs="?",
        default=None,
        help="Snapshot id or name (omit to list un-promoted snapshots as JSON)",
    )
    session_promote.add_argument("--test-name", help="Name for generated test")
    expect_group = session_promote.add_mutually_exclusive_group()
    expect_group.add_argument(
        "--negative",
        action="store_true",
        help="Mark as negative case: fail if output still matches the broken reference",
    )
    expect_group.add_argument(
        "--positive",
        action="store_true",
        help="Mark as normal match case (clear a previous --negative)",
    )
    session_promote.add_argument(
        "--corr-min",
        type=float,
        default=None,
        help="Override correlation_min from compare.config.json",
    )
    session_promote.add_argument(
        "--rms-max",
        type=float,
        default=None,
        help="Override rms_error_max from compare.config.json",
    )
    session_promote.add_argument(
        "--spectral-max",
        type=float,
        default=None,
        help="Override spectral_distance_max from compare.config.json",
    )
    session_promote.set_defaults(func=_cmd_session_promote)

    session_export = session_sub.add_parser("export", help="Export promoted setups")
    session_export.add_argument("name", help="Session name")
    session_export.add_argument("-o", "--output", type=Path, required=True)
    session_export.add_argument("--format", choices=("pytest", "json"), default="pytest")
    session_export.add_argument("--host-fixture", default="plugin_host")
    session_export.set_defaults(func=_cmd_session_export)

    session_export_presets = session_sub.add_parser(
        "export-presets",
        help="Export .aupreset files and manifest for the plugin developer",
    )
    session_export_presets.add_argument("name", help="Session name")
    session_export_presets.add_argument("-o", "--output", type=Path, required=True)
    session_export_presets.set_defaults(func=_cmd_session_export_presets)

    session_import_goldens = session_sub.add_parser(
        "import-goldens",
        help="Import external golden triplets ({stem}.aupreset, _input.wav, _output_gld.wav)",
    )
    session_import_goldens.add_argument("name", help="Session name")
    session_import_goldens.add_argument(
        "directory",
        type=Path,
        help="Folder containing golden triplets",
    )
    session_import_goldens.add_argument(
        "--no-promote",
        dest="promote",
        action="store_false",
        help="Import snapshots without promoting them",
    )
    session_import_goldens.set_defaults(func=_cmd_session_import_goldens, promote=True)

    return parser
