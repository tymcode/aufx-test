# juce-plugin-test

Automated audio test framework for JUCE-based VST and AU audio effects plugins.

Compare plugin output against reference waveforms using objective, repeatable metrics — not bit-for-bit equality. Designed for Python-first workflows with a path toward CI integration.

## Features

- **Waveform capture & comparison** — snapshot audio before/after parameter changes and compare to a reference
- **Frequency-band analysis** — measure amplitude in configurable bands over time
- **Difference metrics** — SNR, correlation, RMS error, spectral distance, and more
- **Silence analysis** — distance-from-silence curves and per-channel silence region detection
- **Signal utilities** — per-channel phase inversion, signal summing
- **Visualization** — graph any analysis result for quick inspection

## Install

```bash
python -m venv .venv
source .venv/bin/activate
pip install -e ".[dev]"
```

## Quick start

```python
from juce_plugin_test import Waveform, compare_waveforms, capture_before_after
from juce_plugin_test.graphing import plot_comparison

# Load input and reference
input_wav = Waveform.from_file("fixtures/sine_440hz.wav")
reference = Waveform.from_file("fixtures/expected_output.wav")

# Capture before/after a parameter change (via your plugin host)
before, after = capture_before_after(
    lambda: render_plugin(input_wav),  # your host integration
    adjust_controls={"mix": 0.5},
)

result = compare_waveforms(after, reference, snr_db_min=30.0)
assert result.passed, result.summary()

plot_comparison(before, after, reference, save_path="output/comparison.png")
```

## Plugin host integration (future)

The `PluginHost` protocol in `juce_plugin_test.host` defines the interface for driving a real VST/AU. Implement it with your preferred headless host (e.g. a small JUCE command-line renderer, [pluginval](https://github.com/Tracktion/pluginval), or a custom tool) and pass it to the capture helpers.

See [docs/ci-integration.md](docs/ci-integration.md) for build-pipeline integration notes.

## Running tests

```bash
pytest
```

## Project layout

```
src/juce_plugin_test/
  audio.py          # Waveform type and I/O
  signal_ops.py     # Phase invert, sum
  silence.py        # Silence distance and region detection
  spectrum.py       # Band amplitude over time
  comparison.py     # Objective difference metrics
  capture.py        # Before/after capture helpers
  graphing.py       # Matplotlib visualization
  host.py           # PluginHost protocol (stub)
tests/
fixtures/
docs/
```
