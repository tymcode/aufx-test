"""Objective waveform difference measurements."""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np
from scipy import signal as sp_signal
from scipy.fft import rfft, rfftfreq

from .audio import Waveform


@dataclass(frozen=True)
class DifferenceMetrics:
    """Repeatable objective measurements between two waveforms."""

    snr_db: float
    correlation: float
    rms_error: float
    max_abs_error: float
    spectral_distance: float
    alignment_lag_samples: int = 0
    channel_metrics: dict[int, dict[str, float]] = field(default_factory=dict)

    def as_dict(self) -> dict[str, float | dict]:
        return {
            "snr_db": self.snr_db,
            "correlation": self.correlation,
            "rms_error": self.rms_error,
            "max_abs_error": self.max_abs_error,
            "spectral_distance": self.spectral_distance,
            "alignment_lag_samples": self.alignment_lag_samples,
            "channel_metrics": self.channel_metrics,
        }


@dataclass(frozen=True)
class ComparisonResult:
    """Result of comparing actual output to an expected reference."""

    passed: bool
    metrics: DifferenceMetrics
    failures: tuple[str, ...] = ()

    def summary(self) -> str:
        lines = [
            f"passed={self.passed}",
            f"  snr_db={self.metrics.snr_db:.2f}",
            f"  correlation={self.metrics.correlation:.4f}",
            f"  rms_error={self.metrics.rms_error:.6f}",
            f"  spectral_distance={self.metrics.spectral_distance:.4f}",
            f"  alignment_lag_samples={self.metrics.alignment_lag_samples}",
        ]
        if self.failures:
            lines.append("  failures:")
            lines.extend(f"    - {f}" for f in self.failures)
        return "\n".join(lines)


@dataclass(frozen=True)
class ComparisonThresholds:
    """Minimum quality gates for automated tests."""

    snr_db_min: float = 30.0
    correlation_min: float = 0.95
    rms_error_max: float = 0.05
    spectral_distance_max: float = 0.15


def compute_difference_metrics(
    actual: Waveform,
    expected: Waveform,
    *,
    allow_extra_actual_tail: bool = False,
    max_alignment_samples: int = 0,
) -> DifferenceMetrics:
    """Compute repeatable objective difference metrics between two waveforms.

    When ``allow_extra_actual_tail`` is true and ``actual`` is longer than
    ``expected``, only the overlapping prefix is scored. This avoids treating
    truncated golden reverb tails as failures when the live render is longer.
    """
    actual, expected, alignment_lag = align_waveforms(
        actual,
        expected,
        max_lag_samples=max_alignment_samples,
    )
    if allow_extra_actual_tail:
        if actual.num_samples > expected.num_samples:
            actual = actual.with_data(actual.data[: expected.num_samples])
        actual_a, expected_a = actual.padded_to_match(expected)
    else:
        actual_a, expected_a = actual.padded_to_match(expected)
    channels = max(actual_a.num_channels, expected_a.num_channels)
    a_data = _pad_channels(actual_a.data, channels)
    e_data = _pad_channels(expected_a.data, channels)

    channel_metrics: dict[int, dict[str, float]] = {}
    snrs: list[float] = []
    corrs: list[float] = []
    rmses: list[float] = []
    maxes: list[float] = []
    spec_dists: list[float] = []

    for ch in range(channels):
        a_ch = a_data[:, ch]
        e_ch = e_data[:, ch]
        cm = _channel_metrics(a_ch, e_ch, actual_a.sample_rate)
        channel_metrics[ch] = cm
        snrs.append(cm["snr_db"])
        corrs.append(cm["correlation"])
        rmses.append(cm["rms_error"])
        maxes.append(cm["max_abs_error"])
        spec_dists.append(cm["spectral_distance"])

    return DifferenceMetrics(
        snr_db=float(np.mean(snrs)),
        correlation=float(np.mean(corrs)),
        rms_error=float(np.mean(rmses)),
        max_abs_error=float(np.max(maxes)),
        spectral_distance=float(np.mean(spec_dists)),
        alignment_lag_samples=alignment_lag,
        channel_metrics=channel_metrics,
    )


def compare_waveforms(
    actual: Waveform,
    expected: Waveform,
    *,
    thresholds: ComparisonThresholds | None = None,
    snr_db_min: float | None = None,
    correlation_min: float | None = None,
    rms_error_max: float | None = None,
    spectral_distance_max: float | None = None,
    allow_extra_actual_tail: bool = False,
    max_alignment_samples: int = 0,
) -> ComparisonResult:
    """Compare actual output to expected reference against quality thresholds.

    When ``thresholds`` is omitted, defaults come from ``compare.config.json``.

    Set ``allow_extra_actual_tail=True`` to ignore trailing samples beyond the
    reference length (useful when imported goldens have truncated tails).

    Set ``max_alignment_samples`` to compensate for a small fixed plugin
    latency before scoring. Positive lag means the actual signal was delayed.
    """
    if thresholds is None:
        from .compare_config import cached_compare_config

        t = cached_compare_config().thresholds
    else:
        t = thresholds
    if snr_db_min is not None:
        t = ComparisonThresholds(
            snr_db_min=snr_db_min,
            correlation_min=t.correlation_min,
            rms_error_max=t.rms_error_max,
            spectral_distance_max=t.spectral_distance_max,
        )
    if correlation_min is not None:
        t = ComparisonThresholds(
            snr_db_min=t.snr_db_min,
            correlation_min=correlation_min,
            rms_error_max=t.rms_error_max,
            spectral_distance_max=t.spectral_distance_max,
        )
    if rms_error_max is not None:
        t = ComparisonThresholds(
            snr_db_min=t.snr_db_min,
            correlation_min=t.correlation_min,
            rms_error_max=rms_error_max,
            spectral_distance_max=t.spectral_distance_max,
        )
    if spectral_distance_max is not None:
        t = ComparisonThresholds(
            snr_db_min=t.snr_db_min,
            correlation_min=t.correlation_min,
            rms_error_max=t.rms_error_max,
            spectral_distance_max=spectral_distance_max,
        )

    metrics = compute_difference_metrics(
        actual,
        expected,
        allow_extra_actual_tail=allow_extra_actual_tail,
        max_alignment_samples=max_alignment_samples,
    )
    failures: list[str] = []
    if metrics.snr_db < t.snr_db_min:
        failures.append(f"SNR {metrics.snr_db:.2f} dB < {t.snr_db_min:.2f} dB")
    if metrics.correlation < t.correlation_min:
        failures.append(f"correlation {metrics.correlation:.4f} < {t.correlation_min:.4f}")
    if metrics.rms_error > t.rms_error_max:
        failures.append(f"RMS error {metrics.rms_error:.6f} > {t.rms_error_max:.6f}")
    if metrics.spectral_distance > t.spectral_distance_max:
        failures.append(
            f"spectral distance {metrics.spectral_distance:.4f} > {t.spectral_distance_max:.4f}"
        )

    return ComparisonResult(
        passed=len(failures) == 0,
        metrics=metrics,
        failures=tuple(failures),
    )


