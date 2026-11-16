# Ray-traced ambient occlusion in `ref_metal` (TASK57, RT5)

RT5 adds the second ray-traced effect, `r_rt_ao`
([design](../plans/metal-raytracing-design.md), section 2, row RT5 of section 6). This file
holds the card's results. The method sections it uses are in the shared runbook: the
[shadows' proofs](../plans/metal-port-harness.md#ray-traced-shadows-from-dynamic-lights-in-ref_metal-task55-rt3)
(the replays of one trace with a cvar rewritten, the GPU time from the encoder's timestamps)
and [judging an effect's footprint](../plans/metal-port-harness.md#judging-an-effects-footprint-task133).

**Status.** The pass, its cvars, its host test, the scenarios and the documentation are in
the branch, and the engine runs are made (*Runs on the bare-metal host*, below). Effect off,
every frame is `origin/main`'s byte for byte. Effect on, the factor never goes under
`r_rt_ao_min`, and the frame-level floor check passes all 270 frames with its allowance
derived (*Rework 1*): the four frames the first check failed are frames where gl1's screen
blend rounds the 8-bit frame a second time, in both views, which that check's one level did
not allow for. With the shadows on as well, the pixels that come out brighter than the
shadows-only frame are gl1's rule for a light sum past 1 (normalise by the brightest
channel), applied to the AO'd lightmap term; the resolve already follows it. The `rt-ao`
pins are not recorded: the owner accepts the look first. Main's TASK131 (the shadows' surface
attachment) is merged in and the engine results re-verified on the merge (*After merging
TASK131*).

## What it does

With `r_rt_ao 1`, on a device with ray tracing and with the world lightmapped (`r_fullbright
0`), a frame whose instance structure was built takes the G-buffer path. Before the raster,
`RM_RTAO_View` settles what the trace needs that can fail: the view's inverse (TASK55's
`rm_rts_view`) and the outputs at the render size (`rmb_rtao_prepare`). A compute pass
between the raster (and RT3's kernel, when the shadows are on) and the resolve (`rm_rtao_trace`,
`rm_rtao.metal`) takes each lightmapped world pixel (material 1):

- its position from the depth and its normal from the G-buffer, as RT3 does;
- two rays, cosine-weighted over the hemisphere about the stored normal (`mtl_rtao_dir`), the
  two azimuths in the two halves of the circle (stratified), each `r_rt_ao_range` units long;
- what a ray meets: `(1 - t / range)^2` of the pixel's ambient light is taken for a hit at
  distance `t` (`mtl_rtao_occlusion`), so a face at the pixel (a crease) takes it all and one at
  the ray's end none; the AO is one minus the two rays' mean (`mtl_rtao_ao`).

It writes the AO at 8 bits (`R8Unorm`) and the light the resolve reads (`RGBA16Unorm`, which
holds every 8-bit value exactly): a world pixel's lightmap sample times the factor, the 8-bit
AO clamped to `[r_rt_ao_min, 1]` (`mtl_rtao_factor`, `mtl_rtao_light`); every other pixel's
light as the raster wrote it. The resolve (`rmb_gbuf_resolve`) binds that light in place of the
light attachment, for the stock resolve and for RT3's alike, so the product is the design's
`albedo x (lightmap x AO + dlight x vis)`: the AO scales the lightmap term only, and RT3 adds
its dynamic-light term after it. With `r_rt_shadows 0` gl1's dynamic lights are in the
lightmap itself (there is no separate term), and the AO scales them with it.

- *Ray origin.* The surface point moved along the stored normal by `0.5 + k d^2` units, `d` the
  distance from the eye and `k = 4 x 2^-24 / gl_znear` (`mtl_rtao_bias`, TASK55's rule): the
  start is above the true plane wherever the depth is within four float steps. Every ray is at
  least 1.8 degrees above the stored plane (`MTL_RTAO_MIN_COS`, a cosine of 1/32). The stored
  normal (10 bits an axis) is within 0.0018 radians (0.1 degree) of the true one, so a ray never
  points below the true plane either: from a start above the plane, moving away from it, a ray
  cannot meet it at any range. A pixel does not occlude itself.
- *Geometry tested.* The frame's instance structure (TASK53) with a new mask bit,
  `MTL_RTAO_MASK` (`0x04`), which `rmb_rt_frame` sets on every instance but the view weapon
  and the viewer's own model (`rm_rt_inst_t.noao`, set by `rm_rt_instances` from
  `RF_WEAPONMODEL` and `RF_VIEWERMODEL`). The rays meet the world, the brush entities (doors,
  lifts, trains, in their moved place) and the alias models (monsters, players, items), so a
  model darkens the floor beside and under it. They do not meet the view weapon: it is in the
  structure only with `r_rt_gunshadow 1`, and then without the bit, since from the eye it would
  darken the floor in front of the player. They do not meet the viewer's own model: the client
  keeps it out of the refdef in the first person (`cl_entities.c`), and an instance of it would
  have no bit. Sprites, beams, shells and particles are not in the structure. TASK55's two bits
  stay as they were, and a shadow ray's three masks (`0xff`, `0x01`, `0x02`) meet the same
  instances as before (the host test holds it).
- *What passes a ray.* The map's per-triangle material ids (`rm_rt_materials`, bound by
  `rmb_rt_bind_materials`): a sky triangle is open (outdoors, a low sky brush must not darken
  the ground), and water and translucent triangles let the ambient light through. A ray passes
  up to four of them, the nearest first. Everything else, alias models included, occludes.
- *Noise.* A 64 x 64 blue-noise tile made in code when the pass opens (`rm_rtao_bluenoise`):
  Ulichney's void-and-cluster on the torus, a Gaussian of sigma 1.5, integer energies, the
  initial pattern from a fixed generator, so the tile is the same on every run and host. A
  pixel's four numbers (two rays, azimuth and elevation) are the tile at four offsets
  (`mtl_rtao_tile_index`), each moved on per frame by a constant of the R2 and R4 sequences and
  wrapped (`mtl_rtao_noise`). With `r_rt_ao_seed` 1 or more the frame is that seed on every
  frame, so the frames are stateless (the scenarios set 1); with `0` the noise moves on every
  frame. Without RT7's accumulation the darkening is grainy.
