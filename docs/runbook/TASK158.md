# Water reflections shaded in world space, rays only on water pixels (TASK158, RT6)

TASK158 follows [TASK58](TASK58.md) (`r_rt_water`, design
[section 2](../plans/metal-raytracing-design.md#2-the-three-effects-as-compute-passes)). It uses
the shared runbook's method for the GPU time
([the shadows' proofs](../plans/metal-port-harness.md#ray-traced-shadows-from-dynamic-lights-in-ref_metal-task55-rt3))
and TASK58's masked comparison (*The mask* in its file).

**Status (after the rework, *Rework* below).** World-space hit shading is a setting,
`r_rt_water_hits` (default `0`, TASK58's pass: frames byte-identical to main's, the same cost).
At `1` the share of water pixels with no reflection falls from 2.35 % to 0 over the pool and from
8.38 % to 0 under it, and every pixel outside the water is byte-identical to the effect off on the
G-buffer path. Measured, its pass costs more than TASK58's: 4.66 against 3.40 ms at 1080p held with
water in view, 2.06 against 1.33 ms held in a view with no water, 1.20 against 0.99 ms not held. A
re-count of the stages' own work, not a frame measurement, puts it at TASK58's (3.42 ms): the
raster's span is mostly a wait for the work ahead of it. The follow-up that removes that pass is
TASK162. The sections between this line and *Rework* are the first pass's (head `41a21150`);
*Rework* supersedes their numbers, and *Review round 1* its hidden-model rule. The pins are not
recorded (the owner accepts the look first).

## What changed

- *Rays only for water pixels.* Before the trace, a raster of the map's water triangles
  (`rm_vertex_rtw_cand`, the ids from `rmb_rt_bind_water_raster`, built with the map's structures in
  `rmb_rt_build_map`) draws with the view's matrix over the reflection target, cleared to 0,
  depth-tested (less or equal, no write) against the G-buffer's depth; each fragment marks its pixel
  (`MTL_RTW_CLASS_CAND`). A compaction over every pixel (`rm_rtw_compact`) lists the pixels of opaque
  water and those with a mark among their nine (`mtl_rtw_listed`: the raster and the ray agree on a
  pixel's centre but for edge ties and float error), one atomic per SIMD group; `rm_rtw_args` turns
  the length into threadgroups, and the trace runs on the list by an indirect dispatch, one pixel a
  thread. The trace itself is TASK58's on each listed pixel (the eye ray included), so it finds the
  same water: the water pixels and the picture's share are equal to main's on every line (below).
- *Which path a hit takes* (`mtl_rtw_choose_ws`). The picture's pixel wherever TASK58's screen-space
  lookup finds the hit (exact, and cheaper: the resolve's lightmap, AO and shadowed dynamic lights).
  Else a map triangle with a texture is shaded in world space (class `MTL_RTW_LIT | MTL_RTW_WS`),
  and the sky brush or nothing met takes the sky box's texel in the ray's direction
  (`MTL_RTW_SKY | MTL_RTW_WS`). What is passed over is exactly this: an alias model the picture
  does not show; the ray goes on behind it, however many it crosses (*Review round 1*: the first
  pass gave up after four faces), so a hidden player neither reflects nor leaves a hole in the
  reflection behind it. A hidden brush model (a door, a plat) is a map face and is shaded where it
  stands.
- *World-space shading* (`rm_rtw_shade_face`). The hit's triangle is the instance's first triangle
  plus the primitive id, in the world's index buffer; its texture and lightmap coordinates are its
  vertices' (`rm_wvert_t.uv`, `.lmuv`) mixed by the hit's barycentrics (`mtl_rtw_bary`). Its record
  (`rm_rtw_shade_build`, one 32-bit word a triangle, built from the surface records once a map)
  gives the index of its image in the frame's texture table (an argument buffer of the world's
  images, `rm_rtw_tex_slots`, each frame's texture ids from `gltextures`), its lightmap slice and its
  flags. The texel is sampled at the mip level of the path's footprint (`mtl_rtw_lod`, eye to water
  to hit), a flowing face's scroll added; the light is the lightmap array's sample in the slice
  (white without one), raised by each dynamic light the lightmaps left out when RT3 traces (gl1's add
  at the hit, unshadowed, `mtl_rts_add`, `mtl_rts_light`); the colour is the stock product of the
  face's kind (`mtl_rtw_face_color`: the lightmap unit, the warp's colour, the translucent faces'
  inverse intensity, `r_fullbright`). The sky: the ray's direction turned back by the sky's
  rotation (`rm_rtw_sky_rows`, `mtl_rtw_sky_dir`), gl1's face and coordinates
  (`mtl_rtw_sky_st`, checked against `rm_sky_vec` on 2,646 points), the face's image from
  `RM_SkyBox` (new in `rm_warp.c`, read-only).
- Left out: an animated texture is shaded with its first frame, a warped face without its warp,
  and a hit off the screen gets no AO and no dynamic-light shadow.
- *Outside the water files*: `rm_warp.c` (`RM_SkyBox`) and its line in `header/local.h`; `rmb_rt.m`
  (the water ids and their raster binding) is in the card's list.

`r_rt_water_show 2` shows the new classes (yellow a face in world space, cyan the sky box), and
the `r_rt_stats 2` line adds the listed pixels, `world` and `sky box`. The rework makes the path a
setting, `r_rt_water_hits` (*Rework*).

## The host test

`tests/metal/rm_rtwater_test.c` (312 checks): the choice with world-space shading, the class and
candidate bytes, the record's packing, the barycentrics, the mip level, the list's helpers, a
face's colour against `mtl_gb_resolve`, the sky box against `rm_sky_vec` and `rm_sky_matrix`, the
texture table and the records (the walk of `rm_rt_materials` and its refusals), the view's shading
fields and the stats line. Red first: with the four CPU functions stubbed, 33 of 312 checks failed
(`<artifacts>/TASK158/tdd/red-cpu-stubs.out`).

## Runs on the bare-metal host

Engine commit measured: `41a21150` (the head), against main `d6be90fd` (an export built in
scratch). Every run checked that the screen was unlocked and no other `quake2` ran, and held the
timing lock (`tools/heavy.sh`, one hold a record, under a minute); the one-minute load was 1.6 to
3.0. `PINS_SEED=1`; quality runs under `MTL_DEBUG_LAYER=1` (each log holds the banner and no other
validation line, and a matched teardown: `rt 60/60` with the effect, `49/49` on main). The demo:
`scripts/pins.sh make-demo water` made 143 messages (over the script's range, as TASK58's did on
the shared host), used as made beside both builds; the camera is over the pool for the first 61
traced frames and under it after. Each case is its own record of the scratch harness copy (the
cvars added to `rt-water.cfg`); records of a scenario are deterministic, so the off, on and mask
records show the same frames. Everything is in `<artifacts>/TASK158/`.

### Image quality

| | Over the pool | Under it |
|---|---|---|
| Water, share of the pixels (main and head alike) | 42.2 % (1080p 41.1 %) | 40.3 % (39.3 %) |
| No reflection, main | 2.35 % (1080p 1.91 %) | 8.48 % (8.67 %) |
| No reflection, head | 0 | 0 |
| Head: from the picture / shaded in world space | 97.65 % / 2.35 % | 91.52 % / 8.48 % |

640 x 480 every 7th (137 lines) and 1920 x 1080 every 30th (136). The picture's share is main's to
the line: every hit the screen showed is still taken from it, and the world-space path covers
exactly what had none. No ray on this path meets the sky brush.

- *Masked comparison* (TASK58's method, frames by index, `framediff --mask --threshold 0`): the
  effect off on the G-buffer path (`r_rt_water 0`, `r_rt_gbuffer 1`) against the effect on, under
  the `r_rt_water_show 1` mask: **20 of 20 frames at 640 x 480 and 5 of 5 at 1920 x 1080 score 0**.
- *Effect off*: not recorded again. With `r_rt_water 0` none of the new code runs (the pass is not
  opened; `RM_SkyBox` is called only from it); the one new object outside the pass is the water ids'
  buffer built with the map's structures, which changes the teardown's `rt` count, not a frame.
- *Frames for the owner*: four current/new pairs at 1920 x 1080 (captures 1 to 3 over the pool, 4
  under it), `<artifacts>/TASK158/frames/<n>-current.png` (main) and `<n>-new.png`, never committed:
  0.08 to 0.31 % of each frame's pixels differ, by at most 24 levels.

### Cost

GPU time from the passes' timestamps: the candidates raster (start of its vertex stage to the end
of its fragment stage) plus the compute encoder (the compaction, the arguments, the trace), the
`r_rt_stats 2` line; no validation layer; each run's lines past its first 20; "held" is the
display's sync (the scenario's default), "not held" `r_vsync 0`. Head: three runs a case,
alternated; main: one run a case (for held, the drift check of the stored baseline
`<artifacts>/baselines/d89bf519f27c/rt-water-cost.txt`, 3.40 / 9.52 / 3.48 ms: inside its range).

| Output | Sync | Main (TASK58) | Head, median (run medians) | p10 | p90 |
|---|---|---|---|---|---|
| 1920 x 1080 | held | 3.40 | 4.61 (4.63, 4.61, 4.60) | 4.36 | 4.89 |
| 1920 x 1080 | not held | 0.98 | 1.21 (1.21, 1.21, 1.20) | 1.11 | 2.89 |
| 5120 x 2880 | held | 9.49 | 12.14 (12.16, 12.09, 12.19) | 11.72 | 12.67 |
| 5120 x 2880 | not held | 6.81 | 18.17 (18.00, 18.47, 17.86) | 7.92 | 28.61 |
| 5120 x 2880, `r_metalfx 2` | held | 3.40 | 4.07 (3.98, 4.10, 3.93) | 3.71 | 4.33 |
| 5120 x 2880, `r_metalfx 2` | not held | 1.21 | 3.62 (3.56, 3.56, 3.74) | 1.22 | 4.02 |

*A frame with no water* (`demo1.dm2`, 1920 x 1080 held, 659 frames with nothing listed): 2.04 ms on
the head against 1.33 ms for main's every-pixel pass. So the fixed part (the raster's depth load
and store and its cleared target, the compaction over every pixel) costs more than the eye rays it
saves here, and the not-held 5120 x 2880 spread (p10 7.9, p90 28.6 ms) says the raster's span also
takes in other work of the frame: on this GPU a render pass's vertex and fragment stages overlap
the passes around it, so the two-span sum over-counts. The split between the raster, the
compaction and the trace was not measured; that, and moving the marks into the G-buffer raster
itself (no separate pass, no depth reload) or the compaction into a quarter-size pre-pass, is the
next step. Against the share: 0.88 ms at 1080p (section 8, trace only) is not met by either build;
not held the head is 1.21 ms, held 4.61 ms.

## Rework

The first pass met the image goal and missed the cost goal (above). The rework makes the world-space
path a setting and splits its cost. Source commit measured: `c6cdd3ec` (the branch's last source
commit; the commits after it are documents), against main `a0eabe11` (its `ref_metal` built from an
export in scratch and loaded by this branch's client, whose only change from main's is the video
menu).

### The setting

- `r_rt_water_hits` (archived, default `0`; `rm_rtw_hits`: `1` from 1 up, `0` below 1 or not
  finite), read each view by `RM_RTW_View`:
  - `0`: TASK58's pass. Its kernel `rm_rtw_trace` is TASK58's source verbatim (the first pass's
    kernel is now `rm_rtw_trace_ws`); one compute encoder over every pixel (`rmb_rtw_trace_ss`); the
    outputs made as TASK58 made them (the reflection target not a render target, no list); none of
    the world-space shading's per-frame work (`RM_RTW_Shading`, the shading records, the texture
    table, the lights) runs. The new objects are made once, when the pass opens: the teardown
    counts `rt 59/59` against main's `49/49` (the world-space pipelines, the depth state, the
    argument encoder and buffer, two 1 x 1 textures, and the map's water ids).
  - `1`: this card's path (`rmb_rtw_trace_ws`): the candidates' raster, then the compaction and the
    arguments in one compute encoder and the trace in another, so each is timed on its own.
  - A change remakes the outputs (`rmb_rtw_prepare(w, h, ws)`) from the next frame.
- Video menu, renderer options: "water reflections" after "denoiser", `screen space` / `world
  space`, always shown, greyed with "Metal renderer only" under another renderer and "needs
  r_rt_water 1" while `r_rt_water` is off (`VIDITEM_RTW_HITS`, `VidRules_WaterOn`,
  `VidRules_WaterHitsIndex`); apply writes it only when moved. Tests: `tests/unit/test_vidrules.c`
  (`vidrules_water_hits_needs_metal_and_the_water`) and `tests/menu/videomenu_test.c`
  (`test_water_hits_item`, and the item in the greyed-item check).
