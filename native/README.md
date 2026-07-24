# plugin_renderer (JUCE)

Headless CLI host for offline plugin rendering in automated tests. Implements the contract consumed by Python's `SubprocessPluginHost`.

## Prerequisites

- CMake 3.22+
- A C++17 compiler (Xcode on macOS)
- [JUCE](https://github.com/juce-framework/JUCE) 7 or later

## Setup JUCE

This project auto-detects a sibling checkout at `../JUCE` (e.g. `~/dev/JUCE` next to `~/dev/aufx-test`).

Alternatively, clone JUCE as a submodule:

```bash
git submodule add https://github.com/juce-framework/JUCE.git native/JUCE
git submodule update --init --recursive
```

Or point CMake at any checkout:

```bash
cmake -S native -B native/build -DJUCE_PATH=/path/to/JUCE
```

## Build

From `aufx-test/` (with `~/dev/JUCE` as a sibling, no extra flags needed):

```bash
cmake -S native -B native/build -DCMAKE_BUILD_TYPE=Release
cmake --build native/build --target plugin_renderer
cmake --build native/build --target plugin_host_app
```

The headless renderer binary:

```
native/build/plugin_renderer/plugin_renderer_artefacts/Release/plugin_renderer
```

The GUI plugin host app (manual exploration):

```
native/build/plugin_host_app/plugin_host_app_artefacts/Release/AU Effects Explorer.app
```

Launch without Python (recommended):

```bash
APP="native/build/plugin_host_app/plugin_host_app_artefacts/Release/AU Effects Explorer.app"
open "$APP" --args --config "$(pwd)/host.config.json" --project-root "$(pwd)"
```

Optional Python wrapper (requires a working `.venv`):

```bash
aufx-test host
```

See [docs/manual-exploration.md](../docs/manual-exploration.md) if venv activation fails with permission errors.
## Usage

```bash
plugin_renderer \
  --plugin "/Library/Audio/Plug-Ins/Components/MyEffect.component" \
  --input fixtures/sine.wav \
  --output /tmp/out.wav \
  --preset sessions/demo/artifacts/abc123_half_mix.aupreset
```

Optional flags:

```bash
  --param mix=0.5            # override after preset load (repeatable)
  --sample-rate 48000
  --block-size 512
  --dump-parameters --format json
```

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

Then run exported session tests:

```bash
pytest tests/generated/test_my_effect.py
```

## .aupreset loading

On macOS, presets are parsed with `NSPropertyListSerialization` (supports Logic's binary plists). The state blob is read from the `data`, `jucePluginState`, or `state` key and passed to `AudioProcessor::setStateInformation()`.

On other platforms, XML `.aupreset` plists are supported via JUCE's XML parser.

## Integrate with your plugin project

You can either:

1. **Keep this as a standalone tool** in `aufx-test/native/` (simplest to start), or
2. **Add `add_subdirectory()` from your plugin's CMake** and install `plugin_renderer` next to your AU/VST3 build outputs.

For option 2, expose the path to CI and point `SubprocessPluginHost` at the built binary.

See also [docs/plugin-renderer.md](../docs/plugin-renderer.md).
