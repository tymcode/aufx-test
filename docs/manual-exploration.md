# Manual plugin exploration

Launch the **Plugin Host** app to audition plugins with fixture WAVs, hear output on your speakers, and capture test cases — input fixture WAV, rendered output WAV, and `.aupreset` state in one step.

Snapshots can then be promoted to be added as testcases to run in `pytest` automation. Snapshots can be used as `golden` references, which expect subsequent renders using the same preset to be materially the same, or as `broken` references, which expect subsequent renders to not match.

Snapshot artifacts (input WAV, output WAV, and preset) can also be added to bug reports so the developer can reproduce issues.

The old terminal-only `explore` REPL is still available for scripting. For day-to-day work, launch **AU Effects Explorer** directly (see [Launch the host app](#launch-the-host-app)); the Python `aufx-test host` wrapper is optional.

## Prerequisites

Build the native host (Python is **not** required to launch it):

```bash
cd ~/dev/aufx-test
cmake -S native -B native/build -DCMAKE_BUILD_TYPE=Release
cmake --build native/build --target plugin_host_app
```

The app lands at:

```text
native/build/plugin_host_app/plugin_host_app_artefacts/Release/AU Effects Explorer.app
```

Or package a shareable copy into `dist/`:

```bash
./scripts/package_mac_app.sh
```

Optional — only needed for `aufx-test` CLI helpers (`session`, `explore`, pytest, or the `aufx-test host` wrapper):

```bash
# Use Homebrew Python — avoid old pyenv 3.9 builds (broken ssl / ancient pip)
/opt/homebrew/opt/python@3.13/bin/python3.13 -m venv .venv
source .venv/bin/activate
python -m pip install -U pip
pip install -e ".[dev]"
aufx-test --help
```

If activate/pip fails with permission errors or `ssl` / TLS warnings (often a stale pyenv Python linked against removed `openssl@1.1`):

```bash
xattr -cr .venv 2>/dev/null || true
rm -rf .venv
/opt/homebrew/opt/python@3.13/bin/python3.13 -m venv .venv
source .venv/bin/activate
python -m pip install -U pip
pip install -e ".[dev]"
```

Confirm: `python -c "import ssl; print(ssl.OPENSSL_VERSION)"` must succeed before `pip install`.
## Configure plugins

Edit `host.config.json` at the project root for the repo-rooted workflow. The toolbar dropdown is seeded from this list; use **Plugins → Add Plugin…** / **More plugins…** to pick from the AU cache and append entries. **Plugins → Rescan Audio Units…** (`Cmd+R`) refreshes the AU cache; **Rescan Source Clips** (`Cmd+Shift+R`) reloads fixture WAVs.

```json
{
  "fixtures_dir": "fixtures",
  "sessions_root": "sessions",
  "python_cli": ".venv/bin/aufx-test",
  "log_file": "sessions/plugin_host.log",
  "default_plugin": "deepz",
  "plugins": [
    {
      "id": "deepz",
      "name": "DEEP/Z",
      "manufacturer": "Temecula DSP",
      "path": "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component",
      "presets_dir": "~/Library/Audio/Presets/Temecula DSP/DEEP:Z",
      "default_preset": "~/Library/Audio/Presets/Temecula DSP/DEEP:Z/Init Serial.aupreset",
      "session": "DEEPZ exploration"
    }
  ]
}
```

Paths may be absolute, `~/…`, or relative to the project root. `default_preset` is optional. Add more objects to `plugins` for additional dropdown entries.

Each host launch appends an 8-character session hash to `log_file` (e.g. `sessions/plugin_host_a1b2c3d4.log`).

## Launch the host app

### Recommended (no Python)

From the project directory, open the built app and point it at the repo config:

```bash
cd ~/dev/aufx-test

APP="native/build/plugin_host_app/plugin_host_app_artefacts/Release/AU Effects Explorer.app"

open "$APP" --args \
  --config "$(pwd)/host.config.json" \
  --project-root "$(pwd)"
```

Or run the binary directly:

```bash
"$APP/Contents/MacOS/AU Effects Explorer" \
  --config "$(pwd)/host.config.json" \
  --project-root "$(pwd)"
```

After packaging, the same flags work with `dist/AU Effects Explorer.app`.

Without `--project-root` / `--config`, the app uses its exploration data folder (default `~/Library/Application Support/AU Effects Explorer/`) — see [mac-app-distribution.md](mac-app-distribution.md).

### Optional Python wrapper

If your venv works, this is equivalent to the `open … --args` form above:

```bash
source .venv/bin/activate
aufx-test host
```

Overrides:

```bash
aufx-test host --config path/to/host.config.json --project-root .
```

If `aufx-test` is not on your PATH:

```bash
.venv/bin/aufx-test host
```

You do **not** need a working venv just to run the host UI.## Using the app

```
┌──────────────────────────────────────────────────────┐
│  Plugin Host | [Manufacturer — Plugin ▼] | status... │
│  Preset ▼ [Load] Save as [____] [Save] ☐ Replace     │
│  Fixture ▼ [Play] [Stop]                             │
│  [Capture] Description [________] Test Role ▼        │
│  ┌────────────────────────────────────────────────┐  │
│  │   plugin UI (embedded)                         │  │
│  └────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────┘
         │ fixture WAV ──► plugin ──► speakers
         │ Capture ──► offline bounce + .aupreset + session
```

1. **Plugin** — dropdown lists entries from `host.config.json`. Switching loads that plugin and its `presets_dir` / `session`.
2. **Preset** — pick a `.aupreset` from that plugin’s `presets_dir`, click **Load**.
3. **Save as** — enter a name and click **Save** to write a new `.aupreset` into the presets folder (it appears in the dropdown). Enable **Replace existing** to overwrite a preset of the same name.
4. **Fixture** — pick an input WAV (`fixtures/guitar.wav`, etc.).
5. **Play** — loop the fixture through the plugin to system audio until Stop. Space bar toggles play/stop (unless a text field has focus), including while **Level Meters** is focused.
6. **Stop** — stop playback.
7. **Test Role** — whether this is a `golden` reference (expects match), `broken` reference (expects mismatch) or `suspect`, which currently behaves the same as `broken`
8. **Capture Test Case** (`Cmd+T`) — saves:
  - current plugin state as `.aupreset` when **Capture settings → Software** is on (live settings, not the last loaded preset file)
  - offline reference render from a one-shot playback of the fixture WAV (same tail logic as CI), when Capture is Rendered plugin or Both
  - hardware return WAV when Capture is Hardware or Both
  - hardware sysex dump when **Capture settings → Hardware** is on (dimmed if MIDI out is unset in MIDI Setup)
  - snapshot entry in the configured session (e.g. `sessions/DEEPZ exploration/session.json`) including the entered description and test role
  - optional **Generate report** (default on): runs `aufx-test compare --root … --write-report` for the new snapshot when capture succeeds (`python_cli` in `host.config.json` must point at `.venv/bin/aufx-test`)
  - for Hardware / Both: **Calibrate** (default on) measures the return noise floor for the silence gate and subtracts measured DC offset from the hardware WAV

Tweak the plugin UI between captures. Each capture creates a new snapshot you can promote and export.

## Hardware Audio Setup

**Plugins → Hardware Audio Setup…** configures the CoreAudio insert loop (send / return / monitor), buffer size, and latency.

- **Auto-detect** — plays `fixtures/impulse.wav` five times through the send pair, averages the correlation-peak latencies (and peak loop gain). Put the hardware box on a dry/bypass program first.
- **Test** — plays the current source clip out the send pair so you can verify routing and levels (software effect is muted while the dialog is open).

## Level Meters

**View → Level Meters** (`Cmd+M`) opens a floating window with live **Send** / **Return** meters for the active path:

- **Use Software** — pre-plugin fixture vs post-plugin (current bypass / mix / preset)
- **Use Hardware** — insert send pair vs return pair

Live meters show **sample peaks** (per audio block), with smoothed bars for readability, a **sticky peak hold**, and a red **CLIP** badge when any sample reaches 0 dBFS. Click a meter to clear hold and CLIP.

**Level sweep…** prompts for a plot name, plays `fixtures/synth_waves/sine_0db_1ch_5s_48k.wav` at stepped send gains through the **current** path (not dry-thru unless Bypass is on), and writes `<project-root>/calibration/<slug>.json` plus `<slug>.png` when `python_cli` is set.

Each sweep step records **true sample peak**, **RMS**, and **BS.1770-style LUFS** over a **0.75 s settle + 2.5 s analysis window** (hardware records ~4 s per step) so pulsing / modulated effects get a usable read. The plot shows peak, RMS, and LUFS vs commanded send. Capture-time correction from the curve is not applied yet.

```bash
aufx-test calibrate-plot calibration/my_plot.json -o calibration/my_plot.png
```

## After capturing

```bash
# Catalog the snapshots for a given session folder
aufx-test session show                      # list exploration folders
aufx-test session show "DEEPZ exploration"  # summary for one session

# Promote shapshots as automatable testcases

# list unpromoted tests in session
aufx-test session promote "DEEPZ exploration"

# _gld suffix interpreted as golden
aufx-test session promote "DEEPZ exploration" flange_negative_regen_gld \
  --test-name flange_negative_regen

# _bkn suffix interpreted as broken
aufx-test session promote "DEEPZ exploration" pan_lfo_pulse_bkn \
  --test-name pan_lfo_pulse

# Export pytest module -- adds all promoted snapshots
aufx-test session export "DEEPZ exploration" \
  -o tests/generated/test_deepz.py

# Compare hardware-vs-software for one snapshot by id/name/stem
aufx-test compare --root sessions "DEEPZ exploration" flange_negative_regen_gld

# Write compare artifacts into that snapshot's artifacts/<stem>/ folder
aufx-test compare --root sessions "DEEPZ exploration" flange_negative_regen_gld \
  --write-report

# Software-only or hardware-only captures: informational dry vs wet report
# (spectrogram + metrics, no pass/fail) when input_audio is present
aufx-test compare --root sessions "DEEPZ exploration" software_only_snap \
  --write-report

# Override report directory explicitly
aufx-test compare --root sessions "DEEPZ exploration" flange_negative_regen_gld \
  --write-report reports/deepz/flange_negative_regen_gld

# Generated report files:
#   compare.json
#   compare_waveform.png
#   compare_metrics.png
#   compare_report.html   (plots + formatted metrics + raw JSON)
#
# compare_report.html includes toggle controls for available views:
#   - Hardware vs Software (default, when both wet captures exist)
#   - Dry vs Software Wet (software capture + input, or Both + input)
#   - Dry vs Hardware Wet (hardware capture + input, or Both + input)
# Software-only / hardware-only reports are informational (no PASSED/FAILED).
# It also shows Source Clip near the top when known.
```

Headless replay in CI still uses `plugin_renderer` + `SubprocessPluginHost` with the captured `.aupreset`.

## Python API

```python
from aufx_test.host_app import launch_host_app

process = launch_host_app()  # reads host.config.json
process.wait()
```



## Troubleshooting


| Problem                         | Fix                                                                      |
| ------------------------------- | ------------------------------------------------------------------------ |
| venv activate / pip SSL errors | Recreate with Homebrew Python: `rm -rf .venv && /opt/homebrew/opt/python@3.13/bin/python3.13 -m venv .venv` then `pip install -U pip && pip install -e ".[dev]"` — or skip Python and `open` the `.app` (see Launch) |
| `aufx-test: command not found`  | Recreate/activate `.venv`, or use `.venv/bin/aufx-test`, or launch the `.app` directly |
| Plugin host app not found       | Build: `cmake --build native/build --target plugin_host_app`             |
| Config / plugin path errors     | Edit `host.config.json`; ensure each `path` exists                       |
| No presets in dropdown          | Set `presets_dir` for that plugin entry                                  |
| Capture fails on session update | Ensure `python_cli` in config points at `.venv/bin/aufx-test` when using Python session tools |
| Need error details              | Check `sessions/plugin_host_<hash>.log` (from `log_file` + session hash) |
| No audio on Play                | Check macOS output device; restart the host app                          |




## Legacy: terminal-only explore REPL

For scripting without the GUI:

```bash
aufx-test explore \
  --new-name "DEEP:Z exploration" \
  --plugin "/Library/Audio/Plug-Ins/Components/TemeculaDSPDEEPZ.component"
```

This records bounces you make manually in a DAW — useful when you already have rendered WAVs, but not required for the host-app workflow.