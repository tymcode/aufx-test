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
from .reporting import band_analysis, write_compare_html_report
from .session import ExperimentSession, StateSnapshot, slugify
from .testgen import export_setups_json, export_test_module


def _snapshot_source_wave(
    session: ExperimentSession,
    snap: StateSnapshot,
) -> tuple[Waveform | None, str | None]:
    """Load dry input audio and a display name for the source clip, if present."""
    source_wave: Waveform | None = None
    source_clip: str | None = snap.source_clip_name
    if snap.input_audio:
        source_path = session.resolve_path(snap.input_audio)
        if source_path.exists():
            source_wave = Waveform.from_file(source_path)
            if not source_clip:
                source_name = source_path.stem
                if source_name.endswith("_input"):
                    source_name = source_name[: -len("_input")]
                source_clip = source_name
    return source_wave, source_clip


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

    write_report = getattr(args, "write_report", None)
    actual_path = args.actual
    expected_path = args.expected
    auto_report_dir: Path | None = None
    context: dict[str, str] = {}
    source_wave: Waveform | None = None
    # "hardware_vs_software" gates pass/fail. Dry/wet modes are informational only.
    compare_mode = "hardware_vs_software"
    sw_wave: Waveform | None = None
    hw_wave: Waveform | None = None

    # Convenience mode:
    #   aufx-test compare --root <sessions_root> <session_name> <snapshot_id>
    # With both outputs: hardware vs software (pass/fail).
    # With only software or only hardware + input: dry vs wet report (no gate).
    if args.root is not None:
        session = _load_session(args.actual, args.root)
        snap = session.get_snapshot(args.expected)
        has_sw = bool(snap.output_audio)
        has_hw = bool(snap.output_audio_hw)
        source_wave, source_clip = _snapshot_source_wave(session, snap)
        context = {
            "session_name": session.name,
            "snapshot_name": snap.name,
            "snapshot_id": snap.id,
        }
        if source_clip:
            context["source_clip"] = source_clip

        if has_sw and has_hw:
            compare_mode = "hardware_vs_software"
            actual_path = session.resolve_path(snap.output_audio_hw)
            expected_path = session.resolve_path(snap.output_audio)
            if write_report == "auto":
                auto_report_dir = Path(expected_path).parent
            if not args.json:
                print(f"Comparing snapshot {snap.id} ({snap.name})")
                print(f"  actual   (hardware): {actual_path}")
                print(f"  expected (software): {expected_path}")
        elif has_sw and source_wave is not None:
            compare_mode = "dry_vs_software"
            wet_path = session.resolve_path(snap.output_audio)
            actual_path = wet_path
            expected_path = session.resolve_path(snap.input_audio) if snap.input_audio else wet_path
            if write_report == "auto":
                auto_report_dir = Path(wet_path).parent
            if not args.json:
                print(f"Dry vs software report for snapshot {snap.id} ({snap.name})")
                print(f"  dry: {expected_path}")
                print(f"  wet: {wet_path}")
        elif has_hw and source_wave is not None:
            compare_mode = "dry_vs_hardware"
            wet_path = session.resolve_path(snap.output_audio_hw)
            actual_path = wet_path
            expected_path = session.resolve_path(snap.input_audio) if snap.input_audio else wet_path
            if write_report == "auto":
                auto_report_dir = Path(wet_path).parent
            if not args.json:
                print(f"Dry vs hardware report for snapshot {snap.id} ({snap.name})")
                print(f"  dry: {expected_path}")
                print(f"  wet: {wet_path}")
        elif has_sw or has_hw:
            missing = "input_audio (dry source clip)"
            raise ValueError(
                f"Snapshot {snap.id!r} has only one wet capture and no {missing}; "
                "capture with a source clip to report dry vs wet, or capture Both for HW vs SW."
            )
        else:
            raise ValueError(
                f"Snapshot {snap.id!r} has neither output_audio nor output_audio_hw"
            )

    if args.root is None:
        compare_mode = "file_pair"
        gated = True
    else:
        gated = compare_mode == "hardware_vs_software"

    actual = Waveform.from_file(actual_path)
    expected = Waveform.from_file(expected_path)
    if compare_mode == "hardware_vs_software":
        hw_wave = actual
        sw_wave = expected
    elif compare_mode == "dry_vs_software":
        sw_wave = actual
        source_wave = expected if source_wave is None else source_wave
    elif compare_mode == "dry_vs_hardware":
        hw_wave = actual
        source_wave = expected if source_wave is None else source_wave

    if gated:
        result = compare_waveforms(actual, expected, thresholds=thresholds)
    else:
        # Informational dry/wet: score wet vs dry without quality gates.
        wet = sw_wave if compare_mode == "dry_vs_software" else hw_wave
        assert wet is not None and source_wave is not None
        result = compare_waveforms(wet, source_wave)
    band_summary = band_analysis(
        actual if gated else (sw_wave or hw_wave or actual),
        expected if gated else source_wave or expected,
        config=config,
    )

    payload: dict[str, object] = {
        "mode": compare_mode,
        "gated": gated,
        **result.metrics.as_dict(),
        "band_analysis": band_summary,
    }
    if gated:
        payload["passed"] = result.passed
        payload["thresholds"] = thresholds.__dict__
    else:
        payload["passed"] = None
    if context:
        payload["context"] = context
    if args.json:
        print(json.dumps(payload, indent=2))
    else:
        if gated:
            print(result.summary())
        else:
            print(f"Informational {compare_mode.replace('_', ' ')} metrics (not gated):")
            m = result.metrics
            print(
                f"  correlation={m.correlation:.4f}  snr_db={m.snr_db:.2f}  "
                f"rms_error={m.rms_error:.6f}  spectral_distance={m.spectral_distance:.6f}"
            )
        print(f"band analysis ({band_summary['num_of_bands']} bands from compare.config):")
        for row in band_summary["bands"]:
            print(
                f"  {row['band']:12}  "
                f"{row['low_hz']:.0f}-{row['high_hz']:.0f} Hz  "
                f"Δ {row['delta_db']:+.2f} dB"
            )

    if write_report is not None:
        if write_report == "auto":
            report_dir = auto_report_dir or Path(actual_path).expanduser().resolve().parent
        else:
            report_dir = Path(write_report).expanduser().resolve()
        report_dir.mkdir(parents=True, exist_ok=True)

        report_json = report_dir / "compare.json"
        report_waveform = report_dir / "compare_waveform.png"
        report_metrics = report_dir / "compare_metrics.png"
        report_html = report_dir / "compare_report.html"
        report_dry_sw_waveform = report_dir / "compare_dry_vs_software_waveform.png"
        report_dry_sw_metrics = report_dir / "compare_dry_vs_software_metrics.png"
        report_dry_hw_waveform = report_dir / "compare_dry_vs_hardware_waveform.png"
        report_dry_hw_metrics = report_dir / "compare_dry_vs_hardware_metrics.png"

        report_json.write_text(json.dumps(payload, indent=2) + "\n")
        report_wave_size = (20, 6)
        report_metrics_size = (14, 4)
        report_dpi = 200
        comparison_views: list[dict[str, object]] = []
        plot_thresholds = thresholds if gated else None

        if compare_mode in ("hardware_vs_software", "file_pair"):
            labels = ["SW", "HW"] if compare_mode == "hardware_vs_software" else ["expected", "actual"]
            title = (
                "Hardware vs Software"
                if compare_mode == "hardware_vs_software"
                else "Actual vs Expected"
            )
            plot_comparison(
                expected,
                actual,
                labels=labels,
                title=title,
                spectrogram_background=actual if compare_mode == "hardware_vs_software" else None,
                figsize=report_wave_size,
                dpi=report_dpi,
                save_path=report_waveform,
            )
            plot_difference_metrics(
                result,
                thresholds=plot_thresholds,
                figsize=report_metrics_size,
                dpi=report_dpi,
                save_path=report_metrics,
                show=False,
            )
            comparison_views.append(
                {
                    "key": compare_mode,
                    "label": (
                        "Hardware vs Software (default)"
                        if compare_mode == "hardware_vs_software"
                        else "Actual vs Expected"
                    ),
                    "waveform_plot": report_waveform,
                    "metrics_plot": report_metrics,
                    "metrics": result.metrics.as_dict(),
                }
            )
            if source_wave is not None and compare_mode == "hardware_vs_software":
                sw_result = compare_waveforms(expected, source_wave, thresholds=thresholds)
                plot_comparison(
                    source_wave,
                    expected,
                    labels=["Dry", "Wet"],
                    title="Dry vs Software Wet",
                    save_path=report_dry_sw_waveform,
                    spectrogram_background=expected,
                    figsize=report_wave_size,
                    dpi=report_dpi,
                )
                plot_difference_metrics(
                    sw_result,
                    thresholds=thresholds,
                    figsize=report_metrics_size,
                    dpi=report_dpi,
                    save_path=report_dry_sw_metrics,
                    show=False,
                )
                comparison_views.append(
                    {
                        "key": "dry_vs_software",
                        "label": "Dry vs Software Wet",
                        "waveform_plot": report_dry_sw_waveform,
                        "metrics_plot": report_dry_sw_metrics,
                        "metrics": sw_result.metrics.as_dict(),
                    }
                )

                hw_result = compare_waveforms(actual, source_wave, thresholds=thresholds)
                plot_comparison(
                    source_wave,
                    actual,
                    labels=["Dry", "Wet"],
                    title="Dry vs Hardware Wet",
                    save_path=report_dry_hw_waveform,
                    spectrogram_background=actual,
                    figsize=report_wave_size,
                    dpi=report_dpi,
                )
                plot_difference_metrics(
                    hw_result,
                    thresholds=thresholds,
                    figsize=report_metrics_size,
                    dpi=report_dpi,
                    save_path=report_dry_hw_metrics,
                    show=False,
                )
                comparison_views.append(
                    {
                        "key": "dry_vs_hardware",
                        "label": "Dry vs Hardware Wet",
                        "waveform_plot": report_dry_hw_waveform,
                        "metrics_plot": report_dry_hw_metrics,
                        "metrics": hw_result.metrics.as_dict(),
                    }
                )
        elif compare_mode == "dry_vs_software":
            assert sw_wave is not None and source_wave is not None
            plot_comparison(
                source_wave,
                sw_wave,
                labels=["Dry", "Wet"],
                title="Dry vs Software Wet",
                save_path=report_waveform,
                spectrogram_background=sw_wave,
                figsize=report_wave_size,
                dpi=report_dpi,
            )
            plot_difference_metrics(
                result,
                figsize=report_metrics_size,
                dpi=report_dpi,
                save_path=report_metrics,
                show=False,
            )
            comparison_views.append(
                {
                    "key": "dry_vs_software",
                    "label": "Dry vs Software Wet",
                    "waveform_plot": report_waveform,
                    "metrics_plot": report_metrics,
                    "metrics": result.metrics.as_dict(),
                }
            )
        else:  # dry_vs_hardware
            assert hw_wave is not None and source_wave is not None
            plot_comparison(
                source_wave,
                hw_wave,
                labels=["Dry", "Wet"],
                title="Dry vs Hardware Wet",
                save_path=report_waveform,
                spectrogram_background=hw_wave,
                figsize=report_wave_size,
                dpi=report_dpi,
            )
            plot_difference_metrics(
                result,
                figsize=report_metrics_size,
                dpi=report_dpi,
                save_path=report_metrics,
                show=False,
            )
            comparison_views.append(
                {
                    "key": "dry_vs_hardware",
                    "label": "Dry vs Hardware Wet",
                    "waveform_plot": report_waveform,
                    "metrics_plot": report_metrics,
                    "metrics": result.metrics.as_dict(),
                }
            )

        write_compare_html_report(
            report_html,
            payload=payload,
            comparison_views=comparison_views,
        )
        if not args.json:
            print(f"Report written to {report_dir}")

    if args.plot:
        if gated:
            plot_comparison(expected, actual, save_path=args.plot)
        elif compare_mode == "dry_vs_software":
            assert sw_wave is not None and source_wave is not None
            plot_comparison(
                source_wave,
                sw_wave,
                labels=["Dry", "Wet"],
                title="Dry vs Software Wet",
                spectrogram_background=sw_wave,
                save_path=args.plot,
            )
        else:
            assert hw_wave is not None and source_wave is not None
            plot_comparison(
                source_wave,
                hw_wave,
                labels=["Dry", "Wet"],
                title="Dry vs Hardware Wet",
                spectrogram_background=hw_wave,
                save_path=args.plot,
            )
        if not args.json:
            print(f"Plot saved to {args.plot}")
    elif args.metrics_plot:
        plot_difference_metrics(
            result,
            thresholds=thresholds if gated else None,
            save_path=args.metrics_plot,
            show=False,
        )
        if not args.json:
            print(f"Metrics plot saved to {args.metrics_plot}")

    if not gated:
        return 0
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
            "are interpreted as <session_name> <snapshot_id>. With both wet "
            "captures, compares output_audio_hw vs output_audio (pass/fail). "
            "With only software or only hardware plus input_audio, writes an "
            "informational dry-vs-wet report (no pass/fail)."
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