- *Where it does nothing.* Models (their vertex light), water, the sky and `r_fullbright`
  surfaces keep their light; a world pixel without depth or normal reads AO 1.

## Cvars

`r_rt_ao` (on switch, archived, default `0`), `r_rt_ao_range` (archived, `128`, clamped to 1
to 8192), `r_rt_ao_min` (archived, `0.4`, clamped to 0 to 1), `r_rt_ao_seed` (development,
`0`) and `r_rt_ao_show` (development view: `1` the factor in grey, `2` the classes, white
unoccluded, blue occluded, red at the floor, black not a world pixel). `r_rt_ao_show` is in the
multiplayer cheat reset (`cl_main.c`), as RT3's view is; the others cannot make anything easier
to see (the factor only darkens, and never below the floor). All are in
`doc/040_cvarlist.md`. With `r_rt_stats 2` each traced frame prints a line: the pixels, the
world pixels traced, those occluded, those at the floor, the pixels whose normal is within 8
degrees of up (floors) and of them those occluded, the rays, the rays that met an occluder and
the pass's GPU time.

## The host test

`tests/metal/rm_rtao_test.c`, run by `tests/check-ref-metal.sh` (164 checks):

- *An open plane reads AO 1.* For 400 views (eye at random, planes of every orientation facing
  it, 8 to 3,000 units away, at grazing angles too), 40 pixels each: the true point on the plane
  at the pixel's centre, its depth as `depth32Float` holds it and up to four float steps off, its
  normal as `rgb10a2` holds it, then the kernel's unprojection, start and 64 frames of noise for
  both rays. None of 33,055 starts is below the plane, none of 4,231,040 rays meets it within
  the range (from above, or from below going up, as the structures cull no face), and the light
  is the effect-off light bit for bit.
- *The floor.* At any factor, in each of gl1's lightmap environments (modulate, combine with its
  scale, replace for `r_lightmap`), the resolve's world pixel is at least `r_rt_ao_min` times the
  effect-off pixel and at most that pixel, per channel.
- The factor 1 leaves the light bit for bit; only material 1 is scaled.
- The tile is a permutation, the same on every call, blue (no two of the first sixteenth of the
  ranks within 2.8 pixels, of the first tenth within 2; every 8 x 8 block's mean rank within 0.03
  of the middle, where white noise gives about 0.09).
- The directions are unit vectors, at least `MTL_RTAO_MIN_COS` above the plane, the two rays'
  azimuths opposite, the mean cosine 2/3 (cosine-weighted).
- The instances: with `r_rt_gunshadow 1` the gun and a `RF_VIEWERMODEL` entity are instances
  without the AO bit, every other instance has it, and TASK55's masks meet each as before.
- The cvars' readings, the view (the same inverse, viewport, eye and bias as RT3's) and the
  stats line (its exact text and length).

Seven mutants of `common/mtl_rtao_math.h` fail it: the start without its half unit, the start
without the depth term, no minimum elevation, the sky occluding, a linear falloff, the gun with
the AO bit, every material scaled.

## Checks

On the branch head (no engine run):

- `make test`: `yq2test: 163 tests, 0 failed`.
- `tests/check-ref-metal.sh`: ok (AO 164 checks, 0 failed; `ref_metal` built).
- `tests/check-pins.sh`: `pins selftest: ok` (with `content_tags`: `rt-ao` and its companion
  `rt-ao-entities`).
- `tests/check-cvar-docs.sh`, `tests/check-doc-links.sh`, `tests/check-no-local-identity.sh`: ok.
- Builds: `make client`, `make -B ref_metal` with and without `METAL_SHADERS=no`, `make
  ref_gl1 WITH_METAL_SHIM=yes`: no error, no warning in the new files.

## Runs on the bare-metal host

Host: the bare-metal host (Apple M6, apple9, `supportsRaytracing`). Engine commit measured:
`4654bfeb` (pin ids `4654bfebcdf9-*`), an ancestor of the card's head; the commits after it
change only the scenario cfgs' comment headers (the footprint band) and the documentation.
Before every engine run the wrapper checked that the screen was unlocked and that no other
`quake2` ran; every engine batch held the shared timing lock (taken with `mkdir`, released by a
`trap`, at most about two and a half minutes a hold) and logged the one-minute load before and
after each run: 2.3 to 5.4 (three other lanes share the host). Every record is `PINS_SEED=1`.
The views compared within one record are replays of its trace (TASK46), with renderer cvars
rewritten in a clone of the trace at the same length, as TASK55 did: `r_rt_ao 1` to `0` with
`r_rt_gbuffer 0` to `1` (the *off* view, the same G-buffer path), `r_rt_ao_show` to `2` (the
classes) or `1` (the factor), `r_rt_stats` to `2` (the stats line), `r_rt_ao_range 128` to
`002`, `r_rt_ao_min 0.4` to `1.0`. Each record's trace replayed as it is gives the live record
byte for byte (224 of 224 frames). Every log, frame, tool and list is in
`<artifacts>/TASK57/` (scratch stores `store/` and `store-base/`, `logs/`, `tools/`); nothing
derived from game data is in the repository.

### Effect off

Every scenario recorded on the head with `MTL_DEBUG_LAYER=1`, against records of `origin/main`
(`718e59d4`, an export of the tree built in scratch and recorded by its own `scripts/pins.sh`
into a scratch store, the same settings): the 14 scenarios of TASK55's Proof 1 at `demo1.dm2`
(23 frames each; `cinematic` 12, `idlog.cin` every 7th), `dlights` at `demo2.dm2` every 7th (89),
and `rt-shadows` and `rt-shadows-entities` at both demos. **19 records, 624 frames, every one
byte-identical to the base.** Every log holds the validation banner and no other validation
line, and `teardown: 0 live objects`; `rt 0/0` for the 15 records without an effect, and for
the shadows `rt 75/75` and `54/54` (without entities, `demo1.dm2` and `demo2.dm2`), `318/318`
and `472/472` (with them). So with `r_rt_ao 0` nothing of the pass runs, and a shadow ray's
masks meet the same instances now that they carry the AO bit.

