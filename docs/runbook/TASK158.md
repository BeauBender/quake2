# Water reflections shaded in world space, rays only on water pixels (TASK158, RT6)

TASK158 follows [TASK58](TASK58.md) (`r_rt_water`, design
[section 2](../plans/metal-raytracing-design.md#2-the-three-effects-as-compute-passes)). It uses
the shared runbook's method for the GPU time
([the shadows' proofs](../plans/metal-port-harness.md#ray-traced-shadows-from-dynamic-lights-in-ref_metal-task55-rt3))
and TASK58's masked comparison (*The mask* in its file).

**Status.** World-space hit shading works: the share of water pixels with no reflection falls
from 2.35 % to 0 over the pool and from 8.48 % to 0 under it, and every pixel outside the water
is byte-identical to the effect off on the G-buffer path. **The cost goal is not met**: the pass
as measured costs more than TASK58's, not less (*Cost*), and the card's 0.88 ms share at 1080p
is not reached. The pins are not recorded (the owner accepts the look first).

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
  (`MTL_RTW_SKY | MTL_RTW_WS`). A model (an alias model's instance) the picture does not show is
  passed: the ray goes on behind it, at most four hits, so a hidden player neither reflects nor
  leaves a hole in the reflection behind it.
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
the `r_rt_stats 2` line adds the listed pixels, `world` and `sky box`. No new cvar.

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

## Checks

`make test`, `tests/check-ref-metal.sh` (water 312 checks), `tests/check-pins.sh`,
`tests/check-cvar-docs.sh`, `tests/check-doc-links.sh`, `tests/check-no-local-identity.sh`; builds
`make client`, `make -B ref_metal` with and without `METAL_SHADERS=no`, `make STATIC_MODULES=1
STATIC_RENDERER=metal client` and `make ASAN=1 client game ref_metal` (results in the card's
report).
