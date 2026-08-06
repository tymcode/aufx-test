# Quadraverse

**Quadraverse** is a patch editor and librarian for the Alesis Quadraverb / Quadraverb Plus family. It moves patches between disk, hardware, and the Temecula DSP **QDV-1** plugin; imports and exports banks or individual programs; converts legacy `.ssx` banks; and supports realtime parameter editing with rapid sonic comparison between the different patches, on the plugin or on hardware.

It sits alongside [AU Effects Explorer](aufx-explorer.md) in the same project: Explorer is the general AU / hardware-in-loop testing applicaiton; Quadraverse is a Quadraverb-specific librarian and editor.  They both share many functions.

## What it does

- Load and save patches between **disk**, **hardware** (MIDI SysEx), and **QDV-1** (plugin state / user presets / `.aupreset`)
- Import a full SysEx bank, or load selected programs from a bank into editable working copies
- Export everything currently loaded as a bank (SysEx and/or QDV-1 presets)
- Convert legacy `.ssx` bank files into standard `.syx` (see [ssx-format.md](ssx-format.md))
- Edit parameters in realtime; **Send Patch** pushes the active edit to hardware and/or the plugin
- Toggle **Use Hardware** for A/B listening, and mark patch contexts for **Compare** / comparison reports
- Host QDV-1 in-app for immediate plugin audition while editing



## Terminology

Quadraverse draws a deliberate line between a **patch** and a **patch context**.

### Patch

A **patch** is a stored program: relatively static once written. Examples:

- A location on the Quadraverb’s front-panel memory
- A single-program or bank `.syx` dump
- A QDV-1 user preset or `.aupreset` file
- A program inside an imported bank (including one decoded from `.ssx`)

Patches are what you archive, share, and restore.

### Patch context

A **patch context** is what older editors usually called an *edit buffer*: a patch that has been **loaded into a working slot** where it can be manipulated.

In a context you can:

- Change parameters live (configuration, block modes, mix, modulation matrix, …)
- Rename, duplicate, or discard the working copy without touching the original store yet
- Send the current state to hardware or QDV-1
- Include or exclude the context from sonic **Compare**

You can keep several patch contexts open at once (A, B, C, …), switch among them, and compare parameters that differ. Edits apply to the **active** context.

A context is "dirty" when it has diverged from the last load/save. Saving writes a **patch** again (preset, sysex, device slot, and so on); the context remains the editable copy until you drop it.

### Bank

A **bank** is a collection of patches.

- **Import bank** — read a SysEx bank (or converted `.ssx`) and choose any or all programs to load into new patch contexts
- **Save bank** — export **everything currently loaded as a patch context** as a SysEx bank and/or as QDV-1 presets

So: patches live in files and devices; patch contexts are the live set you are working on; a saved bank is a snapshot of those contexts (or of device/plugin memory, when dumping hardware).

## Typical workflows



### Edit a factory or user patch

1. **Load Patch…** from device, plugin, preset file, or sysex dump → creates / fills a patch context.
2. Edit in the patch editor (realtime SysEx parameter edits to hardware when connected; QDV-1 follows the same program model).
3. **Send Patch** when you want an explicit full push, or rely on live edits while auditioning.
4. **Save Patch** as preset and/or sysex when you are happy with the result.



### Bring in a bank

1. **Import Sysex Bank…**, or **Convert SSX…** then work from the resulting `.syx`.
2. Pick specific programs, or load the whole bank into patch contexts.
3. Edit, compare, and re-export with **Save Bank** when done.



### Hardware vs plugin comparison

When you say "send patch" Quadraverse will send it to whatever target is in the Target view, whether it's hardware or the plugin.

1. Configure MIDI and hardware audio (same idea as Explorer’s insert loop).
2. Load one or more patches into a patch context; enable **Compare** on the contexts you care about.
3. Use **Use Hardware** to flip the monitor path; open **Comparison Report…** for a structured diff / listen pass.
4. Differing parameters across compared contexts can be inspected and copied section-by-section in the editor.



### Round-trip with QDV-1

- 



## Project files

Quadraverse projects remember open patch contexts, sources, and compare flags so you can leave a working set and return to it. Use **New / Open / Save Project** for that session state; use Load/Save Patch and Save Bank for the portable patch and bank artifacts.

## Device support


| Target                   | Role                                                                       |
| ------------------------ | -------------------------------------------------------------------------- |
| Alesis Quadraverb / Plus | Primary hardware via SysEx dump, restore, and live parameter edit          |
| Temecula DSP QDV-1       | Hosted AU; presets and `.aupreset`; shared program model with the hardware |
| `.ssx`                   | Import/convert only (raw bank SysEx under another extension)               |
| Quadraverb 2             | Profile stub; full support later                                           |




## Related docs

- [ssx-format.md](ssx-format.md) — what `.ssx` actually is and how conversion works
- [quadraverb_sysex.html](quadraverb_sysex.html) — QV1/Plus SysEx reference
- [QDV1-preset-api/](QDV1-preset-api/README.md) — QDV-1 parameter map and state serialisation
- [aufx-explorer.md](aufx-explorer.md) — general AU host / hardware-in-loop capture



## Build

From `native/`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target Quadraverse -j
cmake --build build --target quadraverse_tests -j
./build/quadraverse_app/quadraverse_tests_artefacts/Debug/quadraverse_tests
```

App: `build/quadraverse_app/Quadraverse_artefacts/Debug/Quadraverse.app`