# Manual plugin exploration

Launch the **Plugin Host** app to audition plugins with fixture WAVs, hear output on your speakers, and capture test cases — input fixture WAV, rendered output WAV, and `.aupreset` state in one step.

Snapshots can then be promoted to be added as testcases to run in `pytest` automation. Snapshots can be used as `golden` references, which expect subsequent renders using the same preset to be materially the same, or as `broken` references, which expect subsequent renders to not match.

Snapshot artifacts (input WAV, output WAV, and preset) can also be added to bug reports so the developer can reproduce issues.

The old terminal-only `explore` REPL is still available for scripting, but `aufx-test host` **is the recommended workflow**.

## Prerequisites

```bash
cd ~/dev/aufx-test
python -m venv .venv
source .venv/bin/activate
pip install -e ".[dev]"

cmake -S native -B native/build -DCMAKE_BUILD_TYPE=Release
cmake --build native/build --target plugin_host_app
```

Verify:

```bash
aufx-test --help          # CLI installed in venv
ls native/build/plugin_host_app/plugin_host_app_artefacts/Release/
```



## Configure plugins

Edit `host.config.json` at the project root. The plugin selector is populated **only** from this list (no AU scan):

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
      "default_preset": "/Users/mikejennings/Library/Audio/Presets/Temecula DSP/DEEP:Z/Init Serial.aupreset",
      "session": "DEEPZ exploration"
    }
  ]
}
```

Paths may be absolute, `~/…`, or relative to the project root. `default_preset` is optional. Add more objects to `plugins` for additional dropdown entries.

Each host launch appends an 8-character session hash to `log_file` (e.g. `sessions/plugin_host_a1b2c3d4.log`).

## Launch the host app

From the project directory:

```bash
source .venv/bin/activate
aufx-test host
```

Optional overrides:

```bash
aufx-test host --config path/to/host.config.json --project-root .
```

If `aufx-test` is not on your PATH:

```bash
.venv/bin/aufx-test host
```



## Using the app

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
5. **Play** — loop the fixture through the plugin to system audio until Stop. Space bar toggles play/stop (unless a text field has focus).
6. **Stop** — stop playback.
7. **Test Role** — whether this is a `golden` reference (expects match), `broken` reference (expects mismatch) or `suspect`, which currently behaves the same as `broken`
8. **Capture Test Case** — saves:
  - current plugin state as `.aupreset`
  - offline reference render from a one-shot playback of the fixture WAV (same tail logic as CI)
  - snapshot entry in the configured session (e.g. `sessions/DEEPZ exploration/session.json`) including the entered description and test role

Tweak the plugin UI between captures. Each capture creates a new snapshot you can promote and export.

## After capturing

```bash
# Catalog the snapshots for a given session folder
aufx-test session show "DEEPZ exploration"

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
| `aufx-test: command not found`  | `source .venv/bin/activate` or use `.venv/bin/aufx-test`                 |
| Plugin host app not found       | Build: `cmake --build native/build --target plugin_host_app`             |
| Config / plugin path errors     | Edit `host.config.json`; ensure each `path` exists                       |
| No presets in dropdown          | Set `presets_dir` for that plugin entry                                  |
| Capture fails on session update | Ensure `python_cli` in config points at `.venv/bin/aufx-test`            |
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