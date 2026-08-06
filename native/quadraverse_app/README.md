# Quadraverse

Patch editor / librarian for Alesis Quadraverb / Plus, Temecula DSP QDV-1 and hardware audition via SysEx.

Product overview, terminology (patch vs patch context vs bank), and workflows:
**[docs/quadraverse.md](../../docs/quadraverse.md)**.

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

- [docs/quadraverse.md](../../docs/quadraverse.md) — overview and workflows
- `docs/QDV1-preset-api/` — QDV-1 parameter map and state serialisation
- `docs/ssx-format.md` — `.ssx` = raw bank SysEx
