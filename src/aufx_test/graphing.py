"""Matplotlib visualization for test results."""

from __future__ import annotations

from pathlib import Path
from typing import Any

import matplotlib.pyplot as plt
import numpy as np

from .audio import Waveform
from .comparison import ComparisonResult, DifferenceMetrics
from .silence import SilenceRegion
from .spectrum import band_amplitude_to_db


def _save_or_show(fig: plt.Figure, save_path: str | Path | None, show: bool) -> None:
    if save_path is not None:
        path = Path(save_path)
        path.parent.mkdir(parents=True, exist_ok=True)
        fig.savefig(path, dpi=150, bbox_inches="tight")
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
    spectrogram_background: Waveform | None = None,
    spectrogram_channel: int = 0,
    spectrogram_alpha: float = 0.5,
    title: str = "Before / After / Reference",
    save_path: str | Path | None = None,
    show: bool = False,
) -> plt.Figure:
    """Plot before/after/reference waveforms, optionally over a wet spectrogram."""
    labels = ["before", "after"]
    waves: list[Waveform] = [before, after]
    colors = ["#e6edf3", "#58a6ff", "#ff7b72"] if spectrogram_background else [None, None, None]
    if reference is not None:
        waves.append(reference)
        labels.append("reference")

    fig, ax = plt.subplots(figsize=(12, 4))
    wave_ax = ax
    if spectrogram_background is not None:
        bg = spectrogram_background.channel_data(spectrogram_channel)
        nfft = 1024 if spectrogram_background.num_samples >= 1024 else 256
        nfft = max(64, nfft)
        noverlap = int(nfft * 0.75)
        ax.specgram(
            bg,
            NFFT=nfft,
            Fs=spectrogram_background.sample_rate,
            noverlap=noverlap,
            cmap="magma",
            alpha=max(0.0, min(1.0, spectrogram_alpha)),
        )
        ax.set_ylabel("Frequency (Hz)")
        # Overlay waveform on its own amplitude axis so it remains visible.
        wave_ax = ax.twinx()
        wave_ax.set_ylabel("Amplitude")
        wave_ax.patch.set_alpha(0.0)
        wave_ax.set_zorder(ax.get_zorder() + 1)
        ax.grid(False)

    for i, (wav, label) in enumerate(zip(waves, labels, strict=True)):
        t = np.arange(wav.num_samples) / wav.sample_rate
        line_kwargs: dict[str, Any] = {"label": label, "alpha": 0.9}
        if spectrogram_background:
            line_kwargs["color"] = colors[i]
            line_kwargs["linewidth"] = 1.5
        wave_ax.plot(t, wav.channel_data(channel), **line_kwargs)

    ax.set_xlabel("Time (s)")
    if spectrogram_background is None:
        ax.set_ylabel("Amplitude")
    ax.set_title(title)
    wave_ax.legend(loc="upper right")
    wave_ax.grid(True, alpha=0.25)
    fig.tight_layout()
    _save_or_show(fig, save_path, show)
    return fig


def plot_difference_metrics(
    metrics: DifferenceMetrics | ComparisonResult,
    *,
    title: str = "Difference Metrics",
    save_path: str | Path | None = None,
    show: bool = False,
) -> plt.Figure:
    """Bar chart of objective difference metrics."""
    m = metrics.metrics if isinstance(metrics, ComparisonResult) else metrics
    names = ["snr_db", "correlation", "rms_error", "max_abs_error", "spectral_distance"]
    values = [getattr(m, name) for name in names]

    fig, ax = plt.subplots(figsize=(8, 4))
    bars = ax.bar(names, values, color=["#4c72b0", "#55a868", "#c44e52", "#8172b2", "#ccb974"])
    ax.set_title(title)
    ax.set_ylabel("Value")
    for bar, val in zip(bars, values, strict=True):
        ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height(), f"{val:.3f}", ha="center", va="bottom")
    fig.tight_layout()
    _save_or_show(fig, save_path, show)
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
