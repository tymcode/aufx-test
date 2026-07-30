"""Mismatch reporting helpers for automated waveform tests."""

from __future__ import annotations

import json
import os
from html import escape
from pathlib import Path
from typing import Any
from urllib.parse import quote

from .audio import Waveform
from .compare_config import CompareConfig, load_compare_config
from .comparison import (
    ComparisonResult,
    ComparisonThresholds,
    align_waveforms,
    compare_waveforms,
)
from .graphing import plot_difference_metrics, plot_waveforms
from .spectrum import analysis_bands, band_amplitude_over_time, band_amplitude_to_db


def compare_for_test(
    actual: Waveform,
    expected: Waveform,
    *,
    thresholds: ComparisonThresholds | None = None,
    allow_extra_actual_tail: bool = True,
    max_alignment_samples: int = 64,
) -> ComparisonResult:
    """Compare waveforms with test-friendly length handling.

    When ``allow_extra_actual_tail`` is true (default), a longer actual render
    is trimmed to the reference length before scoring. Imported goldens may
    have truncated reverb tails; extra actual tail is not treated as a failure.
    """
    return compare_waveforms(
        actual,
        expected,
        thresholds=thresholds,
        allow_extra_actual_tail=allow_extra_actual_tail,
        max_alignment_samples=max_alignment_samples,
    )


def band_analysis(
    actual: Waveform,
    expected: Waveform,
    *,
    config: CompareConfig | None = None,
) -> dict[str, Any]:
    """Per-band mean amplitude delta using bands from compare.config.json."""
    config = config or load_compare_config()
    bands = analysis_bands(config=config)
    actual_bands = band_amplitude_over_time(actual, bands=bands, config=config)
    expected_bands = band_amplitude_over_time(expected, bands=bands, config=config)
    rows: list[dict[str, Any]] = []
    for band in bands:
        a = actual_bands[band.name]
        e = expected_bands[band.name]
        a_db = band_amplitude_to_db(a)
        e_db = band_amplitude_to_db(e)
        a_mean = float(
            sum(a_db[f"channel_{ch}"].mean() for ch in range(actual.num_channels)) / actual.num_channels
        )
        e_mean = float(
            sum(e_db[f"channel_{ch}"].mean() for ch in range(expected.num_channels)) / expected.num_channels
        )
        rows.append(
            {
                "band": band.name,
                "low_hz": band.low_hz,
                "high_hz": band.high_hz,
                "actual_db": round(a_mean, 3),
                "expected_db": round(e_mean, 3),
                "delta_db": round(a_mean - e_mean, 3),
            }
        )
    return {
        "num_of_bands": config.num_of_bands,
        "window_samples": config.window_samples,
        "band_low_hz": config.band_low_hz,
        "band_high_hz": config.band_high_hz,
        "bands": rows,
    }


def write_mismatch_report(
    results_dir: str | Path,
    *,
    name: str,
    actual: Waveform,
    expected: Waveform,
    result: ComparisonResult,
    input_audio: Waveform | None = None,
    thresholds: ComparisonThresholds | None = None,
    setup: dict[str, Any] | None = None,
) -> Path:
    """Write actual/expected WAVs, JSON metrics, and plots for a failed comparison."""
    out = Path(results_dir) / name
    out.mkdir(parents=True, exist_ok=True)

    actual.to_file(out / "actual.wav")
    expected.to_file(out / "expected.wav")
    if input_audio is not None:
        input_audio.to_file(out / "input.wav")

    # Align for reporting the same way scoring does when tails may be truncated.
    actual_cmp, expected_cmp, _ = align_waveforms(
        actual,
        expected,
        max_lag_samples=abs(result.metrics.alignment_lag_samples),
    )
    if actual.num_samples > expected.num_samples:
        actual_cmp = actual_cmp.with_data(actual_cmp.data[: expected.num_samples])

    payload: dict[str, Any] = {
        "name": name,
        "passed": result.passed,
        "failures": list(result.failures),
        "metrics": result.metrics.as_dict(),
        "lengths": {
            "actual_samples": actual.num_samples,
            "expected_samples": expected.num_samples,
            "actual_seconds": round(actual.duration_seconds, 4),
            "expected_seconds": round(expected.duration_seconds, 4),
            "compared_samples": min(actual_cmp.num_samples, expected_cmp.num_samples),
            "extra_actual_tail_seconds": round(
                max(0.0, actual.duration_seconds - expected.duration_seconds), 4
            ),
            "alignment_lag_samples": result.metrics.alignment_lag_samples,
            "alignment_lag_ms": round(
                result.metrics.alignment_lag_samples / expected.sample_rate * 1000.0, 4
            ),
        },
        "band_analysis": band_analysis(actual_cmp, expected_cmp),
    }
    if thresholds is not None:
        payload["thresholds"] = thresholds.__dict__
    if setup is not None:
        payload["setup"] = {
            k: setup.get(k)
            for k in (
                "name",
                "plugin_path",
                "input_audio",
                "reference_output",
                "preset_file",
                "expect_match",
                "source_snapshot_id",
            )
            if k in setup
        }

    (out / "mismatch.json").write_text(json.dumps(payload, indent=2) + "\n")

    plot_waveforms(
        actual_cmp,
        expected_cmp,
        labels=["actual", "expected"],
        title=f"{name}: actual vs expected",
        save_path=out / "waveform.png",
        show=False,
    )
    plot_difference_metrics(result, title=f"{name}: metrics", save_path=out / "metrics.png", show=False)

    return out