### Effect on: the floor check

`rt-ao` and `rt-ao-entities` at `demo1.dm2` (23 frames) and `demo2.dm2` every 7th (89), each
against its off replay with `aofloor off on 0.4` (`<artifacts>/TASK57/tools/aofloor.c`; its
allowance as rework 1 derived it, *Rework 1*, part 1: the channels below `0.4 x off - 1.402`
levels, two 8-bit roundings, the resolve's and the screen blend's, and the light's 16-bit
store, and above `off`; self-tested on frames scaled by 0.5, 0.3 and 1.02). The first round
allowed one level, and failed 4 frames (demo2 frame 42 and the 1080p frame 3, 33 to 38
channels each); *Rework 1* has the cause.

| Record | Frames | Pass | Channels under | Brighter | Smallest on / off (off >= 16) | Largest `0.4 x off - on` |
|---|---|---|---|---|---|---|
| `rt-ao`, `demo1.dm2` | 23 | 23 | 0 | 0 | 0.353 | 1.0 |
| `rt-ao`, `demo2.dm2` every 7th | 89 | 89 | 0 | 0 | 0.348 | 1.2 |
| `rt-ao-entities`, `demo1.dm2` | 23 | 23 | 0 | 0 | 0.353 | 1.0 |
| `rt-ao-entities`, `demo2.dm2` every 7th | 89 | 89 | 0 | 0 | 0.348 | 1.2 |
| `rt-ao`, `demo1.dm2`, 1920 x 1080 | 23 | 23 | 0 | 0 | 0.353 | 1.2 |
| `rt-ao-entities`, `demo1.dm2`, 1920 x 1080 | 23 | 23 | 0 | 0 | 0.353 | 1.2 |

- *The factor itself never goes under the floor.* The factor view (`r_rt_ao_show 1`) of the
  four 640 x 480 records: on all 66,697,514 world pixels (the classes view's white, blue and
  red) of the 224 frames the smallest factor is 102/255, `r_rt_ao_min` exactly.
