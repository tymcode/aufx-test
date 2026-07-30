"""Matplotlib visualization for test results."""

from __future__ import annotations

from pathlib import Path
from typing import Any

import matplotlib.pyplot as plt
import numpy as np

from .audio import Waveform
from .comparison import ComparisonResult, ComparisonThresholds, DifferenceMetrics
from .silence import SilenceRegion
from .spectrum import band_amplitude_to_db


def _save_or_show(
    fig: plt.Figure,
    save_path: str | Path | None,
    show: bool,
    *,
    dpi: int = 150,
) -> None:
    if save_path is not None:
        path = Path(save_path)
        path.parent.mkdir(parents=True, exist_ok=True)
        fig.savefig(path, dpi=dpi, bbox_inches="tight")
    if show:
        plt.show()
    plt.close(fig)


def plot_waveforms(
    *waveforms: Waveform,
    labels: list[str] | None = None,
    channel: int = 0,
    max_seconds: float | None = None,
    title: str = "Waveforms",
    save_path: str | Path | None = None,
    show: bool = False,
) -> plt.Figure:
    """Plot one or more waveforms on the same axes."""
    fig, ax = plt.subplots(figsize=(12, 4))
    labels = labels or [f"signal_{i}" for i in range(len(waveforms))]

    for wav, label in zip(waveforms, labels, strict=True):
        n = wav.num_samples if max_seconds is None else min(wav.num_samples, int(max_seconds * wav.sample_rate))
        t = np.arange(n) / wav.sample_rate
        ax.plot(t, wav.channel_data(channel)[:n], label=label, alpha=0.8)

    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Amplitude")
    ax.set_title(title)
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    _save_or_show(fig, save_path, show)
    return fig


