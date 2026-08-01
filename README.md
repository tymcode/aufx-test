# aufx-test

Automated audio test framework for AU / FX (VST/AU) audio effects plugins.

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

**Requirements:** macOS 13+, Xcode Command Line Tools, CMake ≥ 3.22, Python ≥ 3.10 (with a working `ssl` module).

```bash
git clone --recurse-submodules <repo-url> aufx-test
cd aufx-test
./scripts/bootstrap.sh
```

That initializes the pinned JUCE submodule (`native/JUCE` @ 8.0.14), creates `.venv`, installs the Python package, seeds `host.config.json` if missing, and builds `plugin_renderer` + **AU Effects Explorer**.

Manual alternative (Apple Silicon Homebrew example):

```bash
/opt/homebrew/opt/python@3.13/bin/python3.13 -m venv .venv
source .venv/bin/activate
python -m pip install -U pip
pip install -e ".[dev]"
```

If `source .venv/bin/activate` or `pip` fails with SSL / permission errors, recreate the venv with a current Homebrew Python (not an old pyenv 3.9.x build). Check: `which python` should point at `.venv/bin/python`, and `python -c "import ssl; print(ssl.OPENSSL_VERSION)"` should succeed.
## Quick start

```python
from aufx_test import Waveform, compare_waveforms, capture_before_after
from aufx_test.graphing import plot_comparison

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

The `PluginHost` protocol in `aufx_test.host` defines the interface for driving a real VST/AU. Implement it with your preferred headless host (e.g. a small JUCE command-line renderer, [pluginval](https://github.com/Tracktion/pluginval), or a custom tool) and pass it to the capture helpers.

See [docs/ci-integration.md](docs/ci-integration.md) for build-pipeline integration notes.

## Manual exploration → automated tests

Experiment in the **AU Effects Explorer** host, or capture from a DAW:

```bash
# After ./scripts/bootstrap.sh (or build plugin_host_app yourself)
APP="native/build/plugin_host_app/plugin_host_app_artefacts/Release/AU Effects Explorer.app"
open "$APP" --args --config "$(pwd)/host.config.json" --project-root "$(pwd)"

# Optional: Python wrapper (needs a working .venv — see docs/manual-exploration.md)
# source .venv/bin/activate && aufx-test host

# Legacy: terminal REPL after bouncing in a DAW
aufx-test explore --new-name "DEEP:Z exploration" \
  --plugin "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component"

# Or capture non-interactively after bouncing in your DAW
aufx-test session snap "MyEffect" "half mix" \
  --input fixtures/sine.wav --output ~/Desktop/bounce.wav \
  --aupreset ~/Library/Audio/Presets/MyEffect/half_mix.aupreset

# Promote good captures and export pytest
aufx-test session promote "MyEffect" half_mix --test-name test_mix_half
aufx-test session export "MyEffect" -o tests/generated/test_my_effect.py
aufx-test session export-presets "MyEffect" -o share/with-developer/
```

Headless replay uses `SubprocessPluginHost` with the JUCE `plugin_renderer` CLI and `.aupreset` state blobs.

See [native/README.md](native/README.md) and [docs/plugin-renderer.md](docs/plugin-renderer.md).

See [docs/manual-exploration.md](docs/manual-exploration.md) for the full workflow.

### Share a Mac build

```bash
./scripts/package_mac_app.sh
# → dist/AU-Effects-Explorer-macOS.zip  (universal arm64 + x86_64, macOS 13+)
```

See [docs/mac-app-distribution.md](docs/mac-app-distribution.md).

### Batch golden compares (`compares/<device>/`)

Layout::

```
compares/deepz/
  dry/{Source}.wav
  presets/au/{NN}-{Preset Name}.aupreset
  goldens/{Source}-{NN}-{Preset-Slug}.wav
```

Render each dry source through the preset embedded in the golden filename, compare to the golden, and write per-fail reports:

```bash
# Discover runnable cases (skips goldens whose preset is missing)
aufx-test compare-batch compares/deepz --list

# Full batch against DEEP/Z from host.config.json
aufx-test compare-batch compares/deepz --plugin-id deep_z

# Subset + smoke
aufx-test compare-batch compares/deepz --plugin-id deep_z --filter 'Vocals-*.wav' --limit 3
```

By default each render feeds **1.5 s of silence** after loading the preset so the
plugin can finish transitioning from the previous effect; that lead-in is trimmed
from the capture before compare (`--settle-seconds 0` to disable). Leading silence
on actuals (−60 dB peak) is also stripped before compare so captures line up with
onset-trimmed goldens.

Outputs under `test-results/deepz/` (override with `--results-root`):

- `actuals/` — rendered WAVs
- `fails/<case>/` — mismatch report (audio, plots, `mismatch.json`) per failure
- `summary.json`, `report.html`

## Running tests

```bash
pytest

# Shareable report with failure metrics, plots, and audio players
pytest tests/generated/ --aufx-html=test-results/report.html
```

The HTML file links to artifacts in `test-results/`; share that folder (or zip
it) so the report's audio players and images remain available.

## Project layout

```
src/aufx_test/
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
  compare_batch.py  # dry→preset→golden batch compares
  graphing.py       # Matplotlib visualization
  host.py           # PluginHost protocol
native/
  plugin_renderer/  # JUCE headless CLI (see native/README.md)
  CMakeLists.txt
compares/           # Per-device dry / presets / goldens trees
tests/
fixtures/
docs/
```
