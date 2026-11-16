# TASK131: the dynamic-light shadow pass finds each pixel's surface without an eye ray

RT3's pass (TASK55; the shared runbook's
[Ray-traced shadows from dynamic lights](../plans/metal-port-harness.md#ray-traced-shadows-from-dynamic-lights-in-ref_metal-task55-rt3))
found each lit world pixel's surface with a ray from the eye. The review's note N5 named that ray
as the main extra cost. This card gives the pass the surface from the G-buffer instead
(`docs/plans/metal-raytracing-design.md`, sections 2 and 3). Base: `origin/main` at `718e59d4`.

## The design

**The surface attachment.** While the shadows trace, the G-buffer raster has a sixth colour
attachment, an `R32Uint` at colour 5. It is cleared to 0. A draw from the world's static vertex
buffer (the world's faces and the brush models', `rmb_draw_world` and `rmb_draw_world_lm`) writes
`mtl_rts_gb_value`:

- the low 24 bits hold the first vertex of the triangle (the provoking vertex, `[[vertex_id]]`)
  plus one;
- the top 8 bits hold the entity that draws it: 0 for the world, i + 1 for the refdef's brush
  entity i (`RM_GB_EntityMotion`), and 255 for a brush model that is not one of the refdef's
  entities.

Every other draw writes 0: alias models, and the dynamic world draws (the sky, the warped and
flowing batches), whose vertices are not that buffer's. The tag reaches the vertex function at
buffer 4 (`rmb_gbuf_bind_world`). The vertex function and the fragment function take function
constant 5 (`rm_gb_surf`). The raster with the attachment uses a second set of four pipelines,
made at the first view that asks for it.

**The kernel's lookup** (`rm_rts_trace`, `rm_rts_gb_surface`; on the CPU `rm_rts_gb_lookup`, held
by the host test) works in this order:

1. It decodes the value (`mtl_rts_gb_decode`).
2. It takes the vertex's surface from the map's vertex-to-surface table (`rm_rts_vertex_map`,
   built beside TASK55's triangle table when the shadows are first on for the map).
3. For a brush entity, it takes the world-to-object rows from the frame's entity table. That table
   is `rm_rts_ent_rows`, the inverse of the instance transform `rm_rt_transform`, written into a
   slot of a ring of `RMB_FRAMES_IN_FLIGHT`.

The lights are then tested against the surface's own plane, in its model's space, with the light
where it is (`mtl_rts_near_plane`, gl1's test with TASK55's margin). The falloff is then
TASK55's `mtl_rts_surf` and `mtl_rts_surf_light`. A pixel whose value names no surface keeps
TASK55's path: the world's test, every pixel of a frame with a moved brush entity, and the eye
ray. Those pixels are the flowing batches' (material 1, drawn dynamically) and a brush model the
raster cannot name. A map with more vertices than `MTL_RTS_GB_VMAX` (2^24 - 2) gets no vertex
table, and then every pixel takes the eye ray.

**What did not change.** With `r_rt_shadows 0` the raster, its five attachments and its pipelines
are TASK54's. The forward path and the forward-plus-motion path never have the attachment:
`rm_vertex_world_gb` and `rm_fragment_mv` are built there with `rm_gb_surf` false, and their
varyings and outputs are as before. The shadow rays, the carried-light rule
(`rm_rts_carried`, the instance masks), the term and the resolve are TASK55's.

**Memory.** The attachment takes 4 bytes a pixel, only while the shadows trace:

| Size | By the format | Allocated |
|---|---|---|
| 1920 x 1080 | 8,294,400 bytes (7.9 MiB) | 8,486,912 bytes (the `G-buffer surface attachment` line) |
| 5120 x 2880 | 58,982,400 bytes (56.25 MiB) | 60,030,976 bytes |

The vertex table takes 4 bytes per world vertex, made once per map. The entity table takes 128 rows
of 64 bytes in each of the three ring slots, 24 KiB. The shadows' own outputs (TASK55's
`RG32Uint` and `RGBA16Float`) are unchanged.

## Host test

