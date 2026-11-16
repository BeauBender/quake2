# Ray-traced water reflections in `ref_metal` (TASK58, RT6)

RT6 adds the third ray-traced effect, `r_rt_water`
([design](../plans/metal-raytracing-design.md), section 2, row RT6 of section 6). This file
holds the card's results. The method sections it uses are in the shared runbook: the
[shadows' proofs](../plans/metal-port-harness.md#ray-traced-shadows-from-dynamic-lights-in-ref_metal-task55-rt3)
(the replays of one trace with a cvar rewritten, the GPU time from the encoder's timestamps)
and [judging an effect's footprint](../plans/metal-port-harness.md#judging-an-effects-footprint-task133).
It was written on TASK57 (RT5, [its file](TASK57.md)) before that card merged, and then merged
with `origin/main` at `ddbfb6ff` (TASK57 merged, with TASK131's shadow surface attachment,
TASK20's static-link mode, TASK111's MetalFX scalers and FIX51).

**Status.** The pass, its cvars, its host test, the scenarios and the documentation are in the
branch, and the engine runs are made (*Runs on the bare-metal host*): with the effect off every
scenario is byte-identical to `origin/main`; with it on every pixel outside the water is
byte-identical to the effect off on the same G-buffer path (`r_rt_gbuffer 1`), alone and with
the shadows and the AO on, and within one level of a colour channel of the default path, the
effect off with `r_rt_gbuffer 0` (*Review round 1*). The pins are not recorded: the owner
accepts the look first (design section 5).

**The merge.** TASK57's own files are main's (its review fixes: the noise in fixed point, the
view cleared each frame). The shared hookups keep every card's behaviour, in the order the hit
shading needs: the shadow rays (with TASK131's surface attachment, `rmb_rts_trace(..., t_surface)`),
then the AO, then the resolve, then the reflections last, from the resolve's lit result
(`rmb_gbuf_resolve`). Two changes beyond the merge itself: the reflection rays have an instance
mask bit of their own (`MTL_RTW_MASK` `0x08`, where it was TASK57's AO bit `0x04`; *Geometry
tested*), and `RM_RTW_Frame` takes TASK57's review fix for its view (no view survives from an
earlier frame, and `r_rt_water_show` draws only for a view that was handed over). The water pass
uses one TASK57 helper, `mtl_rtao_bias`, which main did not change; it does not use the noise.

## What it does

With `r_rt_water 1`, on a device with ray tracing and on a map whose structures hold water (a
warped triangle of the world or a brush model, `rmb_rt_map_water`), a frame whose instance
structure was built takes the G-buffer path. On a map without water nothing is traced and the
view stays on the stock path. Before the raster, `RM_RTW_View` settles what can fail: the
view's inverse and projection (`rm_rtw_view`, TASK55's `rm_rts_view` plus the forward matrix)
and the outputs at the render size (`rmb_rtw_prepare`). A compute pass runs after the lighting
resolve, the last of the ray passes, because its hit shading reads their lit result
(`rm_rtw_trace`, `rm_rtwater.metal`). For each pixel of the view:

- *The water.* A pixel of opaque water (G-buffer material 3, `mtl_rtw_is_water`) takes its point
  from the depth and its normal from the G-buffer, as RT3 and RT5 do. On any other pixel a ray
  from the eye looks for translucent water in front of the raster's surface, which the forward
  pass draws later and the G-buffer does not hold: up to that surface less TASK55's tolerance (2
  units, or 0.2 %; `mtl_rtw_eye_max`), the whole map where the raster drew the sky or nothing. At
  each face it meets (`mtl_rtw_eye_step`, on the map's per-triangle ids) water is the pixel's
  water, a translucent face or a model is passed (both are drawn over the lit image later), and a
  world face or the sky brush stops it. The translucent water's normal is its triangle's, from the
  map's vertex and index buffers (`rmb_rt_bind_geometry`), turned by the instance's transform; its
  distance from the eye is kept (`R32Float`).
- *The direction.* The flat normal, turned to face the eye, tilted by `r_rt_water_normal` (0.08)
  along the plane's texture axes (`mtl_rtw_normal`) by the stock warp's offsets at the point:
  gl1's table (`r_turbsin` as `RI_Init` halves it, over its largest value, `rm_rtw_turb_table`)
  at gl1's index (`mtl_rtw_warp_index`, rm_warp_index in floats), `s` moved by the table at `t`
  and `t` by the table at `s`, as `R_EmitWaterPolys`. The axes are qbsp's
  `TextureAxisFromPlane` (`mtl_rtw_axes`), so the coordinates are the face's texture coordinates
  for a texture at scale 1; a texinfo shift moves the phase only. The geometry stays flat. The
  ray is the view direction mirrored about the tilted normal (`mtl_rtw_reflect`), kept at least
  1.8 degrees (`MTL_RTW_MIN_COS`, a cosine of 1/32) above the flat plane.
- *The start.* The point moved along the flat normal by `0.5 + k d^2` units (`mtl_rtao_bias`,
  TASK55's rule, `k = 4 x 2^-24 / gl_znear`): above the true plane wherever the depth is within
  four float steps, and with the direction above the plane the ray's distance to it only grows,
  so a ray from the water never meets that water (the host test holds it for 50,000 starts).
- *Geometry tested.* The frame's instance structure (TASK53) with `MTL_RTW_MASK`, the
  reflection rays' own bit (`0x08`, beside TASK55's `0x01` and `0x02` and TASK57's AO bit
  `0x04`), which `rmb_rt_frame` sets on the same instances as the AO bit
  (`mtl_rtw_inst_mask`): every one but the view weapon and the viewer's own model, so the world,
  the brush entities in their moved place and the alias models (monsters, players, items). Never
  the view weapon: it is in the structure only with `r_rt_gunshadow 1`, and then without the
  bit, as in RT5. Never the viewer's own model: the client keeps it out of the refdef in the
  first person and an instance of it would have no bit. Sprites, beams, shells and particles are
  not in the structure. Every instance carries a shadow bit already, so the new bit changes what
  no shadow query (`0xff`, `0x01`, `0x02`) or AO query (`0x04`) meets; with the bits distinct a
  later card can give the reflections their own set. Every face stops a reflection ray, water
  and translucent ones too (design section 1, "opaque for reflection rays"). One bounce, no
  recursion.
- *The hit's colour* (`mtl_rtw_choose`, the sky lookup's choice). The hit projected into the view
  with the forward matrix: where it lands inside the view at a pixel whose depth gives a point
  within 4 units, or 1 %, of the hit along the eye's ray, the colour is that pixel of the
  resolved image, so the hit is shaded as the picture shades it: albedo times the lightmap, RT5's
  AO and RT3's dynamic-light term (with `r_rt_shadows 0` gl1's dynamic lights are in the
  lightmap). A ray that meets the sky brush or nothing takes the pixel in its direction from the
  eye (the box is drawn around the eye, so a sky pixel in that direction is the sky's texel there)
  where the screen shows the sky there. Anywhere else, a hit off the screen or hidden behind
  something nearer, there is no reflection and the water is drawn as with the effect off.
- *The blend* (`mtl_rtw_blend`). The water's RGB mixed toward the reflection's by
  `r_rt_water_reflect` (0.5), its alpha kept. Opaque water: a full-screen draw over the resolved
  target right after the kernel, blended by source alpha with the target's alpha not written,
  every other pixel discarded (`rm_fragment_rtw_blend`). Translucent water: while the view
  traced, the forward pass draws its translucent faces (`RM_PIPE_WORLD_TEXC_BLEND`) with
  `rm_fragment_rtw_trans`, the stock `rm_fragment_world_color` colour, mixed toward the reflection
  only where the fragment is the water the kernel found at its pixel (its distance within
  TASK55's tolerance, `mtl_rtw_is_trans_water`), with the stock blend, so `SURF_TRANS33`/`66`'s
  alpha composes it over what lies behind as before. A pane in front of the water, or any other
  translucent face, keeps its stock colour.

Left out, by this design: a hit the screen does not show gives no reflection. The hit shading
is screen-space: it reads the colour the picture already has at the hit, so a hit off the
screen, behind the camera or hidden behind something nearer on the screen has no colour to read,
and that water pixel keeps its stock colour (the `none` class: 2.4 % of the water pixels above
the pool and 8.5 % under it, *Runs*). The ceiling over the pool, the sky behind the camera and
the far side of a wall are never reflected. Shading such a hit would need every map texture and
the lightmaps bound to the kernel; a follow-up card covers world-space hit shading. The warp's phase follows qbsp's
axes, not the face's texinfo (a rotated or scaled water texture ripples in its own phase); a
moved translucent brush entity's water, which gl1 draws with the world's matrix
(`R_DrawAlphaSurfaces`), is not where the ray finds it and keeps its stock colour; the eye ray is
cast on every pixel of the view that is not opaque water, which is the pass's main cost.

## Cvars

`r_rt_water` (on switch, archived, default `0`), `r_rt_water_normal` (archived, `0.08`,
clamped to 0 to 1), `r_rt_water_reflect` (archived, `0.5`, clamped to 0 to 1) and
`r_rt_water_show` (development view: `1` the mask, white where no water was found and black on
the water; `2` the classes, green a reflection from the picture, blue from the sky, red none,
black not water). `r_rt_water_show` is in the multiplayer cheat reset (`cl_main.c`), as RT3's
and RT5's views are. The others cannot make anything easier to see: a reflection shows only
pixels the view already shows (its colour is read from the resolved image), so it reveals
nothing hidden. All are in `doc/040_cvarlist.md`. With `r_rt_stats 2` each traced frame prints a
line: the pixels, the opaque and the translucent water pixels, the eye rays, the water pixels
whose reflection came from the picture, from the sky and from nowhere, and the pass's GPU time.

## The mask

The masked comparison counts the pixels that must not change. Its mask is the
`r_rt_water_show 1` frame of the same trace: white (counted by `framediff --mask`, any channel
above 0) on every pixel where the kernel found no water, black on the water it found, opaque or
translucent. It is the kernel's own class byte, so it marks every pixel the blend could touch:
the opaque blend draws only where the class says opaque water with a reflection, and the
translucent draw only where it says translucent water at the fragment's distance. Where the
water is hidden behind something nearer that the eye ray does not test (the view weapon), the
mask is black but the pixel keeps its colour, so the mask is a superset of the changed pixels,
which is the safe side.

## The host test

`tests/metal/rm_rtwater_test.c`, run by `tests/check-ref-metal.sh` (196 checks since review
round 1):

- *The material mask.* Of the 256 ids only 3 is a water pixel; the eye ray finds water at 3,
  passes 5 and alias models, stops at every other id; the map's ids are the G-buffer's numbers;
  the eye ray's bound; the class byte; a translucent fragment matches only within the tolerance
  (a pane 8 units in front, or a face behind, does not).
- *The warp's phase.* The table is gl1's over its largest value; over 200,000 coordinates and
  times of a map and a session the float index is gl1's double one but within a float's error of
  a table step, where it is one step off; truncation toward 0 then the two's complement mask;
  index 0 at 2^31 and beyond, where the value is taken modulo 256 before the cast.
- *The direction.* qbsp's axes for each face; the tilted normal a unit vector within `atan(2
  amp)` of the flat one, the flat one at amplitude 0; the reflection a unit vector, at least
  `MTL_RTW_MIN_COS` above the plane, the mirror direction with the flat normal, a grazing one
  lifted in its own vertical plane, the warp moving it.
- *No self-hit.* 50,000 starts from a depth four float steps off and an `rgb10a2` normal: none
  below the true plane, no direction into it.
- *The sky lookup's choice.* Off the screen nothing; the sky's pixel only where the screen shows
  sky; a face's lit pixel only within the tolerance and never on the sky box or an empty pixel.
- *The blend.* No reflection, a weight of 0, below 0 or not a number: the colour bit for bit; the
  alpha (0.33, 0.66, 1) always kept; the mix exact; the weight clamped to 1.
- The cvars' readings, the view (RT3's inverse, viewport, eye and bias; the forward matrix times
  the inverse is the identity), the instances (the gun with `r_rt_gunshadow 1` and an
  `RF_VIEWERMODEL` entity without the bit, the rest with it, for both shadow masks; the other
  bits as before; the bit one of the eight and none of the shadow or AO bits) and the stats line.

Red first: the same test against a copy of `common/mtl_rtw_math.h` and `rm_rtwater.c` whose 17
math functions and 7 pure functions return 0, the input or the flat normal fails 77 of its then
147 checks (`<artifacts>/TASK58/tdd/red-against-stubs.out`). The mask bit's checks, after the
merge, failed 7 of 193 with the shared bit (`<artifacts>/TASK58/tdd/red-mask-collision.out`).

## Checks

On the merged branch:

- `make test`: `yq2test: 163 tests, 0 failed`.
- `tests/check-ref-metal.sh`: ok (water 193 checks, 0 failed; AO 220; shadows 4384; `ref_metal`
  built).
- `tests/check-pins.sh`: `pins selftest: ok` (with `content_tags`: `rt-water`, companion
  `rt-water-entities`).
- `tests/check-cvar-docs.sh`, `tests/check-doc-links.sh`, `tests/check-no-local-identity.sh`,
  `tests/check-runbook-index.sh`: ok.
- Builds: `make client`, `make -B ref_metal` with and without `METAL_SHADERS=no`, `make
  ref_gl1 WITH_METAL_SHIM=yes`, `make STATIC_MODULES=1 STATIC_RENDERER=metal client` and `make
  ASAN=1 client game ref_metal`: no error, no warning in the Metal renderer's files.

## Runs on the bare-metal host

Host: the bare-metal host (Apple M6, `supportsRaytracing`). Engine commit measured: `d89bf519`
(pin ids `d89bf519f27c-*`), the merge with the mask bit and the index line; the commits after it
change only the scenario cfgs' comment headers (the footprint band), the new companion
scenario, the scenarios README and this file. Every engine batch checked that the screen was
unlocked and that no other `quake2` ran, held the shared timing lock (`tools/heavy.sh`, at most
about three minutes a hold) and logged the one-minute load before and after: 1.8 to 4.1 (other
lanes share the host). Every record is `PINS_SEED=1`, and under `MTL_DEBUG_LAYER=1` except the
timing runs. The views compared within one record are replays of its trace (TASK46) with
renderer cvars rewritten in a copy of the trace at the same length, as TASK55 and TASK57 did:
`r_rt_water 1` to `0` with `r_rt_gbuffer 0` to `1` (the *off* view, the same G-buffer path),
`r_rt_water_show` to `1` (the mask) or `2` (the classes), `r_rt_stats` to `2`, and
`r_rt_shadows`, `r_rt_ao` and `r_rt_ao_seed` to `1`. Each record's trace replayed as it is gives
the live record byte for byte (25 of 25 frames). Every log, frame, tool and list is in
`<artifacts>/TASK58/`; nothing derived from game data is in the repository.

**The demo.** `scripts/pins.sh make-demo water` made a demo of 144 to 145 messages on three
tries, over the script's range of 97 to 124 (its waits ran long on the shared host); the third
was used as made (140 rendered frames). The camera is over the pool for the first 63 frames and
under it after, so `PINS_EVERY=30` captures 5 frames (3 over, 2 under) and `PINS_EVERY=7` 20 (9
over, 11 under). The same file was put beside both builds' binaries, so their pin ids match.

### Effect off

Every scenario recorded on the head against records of `origin/main` (`ddbfb6ff`, an export
of the tree built in scratch and recorded by its own `scripts/pins.sh`, the same settings,
stored for later lanes in `<artifacts>/baselines/ddbfb6ff88b8/`): the 18 scenarios at
`demo1.dm2` (23 frames each; `cinematic` 12, `idlog.cin` every 7th), `rt-shadows`,
`rt-shadows-entities`, `rt-ao` and `rt-ao-entities` at `demo1.dm2`, the same four and `dlights`
at `demo2.dm2` every 7th (89), and `skywater` at `water.dm2` (5). **28 records, 945 frames,
every one byte-identical to the base.** Every log holds the validation banner and no other
validation line, and `teardown: 0 live objects`; `rt 0/0` for the 19 records without an
effect, and as main's for the rest: `rt-shadows` 77/77 and 56/56 (`demo1.dm2`, `demo2.dm2`),
with entities 320/320 and 474/474, `rt-ao` 73/73 and 52/52, with entities 316/316 and 470/470.
So with `r_rt_water 0` nothing of the pass runs, and the shadow and AO queries meet the same
instances now that they carry the reflections' bit.

### Effect on: the masked comparison

The mask is the `r_rt_water_show 1` replay of the same trace (*The mask*): white where the
kernel found no water, black on the water it found. `masked-compare.sh` runs `framediff OFF ON
--mask SHOW1 --threshold 0 --bad-threshold 0` on every frame. Each frame's replays are of one
trace, so they are at the same warp time.

| Record | Frames | Score 0 | Masked pixels a frame (no water) | Water pixels a frame |
|---|---|---|---|---|
| `rt-water`, every 30th | 5 | 5 | 179,683 to 183,832 of 307,200 | 123,368 to 127,517 |
| `rt-water`, every 7th | 20 | 20 | 179,113 to 183,926 | 123,274 to 128,087 |
| `rt-water`, 1920 x 1080, every 30th | 5 | 5 | 1,241,463 to 1,272,670 of 2,073,600 | 800,930 to 832,137 |
| `rt-water-entities`, every 30th and 7th | 25 | 25 | as `rt-water` | as `rt-water` |

**Every pixel outside the water is byte-identical to the effect off on the G-buffer path
(`r_rt_gbuffer 1`), on every frame.** Against the default path (`r_rt_gbuffer 0`) up to 17 % of
them are one level of a colour channel off, the G-buffer path's own difference (*Review round
1*). The
HUD's pixels over the water (about 2,000 a frame) are white in the mask, as the 2D is drawn
over the view, and unchanged. `rt-water-entities` draws the same frames as `rt-water` on this
camera path (no entity is in view: `rt 49/49` in both).

### Effect on: what the pass touches

The `r_rt_stats 2` replay of the every-7th trace, all 138 traced frames:

| Camera | Water pixels | Opaque | From the picture | From the sky | None |
|---|---|---|---|---|---|
| over the pool (63 frames) | 42.2 % | 0 | 97.6 % | 0 | 2.4 % |
| under it (75 frames) | 40.3 % | 0 | 91.5 % | 0 | 8.5 % |

The pool's water is translucent, so every water pixel there is found by the eye ray. No
reflection ray on this path meets the sky brush.

### Effect on: the footprint

`scripts/pins.sh footprint rt-water` (TASK133), the off replay against the record, the share of
the picture changed by more than 8 levels:

| Record | Frames | Worst changed (over the pool) | Under it | Brighter, worst | Largest change |
|---|---|---|---|---|---|
| `rt-water`, every 30th | 5 | 0.0574 | 0.0011 to 0.0013 | 0.0002 | 54 levels |
| `rt-water`, every 7th | 20 | 0.0579 | 0.0011 to 0.0014 | 0.0002 | 57 levels |
| `rt-water`, 1920 x 1080 | 5 | 0.0579 | 0.0012 to 0.0013 | 0.0006 | 57 levels |

The reflection darkens the pool's water toward the walls it mirrors; under the pool the
reflected surface is close to the water's own colour. Band: `// footprint changed 0..0.08` in
`rt-water.cfg` and in its new companion `rt-water-entities.cfg` (`cl_entities 1`, which
`check-content` asks of a scenario with a band and `cl_entities 0`), 1.4 times the worst frame;
every frame of the five records is inside it.

### Water with the shadows and the AO

- The `rt-water` traces (5 and 20 frames) replayed with `r_rt_shadows 1`, `r_rt_ao 1` and
  `r_rt_ao_seed 1`, with and without the water: under the water mask of the all-on view
  (byte-identical to the water-only mask on every frame), the frame with all three equals the
  shadows-plus-AO frame on every frame (score 0). `cl_lights 0` leaves no dynamic light here.
- The `rt-shadows` traces, which carry dynamic lights (`demo1.dm2`, 23 frames; `demo2.dm2`
  every 7th, 89), replayed with `r_rt_ao 1` and `r_rt_ao_seed 1`, with and without
  `r_rt_water 1`: under the all-on view's water mask every frame scores 0. The water these maps
  show: 33,184 pixels in 1 frame of `demo1.dm2`, 61,515 in 4 frames of `demo2.dm2`; over the
  whole demos (the stats line) 406,463 translucent water pixels (`demo1.dm2`: 75.3 % from the
  picture, 1.1 % the sky, 23.6 % none) and 119,617 opaque ones (`demo2.dm2`: 90.5 % and 9.5 %).

### Frames for the owner

Four off/on pairs at 1920 x 1080 of the one `rt-water` record, replays of its trace, converted
with `sips`, in `<artifacts>/TASK58/frames/` (`<n>-off.png`, `<n>-on.png`, and `<n>-classes.png`,
the classes view), never committed: captures 1 and 2 over the pool and 3 and 4 under it (the
over-pool captures with the most reflected pixels), with 816,215, 812,604, 753,719 and 738,472
pixels reflected from the picture.

### Cost

The pass's GPU time per frame from the compute encoder's timestamps (the `r_rt_stats 2` line),
`water.dm2` whole, warm (each run's lines past its first 20: 118 frames a run), three runs a
case alternated, no validation layer, from a scratch copy of the harness whose `rt-water` cfg
adds `r_rt_stats 2` (and `r_metalfx 2`) and runs the head's binary:

| Output | Pass at | Median (run medians) | p10 | p90 | Max |
|---|---|---|---|---|---|
| 1920 x 1080 | 1920 x 1080 | 3.40 ms (3.37 to 3.43) | 2.58 | 3.99 | 4.40 |
| 5120 x 2880 | 5120 x 2880 | 9.52 ms (9.31 to 9.74) | 7.02 | 10.01 | 11.91 |
| 5120 x 2880, `r_metalfx 2` | 1920 x 1080 | 3.48 ms (3.41 to 3.52) | 2.55 | 3.88 | 4.52 |

5120 x 2880 is `PINS_HIDPI=1` at 2560 x 1440. With the temporal scaler at its default size
(1080 lines on this GPU family) the pass runs at 1920 x 1080 and costs what it costs there. At
640 x 480 it takes about 0.53 ms. Over the pool and under it cost the same (3.37 and 3.41 ms at
1080p). The pass's targets: 2.7 MB at 640 x 480, 17.0 MB at 1920 x 1080, 120.1 MB at 5120 x 2880.
The load was 2.7 to 3.5 over the runs; the numbers are stored in
`<artifacts>/baselines/d89bf519f27c/`.

Against design section 2: the effect has no time budget of its own. Section 8 bounds the
reflection rays at 0.88 ms at 1080p on every pixel, trace only; the pass here also casts the eye
ray on every pixel that is not opaque water (all of them on this path) and shades the hit, and
takes 3.4 ms, about four times that bound and more than TASK57's AO (3.67 ms) of the 4 to 7 ms
that section 2 expects for every effect together on apple9. At 5120 x 2880 it is 57 % of a 60 Hz
frame, so on that output the effect needs the internal resolution (with MetalFX, 3.5 ms).

`timedemo.txt` at 1920 x 1080, `skywater` and `rt-water` alternated, three rounds: 60.0, 60.1
and 60.2 fps off, 60.1, 60.0 and 60.2 on (140 frames in 2.3 s), the display's rate.

### Validation and ASAN

- Validation: the 37 effect-on logs under `MTL_DEBUG_LAYER=1` (the 5 records, their 23 replays
  on `water.dm2`, the 8 replays of the `rt-shadows` traces with all three effects, the ASAN
  record) hold the banner and no other validation line, and end in a matched teardown, `0 live
  objects`, `0 unreleased, 0 unmatched`, with `rt 49/49` (the water alone), `57/57` (shadows and
  AO on `water.dm2`), `65/65` (all three), and `84/84` and `92/92` (`demo1.dm2`), `63/63` and
  `71/71` (`demo2.dm2`) for shadows plus AO and all three.
- ASAN: `make ASAN=1 client game ref_metal` (both binaries link the sanitizer's runtime), one
  `rt-water` record at `water.dm2` with `MTL_DEBUG_LAYER=1`: no sanitizer line, no validation
  line, the teardown matched (`rt 49/49`), and the 5 frames byte-identical to the release
  build's.

## Review round 1

The panel on `348b8671` found one Important (a documentation claim) and five notes. Engine
commit measured: `55d4c61e` (pin ids `55d4c61ea8ab-*`), the code fixes below; the commits after
it change only `doc/040_cvarlist.md` and this file. Every engine run checked that the screen was
unlocked and that no other `quake2` ran, held the shared timing lock (`tools/heavy.sh`, one hold
a run, at most about 25 seconds), and is `PINS_SEED=1` under `MTL_DEBUG_LAYER=1`; the one-minute
load was 1.6 to 6.2 (other lanes share the host). No time was measured. The views compared
within one record are replays of its trace with cvars rewritten, as in *Runs on the bare-metal
host*. Logs, tools and lists are in `<artifacts>/TASK58/rework1/`.

### The fixes

- **I1, the non-water pixels.** `r_rt_water` puts the view on the G-buffer path, which
  `r_rt_gbuffer`'s own entry gives as within one level of a colour channel of the default path,
  and the masked comparison above was made against the effect off on that same path. Measured
  against the default path (`r_rt_water 0` with `r_rt_gbuffer 0`, the picture a user without the
  effect sees), on the `rt-water` traces, under the water mask, `framediff --footprint --delta 0`:

  | Record | Frames | Non-water pixels a frame | Share of them differing, per frame | Largest channel change |
  |---|---|---|---|---|
  | every 30th | 5 | 179,683 to 183,832 | 0.1338 to 0.1662 | 1 level |
  | every 7th | 20 | 179,113 to 183,926 | 0.1335 to 0.1667 | 1 level |
  | 1920 x 1080, every 30th | 5 | 1,241,463 to 1,272,670 | 0.1350 to 0.1723 | 1 level |

  The same pixels differ between the two off views (the default path against `r_rt_gbuffer 1`,
  under the mask: the same counts), and against the G-buffer off view the effect changes none
  of them (score 0 on all 30 frames, as before). So a pixel that is not water moves by at most
  one level when the effect is switched on, and only because of the path. The `r_rt_water`
  entry now says "as with `r_rt_gbuffer 1`: within one level of a colour channel of the picture
  without the effect", and *Status* and the masked comparison above name the path.
- **N1, the warp index's range guard.** It matters only at `|x|` of 2^31 and beyond, which the
  engine's inputs do not reach (`cl.time` wraps at 2^31 ms); kept, so the shared function is
  defined for every finite input. The host test now checks the index there for both signs of
  the coordinate and the time (196 checks). A copy without the guard (`t = t + 0.0f;`) fails it:
  under the sanitizers of `tests/check-ref-metal.sh` UBSan stops at the cast ("2.15946e+09 is
  outside the range of representable values of type 'int'"), and without them the check fails
  (`195 checks, 1 failed`; the cast gives 255 on arm64).
- **N2, each show view its own.** With `r_rt_shadows_show` or `r_rt_ao_show` on (their views
  are handed over first), `RM_RTW_View` hands the back end no view, so the reflections are
  neither traced nor blended over that view. Red first: the `rt-shadows` trace at `demo2.dm2`
  (every 7th, 89 frames, opaque water in 4 of them) replayed by the pre-fix build (an export of
  `348b8671`), each show view with `r_rt_water 1` against `0`: `r_rt_shadows_show 1` differed in
  1 frame (3,148 pixels, up to 127 levels), `r_rt_ao_show 1` (with `r_rt_ao 1`, `r_rt_ao_seed 1`)
  in 4 frames (14,326 pixels, up to 128 levels). On the fix both are byte-identical with and
  without the water on all 89 frames, and equal to the pre-fix build's view without it; on the
  `rt-water` trace too (5 of 5).
- **N3, show-only after a drawn view.** `RM_RTW_ShowOnly` now also needs `rmb_rtw_shown()`: the
  back end drew this view's `r_rt_water_show` view, after a dispatched trace. A trace that cannot
  bind its scene, materials or geometry leaves the forward pass in, so the frame is drawn as
  without the reflections rather than as the bare resolve. Traced only; no engine run reaches
  that failure.
- **N4, the view ended on every path.** `rmb_gbuf_view_end` calls `rmb_rtw_view_end()` before its
  early return for a pass that did not open, so the translucent pipeline cannot survive into a
  second view of the frame. Traced only, as the finding.
- **Security N1, the fragment reads bounded.** `rm_fragment_rtw_blend` discards a pixel outside
  the reflection or the material texture before any read, and `rm_fragment_rtw_show` draws one
  outside the reflection as a pixel without water, as `rm_fragment_rtw_trans` already checks.
  The textures are the target's size, so no pixel takes the new branch.

### Results

- *Effect off.* The 28 records of *Effect off* (the 18 scenarios at `demo1.dm2`, `rt-shadows`,
  `rt-shadows-entities`, `rt-ao` and `rt-ao-entities` at `demo1.dm2`, the same four and
  `dlights` at `demo2.dm2` every 7th, `skywater` at `water.dm2`) against main's stored records at
  `ddbfb6ff` (`<artifacts>/baselines/ddbfb6ff88b8/pins/`, not recorded again): **945 of 945
  frames byte-identical**, `rt 0/0` for the 19 records without an effect, and `rt-shadows`
  77/77 and 56/56, with entities 320/320 and 474/474, `rt-ao` 73/73 and 52/52, with entities
  316/316 and 470/470, as main's.
- *`rt-water`.* The five records (every 30th, every 7th, 1920 x 1080, and `rt-water-entities`
  every 30th and 7th) against this lane's at `d89bf519`: **55 of 55 frames byte-identical**
  (`rt 49/49`). The `rt-water` cfg's settings hash changed with its footprint band comment, so
  those three ids differ from `d89bf519`'s (`1494551982`, `3849274443` and `1964917750` for
  `1104553152`, `833530645` and `2175638925`); the companion's are the same. The replays of the
  new traces are byte-identical to `d89bf519`'s too: the masks (`r_rt_water_show 1`, 30 frames),
  the G-buffer off views (30), the classes view (`r_rt_water_show 2`, 5), and every effect on
  (`r_rt_shadows 1`, `r_rt_ao 1`, `r_rt_ao_seed 1`, `r_rt_water 1`: 5 frames on `water.dm2`, `rt
  65/65`, and the 89 of the `demo2.dm2` `rt-shadows` trace, `rt 71/71`).
- *Validation.* All 58 logs of this round (33 records, 25 replays, the 4 of the pre-fix build
  among them) hold the validation banner and no other validation line, and end in a matched
  teardown, `0 live objects` and `0 unreleased, 0 unmatched`: with every effect on, each show
  view with the water on (`rt 56/56` and `54/54` on `water.dm2`, `62/62` and `69/69` on
  `demo2.dm2`) and the water's own show views.
- *Cost.* Not measured: the bound checks add a compare or two a pixel to two full-screen draws,
  N2 removes the pass from a frame that shows another pass's view, and N3 and N4 add a flag.
