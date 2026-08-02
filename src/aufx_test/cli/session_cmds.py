"""Session subcommand implementations."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from ..compare_config import load_compare_config
from ..comparison import ComparisonThresholds
from ..explore import capture_snapshot_from_cli
from ..params import parse_param_args
from ..session import ExperimentSession
from ..testgen import export_setups_json, export_test_module
from .session_loader import _list_session_folders, _load_session


def _cmd_session_new(args: argparse.Namespace) -> int:
    session = ExperimentSession.create(
        args.name,
        plugin_path=args.plugin,
        description=args.description or "",
        root_dir=args.root,
    )
    print(f"Created session {session.name!r} at {session.session_file}")
    return 0


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