def plot_comparison(
    before: Waveform,
    after: Waveform,
    reference: Waveform | None = None,
    *,
    channel: int = 0,
    labels: list[str] | None = None,
    spectrogram_background: Waveform | None = None,
    spectrogram_channel: int = 0,
    spectrogram_alpha: float = 0.85,
    title: str = "Before / After / Reference",
    figsize: tuple[float, float] = (12, 4),
    dpi: int = 150,
    save_path: str | Path | None = None,
    show: bool = False,
) -> plt.Figure:
    """Plot before/after/reference waveforms, optionally over a wet spectrogram."""
    from matplotlib import mlab
    from matplotlib.transforms import blended_transform_factory

    series_labels = list(labels) if labels is not None else ["before", "after"]
    if len(series_labels) < 2:
        raise ValueError("labels must include at least two entries (before, after)")
    waves: list[Waveform] = [before, after]
    colors = ["#f0f6fc", "#58a6ff", "#ff7b72"] if spectrogram_background else [None, None, None]
    if reference is not None:
        waves.append(reference)
        if len(series_labels) < 3:
            series_labels.append("reference")

    fig, ax = plt.subplots(figsize=figsize)
    if spectrogram_background is not None:
        bg = spectrogram_background.channel_data(spectrogram_channel)
        nfft = 2048 if spectrogram_background.num_samples >= 4096 else 1024
        if spectrogram_background.num_samples < 1024:
            nfft = 256
        nfft = max(64, nfft)
        noverlap = int(nfft * 0.75)
        pxx, freqs, times = mlab.specgram(
            bg,
            NFFT=nfft,
            Fs=spectrogram_background.sample_rate,
            noverlap=noverlap,
        )
        # Single-axes composite avoids twin-axis patches covering the spectrogram.
        ax.imshow(
            10.0 * np.log10(np.maximum(pxx, 1e-12)),
            origin="lower",
            aspect="auto",
            extent=[times[0], times[-1], freqs[0], freqs[-1]],
            cmap="magma",
            interpolation="nearest",
            alpha=max(0.0, min(1.0, spectrogram_alpha)),
            zorder=0,
        )
        ax.set_ylabel("Frequency (Hz)")
        ax.set_xlim(0.0, max(wav.duration_seconds for wav in waves))
        # Envelope overlay (not raw samples) keeps the spectrogram readable.
        wave_transform = blended_transform_factory(ax.transData, ax.transAxes)
        peak = max(float(np.max(np.abs(wav.channel_data(channel)))) for wav in waves) or 1.0
        for i, (wav, label) in enumerate(zip(waves, series_labels[: len(waves)], strict=True)):
            samples = np.abs(wav.channel_data(channel)) / peak
            # ~5 ms windows → smooth amplitude envelope for overlay.
            win = max(1, int(wav.sample_rate * 0.005))
            n = samples.shape[0]
            n_win = max(1, n // win)
            usable = n_win * win
            shaped = samples[:usable].reshape(n_win, win)
            env = shaped.max(axis=1)
            t = (np.arange(n_win) * win + (win * 0.5)) / wav.sample_rate
            y = 0.08 + 0.34 * env
            ax.plot(
                t,
                y,
                transform=wave_transform,
                label=label,
                color=colors[i],
                linewidth=1.8,
                alpha=0.95,
                zorder=2 + i,
            )
        ax.legend(loc="upper right")
        ax.grid(False)
    else:
        for wav, label in zip(waves, series_labels[: len(waves)], strict=True):
            t = np.arange(wav.num_samples) / wav.sample_rate
            ax.plot(t, wav.channel_data(channel), label=label, alpha=0.9, linewidth=1.0)
        ax.set_ylabel("Amplitude")
        ax.legend(loc="upper right")
        ax.grid(True, alpha=0.25)

    ax.set_xlabel("Time (s)")
    ax.set_title(title)
    fig.tight_layout()
    _save_or_show(fig, save_path, show, dpi=dpi)
    return fig


def plot_difference_metrics(
    metrics: DifferenceMetrics | ComparisonResult,
    *,
    thresholds: ComparisonThresholds | dict[str, float] | None = None,
    title: str = "Difference Metrics",
    figsize: tuple[float, float] = (8, 4),
    dpi: int = 150,
    save_path: str | Path | None = None,
    show: bool = False,
) -> plt.Figure:
    """Bar chart of objective difference metrics.

    When ``thresholds`` is provided, each gated metric column gets a gray
    horizontal line at its tolerance (min for SNR/correlation, max for errors).
    """
    m = metrics.metrics if isinstance(metrics, ComparisonResult) else metrics
    names = ["snr_db", "correlation", "rms_error", "max_abs_error", "spectral_distance"]
    values = [getattr(m, name) for name in names]

    fig, ax = plt.subplots(figsize=figsize)
    bars = ax.bar(names, values, color=["#4c72b0", "#55a868", "#c44e52", "#8172b2", "#ccb974"])
    ax.set_title(title)
    ax.set_ylabel("Value")

    thr_map: dict[str, float] = {}
    if thresholds is not None:
        if isinstance(thresholds, ComparisonThresholds):
            thr_map = {
                "snr_db": thresholds.snr_db_min,
                "correlation": thresholds.correlation_min,
                "rms_error": thresholds.rms_error_max,
                "spectral_distance": thresholds.spectral_distance_max,
            }
        else:
            thr_map = {
                "snr_db": float(thresholds.get("snr_db_min", 0.0)),
                "correlation": float(thresholds.get("correlation_min", 0.0)),
                "rms_error": float(thresholds.get("rms_error_max", 0.0)),
                "spectral_distance": float(thresholds.get("spectral_distance_max", 0.0)),
            }

    for bar, _name, val in zip(bars, names, values, strict=True):
        label = "inf" if val == float("inf") else f"{val:.3f}"
        ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height(), label, ha="center", va="bottom")

    # Lock scale to the metric bars before drawing thresholds. A high SNR gate
    # must not expand ylim and crush the other columns; off-scale lines clip.
    y_lo, y_hi = ax.get_ylim()

    for bar, name in zip(bars, names, strict=True):
        if name not in thr_map:
            continue
        thr = thr_map[name]
        x0 = bar.get_x()
        x1 = x0 + bar.get_width()
        ax.hlines(
            thr,
            x0,
            x1,
            colors="#6e7781",
            linewidths=2.0,
            zorder=3,
            label="threshold" if name == next(iter(thr_map)) else None,
            clip_on=True,
        )
        if y_lo <= thr <= y_hi:
            ax.text(
                (x0 + x1) / 2,
                thr,
                f"{thr:.3g}",
                ha="center",
                va="bottom",
                fontsize=8,
                color="#6e7781",
                zorder=4,
                clip_on=True,
            )

    ax.set_ylim(y_lo, y_hi)

    if thr_map:
        handles, labels = ax.get_legend_handles_labels()
        if "threshold" in labels:
            ax.legend(loc="upper right", fontsize=8)

    fig.tight_layout()
    _save_or_show(fig, save_path, show, dpi=dpi)
    return fig


def plot_distance_from_silence(
    silence_data: dict[str, np.ndarray],
    *,
    title: str = "Distance from Silence",
    save_path: str | Path | None = None,
    show: bool = False,
) -> plt.Figure:
    """Plot distance-from-silence curves per channel."""
    fig, ax = plt.subplots(figsize=(12, 4))
    times = silence_data["times"]
    for key, values in silence_data.items():
        if key == "times":
            continue
        ax.plot(times, values, label=key, alpha=0.8)
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("dB above silence threshold")
    ax.set_title(title)
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    _save_or_show(fig, save_path, show)
    return fig


