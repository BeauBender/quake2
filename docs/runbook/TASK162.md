# Water reflections mark their pixels in the G-buffer raster (TASK162, RT6)

TASK162 follows [TASK158](TASK158.md) (`r_rt_water_hits 1`, the world-space path, whose *Rework*
measured the separate water raster's wait) and [FIX81](FIX81.md) (the pass-over loop at both
settings). Design anchor: [section 2](../plans/metal-raytracing-design.md#2-the-three-effects-as-compute-passes)
(RT6). It uses TASK58's masked comparison (*The mask* in [its file](TASK58.md)) and the shared
runbook's method for the GPU time
([the shadows' proofs](../plans/metal-port-harness.md#ray-traced-shadows-from-dynamic-lights-in-ref_metal-task55-rt3)).

## The design

**Where the mark goes.** In the alpha of the G-buffer's normal attachment (`RGB10A2Unorm`, two
bits). The raster writes it 1 where the pixel has a normal and 0 where it has none, and every reader
(the shadows, the AO and its upsampling, the water's trace) tests only `a > 0.5`. At `r_rt_water_hits
1` the G-buffer raster ends with one more draw, before the pass closes: the map's water triangles
(the ids, the vertex function and the matrix the separate raster used), depth-tested less or equal
against the raster's own depth and not writing it, writing only that alpha by a fixed blend,
`a' = 1/3 + a/3` (source factor one, destination factor the source's alpha, 1/3). So a pixel with a
normal goes from 1 to 2/3 and one without from 0 to 1/3; a second water layer over the same pixel
keeps its level (4/9 stores as 1/3, 5/9 as 2/3). A pixel is marked when its stored level is 1 or 2;
it has a normal when its alpha is above 0.5, as before, for all four levels.

- *Not the material id.* An integer attachment does not blend, so the mark would replace the id
  that the resolve and every effect read for the surface behind translucent water, or need the
  attachment read back in the fragment (programmable blending: Apple GPUs only, while an AMD Mac
  traces too) and a mask at every reader, most of them outside this card.
- *Not a new attachment.* Every raster pipeline would need a variant with its format, as TASK131's
  surface attachment has, and the pass would store one more target a frame.

**What the world-space path reads instead.** The compaction reads the normal attachment's marks
(itself and its eight neighbours, `mtl_rtw_marked`) beside the material (opaque water), so its list
is the one the separate raster gave (`mtl_rtw_listed` unchanged). It also writes 0 to the reflection
target at every pixel it does not list, which is what the separate raster's clear left there; the
trace writes every listed pixel, as before. The reflection target is no longer a render target. No
render pass of its own, no reload of the depth, no cleared target, so no wait for the frame's earlier
passes. The compaction, the arguments, the indirect trace and FIX81's pass-over loop are unchanged.

**Why setting 0 is unchanged.** The draw is made only when the view's outputs were made for the
world-space path (`rmb_rtw_prepare(w, h, 1)`, which `RM_RTW_View` asks for only when `rm_rtw_path`
gives 1). At `0` the raster draws what main draws, the normal attachment's alpha is main's, and
TASK58's kernel `rm_rtw_trace` runs over every pixel as on main. The mark's pipelines are the
world-space path's resources, built in `rmb_rtw_open_ws` with the others, which setting 0 needs none
of.

## What changed

- `rm_rtwater.metal`: `rm_fragment_rtw_mark` (the mark's alpha into colour 1) replaces
  `rm_fragment_rtw_cand`; the vertex function `rm_vertex_rtw_cand` is TASK158's, unchanged.
  `rm_rtw_compact` reads the marks from the normal attachment (texture 2) and writes the reflection
  target (now write-only to it) 0 at every pixel it does not list.
- `rmb_rtwater.m`: `rmb_rtw_mark(enc, surf)`, the draw on the G-buffer raster's encoder, two
  pipelines (without and with TASK131's surface attachment) built in `rmb_rtw_open_ws` with the
  other world-space resources, the depth state as before. `rmb_rtw_trace_ws` has no render pass; a
  view whose raster drew no marks is not traced on the world-space path (no reflection that frame,
  as when a trace fails). The reflection target is no longer a render target. The `r_rt_stats 2`
  line keeps its format (`rm_rtwater.c` is outside the card): at `1` the raster's three figures print
  `-`, the pass's sum is the compaction and the trace, and the span runs from the compaction's start
  to the trace's end. The marks' own draw is part of the G-buffer raster and is not timed apart.
- `rmb_gbuf.m`: `rmb_gbuf_raster_formats` (the raster's attachment formats, now shared with the
  marks' pipelines), the raster's encoder kept while it is open, and the call of `rmb_rtw_mark` at the
  start of `rmb_gbuf_resolve`, before the shadows and the AO read the normals. `rm_gbuf.metal`: its
  comment on the normal's alpha.
- `common/mtl_rtw_math.h` (outside the card's list, needed for a pure C test the kernel shares):
  `MTL_RTW_MARK_A`, `mtl_rtw_mark_blend`, `mtl_rtw_alpha_level`, `mtl_rtw_marked`; the comments of
  `MTL_RTW_CLASS_CAND` (now the host test's reference only) and `mtl_rtw_listed`.
- `rm_rtwaterscene.c` needed no change: `RM_RTW_View` already prepares the outputs and the view
  before the raster.

## The host test

`test_gbuf_mark` in `tests/metal/rm_rtwater_test.c`: the four alpha levels and their reading;
every level after 0 to 4 blends, in float and in half precision: marked from the first, "has a
normal" (`a > 0.5`) kept, and the blend's value within 0.4 of the level it stores, so a conversion
off by up to 0.6 of a level, rounding or truncating, stores no other; and over 40 scripted frames
(random materials, normals, a viewport, 0 to 3 water layers a pixel) the old raster's
`MTL_RTW_CLASS_CAND` marks and the G-buffer's give the same marks and the same list through the
compaction's loop over the nine, no marked pixel is left unlisted, and every normal reads as the
raster wrote it. **Red first** with the helpers stubbed (the blend returning its input, nothing
marked): 43 of 588 checks failed (`<artifacts>/TASK162/tdd/red-stubs.out`); with them, 588
checks, 0 failed.

## Runs on the bare-metal host

Commits: the branch `b4256343` (the source; later commits are documents) against main `716e09a2`,
main's `ref_metal` built from an export of it and loaded by this branch's client (byte-identical:
the card changes only `ref_metal`). The screen was locked for every run; records and byte
comparisons are valid locked (the brief's 2026-11-16 check). No other `quake2` ran; every batch held
`tools/heavy.sh`; the one-minute load was 2.1 to 3.0. `PINS_SEED=1`, FIX81's `water.dm2` (the
same file beside both builds), `MTL_DEBUG_LAYER=1` on every run: each log holds the
validation banner and no other validation line. A scratch copy of the harness adds the cases
(`rtw-h1`: `rt-water.cfg` with `r_rt_water_hits 1`; `-e` with `cl_entities 1`; `-cls` with
`r_rt_water_show 2`; `-sh` with `cl_lights 1` and `r_rt_shadows 1`, so the raster has the surface
attachment and the marks take their second pipeline; `-fx` with `r_metalfx 2`). Everything is in
`<artifacts>/TASK162/` (`logs/compare-3.txt`).

### Setting 0: byte for byte against main

| Scenario | Output | Frames equal |
|---|---|---|
| `rt-water` | 640 x 480 | 20 of 20 |
| `rt-water` | 1920 x 1080 | 5 of 5 |
| `rt-water-entities` | 640 x 480 | 20 of 20 |
| `fx-off` (`r_rt_water 0`, `r_rt_gbuffer 1`, entities) | 640 x 480 | 20 of 20 |
| `skywater` | 640 x 480 | 20 of 20 |
| `rt-water`, shadows on (`rtw-h0sh`) | 640 x 480 | 20 of 20 |
| `rt-water`, `r_metalfx 2` (`rtw-h0fx`) | 5120 x 2880 | 5 of 5 |

The teardown counts one more `rt` object with the effect on (60/60 against main's 59/59, 69/69
against 68/68 with the shadows): the marks' two pipelines in place of the separate raster's one.

### Setting 1: against main's setting 1

TASK158's masked comparison allows changes only where the old raster and the G-buffer disagree; there
are none. Every frame is byte-identical to main's at `1` (the classes view's too), so the masked
comparison and the shares are main's, as TASK158 measured them (from the picture 97.65 / 91.62 %, in
world space 2.35 / 8.38 %, none without a reflection, over and under the pool). A frame whose view drew no marks is not traced, so it would have no reflection
and differ from main: equal frames also show the marks ran on every traced frame.

| Scenario | Output | Frames equal |
|---|---|---|
| `rtw-h1` | 640 x 480 | 20 of 20 |
| `rtw-h1` | 1920 x 1080 | 5 of 5 |
| `rtw-h1` | 3840 x 2160 (`PINS_HIDPI=1`) | 5 of 5 |
| `rtw-h1e` (entities: FIX81's pass-over loop) | 640 x 480 | 20 of 20 |
| `rtw-h1cls` (the classes view, `r_rt_water_show 2`) | 640 x 480 | 20 of 20 |
| `rtw-h1sh` (shadows on: the surface attachment's pipeline) | 640 x 480 | 20 of 20 |
| `rtw-h1sh` | 1920 x 1080 | 5 of 5 |
| `rtw-h1fx` (`r_metalfx 2`) | 5120 x 2880 | 1 of 5; see below |

**`rtw-h1fx` is not deterministic on main.** Three records of main itself: the first and second are
equal, the third differs from them on frames 1 to 4 by 1.9 to 3.3 % of the pixels, at most 7 levels,
spread over the whole frame (frame 0 always equal). The branch's two records differ from each other
and from each of main's by the same amounts (1.87 to 3.27 %, at most 7). So the branch's frames are
inside main's own run-to-run spread for this case, and the spread is outside the water too: the
temporal scaler's history, not the marks. It is a finding for a card of its own (main, `r_rt_water_hits
1` with `r_metalfx 2`); `0` with `r_metalfx 2` was equal on its one pair of records.

### Cost

Measured on the owner's protocol, with the screen unlocked. The number is **the frame's own GPU time**,
not the `r_rt_stats 2` sum (which at `1` no longer holds the marks' draw, it is inside the G-buffer
raster): `MTL_GPU_TIME=1`'s `gpu_ms ... frame` line, the frame's command buffer's `GPUEndTime -
GPUStartTime` (the whole 3D view, the effects and the 2D; the MetalFX scaler's pass runs in a command
buffer of its own and is not in it). Each run's first 20 lines are dropped (136 frame lines a run,
687 for `demo1.dm2`). `r_rt_stats` is off, no validation layer, `PINS_EVERY=100000` (nothing read
back), `PINS_SEED=1`. "Held" is the display's sync (the scenarios' default), "not held" is `r_vsync 0`.
FIX81's `water.dm2` is the view with water; `demo1.dm2` (`rt-water` cfg) is the view with almost
none. 5120 x 2880 is `PINS_HIDPI=1` at 2560 x 1440; the MetalFX cases are that size with `r_metalfx 2`.
Three runs a case and build, alternated in rounds: this branch at `0`, this branch at `1`, main at
`1`. Main's build is main's `ref_metal` (the export of `716e09a2`) beside the branch's client; no
commit on main since touches `src/client/refresh/metal` (`git log 716e09a2..origin/main -- src/client/refresh/metal`
is empty), so it stands for main today. The branch's build is `b4256343`'s; `8d17698f` changes
comments and documents only. Everything is in `<artifacts>/TASK162/timing/` (`logs/*-summary.txt`,
`logs/*.engine.log`, `snaps/`, `batch.sh`).

Median of the three run medians in ms, with the range of the run medians:

| Output | Sync | Branch, `r_rt_water_hits 0` | Branch, `1` | Main, `1` | `1` against `0` |
|---|---|---|---|---|---|
| 1920 x 1080 | held | 5.10 (4.91 to 5.11) | 4.73 (4.68 to 4.78) | 4.72 (4.67 to 4.82) | -0.37 ms (-7 %) |
| 1920 x 1080 | not held | 2.23 (2.16 to 2.38) | 2.14 (1.51 to 2.18) | 2.24 (2.09 to 2.29) | -0.09 ms, inside the ranges |
| 5120 x 2880 | held | 13.56 (13.41 to 13.68) | 13.20 (12.85 to 13.25) | 13.08 (12.96 to 13.60) | -0.36 ms (-3 %) |
| 5120 x 2880 | not held | 10.82 (10.81 to 10.83) | 10.33 (10.01 to 10.34) | 10.43 (10.42 to 10.44) | -0.49 ms (-5 %) |
| 5120 x 2880, `r_metalfx 2` | held | 12.58 (12.49 to 12.97) | 12.38 (11.92 to 13.56) | 12.30 (11.76 to 12.47) | -0.20 ms, inside the ranges |
| 5120 x 2880, `r_metalfx 2` | not held | 7.80 (7.78 to 7.86) | 7.68 (7.65 to 7.71) | 7.71 (7.68 to 7.73) | -0.12 ms (-2 %) |
| 1920 x 1080, no water (`demo1.dm2`) | held | 2.58 (2.42 to 2.88) | 1.57 (1.54 to 1.91) | 1.60 (1.53 to 1.67) | -1.01 ms (-39 %) |

p10 / p90 over the pooled frames are in the logs (for example 5120 x 2880 not held: `0` 10.12 / 10.96,
`1` 9.92 / 10.56). What the numbers say:

- **Setting 1 is not dearer than setting 0 in any case, and cheaper in five.** (Ranges do not overlap in five rows; two are inside them.) The frame is 0.1 to
  0.5 ms shorter with water in view, and 1.0 ms (39 %) shorter in a view with almost none, where `0`
  traces every pixel and `1` lists next to none. The two rows whose gap is inside the ranges (1080p not held, MetalFX held) are "not dearer".
- **The marks' draw costs nothing the frame shows.** This branch at `1` is within 0.12 ms of main at
  `1` in every case, inside the run-to-run range in six of seven. The separate raster's wait (TASK158's
  *Rework*: the raster's span, 3.25 ms of the 4.66 held at 1080p, was the pass waiting on the frame's
  earlier passes) did not add to the frame on main either, so the frame-level gain of moving the marks
  into the G-buffer raster is not measurable here: its gain is the stats sum's (the raster's `-` figures
  and no reload of the depth), not the frame's. The `r_rt_stats 2` figures of TASK158 (4.66 against 3.40
  ms at 1080p held) overstated what setting 1 costs the frame; this table replaces them as the cost.
- **The GPU's clock moves with the load, so compare within a row.** Held at 1080p the GPU sits in its
  lowest state (about 502 MHz, 0 % at the top state, 23 to 33 % active) for all three builds, held under
  MetalFX at 854 MHz; not held it runs 920 to 984 MHz at 1080p, 1540 to 1665 MHz at the larger sizes
  (77 to 94 % at the top state). Per row the three builds' medians are within 5 % of each other (largest:
  5120 x 2880 held, `0` 1273 MHz, `1` 1215 MHz, main 1209 MHz; the lower clock at `1` makes its frame
  longer, so its 0.36 ms advantage there is not a clock effect). The held 1080p absolute figures (4.7 to
  5.1 ms) are at the clock floor and so are larger than the not held 2.2 ms; none of that changes the
  comparison.

**Frequencies** (the no-root IOReport probe of the bench core, `freq dump` before and after each run,
`freq delta`; `eacc` is the efficiency cluster, `pacc0` the performance cluster):

| Window | eacc MHz (top state %) | pacc0 MHz (top state %) | GPU MHz (top state %), active % |
|---|---|---|---|
| First pre-warm (batch A, 240 s, 202 runs, cold start to warm) | 1551 (10.3) | 4323 (70.7) | 922 (1.6), 43 |
| Batch B pre-warm (110 runs, 5120 x 2880 not held) | 1353 (6.5) | 4480 (78.9) | 1650 (90.3), 75 |
| Batch C pre-warm (110 runs, MetalFX not held) | 1311 (4.9) | 4303 (75.5) | 1563 (79.5), 60 |
| Runs, 1080p held (all builds) | 1266 (3.2) | 4083 (54) | 502 (0), 33 |
| Runs, 1080p not held | 1573 (10.0) | 4144 (59) | 957 (0), 41 |
| Runs, `demo1.dm2` held | 1251 (2.2) | 3943 (55) | 502 (0), 23 |
| Runs, 5120 x 2880 held | 1322 (3.1) | 4003 (52) | 1220 (5.4), 73 |
| Runs, 5120 x 2880 not held | 1319 (4.1) | 4482 (80) | 1663 (93.5), 73 |
| Runs, MetalFX held | 1250 (2.3) | 4080 (55) | 854 (0), 73 |
| Runs, MetalFX not held | 1326 (3.6) | 4241 (72) | 1541 (77.3), 59 |

The pre-warm windows are the cold-to-warm view (the first one is the card's first full-load window of the session); the per-run rows are a run's own window, a few seconds each, medians over nine runs.
The performance cluster runs at 3.3 to 4.8 GHz in every window, so the CPU side is not throttled; only
the GPU clock differs between the held and not held cases, as above.

**Protocol notes.** Three batches (A: 1080p held, 1080p not held, `demo1`; B: 5120 x 2880 held and not
held; C: the same under `r_metalfx 2`), each one `heavy.sh` hold (B and C queued behind another lane's
hold first), each with its own 240 s pre-warm of the batch's first case at `1`, display sync off, no
thermal or performance warning at any check (all 132 snapshots: "no warning level recorded"; the
low-power mode off). Before and after every run: the load, `memory_pressure`, the swap, the GPU
utilization (Device, Renderer, Tiler, in-use memory), `pmset -g therm`, the top five processes by name.
**The screen was unlocked and checked before and after each batch (and at every run: `locked=0` in
all 63 runs and the three pre-warms).** The one-minute load was 1.6 to 4.8 (A 1.7 to 4.8, B 1.6 to 2.8,
C 1.8 to 2.9; the high end is the host's other work, outside a batch's own runs); the GPU's device
utilization before a run was 0 to 7 %, under the 10 % gate, so no run was repeated. The busy
processes in the snapshots were `mediaanalysisd` (up to 91 % of a core, in about half of the
snapshots), `searchpartyd` (up to 88 %, seven snapshots) and a browser helper (up to 45 %); no
`backupd`, and `mds_stores` only at 1.0 to 1.5 % in two snapshots. These ran at all three builds alike (alternated), but they are a load the
medians carry, and are the reason for taking medians. No other `quake2` ran.

**Proposal on the default.** The numbers support making `r_rt_water_hits 1` the default as far as cost
goes: at no size, sync or MetalFX setting is the frame longer than at `0`, it is shorter by up to 0.5
ms with water in view and by 1.0 ms without, and the branch costs what main's setting 1 cost. They
do not settle it alone: setting 1's picture is not byte-identical to 0's (TASK158: 0.08 to 0.31 % of
a frame's pixels differ, by at most 24 levels, where the reflection is traced in world space instead
of from the picture), which is the owner's call on the look; every `rt-water*` pin would be
re-recorded; and `r_metalfx 2` with `1` has the temporal-scaler non-determinism found above (its
own card). The gaps in two of seven rows are inside the ranges of three runs on one machine (an
M-series Mac, no AMD GPU), so "cheaper" is claimed only for the five whose ranges do not overlap
(1080p held, 5120 x 2880 held and not held, MetalFX not held, `demo1`) and "not dearer" for the other two. Proposal: flip
the default to `1` in a card of its own (cvar default, `doc/040_cvarlist.md`, the pins), after the
owner accepts the look.

## Follow-ups (outside the card's files)

- `rm_rtwater.c`'s stats line could drop its raster figures (`header/rm_rtwater.h`, `rmb_rt.m` and
  `doc/040_cvarlist.md` were brought up to the G-buffer marks in `8d17698f`).

## Checks

On the branch, each through `tools/heavy.sh`: the water host test (588 checks, 0 failed),
`make client ref_metal game ref_soft` (exit 0), `tests/check-ref-metal.sh` (ok: water 588 checks,
0 failed; `ref_metal` built), `tests/check-doc-links.sh` (ok), `tests/check-no-local-identity.sh` (ok).
The full `scripts/test.sh` was not run on this lane.
`tests/check-runbook-index.sh` fails on this file until its index line is added at merge.
