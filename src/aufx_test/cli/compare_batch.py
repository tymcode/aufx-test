"""CLI for batch dry→preset→render→golden compares."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from ..compare_batch import (
    discover_compare_cases,
    resolve_plugin_path,
    resolve_renderer_bin,
    run_compare_batch,
)
from ..compare_config import clear_compare_config_cache, load_compare_config
from ..comparison import ComparisonThresholds


def _cmd_compare_batch(args: argparse.Namespace) -> int:
    clear_compare_config_cache()
    compares_dir = Path(args.directory).expanduser().resolve()

    try:
        cases, warnings = discover_compare_cases(
            compares_dir,
            dry_dir=args.dry_dir,
            presets_dir=args.presets_dir,
            goldens_dir=args.goldens_dir,
            filter_glob=args.filter,
        )
    except (FileNotFoundError, ValueError) as exc:
        print(f"error: {exc}")
        return 2

    for warning in warnings:
        print(f"warning: {warning}")

    if args.limit is not None:
        cases = cases[: max(0, args.limit)]

    if args.list_only:
        for case in cases:
            print(
                f"{case.name}\n"
                f"  dry={case.dry.name}  preset={case.preset.name}  golden={case.golden.name}"
            )
        print(f"{len(cases)} case(s)" + (f", {len(warnings)} skipped" if warnings else ""))
        return 0

    if not cases:
        print("error: no runnable compare cases (check dry/presets/goldens layout)")
        return 2

    try:
        plugin = resolve_plugin_path(
            explicit=args.plugin,
            plugin_id=args.plugin_id,
            host_config=args.host_config,
        )
        renderer = resolve_renderer_bin(args.renderer)
    except (FileNotFoundError, ValueError) as exc:
        print(f"error: {exc}")
        return 2

    if not renderer.is_file():
        print(f"error: plugin_renderer not found: {renderer}")
        print("  Build it, or set AUFX_PLUGIN_RENDERER / pass --renderer")
        return 2

    config = load_compare_config(args.compare_config)
    thresholds = config.thresholds
    if any(
        v is not None
        for v in (args.snr_min, args.corr_min, args.rms_max, args.spectral_max)
    ):
        thresholds = ComparisonThresholds(
            snr_db_min=args.snr_min if args.snr_min is not None else thresholds.snr_db_min,
            correlation_min=(
                args.corr_min if args.corr_min is not None else thresholds.correlation_min
            ),
            rms_error_max=(
                args.rms_max if args.rms_max is not None else thresholds.rms_error_max
            ),
            spectral_distance_max=(
                args.spectral_max
                if args.spectral_max is not None
                else thresholds.spectral_distance_max
            ),
        )

    results_root = (
        Path(args.results_root).expanduser().resolve()
        if args.results_root is not None
        else Path("test-results") / compares_dir.name
    )

    print(f"compares: {compares_dir}")
    print(f"plugin:   {plugin}")
    print(f"renderer: {renderer}")
    print(f"results:  {results_root}")
    print(f"settle:   {args.settle_seconds:g}s")
    print(f"cases:    {len(cases)}" + (f" ({len(warnings)} skipped)" if warnings else ""))
    print()

    batch = run_compare_batch(
        cases,
        plugin_path=plugin,
        renderer_bin=renderer,
        results_root=results_root,
        thresholds=thresholds,
        keep_actuals=not args.no_keep_actuals,
        fail_fast=args.fail_fast,
        write_html=not args.no_html,
        settle_seconds=args.settle_seconds,
    )

    for case in batch.cases:
        if case.status == "passed":
            print(f"PASS {case.name}")
        elif case.status == "failed":
            print(f"FAIL {case.name}")
            if case.report_dir is not None:
                print(f"     report: {case.report_dir}")
        else:
            print(f"ERROR {case.name}: {case.message}")

    print()
    print(
        f"summary: {batch.passed} passed, {batch.failed} failed, "
        f"{batch.errors} errors, {batch.skipped} skipped"
    )
    print(f"summary.json: {results_root / 'summary.json'}")
    if not args.no_html:
        print(f"report.html:  {results_root / 'report.html'}")

    if args.json:
        print(json.dumps(batch.to_dict(), indent=2))

    return 1 if (batch.failed or batch.errors) else 0


def add_compare_batch_parser(sub: argparse._SubParsersAction) -> None:
    parser = sub.add_parser(
        "compare-batch",
        help=(
            "Render dry sources through presets named in golden filenames and "
            "compare against goldens (writes per-fail reports)"
        ),
    )
    parser.add_argument(
        "directory",
        type=Path,
        help="Compares root (expects dry/, presets/au/, goldens/)",
    )
    parser.add_argument("--dry-dir", type=Path, help="Override dry sources folder")
    parser.add_argument("--presets-dir", type=Path, help="Override presets/au folder")
    parser.add_argument("--goldens-dir", type=Path, help="Override goldens folder")
    parser.add_argument(
        "--filter",
        metavar="GLOB",
        help="Only goldens matching this glob (e.g. 'Vocals-*.wav')",
    )
    parser.add_argument(
        "--limit",
        type=int,
        default=None,
        help="Process at most N cases (after filter)",
    )
    parser.add_argument(
        "--list",
        dest="list_only",
        action="store_true",
        help="List discovered cases without rendering",
    )
    parser.add_argument(
        "--plugin",
        help="Plugin path or JUCE id (overrides --plugin-id / host.config.json / env)",
    )
    parser.add_argument(
        "--plugin-id",
        help="Lookup plugin id in host.config.json (e.g. deep_z)",
    )
    parser.add_argument(
        "--host-config",
        type=Path,
        help="Path to host.config.json (default: <project-root>/host.config.json)",
    )
    parser.add_argument(
        "--renderer",
        type=Path,
        help="Path to plugin_renderer (default: AUFX_PLUGIN_RENDERER or native build)",
    )
    parser.add_argument(
        "--results-root",
        type=Path,
        help="Output root for actuals/, fails/, summary.json, report.html",
    )
    parser.add_argument(
        "--compare-config",
        type=Path,
        help="Path to compare.config.json",
    )
    parser.add_argument("--snr-min", type=float, default=None)
    parser.add_argument("--corr-min", type=float, default=None)
    parser.add_argument("--rms-max", type=float, default=None)
    parser.add_argument("--spectral-max", type=float, default=None)
    parser.add_argument(
        "--settle-seconds",
        type=float,
        default=1.5,
        help=(
            "Silence lead-in after each preset load before the dry source "
            "(default: 1.5). Trimmed from the render so captures start after "
            "the plugin finishes transitioning. Use 0 to disable."
        ),
    )
    parser.add_argument(
        "--fail-fast",
        action="store_true",
        help="Stop after the first failure or render error",
    )
    parser.add_argument(
        "--no-keep-actuals",
        action="store_true",
        help="Do not write rendered WAVs under results/actuals/",
    )
    parser.add_argument(
        "--no-html",
        action="store_true",
        help="Skip aggregate report.html",
    )
    parser.add_argument("--json", action="store_true", help="Print machine-readable summary")
    parser.set_defaults(func=_cmd_compare_batch)
