"""Compare command implementation."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from ..audio import Waveform
from ..compare_config import clear_compare_config_cache, load_compare_config
from ..comparison import ComparisonThresholds, compare_waveforms
from ..graphing import plot_comparison, plot_difference_metrics
from ..reporting import band_analysis, write_compare_html_report
from ..session import ExperimentSession, StateSnapshot
from .session_loader import _load_session


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
