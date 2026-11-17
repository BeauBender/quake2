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

**Pending: the screen was locked for the whole of this card's run** (`ioreg` showed
`CGSSessionScreenIsLocked` at every check), and a timing run needs it unlocked. Nothing was measured;
the cases stay the card's (1920 x 1080 and 5120 x 2880, with and without `r_metalfx 2`, held and not
held, three runs a case alternated with main, and `demo1.dm2` for a view with no water), by the owner's
protocol (one `heavy.sh` hold a batch, a 4-minute full-load pre-warm inside it, before every run the
load, `memory_pressure -Q`, the GPU utilization from `ioreg` and `pmset -g therm`, "frequency: not
recorded" until TASK166 lands). The `r_rt_stats 2` sum at `1` is now the compaction and the trace; the
marks' draw is inside the G-buffer raster, so the frame's own GPU time is the fair comparison with
`0`. The proposal on the default waits for those numbers.

## Follow-ups (outside the card's files)

- `header/rm_rtwater.h` (the stats fields' and `rmb_rtw_prepare`'s comments), `rmb_rt.m` (two
  comments) and `doc/040_cvarlist.md` (`r_rt_stats 2`'s "three parts" and `r_rt_water_hits`' cost)
  still describe the separate raster; `rm_rtwater.c`'s stats line could drop its raster figures.

## Checks

On the branch, each through `tools/heavy.sh`: the water host test (588 checks, 0 failed),
`make client ref_metal game ref_soft` (exit 0), `tests/check-ref-metal.sh` (ok: water 588 checks,
0 failed; `ref_metal` built), `tests/check-doc-links.sh` (ok), `tests/check-no-local-identity.sh` (ok).
The full `scripts/test.sh` was not run on this lane.
`tests/check-runbook-index.sh` fails on this file until its index line is added at merge.

## FIX98: TASK162's review notes

Four items, on top of FIX92's head.

1. **`raster_enc`** (`rmb_gbuf.m`): cleared right after `rmb_rtw_mark` uses it. Chosen over an
   accessor in `rmb_encode.m`, which would add a cross-file interface for one caller; the clear is one
   line and leaves the pointer valid only for the draw it exists for.
2. **The blend, once.** `common/mtl_rtw_math.h` defines `MTL_RTW_MARK_SRC_FACTOR` (One) and
   `MTL_RTW_MARK_DST_FACTOR` (source alpha). `mtl_rtw_mark_blend` evaluates them and
   `rmb_rtw_mark_pipe` sets the normal attachment's alpha factors from them (`rmb_rtw_factor` maps each
   to its `MTLBlendFactor`), so the host test checks what the GPU blends; the test also asserts the two
   factors. `tests/check-ref-metal.sh` refuses a pipeline that sets either alpha factor from a
   literal or drops a macro, with two mutants (a literal destination factor; the source factor spelled
   as the enumerator): both refused ("blend factors once, 2 mutants refused" in its summary line).
   Not red-first: the macros did not exist before; the mutants are the failing cases.
3. **The stats.** `raster_ms`, `vert_ms` and `frag_ms` are gone from `rm_rtw_stats_t` and from the
   `r_rt_stats 2` line (`world space: compaction a, trace b, span c ms`); `doc/040_cvarlist.md`
   follows. Nothing in `scripts/` parses them (`git grep raster_ms` finds nothing left in the tree),
   so the bench adapter cannot be affected. The host test's format cases were changed with it.
4. **The timer slots.** `RMB_RTW_SAMPLES` is 4 (compaction 0 and 1, trace 2 and 3; TASK58's path
   writes 0 and 1), so the sample buffer is half the size and the world-space resolve reads 4.
   Items 3 and 4 are one commit: they are the same lines of `rmb_rtw_read`. The GPU times were not
   measured here (screen locked): the slot move is covered by the build and by `MTL_DEBUG_LAYER=1`
   runs with no validation error, not by a timing.

### Pins against FIX92's head (screen locked, `MTL_DEBUG_LAYER=1`, `PINS_SEED=1`, one-minute load 3.0 to 3.6)

The base is FIX92's `ref_metal`, the new is this lane's, both under the same client binary
(byte-identical). Each log holds only the validation banner, no validation error.

| Scenario | Output | Setting | Frames equal |
|---|---|---|---|
| `rt-water` | 640 x 480, every 6th | 0 | 16 of 16 |
| `rt-water` | 1920 x 1080, every 30th | 0 | 4 of 4 |
| `rt-water-entities` | 640 x 480 | 0 | 16 of 16 |
| `fx-off` | 640 x 480 | 0 | 16 of 16 |
| `skywater` | 640 x 480 | 0 | 16 of 16 |
| `rtw-h1` | 640 x 480 | 1 | 16 of 16 |
| `rtw-h1e` (entities) | 640 x 480 | 1 | 16 of 16 |

5K with `r_metalfx 2` was left out (FIX96).

### Checks

Each through `tools/heavy.sh`: `tests/check-ref-metal.sh` (ok: water 727 checks, 0 failed; 72
vocabulary mutants refused; blend factors once, 2 mutants refused; `ref_metal` built),
`tests/check-cvar-docs.sh` (ok), `tests/check-doc-links.sh` (ok), `tests/check-no-local-identity.sh`
(ok). The full `scripts/test.sh` was not run on this lane (the brief).
