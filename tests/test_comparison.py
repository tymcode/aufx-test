"""Tests for waveform comparison."""

import numpy as np

from aufx_test import Waveform, compare_waveforms, compute_difference_metrics, difference_signal


def test_identical_waveforms_pass(sine_mono):
    result = compare_waveforms(sine_mono, sine_mono)
    assert result.passed
    assert result.metrics.snr_db == float("inf") or result.metrics.snr_db > 100
    assert result.metrics.correlation > 0.999


def test_slightly_different_still_passes(sine_mono, sample_rate):
    noisy = sine_mono.with_data(sine_mono.data + np.random.default_rng(42).normal(0, 0.001, sine_mono.data.shape))
    result = compare_waveforms(noisy, sine_mono, snr_db_min=20.0, correlation_min=0.9)
    assert result.passed


def test_large_difference_fails(sine_mono, sample_rate):
    different = Waveform.sine(880.0, 0.5, sample_rate=sample_rate)
    result = compare_waveforms(different, sine_mono)
    assert not result.passed
    assert len(result.failures) > 0


def test_extra_actual_tail_ignored_when_enabled(sine_mono, sample_rate):
    """Longer actual renders should still pass when the shared prefix matches."""
    # Non-silent tail: without trimming, padded reference silence vs live tail hurts metrics.
    tail = Waveform.sine(220.0, 0.25, sample_rate=sample_rate, channels=sine_mono.num_channels).data
    longer = sine_mono.with_data(np.vstack([sine_mono.data, tail]))
    padded = compare_waveforms(longer, sine_mono)
    trimmed = compare_waveforms(longer, sine_mono, allow_extra_actual_tail=True)
    assert trimmed.passed
    assert trimmed.metrics.correlation > 0.999
    assert padded.metrics.correlation < trimmed.metrics.correlation
    assert not padded.passed or padded.metrics.rms_error > trimmed.metrics.rms_error


def test_small_actual_latency_is_aligned(sample_rate):
    rng = np.random.default_rng(7)
    data = rng.normal(0.0, 0.1, (sample_rate // 2, 1))
    expected = Waveform(data=data, sample_rate=sample_rate)
    delayed = Waveform(
        data=np.vstack([np.zeros((3, 1)), data[:-3]]),
        sample_rate=sample_rate,
    )

    strict = compare_waveforms(delayed, expected)
    aligned = compare_waveforms(delayed, expected, max_alignment_samples=64)

    assert not strict.passed
    assert aligned.passed
    assert aligned.metrics.alignment_lag_samples == 3
    assert aligned.metrics.correlation > 0.999


def test_difference_signal(sine_mono):
    shifted = sine_mono.with_data(sine_mono.data * 0.9)
    diff = difference_signal(shifted, sine_mono)
    assert diff.data.shape == sine_mono.data.shape
    assert np.allclose(diff.data, sine_mono.data * -0.1, atol=1e-10)


def test_metrics_are_repeatable(sine_mono):
    m1 = compute_difference_metrics(sine_mono, sine_mono)
    m2 = compute_difference_metrics(sine_mono, sine_mono)
    assert m1.as_dict() == m2.as_dict()
