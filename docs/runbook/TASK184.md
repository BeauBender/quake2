# Retro post-process (TASK184, PM2) — WIP

Card: TASK184 (#531). **Work in progress**: the pass, its settings, the host tests, the scenarios and
the timing are done; the base-against-branch pin comparison and the full gate are not yet run (see
*What is left*).

`r_presentation 2` (*Retro*) now runs on every device, ray tracing or not. After the 3D view (the
world, the models and the blend) one full-screen fragment pass rewrites the view's rectangle: each
pixel quantized to RGB565 with a 4 x 4 ordered (Bayer) dither, optionally a 2 x 2 box output filter
over the quantized pixels, then one gamma curve on top of `vid_gamma`. No fog. The 2D layer stays
stock (plan call P6). The menu item stays greyed "awaiting review" until PM4.

## What it is

- `rm_pres_decide` (`src/client/refresh/metal/rm_rtgate.c`) gives Retro for a request of 2 on every
  gate path; every other value decides as before. The report line reads
  `ref_metal: presentation: retro (r_presentation 2): the 3D view in RGB565 with the 4 x 4 dither, r_retro_filter and r_retro_gamma read each frame`.
  Retro overlays no cvar (`rm_pres_value` unchanged).
- The arithmetic is in `src/client/refresh/metal/common/mtl_retro_math.h`, shared by the shader
  (`rm_fragment_retro` in `rm_shaders.metal`) and the host test, in integers on 8-bit values.
- `rmb_retro_pass` (`rmb_encode.m`) ends the open pass, copies the rectangle to a scratch texture and
  draws one triangle over it with a scissor; the pipeline (`rmb_retro_pipe`, `rmb_resource.m`) and the
  scratch texture are made at the first Retro frame, so a stock run builds nothing more.
- It runs at the end of `RI_RenderFrame` (both the forward and the G-buffer path), not in
  `RI_EndWorldRenderpass`: in a normal frame the client never calls that, and the 2D draws reopen a
  pass of their own (`rmb_draw_2d`). A first build hooked the latter and its retro frames were
  byte-identical to `world`'s; that is how it was found.
- Settings, read every frame, archived: `r_retro_filter` (0 off, the default; else on) and
  `r_retro_gamma` (0 none, the default; 1 exponent `1/1.15`; 2 `1/1.3`; anything else counts as 0).
  Documented in `doc/040_cvarlist.md`.

## Tests and mutants

`tests/metal/rm_rtgate_test.c` (run by `tests/check-ref-metal.sh`): Retro on every path, 3 still not
built, the retro report line, and the CPU reference of the pass: the Bayer matrix, known pixels (black
and white stay at every rank; byte 128 at rank 0 and 15 gives 123 and 132 in 5 bits, 125 and 130 in
6 bits), every output a level's byte, monotonic in the byte and the rank, the 16 ranks averaging to
the byte within 1, the box filter's rounding and the curves. 794 checks, 0 failed;
`check-ref-metal: ok`.

Each mutant was applied to a copy of the sources and the test built against it:

| mutant | file | result |
| --- | --- | --- |
| request 2 decides stock | `rm_rtgate.c` | 5 of 794 failed |
| Retro needs ray tracing | `rm_rtgate.c` | 3 of 794 failed |
| retro report line is the generic one | `rm_rtgate.c` | 1 of 794 failed |
| quantize without the dither | `mtl_retro_math.h` | 4 of 794 failed |
| level widened by truncation | `mtl_retro_math.h` | 4 of 794 failed |
| Bayer matrix transposed | `mtl_retro_math.h` | 1 of 794 failed |
| box filter without rounding | `mtl_retro_math.h` | 2 of 794 failed |
| curves 1 and 2 swapped | `mtl_retro_math.h` | 1 of 794 failed |

The shader's output is not read back against the CPU reference; it is pinned by the scenarios.

## Scenarios and records

`scripts/pins/scenarios/retro.cfg` (`world` with `r_presentation 2`, filter and curve off) and
`retro-filter.cfg` (with `r_retro_filter 1`), in the scenarios README and `doc/100_renderpins.md`.
Recorded on Metal (apple9, this Mac), 640 x 480, `demo1.dm2`, no trace, branch commit `29308331`,
into the pin store; no existing record was replaced. Hash of the 23 frames' hashes:

| scenario | hash | frames equal to `world`'s |
| --- | --- | --- |
| `world` (scratch store) | `b15b40cf1604` | 23 of 23 |
| `retro` | `1c33e27aa3ff` | 0 of 23 |
| `retro-filter` | `33dfbc89de5a` | 0 of 23 (0 of 23 equal to `retro`'s) |

A record of each in a scratch store gave the same hashes as the store's. No frame was looked at;
the owner judges them in PM4.

## Timing

`MTL_GPU_TIME=1`, the median of the frame's `gpu_ms ... frame` lines over 707 frames of `demo1.dm2`,
screen unlocked, a warm-up run then two runs, each record in a scratch store. The pass's cost is the
difference from `world`. No thermal or performance warning was recorded.

| output | `world` | `retro` | `retro-filter` |
| --- | --- | --- | --- |
| 1920 x 1080, runs 1 and 2 | 0.362, 0.295 ms | 0.891, 1.073 ms | 1.435, 1.325 ms |
| 5120 x 2880 (2560 x 1440, `PINS_HIDPI=1`), runs 1 and 2 | 1.435, 2.203 ms | 3.570, 3.634 ms | 6.066, 6.284 ms |

So the pass takes about 0.5 to 0.8 ms at 1080p and 1.4 to 2.1 ms at 5K, and 1.0 to 1.1 ms and 3.9 to
4.6 ms with the filter (it quantizes four pixels for each one). That is above the plan's estimate of
under 0.5 ms at 5K (section 5.3). The copy is part of it; framebuffer fetch with the filter off, and
one quantize a pixel ahead of the filter, are the obvious ways down. Not done in this slice.

## What is left

- The base-against-branch comparison at `r_presentation 0` of every committed scenario (seeded,
  `PINS_SEED=1`, which needs the trace; the first attempt ran with `PINS_TRACE=0`, which `record`
  refuses with a seed, so nothing was compared). The water scenarios also need `make-demo water`,
  which needs `ref_soft` built beside the binary.
- `scripts/test.sh` and the named checks: `check-pins`, `check-cvar-docs`, `check-video-menu`,
  `check-doc-links`, `check-runbook-index`, `check-no-local-identity`, `make test`.
- The retro overlay of plan call P8 (the `r_rt_*` effects and `r_metalfx` off under Retro) is not
  in this slice: `rm_pres_value` leaves every cvar the user's under Retro.