`tests/metal/rm_rtshadow_test.c`, run by `tests/check-ref-metal.sh`: 4,384 checks (TASK55's 2,767
and the ones its rework added, then this card's). This card adds the following:

- `test_gb_encode`: the tag and the value, decoded back for the vertices up to the table's bound
  and every entity. It also covers a vertex past the table, an entity past the frame's rows, the
  255 entity, value 0, no table, and the surface-index check.
- `test_vertex_map`: the table with the unlit bit, a vertex no surface holds, a range past the
  buffer, overlapping ranges, an end that wraps, an empty surface, a NULL input, a negative count,
  the unlit bit's bound, and the 24 bits' bound.
- `test_ent_rows`: identity for an entity in place. For 400 entities turned and moved every way, a
  model point is taken to the world by `rm_rt_transform` and back by the rows. A transform that is
  not finite gives the identity's rows, marked unusable.
- `test_gb_lookup`:
  - every vertex drawn by the world, by an entity in place, by a moved entity and by one without a
    usable row;
  - the values that name no surface;
  - through the lookup, gl1's add (`rm_lm_dlight`) at every luxel of 150 turned and moved brush
    faces, with no luxel that a light reaches dropped by the plane test.
- `test_near_plane`: over 20,000 planes, points and lights, the test before the falloff keeps every
  case in range or adding, and it does prune.

Failing first: against stubs of `rm_rts_vertex_map`, `rm_rts_ent_rows` and `rm_rts_gb_lookup`, 1,331
of 4,300 checks failed (`<artifacts>/TASK131/tdd-red.txt`).

## Checks (no engine)

Run on the card's head:

- `make test`: 163 tests, 0 failed;
- `tests/check-ref-metal.sh`: ok (12,701 host checks, ray tracing 165, G-buffer 167, shadows 4,384,
  MetalFX 319, 0 failed);
- `tests/check-pins.sh`, `tests/check-cvar-docs.sh`, `tests/check-doc-links.sh` and
  `tests/check-no-local-identity.sh`: ok;
- the builds of `client`, `ref_metal`, `ref_metal METAL_SHADERS=no` and `ref_gl1 WITH_METAL_SHIM=yes`:
  clean.

## Engine runs

Host: the bare-metal Mac, `Apple M6` (apple9, hardware ray tracing). Every run below is of engine
commit `96b9c661` (pin ids `96b9c661a702-*`), an ancestor of the card's head; the commits after it
change only this file and the design document. The "before" is TASK55's pass as it is on
`main` at `718e59d4`, the card's base: main moved on while the runs were paused, so the base was
not `origin/main` when they ran. It is an export of `718e59d4` built in scratch (pin ids
`nogit-*`), recorded by its own `scripts/pins.sh` into scratch stores. Both builds play the same
seeded scenarios and traces.

Before every run the wrapper checked that the screen was unlocked
(`ioreg -n Root -d1 | grep -c CGSSessionScreenIsLocked` printed 0) and that no other `quake2` ran,
and it logged the load. Every engine batch held the shared timing lock and released it through a
`trap`. The method is TASK55's
([the shared runbook](../plans/metal-port-harness.md#ray-traced-shadows-from-dynamic-lights-in-ref_metal-task55-rt3)).
Every log, frame, trace, script and result is in `<artifacts>/TASK131/` and its scratch stores.
Nothing derived from game data is in the repository.

### GPU time, before and after

This is TASK55's Proof 4. The pass's GPU time comes from timestamps at the compute encoder's
boundaries (the `r_rt_stats 2` line), over `demo1.dm2` whole. The scenarios are `rt-shadows` and
`rt-shadows-entities`, seeded, with no validation layer. At each size, three runs of each build
were alternated run by run. 5120 x 2880 is `PINS_HIDPI=1` at 2560 x 1440. During the batch
(16:14 to 16:19) the one-minute load was 4.23 when the lock was taken and 2.91 when it was
released, and 2.91 to 5.70 over the runs. Other agents share the host.

The warm frames are TASK55's: the frames past the first 100 that the base traced, 308 a run (309
with entities). The seeded timedemo draws the same frames in both builds, so each frame of the
head is taken at the same frame number as the base's. The head casts no ray on some of those
frames: the base's eye rays counted as traced there. So the head's own "traced" set is smaller,
283 frames a run.

| Size | Entities | Base median (run medians) | Head median (run medians) | Head p10 | Head p90 | Head max | Head - base, per frame (median) |
|---|---|---|---|---|---|---|---|
| 1920 x 1080 | off | 2.579 ms (2.561 to 2.600) | 1.662 ms (1.649 to 1.677) | 1.352 to 1.368 | 2.068 to 2.137 | 4.025 | -0.834 ms |
| 1920 x 1080 | on | 3.232 ms (3.206 to 3.273) | 1.729 ms (1.713 to 1.747) | 1.348 to 1.368 | 2.966 to 3.335 | 7.919 | -1.515 ms |
| 5120 x 2880 | off | 8.814 ms (8.551 to 9.127) | 7.298 ms (7.178 to 7.426) | 4.596 to 5.255 | 8.818 to 9.093 | 14.380 | -1.573 ms |
| 5120 x 2880 | on | 9.573 ms (9.015 to 9.963) | 7.276 ms (7.021 to 7.590) | 4.240 to 4.433 | 10.113 to 10.455 | 28.901 | -2.497 ms |

The base's spread on the same frames is p10 1.610 to 1.614 and p90 3.428 to 3.786 (1080p), 1.985
to 1.986 and 4.894 to 4.967 (1080p with entities), 5.372 to 5.749 and 11.121 to 11.364 (2880),
6.291 to 6.569 and 12.051 to 12.966 (2880 with entities). Its medians are within 0.35 ms of
TASK55's at 1080p and 0.52 ms at 2880, on a different main and load.

- *Rays a pixel* on the warm frames: 0.861 before and 0.247 after without entities, 1.092 and
  0.240 with them. The head's rays are the shadow rays and the eye rays left to the pixels the
  attachment does not name. Per frame, both builds count the same pixels in range and shadowed to
  within a few pixels. Over the 229 frames past the 100th at 1080p, the pixels traced differ by
  198 in all (1,035 with entities).
- *The pass's floor.* On the 23 frames a run with no light in range in either build, the pass
  costs the same before and after: 1.311 and 1.300 ms at 1080p (1.443 and 1.290 with entities).
  At 5120 x 2880 it is 5.93 and 6.42 ms (5.64 and 5.53 with entities), with a wide spread (p10
  3.4 to 4.3 ms, max 8.8 to 11.6 ms). The kernel still reads
  the G-buffer and writes both of its outputs at every pixel. At 5120 x 2880 that floor is most of
  the pass, which is why the gain there is smaller than at 1080p.
- *The budget.* Design section 2 gives 1.952 ms at 1920 x 1080 for four any-hit rays on every
  pixel. The head's median is under it: 1.662 ms without entities and 1.729 ms with them. Its p90
  is over it: 2.07 to 2.14 ms without entities, 2.97 to 3.34 ms with them. Those are the frames
  with lights over most of the picture, where the four shadow rays of each lit pixel are traced.

### The double-count invariant

This is TASK55's Proof 2. There are four live seeded records under `MTL_DEBUG_LAYER=1`. Each
record's trace was replayed with the effect on, with it off on the G-buffer path
(`r_rt_gbuffer 1`), with it off on the default path, and as the mask and classes views
(`r_rt_shadows_show 1` and `2`). Only the trace's cvar snapshot was rewritten. Each "on" replay is
byte-identical to its live record. The G-buffer path with the effect off is byte-identical
between the head and the base on every frame of the four records, so the check's "off" side is
TASK55's.

| Record | Frames | Masked pixels a frame | Worst max, worst masked score |
|---|---|---|---|
| `rt-shadows`, `demo1.dm2` | 23 | 48,559 to 307,100 | 1, 0.000000 |
| `rt-shadows`, `demo2.dm2` every 7th | 89 | 111,665 to 306,963 | 0, 0.000000 |
| `rt-shadows-entities`, `demo1.dm2` | 23 | 49,294 to 307,100 | 1, 0.000000 |
| `rt-shadows-entities`, `demo2.dm2` every 7th | 89 | 115,138 to 306,963 | 0, 0.000000 |

The invariant holds on every frame of the four (no masked pixel off by more than 1/255). The
"1" is four pixels of `demo1.dm2` frame 10, one level each. They are not the lookup's; see the
mask comparison below. Against the default path, the masked pixels differ as TASK55's did, by
what TASK54's G-buffer path itself differs from it. That is within 1/255 for `rt-shadows` at
`demo1.dm2`, up to 3 at `demo2.dm2` frame 9, and up to 44 and 67 with entities.

### The carried-light rule

The classes view of the entity records counts, per frame, the pixels with a light in range, the
traced pixels and the shadowed ones. It is under the mask and has players and models drawn. The
totals of the head and of the base (TASK55's pass):

| Record | Traced, head / base | Shadowed, head / base | Share | Frames whose counts differ |
|---|---|---|---|---|
| `rt-shadows`, `demo1.dm2` | 888,178 / 888,178 | 7,440 / 7,440 | 0.84 % | 2 of 23, by 1 pixel |
| `rt-shadows`, `demo2.dm2` | 2,526,921 / 2,526,923 | 23,181 / 23,181 | 0.92 % | 7 of 89, by 1 pixel |
| `rt-shadows-entities`, `demo1.dm2` | 867,132 / 867,130 | 14,795 / 14,793 | 1.71 % | 3 of 23, by 1 or 2 |
| `rt-shadows-entities`, `demo2.dm2` | 2,370,698 / 2,370,702 | 112,933 / 112,935 | 4.76 % | 7 of 89, by 1 or 2 |

These are TASK55's shares. The frames [TASK55's file](TASK55.md) names keep their counts:

- `demo1.dm2` frame 8: 0 shadowed of 242,931 traced;
- `demo2.dm2` frame 23: 1,006 shadowed;
- the models' shadows of `demo2.dm2` frames 6 (32,038) and 33 (24,648).

A model still does not shadow the light it carries, and the real shadows stay.

### The effect-on mask against TASK55's

For each record, the head's mask, classes and "on" frames were compared with the base's, frame
for frame (the same trace). Each count below is of pixels:

| Record | Mask | Classes | On frames (largest difference) |
|---|---|---|---|
| `rt-shadows`, `demo1.dm2` | 2 in 2 frames | 2 | 4 in 1 frame (1) |
| `rt-shadows`, `demo2.dm2` | 7 in 6 frames | 8 | 11 in 3 frames (38) |
| `rt-shadows-entities`, `demo1.dm2` | 2 in 2 frames | 4 | 4 in 1 frame (1) |
| `rt-shadows-entities`, `demo2.dm2` | 10 in 6 frames | 11 | 16 in 4 frames (38) |

Every pixel whose mask or class differs is isolated (no differing neighbour) and lies on a class
boundary: the edge of a light's reach. Most go from "in range" to "none" or back. Two in
`demo2.dm2` frame 21 go from shadowed to none.

A diagnostic build settled where the differences come from. It was never committed: a scratch
export of the head whose kernel never reads the attachment, while the raster still writes it. It
replayed the two `rt-shadows` records' traces. Its mask and classes equal the base's on every
frame. So every mask and class difference comes from the lookup itself. There, the surface the
raster drew at a pixel and its exact plane decide, where TASK55 had the triangle an eye ray met
and the world's test on the G-buffer's normal. They disagree only at the very edge of a light's
reach, in about one frame in ten, one or two pixels at a time.

The diagnostic's "on" frames still differ from the base's in two places. Both are pixels with no
light in range:

- the 4 pixels of `demo1.dm2` frame 10, one level each (the invariant's 1 above);
- 2 of the 8 pixels of `demo2.dm2` frame 23, by up to 3.

Those come from the raster's second set of pipelines, the ones with the attachment: the vertex
and fragment functions built with `rm_gb_surf` give a few pixels a static sample that differs in
the last levels. The pipelines without it, those of every effect-off path, are byte-identical to
the base (below). Nothing in the shaders forces both sets to compute the same position. That
would take `[[invariant]]` with `-fpreserve-invariance`, a change to the shader build flags. The
rest of the "on" differences (9 pixels in `demo2.dm2` frames 6, 23 and 28) are the lookup's edge
pixels above.

### The footprint bands

The command was `scripts/pins.sh footprint` with each cfg's own band (`darker 0..0.12`, on both
`rt-shadows.cfg` and `rt-shadows-entities.cfg`), run on the replays above. Every frame of the four
records is inside the band:

| Record | Worst darker: head (off by the cvar), head (G-buffer path off), base |
|---|---|
| `rt-shadows`, `demo1.dm2` | 0.006536, 0.006556, 0.006556 |
| `rt-shadows`, `demo2.dm2` | 0.014359, 0.014359, 0.014359 |
| `rt-shadows-entities`, `demo1.dm2` | 0.012422, 0.012422, 0.012422 |
| `rt-shadows-entities`, `demo2.dm2` | 0.099619, 0.099629, 0.099629 |

`scripts/pins.sh check-content`: `content tags ok`.

### Effect off

Every scenario was recorded seeded under `MTL_DEBUG_LAYER=1` on the head and on the base,
alternated. There are 18 scenarios at the default cadence: `world`, `world-fullbright`, `hud2d`,
`hud2d-sharp`, `hud2d-scale8`, `world-scale8`, `world-lmcubic`, `skywater`, `dlights`,
`dlights-lmcubic`, `models`, `particles`, `particles-cap`, `world-metalfx-interp`,
`world-metalfx1`, `world-metalfx2`, `world-metalfx3` and `cinematic` (`idlog.cin` every 7th, 12
frames). The 19th record is `dlights` at `PINS_DEMO=demo2.dm2 PINS_EVERY=7` (89 frames). Every frame
is byte-identical to the base's. Every head log holds `rt 0/0`: with the shadows off, nothing of
this card is allocated.

### Validation

Every record and replay above ran under `MTL_DEBUG_LAYER=1`, except the timing runs. That is 4
live records, 36 replays of them, the 6 diagnostic replays, 38 effect-off records and the 3 ASAN
runs. Each log has the banner and no other validation line, no failed command buffer, and
`teardown: 0 live objects`. The "on" replays of `rt-shadows` at `demo1.dm2` show what this card
adds: `texture 240/240, state 70/70, rt 77/77` against the base's `239/239, 66/66, 75/75`. That is
the attachment, the four pipelines with it, and the vertex and entity tables.

### ASAN

`make -j4 ASAN=1 client game ref_metal`, then three runs under `MTL_DEBUG_LAYER=1`:

- `rt-shadows` and `rt-shadows-entities` recorded through `scripts/pins.sh` with
  `PINS_BIN=debug/quake2` (`demo1.dm2`, 23 frames each): no sanitizer line, the banner only,
  teardown `0 live objects` (`rt 77/77`, `rt 320/320`).
- TASK55's session, a scratch cfg beside the binary (deleted after, with the `config.cfg` the run
  saves): `r_rt_shadows 1`, `map base1` with the blaster fired, `vid_restart`, firing again, then
  `map base2` and firing again. It exited with status 0. It printed no sanitizer line and no
  validation line past the banner, and the three maps' sessions ran 440, 443 and 444 frames. The
  attachment was made in each renderer session at 2560 x 1920 (19,660,800 bytes by the format,
  19,922,944 allocated). Both teardowns were `0 live objects` (`rt 99/99`, `rt 209/209`).

### Defects

None found, so no code changed after the runs. The checks without the engine were run again on
`4b963326`, the runs' documentation commit:

- `make test`: 163 tests, 0 failed;
- `tests/check-ref-metal.sh`: ok, with 12,701 checks (shadows 4,384) and 0 failed;
- `tests/check-pins.sh`, `tests/check-cvar-docs.sh`, `tests/check-doc-links.sh` and
  `tests/check-no-local-identity.sh`: ok;
- the builds of `client`, `ref_metal`, `ref_metal METAL_SHADERS=no` and
  `ref_gl1 WITH_METAL_SHIM=yes`: exit 0.