- The `r_rt_stats 2` line at `1`: `..., <sum> ms, world space: raster <a> (vertex <v>, fragment
  <f>), compaction <b>, trace <c>, span <d> ms`, from eight timestamps a frame slot (the raster's
  start of vertex, end of vertex, start of fragment and end of fragment; each compute encoder's
  start and end). At `0`, TASK58's two (its one encoder).
- Host test (`tests/metal/rm_rtwater_test.c`, 325 checks): `rm_rtw_hits` and the split line, red
  first (the missing function and fields).

### Method

Every engine run went through `tools/heavy.sh` (the timing lock), after checking that the screen was
unlocked and that no other `quake2` ran; the one-minute load was 1.3 to 3.6 over the batches (each
run's before and after in `<artifacts>/TASK158/rework/logs/runs.txt`). The demo: `scripts/pins.sh
make-demo water` made 144 messages, over the script's range as on the first pass, used as made beside
both builds. `PINS_SEED=1`. A scratch copy of the harness holds one scenario a case: `rt-water.cfg`
plus `r_rt_water_hits`, `r_rt_stats 2`, `r_metalfx` and, not held, `r_vsync 0`. Cost: no validation
layer, `PINS_EVERY=100000`, each run's lines past its first 20 (117 held, 116 not held); three runs a
case for the branch at each setting, alternated in rounds, and one for main beside them in the first
round. 5120 x 2880 is `PINS_HIDPI=1` at 2560 x 1440. Quality: `MTL_DEBUG_LAYER=1`, each log holding
the validation banner and no other validation line. Everything is in `<artifacts>/TASK158/rework/`
(`logs/cost-report.txt`); the same batch on `6f15df82`, before the two stage timestamps were added,
is in `store-6f15df82/` and agrees within 0.3 ms on every median.

### Cost, both settings

GPU time of the pass in ms, median of the three runs' lines (the run medians), and p10 / p90:

| Output | Sync | Main | `hits 0` | `hits 1` | `hits 1` p10 / p90 |
|---|---|---|---|---|---|
| 1920 x 1080 | held | 3.39 | 3.40 (3.40, 3.40, 3.40) | 4.66 (4.67, 4.66, 4.66) | 4.55 / 4.89 |
| 1920 x 1080 | not held | 0.98 | 0.99 (0.98, 0.99, 0.98) | 1.20 (1.21, 1.20, 1.20) | 1.11 / 3.55 |
| 5120 x 2880 | held | 9.32 | 9.44 (9.49, 9.51, 9.28) | 12.24 (12.13, 12.03, 12.32) | 11.90 / 12.84 |
| 5120 x 2880 | not held | 6.80 | 6.78 (6.78, 6.78, 6.77) | 18.76 (18.53, 18.31, 19.10) | 8.47 / 28.53 |
| 5120 x 2880, `r_metalfx 2` | held | 3.44 | 3.46 (3.42, 3.47, 3.47) | 3.82 (3.79, 3.83, 3.82) | 3.64 / 4.05 |
| 5120 x 2880, `r_metalfx 2` | not held | 0.99 | 0.99 (0.99, 0.99, 0.99) | 3.66 (3.66, 3.73, 3.56) | 1.15 / 4.07 |
| No water in view (`demo1.dm2`, 639 frames), 1920 x 1080 | held | 1.33 | 1.33 | 2.06 | 1.96 / 2.16 |

`hits 0` is main within 0.12 ms on every case, p10 to p90 overlapping: the setting costs nothing at `0`.

### The split of `hits 1`

Medians over the three runs, ms:

| Output | Sync | Raster's span | Its vertex stage | Its fragment stage | Compaction | Trace | Whole span | By the stages |
|---|---|---|---|---|---|---|---|---|
| 1920 x 1080 | held | 1.49 | 0.03 | 0.28 | 0.53 | 2.58 | 4.66 | 3.42 |
| 1920 x 1080 | not held | 0.23 (p90 2.64) | 0.01 | 0.03 | 0.15 | 0.75 | 1.21 | 0.94 |
| 5120 x 2880 | held | 3.25 | 0.03 | 0.79 | 1.65 | 7.35 | 12.25 | 9.82 |
| 5120 x 2880 | not held | 12.10 (p90 21.82) | 0.02 | 0.20 | 1.19 | 5.05 | 18.77 | 6.46 |
| 5120 x 2880, `r_metalfx 2` | held | 0.48 | 0.02 | 0.06 | 0.32 | 2.97 | 3.82 | 3.37 |
| 5120 x 2880, `r_metalfx 2` | not held | 2.55 | 0.01 | 0.03 | 0.16 | 0.75 | 3.67 | 0.95 |
| No water in view, 1920 x 1080 | held | 1.57 | 0.02 | 0.25 | 0.48 | 0.00 | | 0.75 |

"By the stages" adds the vertex stage, the fragment stage, the compaction and the trace (a sum of
medians). The trace runs on 825,000 of 2,073,600 pixels (40 %) at 1080 lines and on 5,860,000 of
14,745,600 at 5120 x 2880.

**The 5120 x 2880 not-held spread** is the raster's span alone. The compaction (1.19 ms, p90 2.04)
and the trace (5.05, p90 5.67) are as tight as held, and the three spans add to the whole span within
0.01 ms, so they do not overlap each other. Inside the raster the vertex stage takes 0.02 ms and the
fragment stage 0.20 ms; the 11.9 ms between them (p90 21.6) is the fragment stage waiting for the
work ahead of it in the frame: the pass's vertex stage starts as soon as the GPU reaches the pass,
its fragment stage only once the depth it loads and the passes before it are done. So the spread is
*overlapping spans*: the raster's span takes in time the GPU spends on the frame's other passes. It
is not the GPU's clock (held, at the lower clock, the same span is 3.25 ms, while each stage is
slower held, as it should be) and not the raster's own cost (0.22 ms). The same wait, smaller, is in
every case (1.18 ms at 1080p held, 1.30 ms in a view with no water), which is why the summed figure
is over TASK58's. On the stages' count (a re-count, not a frame measurement), `hits 1` costs what
`hits 0` does with water in view (3.42
against 3.40 ms at 1080p held, 0.94 against 0.99 not held, 9.82 against 9.44 at 5120 x 2880 held)
and about half of it in a view without water (0.75 against 1.33 ms). Whether the GPU idles in the
wait is not something these timestamps show; the frame's own GPU time is the measurement that
settles what the wait costs the frame.

### Image, on `c6cdd3ec`

- `hits 0` against main, byte for byte (`cmp` of every frame): `rt-water` at 640 x 480 (20 frames)
  and 1920 x 1080 (5 frames), 0 differ. Effect off (`r_rt_water 0`) at 640 x 480: 20 frames, 0
  differ.
- `hits 1`, TASK58's masked comparison (`framediff --mask --threshold 0`): the effect off on the
  G-buffer path against the effect on, under the `r_rt_water_show 1` mask: 20 of 20 frames at
  640 x 480 score 0.
- The shares, 640 x 480, every frame's stats line under `r_rt_water_show 2` (61 lines over the
  pool, 77 under it):

| | Over the pool | Under it |
|---|---|---|
| Water, share of the pixels | 42.2 % | 40.4 % |
| From the picture (both settings) | 97.65 % | 91.62 % |
| No reflection, `hits 0` | 2.35 % | 8.38 % |
| Shaded in world space, `hits 1` | 2.35 % | 8.38 % |
| No reflection, `hits 1` | 0 | 0 |

### Follow-up: the cheaper design

Mark the water pixels in the G-buffer raster itself. The world's water already draws there (material
3 for opaque water; translucent water is drawn over the raster's surface); writing a candidate mark
into an attachment the G-buffer pass already has, or a one-byte target bound beside it, where a water
triangle passes the depth test gives the compaction its input with no pass of its own: no reload of
the depth, no cleared target and no wait for the frame's earlier passes, and the compaction can run
at a quarter size or be folded into the resolve. That removes the raster's span (1.2 to 12 ms of
waiting here) and its fixed part; the trace on the list stays. It is TASK162 (marking water in the
G-buffer raster).

## Review round 1

The panel on `4ddddfbb` found one Important in both the correctness and the security pass, and the
spec pass asked for the cost to lead with its measurement. Engine commit measured: `19927e61` (the
fixes below; the commits after it are this section), against main `a0eabe11` by main's stored
records from the rework (`<artifacts>/TASK158/rework/store/M`: main has not moved, and the records
of a scenario are deterministic). Everything is in `<artifacts>/TASK158/rework2/`.

### A hidden model never leaves a hole

- *The finding.* At `r_rt_water_hits 1` the reflection ray passed hidden alias models at most four
  faces deep and then ended with no reflection. The rays cull no triangle, so a closed model is two
  faces and a player with its weapon four: the water showed a stock-colour hole where hidden models
  overlapped.
- *The fix.* The map's instances (the world and the inline brush models) carry a bit of their own,
  `MTL_RTW_MASK_MAP` (`0x10`, set by `mtl_rtw_inst_mask_ws` in `rmb_rt_frame`); alias instances do
  not. The loop's step is `mtl_rtw_ws_next` in `mtl_rtw_math.h`: a hidden alias model is passed with
  the same mask for its first three faces, as before, and at the fourth the ray is traced on with
  `MTL_RTW_MASK_MAP` alone, so it ends where it would end without the models, in at most five
  intersections. Every other choice stands as before. TASK58's kernel `rm_rtw_trace` and every
  other query's mask are unchanged: the bit is added, nothing else.
- *What is passed over, exactly.* An alias model (a player, a monster, an item, a weapon) the
  picture does not show. A hidden brush model (a door, a plat) is a map face and is shaded where it
  stands. `doc/040_cvarlist.md`, the kernel's comments and *What changed* above say so.
- *Host test* (`test_ws_models`): a driver of the kernel's loop over scripted faces, with an
  intersector that honours the masks, `mtl_rtw_choose_ws` and `mtl_rtw_ws_next`. Four to nine hidden
  model faces before a wall give `LIT|WS` (the picture's `LIT` where it shows the wall; the sky box
  or none with no wall); any number of them before any end give what that end gives alone, in at
  most five casts; the earlier cases (no model, a model the picture shows, one hidden model, a
  hidden door, a hidden model in front of a shown one) are unchanged. Red first, with the step's old
  rule: 21 of 397 checks failed, the four-face cases ending with no reflection.

### Setting 0 needs nothing of setting 1

- *The finding.* `rmb_rtw_open` refused the whole pass, setting 0 included, when a resource only
  setting 1 uses could not be built.
- *The fix.* The pass opens on what main's needs (TASK58's kernel, the blend, the show view, the
  translucent draw, the counts). The world-space path's resources (its three kernels, the
  candidates' raster and its depth state, the texture table's argument encoder and buffer, the two
  white textures) are built after them (`rmb_rtw_open_ws`); if one fails, all of them are released
  (`rmb_rtw_close_ws`) and the pass stays open, `rmb_rtw_ws_ok` saying why. `RM_RTW_View` takes the
  path from `rm_rtw_path`: setting 1 only with them, else TASK58's path, with the line `ref_metal: rt
  water: <why>; r_rt_water_hits 1 refused, its hits shaded as 0's` once a pass (again after the pass
  reopens). `rmb_rtw_prepare` refuses the world-space outputs without them as a backstop.
