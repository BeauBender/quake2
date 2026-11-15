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
| 1920 x 1080 | 8,294,400 bytes (7.9 MiB) | pending (the `G-buffer surface attachment` line) |
| 5120 x 2880 | 58,982,400 bytes (56.25 MiB) | pending |

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

## Engine runs: pending

The coordinator paused every engine run on the host before any was started (the display was in
use). None of the following has run yet. Each run takes the timing lock, checks the screen and
that no other `quake2` runs, and logs the load
([the shared runbook's method](../plans/metal-port-harness.md#ray-traced-shadows-from-dynamic-lights-in-ref_metal-task55-rt3)):

1. **GPU time, before and after.** This is TASK55's Proof 4 method: the `r_rt_stats 2` line over
   `demo1.dm2` whole, the warm frames past the first 100 that traced. The scenarios are
   `rt-shadows` and `rt-shadows-entities`, seeded. The sizes are 1920 x 1080 and 5120 x 2880
   (`PINS_HIDPI=1` at 2560 x 1440). The builds are the base `718e59d4` and the head, three runs
   each, alternated.
2. **The double-count invariant** on every frame of both `rt-shadows` scenarios: `demo1.dm2` (23
   frames) and `demo2.dm2` every 7th (89), seeded, replays of one live trace per record with the
   effect on, off and the mask (TASK55's Proof 2).
3. **The carried-light rule** with players and models drawn (`rt-shadows-entities`): the classes
   per frame against TASK55's.
4. **The effect-on mask against TASK55's**, frame for frame: the count of differing pixels and
   where they are.
5. **The `// footprint` bands** of both cfgs (`scripts/pins.sh footprint` and `check-content`).
6. **Effect off**: every scenario byte-identical to the base.
7. **Validation**: one `MTL_DEBUG_LAYER=1` run with a matched teardown.
8. **ASAN**: one `ASAN=1` build run on `rt-shadows`.
