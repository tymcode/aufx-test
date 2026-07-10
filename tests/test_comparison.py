"""Tests for waveform comparison."""

import numpy as np

from juce_plugin_test import Waveform, compare_waveforms, compute_difference_metrics, difference_signal


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


def test_difference_signal(sine_mono):
    shifted = sine_mono.with_data(sine_mono.data * 0.9)
    diff = difference_signal(shifted, sine_mono)
    assert diff.data.shape == sine_mono.data.shape
    assert np.allclose(diff.data, sine_mono.data * -0.1, atol=1e-10)


def test_metrics_are_repeatable(sine_mono):
    m1 = compute_difference_metrics(sine_mono, sine_mono)
    m2 = compute_difference_metrics(sine_mono, sine_mono)
    assert m1.as_dict() == m2.as_dict()
