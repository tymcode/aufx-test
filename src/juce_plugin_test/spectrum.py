"""Frequency-band amplitude analysis over time."""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from scipy import signal as sp_signal

from .audio import Waveform
from .compare_config import CompareConfig, cached_compare_config


@dataclass(frozen=True)
class FrequencyBand:
    """Half-open frequency range ``[low_hz, high_hz)``."""

    name: str
    low_hz: float
    high_hz: float

    def __post_init__(self) -> None:
        if self.low_hz < 0 or self.high_hz <= self.low_hz:
            raise ValueError(f"Invalid band {self.name}: [{self.low_hz}, {self.high_hz})")


# Classic musically motivated catalog used when num_of_bands == 7.
BAND_CATALOG = (
    FrequencyBand("sub", 20, 60),
    FrequencyBand("bass", 60, 250),
    FrequencyBand("low_mid", 250, 500),
    FrequencyBand("mid", 500, 2000),
    FrequencyBand("high_mid", 2000, 6000),
    FrequencyBand("presence", 6000, 12000),
    FrequencyBand("air", 12000, 20000),
)


def log_spaced_bands(
    count: int,
    *,
    low_hz: float = 20.0,
    high_hz: float = 20000.0,
) -> tuple[FrequencyBand, ...]:
    """Build ``count`` contiguous log-spaced bands between ``low_hz`` and ``high_hz``."""
    if count < 1:
        raise ValueError(f"count must be >= 1, got {count}")

    edges = np.geomspace(low_hz, high_hz, count + 1)
    bands: list[FrequencyBand] = []
    for i in range(count):
        lo = float(edges[i])
        hi = float(edges[i + 1])
        bands.append(FrequencyBand(f"band_{i + 1}", lo, hi))
    return tuple(bands)


def analysis_bands(
    num_bands: int | None = None,
    *,
    config: CompareConfig | None = None,
) -> tuple[FrequencyBand, ...]:
    """Bands used for compare/analysis, sized from ``compare.config.json`` by default.

    When ``num_of_bands`` is 7, the classic named catalog is used. Other counts
    use contiguous log-spaced bands between ``band_low_hz`` and ``band_high_hz``.
    """
    cfg = config if config is not None else cached_compare_config()
    count = cfg.num_of_bands if num_bands is None else num_bands
    if count < 1:
        raise ValueError(f"num_bands must be >= 1, got {count}")
    if count == len(BAND_CATALOG):
        return BAND_CATALOG
    return log_spaced_bands(count, low_hz=cfg.band_low_hz, high_hz=cfg.band_high_hz)


# Backward-compatible alias: default analysis set from compare.config.json.
DEFAULT_BANDS = analysis_bands()


def band_amplitude_over_time(
    waveform: Waveform,
    bands: tuple[FrequencyBand, ...] | None = None,
    *,
    window_samples: int | None = None,
    hop_samples: int | None = None,
    metric: str = "rms",
    config: CompareConfig | None = None,
) -> dict[str, dict[str, np.ndarray]]:
    """Measure amplitude within frequency bands over time.

    Returns ``{band_name: {"times": array, "channel_N": array}}``.
    Band amplitudes are in linear scale (not dB) for consistent differencing.

    When ``bands`` / ``window_samples`` are omitted, uses ``compare.config.json``.
    """
    cfg = config if config is not None else cached_compare_config()
    resolved = bands if bands is not None else analysis_bands(config=cfg)
    win = cfg.window_samples if window_samples is None else window_samples
    hop = hop_samples if hop_samples is not None else max(1, win // 2)
    nyquist = waveform.sample_rate / 2
    filters = _build_bandpass_filters(resolved, waveform.sample_rate, nyquist)

    result: dict[str, dict[str, np.ndarray]] = {}
    times: list[float] | None = None

    for band, (sos_low, sos_high) in zip(resolved, filters, strict=True):
        band_data: dict[str, np.ndarray] = {}
        channel_series: list[list[float]] = []

        for ch in range(waveform.num_channels):
            channel = waveform.data[:, ch]
            filtered = _bandpass(channel, sos_low, sos_high)
            values, frame_times = _framed_amplitude(
                filtered,
                waveform.sample_rate,
                win,
                hop,
                metric,
            )
            channel_series.append(values)
            if times is None:
                times = frame_times

        band_data["times"] = np.array(times)
        for ch, values in enumerate(channel_series):
            band_data[f"channel_{ch}"] = np.array(values)
        result[band.name] = band_data

    return result


def band_amplitude_to_db(
    band_result: dict[str, np.ndarray],
    floor_db: float = -120.0,
) -> dict[str, np.ndarray]:
    """Convert linear band amplitudes to dB for plotting."""
    out: dict[str, np.ndarray] = {"times": band_result["times"]}
    for key, values in band_result.items():
        if key == "times":
            continue
        safe = np.maximum(values, 10 ** (floor_db / 20))
        out[key] = 20.0 * np.log10(safe)
    return out


def _build_bandpass_filters(
    bands: tuple[FrequencyBand, ...],
    sample_rate: int,
    nyquist: float,
) -> list[tuple[np.ndarray | None, np.ndarray | None]]:
    filters: list[tuple[np.ndarray | None, np.ndarray | None]] = []
    for band in bands:
        low = max(band.low_hz, 1.0)
        high = min(band.high_hz, nyquist * 0.99)
        sos_low = sp_signal.butter(4, low / nyquist, btype="high", output="sos") if low > 1 else None
        sos_high = (
            sp_signal.butter(4, high / nyquist, btype="low", output="sos")
            if high < nyquist * 0.99
            else None
        )
        filters.append((sos_low, sos_high))
    return filters


def _bandpass(
    channel: np.ndarray,
    sos_low: np.ndarray | None,
    sos_high: np.ndarray | None,
) -> np.ndarray:
    out = channel
    if sos_low is not None:
        out = sp_signal.sosfiltfilt(sos_low, out)
    if sos_high is not None:
        out = sp_signal.sosfiltfilt(sos_high, out)
    return out


def _framed_amplitude(
    channel: np.ndarray,
    sample_rate: int,
    window_samples: int,
    hop: int,
    metric: str,
) -> tuple[list[float], list[float]]:
    values: list[float] = []
    times: list[float] = []
    for start in range(0, len(channel) - window_samples + 1, hop):
        window = channel[start : start + window_samples]
        if metric == "rms":
            amp = float(np.sqrt(np.mean(window**2)))
        elif metric == "peak":
            amp = float(np.max(np.abs(window)))
        else:
            raise ValueError(f"Unknown metric: {metric!r}")
        values.append(amp)
        times.append((start + window_samples / 2) / sample_rate)
    return values, times
