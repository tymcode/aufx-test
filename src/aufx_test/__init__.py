"""AU/FX plugin audio test framework."""

from .audio import Waveform
from .aupreset import AUpresetError, AUpresetInfo, import_aupreset, validate_aupreset
from .capture import CapturePair, capture_before_after, capture_with_host, compare_captured_to_reference
from .compare_config import CompareConfig, load_compare_config, num_of_bands
from .compare_config import default_config_path as default_compare_config_path
from .comparison import (
    ComparisonResult,
    ComparisonThresholds,
    DifferenceMetrics,
    compare_waveforms,
    compute_difference_metrics,
    difference_signal,
)
from .explore import capture_snapshot_from_cli, run_explore
from .host import PassthroughHost, PluginHost
from .host_app import default_config_path, default_host_app_bin, launch_host_app
from .paths import unique_output_path
from .session import ExperimentSession, StateSnapshot, TestSetup
from .signal_ops import invert_phase, sum_signals
from .silence import SilenceRegion, distance_from_silence, measure_silence_regions
from .spectrum import DEFAULT_BANDS, FrequencyBand, analysis_bands, band_amplitude_over_time
from .subprocess_host import RendererError, SubprocessPluginHost
from .testgen import export_setups_json, export_test_module

__all__ = [
    "AUpresetError",
    "AUpresetInfo",
    "CapturePair",
    "ComparisonResult",
    "CompareConfig",
    "ComparisonThresholds",
    "DEFAULT_BANDS",
    "DifferenceMetrics",
    "ExperimentSession",
    "FrequencyBand",
    "PassthroughHost",
    "PluginHost",
    "RendererError",
    "SilenceRegion",
    "StateSnapshot",
    "SubprocessPluginHost",
    "TestSetup",
    "Waveform",
    "analysis_bands",
    "band_amplitude_over_time",
    "default_compare_config_path",
    "load_compare_config",
    "num_of_bands",
    "capture_before_after",
    "capture_snapshot_from_cli",
    "capture_with_host",
    "compare_captured_to_reference",
    "compare_waveforms",
    "compute_difference_metrics",
    "difference_signal",
    "distance_from_silence",
    "export_setups_json",
    "export_test_module",
    "import_aupreset",
    "invert_phase",
    "launch_host_app",
    "default_config_path",
    "default_host_app_bin",
    "measure_silence_regions",
    "run_explore",
    "sum_signals",
    "unique_output_path",
    "validate_aupreset",
]

__version__ = "0.1.0"
