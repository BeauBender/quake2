Temporal accumulation in `ref_metal` (TASK59, RT7), slice 1

RT7 accumulates the ray-traced effects over frames
([design](../plans/metal-raytracing-design.md), section 2, "Temporal accumulation", the
"Reset" row of the scaler's inputs, and row RT7 of section 6). Slice 1 adds the history, the
rejection rule, the blend and the resets, under `r_rt_temporal`, off by default. This file
holds the slice's results and what it leaves to slice 2.

**Status.** With `r_rt_temporal 0` (the default) nothing is made, written or read: the AO
pass's encoder has its one dispatch as before, and the stock pins are byte-identical to the
base (the hold, below). With it above 0 the AO is accumulated. The shadows and the sun are
not accumulated yet (*Left for the next slices*).

## What exists in the code, traced

- **Motion vectors.** The G-buffer path's `rg16Float` attachment (`rmb_gbuf_motion()`,
  `header/rm_gbuf.h`): pixels of the render size, previous minus current, +x right, +y
  down, from the unjittered current and previous projections (TASK54). The accumulation
  reads them as they are.
- **Material id.** The G-buffer's `R8Uint` material attachment (`MTL_GB_MAT_*`,
  `common/mtl_gbuf_math.h`): 1 world, 2 alias model, 3 warped water, 4 sky, 0 where no draw
  wrote. Every ray pass already binds it; the AO's accumulation reads it there.
- **Dynamic light set.** No per-pixel light set exists for the AO or the sun: neither reads
  a dynamic light. The shadows' kernel writes one, the packed indices of the four nearest
  lights in reach (`mtl_rts_pack_idx`, the second word of its `out` texture). The rule
  takes the set as a word on both sides; the AO passes 0 for both. The shadows' slice is to
  pass that word with its flag bits masked off. No G-buffer attachment changes.

## What slice 1 adds

- `common/mtl_rtt_math.h`, shared by the kernel and the host test:
  - `mtl_rtt_reproject`: the pixel's centre moved by its motion vector, off the screen when
    outside the render size or not finite;
  - `mtl_rtt_reject`: the reasons a history is dropped: the view's reset, off the screen,
    the distance from the eye beyond the tolerance, a different material id, a different
    dynamic light set, an empty history;
  - the depth tolerance (`mtl_rtt_depth_ok`): each frame's distance from its own eye may
    differ by the eye's move between the frames plus 2 units plus 2 % of the distance;
  - `mtl_rtt_blend` and `mtl_rtt_count`: the exponential blend, the history weighted by
    `r_rt_temporal` but never more than n / (n + 1) for a history of n samples, so a
    young history is the plain mean of its samples; the count is capped at 255 for the
    denoiser of slice 2.
- `rm_rttemporal.c` (`header/rm_rttemporal.h`): `rm_rtt_alpha` reads `r_rt_temporal` (0 for
  a value at or below 0 or not finite, at most 0.98), and the reset state: `rm_rtt_step`
  gives a view's reasons (first view, render size, rectangle, a move of more than 256 units
  on an axis), `rm_rtt_frame_end` makes the first view after a frame that accumulated
  nothing reset, `rm_rtt_state_reset` is the map load and the renderer's start.
- The AO's accumulation: `rm_rtao_temporal` in `rm_rtao.metal`, a second dispatch in the
  AO pass's encoder after the trace (`rmb_rtao.m`). It reads this frame's AO, the history at
  the reprojected texel, writes the blended AO with its distance, material id and count to
  the other history, and writes the light the resolve reads again from the blended AO. The
  two histories are `RGBA32Float` at the render size, made at the first view that
  accumulates and released when `r_rt_temporal` is 0; the engine logs
  `ref_metal: rt temporal WxH: N bytes allocated` when it makes them.
