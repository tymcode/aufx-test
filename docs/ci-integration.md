# CI / Build Integration (Future Phase)

This document outlines how to wire `aufx-test` into your plugin build pipeline.

## Architecture

```
┌─────────────────┐     ┌──────────────────┐     ┌─────────────────┐
│  JUCE Plugin    │────▶│  Headless Host   │────▶│  output.wav     │
│  (.vst3 / .au)  │     │  (CLI wrapper)   │     │                 │
└─────────────────┘     └──────────────────┘     └────────┬────────┘
                                                          │
                        ┌──────────────────┐              │
                        │  aufx-test│◀─────────────┘
                        │  (Python)        │
                        └────────┬─────────┘
                                 │
                        ┌────────▼─────────┐
                        │  pytest / CI     │
                        │  pass/fail +     │
                        │  artifact plots  │
                        └──────────────────┘
```

## Step 1: Headless plugin renderer

Build a small JUCE command-line app (or extend your plugin project) that:

1. Loads the plugin binary
2. Reads input WAV from `--input`
3. Sets parameters from `--param name=value` flags
4. Renders offline and writes `--output`

Implement `PluginHost` in Python to call this CLI:

```python
import subprocess
from aufx_test import Waveform, PluginHost

class SubprocessPluginHost:
    def __init__(self, renderer_bin: str, plugin_path: str):
        self.renderer_bin = renderer_bin
        self.plugin_path = plugin_path
        self._params: dict = {}

    def set_parameters(self, params):
        self._params.update(params)

    def process(self, input_waveform: Waveform) -> Waveform:
        input_waveform.to_file("/tmp/test_in.wav")
        args = [self.renderer_bin, "--plugin", self.plugin_path,
                "--input", "/tmp/test_in.wav", "--output", "/tmp/test_out.wav"]
        for k, v in self._params.items():
            args.extend(["--param", f"{k}={v}"])
        subprocess.run(args, check=True)
        return Waveform.from_file("/tmp/test_out.wav")
```

## Step 2: Reference fixtures

Store golden-reference WAV files in `fixtures/` (or `tests/references/`). Regenerate references intentionally when sound changes:

```bash
./plugin_renderer --plugin MyEffect.vst3 --input fixtures/sine.wav \
    --param mix=0.5 --output fixtures/refs/mix_half.wav
```

## Step 3: pytest in CI

```yaml
# .github/workflows/audio-tests.yml
jobs:
  audio-tests:
    runs-on: macos-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with:
          python-version: "3.12"
      - run: pip install -e ".[dev]"
      - run: cmake --build build --target PluginRenderer
      - run: pytest tests/ --junitxml=results.xml
      - uses: actions/upload-artifact@v4
        if: always()
        with:
          name: test-plots
          path: output/
```

## Step 4: Threshold tuning

Start with loose thresholds and tighten as references stabilize:

```python
THRESHOLDS = ComparisonThresholds(
    correlation_min=0.98,
    rms_error_max=0.02,
    spectral_distance_max=0.08,
)
```

Non-deterministic plugins (randomized noise, analog modeling) may need longer averaging windows or statistical comparisons across multiple renders.

## Step 5: JavaScript graphing (optional)

For web-based dashboards, export metrics JSON from pytest and render with Chart.js or D3. The Python graphing module can write JSON sidecars alongside PNG plots for this purpose.
