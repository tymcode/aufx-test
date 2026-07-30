"""Command-line entry point."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from . import __version__
from .audio import Waveform
from .compare_config import clear_compare_config_cache, load_compare_config
from .comparison import ComparisonThresholds, compare_waveforms
from .explore import capture_snapshot_from_cli, run_explore
from .graphing import plot_comparison, plot_difference_metrics
from .host_app import launch_host_app
from .params import parse_param_args
from .reporting import band_analysis
from .session import ExperimentSession, slugify
from .testgen import export_setups_json, export_test_module


def _cmd_compare(args: argparse.Namespace) -> int:
    clear_compare_config_cache()
    config = load_compare_config(args.compare_config)
    thresholds = config.thresholds
    if args.snr_min is not None or args.corr_min is not None:
        thresholds = ComparisonThresholds(
            snr_db_min=args.snr_min if args.snr_min is not None else thresholds.snr_db_min,
            correlation_min=args.corr_min if args.corr_min is not None else thresholds.correlation_min,
            rms_error_max=thresholds.rms_error_max,
            spectral_distance_max=thresholds.spectral_distance_max,
        )

    actual_path = args.actual
    expected_path = args.expected

    # Convenience mode:
    #   aufx-test compare --root <sessions_root> <session_name> <snapshot_id>
    # Compares the hardware capture against the software/rendered output from
    # that snapshot (output_audio_hw vs output_audio). The positional args are
    # repurposed as session/snapshot names rather than adding a separate
    # subcommand so the muscle memory of "compare A B" carries over.
    if args.root is not None:
        session = _load_session(args.actual, args.root)
        snap = session.get_snapshot(args.expected)
        # Read session.json directly: output_audio_hw is written by the native
        # host (SessionSnap.cpp) and isn't part of the ExperimentSession
        # snapshot dataclass. TODO: add hardware fields to the dataclass and
        # drop this raw-JSON sidestep.
        payload = json.loads(session.session_file.read_text())
        snap_json = next((s for s in payload.get("snapshots", []) if s.get("id") == snap.id), None)

        if snap_json is None:
            raise KeyError(f"Snapshot metadata not found in {session.session_file}: {snap.id}")

        sw_output = snap_json.get("output_audio")
        hw_output = snap_json.get("output_audio_hw")
        if not sw_output:
            raise ValueError(f"Snapshot {snap.id!r} has no output_audio")
        if not hw_output:
            raise ValueError(
                f"Snapshot {snap.id!r} has no output_audio_hw; capture with source='Both' first."
            )

        actual_path = session.resolve_path(hw_output)
        expected_path = session.resolve_path(sw_output)
        if not args.json:
            print(f"Comparing snapshot {snap.id} ({snap.name})")
            print(f"  actual   (hardware): {actual_path}")
            print(f"  expected (software): {expected_path}")

    actual = Waveform.from_file(actual_path)
    expected = Waveform.from_file(expected_path)
    result = compare_waveforms(actual, expected, thresholds=thresholds)
    band_summary = band_analysis(actual, expected, config=config)

    if args.json:
        payload = {
            "passed": result.passed,
            **result.metrics.as_dict(),
            "thresholds": thresholds.__dict__,
            "band_analysis": band_summary,
        }
        print(json.dumps(payload, indent=2))
    else:
        print(result.summary())
        print(f"band analysis ({band_summary['num_of_bands']} bands from compare.config):")
        for row in band_summary["bands"]:
            print(
                f"  {row['band']:12}  "
                f"{row['low_hz']:.0f}-{row['high_hz']:.0f} Hz  "
                f"Δ {row['delta_db']:+.2f} dB"
            )

    if args.plot:
        plot_comparison(expected, actual, save_path=args.plot)
        if not args.json:
            print(f"Plot saved to {args.plot}")
    elif args.metrics_plot:
        plot_difference_metrics(result, save_path=args.metrics_plot, show=False)
        if not args.json:
            print(f"Metrics plot saved to {args.metrics_plot}")

    return 0 if result.passed else 1


def _cmd_session_new(args: argparse.Namespace) -> int:
    session = ExperimentSession.create(
        args.name,
        plugin_path=args.plugin,
        description=args.description or "",
        root_dir=args.root,
    )
    print(f"Created session {session.name!r} at {session.session_file}")
    return 0


def _list_session_folders(root: Path) -> list[str]:
    """Folder names under root that contain a session.json (sorted)."""
    if not root.is_dir():
        return []
    names = [
        path.parent.name
        for path in sorted(root.glob("*/session.json"))
        if path.parent.is_dir()
    ]
    return names


def _load_session(name: str, root: Path) -> ExperimentSession:
    """Resolve a session by folder name, slugified name, or explicit path.

    Deliberately forgiving because sessions are created from two places (this
    CLI and the native host app, which slugifies display names) and users type
    whichever name they remember. Falls back to scanning every session.json
    under root and matching on the stored display name. Note the default root
    is ./sessions relative to the CWD — pass --root when working against the
    host app's sessions dir (~/Library/AU Effects Explorer/sessions).
    """
    candidates = [
        root / name,
        root / slugify(name),
        Path(name),
    ]
    for candidate in candidates:
        session_file = candidate / "session.json" if candidate.is_dir() else candidate
        if session_file.exists():
            return ExperimentSession.load(session_file, root_dir=root)

    for path in root.glob("*/session.json"):
        session = ExperimentSession.load(path, root_dir=root)
        if session.name == name or path.parent.name == name or path.parent.name == slugify(name):
            return session
    raise FileNotFoundError(f"Session not found: {name!r} in {root}")


def _cmd_session_snap(args: argparse.Namespace) -> int:
    session = _load_session(args.name, args.root)
    try:
        parameters = parse_param_args(args.param)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(str(exc)) from exc
    snapshot = capture_snapshot_from_cli(
        session,
        name=args.snapshot_name,
        parameters=parameters,
        params_file=args.params_file,
        input_audio=args.input,
        output_audio=args.output,
        preset_file=args.preset,
        notes=args.notes or "",
        tags=[t.strip() for t in (args.tags or "").split(",") if t.strip()],
    )
    print(f"Captured {snapshot.id} {snapshot.name!r}")
    print(f"  session: {session.session_file}")
    return 0


def _cmd_session_show(args: argparse.Namespace) -> int:
    if not args.name:
        names = _list_session_folders(args.root)
        if not names:
            print(f"No exploration sessions found in {args.root.resolve()}")
            return 0
        for name in names:
            print(name)
        return 0

    session = _load_session(args.name, args.root)
    print(session.summary())
    return 0


def _cmd_session_promote(args: argparse.Namespace) -> int:
    session = _load_session(args.name, args.root)
    if not args.snapshot:
        pending = [
            {"id": snap.id, "name": snap.name}
            for snap in session.snapshots
            if not snap.promoted
        ]
        print(json.dumps(pending))
        return 0

    base = load_compare_config().thresholds
    thresholds = ComparisonThresholds(
        snr_db_min=args.snr_min if args.snr_min is not None else base.snr_db_min,
        correlation_min=args.corr_min if args.corr_min is not None else base.correlation_min,
        rms_error_max=args.rms_max if args.rms_max is not None else base.rms_error_max,
        spectral_distance_max=args.spectral_max if args.spectral_max is not None else base.spectral_distance_max,
    )
    expect_match: bool | None = None
    if args.negative:
        expect_match = False
    elif args.positive:
        expect_match = True

    setup = session.promote_snapshot(
        args.snapshot,
        test_name=args.test_name,
        thresholds=thresholds,
        expect_match=expect_match,
    )
    session.save()
    kind = "negative" if not setup.expect_match else "match"
    print(f"Promoted {args.snapshot!r} → test {setup.name!r} ({kind})")
    return 0


def _cmd_session_export(args: argparse.Namespace) -> int:
    session = _load_session(args.name, args.root)
    if args.format == "json":
        path = export_setups_json(session, args.output)
    else:
        path = export_test_module(session, args.output, host_fixture=args.host_fixture)
    print(f"Exported to {path}")
    return 0


def _cmd_session_export_presets(args: argparse.Namespace) -> int:
    session = _load_session(args.name, args.root)
    path = session.export_developer_presets(args.output)
    print(f"Exported developer preset bundle to {path}")
    return 0


def _cmd_session_import_goldens(args: argparse.Namespace) -> int:
    session = _load_session(args.name, args.root)
    directory = Path(args.directory)
    if not directory.is_absolute():
        directory = Path.cwd() / directory

    thresholds = None
    if args.promote:
        base = load_compare_config().thresholds
        thresholds = ComparisonThresholds(
            snr_db_min=base.snr_db_min,
            correlation_min=base.correlation_min,
            rms_error_max=base.rms_error_max,
            spectral_distance_max=base.spectral_distance_max,
        )

    imported, warnings = session.import_goldens(
        directory,
        promote=args.promote,
        thresholds=thresholds,
    )
    for warning in warnings:
        print(f"warning: {warning}")
    for snap in imported:
        role = snap.reference_kind or "unknown"
        status = "promoted" if snap.promoted else "imported"
        print(f"{status} {snap.id} {snap.name!r} ({role})")
    print(f"Imported {len(imported)} golden(s) into session {session.name!r}")
    # Idempotent re-imports (everything already present) are success.
    if imported or any("Skipped existing" in w for w in warnings):
        return 0
    return 1


def _cmd_explore(args: argparse.Namespace) -> int:
    if args.name:
        session = _load_session(args.name, args.root)
    else:
        name = args.new_name or input("Session name: ").strip()
        session = ExperimentSession.create(
            name,
            plugin_path=args.plugin,
            description=args.description or "",
            root_dir=args.root,
        )
    run_explore(session, params_file=args.params_file)
    return 0


def _cmd_host(args: argparse.Namespace) -> int:
    process = launch_host_app(
        config=args.config,
        host_app_bin=args.host_app,
        project_root_dir=args.project_root,
    )
    if args.detach:
        print(f"Launched plugin host (pid {process.pid})")
        return 0
    return process.wait()


def main(argv: list[str] | None = None) -> int:
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
            "are interpreted as <session_name> <snapshot_id>, and the command "
            "compares output_audio_hw (actual) vs output_audio (expected)."
        ),
    )
    compare.add_argument(
        "--snr-min",
        type=float,
        default=None,
        help="Override snr_db_min from compare.config.json",
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
    compare.add_argument("--json", action="store_true", help="Print metrics as JSON")
    compare.set_defaults(func=_cmd_compare)

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
        "--snr-min",
        type=float,
        default=None,
        help="Override snr_db_min from compare.config.json",
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

    args = parser.parse_args(argv)
    if not args.command:
        parser.print_help()
        return 0

    if args.command == "session" and not getattr(args, "session_command", None):
        session.print_help()
        return 0

    if args.command == "session":
        args.root = getattr(args, "root", Path("sessions"))

    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
