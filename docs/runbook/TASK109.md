# Option A on TASK111's scaler, the `effect` reset and the `r_rt_stats` reset line (TASK109)

TASK109 is RT11 of `docs/plans/metal-raytracing-design.md` (section 6), option A of
section 2. TASK111 already built the scaler, its jitter, its motion vectors and most
of its reset events, so this card reconciles the plan with that code and adds what
is left for the ray-traced path. Its slices:

- **Slice 1, the docs.** By the owner's direction of 2026-11-14 on #263 there is no
  `r_rt_scale`. `r_metalfx_scale` is the internal scale for every path. Option A is
  `r_metalfx 2` with `r_metalfx_scale 0`, and options E and C are `r_metalfx 0`.
  - The design's section 2 and row RT11, `docs/plans/metal-port-cards.md` row RT11
    and `docs/plans/metal-renderer.md` now say so.
  - The design also records these facts:
    - the cut threshold, `RM_FX_CUT_DIST`, which is 256 units on an axis;
    - stereo is ignored: two eyes are two views of one frame;
    - `timerefresh`'s views step the jitter;
    - at `r_metalfx_scale 1` the scaler still runs at 1:1, as temporal
      anti-aliasing. TASK105's text said there was no scaler there; the code's
      behaviour is kept and the text corrected.
  - `presentation-modes.md` 4.4 belongs to TASK190 and is left alone here.
- **Slice 2, the code.**
  - **The `effect` reason.** The scaler has a new reset reason,
    `RM_FX_RESET_EFFECT` (`effect`). It is set when the first scaled world view with
    a ray-traced effect on (`rm_rtfx_active`) follows a scaled world view with none.
  - **What it resets.** An effect kept on resets nothing, an effect turned off resets
    nothing, and on, off, on resets again. The player-setup menu's model view
    (`RDF_NOWORLDMODEL`) takes the forward path and never steps the scaler. A `fov`
    change is no input of the step.
  - **The reset line.** Under `r_rt_stats 1`, a scaled world view that resets either
    history prints one line:
    `ref_metal: r_rt_stats: reset: scaler <reasons>, temporal <reasons>`.
    - It is formatted by `rm_fx_reset_line_format` and decided by `rm_fx_reset_says`,
      both pure.
    - RT7's reasons come from `RM_RTT_TakeReasons`.
    - With the scaler off, no line prints.
- **Not here.**
  - Mode 3's `rm_fx_run(..., 0, ...)` belongs to TASK113.
  - The RT-primary overlay row and its denoiser belong to TASK190.
  - The measurement is slice 4.

## Host checks (no lock)

| Check | Result |
|---|---|
| `tests/check-ref-metal.sh` (the MetalFX host test and its three mutants) | ok: `MetalFX 392 checks, 0 failed, 3 reset mutants refused` |
| `make test` | `yq2test: 591 tests, 0 failed` |
| `tests/check-cvar-docs.sh`, `tests/check-doc-links.sh`, `tests/check-runbook-index.sh`, `tests/check-pin-chars.sh` | ok, each |

The table above is from the branch before the hold. The hold runs the same checks again on
its final commit: all ran again at `c063b41b2c24`, on main `f10aa5bc3abe` merged in, and passed: `make test` (`yq2test: 591 tests, 0 failed`), `tests/check-ref-metal.sh` (`MetalFX 392 checks, 0 failed, 3 reset mutants refused`), and the four doc checks above, each ok.

The tests were red first, at `c73e08df`: the build of `tests/metal/rm_fx_test.c` stopped on
`rm_fx_step`'s new effect argument. They were green at `b5d1ac15`.

These are the mutants of `rm_metalfx.c` that `tests/metal/rm_fx_test.c` must refuse.
Each one compiles:

- the reason dropped;
- the comparison inverted, so an effect turned off resets and one turned on does not;
- the two fields of the line swapped.

## The hold (M6, under the timing lock)

The hold builds main (the before) and this branch in scratch. Its checks:

- The stock pins, the `world-metalfx*` records and `hdr-metalfx2` are byte-identical
  to main's.
- The `rt-*` records are identical. They run at `r_metalfx 0`, where nothing here
  changes.
- One live run at `r_metalfx 2`, `r_metalfx_scale 0.5` and `r_rt_ao 1`, with
  `r_rt_stats 1`, prints the reset line for each event below and none for the last
  two.

| Event | Line |
|---|---|
| map load | 1 line: `scaler first, temporal first` |
| `vid_restart` | 1 line: `scaler first, temporal first` |
| a camera cut (the game's `teleport`, 300 units away) | 1 line: `scaler cut, temporal cut` |
| a teleport (the same command, 700 units on; base1 has no teleporter a script can reach) | 1 line: `scaler cut, temporal cut` |
| `r_metalfx_scale` 0.5 to 0.75 | 1 line: `scaler size, temporal size rect` |
| `viewsize 60`, then `viewsize 100` | 1 line each: `scaler size rect, temporal size rect` |
| the window's size (`r_customwidth` and a `vid_restart`: the tooling has no live resize) | 1 line: `scaler first, temporal first` (a `vid_restart` rebuilds the scaler, so it reads as `first`) |
| the effect off (no line), then on | off: no line; on: 1 line, `scaler effect, temporal first` |
| the player-setup menu's model view (no line) | no line |
| `fov` 90 to 30 (no line) | no line |

The menu is closed with the console's toggle, because no command closes a menu by itself.
The hold does not judge those frames.

What the host test cannot show, and the live run does:

- The model-view and `fov` cases of `rm_fx_test.c` pass because the code path never steps the
  scaler for them: `RM_GB_Begin` returns before `RM_FX_Begin` for the model view, and `fov` is
  not an argument of `rm_fx_step`. The test alone could not catch a regression there.
- `RM_FX_End`'s wiring (the take before the `fx_view` early return) has no host test.
- The effect reason follows each effect's own active flag (`rm_rtfx_active()`), not the
  `r_rt_*` switch alone. A switch that is on but whose effect cannot run (no ray tracing, or
  the sun on a map with little sky) does not trigger it, and nor does a second effect added
  while one is on.
- `vid_restart` and the resize both reset as `first`, since the tooling has no live resize.

The base and the branch: main `f10aa5bc3abe`, branch `c063b41b2c24`; the hold: 16 events, 0 wrong. Stock and MetalFX records: 29 of 29 scenarios byte-identical to the base (stock 17 of 17, `metalfx` 5 of 5).
`rt-*` records: 7 of 7 byte-identical to the base.
