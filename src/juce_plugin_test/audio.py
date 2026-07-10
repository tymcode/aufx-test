"""Core audio types and file I/O."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Self

import numpy as np
import soundfile as sf


@dataclass(frozen=True)
class Waveform:
    """Multichannel audio buffer with sample rate metadata.

    Data layout: shape ``(num_samples, num_channels)``. Mono signals use
    ``num_channels == 1``.
    """

    data: np.ndarray
    sample_rate: int

    def __post_init__(self) -> None:
        arr = np.asarray(self.data, dtype=np.float64)
        if arr.ndim == 1:
            arr = arr[:, np.newaxis]
        elif arr.ndim != 2:
            raise ValueError(f"Expected 1D or 2D array, got shape {arr.shape}")
        object.__setattr__(self, "data", arr)

    @property
    def num_samples(self) -> int:
        return self.data.shape[0]

    @property
    def num_channels(self) -> int:
        return self.data.shape[1]

    @property
    def duration_seconds(self) -> float:
        return self.num_samples / self.sample_rate

    @property
    def channel(self) -> np.ndarray:
        """First channel as 1D array (convenience for mono tests)."""
        return self.data[:, 0]

    def channel_data(self, index: int) -> np.ndarray:
        if not 0 <= index < self.num_channels:
            raise IndexError(f"Channel {index} out of range (0..{self.num_channels - 1})")
        return self.data[:, index]

    def with_data(self, data: np.ndarray) -> Self:
        return type(self)(data=data, sample_rate=self.sample_rate)

    def resampled_to(self, target_rate: int) -> Self:
        if target_rate == self.sample_rate:
            return self
        from scipy import signal as sp_signal

        ratio = target_rate / self.sample_rate
        new_len = int(round(self.num_samples * ratio))
        resampled = sp_signal.resample(self.data, new_len, axis=0)
        return type(self)(data=resampled, sample_rate=target_rate)

    def aligned_to(self, other: Waveform) -> tuple[Self, Waveform]:
        """Trim both waveforms to the shorter length and matching sample rate."""
        if self.sample_rate != other.sample_rate:
            other = other.resampled_to(self.sample_rate)
        length = min(self.num_samples, other.num_samples)
        return self.with_data(self.data[:length]), other.with_data(other.data[:length])

    @classmethod
    def from_file(cls, path: str | Path) -> Self:
        data, sample_rate = sf.read(str(path), always_2d=True)
        return cls(data=data, sample_rate=int(sample_rate))

    def to_file(self, path: str | Path) -> None:
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        sf.write(str(path), self.data, self.sample_rate)

    @classmethod
    def silence(cls, duration_seconds: float, sample_rate: int = 48000, channels: int = 2) -> Self:
        n = int(duration_seconds * sample_rate)
        return cls(data=np.zeros((n, channels)), sample_rate=sample_rate)

    @classmethod
    def sine(
        cls,
        frequency_hz: float,
        duration_seconds: float,
        amplitude: float = 0.5,
        sample_rate: int = 48000,
        channels: int = 1,
        phase_rad: float = 0.0,
    ) -> Self:
        n = int(duration_seconds * sample_rate)
        t = np.arange(n) / sample_rate
        mono = amplitude * np.sin(2 * np.pi * frequency_hz * t + phase_rad)
        data = np.tile(mono[:, np.newaxis], (1, channels))
        return cls(data=data, sample_rate=sample_rate)
