# Retro forces ray tracing and MetalFX off (TASK197, slice 1; plan call P8)

Stacked on TASK185 (PM3, `7550c9bc`). Slice 1 only: the overlay and the menu. The cost work (the copy of the
view, the filter's neighbour reuse, the 1080p and 5K timing with the screen unlocked) is not in this slice and
is left for a child card.

## What changed

- **The Retro table** (`src/client/refresh/metal/rm_rtgate.c`). `rm_pres_value` now looks a name up in the table
  of the mode: Enhanced's (unchanged) or the new Retro table. Retro reads `r_rt_shadows`, `r_rt_sun`, `r_rt_ao`,
  `r_rt_water`, `r_rt_gunshadow` and `r_metalfx` as 0. Every other name, the sliders of the effects included,
  stays the user's: with the switches at 0 no effect runs, so the sliders do nothing. No cvar is written; the
  overlay returns a value and stores nothing. `rm_effect_overlaid` reports a name in the mode's table, so
  `RM_FX_Read` sees `r_metalfx` as set by the presentation, as under Enhanced.
- **`RM_FX_Begin`'s early return under Retro is kept.** It is what gives Retro its 480-line view size with no
  scaler (TASK185), and it also skips `RM_FX_Read`; the overlay is the second line of the same rule (a later
  `RM_FX_Read` or a scaler path reads `r_metalfx` as 0 under Retro) and does not replace it.
- **The report line** for Retro now ends with what it overrides: `...; r_rt_shadows, r_rt_sun, r_rt_ao,
  r_rt_water, r_rt_gunshadow and r_metalfx read 0`.
- **The menu** (`src/client/menu/vidrules.c`, `videomenu.c`, `vidrules.h`). `VidRules_RTOverridden` is true for
  the five switches under Retro (on Metal, with or without ray tracing, since Retro needs none);
  `VidRules_RTEffective` gives 0 for them; `VidRules_RTRow` greys the switches and the tuning rows of the
  effects "set by the presentation". The Tuning screen greys the scaler and the effects' rows the same way
  (`VidRules_TuneOverridden`). Flat light, the debug view, the presentation, HDR and the presets stay live.
  The Ray tracing screen's status bar says `Retro reads the effects and the scaler as off`. Disable, never hide.
  Reset to defaults keeps the five switches under Retro, as it keeps Enhanced's four.
- `doc/040_cvarlist.md`'s `r_presentation` entry names the Retro overrides.

## Tests

- `tests/metal/rm_rtgate_test.c` (run by `tests/check-ref-metal.sh`): Retro's overlay for every gate cvar and
  for `r_metalfx`, `rm_effect_overlaid`, the report line; `tests/metal/rm_pres_table_test.c`: Retro reads the
  names of the preset fixture as 0 where it decides them, as the user's elsewhere.
- `tests/menu/videomenu_test.c` (`tests/check-video-menu.sh`), `test_screens_under_retro`: the five switches and
  the seven tuning rows of the Ray tracing screen, and on the Tuning screen the scaler and four rows, greyed "set by the presentation", showing off, no cvar written, on both screens; the
  scaler live at mode 0. `tests/unit/test_vidrules.c`: the rules.
- **Mutants, red then green.**
  - `r_rt_sun` dropped from the Retro table: `check-ref-metal` fails `rm_rtgate_test.c:484` (three times),
    `:502 rm_pres_value(RM_PRES_RETRO, retro_off[k], 3.0f) == 0.0f` and `:503 rm_effect_overlaid(...)`, last line
    `rm_rtgate_test: 854 checks, 5 failed`. Restored: `check-ref-metal: ok (13159 checks, 0 failed; ...)`.
  - The menu's Retro switch rows narrowed from five to four (the gun shadow left live): `videomenu_test.c:1106`,
    `gun shadow under Retro: greyed 0, applies from the next frame`, `videomenu_test: 3318 checks, 1 failed`.
    Restored: `videomenu_test: 3352 checks, 0 failed`.

## Proof

- `make client game ref_metal ref_soft`: exit 0.
- `tests/check-ref-metal.sh`, last line: `check-ref-metal: ok (13159 checks, 0 failed; ray tracing 165 checks, 0
  failed; ...; ref_metal built)`.
- `tests/check-video-menu.sh`, last line: `check-video-menu: ok`.
- `make test`, last line: `yq2test: 444 tests, 0 failed`. (The runner prints a `FAIL runner_expect_exit_signalled`
  block: that is the runner's own test of a child that dies by a signal, and it counts as a pass.)
- One note on the first `make test` of the holds: it failed `vidrules_rt_status_names_the_presentation`
  (`tests/unit/test_vidrules.c:857`), because the Retro status string first contained the word `overrides`, which
  that check keys on for Enhanced. The string was reworded and the check is green.

## Pins

Every pin was recorded seeded (`PINS_SEED=1`) on Metal at 640 x 480 by the branch and compared by the hash of
the frames' hashes with TASK185's records (the branch of `7550c9bc`; that commit adds only a runbook line to the
code the records were made from). All at `r_presentation 0`, 23 frames unless noted.

| Scenario | Hash | Result |
|---|---|---|
| `world` | `b15b40cf1604` | identical |
| `world-fullbright` | `6e92dc9a2a96` | identical |
| `world-lmcubic` | `1a8ff9d43e26` | identical |
| `world-scale8` | `b611e93476e1` | identical |
| `world-metalfx1` | `8bb165628bea` | identical |
| `world-metalfx2`, `world-metalfx3` | `15df2f0d4ca4` | identical |
| `world-metalfx-interp` | `b15b40cf1604` | identical |
| `hud2d`, `hud2d-sharp` | `f9ffe0290d69` | identical |
| `hud2d-scale8` | `32724f15ca55` | identical |
| `models` | `552ee3cb16b8` | identical |
| `particles`, `particles-cap` | `75b26afbad69` | identical |
| `dlights` | `ac67e8f5ce7a` | identical |
| `dlights-lmcubic` | `47b37c13760d` | identical |
| `rt-ao` | `53c06d8a47ad` | identical |
| `rt-ao-entities` | `f1a43c24854f` | identical |
| `rt-shadows` | `bb85d7a96bf0` | identical |
| `rt-shadows-entities` | `6cd65dd54373` | identical |
| `rt-sun` | `374691775071` | identical |
| `hdr` | `11b7dcc7968c` | identical |
| `hdr-metalfx2` | `966095662f89` | identical |
| `cinematic` (12 frames, `idlog.cin`, `PINS_EVERY=7`) | `83cda5a3ee9b` | identical |
| `skywater` (4 frames) | `bd6e66d3c43a` | identical |
| `rt-water`, `rt-water-entities` (4 frames) | `60b1e91c2456` | identical |

All 27 stock scenarios are byte-identical, so no pin at `r_presentation 0` or `1` changed (the Enhanced table is untouched and its host tests are green).

### The Retro scenarios

TASK185's stored Retro records are older than its final code (the store directory is `820752ea`), so they were
not a fair reference. The Retro scenarios were recorded again by a `ref_metal` built from `7550c9bc`'s
`rm_rtgate.c` (the only renderer file this card changes) and by the branch, in one release directory:

| Scenario | 7550c9bc | branch | Result |
|---|---|---|---|
| `retro` | `ba8fd41fb1c5` | `ba8fd41fb1c5` | identical, 23 frames |
| `retro-filter` | `13e096a71b18` | `78660fd859cd` | 22 of 23 frames identical, frame 12 differs (13458 bytes) |

`retro` does not change, as expected: its scenario sets no `r_rt_*` effect and no scaler, so the overlay reads
0 where the user's value was 0. For `retro-filter` one frame of the 23 differs. This card's change does not touch
the filter, the pass or the scenario's settings, and `retro` (the same demo and frames, no filter) is identical;
the branch's record is the same in two separate runs (`78660fd859cd` twice). The base was recorded once, so
whether frame 12 is a nondeterministic frame of the filter pass (the pass reads a scratch copy of the view) or
something the overlay does is not settled by this slice; it is an open item for the child card, which should
record the base two or three times. No pin was re-recorded and no reference of the store was replaced.

## What is left

The cost work, as the card says (the copy of the view, the filter's neighbour reuse, 1080p and 5K timing with
the screen unlocked), and the `retro-filter` frame 12 question above.

## Holds

Three runs of the heavy lock were used, one over the two the brief allowed: the second failed at once (the
build was out of date after a menu change, so `pins.sh record` refused), and the third reran what it was meant to
do and added the Retro base build. The artifacts are in the TASK197 folder of the artifacts store; no frame was viewed.

## Review round 1 (menu)

- I1: the Tuning screen's scaler row shows `r_metalfx` as the renderer reads it under Retro (0), not the user's
  value. Red: `videomenu_test.c:1124 ... I1: scaler row under Retro shows 2, the renderer reads 0`; green after
  `TuneMenu_Values` shows `TuneIndex(c, 0)` for an overridden `r_metalfx` row.
- N2: the main video menu's "MetalFX scaler" and "MetalFX scale" items are greyed "set by the presentation" under
  Retro (`SetRule` in `videomenu.c` asks the new `VidRules_MetalFXOverridden`, so `VidRules_Item`'s signature is
  unchanged) and Apply skips a greyed item. Red: `videomenu_test.c:1143 ... scaler greyed 0, scale greyed 0`.
- N3: "reset to defaults" under Retro skips every row greyed "set by the presentation", tuning rows included.
  Red: `videomenu_test.c:1134 ... reset under Retro wrote a greyed row: range 128, floor 0.5`.
- N4 (not fixed, predates this card): the menu reads `r_presentation` live, while the renderer latches it at
  `RI_Init` or `vid_restart`, so the Retro greying shows before Apply while the renderer still runs the old mode.
