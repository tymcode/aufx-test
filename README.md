# juce-plugin-test

Automated audio test framework for JUCE-based VST and AU audio effects plugins.

Compare plugin output against reference waveforms using objective, repeatable metrics — not bit-for-bit equality. Designed for Python-first workflows with a path toward CI integration.

## Features

- **Waveform capture & comparison** — snapshot audio before/after parameter changes and compare to a reference
- **Frequency-band analysis** — measure amplitude in configurable bands over time (`compare.config.json`)
- **Difference metrics** — SNR, correlation, RMS error, spectral distance, and more
- **Silence analysis** — distance-from-silence curves and per-channel silence region detection
- **Signal utilities** — per-channel phase inversion, signal summing
- **Visualization** — graph any analysis result for quick inspection
- **Manual exploration sessions** — capture plugin UI states and bounce audio for later automation

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

## Manual exploration → automated tests

Experiment in the plugin host GUI, or capture from a DAW:

```bash
# Recommended: native host with plugin UI + fixture playback + capture
# Configure plugins in host.config.json, then:
juce-plugin-test host

# Legacy: terminal REPL after bouncing in a DAW
juce-plugin-test explore --new-name "DEEP:Z exploration" \
  --plugin "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component"

# Or capture non-interactively after bouncing in your DAW
juce-plugin-test session snap "MyEffect" "half mix" \
  --input fixtures/sine.wav --output ~/Desktop/bounce.wav \
  --aupreset ~/Library/Audio/Presets/MyEffect/half_mix.aupreset

# Promote good captures and export pytest
juce-plugin-test session promote "MyEffect" half_mix --test-name test_mix_half
juce-plugin-test session export "MyEffect" -o tests/generated/test_my_effect.py
juce-plugin-test session export-presets "MyEffect" -o share/with-developer/
```

Headless replay uses `SubprocessPluginHost` with the JUCE `plugin_renderer` CLI and `.aupreset` state blobs.

Build the renderer:

```bash
git submodule add https://github.com/juce-framework/JUCE.git native/JUCE
cmake -S native -B native/build -DCMAKE_BUILD_TYPE=Release
cmake --build native/build --target plugin_renderer
cmake --build native/build --target plugin_host_app
```

See [native/README.md](native/README.md) and [docs/plugin-renderer.md](docs/plugin-renderer.md).

See [docs/manual-exploration.md](docs/manual-exploration.md) for the full workflow.

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
  session.py        # Manual exploration sessions
  explore.py        # Interactive capture REPL
  testgen.py        # Export sessions to pytest
  subprocess_host.py
  graphing.py       # Matplotlib visualization
  host.py           # PluginHost protocol
native/
  plugin_renderer/  # JUCE headless CLI (see native/README.md)
  CMakeLists.txt
tests/
fixtures/
docs/
```
