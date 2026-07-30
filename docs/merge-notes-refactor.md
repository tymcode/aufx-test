# Merge notes: `refactor` → `main`

**Branch:** `refactor`  
**Base:** `main`  
**Scope:** ~75 files, +8.5k / −3.3k lines (native host refactor + hardware/MIDI capture work + fixtures)

## Summary

`refactor` lands a large structural cleanup of **AU Effects Explorer** plus the hardware-insert / capture / MIDI features that motivated that cleanup. `MainWindow` is no longer a monolith; audio engine responsibilities are split into collaborators; test-case capture goes through a dedicated pipeline; several launch/capture bugs found while exercising the split are fixed.

## Commits (oldest → newest)

| Commit | Notes |
|--------|--------|
| `9b19245` | Hardware capture reliability; monitor-output routing; snapshot-ID compare targeting; AU identifiers in renderer paths |
| `41ef7f9` | MIDI sysex modules (incl. Quadraverb dry-thru); MIDI setup persistence; richer hardware capture artifacts |
| `886773d` | Drum / piano fixture WAVs |
| `d8d7570` | Comment pass on host code |
| `6801bd6` | MainContent / engine module split; CapturePipeline; SourceClipLibrary / TestCaseLoader; capture & startup bugfixes |
| `28f42ac` | Reference 0 dB sine fixture; trimmed supersaw; portable `~/…` presets paths in config save |

## Architecture (what moved where)

### UI / window

| Before | After |
|--------|--------|
| Huge nested `MainContent` inside `MainWindow.cpp` | `MainContent.*` — control strip, transport, fixtures, editor/VU |
| Preset + HW sysex row in MainContent | `PresetHardwareState.*` |
| Capture dialog / orchestration in MainContent | `TestCaseCapture.*` (+ `CapturePipeline.*`) |
| Chrome widgets inline | `HostChromeControls.*` |
| Window/menus only partly separated | `MainWindow.*` — DocumentWindow, menus, Lights Out |

### Audio engine collaborators

| Unit | Role |
|------|------|
| `HardwareLoopOps` | Insert loop, latency buffer, capture / auto-detect |
| `MidiHostServices` | MIDI I/O, activity, sysex dump wait |
| `HostClockMetronome` | Host playhead, BPM, click |
| `MonitorOutputBridge` | Optional second CoreAudio device for playthrough / screen record |
| `HostAudioHelpers.h` | Shared message-pump / crossfade helpers |

`PluginAudioEngine` keeps the public API and forwards to these units.

### Capture & sessions

- **`CapturePipeline`** — software offline render, hardware record + progress, sysex dump, `HardwareModeGuard`, artifact paths
- **`SourceClipLibrary`** — fixtures/ library + external “Loaded” clips; **Plugins → Rescan Source Clips**
- **`TestCaseLoader`** + **`SessionSnap` read APIs** — apply a snapshot (clip / preset / sysex); load-testcase UI still minimal
- **`SessionArtifactSchema.h`**, **`HostFileUtils.h`**, **`HostDialog.h`**, **`AudioBufferUtils.h`** — shared naming / I/O / dialogs

### Other native / Python

- Hardware Audio Setup / MIDI Setup dialogs, VU meters, Lights Out tweaks
- Sysex registry + Quadraverb module
- Renderer / CLI compare targeting and AU identifier handling
- Generated `test_qdv1_exploration.py`
- `.gitignore`: ignore `native/plugin_host_app/build/` and host artefacts dirs

## Behaviour fixes worth calling out in review

1. **Startup plugin UI** — Editor is created once after the window is visible. Creating then destroying the editor during construction caused AUGenericView fallback (looked like the wrong / “Apple” UI while the dropdown still showed the config default).
2. **Second Capture Test Case crash** — Offline render no longer calls `releaseResources()` on the live engine plugin; uses non-realtime + device sample rate / channel layout. Fixed SIGSEGV in AU `renderGetInput` on second capture.
3. **Software vs hardware take length** — Offline render resamples to **device** rate so A/B files share SR and wall-clock length.
4. **Hardware Audio Setup monitoring** — With software effect muted, monitor crossfade forces the hardware return (previously silent in software mode).
5. **Capture Both progress dialog** — Clearer auto-stop copy; dialog hidden on finish. Hardware-only does **not** fall back to fixture duration (that cut reverb tails); it waits for silence or manual **Stop**. Dialog has **Stop** (save take) and **Cancel** (abort).
6. **Portable config** — Saving `presets_dir` / `default_preset` under the home directory writes `~/…` instead of `/Users/…`.

## Config / fixtures

- Prefer `presets_dir` as `~/Library/Audio/Presets/…` or omit it (host derives from manufacturer/name).
- New fixtures: drums/perc, piano, `sine_0db_1ch_5s_48k.wav`; supersaw trimmed.
- Local `host.config.json` may still be machine-specific (plugin list); bundled Resources config stays minimal (Matrix Reverb).

## How to verify after merge

```bash
cmake --build native/build --target plugin_host_app -j$(sysctl -n hw.ncpu)

APP="native/build/plugin_host_app/plugin_host_app_artefacts/Release/AU Effects Explorer.app"
open "$APP" --args --config "$(pwd)/host.config.json" --project-root "$(pwd)"
```

Checklist:

- [ ] Default plugin from config loads with **native** editor (not AUGenericView); dropdown matches instance
- [ ] Hardware Audio Setup → Test: hear loop return; VU moves; calibration still works
- [ ] Capture Test Case → **Both**: software + hardware files; progress auto-dismisses; second capture does not crash
- [ ] Capture Test Case → **Hardware** only: does not auto-stop at dry clip length; Stop saves / Cancel aborts; **Calibrate** (default on) sets silence gate from noise floor so reverb tails end cleanly
- [ ] Software and hardware WAVs same sample rate; dry content length aligns
- [ ] **Plugins → Rescan Source Clips** picks up new files under `fixtures/` without relaunch
- [ ] MIDI Setup / sysex dump (if hardware present) still works

## Not in this branch (follow-ups)

- Calibration Boost (multi-impulse, DC, L/R, LUFS) — deferred; hooks live on `HardwareLoopOps`
- Load Testcase menu/dialog — backend (`TestCaseLoader` / `SessionSnap`) ready; UI not finished

## Merge recommendation

Merge as a normal PR into `main` (not squash-only if you want to keep the commit story; squash is fine if you prefer one landing commit). Conflict risk is mostly `MainWindow.*`, `PluginAudioEngine.*`, `host.config.json`, and `VERSION` if `main` moved.