- *Test and trace.* The decision is host-tested (`rm_rtw_path`, in `test_cvars`: the path, the line
  once and again after a reopen, setting 0 never asking for the resources). Red first, with the
  head's rule (the path follows the cvar): 5 of 406 checks failed. No seam can make a pipeline fail
  on this device, so the Metal side is traced: with any of `trace_ws_pipe`, `compact_pipe`,
  `args_pipe`, `cand_pipe`, `cand_depth`, `tex_enc`, `tex_ab`, `white2d` or `white_arr` nil,
  `rmb_rtw_open_ws` releases all nine and returns 0, `rmb_rtw_open` still returns 1, `rmb_rtw_ws_ok`
  returns 0, so `rm_rtw_path` gives 0 and `RM_RTW_View` calls `rmb_rtw_prepare(w, h, 0)`, which needs
  only `trace_pipe` and `counts`; `rmb_rtw_trace` then takes `rmb_rtw_trace_ss`, which binds none of
  the nine. On this device all of them build: no run printed the refusal line, and the teardown
  counts are the rework's.

### The cost's headline

Design section 2's water-cost subsection now leads with the measured figures (4.66 against 3.40 ms
at 1080p held with water in view, 2.06 against 1.33 ms held without water, 1.20 against 0.99 ms not
held, and the rest of the table), labels the stages' count a re-count and not a frame measurement,
and names the follow-up, TASK162 (marking water in the G-buffer raster). The status paragraph at the
top says the same.