def assert_setup_comparison(
    actual: Waveform,
    expected: Waveform,
    *,
    setup: dict[str, Any],
    thresholds: ComparisonThresholds | None = None,
    input_audio: Waveform | None = None,
    results_root: str | Path = "test-results",
    allow_extra_actual_tail: bool = True,
    max_alignment_samples: int = 64,
) -> ComparisonResult:
    """Compare a test setup and dump artifacts when the assertion fails."""
    result = compare_for_test(
        actual,
        expected,
        thresholds=thresholds,
        allow_extra_actual_tail=allow_extra_actual_tail,
        max_alignment_samples=max_alignment_samples,
    )
    expect_match = setup.get("expect_match", True)
    failed = (expect_match and not result.passed) or ((not expect_match) and result.passed)
    if failed:
        report_dir = write_mismatch_report(
            results_root,
            name=str(setup.get("name") or "unnamed"),
            actual=actual,
            expected=expected,
            result=result,
            input_audio=input_audio,
            thresholds=thresholds,
            setup=setup,
        )
        if expect_match:
            raise AssertionError(f"{result.summary()}\nMismatch report: {report_dir}")
        raise AssertionError(
            "Negative case: output still matches the broken reference\n"
            f"{result.summary()}\nMismatch report: {report_dir}"
        )
    return result


