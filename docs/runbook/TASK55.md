# Ray-traced shadows from dynamic lights in `ref_metal` (TASK55, RT3)

RT3 adds the first ray-traced effect, `r_rt_shadows` (`docs/plans/metal-raytracing-design.md`,
section 2). Host: the bare-metal Mac, `Apple M6` (apple9, `supportsRaytracing: yes`), macOS
27.2.1 (26B131). Base: `origin/main` at `33845c6c` (TASK123 and TASK100; neither touches
`ref_metal`). Every run below is of engine commit `3d659a2b` (pin ids `3d659a2bb17b-*`), an
ancestor of the card's head; the commits after it change only the host test and the
documentation. Two results are the round before's, of `4deded44`, and say so: the ledger with
`r_rt_stats 1` and the shadows off (Proof 1) and the fps line (Proof 4); the commits since
change only what runs with the shadows on. After the measurements the lane merged `origin/main`
at `0fb0603c` (merged, not rebased, so `3d659a2b` stays an ancestor). That brought in TASK111
(the MetalFX scalers, which change `RM_GB_Choose`, `RM_GB_Begin`, `RM_GB_End`, `rmb_gbuf.m` and
`rm_main.c`), TASK123, TASK129, TASK100 and TASK130 (the shared image helpers). The merged head
was not measured again. With the scalers off (`r_metalfx 0`, the default, as in every run
here), `RM_GB_Choose` takes the same path as before and TASK111's other hooks do nothing;
TASK111's own bare-metal run found all 14 scenarios byte-identical to its base with the
scalers off (#265). The merge's resolution is in the merge commit. Before every run the wrapper checked that the screen
was unlocked (`ioreg -n Root -d1 | grep -c CGSSessionScreenIsLocked` 0) and that no other
`quake2` ran, and logged the one-minute load: 2.4 to 5.6 (other agents share the host). Every
engine batch held the shared timing lock. Every log, frame, mask, script and result is in
`<artifacts>/TASK55/` (this round in `rework2/`, the round before in `rework1/`) and its scratch
stores; nothing derived from game data is in the repository.

**What it does.** With `r_rt_shadows 1`, on a device with ray tracing, and with gl1's dynamic
lights on the lightmaps (`gl1_dynamic` on, neither `r_fullbright` nor `gl1_flashblend`), a frame
whose instance structure was built takes the G-buffer path. Before the raster builds the
lightmaps, `RM_RTS_View` settles everything the trace needs that can fail (the view's inverse
and the kernel's outputs at the render size, `rmb_rts_prepare`); only then does gl1's
dynamic-light term leave the lightmaps (`r_dlight_term`), and only until the resolve. A frame
that cannot trace keeps the stock lightmaps. The remaining failures are a command buffer or an
encoder Metal does not give, which lose the frame anyway. A compute pass between the raster and
the resolve (`rm_rts_trace`, `rm_rtshadow.metal`) does the following for each lightmapped world
pixel (material 1):

- reconstructs the position from the depth and takes the normal from the G-buffer;
- finds the pixel's surface by a ray from the eye to that point, through a triangle-to-surface
  table (`rm_rts_surface_map`) built when the shadows are first on for the map;
- moves the pixel into that triangle's model space (the hit's world-to-object transform, the
  identity for the world), because gl1 marks and lights a brush entity's surfaces with the
  light's untransformed world origin against the model-space planes and texture axes
  (`R_MarkBrushLights`); the light stays where it is (`mtl_rts_surf`);
- skips a surface without light samples, which gl1 lights white and adds no dynamic light to
  (`R_BuildLightMap`; the table marks its triangles, `MTL_RTS_SURF_UNLIT`);
- evaluates gl1's `R_AddDynamicLights` for every light on that surface's own texture axes and
  shifts: at the four luxels around the pixel, interpolated as the bilinear lightmap sample
  (`mtl_rts_add_lm`, `mtl_rts_surf_light`);
- traces one shadow ray toward each of the four lights that add the most (`intensity - dist`),
  which passes the models that carry its light (*a light's own model*, below).

The eye ray is cast where a light reaches the pixel's plane in the world, and on every world
pixel of a frame with a moved or turned brush entity (its pixels are tested in its model's
space, where the world's test does not hold).

It writes the four visibilities at 8 bits each with the four light indices (an `RG32Uint`) and the
term, `visibility x add x colour` summed over the lights (an `RGBA16Float`). The resolve adds the
term to the static lightmap's sample as `R_BuildLightMap`'s store would (`mtl_rts_light`: scaled
down by the brightest channel past 1). A light past the four adds its light unshadowed.

- *Ray origin.* The surface point moved along its normal by `0.5 + k d^2` units, `d` the distance
  from the eye and `k = 4 x 2^-24 / gl_znear` (`mtl_rts_bias`). The depth buffer's step at `d` is
  `2^-24 d^2 / znear`, so the start is above the true plane within four depth steps. Half a unit is
  under the thinnest brush (one unit). The ray ends half a unit short of the light.
- *Translucency.* A per-triangle transmission built at map load (`rm_rts_transmit`): water and
  translucent triangles pass 171/255 (`SURF_TRANS33`) or 87/255 (`SURF_TRANS66`), as gl1 draws
  them. Water drawn opaque, the sky, the world and models stop the ray. On a map with such
  triangles a ray looks past each one, nearest first (at most 8). Neither demo map has any
  (TASK53's counts), so the frames below do not exercise it; the host test does.
- *Alias models* are lit as before, by `R_LightPoint`, whose vertex light holds the dynamic
  lights once; the resolve adds no term to material 2. They cast shadows (they are in the
  instance structure, the view weapon only with `r_rt_gunshadow`); they receive none.
- *A light's own model.* An alias model does not shadow a light it carries: one whose origin,
  taken into the model's space, lies in gl1's box of the model's frame and old frame
  (`R_CullAliasModel`) grown by one unit on every side (`rm_rts_carried`, `RM_RTS_CARRY_TOL`).
  That is a rocket's or a bolt's light inside its own model, an explosion's at the origin of its
  model, a monster's muzzle flash at the origin of the flash and smoke models it spawns and at
  the gun of the monster that fires it. `RM_RT_Frame` finds them per frame for the lights as
  `R_TakeDlights` takes them, and `rmb_rt_frame` keeps each instance's lights beside its first
  triangle and gives a carrier its own instance mask (`mtl_rts_inst_mask`). A light no model
  carries keeps the any-hit query over every instance. The ray of a carried light first tests
  every instance but the carriers, as any light; then the carriers alone, the nearest first: it
  passes a triangle of a model that carries this light (`mtl_rts_passes`) and stops at one that
  does not (`rm_rts_visibility`; past 16 of its own model's triangles it counts as through).
  Nothing is non-opaque. Real shadows stay: the world and brush entities are never carriers,
  and another model shadows the light unless the light is inside that model's own box. The unit
  covers the rounding of the transform and a light on the box's face (a gun's tip).
- *Where it cannot match per texel.* The term is added after the store, not before:
  - the store truncates the sum and scales a saturated luxel down before filtering; the resolve
    does so per pixel after filtering (within two levels on the host test's luxels);
  - `vid_gamma` other than 1 and `gl1_minlight` are applied by gl1 to the sum and here to the
    static part only;
  - `metal_lightmap_filter 1` filters the static part with its cubic, while the term is
    interpolated bilinearly;
  - a pixel whose eye ray does not meet its own triangle (an edge) takes the world's space, the
    base axes and the continuous falloff (on a moved brush entity, its world position).

**Proof 1, effect off.** Every scenario recorded on `3d659a2b`, with `MTL_DEBUG_LAYER=1` and
`PINS_SEED=1`, against the round before's records of the base `33845c6c` (an export of the tree
built in scratch, recorded by its own `scripts/pins.sh` into a scratch store, the same settings):

- 14 scenarios, 23 frames each (`cinematic` 12), and `dlights` at `PINS_DEMO=demo2.dm2
  PINS_EVERY=7` (89 frames): byte-identical to the base on every frame. The scenarios are
  `world`, `world-fullbright`, `hud2d`, `hud2d-sharp`, `hud2d-scale8`, `world-scale8`,
  `world-lmcubic`, `skywater`, `dlights`, `dlights-lmcubic`, `models`, `particles`,
  `particles-cap` and `cinematic` (`idlog.cin`, every 7th).
- Every log holds the validation banner and no other validation line, no failed command buffer,
  `teardown: 0 live objects` and `rt 0/0`: nothing ray-traced is built or allocated.
- With `r_rt_stats 1` and `r_rt_shadows 0` (the same trace replayed by both builds; measured on
  `4deded44`) the head allocates what the base does: the same ledger (`rt 66/66`), the same
  structure lines, no shadow line, the frames identical. `rmb_rt_frame` takes no ring entry and
  keeps no scene without the shadows, and the map's shadow tables and transmission buffer are
  built when the shadows are first on. Without the shadows every instance keeps TASK53's mask
  (`0xff`) and no carrier is looked for.

Since the frames are the base's byte for byte, their scores against TASK83's `gl1` references
are the base's, those TASK54 recorded at TASK28's values. They were not compared again.

**Proof 2, effect on.** The double-count check compares, pixel for pixel, a view with the effect
on and the same view with it off, on the pixels an `r_rt_shadows_show 1` mask marks (no light in
range). Its two views must have been given the same lights. A control settled whether separate
records do: three RT-off records of `dlights` at `demo2.dm2` every 7th, and two at `demo1.dm2`,
are byte-identical, and a developer print (since removed) showed the same light radii and times
on every one of the 617 frames of each `demo2.dm2` run. The demo's frame time does not reach
the lights (in a timedemo `cl.time` steps by the demo's 100 ms). So the round before's failures
were not a frame-time effect. To give every view the same `refdef_t` all the same, each view
below is a replay of one live record's trace (the harness's TASK46 replay, bit-identical to live
by construction). The trace's cvar snapshot is the only thing that differs between the replays:
the value of `r_rt_shadows`, `r_rt_shadows_show` or `r_rt_gbuffer`, rewritten in a copy of the
trace with the same length (the replayer restores it over the command line). Each record's
"on" replay is byte-identical to the live record.

The cause of the four failing `demo2.dm2` frames (33, 36, 76, 77) was the plane, not the time.
Their lights are explosions, which sit an eighth of a unit above the floor they hit. The pass
measured each light against the G-buffer's normal, 10 bits an axis and off by about 1/1000, so
over a hundred units the light fell behind the floor on one side of a line through its foot.
The floor there went dark where gl1 lit it. The pass now takes the surface's own plane for a
found surface (`rm_rts_plane`, `mtl_rts_surf`), and the world's test before the search allows for
the normal's and the depth's error (`mtl_rts_near`). The same replays then passed.

Records (`PINS_SEED=1`): `rt-shadows` and `rt-shadows-entities` (`cl_entities 1`) at `demo1.dm2`
(23 frames) and `demo2.dm2` every 7th (89 frames). Against the G-buffer path with the effect off,
the RT-off pixel on the path the effect takes:

| Record | Frames | Masked pixels a frame | Worst max, worst masked score |
|---|---|---|---|
| `rt-shadows`, `demo1.dm2` | 23 | 48,559 to 307,100 | 0, 0.000000 |
| `rt-shadows`, `demo2.dm2` every 7th | 89 | 111,665 to 306,963 | 0, 0.000000 |
| `rt-shadows-entities`, `demo1.dm2` | 23 | 49,294 to 307,100 | 0, 0.000000 |
| `rt-shadows-entities`, `demo2.dm2` every 7th | 89 | 115,138 to 306,963 | 0, 0.000000 |

The invariant holds on every frame of the four. Against the default path the masked pixels
differ by what the G-buffer path itself differs from it with the effect off, frame for frame
the same max and score: `rt-shadows` within 1/255 at `demo1.dm2`, up to 3 on `demo2.dm2`
frame 9; with entities up to 44 (`demo1.dm2`) and 67 (`demo2.dm2`). That is TASK54's path, not
this pass.

- *Moved brush entities.* Both entity records hold doors and lifts that move (`demo1.dm2`: `*50`
  in 25 places, `*42` and `*43` in 14; `demo2.dm2`: `*32` in 33, `*36` in 9), with lights in 270
  and 238 of their frames. The host test holds the review's probe (a floor at model z 0, its
  entity at z -200, a light at world (8, 8, 50) of radius 200: gl1 adds 138 at luxel 0, and so
  does the pass) and 200 turned and moved faces at every luxel.
- *Alias models* (the round before's replays, `4deded44`). With the material view
  (`r_rt_gbuffer_show 3`): of
  1,180,417 alias-model pixels none is in range and none changes with the effect; every pixel
  the effect changes (1,895,766) is a world pixel with a light in range.
- *A frame with nothing in range* (13 of 23 at `demo1.dm2`, 59 of 89 at `demo2.dm2`): its mask is
  white wherever the 2D is not drawn. `r_rt_shadows_show` draws its view with no light in range
  when nothing is traced.

Per frame (the classes view, `r_rt_shadows_show 2`, of the same replays), pixels with a light
in range, of 307,200:

- `rt-shadows`, `demo1.dm2`: 0 to 258,122 (mean 49,046); traced 38,616 on average, 0.84 %
  shadowed;
- `rt-shadows`, `demo2.dm2`: 0 to 194,858 (mean 34,991); traced 28,392, 0.92 % shadowed;
- `rt-shadows-entities`, `demo1.dm2`: 0 to 257,387 (mean 47,999); traced 37,701, 1.71 %
  shadowed (14,793 of 867,130 traced pixels), 48.4 % before the rule;
- `rt-shadows-entities`, `demo2.dm2`: 0 to 191,385 (mean 32,929); traced 26,637, 4.76 % shadowed
  (112,935 of 2,370,702), 36.6 % before.

Both records without entities are as the round before, frame for frame. The round before's own
four traces, replayed on `3d659a2b`, give the same classes as this round's records: the same
`refdef_t` before and after the rule.

*A light's own model, the numbers.* Before the rule a monster's muzzle flash put its light at the
very origin of the flash and smoke models it spawns, and a rocket or a bolt carries its light
inside its own model, so every ray to such a light met the model around it. Per frame with a
light in range, in range / shadowed pixels, with the round before's shadowed in brackets:

- `demo1.dm2` (10 of 23 frames): 5: 12,384 / 374 (6,110); 7: 59,708 / 3,665 (40,180); 8:
  257,387 / 0 (242,931); 10: 95,665 / 0 (70,898); 14: 91,601 / 4,919 (4,919); 16: 122,324 /
  1,894 (1,894); 18: 42,828 / 0 (0); 19: 125,334 / 0 (0); 20: 195,355 / 383 (383); 21: 101,394 /
  3,558 (52,217);
- `demo2.dm2` (29 of 89 frames): 4: 179,337 / 583 (583); 5: 174,901 / 2,421 (2,421); 6: 139,373 /
  32,038 (32,038); 9: 99,101 / 0 (0); 10: 95,798 / 157 (157); 11: 103,579 / 203 (203); 12:
  101,197 / 5 (5); 13: 105,326 / 21 (21); 14: 62,825 / 55 (16,935); 15: 72,986 / 3,249 (3,249);
  21: 125,851 / 7,365 (7,365); 23: 191,385 / 1,006 (166,114); 27: 24,966 / 284 (6,153); 28:
  87,827 / 0 (0); 32: 39,748 / 2,232 (3,694); 33: 170,228 / 24,648 (93,419); 34: 11,597 / 755
  (3,166); 36: 29,269 / 4,065 (24,008); 42: 93,374 / 0 (0); 44: 109,545 / 89 (78,832); 45:
  104,158 / 793 (12,926); 47: 96,967 / 2,975 (2,975); 49: 91,112 / 30 (30); 58: 83,770 / 404
  (404); 76: 122,492 / 20,774 (83,010); 77: 7,887 / 582 (1,660); 78: 81,834 / 1 (40,296); 82:
  174,737 / 5,597 (153,984); 83: 149,518 / 2,603 (134,284).

`demo1.dm2` frame 8 goes from 242,931 shadowed pixels of 242,931 traced to none, `demo2.dm2` frame
23 from 166,114 of 166,114 to 1,006. What is left is explained by two diagnostic builds (never
committed: `RM_RT_Frame` patched in a scratch build, the same replays of the classes):

- every alias model passing every light: 0.85 % (`demo1.dm2`) and 0.81 % (`demo2.dm2`) shadowed,
  the world's and the brush entities' share, as without entities (0.84 % and 0.92 %);
- the carriers' boxes grown by 15 more units: 1.17 % and 3.14 %.

So of `demo2.dm2`'s 112,935 shadowed pixels the world and the brush entities cast 19,139, models
the light is within 16 units of but outside 38,514, and models farther from it 55,282
(`demo1.dm2`: 7,406, 4,622 and 2,765). Those are real shadows: a model between the light and the
surface, which gl1 never draws and the effect is for.

*Real shadows stay.* `demo2.dm2` frame 6 keeps its 32,038 shadowed pixels, the same as before the
rule; with every alias model passing every light only 50 are left (the world's), so models cast
the other 31,988 (22,800 of them are within 16 units of the light). Frame 21 keeps 7,365, all of
them the world's (as with the models passing), and frame 23's 1,006 are the world's too.

The per-frame lists are in `<artifacts>/TASK55/rework2/invariant/`, the diagnostics in
`rework2/e84fed75/diag/`.

**Proof 3, look.** For the owner, in `<artifacts>/TASK55/sidebyside/` (off, on and the classes
view of the same frame, replays of one trace each, `3d659a2b`; its README gives the counts). The
frames with the most shadowed pixels show the effect best:

- `rt-shadows`, `demo1.dm2` frames 21 and 7 (about 3,500 shadowed pixels);
- `rt-shadows`, `demo2.dm2` frames 21 (7,363), 36 (4,344), 76 (3,902) and 15 (2,598);
- `rt-shadows-entities`, `demo2.dm2` frames 6 (32,038) and 33 (24,648), shadows of models, and
  76 (20,774), `demo1.dm2` frame 14 (4,919);
- `rt-shadows-entities`, `demo1.dm2` frame 8 and `demo2.dm2` frame 23, the round before's open
  finding: lit now (0 and 1,006 shadowed).

**Proof 4, performance** (no validation layer; a scratch store; the timing lock held; the load
2.4 to 4.4, other agents on the host):

- `timedemo.txt` at 1920 x 1080 (`4deded44`), `dlights` and `rt-shadows` alternated, three
  rounds: 60.0, 59.9 and 60.0 fps off, 59.9, 60.0 and 59.9 on (689 frames in 11.5 s), the
  display's rate (TASK54).
- The shadow pass's GPU time per frame: timestamps at the compute encoder's boundaries, the
  `r_rt_stats 2` line, over `demo1.dm2` whole. Warm: each run's frames past its first 100 that
  traced, 308 a run (309 with entities). Three runs each of `rt-shadows` and
  `rt-shadows-entities`, alternated:

| Size | Entities | Median (run medians) | p10 | p90 | Max |
|---|---|---|---|---|---|
| 1920 x 1080 | off | 2.550 ms (2.546 to 2.557) | 1.609 to 1.624 | 3.476 to 3.488 | 4.662 |
| 1920 x 1080 | on | 3.577 ms (3.483 to 3.633) | 2.007 to 2.026 | 4.791 to 4.962 | 8.102 |
| 5120 x 2880 | off | 8.525 ms (8.486 to 8.597) | 5.427 to 5.511 | 11.602 to 11.908 | 22.912 |
| 5120 x 2880 | on | 9.053 ms (8.504 to 9.473) | 6.103 to 6.325 | 12.933 to 13.595 | 34.100 |

5120 x 2880 is `PINS_HIDPI=1` at 2560 x 1440. Rays a pixel (the eye rays and the shadow rays):
0.86 without entities, 1.09 with them; 31.5 % and 30.7 % of pixels have a light in range, 23.4 %
and 22.7 % trace. Without entities the round before measured 2.546 and 8.681 ms.

- *The rule's own cost*, against the build without it: the two 1080p traces of the records
  above replayed by three builds beside the same `quake2`, three rounds alternated. `37b7a7da`
  (the round before's head, no rule) 2.509 ms without entities and 3.275 ms with them; this
  round's first build (`e84fed75`: carriers non-opaque, the carried light's ray a ray query)
  3.048 and 3.854; `3d659a2b` (instance masks) 2.528 and 3.475. The query cost half a
  millisecond even without entities, a heavier kernel for every pixel, and was replaced; the
  masks cost 0.02 ms without entities and 0.20 ms (6 %) with them. Both builds give the same
  frames.

**Proof 5, validation and ASAN.**

- Validation: every record and replay of Proofs 1 and 2 ran with `MTL_DEBUG_LAYER=1` (15 records
  with the effect off, 4 with it on and 24 replays of them): the banner and no other validation
  line, no failed command buffer, `teardown: 0 live objects` (`rt 0/0` with the effect off; on,
  `rt 75/75` and `rt 54/54` without entities, `rt 472/472` and `rt 318/318` with them, as the
  round before).
- ASAN: `make ASAN=1 client game ref_metal`, then `debug/quake2` with `MTL_DEBUG_LAYER=1` and a
  scratch script beside the binary, deleted after with the `config.cfg` the run saves. The
  script sets `r_rt_shadows 1`, `r_rt_stats 1` and `cheats 1`, then plays `map base1`, the
  blaster fired for 200 frames (its bolts carry their light), `vid_restart`, firing again, `map
  base2` and firing again.
- Result: status 0, no sanitizer line, no validation line past the banner, the pass's targets
  made in each renderer session (2560 x 1920), an instance structure in 446, 441 and 445 frames
  (the `r_rt_stats` summaries of the three maps' sessions), both teardowns `0 live objects`
  (`rt 93/93`, `rt 186/186`).

**Host test and mutants.** `tests/metal/rm_rtshadow_test.c` (2,767 checks), run by
`tests/check-ref-metal.sh`. It holds the falloff to `rm_lm_dlight`:

- at every luxel of 600 axial faces (front and back);
- at every luxel and a point inside every cell of 400 faces with scaled, rotated, sheared and
  shifted axes;
- at every luxel of 200 faces of turned and moved brush entities, in their model's space with the
  light's world origin, and the review's probe;
- at every luxel of a floor with an explosion's light an eighth above it, the floor's normal as
  rgb10a2 returns it (55 of its 100 lit luxels have the light behind that normal);
- and none on a surface without light samples.

It also holds the in-range margin, the choice of four, the packing, the bias, the resolve's light
against `rm_lm_store`, the unprojection, the transmission, the surface map with its unlit bit,
the plane and the world's test. For a light's own model it holds gl1's box of two frames, and
`rm_rts_carried`: lights at the model's origin, inside, just inside and just outside the grown
box on each side under a turn, a second monster between a muzzle flash and a wall (it carries
none of it), the flash at the gun of the monster that fires it, a scaled and sheared transform,
400 models turned every way against the box test in their own space, 32 lights and no more, an
inverted box, a basis too small to invert and lights that are not finite; and the light bit and
the instance and ray masks.

Failing first:

- the card's first round: against a stub `rm_rtshadow.c`, 53 of 2,160 checks; with the shared
  header stubbed too, 2,045;
- rework 1, each fix reverted in a copy: the pixel kept in the world's space fails the probe;
  an unlit surface lit fails 2 checks, the table without the unlit bit 1;
- the four `demo2.dm2` frames failed the replayed check before the exact plane and pass after;
- rework 2, a light's own model: against stubs of `rm_rts_alias_box`, `rm_rts_carried`,
  `mtl_rts_light_bit` and `mtl_rts_passes`, 18 of 2,755 checks; `demo1.dm2` frame 8 and
  `demo2.dm2` frame 23 shadowed everywhere before the rule and lit after (Proof 2).

Mutants:

- the card's first round's 36: 34 killed, 2 equivalent;
- rework 1's 11, of `common/mtl_rts_math.h` and `rm_rtshadow.c`: 9 killed; the two survivors
  are equivalent (`sf.unlit = unlit` for `unlit ? 1 : 0`, the field only tested for zero; `>` for
  `>=` in the surface map's bound, an index at the bit needs 2^31 surfaces);
- rework 2's 16, of the box, `rm_rts_carried`, the light bit, `mtl_rts_passes` and the masks
  (`<artifacts>/TASK55/rework2/tdd/`): 15 killed. The survivor is equivalent: without the test
  for a transform that is not finite, a basis that is not finite gives a determinant that is not,
  and a translation that is not finite fails every box test, so the result is 0 either way. Three
  of the 16 survived the first run (an inverted box, a near-singular basis, no cap at 32 lights)
  and are killed by the checks added for them.
