"""Silence detection and distance-from-silence analysis."""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from .audio import Waveform


@dataclass(frozen=True)
class SilenceRegion:
    """A contiguous silent region within one channel."""

    channel: int
    start_sample: int
    end_sample: int  # exclusive

    @property
    def length_samples(self) -> int:
        return self.end_sample - self.start_sample

    def duration_seconds(self, sample_rate: int) -> float:
        return self.length_samples / sample_rate


def amplitude_to_db(amplitude: np.ndarray, floor_db: float = -120.0) -> np.ndarray:
    """Convert linear amplitude to decibels with a noise floor."""
    safe = np.maximum(np.abs(amplitude), 10 ** (floor_db / 20))
    return 20.0 * np.log10(safe)


def distance_from_silence(
    waveform: Waveform,
    *,
    threshold_db: float = -60.0,
    window_samples: int = 2048,
    hop_samples: int | None = None,
    metric: str = "rms",
) -> dict[str, np.ndarray]:
    """Measure how far each channel is from silence over time.

    Returns a dict with keys ``times`` (seconds) and ``channel_N`` (dB above
    the silence threshold for each channel).
    """
    hop = hop_samples or window_samples // 2
    threshold_linear = 10 ** (threshold_db / 20)
    times: list[float] = []
    per_channel: list[list[float]] = [[] for _ in range(waveform.num_channels)]

    for start in range(0, waveform.num_samples - window_samples + 1, hop):
        end = start + window_samples
        times.append((start + window_samples / 2) / waveform.sample_rate)
        for ch in range(waveform.num_channels):
            window = waveform.data[start:end, ch]
            amp = _window_amplitude(window, metric)
            distance_db = 20.0 * np.log10(max(amp, 1e-12) / threshold_linear)
            per_channel[ch].append(distance_db)

    result: dict[str, np.ndarray] = {"times": np.array(times)}
    for ch, values in enumerate(per_channel):
        result[f"channel_{ch}"] = np.array(values)
    return result


def measure_silence_regions(
    waveform: Waveform,
    *,
    threshold_db: float = -60.0,
    min_duration_samples: int = 1,
) -> dict[int, list[SilenceRegion]]:
    """Find silent regions per channel independently.

    Returns ``{channel_index: [SilenceRegion, ...]}``.
    """
    threshold_linear = 10 ** (threshold_db / 20)
    regions: dict[int, list[SilenceRegion]] = {}

    for ch in range(waveform.num_channels):
        channel = waveform.data[:, ch]
        silent = np.abs(channel) < threshold_linear
        regions[ch] = _regions_from_mask(silent, ch, min_duration_samples)

    return regions


def leading_silence_samples(
    waveform: Waveform,
    *,
    threshold_db: float = -60.0,
) -> int:
    """Return how many leading samples sit below ``threshold_db`` (peak, any channel)."""
    if waveform.num_samples == 0:
        return 0
    threshold_linear = 10 ** (threshold_db / 20)
    amp = np.max(np.abs(waveform.data), axis=1)
    above = np.where(amp >= threshold_linear)[0]
    if len(above) == 0:
        return waveform.num_samples
    return int(above[0])


def trim_leading_silence(
    waveform: Waveform,
    *,
    threshold_db: float = -60.0,
) -> Waveform:
    """Drop samples before the first peak at or above ``threshold_db``.

    Fully silent waveforms are returned unchanged so callers never get an
    empty buffer. Matches the −60 dB onset trim used for deepz goldens.
    """
    start = leading_silence_samples(waveform, threshold_db=threshold_db)
    if start <= 0 or start >= waveform.num_samples:
        return waveform
    return waveform.with_data(waveform.data[start:])


def _window_amplitude(window: np.ndarray, metric: str) -> float:
    if metric == "rms":
        return float(np.sqrt(np.mean(window**2)))
    if metric == "peak":
        return float(np.max(np.abs(window)))
    raise ValueError(f"Unknown metric: {metric!r}. Use 'rms' or 'peak'.")


def _regions_from_mask(
    silent: np.ndarray,
    channel: int,
    min_duration: int,
) -> list[SilenceRegion]:
    regions: list[SilenceRegion] = []
    in_region = False
    start = 0
    for i, is_silent in enumerate(silent):
        if is_silent and not in_region:
            start = i
            in_region = True
        elif not is_silent and in_region:
            if i - start >= min_duration:
                regions.append(SilenceRegion(channel=channel, start_sample=start, end_sample=i))
            in_region = False
    if in_region and len(silent) - start >= min_duration:
        regions.append(
            SilenceRegion(channel=channel, start_sample=start, end_sample=len(silent))
        )
    return regions