def plot_band_amplitude(
    band_results: dict[str, dict[str, np.ndarray]],
    *,
    channel: int = 0,
    use_db: bool = True,
    title: str = "Band Amplitude Over Time",
    save_path: str | Path | None = None,
    show: bool = False,
) -> plt.Figure:
    """Plot frequency-band amplitude over time."""
    fig, ax = plt.subplots(figsize=(12, 5))
    ch_key = f"channel_{channel}"

    for band_name, data in band_results.items():
        plot_data = band_amplitude_to_db(data) if use_db else data
        if ch_key in plot_data:
            ax.plot(plot_data["times"], plot_data[ch_key], label=band_name, alpha=0.8)

    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Amplitude (dB)" if use_db else "Amplitude (linear)")
    ax.set_title(title)
    ax.legend(loc="upper right", fontsize=8)
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    _save_or_show(fig, save_path, show)
    return fig


def plot_silence_regions(
    waveform: Waveform,
    regions: dict[int, list[SilenceRegion]],
    *,
    channel: int = 0,
    title: str = "Silence Regions",
    save_path: str | Path | None = None,
    show: bool = False,
) -> plt.Figure:
    """Plot waveform with silent regions highlighted."""
    fig, ax = plt.subplots(figsize=(12, 4))
    t = np.arange(waveform.num_samples) / waveform.sample_rate
    ax.plot(t, waveform.channel_data(channel), color="#333", linewidth=0.8, label=f"channel {channel}")

    for region in regions.get(channel, []):
        start_t = region.start_sample / waveform.sample_rate
        end_t = region.end_sample / waveform.sample_rate
        ax.axvspan(start_t, end_t, alpha=0.3, color="#c44e52", label="_silence")

    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Amplitude")
    ax.set_title(title)
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    _save_or_show(fig, save_path, show)
    return fig


def plot_test_results(
    result: dict[str, Any],
    *,
    save_path: str | Path | None = None,
    show: bool = False,
) -> plt.Figure:
    """Generic multi-panel plot for combined test output.

    Expected keys (all optional):
    ``waveforms``, ``comparison``, ``silence_distance``, ``band_amplitude``,
    ``silence_regions`` (+ ``waveform_for_regions``).
    """
    panels = _collect_panels(result)
    fig, axes = plt.subplots(len(panels), 1, figsize=(12, 3.5 * len(panels)))
    if len(panels) == 1:
        axes = [axes]

    for ax, (kind, payload) in zip(axes, panels, strict=True):
        _draw_panel(ax, kind, payload)

    fig.tight_layout()
    _save_or_show(fig, save_path, show)
    return fig


def _collect_panels(result: dict[str, Any]) -> list[tuple[str, Any]]:
    panels: list[tuple[str, Any]] = []
    if "waveforms" in result:
        panels.append(("waveforms", result["waveforms"]))
    if "comparison" in result:
        panels.append(("comparison", result["comparison"]))
    if "silence_distance" in result:
        panels.append(("silence_distance", result["silence_distance"]))
    if "band_amplitude" in result:
        panels.append(("band_amplitude", result["band_amplitude"]))
    if "silence_regions" in result:
        panels.append(
            (
                "silence_regions",
                {
                    "waveform": result.get("waveform_for_regions"),
                    "regions": result["silence_regions"],
                },
            )
        )
    return panels


def _draw_panel(ax: plt.Axes, kind: str, payload: Any) -> None:
    if kind == "waveforms":
        waves, labels = payload
        for wav, label in zip(waves, labels, strict=True):
            t = np.arange(wav.num_samples) / wav.sample_rate
            ax.plot(t, wav.channel, label=label, alpha=0.8)
        ax.legend()
        ax.set_title("Waveforms")
    elif kind == "comparison":
        metrics = payload.metrics if hasattr(payload, "metrics") else payload
        names = ["snr_db", "correlation", "rms_error", "spectral_distance"]
        ax.bar(names, [getattr(metrics, n) for n in names])
        ax.set_title("Difference Metrics")
    elif kind == "silence_distance":
        for key, values in payload.items():
            if key != "times":
                ax.plot(payload["times"], values, label=key)
        ax.legend()
        ax.set_title("Distance from Silence")
    elif kind == "band_amplitude":
        for band_name, data in payload.items():
            db = band_amplitude_to_db(data)
            ax.plot(db["times"], db["channel_0"], label=band_name)
        ax.legend(fontsize=7)
        ax.set_title("Band Amplitude")
    elif kind == "silence_regions":
        wav = payload["waveform"]
        regions = payload["regions"]
        if wav is not None:
            t = np.arange(wav.num_samples) / wav.sample_rate
            ax.plot(t, wav.channel, color="#333", linewidth=0.8)
            for region in regions.get(0, []):
                ax.axvspan(
                    region.start_sample / wav.sample_rate,
                    region.end_sample / wav.sample_rate,
                    alpha=0.3,
                    color="#c44e52",
                )
        ax.set_title("Silence Regions")
    ax.grid(True, alpha=0.3)
