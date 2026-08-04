# QDV1 — preset and parameter code

Reference source for how QDV1 stores presets, serialises its state, and
addresses parameters. Excerpted from the plugin; it is not a buildable
project and is not meant to compile stand-alone.

## Files

| File | What it is |
|---|---|
| `QvPresetStore.h` / `.cpp` | The whole preset layer: factory + user presets, tags, and all disk I/O. Complete and self-contained (JUCE only). |
| `QvStateSerialisation.cpp` | The two functions that save and restore plugin state. The same blob is what a user preset stores. Excerpt. |
| `QvParameters.h` | The parameter map: every configuration, block, mode and page, with ranges, defaults and display wording. |
| `MidiLearn.h` | Stable identity for a MIDI-mappable control, and the range lookup a 0–127 controller is scaled into. |

## How the pieces fit

A parameter is addressed as a **(function, page)** pair — the same address the
hardware's front panel and its SysEx parameter edit use. Which pages exist at
any moment depends on two things:

1. the **configuration** (function 7, page 0 — eight of them), and
2. each block's **mode**, which is that block's own page 0.

`QvParameters.h` is the table that answers "what exists right now". An edit
sent to a page the current configuration doesn't have is silently dropped.

Presets come in two kinds:

- **Factory** — the unit's 100 stored programs. Read-only; loading one is a
  program change. Users may tag them but not rename or delete them.
- **User** — a saved snapshot of the plugin state blob, stored as one JSON
  file per preset with the blob base64'd inside it.

## On-disk layout

Base directory: `~/Library/Application Support/TemeculaDSP/QDV1`
(overridable via the `QDV1_PRESET_DIR` environment variable).

```
presets/<uuid>.json    one file per user preset:
                       { "id", "name", "tags":[..], "state":"<base64 blob>" }
preset_meta.json       factory tag overrides, keyed by id ("factory:12");
                       written only when they differ from the built-in defaults
preset_tags.json       the tag catalog + the one-shot defaults-seeded flag
```

`QvPresetStore` is message-thread only and does no internal locking. It is a
`juce::ChangeBroadcaster`; the browser UI listens and rebuilds on every
mutation.

## Ranges

Parameter ranges in `QvParameters.h` are the unit's own clamps, measured on
the hardware rather than taken from the published documentation — the two
disagree in eight places.

## Not included

The audio engine and everything that drives it. This package is the preset,
state and parameter-addressing layer only.