### Re-verified on the engine

Every run went through `tools/heavy.sh` after checking that the screen was unlocked and no other
`quake2` ran (one-minute load 1.6 to 2.3, `<artifacts>/TASK158/rework2/logs/runs.txt`). `PINS_SEED=1`,
the rework's scratch harness copy and scenarios, and the demo main's stored records played (the same
checksum, so the same record ids). Quality runs under `MTL_DEBUG_LAYER=1`: each log holds the
validation banner and no other validation line.

| Check | Result |
|---|---|
| `hits 0` against main, byte for byte (`cmp`), `rt-water` at 640 x 480 | 20 of 20 frames equal |
| The same at 1920 x 1080 | 5 of 5 equal |
| `hits 1`, masked comparison (the effect off on the G-buffer path against on, outside the `r_rt_water_show 1` mask, `framediff --mask --threshold 0`), 640 x 480 | 20 of 20 score 0 |
| The same at 1920 x 1080 | 5 of 5 score 0 |
| `hits 1` against the rework's head `c6cdd3ec`, 640 x 480 | 20 of 20 equal: no reflected ray of this demo crosses four hidden model faces |
| No reflection at `hits 1` (`r_rt_water_show 2`, 640 x 480, every frame's line) | 0 over the pool (61 lines) and 0 under it (77); from the picture 97.65 / 91.62 %, in world space 2.35 / 8.38 %, as in the rework |
| Teardown | `rt 59/59` at `0` and `61/61` at `1`, as in the rework; `0/0` with the effect off |
| Drift check, `hits 1`, 1920 x 1080 held (one run, 117 lines) | median 4.65 ms (p10 4.55, p90 4.87) against the stored 4.66 (4.55 / 4.89): inside the range, not measured again |

### Checks, this round

On `19927e61`, each through `tools/heavy.sh`: `make test` (171 tests, 0 failed),
`tests/check-ref-metal.sh` (ok; water 406 checks, 0 failed), `tests/check-pins.sh`,
`tests/check-cvar-docs.sh`, `tests/check-video-menu.sh`, `tests/check-doc-links.sh` and
`tests/check-no-local-identity.sh` (each ok); builds `make client`, `make -B ref_metal` with and
without `METAL_SHADERS=no` and `make STATIC_MODULES=1 STATIC_RENDERER=metal client` (each exit 0).

## Checks

The rework's, on the branch head: in the card's report (`make test`, `tests/check-ref-metal.sh`,
`tests/check-pins.sh`, `tests/check-cvar-docs.sh`, `tests/check-video-menu.sh`,
`tests/check-doc-links.sh`, `tests/check-no-local-identity.sh`; builds `make client`, `make -B
ref_metal` with and without `METAL_SHADERS=no`, `make STATIC_MODULES=1 STATIC_RENDERER=metal
client`). The first pass's:

`make test`, `tests/check-ref-metal.sh` (water 312 checks), `tests/check-pins.sh`,
`tests/check-cvar-docs.sh`, `tests/check-doc-links.sh`, `tests/check-no-local-identity.sh`; builds
`make client`, `make -B ref_metal` with and without `METAL_SHADERS=no`, `make STATIC_MODULES=1
STATIC_RENDERER=metal client` and `make ASAN=1 client game ref_metal` (results in the card's
report).
