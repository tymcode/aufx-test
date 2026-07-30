# Plugin renderer CLI contract

The reference implementation lives in [`native/plugin_renderer/`](../native/plugin_renderer/). Build instructions: [`native/README.md`](../native/README.md).

Your JUCE headless host should implement this interface so `SubprocessPluginHost` can drive it from Python tests.

## Minimal invocation

```bash
plugin_renderer \
  --plugin "/Library/Audio/Plug-Ins/Components/MyEffect.component" \
  --input  /tmp/input.wav \
  --output /tmp/output.wav \
  --preset /path/to/state.aupreset
```

State is loaded from the `.aupreset` first. Optional `--param` flags apply overrides afterward.

After the input WAV finishes, the renderer feeds silence and continues until the plugin output stays below `--silence-threshold-db` for `--tail-silence` seconds (default 1s). A `--max-tail` safety limit prevents runaway renders.

## Required flags

| Flag | Description |
|------|-------------|
| `--plugin PATH` | VST3 bundle, AU `.component`, or path your host understands |
| `--input PATH` | Input WAV (any channel count/sample rate your host supports) |
| `--output PATH` | Output WAV written after offline render |
| `--preset PATH` | Audio Unit preset (`.aupreset`) containing plugin state blob |

## Optional flags

| Flag | Description |
|------|-------------|
| `--param NAME=VALUE` | Override a single parameter after preset load (repeatable) |
| `--sample-rate HZ` | Force processing sample rate |
| `--block-size N` | Processing block size (default 512) |
| `--tail-silence SECS` | After input ends, keep rendering until output is silent for this long (default 1.0) |
| `--silence-threshold-db DB` | Silence detection threshold in dBFS (default -60) |
| `--max-tail SECS` | Safety cap on tail length after input ends (default 120) |
| `--dump-parameters` | Print parameter metadata instead of rendering |
| `--format json` | Use with `--dump-parameters` for machine-readable output |

## Exit codes

| Code | Meaning |
|------|---------|
| `0` | Success |
| non-zero | Failure (write details to stderr) |

## Loading .aupreset in JUCE

Inside your renderer, after instantiating the plugin:

1. Read the plist from the `.aupreset` file
2. Extract the `data` field (NSData bytes) — this is the AU state blob
3. Call `AudioProcessor::setStateInformation(data, size)` (or the AU equivalent via your `AudioPluginInstance`)

For JUCE-wrapped AUs, the plist may use a `jucePluginState` key instead of `data`. Support both.

```cpp
void loadAUpreset(juce::AudioPluginInstance& plugin, const juce::File& presetFile)
{
    auto xml = juce::XmlDocument::parse(presetFile);  // or parse plist
    // Extract state bytes from <data> or <jucePluginState>
    plugin.setStateInformation(stateBytes.getData(), (int) stateBytes.getSize());
}
```

After loading state, call `prepareToPlay(sampleRate, blockSize)` before processing.

## Capturing .aupreset from a DAW

### Logic Pro
1. Load your AU plugin on a track
2. Configure the sound you want to test
3. Click the plugin's preset menu → **Save As AU Preset…**
4. Save as `MySound.aupreset`
5. Pass to the framework:

```bash
aufx-test session snap "MyEffect" "half mix" \
  --input fixtures/sine.wav \
  --output ~/Desktop/bounce.wav \
  --aupreset ~/Library/Audio/Presets/MyEffect/MySound.aupreset
```

### Sharing with the developer

```bash
aufx-test session export-presets "MyEffect" -o share/with-developer/
```

This produces:

```
share/with-developer/
  manifest.json          # snapshot metadata, notes, input/output paths
  presets/
    abc123_half_mix.aupreset
    def456_bypass.aupreset
```

Send the whole folder to the developer. They can load any `.aupreset` in Logic or your renderer to reproduce the exact state.

## Python usage

```python
from aufx_test import SubprocessPluginHost, Waveform

with SubprocessPluginHost(
    renderer_bin="build/plugin_renderer",
    plugin_path="/path/to/MyEffect.component",
) as host:
    host.load_preset("sessions/demo/artifacts/abc123_half_mix/abc123_half_mix.aupreset")
    output = host.process(Waveform.from_file("fixtures/sine.wav"))
    output.to_file("/tmp/rendered.wav")
```

## pytest fixture

```python
# tests/conftest.py
import pytest
from aufx_test import SubprocessPluginHost

@pytest.fixture(scope="session")
def plugin_host():
    with SubprocessPluginHost(
        renderer_bin="build/plugin_renderer",
        plugin_path="build/MyEffect_artefacts/Release/AU/MyEffect.component",
    ) as host:
        yield host
```

Generated tests from `session export` call `host.load_preset(setup["preset_file"])` automatically.
