"""Tests for Waveform type."""

from juce_plugin_test import Waveform


def test_sine_shape(sine_mono):
    assert sine_mono.num_channels == 1
    assert sine_mono.num_samples == 24000
    assert abs(sine_mono.channel.max()) <= 0.5


def test_stereo_sine(sine_stereo):
    assert sine_stereo.num_channels == 2
    assert sine_stereo.data.shape == (24000, 2)


def test_aligned_to_truncates(sine_mono, sample_rate):
    short = Waveform.sine(440.0, 0.25, sample_rate=sample_rate)
    a, b = sine_mono.aligned_to(short)
    assert a.num_samples == b.num_samples == short.num_samples


def test_silence_factory(sample_rate):
    s = Waveform.silence(0.1, sample_rate, channels=2)
    assert s.num_samples == int(0.1 * sample_rate)
    assert (s.data == 0).all()
