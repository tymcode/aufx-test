# aufx-test

This is a test framework for Audio Unit effects plugins. Created primarily to test AU emulations of hardware effects units, it has two parts, an application and command line tools. 
*AU Effects Explorer* is a host app with hardware-in-loop support for manual testing that can generate testcases for defect reporting and test automation.`aufx-test` has a set of functions for use in CI and to manage a `pytest` framework for running regression tests.

## AU Effects Explorer
This host app is designed to compare audio processing between software and hardware.  It is also an efficient way to manually test plugins in general, providing required MIDI and DAW functions without the complexities of a DAW.

When you select a plugin, you are said to begin an *exploration session*. Understanding that will help understand the folder structure for exported artifacts.

### Features
* Hosting of arbitrary AU plugins
* mock DAW features such as a playhead (for testing BPM-based time divisions)
* A variety of canned source clips in the `fixtures` directory, for repeatability
* Host bypass, input gain, and mix controls
* Support for MIDI controllers
* Hardware-in-Loop integration for external equipment with easy switching between hardware and software
* Send/Return level metering with peak/RMS/LUFS clip indicators
* Save/Load .aupresets to capture and restore plugin state
* Experimental support for Receive/Transmit sysex for test setup (Quadrasynth only for now)
* Capture testcase data to add to automation. Captured testcases collect all required artifacts to reproduce the bug and perform regression testing; the folders can be zipped and shared with the developer or attached to a bug report.
* Testcases can be categorized as golden or broken so that the test framework knows whether to pass or fail a match.
* Create report comparing software results against hardware (or dry vs wet)
* A variety of calibration features for levels and latency compensation
* Lights Out mode blacks out the background for recording screencaps. Monitoring supports Multi-Output Devices with loopbacks to ensure that the audio is in the screencap  

Compare plugin output against reference waveforms using objective, repeatable metrics — not bit-for-bit equality. Designed for a flow from manual exploration to automated testing with a path toward CI integration.

### Requirements
AU Effects Explorer is universal (arm64 + x86_64) and requires macOS 13+.

### Install
Download prebuilt executable from Releases.
Currently it is self-signed so Mac Gatekeeper will try to prevent you from running it.  
1. Clear quarantine if needed: `xattr -cr "AU Effects Explorer.app"`
2. Right-click the app → Open (first launch), or:
     System Settings → Privacy & Security → Open Anyway


### Basic Use
At first launch it will scan for installed AU plugins. To keep the tool's plugin list fast and efficient to use, you must choose which of the scanned plugins appear in it.  You can add or remove them from the list as needed, and rescan when new plugins are installed.

When the plugin loads, select a "Source Clip" to loop into the plugin and click Begin.

To support testing and using MIDI functions like MIDI Learn, select **MIDI Setup** from the Mac menu bar to enable your selected controllers. To use hardware-in-loop features, select **Hardware Audio Setup** from the Mac menu bar and specify the send and return ports, as well as setting buffer size and determining latency.

When you get a setup that is working correctly, and you want your test automation to make sure that it continues to work right when a build changes, select the Capture Test Case menu item and mark it as "golden".  When you find a case where it's misbehaving, capture the test case and mark it "broken", and you can use automated testing to flag if the plugin continues to sound like that in future builds. In either case it will reveal a folder in the finder containing test artifacts that will be required for setting up automated testing.  You can provide this folder with your bug report; it will contain all the artifacts needed to reproduce the issue, as well as a spectrographic analysis report.

If you've used Capture Test Case and want to include a testcase in automated testing, use `aufx-test`, described below.

### `aufx-test` command-line tool

After `./scripts/bootstrap.sh` (or `pip install -e .`), the CLI covers compare, capture, session management, and host launch:

| Command | What it does |
|---------|----------------|
| `aufx-test compare` | Score one actual WAV vs a golden (or, with `--root`, a session snapshot: HW vs SW, or dry vs wet). Optional plots / HTML via `--write-report`. |
| `aufx-test compare-batch` | Render every dry×preset named by goldens under `compares/<device>/`, compare to those goldens, write `actuals/`, `fails/`, `summary.json`, `report.html`. One plugin per run (`--plugin-id` / `--plugin`). |
| `aufx-test host` | Launch **AU Effects Explorer** with `--config` / `--project-root`. |
| `aufx-test explore` | Legacy interactive REPL to build a session after bouncing in a DAW. |
| `aufx-test session new` | Create an empty exploration session. |
| `aufx-test session snap` | Add a snapshot non-interactively (input / bounce / `.aupreset`). |
| `aufx-test session show` | List sessions or print one session’s snapshots. |
| `aufx-test session promote` | Mark a snapshot for automation (optional thresholds / `--negative` for broken cases). |
| `aufx-test session export` | Emit pytest (or JSON) for promoted setups → typically `tests/generated/`. |
| `aufx-test session export-presets` | Bundle `.aupreset` files + manifest for sharing with a plugin developer. |
| `aufx-test session import-goldens` | Import external golden triplets into a session. |
| `aufx-test calibrate-plot` | Plot a level-sweep calibration JSON to PNG. |

Typical host → automation flow: capture in Explorer → `session promote` → `session export` → `pytest`. For factory-preset golden trees, use `compare-batch` (see below).


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
git clone --recurse-submodules https://github.com/tymcode/aufx-test.git
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
input_wav = Waveform.from_file("fixtures/synth_waves/sine.wav")
reference = Waveform.from_file("path/to/golden_or_bounce.wav")

# Capture before/after a parameter change (via your plugin host)
pair = capture_before_after(
    lambda: render_plugin(input_wav),  # your host integration
    adjust_controls={"mix": 0.5},
)

result = compare_waveforms(pair.after, reference, snr_db_min=30.0)
assert result.passed, result.summary()

plot_comparison(pair.before, pair.after, reference, save_path="output/comparison.png")
```

## Plugin host integration

The `PluginHost` protocol in `aufx_test.host` is the interface for driving a real AU. The built-in implementation is `SubprocessPluginHost`, which shells out to the JUCE `plugin_renderer` CLI with `.aupreset` state. Use that for headless replay, or implement the protocol with another host if needed.

See [docs/ci-integration.md](docs/ci-integration.md) for build-pipeline integration notes.

## Manual exploration → automated tests

Experiment in the **AU Effects Explorer** host, or capture from a DAW:

```bash
# After ./scripts/bootstrap.sh (or build plugin_host_app yourself)
APP="native/build/plugin_host_app/plugin_host_app_artefacts/Release/AU Effects Explorer.app"
open "$APP" --args --config "$(pwd)/host.config.json" --project-root "$(pwd)"

# Optional: Python wrapper (needs a working .venv — see docs/aufx-explorer.md)
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

See [docs/aufx-explorer.md](docs/aufx-explorer.md) for the full host workflow.

### Share a Mac build

```bash
./scripts/package_mac_app.sh
# → dist/AU-Effects-Explorer-macOS.zip  (universal arm64 + x86_64, macOS 13+)
```

See also [native/README.md](native/README.md).

### Batch golden compares (`compares/<device>/`)

Layout:

```
compares/{plugin ID}/
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
  plugin_host_app/  # AU Effects Explorer (GUI host)
  plugin_renderer/  # JUCE headless CLI (see native/README.md)
  JUCE/             # pinned JUCE submodule
  CMakeLists.txt
compares/           # Per-device dry / presets / goldens trees
tests/
fixtures/
docs/
```
