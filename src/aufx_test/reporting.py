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
    plot_difference_metrics(
        result,
        thresholds=thresholds,
        title=f"{name}: metrics",
        save_path=out / "metrics.png",
        show=False,
    )

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


def write_compare_html_report(
    output_path: str | Path,
    *,
    payload: dict[str, Any],
    comparison_views: list[dict[str, Any]] | None = None,
) -> Path:
    """Write a standalone HTML compare report for one CLI compare run."""
    output = Path(output_path).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    bands = payload.get("band_analysis", {}).get("bands", [])
    thresholds = payload.get("thresholds", {}) or {}
    context = payload.get("context", {}) or {}
    gated = bool(payload.get("gated", "passed" in payload and payload.get("passed") is not None))
    mode = str(payload.get("mode", "hardware_vs_software"))

    def asset_url(path: str | Path) -> str:
        relative = os.path.relpath(Path(path).resolve(), output.parent)
        return quote(Path(relative).as_posix())

    def metric_status(kind: str, value: float) -> str:
        """Return CSS class for threshold pass/fail; empty when no gate applies."""
        if not gated or not thresholds:
            return ""
        if kind == "correlation":
            return "metric-ok" if value >= float(thresholds.get("correlation_min", 0.0)) else "metric-bad"
        if kind == "snr_db":
            return "metric-ok" if value >= float(thresholds.get("snr_db_min", 0.0)) else "metric-bad"
        if kind == "rms_error":
            return "metric-ok" if value <= float(thresholds.get("rms_error_max", 0.0)) else "metric-bad"
        if kind == "spectral_distance":
            return (
                "metric-ok"
                if value <= float(thresholds.get("spectral_distance_max", 0.0))
                else "metric-bad"
            )
        return ""

    def metrics_html(metrics: dict[str, Any]) -> str:
        correlation = float(metrics.get("correlation", 0.0))
        snr_db = float(metrics.get("snr_db", 0.0))
        rms_error = float(metrics.get("rms_error", 0.0))
        spectral = float(metrics.get("spectral_distance", 0.0))
        lag = int(metrics.get("alignment_lag_samples", 0))
        return (
            f'<span class="{metric_status("correlation", correlation)}">'
            f"Correlation <strong>{correlation:.4f}</strong></span>"
            f'<span class="{metric_status("snr_db", snr_db)}">'
            f"SNR <strong>{snr_db:.2f} dB</strong></span>"
            f'<span class="{metric_status("rms_error", rms_error)}">'
            f"RMS error <strong>{rms_error:.6f}</strong></span>"
            f'<span class="{metric_status("spectral_distance", spectral)}">'
            f"Spectral distance <strong>{spectral:.6f}</strong></span>"
            f"<span>Lag <strong>{lag} samples</strong></span>"
        )

    views = comparison_views or []
    if not views:
        views = [
            {
                "key": "default",
                "label": "Comparison",
                "waveform_plot": None,
                "metrics_plot": None,
                "metrics": payload,
            }
        ]

    controls = "".join(
        (
            f'<button class="view-btn" data-view="{escape(str(view.get("key", "")))}">'
            f'{escape(str(view.get("label", "View")))}</button>'
        )
        for view in views
    )

    panels: list[str] = []
    for idx, view in enumerate(views):
        metrics = view.get("metrics") or {}
        waveform_plot = view.get("waveform_plot")
        metrics_plot = view.get("metrics_plot")
        waveform_img = (
            f'<figure class="plot-card plot-waveform">'
            f'<img src="{asset_url(waveform_plot)}" alt="Waveform comparison">'
            "<figcaption>Waveform comparison</figcaption></figure>"
            if waveform_plot
            else ""
        )
        metrics_img = (
            f'<figure class="plot-card plot-metrics">'
            f'<img src="{asset_url(metrics_plot)}" alt="Difference metrics">'
            "<figcaption>Difference metrics</figcaption></figure>"
            if metrics_plot
            else ""
        )
        style = "" if idx == 0 else ' style="display:none"'
        panels.append(
            f"""
  <section class="view-panel" data-view="{escape(str(view.get("key", "")))}"{style}>
    <h3>{escape(str(view.get("label", "View")))}</h3>
    <div class="metrics">{metrics_html(metrics)}</div>
    <div class="plot-stack">
      {waveform_img}
      {metrics_img}
    </div>
  </section>
"""
        )

    rows = "".join(
        f"<tr><td>{escape(str(row.get('band', '')))}</td>"
        f"<td>{float(row.get('low_hz', 0)):.0f}-{float(row.get('high_hz', 0)):.0f}</td>"
        f"<td>{float(row.get('actual_db', 0)):.3f}</td>"
        f"<td>{float(row.get('expected_db', 0)):.3f}</td>"
        f"<td>{float(row.get('delta_db', 0)):+.3f}</td></tr>"
        for row in bands
    )
    pretty_json = escape(json.dumps(payload, indent=2))
    if gated:
        passed = bool(payload.get("passed", False))
        status_text = "PASSED" if passed else "FAILED"
        status_class = "ok" if passed else "bad"
    else:
        status_text = {
            "dry_vs_software": "DRY VS SOFTWARE",
            "dry_vs_hardware": "DRY VS HARDWARE",
        }.get(mode, "REPORT")
        status_class = "info"
    source_clip = context.get("source_clip")
    snapshot_name = context.get("snapshot_name")
    snapshot_id = context.get("snapshot_id")
    session_name = context.get("session_name")
    context_line = " · ".join(
        str(item)
        for item in (
            f"Session: {session_name}" if session_name else None,
            f"Snapshot: {snapshot_name} ({snapshot_id})" if snapshot_name and snapshot_id else None,
            f"Source Clip: {source_clip}" if source_clip else None,
        )
        if item
    )
    thresholds_section = ""
    if gated and thresholds:
        thresholds_section = f"""
  <h2>Thresholds</h2>
  <div class="metrics">
    <span>SNR min <strong>{float(thresholds.get('snr_db_min', 0)):.2f} dB</strong></span>
    <span>Correlation min <strong>{float(thresholds.get('correlation_min', 0)):.4f}</strong></span>
    <span>RMS max <strong>{float(thresholds.get('rms_error_max', 0)):.6f}</strong></span>
    <span>Spectral max <strong>{float(thresholds.get('spectral_distance_max', 0)):.6f}</strong></span>
  </div>
"""
    band_actual_label = "Wet dB" if not gated else "Actual dB"
    band_expected_label = "Dry dB" if not gated else "Expected dB"

    html = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>AU/FX Compare Report</title>
  <style>
    :root {{ color-scheme: light dark; --ok:#238636; --bad:#cf222e; --muted:#6e7781; }}
    body {{ font: 15px system-ui,sans-serif; max-width:1600px; margin:40px auto; padding:0 24px; }}
    h1 {{ margin-bottom:8px; }}
    .pill {{ display:inline-block; padding:8px 12px; border-radius:8px; border:1px solid #8885; margin:8px 0 16px; }}
    .ok {{ color:var(--ok); background:#23863622; border-color:#23863688; }}
    .bad {{ color:var(--bad); background:#cf222e22; border-color:#cf222e88; }}
    .info {{ color:var(--muted); background:#8882; border-color:#8885; }}
    .metrics {{ display:flex; flex-wrap:wrap; gap:10px; margin:16px 0; }}
    .metrics span {{ padding:8px 12px; border:1px solid #8885; border-radius:8px; }}
    .metrics span.metric-ok {{ background:#23863633; border-color:#23863699; }}
    .metrics span.metric-bad {{ background:#cf222e33; border-color:#cf222e99; }}
    .controls {{ display:flex; gap:8px; flex-wrap:wrap; margin:16px 0; }}
    .view-btn {{ border:1px solid #8885; border-radius:8px; padding:8px 12px; background:transparent; cursor:pointer; }}
    .view-btn.active {{ border-color:#3b82f6; background:#3b82f620; }}
    .plot-stack {{ display:flex; flex-direction:column; gap:18px; margin:18px 0 28px; }}
    .plot-card {{
      margin:0; padding:14px; border:1px solid #8885; border-radius:12px;
      background:#8881; box-shadow:0 8px 24px #0002;
    }}
    .plot-waveform {{ border-color:#3b82f688; background:#3b82f610; }}
    .plot-metrics {{ border-color:#8885; }}
    .plot-card img {{ width:100%; height:auto; display:block; border-radius:6px; }}
    .plot-card figcaption {{ color:var(--muted); margin-top:8px; }}
    table {{ width:100%; border-collapse:collapse; margin:16px 0; }}
    td,th {{ padding:8px; border-bottom:1px solid #8884; text-align:left; }}
    pre {{ white-space:pre-wrap; border:1px solid #8885; border-radius:8px; padding:12px; overflow-x:auto; }}
    code {{ overflow-wrap:anywhere; }}
  </style>
</head>
<body>
  <h1>AU/FX Compare Report</h1>
  <div class="pill {status_class}"><strong>{status_text}</strong></div>
  <div>{escape(context_line)}</div>
  <div class="metrics">{metrics_html(payload)}</div>
  {thresholds_section}
  <h2>Plots</h2>
  <div class="controls">{controls}</div>
  {''.join(panels)}
  <h2>Band Analysis</h2>
  <table>
    <thead><tr><th>Band</th><th>Range (Hz)</th>
      <th>{band_actual_label}</th><th>{band_expected_label}</th><th>Delta dB</th></tr></thead>
    <tbody>{rows}</tbody>
  </table>
  <h2>Raw JSON</h2>
  <pre><code>{pretty_json}</code></pre>
  <script>
    const buttons = document.querySelectorAll(".view-btn");
    const panels = document.querySelectorAll(".view-panel");
    const setActive = (key) => {{
      buttons.forEach((btn) => btn.classList.toggle("active", btn.dataset.view === key));
      panels.forEach((panel) => {{
        panel.style.display = panel.dataset.view === key ? "" : "none";
      }});
    }};
    if (buttons.length) {{
      setActive(buttons[0].dataset.view);
      buttons.forEach((btn) => btn.addEventListener("click", () => setActive(btn.dataset.view)));
    }}
  </script>
</body>
</html>
"""
    output.write_text(html)
    return output
