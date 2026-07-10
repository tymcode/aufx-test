"""Tests for signal manipulation."""

import numpy as np

from juce_plugin_test import Waveform, invert_phase, sum_signals


def test_invert_phase_single_channel(sine_stereo):
    inverted = invert_phase(sine_stereo, channels=0)
    assert np.allclose(inverted.data[:, 0], -sine_stereo.data[:, 0])
    assert np.allclose(inverted.data[:, 1], sine_stereo.data[:, 1])


def test_invert_phase_all_channels(sine_stereo):
    inverted = invert_phase(sine_stereo)
    assert np.allclose(inverted.data, -sine_stereo.data)


def test_sum_signals_identical(sine_mono):
    doubled = sum_signals(sine_mono, sine_mono, gains=(1.0, 1.0))
    assert np.allclose(doubled.data, 2.0 * sine_mono.data)


def test_sum_signals_phase_cancellation(sine_mono):
    inverted = invert_phase(sine_mono)
    cancelled = sum_signals(sine_mono, inverted, gains=(1.0, 1.0))
    assert np.max(np.abs(cancelled.data)) < 1e-10


def test_sum_signals_different_lengths(sample_rate):
    short = Waveform.sine(440.0, 0.25, sample_rate=sample_rate)
    long = Waveform.sine(440.0, 0.5, sample_rate=sample_rate)
    summed = sum_signals(short, long)
    assert summed.num_samples == short.num_samples
