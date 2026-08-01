# Exporting DEEP/Z factory presets for `compares/deepz`

`compare-batch` pairs goldens with `presets/au/{NN}-{Effect Name}.aupreset`.
DEEP/Z does **not** expose factory effects as AU programs or automatable
parameters, so they cannot be dumped headlessly from `plugin_renderer`.

## Manual export (required for missing numbers)

1. Open **AU Effects Explorer** (or Logic) and load **DEEP/Z**.
2. Select each missing factory effect (see list below).
3. Save an AU preset (Logic: plugin menu → **Save As AU Preset…**; host: use your usual preset save).
4. Import into the compares tree:

```bash
# From a folder of newly saved .aupreset files:
python3 scripts/import_deepz_aupresets.py ~/Desktop/deepz-exports --force

# Or scan Temecula’s preset library:
python3 scripts/import_deepz_aupresets.py --from-library --force
```

5. Re-check coverage:

```bash
aufx-test compare-batch compares/deepz --list
```

## Currently missing (as of last check)

`04, 06, 09, 15, 17, 23, 25–31, 43, 45–48, 51, 55, 70, 72, 73, 75`

| # | DEEP/Z effect |
|---|---|
| 04 | Intimacy Hall |
| 06 | Regal Hall |
| 09 | Grand Ensemble Hall |
| 15 | Drum Set Plate |
| 17 | Strummer's Plate |
| 23 | Non-Linear Drums |
| 25 | Bigger Room |
| 26 | Mid-Size Room |
| 27 | Smaller Room |
| 28 | Very Tiny Room |
| 29 | Solid Wood Room |
| 30 | Stonewalled Room |
| 31 | Soft-Walled Room |
| 43 | Live Drum Chamber |
| 45 | Session Drums |
| 46 | Vintage Brown & Swirl |
| 47 | Percussive Plate |
| 48 | Snare Splash |
| 51 | Spatialized Delays |
| 55 | Random Delays |
| 70 | Psychedelic Flange |
| 72 | Rhythmic Flange |
| 73 | Multi-Phaser |
| 75 | Rhythmic Phaser |

(`53 Dark-Matter Delays` was imported from `~/Library/Audio/Presets/Temecula DSP/DEEP/Z/Anti-Matter Delays.aupreset`.)

## Renderer helpers (related)

`plugin_renderer` now supports:

```bash
plugin_renderer --plugin 'AudioUnit:Effects/aumf,DPPL,TDSP' --list-programs
plugin_renderer --plugin '...' --preset IN.aupreset --save-preset OUT.aupreset
```

`--list-programs` only reports the AU’s single “Untitled” program — factory
effects live inside the DEEP/Z state chunk, not the AU program list.
