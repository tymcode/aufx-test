"""Tests for graphing (smoke tests — no display)."""


import pytest

from aufx_test import (
    band_amplitude_over_time,
    compare_waveforms,
    distance_from_silence,
    measure_silence_regions,
)
from aufx_test.graphing import (
    plot_band_amplitude,
    plot_comparison,
    plot_difference_metrics,
    plot_distance_from_silence,
    plot_silence_regions,
    plot_test_results,
    plot_waveforms,
)


@pytest.fixture
def output_dir(tmp_path):
    return tmp_path / "plots"


def test_plot_waveforms(sine_mono, output_dir):
    fig = plot_waveforms(sine_mono, labels=["sine"], save_path=output_dir / "wave.png", show=False)
    assert (output_dir / "wave.png").exists()
    assert fig is not None


def test_plot_comparison(sine_mono, output_dir):
    shifted = sine_mono.with_data(sine_mono.data * 0.95)
    plot_comparison(sine_mono, shifted, sine_mono, save_path=output_dir / "cmp.png", show=False)
    assert (output_dir / "cmp.png").exists()


def test_plot_comparison_with_spectrogram_background(sine_mono, output_dir):
    shifted = sine_mono.with_data(sine_mono.data * 0.85)
    plot_comparison(
        sine_mono,
        shifted,
        save_path=output_dir / "cmp_spec_bg.png",
        spectrogram_background=shifted,
    )
    assert (output_dir / "cmp_spec_bg.png").exists()


def test_plot_comparison_unequal_lengths_share_time_axis(output_dir, sample_rate):
    """Shorter clip must not be stretched to the longer clip's duration."""
    import numpy as np
    from aufx_test.audio import Waveform
    import aufx_test.graphing as graphing

    saved = []

    def _keep(fig, save_path, show, *, dpi=150):
        if save_path is not None:
            from pathlib import Path

            path = Path(save_path)
            path.parent.mkdir(parents=True, exist_ok=True)
            fig.savefig(path, dpi=dpi, bbox_inches="tight")
        saved.append(fig)

    graphing._save_or_show = _keep

    short = Waveform(
        data=(0.5 * np.sin(2 * np.pi * 440 * np.arange(sample_rate) / sample_rate))[:, None],
        sample_rate=sample_rate,
    )
    long = Waveform(
        data=(0.5 * np.sin(2 * np.pi * 220 * np.arange(sample_rate * 3) / sample_rate))[:, None],
        sample_rate=sample_rate,
    )

    graphing.plot_comparison(
        short,
        long,
        labels=["short", "long"],
        spectrogram_background=long,
        save_path=output_dir / "unequal.png",
        show=False,
    )
    ax = saved[-1].axes[0]
    xlim = ax.get_xlim()
    assert xlim[1] == pytest.approx(long.duration_seconds, rel=0.02)

    # Envelope / waveform overlays live on the twin axis when spectrogram is on.
    wave_ax = saved[-1].axes[1] if len(saved[-1].axes) > 1 else ax
    ends = sorted(float(np.asarray(line.get_xdata())[-1]) for line in wave_ax.lines)
    assert ends[0] == pytest.approx(short.duration_seconds, rel=0.05)
    assert ends[1] == pytest.approx(long.duration_seconds, rel=0.05)
    assert ends[0] < xlim[1] * 0.5


def test_plot_difference_metrics(sine_mono, output_dir):
    from aufx_test.comparison import ComparisonThresholds

    result = compare_waveforms(sine_mono, sine_mono)
    thresholds = ComparisonThresholds()
    fig = plot_difference_metrics(
        result,
        thresholds=thresholds,
        save_path=output_dir / "metrics.png",
        show=False,
    )
    assert (output_dir / "metrics.png").exists()
    ax = fig.axes[0]
    hline_ys: list[float] = []
    for coll in ax.collections:
        get_segments = getattr(coll, "get_segments", None)
        if get_segments is None:
            continue
        for seg in get_segments():
            # Each hline segment is [[x0, y], [x1, y]].
            hline_ys.append(float(seg[0][1]))
    assert thresholds.correlation_min in hline_ys
    assert thresholds.rms_error_max in hline_ys
    assert thresholds.spectral_distance_max in hline_ys
    assert len(hline_ys) == 3


def test_plot_distance_from_silence(sine_stereo, output_dir):
    data = distance_from_silence(sine_stereo, window_samples=1024, hop_samples=512)
    plot_distance_from_silence(data, save_path=output_dir / "silence_dist.png", show=False)
    assert (output_dir / "silence_dist.png").exists()


def test_plot_band_amplitude(sine_mono, output_dir):
    bands = band_amplitude_over_time(sine_mono, window_samples=2048, hop_samples=1024)
    plot_band_amplitude(bands, save_path=output_dir / "bands.png", show=False)
    assert (output_dir / "bands.png").exists()


def test_plot_silence_regions(stereo_with_silence, output_dir):
    regions = measure_silence_regions(stereo_with_silence, min_duration_samples=100)
    plot_silence_regions(
        stereo_with_silence,
        regions,
        save_path=output_dir / "regions.png",
        show=False,
    )
    assert (output_dir / "regions.png").exists()


def test_plot_test_results_combined(sine_mono, sine_stereo, output_dir):
    comparison = compare_waveforms(sine_mono, sine_mono)
    silence = distance_from_silence(sine_stereo, window_samples=1024, hop_samples=512)
    bands = band_amplitude_over_time(sine_mono, window_samples=2048, hop_samples=1024)
    regions = measure_silence_regions(sine_stereo, min_duration_samples=100)
    plot_test_results(
        {
            "waveforms": ([sine_mono, sine_mono], ["a", "b"]),
            "comparison": comparison,
            "silence_distance": silence,
            "band_amplitude": bands,
            "silence_regions": regions,
            "waveform_for_regions": sine_stereo,
        },
        save_path=output_dir / "combined.png",
        show=False,
    )
    assert (output_dir / "combined.png").exists()