- The resets: `RM_RTT_Reset` is called from `RM_FX_Reset` (a map load through
  `BeginRegistration`, and the renderer's start, which a `vid_restart` repeats) and
  `RM_RTT_EndFrame` from `RM_FX_EndFrame`, so RT7's history resets on the scaler's events.
  `r_rt_scale` does not exist yet (RT11); its change will reach the history as a change of
  the render size, which resets it today.
- `r_rt_temporal` in `doc/040_cvarlist.md` and classified in `rm_rtgate.c` as a setting.
  It has no tuning-screen row yet: a row of `rt_tune` is also a preset cvar
  (`VidRules_PresetCvar`, counted by `scripts/rt/preset-to-table.sh`) and shifts the rows the
  unit tests name by index, so it lands with the default in slice 2, with the presets and the
  settings optimizer.

## Tests

`tests/metal/rm_rttemporal_test.c`, run by `tests/check-ref-metal.sh` (86 checks): the
reprojection at the edges and on values that are not finite; the rejection rule, the
accepted case and one case per reason alone; the tolerance; the blend, the count, a
constant kept, two alternating samples averaged, a rejection starting again; the cvar; and
every reset event the renderer sees. The check then builds the test against mutants of the
two files, each of which it must fail: the material, light set, depth and off-screen
reasons dropped one at a time, the view's reset ignored, a frame without an accumulated view
not resetting, a map load not resetting (7 refused). The kernel's call to the rule is pinned
as text, `mtl_rtt_reject(t.reset, r.ok, dist, h.y, t.move, m, hm, 0u, 0u, h.w)`, with two
mutants refused (the reset passed as `0u`, the material compared with itself), and the
kernel's calls of `mtl_rtt_reproject`, `mtl_rtt_reject`, `mtl_rtt_blend` and `mtl_rtt_count`
join the kernel-call pins with their ten mutants each. `check-ref-metal: ok (...; temporal
86 checks, 0 failed, 7 rule mutants and 2 kernel mutants refused; ...; 10 kernel calls, 100
kernel-call mutants refused; ...)`.

## The hold

One hold on the final commit, as the scratchpad's `task59s1-hold.sh`: `make -j8 ref_metal`,
`make`, `make test`, `tests/check-ref-metal.sh` and the doc and menu checks; the stock
`metal` pins (every committed scenario but RT-primary's) recorded by the base and by the
branch and compared frame by frame; and two scratch scenarios, never committed, from
`rt-ao.cfg` with the shadows, the sun and the AO on, one AO ray a pixel and the noise
moving (`r_rt_ao_seed 0`), at `r_rt_temporal 0.9` (run twice) and at 0. Their frames are
for the owner and are kept outside the repository.

Results of the first hold (commit `f70d57b7`, base `8870daa5`; the menu row it still had is
gone since, which changes no renderer frame):

- `pins: 29 of 29 stock scenarios byte-identical to the base`, so accumulation off changes
  no frame.
- The accumulated scenario, 23 frames of `demo1.dm2` at 640 x 480: the engine log has
  `ref_metal: rt temporal 640x480: 9895936 bytes allocated` once, the run at
  `r_rt_temporal 0` has no such line, and all 23 frames differ between the two.
- Determinism: two runs of the accumulated scenario give one hash (`e55e54e3528f`), every
  frame byte-identical. The pins harness drives `timedemo 1`, so the frames, the noise's
  frame count (`r_rt_ao_seed 0`) and the histories are the same on every run.

The hold for the final commit repeats every check.

No pin is made of an accumulated scenario in this slice.

## Left for the next slices

- The shadows' and the sun's accumulation, in their kernels or after them, with the
  shadows' light set (the packed indices) passed to the rule.
- The 5x5 bilateral denoiser on AO and sun, scaled by the sample count, and the reflections'
  3x3 blur (slice 2).
- The default of 0.9, set when the denoiser lands and the look is judged, and the
  tuning-screen row with it.
- RT11's `r_rt_scale` and the `r_rt_stats 1` reset line of the design's RT11 row.
- The GPU time of the accumulation, recorded as information (decision 0014: no target).
