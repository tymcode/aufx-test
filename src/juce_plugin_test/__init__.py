"""JUCE plugin audio test framework."""

from .audio import Waveform
from .capture import CapturePair, capture_before_after, capture_with_host, compare_captured_to_reference
from .comparison import (
    ComparisonResult,
    ComparisonThresholds,
    DifferenceMetrics,
    compare_waveforms,
    compute_difference_metrics,
    difference_signal,
)
from .host import PassthroughHost, PluginHost
from .signal_ops import invert_phase, sum_signals
from .silence import SilenceRegion, distance_from_silence, measure_silence_regions
from .spectrum import DEFAULT_BANDS, FrequencyBand, band_amplitude_over_time

__all__ = [
    "CapturePair",
    "ComparisonResult",
    "ComparisonThresholds",
    "DEFAULT_BANDS",
    "DifferenceMetrics",
    "FrequencyBand",
    "PassthroughHost",
    "PluginHost",
    "SilenceRegion",
    "Waveform",
    "band_amplitude_over_time",
    "capture_before_after",
    "capture_with_host",
    "compare_captured_to_reference",
    "compare_waveforms",
    "compute_difference_metrics",
    "difference_signal",
    "distance_from_silence",
    "invert_phase",
    "measure_silence_regions",
    "sum_signals",
]

__version__ = "0.1.0"
