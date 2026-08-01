# Native hosts (JUCE)

The `native/` tree builds two macOS tools used by [aufx-test](../README.md):

| Target | What it is |
|--------|------------|
| **`plugin_renderer`** | Headless CLI that loads an AU, applies an `.aupreset`, renders a WAV offline, and exits |
| **AU Effects Explorer** (`plugin_host_app`) | GUI host for listening, hardware-in-loop, and capturing test cases — see [docs/aufx-explorer.md](../docs/aufx-explorer.md) |

Both share JUCE 8.0.14 (`native/JUCE` submodule), AU loading, and `.aupreset` state handling. Python drives the renderer through `SubprocessPluginHost`; the Explorer is interactive.

## `plugin_renderer` — when and why

Use **`plugin_renderer`** when you need a **deterministic, non-interactive** render of *software* AU output for automation:

* `pytest` / CI replay of promoted session setups
* `aufx-test compare-batch` (dry → preset → golden)
* Any script that must produce the same WAV from the same input + `.aupreset` without opening a GUI

Prefer it over:

| Alternative | Why not (for automation) |
|-------------|---------------------------|
| **AU Effects Explorer** | Built for listening, HIL, MIDI, and capture—not batch CI. It *creates* the presets and goldens the renderer later replays. |
| **`aufx-test explore` REPL** | Interactive prompts around DAW bounces; no offline AU host of its own. |
| **DAW offline bounce** | Great for capture, awkward to script bit-identical CI runs across machines. |
| **pluginval / other validators** | Validate plugin correctness; they are not the aufx-test render contract (`SubprocessPluginHost`). |

In short: **Explorer (or a DAW) to capture state and references; `plugin_renderer` to replay that state headlessly.** Hardware paths are never rendered here—only the AU software path.

Full CLI contract (flags, tails, exit codes): [docs/plugin-renderer.md](../docs/plugin-renderer.md).

### Features

* Offline AU render: `--plugin` + `--input` + `--output` + `--preset`
* Optional `--param name=value` overrides after preset load
* Silence-gated reverb/delay tails (`--tail-silence`, `--silence-threshold-db`, `--max-tail`)
* `--dump-parameters` / `--save-preset` helpers for inspection and round-trips
* Consumed by Python `SubprocessPluginHost` (same flags every time)

## Requirements

* macOS 13+ (AU hosting)
* CMake 3.22+
* C++17 toolchain (Xcode Command Line Tools)
* [JUCE](https://github.com/juce-framework/JUCE) **8.0.14** (git submodule at `native/JUCE`)

## Setup JUCE

Preferred (already configured in this repo):

```bash
git submodule update --init --recursive
# or: ./scripts/bootstrap.sh
```

The pinned checkout is `native/JUCE` @ tag **8.0.14**.

Alternatively, point CMake at any checkout:

```bash
cmake -S native -B native/build -DJUCE_PATH=/path/to/JUCE
```

A sibling `../JUCE` next to the repo is still auto-detected if the submodule is missing.

## Build

From `aufx-test/`:

```bash
cmake -S native -B native/build -DCMAKE_BUILD_TYPE=Release
cmake --build native/build --target plugin_renderer
cmake --build native/build --target plugin_host_app
```

Or use `./scripts/bootstrap.sh` for venv + both targets.

Binaries:

```text
native/build/plugin_renderer/plugin_renderer_artefacts/Release/plugin_renderer

native/build/plugin_host_app/plugin_host_app_artefacts/Release/AU Effects Explorer.app
```

### Packaging the Explorer app

```bash
./scripts/package_mac_app.sh
# → dist/AU-Effects-Explorer-macOS.zip  (universal arm64 + x86_64)
```

Prebuilt Explorer builds are also published on [GitHub Releases](https://github.com/tymcode/aufx-test/releases). Day-to-day GUI workflow: [docs/aufx-explorer.md](../docs/aufx-explorer.md).

## Usage (`plugin_renderer`)

```bash
plugin_renderer \
  --plugin "/Library/Audio/Plug-Ins/Components/MyEffect.component" \
  --input fixtures/synth_waves/sine.wav \
  --output /tmp/out.wav \
  --preset sessions/demo/artifacts/abc123_half_mix.aupreset
```

Useful optional flags:

```bash
  --param mix=0.5            # override after preset load (repeatable)
  --sample-rate 48000
  --block-size 512
  --tail-silence 1.0
  --silence-threshold-db -60
  --max-tail 120
  --dump-parameters --format json
  --save-preset /tmp/roundtrip.aupreset
```

JUCE identifiers (e.g. `AudioUnit:Effects/aumf,DPPL,TDSP`) work as `--plugin` values the same way `host.config.json` stores them.

## Wire into Python tests

```python
# tests/conftest.py
import pytest
from pathlib import Path
from aufx_test import SubprocessPluginHost

ROOT = Path(__file__).resolve().parents[1]
RENDERER = ROOT / "native/build/plugin_renderer/plugin_renderer_artefacts/Release/plugin_renderer"

@pytest.fixture(scope="session")
def plugin_host():
    with SubprocessPluginHost(
        renderer_bin=RENDERER,
        plugin_path="/path/to/MyEffect.component",
    ) as host:
        yield host
```

Then run exported session tests (locally generated under `tests/generated/`, gitignored):

```bash
pytest tests/generated/test_my_effect.py
```

Generated tests skip cleanly when local session artifacts or plugins are missing. Batch goldens use the same binary via `aufx-test compare-batch`.

## `.aupreset` loading

On macOS, presets are parsed with `NSPropertyListSerialization` (supports Logic’s binary plists). The state blob is read from the `data`, `jucePluginState`, or `state` key and passed to `AudioProcessor::setStateInformation()`.

On other platforms, XML `.aupreset` plists are supported via JUCE’s XML parser.

## Launching the Explorer (quick reference)

```bash
APP="native/build/plugin_host_app/plugin_host_app_artefacts/Release/AU Effects Explorer.app"
open "$APP" --args --config "$(pwd)/host.config.json" --project-root "$(pwd)"

# Optional wrapper (needs .venv):
# aufx-test host
```

Full GUI documentation: [docs/aufx-explorer.md](../docs/aufx-explorer.md).
