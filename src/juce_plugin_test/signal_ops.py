"""Signal manipulation utilities."""

from __future__ import annotations

import numpy as np

from .audio import Waveform


def invert_phase(
    waveform: Waveform,
    channels: int | list[int] | None = None,
    *,
    invert: bool = True,
) -> Waveform:
    """Invert phase on selected channels independently.

    Parameters
    ----------
    waveform:
        Input audio.
    channels:
        Channel index or list of indices to invert. ``None`` inverts all channels.
    invert:
        If ``False``, restores (does not invert) the selected channels — useful
        when toggling back.
    """
    data = waveform.data.copy()
    if channels is None:
        indices = list(range(waveform.num_channels))
    elif isinstance(channels, int):
        indices = [channels]
    else:
        indices = list(channels)

    factor = -1.0 if invert else 1.0
    for ch in indices:
        data[:, ch] *= factor
    return waveform.with_data(data)


def sum_signals(
    a: Waveform,
    b: Waveform,
    *,
    gains: tuple[float, float] = (1.0, 1.0),
    normalize: bool = False,
) -> Waveform:
    """Sum two signals after aligning length and sample rate.

    Parameters
    ----------
    gains:
        Per-signal gain applied before summing.
    normalize:
        If ``True``, divide by the number of non-zero gains to prevent clipping
        in test fixtures.
    """
    a_aligned, b_aligned = a.aligned_to(b)
    channels = max(a_aligned.num_channels, b_aligned.num_channels)
    a_data = _pad_channels(a_aligned.data, channels)
    b_data = _pad_channels(b_aligned.data, channels)
    combined = gains[0] * a_data + gains[1] * b_data
    if normalize:
        divisor = sum(1 for g in gains if g != 0.0) or 1.0
        combined /= divisor
    return Waveform(data=combined, sample_rate=a_aligned.sample_rate)


def _pad_channels(data: np.ndarray, target_channels: int) -> np.ndarray:
    if data.shape[1] == target_channels:
        return data
    if data.shape[1] == 1 and target_channels > 1:
        return np.tile(data, (1, target_channels))
    if data.shape[1] < target_channels:
        pad = np.zeros((data.shape[0], target_channels - data.shape[1]))
        return np.hstack([data, pad])
    return data[:, :target_channels]
