# `.ssx` format (Quadraverb)

## Summary

`.ssx` files found in the wild (including factory banks from quadraverb.nl /
Quad Patcher distributions) are **raw Alesis Quadraverb SysEx bank dumps**
under a different file extension. They are not a proprietary librarian
container and are not base64-encoded.

## Layout (verified against `fixtures/ssx/QV1.SSX`, 14708 bytes)

```
F0 00 00 0E 02      Alesis + Quadraverb product ID
02                  opcode: Load Program
65                  pp = 101 (all programs)
[147 bytes × 100]   encoded program payloads
F7
```

Each program uses the documented Alesis continuous MSB-first 8↔7-bit packing
(7 data bytes → 8 MIDI bytes), restarted at every 147-byte boundary.
Decoded size per program: **128 bytes**.

Patch names live at decoded bytes **106–119** (14 ASCII characters).

## Import behaviour (Quadraverse)

1. Validate header / length.
2. Decode all programs and hydrate known `(function, page)` values.
3. Write a `.syx` into the project patch-save directory (user-visible),
   optionally as **individual** Load Program messages (more compatible with
   some hardware / SysEx Librarian workflows than a single `pp=0x65` bank).
4. Patch contexts work from the `.syx`, never the `.ssx`.

## Non-goals

Quadraverse does not write `.ssx`. Export is `.syx` and/or QDV-1 presets /
`.aupreset`.
