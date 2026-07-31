"""Plot hardware or software level-sweep transfer curves."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import matplotlib.pyplot as plt

SINE_RMS_OFFSET = -3.01029995664


def _series(points: list[dict]) -> dict[str, list[float]]:
    send = [float(p["send_db"]) for p in points]
    peak: list[float] = []
    rms: list[float] = []
    lufs: list[float] = []

    for p in points:
        if "measured_peak_db" in p:
            peak.append(float(p["measured_peak_db"]))
        elif "hw_peak_db" in p:
            peak.append(float(p["hw_peak_db"]))
        else:
            peak.append(float(p.get("sw_peak_db", -120.0)))

        if "measured_rms_db" in p:
            rms.append(float(p["measured_rms_db"]))
        elif "hw_rms_db" in p and "sw_rms_db" in p:
            hw = float(p["hw_rms_db"])
            sw = float(p["sw_rms_db"])
            rms.append(hw if hw > -119.0 else sw)
        elif "hw_rms_db" in p:
            rms.append(float(p["hw_rms_db"]))
        else:
            rms.append(float(p.get("sw_rms_db", -120.0)))

        lufs.append(float(p.get("measured_lufs", rms[-1])))

    return {
        "send": send,
        "peak": peak,
        "rms": rms,
        "lufs": lufs,
        "ideal_peak": list(send),
        "ideal_rms": [s + SINE_RMS_OFFSET for s in send],
        "ideal_lufs": [s + SINE_RMS_OFFSET for s in send],
    }


def _cmd_calibrate_plot(args: argparse.Namespace) -> int:
    data = json.loads(Path(args.json_path).expanduser().read_text())
    points = data.get("points") or []
    if not points:
        raise ValueError("level-sweep JSON has no points")

    series = _series(points)
    send = series["send"]

    plot_name = data.get("plot_name") or Path(args.json_path).stem
    path = data.get("path") or "measured"
    device = data.get("device_name") or ""
    title_bits = [str(plot_name), str(path)]
    if device:
        title_bits.append(str(device))

    out_path = Path(args.output).expanduser()
    out_path.parent.mkdir(parents=True, exist_ok=True)

    fig, axes = plt.subplots(
        2, 1, figsize=(10, 9), sharex=True, gridspec_kw={"height_ratios": [2.4, 1.0]}
    )

    ax = axes[0]
    ax.plot(send, series["ideal_peak"], color="#888", linestyle=":", linewidth=1.1, label="Ideal peak")
    ax.plot(send, series["ideal_rms"], color="#888", linestyle="--", linewidth=1.1, label="Ideal RMS/LUFS")
    ax.plot(send, series["peak"], marker="s", linewidth=1.6, label="Peak")
    ax.plot(send, series["rms"], marker="o", linewidth=1.6, label="RMS")
    ax.plot(send, series["lufs"], marker="D", linewidth=1.6, label="LUFS (BS.1770)")
    ax.axhline(0.0, color="#c0392b", linestyle=":", linewidth=0.9, alpha=0.7)
    ax.set_ylabel("Level (dB / LUFS)")
    ax.set_title("Level sweep — " + " / ".join(title_bits))
    ax.grid(True, alpha=0.3)
    ax.legend(loc="best", fontsize=8)

    ax2 = axes[1]
    ax2.axhline(0.0, color="#888", linestyle="--", linewidth=1.0)
    ax2.plot(
        send,
        [p - i for p, i in zip(series["peak"], series["ideal_peak"], strict=True)],
        marker="s",
        linewidth=1.4,
        label="Peak − ideal",
    )
    ax2.plot(
        send,
        [m - i for m, i in zip(series["rms"], series["ideal_rms"], strict=True)],
        marker="o",
        linewidth=1.4,
        label="RMS − ideal",
    )
    ax2.plot(
        send,
        [m - i for m, i in zip(series["lufs"], series["ideal_lufs"], strict=True)],
        marker="D",
        linewidth=1.4,
        label="LUFS − ideal",
    )
    ax2.set_xlabel("Commanded send (dBFS)")
    ax2.set_ylabel("Δ dB")
    ax2.grid(True, alpha=0.3)
    ax2.legend(loc="best", fontsize=8)

    footer = []
    max_peak = data.get("max_abs_measured_minus_ideal_peak_db")
    if max_peak is None:
        max_peak = data.get("max_abs_hw_minus_ideal_peak_db")
    if max_peak is not None:
        footer.append(f"max |peak − send| = {float(max_peak):.2f} dB")
    if data.get("any_clipped"):
        footer.append("CLIP detected at ≥0 dBFS")
    skip = data.get("analyse_skip_seconds")
    win = data.get("analyse_window_seconds")
    if skip is not None and win is not None:
        footer.append(f"window {float(skip):.2f}+{float(win):.2f}s")
    bypassed = data.get("bypassed")
    mix = data.get("mix_amount")
    if bypassed is not None or mix is not None:
        footer.append(f"bypass={bool(bypassed)} mix={float(mix) if mix is not None else 1.0:.2f}")
    if footer:
        fig.text(0.5, 0.01, "  ·  ".join(footer), ha="center", fontsize=9, color="#444")

    fig.tight_layout(rect=(0, 0.04, 1, 1))
    fig.savefig(out_path, dpi=140)
    plt.close(fig)
    print(f"Wrote {out_path}")
    return 0


def add_calibrate_plot_parser(sub: argparse._SubParsersAction) -> None:
    plot = sub.add_parser(
        "calibrate-plot",
        help="Plot level-sweep curves from calibration JSON",
    )
    plot.add_argument("json_path", type=Path, help="Path to level_sweep JSON")
    plot.add_argument(
        "-o",
        "--output",
        type=Path,
        required=True,
        help="Output PNG path",
    )
    plot.set_defaults(func=_cmd_calibrate_plot)
