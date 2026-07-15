"""Shared pytest fixtures."""

from pathlib import Path

import pytest

from juce_plugin_test import Waveform
from juce_plugin_test.host import PassthroughHost


@pytest.fixture
def sample_rate() -> int:
    return 48000


@pytest.fixture
def sine_mono(sample_rate: int) -> Waveform:
    return Waveform.sine(440.0, duration_seconds=0.5, sample_rate=sample_rate, channels=1)


@pytest.fixture
def sine_stereo(sample_rate: int) -> Waveform:
    return Waveform.sine(440.0, duration_seconds=0.5, sample_rate=sample_rate, channels=2)


@pytest.fixture
def stereo_with_silence(sample_rate: int) -> Waveform:
    """Stereo signal: ch0 has silence in the middle, ch1 is continuous."""
    duration = 1.0
    n = int(duration * sample_rate)
    data = Waveform.sine(220.0, duration, sample_rate=sample_rate, channels=2).data.copy()
    mid_start = n // 3
    mid_end = 2 * n // 3
    data[mid_start:mid_end, 0] = 0.0
    return Waveform(data=data, sample_rate=sample_rate)


@pytest.fixture
def passthrough_host() -> PassthroughHost:
    return PassthroughHost()


@pytest.fixture
def sample_aupreset(tmp_path) -> Path:
    import plistlib

    preset = tmp_path / "test.aupreset"
    plistlib.dump(
        {
            "name": "Test Preset",
            "manufacturer": 1146379079,
            "subtype": 1234567890,
            "type": 1635083896,
            "data": b"\x00\x01\x02\x03plugin-state-bytes",
        },
        preset.open("wb"),
    )
    return preset
