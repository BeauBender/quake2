# TASK237: the menu and ref_metal share one presentation-overlay rule

Architecture review finding AR-02 ([window 140](../reviews/2026-11-26-window-140.md)): the renderer
decided which ray-tracing controls each presentation mode overrides from tables in
`src/client/refresh/metal/rm_rtgate.c`, and the video menu re-derived the same outcome in code in
`src/client/menu/vidrules.c` (row indexes `i < 4`, `i <= RT_GUNSHADOW`, `i == RT_WATER`). No test
compiled both, and `rm_rtgate.h` still said Retro decides nothing, though since TASK197 it decides six
cvars.

## The shared table

- **One table.** `src/client/refresh/metal/common/rm_pres_table.h` holds `RM_PRES_STOCK` to
  `RM_PRES_RTPRIMARY`, `RM_PRES_COUNT`, `rm_pres_row_t`, each mode's rows as the macros
  `RM_PRES_ENHANCED_ROWS`, `RM_PRES_RETRO_ROWS` and `RM_PRES_RTPRIMARY_ROWS`, and three `static
  inline` functions: `rm_pres_rows`, `rm_pres_row` (whether a mode decides a cvar, and its value) and
  `rm_pres_needs_rt` (Enhanced and RT-primary run only where the device traces rays).
- **The module boundary.** The header is data and `static inline` code only. ref_metal
  (`rm_rtgate.h` includes it) and the client (`vidrules.c` includes it) each compile their own copy,
  so neither module links a symbol of the other. `ref.h` and every struct that crosses the module
  API are unchanged, so the fallback (two copies and a parity guard) was not needed.
- **The renderer.** `rm_pres_decide`, `rm_pres_value` and `rm_effect_overlaid` read the header;
  `pres_enhanced`, `pres_retro`, `pres_rtprimary` and `pres_table` are gone.
- **The menu.** `VidRules_RTOverridden` and `VidRules_RTEffective` look the row's cvar up in the
  same table, where the renderer runs the mode (`PresInForce`: on Metal, and for a mode that
  `rm_pres_needs_rt` only with `r_rt_available`). `RTPrimaryInForce` and `RTRetroInForce` use the
  same rule. The VIDPRES_ values stay in `vidrules.h`, so the rest of the menu needs no renderer
  header; the parity test holds them to the RM_PRES_ ones.
- **Still two copies.** `VidRules_MetalFXDecided` copies `rm_pres_fx`: it reads the device's
  scalers and the view's size and debug view, which the menu sees otherwise than the renderer.
  `tests/metal/rm_pres_menu_test.c` keeps holding those two in step.

## The parity test

`tests/metal/rm_pres_parity_test.c`, built and run by `tests/check-ref-metal.sh` (segment 10.25)
over `vidrules.c`, `rm_rtgate.c` and `rm_rtfx.c` in one host binary. For 17 `r_presentation` values
(the modes, fractions, values that are no mode, not finite ones), the three gate paths, every
ray-tracing control and five user values, `VidRules_RTOverridden` equals `rm_effect_overlaid` and
`VidRules_RTEffective` equals `rm_pres_value(...) != 0` of the mode `rm_pres_decide` runs. Off Metal
the menu decides nothing. The enums agree. 6742 checks.

Six mutants, each on one side only; the segment fails unless all six build and fail the test:

- `rrow`, `mrow`: a row added to RT-primary's table (the gun shadow read 0). A changed copy of the
  header is included first in `rm_rtgate.c`, then in `vidrules.c`; its include guard keeps the
  tree's copy out of that one file.
- `renum`: `RM_PRES_RETRO` changed in the renderer's file the same way.
- `menum`: `VIDPRES_RETRO` changed in the menu header the test reads.
- `rneed`, `mneed`: the ray-tracing rule dropped from `rm_pres_decide`, then from `PresInForce`.

The test was committed first, over the two copies, with the matching one-sided mutants (sed on each
side's table or code); it passed and refused all four. `rm_pres_menu_test`'s `row` mutant now drops
the `r_metalfx` row from a header copy included first in `rm_rtgate.c`, since the row left that
file.

## No change to what a mode does

- **Host proof.** A scratch program printed 55453 lines over both sides: `rm_pres_decide`,
  `rm_pres_value`, `rm_effect_overlaid`, `rm_pres_fx`, the format lines, `VidRules_RTOverridden`,
  `RTEffective`, `RTRow`, `RTStatus`, `PresentationMode`, `MetalFXDecided`, `MetalFXOverridden` and
  `TuneOverridden`. The output over this branch and over the sources at the base commit `56a39c45`
  was byte-identical (`cmp`).
- **Pins.** The hold compares every committed scenario and every RT-primary record with the base
  commit, byte for byte.

## `scripts/rt/preset-to-table.sh`

Its rows go to the shared header, which it names. It refuses (exit 2) a mode whose
`RM_PRES_<MODE>_ROWS` the header does not define. `--as raytraced` prints `RM_PRES_RTPRIMARY_ROWS`;
it printed `RM_PRES_RAYTRACED_ROWS`, which nothing defined. The stale note that only Enhanced had a
table is gone. `check-ref-metal.sh` checks each mode's macro against the header.

## Checks run

- The segments of `check-ref-metal.sh` from the ray-tracing gate through the cvar table (10 to 15),
  the direct light (7.5) and the temporal-through-A/B (5.65), extracted and run alone: all pass.
- `make test`: `yq2test: 591 tests, 0 failed`.
- `tests/check-cvar-docs.sh`, `tests/check-doc-links.sh`, `tests/check-runbook-index.sh`.