def write_html_report(
    output_path: str | Path,
    *,
    test_records: list[dict[str, Any]],
    mismatch_reports: list[str | Path],
) -> Path:
    """Write a shareable HTML summary of pytest outcomes and mismatch artifacts."""
    output = Path(output_path).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    reports = [Path(path).resolve() for path in mismatch_reports]

    counts = {
        outcome: sum(record.get("outcome") == outcome for record in test_records)
        for outcome in ("passed", "failed", "skipped")
    }
    total = sum(counts.values())

    def asset_url(path: Path) -> str:
        relative = os.path.relpath(path, output.parent)
        return quote(Path(relative).as_posix())

    case_cards: list[str] = []
    for report_dir in reports:
        metadata_path = report_dir / "mismatch.json"
        if not metadata_path.is_file():
            continue
        metadata = json.loads(metadata_path.read_text())
        metrics = metadata.get("metrics", {})
        lengths = metadata.get("lengths", {})
        failures = "".join(f"<li>{escape(str(item))}</li>" for item in metadata.get("failures", []))
        setup = metadata.get("setup", {})
        preset = setup.get("preset_file")
        preset_line = (
            f"<div><strong>Preset:</strong> <code>{escape(Path(preset).name)}</code></div>"
            if preset
            else ""
        )

        audio_players: list[str] = []
        for filename, label in (
            ("input.wav", "Input"),
            ("expected.wav", "Expected"),
            ("actual.wav", "Actual"),
        ):
            path = report_dir / filename
            if path.is_file():
                audio_players.append(
                    f'<div class="audio"><span>{label}</span>'
                    f'<audio controls preload="none" src="{asset_url(path)}"></audio>'
                    f'<a href="{asset_url(path)}" download>download</a></div>'
                )

        images = []
        for filename, label in (("waveform.png", "Waveforms"), ("metrics.png", "Metrics")):
            path = report_dir / filename
            if path.is_file():
                images.append(
                    f'<figure><a href="{asset_url(path)}"><img src="{asset_url(path)}" '
                    f'alt="{label}"></a><figcaption>{label}</figcaption></figure>'
                )

        case_cards.append(
            f"""
            <section class="case">
              <h2>{escape(str(metadata.get("name", report_dir.name)))}</h2>
              {preset_line}
              <div class="metrics">
                <span>Correlation <strong>{float(metrics.get("correlation", 0)):.4f}</strong></span>
                <span>SNR <strong>{float(metrics.get("snr_db", 0)):.2f} dB</strong></span>
                <span>RMS error <strong>{float(metrics.get("rms_error", 0)):.6f}</strong></span>
                <span>Lag <strong>{int(metrics.get("alignment_lag_samples", 0))} samples</strong></span>
              </div>
              <ul class="failures">{failures}</ul>
              <div class="detail">Actual {lengths.get("actual_seconds", "?")}s ·
                Expected {lengths.get("expected_seconds", "?")}s ·
                Extra tail {lengths.get("extra_actual_tail_seconds", 0)}s</div>
              <div class="audio-grid">{''.join(audio_players)}</div>
              <div class="images">{''.join(images)}</div>
              <a href="{asset_url(metadata_path)}">Raw mismatch JSON</a>
            </section>
            """
        )

    rows = "".join(
        f"<tr><td><code>{escape(str(record.get('nodeid', '')))}</code></td>"
        f"<td class=\"{escape(str(record.get('outcome', '')))}\">"
        f"{escape(str(record.get('outcome', 'unknown')))}</td>"
        f"<td>{float(record.get('duration', 0)):.2f}s</td></tr>"
        for record in test_records
    )
    html = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>AU/FX Test Report</title>
  <style>
    :root {{ color-scheme: light dark; --ok:#238636; --bad:#cf222e; --muted:#6e7781; }}
    body {{ font: 15px system-ui,sans-serif; max-width:1200px; margin:40px auto; padding:0 24px; }}
    h1 {{ margin-bottom:4px; }} .summary {{ display:flex; gap:12px; margin:24px 0; }}
    .pill,.metrics span {{ padding:8px 12px; border:1px solid #8885; border-radius:8px; }}
    .failed,.failures {{ color:var(--bad); }} .passed {{ color:var(--ok); }}
    table {{ width:100%; border-collapse:collapse; }}
    td,th {{ padding:8px; border-bottom:1px solid #8884; text-align:left; }}
    .case {{ margin:36px 0; padding:22px; border:1px solid #8885; border-radius:12px; }}
    .metrics {{ display:flex; flex-wrap:wrap; gap:8px; margin:16px 0; }}
    .detail {{ color:var(--muted); margin:12px 0; }}
    .audio-grid {{ display:grid; gap:8px; margin:18px 0; }}
    .audio {{ display:grid; grid-template-columns:75px minmax(250px,1fr) 70px; align-items:center; gap:10px; }}
    audio {{ width:100%; height:36px; }}
    .images {{ display:grid; grid-template-columns:repeat(auto-fit,minmax(320px,1fr)); gap:16px; }}
    figure {{ margin:0; }} img {{ width:100%; border-radius:6px; }} figcaption {{ color:var(--muted); }}
    code {{ overflow-wrap:anywhere; }}
  </style>
</head>
<body>
  <h1>AU/FX Test Report</h1>
  <div class="detail">{total} tests</div>
  <div class="summary">
    <span class="pill passed">{counts['passed']} passed</span>
    <span class="pill failed">{counts['failed']} failed</span>
    <span class="pill">{counts['skipped']} skipped</span>
  </div>
  <h2>Test results</h2>
  <table><thead><tr><th>Test</th><th>Outcome</th><th>Duration</th></tr></thead>
  <tbody>{rows}</tbody></table>
  <h2>Mismatch details</h2>
  {''.join(case_cards) or '<p>No mismatch artifacts were produced.</p>'}
</body>
</html>
"""
    output.write_text(html)
    return output