def align_waveforms(
    actual: Waveform,
    expected: Waveform,
    *,
    max_lag_samples: int,
) -> tuple[Waveform, Waveform, int]:
    """Resample and align waveforms within a bounded integer-sample lag.

    Positive lag means ``actual`` starts later than ``expected``. The
    non-overlapping leading/trailing samples introduced by the lag are removed.
    """
    if max_lag_samples < 0:
        raise ValueError("max_lag_samples must be non-negative")
    if actual.sample_rate != expected.sample_rate:
        actual = actual.resampled_to(expected.sample_rate)
    if max_lag_samples == 0 or actual.num_samples == 0 or expected.num_samples == 0:
        return actual, expected, 0

    channels = max(actual.num_channels, expected.num_channels)
    a = np.mean(_pad_channels(actual.data, channels), axis=1)
    e = np.mean(_pad_channels(expected.data, channels), axis=1)

    # Two seconds is enough to identify fixed host/plugin latency while keeping
    # alignment inexpensive for long reverb renders.
    analysis_samples = min(len(a), len(e), actual.sample_rate * 2)
    a = a[:analysis_samples] - np.mean(a[:analysis_samples])
    e = e[:analysis_samples] - np.mean(e[:analysis_samples])
    if np.linalg.norm(a) == 0 or np.linalg.norm(e) == 0:
        return actual, expected, 0

    correlation = sp_signal.correlate(a, e, mode="full", method="fft")
    lags = sp_signal.correlation_lags(len(a), len(e), mode="full")
    allowed = np.abs(lags) <= max_lag_samples
    lag = int(lags[allowed][np.argmax(correlation[allowed])])

    if lag > 0:
        actual = actual.with_data(actual.data[lag:])
        expected = expected.with_data(expected.data[: expected.num_samples - lag])
    elif lag < 0:
        offset = -lag
        actual = actual.with_data(actual.data[: actual.num_samples - offset])
        expected = expected.with_data(expected.data[offset:])
    return actual, expected, lag


def difference_signal(actual: Waveform, expected: Waveform) -> Waveform:
    """Return the sample-wise difference (actual - expected)."""
    actual_a, expected_a = actual.padded_to_match(expected)
    channels = max(actual_a.num_channels, expected_a.num_channels)
    diff = _pad_channels(actual_a.data, channels) - _pad_channels(expected_a.data, channels)
    return Waveform(data=diff, sample_rate=actual_a.sample_rate)


def _channel_metrics(a: np.ndarray, e: np.ndarray, sample_rate: int) -> dict[str, float]:
    error = a - e
    signal_power = float(np.mean(e**2))
    noise_power = float(np.mean(error**2))
    snr_db = 10.0 * np.log10(signal_power / noise_power) if noise_power > 0 else float("inf")

    corr_matrix = np.corrcoef(a, e)
    correlation = float(corr_matrix[0, 1]) if not np.isnan(corr_matrix[0, 1]) else 0.0

    return {
        "snr_db": snr_db,
        "correlation": correlation,
        "rms_error": float(np.sqrt(np.mean(error**2))),
        "max_abs_error": float(np.max(np.abs(error))),
        "spectral_distance": _spectral_distance(a, e, sample_rate),
    }


def _spectral_distance(a: np.ndarray, e: np.ndarray, sample_rate: int) -> float:
    """Normalized L2 distance between magnitude spectra (0 = identical)."""
    n = min(len(a), len(e))
    window = sp_signal.windows.hann(n)
    spec_a = np.abs(rfft(a[:n] * window))
    spec_e = np.abs(rfft(e[:n] * window))
    norm_a = np.linalg.norm(spec_a)
    norm_e = np.linalg.norm(spec_e)
    if norm_a == 0 and norm_e == 0:
        return 0.0
    if norm_a == 0 or norm_e == 0:
        return 1.0
    spec_a /= norm_a
    spec_e /= norm_e
    return float(np.linalg.norm(spec_a - spec_e) / np.sqrt(len(spec_a)))


def _pad_channels(data: np.ndarray, target_channels: int) -> np.ndarray:
    if data.shape[1] >= target_channels:
        return data[:, :target_channels]
    if data.shape[1] == 1:
        return np.tile(data, (1, target_channels))
    pad = np.zeros((data.shape[0], target_channels - data.shape[1]))
    return np.hstack([data, pad])
