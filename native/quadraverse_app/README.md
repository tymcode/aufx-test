# Quadraverse

JUCE GUI app for editing Alesis Quadraverb / Plus patches, hosting Temecula DSP
QDV-1, and auditioning to hardware via SysEx.

## Build

From `native/`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target Quadraverse -j
cmake --build build --target quadraverse_tests -j
./build/quadraverse_app/quadraverse_tests_artefacts/Debug/quadraverse_tests
```

App: `build/quadraverse_app/Quadraverse_artefacts/Debug/Quadraverse.app`

## Docs

- `docs/QDV1-preset-api/` — QDV-1 parameter map and state serialisation
- `docs/quadraverb_sysex.html` — QV1/Plus SysEx
- `docs/Alesis QuadraVerb 2 MIDI_SYSEX.html` — QV2 (profile stub; full support later)
- `docs/ssx-format.md` — `.ssx` = raw bank SysEx