- *The channels past one rounding.* A shortfall over 0.702 of a level (one rounding's bound) is
  only in frames that carry a screen blend: `demo1.dm2` frames 3, 5, 8, 11 and 16 (both sizes),
  `demo2.dm2` frames 2, 10, 23, 42, 44, 45, 47, 78, 79 and 82 (*Rework 1*).

### Effect on: unchanged pixels and the open floor

- *AO 1 is the stock light.* Against the off view, on the classes view's white pixels: 0 of
  3,764,002, 16,290,064, 3,597,160 and 15,093,412 differ (the four records, every frame).
- *Floors that read 1* (the stats line: pixels whose normal is within 8 degrees of up, and of
  them those occluded): 69.6 % (`rt-ao`, `demo1.dm2`), 80.4 % (`demo2.dm2`), 66.6 % and 75.6 %
  with entities. The rest is floor within 128 units of a wall, a step or a model.
- *A measured open floor.* The largest all-white square in the lower half of a classes view:
  `rt-ao` `demo1.dm2` frame 3, 140 x 140 pixels at (162, 311) from the top left (19,600 pixels
  at AO 1); `demo2.dm2` frame 40, 216 x 216 at (365, 240).
- *No self-occlusion.* With `r_rt_ao_range 2`, where only a crease within two units can
  occlude, 0.74 % (`rt-ao`, `demo1.dm2`), 0.82 % (`demo2.dm2`) and 0.85 % (`rt-ao-entities`,
  `demo1.dm2`) of traced pixels are occluded, and 0.30 %, 0.33 % and 0.47 % of floor pixels,
  against 45.4 %, 39.4 % and 47.2 % at the default 128. A pixel that occluded itself would read
  occluded at any range.
- *What the pass touches* (the stats line at 128): 99.3 %, 99.9 %, 97.4 % and 95.9 % of pixels
  are traced world pixels; of them 39 to 47 % read under 1 and 1.6 to 2.0 % at the floor; 24.5
  to 30.4 % of rays meet an occluder.

### Effect on: the footprint

`scripts/pins.sh footprint` (TASK133), the off replay against the record, the share of the
picture made darker by more than 8 levels:

| Record | Frames | Median | p90 | Worst (frame) | Mean |
|---|---|---|---|---|---|
| `rt-ao`, `demo1.dm2` | 23 | 0.024 | 0.091 | 0.230 (7) | 0.040 |
| `rt-ao`, `demo2.dm2` every 7th | 89 | 0.016 | 0.092 | 0.385 (30) | 0.036 |
| `rt-ao-entities`, `demo1.dm2` | 23 | 0.025 | 0.096 | 0.230 (7) | 0.044 |
| `rt-ao-entities`, `demo2.dm2` every 7th | 89 | 0.015 | 0.091 | 0.383 (30) | 0.037 |

No frame has a brighter pixel. The largest change is 83 levels (`demo1.dm2`) and 72
(`demo2.dm2`). The players and models of the entities records change the picture by about as
much as the world alone. Band: `// footprint darker 0..0.5` in both cfgs, 1.3 times the worst
frame without entities and under the 50 to 70 % of the frame that TASK55's first round
darkened; with it every frame of the four is inside the band.

### Both effects on

The `rt-shadows` traces (`demo1.dm2`, 23 frames; `demo2.dm2` every 7th, 89) replayed with
`r_rt_ao 1` and `r_rt_ao_seed 1`, with the shadows off as well, and with `r_rt_shadows_show 1`
(the mask of no light in range):

- where no dynamic light is in range (5,928,483 and 24,179,352 masked pixels) the frame equals
  the AO-only frame of the same trace byte for byte, every frame;
- against the shadows-only frame (`aofloor shadows both 0.4`, the derived allowance): no
  channel under on either demo (the first round's 34 on `demo2.dm2` were the screen blend's
  second rounding). Where a light is in range 48,146 channels (5 frames, `demo1.dm2`) and
  35,254 (12 frames, `demo2.dm2`) come out brighter than the shadows-only frame: gl1's rule for
  a light sum past 1, which divides all three channels by the brightest, applied to the AO'd
  lightmap term plus the dynamic lights' (*Rework 1*, part 2).

### Frames for the owner

Four off/on pairs at 1920 x 1080, replays of one trace each, converted with `sips`, in
`<artifacts>/TASK57/frames/` (`<n>-off.png`, `<n>-on.png`, and a `README.txt`), never committed.
They are the frames of `rt-ao-entities` at `demo1.dm2` with the most occluded pixels in the
classes view: frames 7, 8, 19 and 13, with 1,387,369, 1,376,902, 1,298,073 and 1,232,492
pixels of 2,073,600 under 1 (84,965, 74,397, 47,482 and 7,923 at the floor).

### Cost

The pass's GPU time per frame, from the compute encoder's timestamps (the `r_rt_stats 2`
line), over `demo1.dm2` whole, warm (each run's lines past its first 100: 587 frames a run),
three runs each of `rt-ao` and `rt-ao-entities` alternated, no validation layer:

| Size | Entities | Median (run medians) | p10 | p90 | Max |
|---|---|---|---|---|---|
| 1920 x 1080 | off | 3.673 ms (3.630 to 3.673) | 3.096 to 3.166 | 4.599 to 4.718 | 6.748 |
| 1920 x 1080 | on | 4.102 ms (3.829 to 4.228) | 3.066 to 3.222 | 4.799 to 5.143 | 6.569 |
| 5120 x 2880 | off | 10.955 ms (10.928 to 11.123) | 9.395 to 9.481 | 11.694 to 11.851 | 13.790 |
| 5120 x 2880 | on | 10.965 ms (10.909 to 11.144) | 9.352 to 9.372 | 11.786 to 11.871 | 14.303 |

5120 x 2880 is `PINS_HIDPI=1` at 2560 x 1440. Two rays a traced pixel. The third 5120 x 2880
round overlapped a merge-queue build on the host that did not take the lock; its medians are
1.5 to 2 % above the first two rounds'. The pass's targets: 2.8 MB at 640 x 480, 19.2 MB at
1920 x 1080, 135.1 MB at 5120 x 2880.

`timedemo.txt` at 1920 x 1080, `world` and `rt-ao` alternated, three rounds: 60.0, 60.0 and 60.0
fps off, 60.0, 60.0 and 60.0 on (689 frames in 11.5 s), the display's rate.

### Validation and ASAN

- Validation: 26 effect-on records and replays under `MTL_DEBUG_LAYER=1` (the four records and
  their off, classes, as-is and range-2 replays, the six of *Both effects on*, the ASAN record)
  hold the banner and no other validation line; all 37 effect-on logs (those, the factor views
  and the 1080p runs) end in a matched teardown, `0 live objects`, `0 unreleased, 0 unmatched`,
  with `rt 73/73` and `52/52` (`rt-ao`, the two demos) and `316/316` and `470/470` (with
  entities).
- ASAN: `make ASAN=1 client game ref_metal` (both binaries link the sanitizer's runtime), one
  `rt-ao` record at `demo1.dm2` with `MTL_DEBUG_LAYER=1`: no sanitizer line, no validation
  line, the teardown matched (`rt 73/73`), and the 23 frames byte-identical to the release
  build's.

## Rework 1: the floor check's four frames, the two effects together, the cost

No engine code changed in this round: the first finding was the tool's, the second is gl1's
rule. The replays of this round ran the build of `78eb9dba` (pin ids `78eb9dba437d-*`), whose
`src/` and `Makefile` are `4654bfeb`'s, the engine commit of the runs above. The new views are
replays of the records' traces with one cvar rewritten, as above: `r_rt_gbuffer_show` to `1`
(the albedo attachment as stored) and `5` (the light attachment), `r_rt_ao_show` to `1`. Every
replay held the timing lock (load 2.0 to 3.8 before and after); the tools (`aomodel`,
`aobright`, `aoprobe`, the corrected `aofloor`) are in `<artifacts>/TASK57/tools/`.

### 1. The floor check's four frames: a second rounding the check left out

**Cause: gl1's screen blend, in both views.** `R_PolyBlend` (`rm_main.c`, from `gl1_main.c`)
draws the refdef's blend over the 3D view after the resolve, into the 8-bit frame. The
scenarios set `cl_blend 0`, but the client clears only the blend's colour
(`VectorClear(cl.refdef.blend)` in `cl_view.c`, three components) and keeps its alpha, so on a
frame whose player state carries a blend (a pickup's flash, damage) `rm_polyblend` draws a
black quad of that alpha: the frame is `rne((1 - alpha) x D)`, `D` the resolve's 8-bit pixel.
The off and on views both have it, so each is rounded twice, and the check allowed for one
rounding. Nothing of the AO rounds there: the pass adds only the light's 16-bit store.

**Proof.** With the albedo and light attachments of the off traces (`demo2.dm2`, 89 frames;
the 1080p `demo1.dm2`, 23), `aomodel` forms gl1's MODULATE product as the resolve does, the
on view's light the factor times the light stored at 16 bits, rounds it to 8 bits, then
applies a black blend of alpha `k/255` with `k` fitted per frame (0 to 160). It reproduces every
world channel of both views of the 112 frames except the ones that something drawn after the
resolve covers: the status bar (1,065 to 2,403 channels a frame at 640 x 480, 2,985 to 15,432 at
1080p) and, in the 1080p frame 21, 259,000 more. (In the on views of `demo2.dm2`, read with the
factor view, up to 67 channels a frame more are one level off: products within float error of
a half level, which the model and the GPU multiply in another order.) The fitted alpha is 0 on
most frames and:

- `demo2.dm2`: frames 2 (63/255), 10 (81), 23 (71), 38 (5), 42 (15), 44 (51), 45 (45), 47 (45),
  78 (81), 79 (76) and 82 (96);
- `demo1.dm2` (1080p): frames 3 (5/255), 5 (61), 8 (51), 11 (30) and 16 (56).

Two failing channels worked through (x, y from the top left; red; the factor at the floor,
102/255 in the factor view):

- `demo2.dm2` frame 42 (314, 241): albedo 96, light 63, blend 15/255. Off: the resolve
  `96 x 63 / 255` = 23.72, stored 24; the blend `24 x 240/255` = 22.59, so 23. On: the light
  `63 x 0.4` = 25.2, the resolve `96 x 25.2 / 255` = 9.49, stored 9; the blend `9 x 240/255` =
  8.47, so 8. The first check wanted at least `0.4 x 23 - 1` = 8.2. Before the blend the on
  pixel, 9, is `0.4 x 24 - 0.6`, inside one rounding.
- 1080p frame 3 (144, 34): albedo 110, light 159, blend 5/255. Off: 68.59, stored 69, then
  `69 x 250/255` = 67.65, so 68. On: 27.44, stored 27, then 26.47, so 26 (the check wanted 26.2).

**The allowance, derived.** Let `X` be the off view's exact product in levels and `m` the
floor. The factor is at least `m`, and the light's `RGBA16Unorm` store errs by at most half a
step of 65,535, so the on view's product is at least `m X - e`, `e = 255 / (2 x 65535)`, 0.002
of a level. The resolve stores both at 8 bits, `D = rne(X)` and `D' = rne(on)`; the blend
makes `F = rne(s D + c)`, `s = 1 - alpha`, `c >= 0` its colour's part (0 here). Then

    m F_off - F_on <= m (s (X + 1/2) + c + 1/2) - (s (m X - 1/2 - e) + c - 1/2)
                    = (1 + m)(1 + s) / 2 + s e - (1 - m) c
                   <= 1 + m + e,

and without a blend (`F = D`, one rounding) at most `(1 + m) / 2 + e`. With `m` 0.4 that is
1.402 levels with a blend and 0.702 without; the first check's one level lay between them, more
than one rounding needs and less than two do. The clamp at 255 only lowers `F_off`. Every stage
is monotonic in the light, so no channel can come out brighter than the off view's.

**The check, corrected** (`aofloor`: the bound `1 + m + e`, and as information the channels
past `(1 + m) / 2 + e`): all 270 frames pass, no channel under and none brighter (the table in
*Effect on: the floor check*). The largest shortfall `0.4 x off - on` is 1.2 levels, in the
four frames that failed before. The channels past one rounding are only in frames that carry a
blend: those of the two fitted records are the blend frames above but `demo2.dm2` frame 38
(alpha 5/255, no channel past it), and the 640 x 480 `demo1.dm2` records' are its frames 3, 5,
8, 11 and 16, the 1080p record's blend frames (one trace). The self-test holds: frames scaled by
0.5 pass, by 0.3 fail under, by 1.02 fail brighter.

So the engine does not round where it need not: the blend over the 8-bit view is gl1's own
pass (gl1 draws it over its 8-bit framebuffer), and the pass's own stages add 0.002 of a level.
The tool's model was wrong, and its first allowance was not derived. Re-run after the
correction: the floor check on all 270 frames (above) and against the shadows-only frames
(part 2). No engine code changed, so the effect-off, `rt-shadows`, validation and ASAN results
of the first round stand for this head.

### 2. Shadows and AO together: gl1's normalisation of a light sum past 1

**The guess holds, and the resolve already follows gl1's rule.** With both effects on,
`rmb_gbuf_resolve` binds the AO pass's light (`lt`, a world pixel's lightmap sample `L` times
its factor `f`) to RT3's resolve (`rmb_rts_resolve`, `rm_fragment_resolve_rts`), which adds the
dynamic lights' term `T` and stores the sum as `R_BuildLightMap` does (`mtl_rts_light`): where
the brightest channel passes 1, all three are divided by it. That is gl1's rule for the same
sum: `gl1_light.c`'s store, after `R_AddDynamicLights` has added the dynamic lights to the
lightmap's sum, rescales all the colour components by 255 over the brightest when it exceeds
255. It is a normalisation, not a per-channel clamp and not an overbright clamp. So the AO and
shadow resolve is `(f L + T) / max(1, brightest channel of (f L + T))`: gl1's rule, with the AO
applied to the lightmap term before it, as the design composes them. Nothing is changed, and
the shadows-only output is untouched.

**Why it brightens.** With the brightest channel's sum past 1, every channel is divided by it.
The factor lowers the static part of that sum, so the divisor falls (or the division goes), and
a channel whose sum is mostly the dynamic light's keeps more of it. A per-channel clamp could
not brighten anything: `min(1, f L + T) <= min(1, L + T)` channel by channel.

**One pixel by hand** (`demo1.dm2` frame 21 of the `rt-shadows` trace, (495, 338); the
attachments and the factor from `r_rt_gbuffer_show 1`, `5` and `r_rt_ao_show 1` replays of the
both trace): albedo (127, 97, 70), static light `L` = (196, 133, 46)/255 = (0.769, 0.522,
0.180), factor 155/255 = 0.608, so `f L` = (0.467, 0.317, 0.110). Shadows only (127, 78, 10),
both (125, 81, 8).

- Shadows only, red: 127, the albedo's own value, so the stored light's red is 1: the sum was
  divided by its red, `M = 0.769 + Tr`, above 1.
- Both, red: 125 = `127 x (0.467 + Tr)` within half a level, so `0.467 + Tr` is 0.980 to 0.988,
  under 1: no division. So `Tr` is 0.513 to 0.521, and `M` 1.282 to 1.290.
- Both, green: 81 puts `0.317 + Tg` at 0.830 to 0.840, `Tg` 0.513 to 0.523; then the shadows-only
  green `(0.522 + Tg) / M` is 0.802 to 0.815, and 78 of 97 reads 0.799 to 0.809: they meet.
- Blue: both 8 puts `Tb` at 0 to 0.012; shadows only `(0.180 + Tb) / M` is 0.140 to 0.150, and 10
  of 70 reads 0.136 to 0.150.

One term reproduces both frames, `T` = (0.517, 0.518, 0.005) (a yellow light, about 132 on
gl1's scale): shadows only, the sum (1.286, 1.040, 0.185) divided by 1.286 is (1, 0.809, 0.144),
times the albedo (127, 78.4, 10.1), so (127, 78, 10); both, the sum (0.984, 0.835, 0.115) is not
divided, (125.0, 81.0, 8.0). Green rises 3 levels: the factor took 0.205 off its sum, and the
divisor's fall from 1.286 to 1 gave it back more.

**Every brightened pixel.** Of the 48,146 (`demo1.dm2`) and 35,254 (`demo2.dm2`) brighter
channels, none is the pixel's brightest (none at its albedo's value in the shadows-only frame).
In the frames without a screen blend every brightened pixel has a shadows-only channel at its
albedo's value, a divided sum: 15,361 pixels of `demo1.dm2` (frames 7, 20, 21) and 30,053 of
`demo2.dm2` (frames 5, 6, 21, 33, 34, 36, 76, 77, 83). The rest are in blend frames, whose
`1 - alpha` hides that mark: `demo1.dm2` frames 5 and 8, `demo2.dm2` frames 23, 78 and 82 (32,785
and 5,195 pixels). By how much: +1 level for 36,797 and 28,997 channels, +2 for 8,821 and
5,065, 3 or more for 2,528 and 1,192.

The floor still holds against the shadows-only frame: the divisor with the AO is at most the
one without, and `f L + T >= m (L + T)`, so the light is at least `m` times the shadows-only
light; the corrected check finds no channel under on either demo (*Both effects on*).

### 3. The cost against the budget

**No budget of its own.** Design section 2 gives one time figure, the total with everything
on ("about 5 rays per pixel", assumed 8 to 14 ms at 1080p on apple7, about half that on apple9's
render-stage path), and section 8's verdicts give the AO's row "no time budget of its own; part
of the totals". Against that total the AO's 3.67 ms median at 1920 x 1080 is 92 % of apple9's
4 ms and 52 % of its 7 ms; with RT3's 2.55 ms the two effects built take 6.22 ms, before the sun
and the reflections. Against the AO's share of the total by section 8's own proportions (two AO
rays 1.75 of 5.14 ms, 34 %, so 1.36 to 2.38 ms of 4 to 7) it is over by 1.3 to 2.3 ms, and against
section 8's closed-scene time for the same two rays a pixel (1.75 ms) by 1.92 ms, 2.1 times.

**Where the time goes**, from the cost runs' stats lines (587 warm frames a run, each with its
traced pixels, rays, hits and GPU time):

- *Not in proportion to the pixels at 1080p.* 5120 x 2880 has 7.11 times the pixels and takes
  2.98 times the time (10.955 against 3.673 ms, medians). All the pass's work a pixel (the two
  rays, the noise's four tile reads, the light's copy: 13 bytes read and 9 written a pixel, 45.6
  MB a frame at 1080p) grows with the pixels, so at 1080p it is at most `10.955 / 7.11` = 1.54
  ms; at least 2.1 ms of the 3.67 ms does not grow with the pixel count.
- *The rays that meet something.* Within a 1080p run a frame's time follows its hits
  (correlation 0.41 to 0.56 over a run's frames, 0.62 to 0.93 ms a million hits, 1.29 million a
  frame on average, so about 0.9 ms); at 5120 x 2880 hardly (0.13 to 0.17, 0.05 ms a million).
  The entities add 0.43 ms at 1080p (more instances to traverse) and nothing measurable at
  5120 x 2880.
- *Not measured:* the split between the rays, the noise and the copy (no run timed the pass with
  the rays skipped), and what the 1080p part that does not scale is. A 1080p `timedemo` frame
  is held to the display's 60 Hz, which leaves the GPU idle part of each frame; its clock at
  that load was not recorded. The follow-up card should time the pass with the rays skipped
  (the noise and the copy alone) and at 1080p with the frame rate not held.

### The head's checks and validation run

- `rt-ao` at `demo1.dm2` recorded on `603a7fee` with `MTL_DEBUG_LAYER=1`: the 23 frames byte
  for byte the round's record (`4654bfeb`); the log holds the validation banner and no other
  validation line, and the teardown is matched (`0 live objects`, `rt 73/73`, `0 unreleased, 0
  unmatched`).
- `make test`: `yq2test: 163 tests, 0 failed`. `tests/check-ref-metal.sh`: ok (AO 164 checks, 0
  failed). `tests/check-pins.sh`, `tests/check-cvar-docs.sh`, `tests/check-doc-links.sh`,
  `tests/check-no-local-identity.sh`: ok. `make client`, `make -B ref_metal` with and without
  `METAL_SHADERS=no`: no error, no warning.

## After merging TASK131

Main took TASK131 (#347, `c9ecf7d7`): while the shadows trace, the G-buffer raster has a
sixth colour attachment, the surface (`R32Uint` at colour 5), drawn by a second set of raster
pipelines, and RT3's kernel reads each pixel's surface from it. The merge, `e37d89bb`,
conflicted in two places, both resolved to keep both cards:

- *`rmb_gbuf_resolve`* (`rmb_gbuf.m`): TASK131 passes the surface attachment to
  `rmb_rts_trace`; this card adds the AO trace after it and binds the AO's light to the
  resolve. Kept in that order: RT3's kernel (with the attachment when the view has it), then
  the AO's, then the resolve (RT3's or the stock one) reading the AO's light. Both effects on,
  both kernels run on the same G-buffer view. The AO reads the depth, normal, material and
  light attachments, which TASK131 left as they were. The surface attachment stays RT3's alone:
  `RM_GB_Begin` asks for it only while the shadows trace, so with the AO alone the raster, its
  pipelines and its attachments are the lane's.
- *Design section 2*: TASK131's paragraph on the surface attachment and its cost, then this
  card's measured cost.

The rest merged on its own (`rm_gbufscene.c`'s path choice, the `Makefile`); no other file
needed a change to build.

**Engine commit measured: `e37d89bb`**, the merge (pin ids `718ada2af145-*`, its child, which
adds only the index line). Host: the bare-metal host (Apple M6). Every build and engine run went
through the host's lock wrapper, which holds the shared timing lock for the whole command;
before each run the batch checked that the screen was unlocked and no other `quake2` ran, and
logged the one-minute load: 2.2 to 5.5 over the batch, 2.2 to 3.5 during the timing runs. The
base is an export of `origin/main` at `c9ecf7d7`, built once and recorded once by its own
`scripts/pins.sh` (pin ids `nogit-*`), kept as the host's baseline record of that commit. The
replays rewrite cvars in a clone of the trace as above (the rewrite reproduces two of the first
round's clones byte for byte). Every record and replay below ran under `MTL_DEBUG_LAYER=1` except
the timing runs, and every log holds the validation banner and no other validation line and
ends in `teardown: 0 live objects`, `0 unreleased, 0 unmatched`.

### Effect off and the shadows alone

23 records on each build, seeded: the 18 scenarios at the default cadence (`world`,
`world-fullbright`, `hud2d`, `hud2d-sharp`, `hud2d-scale8`, `world-scale8`, `world-lmcubic`,
`skywater`, `dlights`, `dlights-lmcubic`, `models`, `particles`, `particles-cap`,
`world-metalfx-interp`, `world-metalfx1` to `3`, and `cinematic` on `idlog.cin` every 7th),
`dlights` at `demo2.dm2` every 7th, and `rt-shadows` and `rt-shadows-entities` at both demos (AO
off). **716 frames, every one byte-identical to `origin/main`'s.** Both builds' logs read `rt
0/0` for the 19 records without an effect, and `rt 77/77`, `56/56` (`rt-shadows`, the two demos),
`320/320` and `474/474` (with entities) for the shadows.

### AO on

- *The records.* `rt-ao` and `rt-ao-entities` at `demo1.dm2` (23 frames) and `demo2.dm2` every
  7th (89): **all 224 frames byte-identical to the lane's pre-merge records** (`4654bfeb`; and
  `rt-ao` at `demo1.dm2` to `603a7fee`'s), with the same `rt 73/73`, `52/52`, `316/316` and
  `470/470`. With the shadows off the raster has no surface attachment.
- *The floor check*, `aofloor off on 0.4` with the derived allowance, against off replays of the
  head's records (`r_rt_ao 0`, `r_rt_gbuffer 1`), which are byte-identical to the pre-merge off
  replays: all 224 frames pass, 0 channels under and 0 brighter; the smallest on / off is 0.353
  (`demo1.dm2`) and 0.348 (`demo2.dm2`), the largest `0.4 x off - on` 1.0 and 1.2 levels. The
  channels past one rounding are in the screen-blend frames *Rework 1* lists, and only there.

### Both effects on

The head's `rt-shadows` traces (`demo1.dm2`, 23 frames; `demo2.dm2` every 7th, 89) replayed with
`r_rt_ao 1` and `r_rt_ao_seed 1`:

- *The two passes in one session.* Each log shows the surface attachment and the AO's targets
  made, and the teardown counts both cards' objects: `texture 240/240, state 70/70, rt 84/84` at
  `demo1.dm2` against the pre-merge both replay's `239/239, 66/66, 82/82` (TASK131's attachment,
  its four raster pipelines with it, and its vertex and entity tables); `187/187, 70/70, 63/63`
  at `demo2.dm2` against `186/186, 66/66, 61/61`.
  This is the card's validation run with both effects on: the banner only, the teardown matched.
- *Against the shadows-only frames* (`aofloor shadows both 0.4`): no channel under on either
  demo. 48,146 channels in 5 frames (`demo1.dm2`) and 35,254 in 12 (`demo2.dm2`) come out
  brighter, the same counts and frames as before the merge: gl1's normalisation of a light sum
  past 1 (*Rework 1*, part 2).
- *Against the pre-merge both replays*: 4 frames differ, in 17 pixels, all in the frames where
  the merge's shadows-only records differ from the pre-merge ones. Those differ in 15 pixels
  (`demo1.dm2` frame 10, 4 pixels by 1 level; `demo2.dm2` frames 6, 23 and 28, 11 pixels by up
  to 38), the counts of [TASK131](TASK131.md)'s "on frames": the lookup's edge pixels, and in
  frames 10 and 23 a few pixels whose static sample TASK131's surface pipelines change in the
  last levels. 14 of the 17 are those pixels, at the same positions. The other 3 are one level
  each, in those two frames (`demo1.dm2` frame 10 at the next pixel of the group, which in turn
  no longer differs at one of its own; `demo2.dm2` frame 23, two): with the AO the lightmap term
  is scaled before it is rounded, so a last-level difference of the sample can round out on
  other pixels. With the shadows off nothing differs (above), so none of this is the AO's.

### Cost

The method of *Cost* above (the `r_rt_stats 2` line's GPU time, `demo1.dm2` whole, each run's
lines past its first 100: 587 frames a run, no validation layer, 1920 x 1080, the same scenario
cfgs: their pin ids' scenario hashes are the first round's). The first round's numbers are the
before; one drift run of the pre-merge build (an export of `32afe666`, whose `src/` is
`4654bfeb`'s) checks that they still hold on the host.

| Build | Entities | Run medians | Median of runs | p10 | p90 | Max |
|---|---|---|---|---|---|---|
| pre-merge, first round | off | 3.630 to 3.673 | 3.673 ms | 3.096 to 3.166 | 4.599 to 4.718 | 6.748 |
| pre-merge, drift run | off | 3.644 | | 3.096 | 4.814 | 6.120 |
| merge, three runs | off | 3.623 to 3.664 | 3.633 ms | 3.072 to 3.131 | 4.723 to 4.979 | 7.427 |
| pre-merge, first round | on | 3.829 to 4.228 | 4.102 ms | 3.066 to 3.222 | 4.799 to 5.143 | 6.569 |
| pre-merge, three runs | on | 3.987 to 4.482 | 4.420 ms | 3.058 to 3.169 | 4.975 to 5.105 | 7.015 |
| merge, five runs | on | 3.930 to 4.442 | 4.326 ms | 3.036 to 3.193 | 5.030 to 5.125 | 7.467 |

- *Without entities* the drift run is inside the first round's range, so those numbers stand as
  the before, and the merge's median is 0.04 ms under them, inside the runs' own spread.
- *With entities* the drift run (4.420) was above the first round's range, so the case was
  measured again: two more pre-merge runs alternated with the merge's. Both builds spread over
  about 0.5 ms from run to run; on the same 587 frames, the merge's per-frame mean minus the
  pre-merge's is a median -0.013 ms (p10 -0.515, p90 +0.467).

So the merge did not move the AO pass: TASK131's change to the raster is the surface attachment,
which a view without the shadows does not have. `timedemo` reads the display's 60.0 fps (59.9 on
one run) on every run.

### Checks on the merge

`make test`: `yq2test: 163 tests, 0 failed`. `tests/check-ref-metal.sh`: ok (AO 164 checks and
shadows 4,384, 0 failed). `tests/check-pins.sh` (`pins selftest: ok`), `tests/check-cvar-docs.sh`,
`tests/check-doc-links.sh`, `tests/check-no-local-identity.sh` and `tests/check-runbook-index.sh`:
ok. Builds: `make client`, `make -B ref_metal` with and without `METAL_SHADERS=no`, `make ref_gl1
WITH_METAL_SHIM=yes` and `make ASAN=1 client game ref_metal`: no error, and no warning in a file
of either card (the ASAN build's eight are the `sprintf` deprecations of common, client, game and
server files and the vendored `stb_vorbis.h`'s).

## The runs as planned

1. **Effect off.** Every scenario recorded on the head with `MTL_DEBUG_LAYER=1`, against
   records of `origin/main` (`718e59d4`) built in scratch with the same settings: the 14
   scenarios of TASK55's Proof 1 and `dlights` at `PINS_DEMO=demo2.dm2 PINS_EVERY=7`;
   byte-identical expected (with `r_rt_ao 0` nothing of the pass runs and every instance mask
   is as before; the masks change only with the shadows or the AO on). The logs: the
   validation banner only, `teardown: 0 live objects`, `rt 0/0`. Also `rt-shadows` and
   `rt-shadows-entities`, whose instances now carry the AO bit beside TASK55's: their frames
   byte-identical to `origin/main`'s expected, since a shadow ray's masks meet the same
   instances.
2. **Effect on.** `rt-ao` and `rt-ao-entities` at `demo1.dm2` (23 frames) and `demo2.dm2`
   every 7th (89 frames), each record's trace replayed with `r_rt_ao` rewritten to `0` and
   `r_rt_gbuffer` to `1` (the off view on the same path), and with `r_rt_ao_show 2` (the
   classes). From these:
   - the floor check, `aofloor off on 0.4`, on every frame of the four: 0 channels under and 0
     brighter expected;
   - unchanged pixels: the AO-1 class against the off view, byte-identical expected
     (`framediff --mask`), with the share of floor pixels (`up`) that read 1 from the stats line
     and one measured region of a frame, an open floor of the classes view, that reads 1;
   - a run at `r_rt_ao_range 2`, where only a crease within two units can occlude: the share of
     traced pixels occluded (self-occlusion would darken most of them);
   - `scripts/pins.sh footprint rt-ao` and `rt-ao-entities` (`--off` the off replay, `--on` the
     record), the worst frame of each, and the band `// footprint darker <lo>..<hi>` set from
     them in both cfgs (the companion carries the same band, `check-content`);
   - both effects on (a scratch copy of `rt-shadows` plus `r_rt_ao 1` and `r_rt_ao_seed 1`,
     never committed): where no dynamic light is in range (`r_rt_shadows_show 1`'s mask) the
     frame equals the `r_rt_ao 1` frame of the same trace with the shadows off, and elsewhere
     it is at least the shadows' frame times the factor (the AO before the term);
   - the pins are not recorded: the owner accepts the look first (design section 5).
3. **Frames for the owner.** Four off/on pairs at 1920 x 1080 (`PINS_WIDTH=1920
   PINS_HEIGHT=1080`), the frames with the most occluded pixels in corners, creases and under
   ledges, converted to PNG (`sips -s format png`) as `<artifacts>/TASK57/frames/<n>-off.png`
   and `<n>-on.png`, never committed.
4. **Cost.** The pass's GPU time per frame (the `r_rt_stats 2` line's timestamps), warm (frames
   past the first 100 of each run), three runs each of `rt-ao` and `rt-ao-entities` over
   `demo1.dm2` whole, at 1920 x 1080 and at 5120 x 2880 (`PINS_HIDPI=1` at 2560 x 1440): median,
   p10, p90 and max, against design section 2's budget; the fps of `timedemo.txt` at 1920 x 1080,
   `world` and `rt-ao` alternated, three rounds, on the bare-metal host (Apple M6). The measured
   cost goes into design section 2.
5. **Validation and ASAN.** The effect-on records above with `MTL_DEBUG_LAYER=1` (no validation
   line, a matched teardown, `teardown: 0 live objects`), and one `rt-ao` record from a `make
   ASAN=1` build (no report).
