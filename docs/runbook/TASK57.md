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
`r_rt_ao_min`; the frame-level floor check passes 266 of 270 frames, and the four that fail
are under by 0.2 of a level past the one-level allowance, for a cause not found
(*The floor check*). The `rt-ao` pins are not recorded: the owner accepts the look first.

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
against its off replay with `aofloor off on 0.4` (`<artifacts>/TASK57/tools/aofloor.c`: the
channels below `0.4 x off - 1` level, the one level being the output's rounding, and above
`off`; self-tested on frames scaled by 0.5, 0.3 and 1.02):

| Record | Frames | Pass | Channels under | Brighter | Smallest on / off (off >= 16) |
|---|---|---|---|---|---|
| `rt-ao`, `demo1.dm2` | 23 | 23 | 0 | 0 | 0.353 |
| `rt-ao`, `demo2.dm2` every 7th | 89 | 88 | 34 (frame 42) | 0 | 0.348 |
| `rt-ao-entities`, `demo1.dm2` | 23 | 23 | 0 | 0 | 0.353 |
| `rt-ao-entities`, `demo2.dm2` every 7th | 89 | 88 | 33 (frame 42) | 0 | 0.348 |
| `rt-ao`, `demo1.dm2`, 1920 x 1080 | 23 | 22 | 37 (frame 3) | 0 | 0.368 |
| `rt-ao-entities`, `demo1.dm2`, 1920 x 1080 | 23 | 22 | 38 (frame 3) | 0 | 0.368 |

- *The factor itself never goes under the floor.* The factor view (`r_rt_ao_show 1`) of the
  four 640 x 480 records: on all 66,697,514 world pixels (the classes view's white, blue and
  red) of the 224 frames the smallest factor is 102/255, `r_rt_ao_min` exactly.
- *The failing channels.* Every one is a pixel at the floor (red in the classes view, factor
  102/255 in the factor view), and every one is one level under `round(0.4 x off)`: `off` 23
  reading 8 (frame 42), 68 reading 26 and 73 reading 28 (frame 3, 1080p), where the check
  allows 8.2, 26.2 and 28.2. The resolve's product (`mtl_world_lm`, float) with a factor of
  0.4 predicts 9, 27 and 29, so something between the light the kernel writes and the frame
  rounds once more than the check allows for. It is not the AO's own copy: with
  `r_rt_ao_min 1.0` (factor 1 everywhere, the light through the pass's `RGBA16Unorm` target)
  all 89 frames of `demo2.dm2` are the off view byte for byte. The cause is not found; it is
  left open (0.2 of a level, 33 to 38 channels in 4 of 270 frames).

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
- against the shadows-only frame (`aofloor shadows both 0.4`): no channel under on
  `demo1.dm2`, 34 on `demo2.dm2` (the same 0.2-level class as above). Where a light is in
  range 48,146 channels (5 frames, `demo1.dm2`) and 35,254 (13 frames, `demo2.dm2`) come out
  brighter than the shadows-only frame. Not measured further: the likely cause is RT3's resolve,
  which takes gl1's store and scales a sum past 1 down by its brightest channel
  (`mtl_rts_light`), so a static part darkened by the AO leaves the sum less scaled.

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

(In progress.)

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
