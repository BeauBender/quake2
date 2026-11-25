# RT-primary sky light and the sun, and the AO off under the mode (TASK189, PM7 slice 1)

Card: TASK189 (#536). Plan: `docs/plans/presentation-modes.md`, section 4.1 ("Sky light", "AO is off
under RT-primary"), section 6 and row PM7 of section 7. Stacked on TASK188 slice 3c (RT-primary's direct
light). Under `r_presentation 3` each traced world pixel now takes two more terms beside the map's lights:
the sky light and the sun. The bounce (slice 2), the water reflections under RT-primary, the own light's
view and the denoiser (PM8, TASK190) are not in this slice.

## What existed before this slice

Traced in the code at `origin/task188s3c`:

- **RT-primary's overlay** (`pres_rtprimary`, `rm_rtgate.c`) already reads `r_rt_ao` and `r_rt_sun` as `0`,
  so neither the AO pass (`rm_rtaoscene.c` decides on `RM_Effect(r_rt_ao)`) nor RT2's sun pass
  (`rm_rtsunscene.c`, `RM_Effect(r_rt_sun)`) runs under the mode. The plan's "AO is off under RT-primary"
  held already; this slice adds its tests.
- **RT2's sun** (TASK56, `rm_rtsun.metal`): one ray a pixel toward `r_rt_sun_angles` or the map's line of
  `scripts/rt/sun-angles.txt`; the nearest face decides (`mtl_rtsun_at`, `mtl_rtsun_step`): the sky lights,
  up to four water or translucent faces are passed, anything else or nothing met shadows; the term
  `sky_color x r_rt_sunscale x visibility x max(0, N.L)` is added to the light attachment where the pixel's
  lightmap is under `r_rt_sunfloor`.
- **The direct light** (`rm_rtdir_trace`) wrote `mtl_rtdir_light(raw, exposure)` at a traced world pixel and
  nothing for the sky or the sun: an outdoor floor took only the map's light entities.

## What it is

Both terms are computed in `rm_rtdir_open` (`rm_rtdir.metal`, new), called once by the kernel beside the
traced light, and added in the world's write after the exposure (`rm_rtdir_out_open`, `mtl_rtsky_light`), in
the light attachment's units, as RT2 adds its sun. The exposure is fitted to the map's point lights against
its lightmap (TASK188 slice 3c), so it does not scale the two terms. The arithmetic is in
`common/mtl_rtsky_math.h` (new), shared with the host test.

- **The sky light.** One ray a pixel, cosine-weighted about the stored normal: `mtl_rtsky_dir`, the AO's
  one-ray direction (`mtl_rtao_dir` ray 0 with the azimuth's number doubled, at least 1/32 above the plane).
  Its two numbers are the AO's blue-noise tile (`rm_rtao_bluenoise`, made again for this pass, buffer 12) at
  the pixel, moved on by the view's frame. The frame is `rm_rtao_frame` of `r_rt_direct_seed`: `r_rt_ao_seed`'s
  rule, so `1` or more fixes the noise (every RT-primary scenario sets `r_rt_direct_seed 1`) and `0` moves it
  each traced view. Where the ray reaches a sky face, the pixel takes the sky's colour: the mean of the sky
  box's five faces above the horizon (`rm_rtl_sky_mean`), times `r_rt_direct_sky` (default `1`, at most `4`,
  `0` traces no ray). With a cosine-weighted ray, the share of rays that reach a sky of one colour is the
  irradiance over pi, so one ray's hit times the colour is the term's estimate. It is noisy: one ray a pixel
  and no denoiser until PM8.
- **The sun.** TASK56's direction (the map's line of the table, else `r_rt_sun_angles`, else the default) and
  colour (`sky_color x r_rt_sunscale`), through `RM_RTSun_Primary` (`rm_rtsunscene.c`, new), which looks the
  map up itself and ignores `r_rt_sun`. One ray a pixel facing the sun (`mtl_rtsun_ndotl` above 0), times
  `max(0, N.L)`. RT2's floor gate is left out: it reads the lightmap at the pixel, which RT-primary has turned
  off.
- **A ray at a face.** Both rays walk `rm_rtdir_sky_hit`, RT2's rule in a function of this file
  (`mtl_rtsky_at` is `mtl_rtsun_step(mtl_rtsun_at(mat), layer)`). `rm_rtsun.metal`, `mtl_rtsun_math.h` and
  `mtl_rtao_math.h` are unchanged, so RT2's kernel and the AO's compile as before (TASK59 slice 1b's warning).
- **The ray bound.** At most one sky ray and one sun ray a pixel (`MTL_RTSKY_RAYS`, `MTL_RTSKY_SUN_RAYS`),
  each of at most five intersections. With the direct light's 32, the dynamic lights' 32 and the own
  light's eye ray, a pixel traces at most 67 rays (`MTL_RTSKY_PIXEL_RAYS`), whatever the map holds.
- **The views.** `r_rt_direct_show 5` is the sky light's term alone and `6` the sun's alone, on white albedo.
  Only the picture (`0`) adds the two terms to the direct term. Views `1` to `4` trace neither ray, so view
  `1` is the direct term alone and view `3` differs from it by its shadows only; `5` and `6` each trace
  only their own (`mtl_rtsky_traces`). Views `5` and `6` still trace the map's and the dynamic lights'
  shadow rays and count them on the `r_rt_stats 2` line, though they do not show that light.
- **The line.** `r_rt_stats 2` prints a second line a frame:
  `ref_metal: rt direct sky: frame N: W world, S sky rays (H reached the sky), U sun rays (L lit), at most 67 rays a pixel`.

New cvars: `r_rt_direct_sky` and `r_rt_direct_seed`, documented in `doc/040_cvarlist.md` and classified as
settings in `rm_rtgate.c`. The renderer API, the protocol, savegames and the game module do not change; no
asset is added.

## Host tests

`tests/metal/rm_rtdir_test.c`, run by `tests/check-ref-metal.sh`:

- `test_sky_sun`: a sky hit gives the sky's colour and a world face, a model or nothing gives 0, through a
  host mirror of `rm_rtdir_sky_hit`'s walk (the sky behind four water or translucent faces reached, behind
  five not); `mtl_rtsky_at` against RT2's rule at all 256 materials and every layer; the sun lit against
  occluded against `mtl_rtsun_light` on a black base; which views trace which term (the picture both, views
  1 to 4 and 7 neither, 5 and 6 their own); the ray bound (67); the
  sky ray unit, above the plane and cosine-weighted (the mean cosine over a 64 x 64 grid within 0.01 of
  2/3, the azimuth's mean within 0.01 of 0); the light with the terms, clamped, and the terms alone; the
  view's fields and their offsets against the kernel's struct; the line; the AO and RT2's sun read `0` under
  mode 3 and `1` under mode 1, and `r_rt_sunscale` and `r_rt_direct_sky` left the user's.
- `test_open_kernel`: the kernel's text pins `rm_rtdir_open`, `rm_rtdir_sky_hit` and `rm_rtdir_out_open`
  whole, and the kernel's statement of the terms and its write. Fifteen kernel mutants, each refused (and
  each compiled with `xcrun metal -c` in scratch): `sky-miss-lit`, `sky-any-face`, `sky-layers`,
  `sky-two-rays`, `sky-no-frame`, `sky-ungated`, `open-dropped`, `sun-no-ndotl`, `sun-dir`, `sun-backface`,
  `open-reads-lm`, `alone-ignored`, `open-exposed`, and `sky-view-dropped` and `sun-view-dropped` (the
  sky's or the sun's gate not given the view, so view 1 adds the term). It also pins `mtl_rtsky_traces`'s
  body in `common/mtl_rtsky_math.h` (`traces_broken`), with three mutants refused that let view 1 trace the
  sky or the sun: `view1-both`, `view1-sun` and `views-low` (the round-0 gate, views 0 to 2). The test
  prints the eighteen as `rm_rtdir_test: 18 sky mutants refused`, and `check-ref-metal.sh` shows the count
  in its direct-light segment.
- The slice 1 to 3c pins (`test_no_lightmap_kernel`) follow the new write and the kernel's new tile buffer;
  their mutants are refused as before.

## The records (M6, apple9, 640 x 480)

Recorded at `cab3f0310a06`, three runs each, hashed as the hash of the frames' sha256s:

| Record | Frames | Run 1 | Run 2 | Run 3 |
|---|---|---|---|---|
| `rtprimary-world` | 23 | `e765655f097f` | `e765655f097f` | `e765655f097f` |
| `rtprimary-direct` | 23 | `8975350df42b` | `8975350df42b` | `8975350df42b` |
| `rtprimary-vis` | 23 | `469ce42fec8c` | `469ce42fec8c` | `469ce42fec8c` |
| `rtprimary-noshadow` | 23 | `b3d10fafdc08` | `b3d10fafdc08` | `b3d10fafdc08` |
| `rtprimary-sky` | 23 | `9acbe75f592b` | `9acbe75f592b` | `9acbe75f592b` |
| `rtprimary-sun` | 23 | `6f6c37b9a084` | `6f6c37b9a084` | `6f6c37b9a084` |
| `rtprimary-styles` (`waste2`) | 89 | `6c7091b0c7d4` | `6c7091b0c7d4` | `6c7091b0c7d4` |
| `rtprimary-sky` on `waste2` | 89 | `f9627ca24ade` | `f9627ca24ade` | `f9627ca24ade` |
| `rtprimary-sun` on `waste2` | 89 | `9b41895c7103` | `9b41895c7103` | `9b41895c7103` |
| `rtprimary-outdoor` (`water.dm2`) | 4 | `4f488097739a` | `4f488097739a` | `4f488097739a` |
| `rtprimary-sky` on `water.dm2` | 4 | `b7a508dbb285` | `b7a508dbb285` | `b7a508dbb285` |
| `rtprimary-sun` on `water.dm2` | 4 | `0164dce9b7c0` | `0164dce9b7c0` | `0164dce9b7c0` |
| `rtprimary-dlights` | 23 | `f247f241a7e9` | `f247f241a7e9` | `f247f241a7e9` |
| `rtprimary-dlights` on `waste2` | 89 | `eb8f97a5b060` | `eb8f97a5b060` | `eb8f97a5b060` |

For the owner's look, not in the repository: slice 3c's captures (`rtprimary-world` 6 and 12, `waste2` 61 and
67, `rtprimary-dlights` 20) from the branch and from slice 3c's code, and the pool of `base1`, every capture of
`rtprimary-world` on `water.dm2` (its sun from the table's line) from both, with the sky's and the sun's terms
alone from the branch. The view that faces open sky is `demo1.dm2`'s captures 21 and 22 (`base2`), of
`rtprimary-world`, `rtprimary-sky` and `rtprimary-sun` (round 2). Slice 3c's own records, for the same scenarios: `rtprimary-world` a8b7641d0739, `rtprimary-styles` 66b4cf67a706, `rtprimary-outdoor` e6017ca1d1b6, `rtprimary-dlights` 921311e433c6. Views 1 to 3 are byte-identical to slice 3c's: `rtprimary-direct` (8975350df42b), `rtprimary-vis` (469ce42fec8c) and `rtprimary-noshadow` (b3d10fafdc08) hash the same on the branch and on slice 3c, which is the proof of I1's fix on frames. The frames of the owner's look scored against slice 3c: `rtprimary-world` 6 and 12 0.000000, `rtprimary-styles` 61 and 67 0.000000, `rtprimary-dlights` 20 0.000010, `rtprimary-outdoor` 0 and 1 0.000000, 2 and 3 0.000001. Round 2 recorded the final commit's code again (`fbfaa003e05e`, which changed only this runbook): every scenario above gave the same hash, three runs for the 23, 89 and 4 frame ones, and the pool's under `rtprimary-pool` (4f488097739a).

## Before and after (`r_rt_stats 2`, M6, apple9, 640 x 480, in the hold)

The before is TASK188 slice 3c (`d18c0c48229b`), built in a scratch copy and run by `PINS_BIN`; the after is
`cab3f0310a06`. Information only (decision 0014), not a gate. The times are milliseconds as the hold's summary states them, measured
with the screen locked, and are not a timing measurement; no performance conclusion is drawn from them:

| Map (scenario, `cl_lights`) | Side | Exposure (mean) | Shadow rays a world pixel | Sky rays, reached | Sun rays, lit | Pass time (mean) |
|---|---|---|---|---|---|---|
| `base2` (`rtprimary-dlights`) | slice 3c | 1.4609 | 15.82 | n/a | n/a | 7.005 ms |
| `base2` (`rtprimary-dlights`) | branch | 1.4609 | 15.82 | 1.000, 2.8 % | 0.677, 1.8 % | 7.715 ms |
| `waste2` (`rtprimary-styles`) | slice 3c | 1.4715 | 26.10 | n/a | n/a | 9.037 ms |
| `waste2` (`rtprimary-styles`) | branch | 1.4715 | 26.10 | 1.000, 0.0 % | 0.573, 0.0 % | 10.240 ms |
| `base1` pool (`rtprimary-outdoor`, `water.dm2`) | slice 3c | 1.7564 | 19.96 | n/a | n/a | 8.917 ms |
| `base1` pool (`rtprimary-outdoor`, `water.dm2`) | branch | 1.7564 | 19.98 | 1.000, 0.0 % | 0.522, 0.0 % | 10.621 ms |

The exposure fits are identical on both sides (1.4609, 1.4715 and 1.7564), and so are the shadow rays a world pixel on `base2` and `waste2` (15.82 and 26.10; `base1`'s 19.96 and 19.98). The sky and sun terms add 0.710 ms (`base2`), 1.203 ms (`waste2`) and 1.704 ms (`base1`) to the pass's mean. Capped world pixels: 5.5 % (`base2`), 34.5 % (`waste2`), 0.0 % (`base1`), the same on both sides. Information only.

## Why the sky and the sun are rare on these views (round 2)

Round 1's means were low (`base2` 2.8 % of the sky rays reached the sky, `waste2` and `water.dm2` 0.0 %). The
trace found no fault in the chain:

- *The scene.* RT-primary's kernel binds the frame's scene and the map's material ids with the same calls as
  RT2's sun (`rmb_rt_bind_scene`, `rmb_rt_bind_materials` in `rmb_rtdir.m` and `rmb_rtsun.m`). The world's
  structure holds every triangle of the world's index buffer, the sky's among them, each with its material
  id from `rm_rt_material` (a `SURF_SKY` face is `RM_RT_MAT_SKY`, 4, which is `MTL_GB_MAT_SKY`), and every map
  instance carries the AO bit (`mtl_rtao_inst_mask`) that the sky and sun rays test (`MTL_RTAO_MASK`).
- *The rule.* `rm_rtdir_sky_hit` is RT2's `rm_rtsun_hit` loop, and `mtl_rtsky_at` is `mtl_rtsun_step` of
  `mtl_rtsun_at`, under which material 4 is lit. The origin and its bias are the direct light's
  (`mtl_rtao_bias` of the same `bias_k`), and the range is RT2's `MTL_RTSUN_RANGE`.
- *The views.* In round 1's lines of `rtprimary-dlights` on `demo1.dm2`, more than 1 % of a frame's sky rays
  reach the sky only on 79 frames, the pass's frames 625 to 704 (23.5 % of them on the mean there, 26.2 % on
  the frame of capture 21, where 57,675 of 307,200 pixels are not world pixels). That is the demo's one outdoor
  stretch, which holds captures 21 and 22, and the 2.8 % is that stretch spread over 687 frames. On `waste2`
  (`demo2.dm2`, every frame of the run) and on `water.dm2` nearly every pixel of every frame is a world pixel,
  and at most 1 and 54 of 307,200 sky rays a frame reach the sky. `water.dm2` looks at the pool of `base1` from
  just above and under its surface (TASK70), not outdoors.

The probe hold sets RT2's sun (`r_presentation 1`, `r_rt_sun 1`, at `r_rt_sunfloor 0.5` and at `1`) beside
RT-primary's sky light and sun on the same three views, joined frame by frame: Round 2's comparison (`base2`, 687 frames, all joined): the means over all 687 frames are RT2 lit 1.2 % (floor 0.5) and 1.8 % (floor 1) against RT-primary's sky reached 2.8 % and sun lit 1.8 %. Over the 80 frames (625 to 704) where RT-primary's sky rays reach the sky above 1 %: RT2 lit 9.9 % (floor 0.5) and 15.2 % (floor 1), RT-primary sky reached 23.6 % and sun lit 15.2 %. Frame 648: RT2 at floor 1 has 55245 lit of 208164 rays (26.5 %), and RT-primary's sun has 55245 of 208164 (26.5 %), equal exactly; RT2 at floor 0.5 has 9888 of 22756 (43.5 %), and RT-primary's sky reaches 65272 of 249525 world pixels (26.2 %). Frame 678 agrees the same way: 14733 of 159737 lit (9.2 %) at floor 1 and for RT-primary's sun. `waste2` (616 of 617 frames joined) and the `base1` pool (92 of 92) read 0.0 % on both sides, at both floors, with no frame above 1 % of sky rays reaching the sky; in the `waste2` row (frame 18) RT-primary's sky is 0 of 307200 and its sun 0 of 136940, in the pool's (frame 16) 0 of 307200 and 0 of 126046. So the trace and the probe agree that there is no bug: these views see almost no sky.

## Stock is unchanged

Every committed `metal` scenario but RT-primary's, seeded, the water scenarios on one `water.dm2` copied to
both, `cinematic` on `idlog.cin` at `PINS_EVERY=7`, base main at `3d02c2ac281a` and the branch at `cab3f0310a06`:
Round 1: 29 of 29 stock scenarios byte-identical to the base (`3d02c2ac281a`; base and branch each ran 29 scenarios, 0 failed; `water.dm2` hashed 7dbf4e8c202e three times). Round 2, on the commit that changed only docs `fbfaa003e05e` against main at `4efcd73b246d`: 29 of 29 byte-identical again (`water.dm2` 85c7fcca4fae twice). At `r_presentation 0` the slice changes no pixel.

## Checks

In one hold on the code of the final commit (`cab3f0310a06`; main merged: no), each check's last line:

- make ref_metal: `===> LD release/ref_metal.dylib`; make: `===> LD release/quake2`.
- `make test`: `yq2test: 591 tests, 0 failed`.
- `check-ref-metal`: `ok (13162 checks, 0 failed; ray tracing 169; G-buffer 177; shadows 4384; AO 283; water 727; sun 711; direct light 933, 8 fit mutants and 18 sky mutants refused; RT data 170; MetalFX 335; ray-tracing gate 972 ...)`.
- `rm_rtdir_test`: `direct light 933 checks, 0 failed, 8 fit mutants and 18 sky mutants refused`.
- `check-cvar-docs`: `ok`; `check-runbook-index`: `ok`; `check-doc-links`: `ok`; `check-video-menu`: `ok`; `check-pin-chars`: `ok`.
- `check-scenario-parsers`: `ok (4 tests, 10 mutants caught)`.

No frame was looked at by the agent. The frames went to the owner directly and were not committed.

## Left for later rows

| Item | Taken by |
|---|---|
| The water reflections under RT-primary (`rmb_rtwater.m` shades the reflected world from the lightmap) | PM7, a later slice. |
| A view of an emissive face's own light alone | PM7, a later slice. |
| The sky light's noise, one ray a pixel | PM8 (TASK190), the denoiser. |
| `r_rt_direct_sky`'s default and whether the sun keeps RT2's `r_rt_sunscale` under the mode | The owner, from the frames (PM11). |
| Outdoor brightness against stock: the exposure is fitted to the map's point and emissive lights against a lightmap that, in qrad3, already holds the light from sky faces (sky faces are not emitters), and the sky's and the sun's terms are added after the fit, so an outdoor area can be brighter than stock | The owner judges it from the frames (PM11); no code change in this slice. |
| Views `5` and `6` still trace the map's and the dynamic lights' shadow rays they do not show (a development view's cost only; the 67-ray bound holds) | Left as it is: skipping them would change `rm_rtdir_direct`'s pinned walk and the fit's sample under those views. |

# Slice 2: one bounce of indirect light, reading the lightmap at its hit

Plan: section 4.1 ("One bounce of indirect light"), the data table of section 4.2 (the lightmap: "the bounce at
the hit; the exposure fit"), section 6 and row PM7. The owner's call of 2026-11-18: the bounce reads the stock
lightmap at its hit. Stacked on slice 1 (`origin/task189s1`).

## What it is

Under `r_presentation 3` each traced world pixel now takes a third term beside the sky light and the sun, computed
in `rm_rtdir_bounce` (`rm_rtdir.metal`) and added in the world's write with them
(`rm_rtdir_out_open(rm_rtdir_out(raw, v.exposure), open + bounce, v.show)`). The arithmetic is in
`common/mtl_rtsky_math.h`.

- **The ray.** One cosine-weighted closest-hit ray a pixel, `mtl_rtsky_dir` (slice 1's sky-ray direction, the AO's
  one ray), on the blue-noise tile's third and fourth numbers at the pixel (`mtl_rtao_tile_index` and
  `mtl_rtao_noise` with dimensions 2 and 3, the AO's second ray's), moved on by the view's frame
  (`r_rt_direct_seed`, the AO's rule). Not slice 1's numbers: the bounce and the sky light estimate two parts of
  one hemisphere's light, and their own numbers keep the two estimates independent instead of retracing the sky
  ray. Its origin and bias are the direct light's (`mtl_rtao_bias`), its range RT2's `MTL_RTSUN_RANGE`.
- **At a face** (`mtl_rtsky_bounce_at`): water (3) and translucent faces (5) are passed, as the shadow rays pass
  them, up to `MTL_RTDIR_LAYERS` of them (the stock renderer gives them no lightmap, and the face behind holds the
  light); a lightmapped world face (1) is shaded; anything else gives 0.
- **The read at the hit.** The hit triangle's lightmap coordinates, its vertices' (`rm_wvert_t.lmuv`, the second
  float4's `zw`, as `rm_model.c` makes them) by the hit's barycentrics (`rm_rtdir_tri_lm`, RT6's
  `rm_rtw_tri_uv`), sampled bilinearly in its slice of the world's lightmap array (`rm_world_t.lmtex`), the array
  the stock draw samples. Its lightstyles: that array is what `R_RegenAllLightmaps` rebuilds on the CPU with the
  frame's `refdef.lightstyles`, as gl1 does, so the read is the stock lightmap with its styles applied, as the
  stock renderer shades that surface. The slice comes from the triangle's bounce record (`rm_rtdir_bounce_table`,
  built at map load from the surface records): `MTL_RTSKY_NOLM` for the sky, warped and translucent faces and a
  face without a lightmap, which give 0.
- **The albedo.** The plan says "times the hit's albedo", so the read is multiplied by the hit face's albedo: the
  mean of its texture's texels, read once a map from its `.wal` through the palette (`rm_rtl_wal_mean`, as
  TASK187 reads an emitter's; `RM_RTL_UNREAD_GREY` for an image that is not a `.wal` or cannot be read), 8 bits a
  channel in the record. That is qrad3's reflectivity of the face, the colour its patches reflect, without qrad3's
  doubling of a dim texture (the stock picture's texel mean, what the player sees). A per-texel albedo at the hit
  would need RT6's texture table in this pass; the mean is the radiosity's own quantity.
- **No second shadow ray.** The lightmap at the hit already holds qrad3's light there, shadows and bounces
  included, so the bounce is one ray and one read.
- **The sky.** A ray that reaches a sky face gives 0, not the sky's colour: slice 1's sky ray is the same
  cosine-weighted estimate of the light from the sky's solid angle, so the bounce counting it too would add it
  twice. The two rays split the hemisphere's light between them: the sky ray counts the sky, the bounce the faces.
- **Models and water.** The plan lights alias models by `R_LightPoint` until PM9 and is silent on the bounce at a
  model: a model has no lightmap, so a bounce ray that meets one gives 0 (it occludes, as it does the AO's rays).
  Water and glass are passed (above); the plan is silent there too, and passing them is the shadow rays' rule.
- **Where it lands: after the exposure.** The exposure scales the traced light from qrad3's light units into the
  lightmap's (the light attachment's). The lightmap read at the hit is already in those units, so the bounce is
  added after the exposure, in the light attachment's units, as the sky light and the sun are; scaling it again
  would apply the exposure twice.
- **Double brightness.** The pixel's own lightmap never enters (section 6, item 5): the array is read at the hit's
  coordinates alone (the pin below). But the exposure is fitted, at map load, from the traced direct light against
  the whole lightmap, which holds qrad3's direct light and its bounces; so the fitted exposure already lifts the
  direct term by the map's mean indirect share, and the bounce adds indirect light on top. On a map whose stock
  light is mostly bounced, RT-primary at `r_rt_direct_bounce 1` can be brighter than stock by up to that share.
  The slice leaves the fit as it is (the fit reads the lightmap at map load, before any ray, and cannot subtract
  a per-pixel bounce) and the owner judges it from the frames; `r_rt_direct_bounce` and a fixed
  `r_rt_direct_exposure` are the two knobs. The `r_rt_stats 2` lines below give the exposure on both sides.
- **The bound.** One bounce ray a pixel (`MTL_RTSKY_BOUNCE_RAYS`) of at most `MTL_RTDIR_LAYERS + 1`
  intersections: a pixel traces at most 68 rays (`MTL_RTSKY_PIXEL_RAYS`), whatever the map holds.
- **The views.** `r_rt_direct_show 7` is the bounce alone, on white albedo. Only the picture (`0`) adds it to the
  direct term; views `1` to `6` trace no bounce ray (`mtl_rtsky_bounces`), so view `1` stays the direct term
  alone (slice 1's round-1 Important) and views `5` and `6` their terms alone. View `7` traces neither the sky's
  nor the sun's ray; like `5` and `6` it still traces the map's and the dynamic lights' shadow rays.
- **The scale.** `r_rt_direct_bounce`, default `1`, at most `4` (`MTL_RTSKY_BOUNCE_MAX`), `0` traces no ray,
  documented in `doc/040_cvarlist.md` and classified as a setting in `rm_rtgate.c`.
- **The line.** `r_rt_stats 2`'s sky line counts the bounce:
  `ref_metal: rt direct sky: frame N: W world, S sky rays (H reached the sky), U sun rays (L lit), B bounce rays (R read a lightmap), at most 68 rays a pixel`;
  and once a map: `ref_metal: rt direct: <map>: N of M triangles read a lightmap at a bounce's hit`.
- **The host side.** `rmb_rtdir.m` binds the world's lightmap array at texture 7 (a 1 x 1 x 1 array when the
  world has none, and the view's record count `nbrec` 0 then), the map's buffers (`rmb_rt_bind_geometry`) at 13
  to 15 and the records at 16; the view grows from 176 to 192 bytes (`nbrec` in the old pad, `bounce` at 176).

The renderer API, the protocol, savegames and the game module do not change; no asset is added. Stock and RT2
kernels are unchanged; at `r_presentation 0` none of this runs (the records are built only where the direct
light uploads its lights, under RT-primary).

## A known limit

`R_RegenAllLightmaps` rebuilds only the lightmaps of the surfaces the frame draws (gl1's chains). A face the
stock renderer has not drawn this frame keeps the lightmap it was last built with: at map load, every style at
`1`. So a bounce that lands on such a face sees a switched or flickering light as it was when that face was last
in view (or fully on). Fixing it means rebuilding the style-changed lightmaps of faces outside the view under
RT-primary, a change to the lightmap path itself; it is left to a later card (see the table below).

## Host tests (slice 2)

`tests/metal/rm_rtdir_test.c`:

- `test_bounce`: `mtl_rtsky_bounce_at` at all 256 materials and every layer (the world shaded, water and glass
  passed up to the layers, the sky, a model and nothing dark); which views trace the bounce (the picture and `7`;
  `1` to `6`, `8` and `9` not; a scale of `0`, below `0` or not a number none) and that `7` is a term alone and
  traces neither the sky's nor the sun's ray; the bound (68); the direction on the tile's third and fourth numbers
  over a 64 x 64 grid, unit and above the plane, at another place in the tile than the sky ray's; the records
  (the albedo's bytes, the slice, none for the sky, warped, translucent and slice-less faces, a record past the
  buffer marking nothing); the term; the scale and the view's field and offset; the scale left the user's under
  the mode; and **the lightmap read at a hit against the stock rule**: a face at luxel (40, 70) of a slice, its
  triangle's vertices' lightmap coordinates made as `rm_model.c` makes them, interpolated at six hit points by
  their barycentrics, land on the luxel the stock rule gives each point (`rm_rtdir_fit_st`, the fit's), and a
  bilinear read there gives that luxel (`rm_rtdir_fit_luxel`) at four luxel centres and the mean of two luxels
  half way between them.
- `test_bounce_kernel`: pins `rm_rtdir_bounce` and `rm_rtdir_tri_lm` whole (parameters and bodies), the kernel's
  statement of the bounce and its write, the lightmap array named only in the kernel's parameter, the bounce's
  call and the bounce (read once, by `atlas.sample`), and `mtl_rtsky_bounces`, `mtl_rtsky_bounce_at` and
  `mtl_rtsky_alone` whole in the header, with the one-ray bound and view `7`. Nineteen mutants, each refused:
  in the kernel `bounce-shadow-ray` (a second shadow ray at the hit), `bounce-unstyled` (the hit's light a
  constant, not the frame's styled lightmap), `bounce-view1` (traced and added under every view, view 1 among
  them), `bounce-own-lm` (the pixel's own lightmap read), `bounce-exposed` (the term before the exposure),
  `bounce-no-albedo`, `bounce-unbounded` (the faces passed unbounded), `bounce-sky-numbers` (the sky ray's
  numbers), `bounce-sky-lit` (the sky's colour at a sky hit, counted twice), `bounce-unbounded-rec` and
  `bounce-dropped`; in the header `view1-bounce`, `bounce-sky-shade`, `bounce-not-alone`, `bounce-layers` and
  `bounce-two-rays`; on the host side `bounce-white-array` and `bounce-other-texture` (`rmb_rtdir.m` binding
  another texture than the world's lightmap array) and `bounce-no-lmtex` (`rm_rtdirscene.c` handing it none).
  Each of the eleven kernel mutants, and every earlier kernel mutant of the file, compiles with `xcrun metal -c`
  (52 in all; slice 0's two new-binding mutants moved to texture index 8, which the array now holds). The test
  prints `rm_rtdir_test: 19 bounce mutants refused`, and `check-ref-metal.sh` shows the count.
- Slice 1's pins follow the new write (`open + bounce`), the kernel's new parameters, the two new count slots and
  the tile read twice more; their mutants are refused as before.

## The records (M6, apple9, 640 x 480, in the hold)

Recorded at `7fe879bd9763`, three runs each, hashed as the hash of the frames' sha256s:

| Record | Frames | Run 1 | Run 2 | Run 3 |
|---|---|---|---|---|
| `rtprimary-world` | 23 | `c2d7b7d94e6c` | `c2d7b7d94e6c` | `c2d7b7d94e6c` |
| `rtprimary-direct` | 23 | `8975350df42b` | `8975350df42b` | `8975350df42b` |
| `rtprimary-vis` | 23 | `469ce42fec8c` | `469ce42fec8c` | `469ce42fec8c` |
| `rtprimary-noshadow` | 23 | `b3d10fafdc08` | `b3d10fafdc08` | `b3d10fafdc08` |
| `rtprimary-sky` | 23 | `9acbe75f592b` | `9acbe75f592b` | `9acbe75f592b` |
| `rtprimary-sun` | 23 | `6f6c37b9a084` | `6f6c37b9a084` | `6f6c37b9a084` |
| `rtprimary-bounce` | 23 | `e2a45f031fcc` | `e2a45f031fcc` | `e2a45f031fcc` |
| `rtprimary-styles` (`waste2`) | 89 | `a254ac92e157` | `a254ac92e157` | `a254ac92e157` |
| `rtprimary-bounce` on `waste2` | 89 | `c8462545efc0` | `c8462545efc0` | `c8462545efc0` |
| `rtprimary-pool` (`water.dm2`) | 4 | `e73ffa9aea7d` | `e73ffa9aea7d` | `e73ffa9aea7d` |
| `rtprimary-bounce` on `water.dm2` | 4 | `0182c9d94cd0` | `0182c9d94cd0` | `0182c9d94cd0` |
| `rtprimary-dlights` | 23 | `6d01f9bb9bca` | `6d01f9bb9bca` | `6d01f9bb9bca` |
| `rtprimary-dlights` on `waste2` | 89 | `8f8b43a1f6ee` | `8f8b43a1f6ee` | `8f8b43a1f6ee` |

Views 1, 2, 3, 5 and 6 against slice 1's code (`0b8880dda497`), which trace no bounce: `rtprimary-direct` `8975350df42b`, identical,
`rtprimary-vis` `469ce42fec8c`, identical, `rtprimary-noshadow` `b3d10fafdc08`, identical, `rtprimary-sky` `9acbe75f592b`, identical, `rtprimary-sun` `6f6c37b9a084`, identical.

For the owner's look, not in the repository: `base2`'s `demo1.dm2` captures 21 and 22 (outdoors) and 12
(indoors), of `rtprimary-world` from the branch and from slice 1's code and of `rtprimary-bounce` from the branch
(slice 1 has no view 7: `r_rt_direct_show 7` reads its maximum, 6, there). The shaded frames scored against slice
1: 21 0.008723, 22 0.009646, 12 0.007054. No frame was looked at by the agent. The frames went to the owner directly and were not committed.

## Before and after (`r_rt_stats 2`, M6, apple9, 640 x 480, in the hold)

The before is slice 1 (`0b8880dda497`), built in a scratch copy and run by `PINS_BIN`; the after is `7fe879bd9763`.
Information only (decision 0014), not a gate. The times are milliseconds as the hold's summary states them, measured
with the screen locked, and are not a timing measurement; no performance conclusion is drawn from them:

| Map (scenario, `cl_lights`) | Side | Exposure (mean) | Shadow rays a world pixel | Bounce rays a world pixel, read a lightmap | Pass time (mean) |
|---|---|---|---|---|---|
| `base2` (`rtprimary-dlights`) | slice 1 | 1.4609 | 15.82 | n/a | 7.717 ms |
| `base2` (`rtprimary-dlights`) | branch | 1.4609 | 15.82 | 1.000, 97.3 % | 8.116 ms |
| `waste2` (`rtprimary-styles`) | slice 1 | 1.4715 | 26.10 | n/a | 10.242 ms |
| `waste2` (`rtprimary-styles`) | branch | 1.4715 | 26.10 | 1.000, 100.0 % | 10.721 ms |
| `base1` pool (`rtprimary-outdoor`, `water.dm2`) | slice 1 | 1.7564 | 19.98 | n/a | 10.623 ms |
| `base1` pool (`rtprimary-outdoor`, `water.dm2`) | branch | 1.7564 | 19.98 | 1.000, 100.0 % | 11.300 ms |

The map lines (triangles that read a lightmap at a bounce's hit, per map): `base2`: 24720 of 25856 triangles read a lightmap at a bounce's hit; `waste2`: 20107 of 20944; `base1`: 20789 of 21767.

## Stock is unchanged (slice 2)

Every committed `metal` scenario but RT-primary's, seeded, the water scenarios on one `water.dm2` copied to every
tree, `cinematic` on `idlog.cin` at `PINS_EVERY=7`, base main at `0f99e9cfcdad` and the branch at `7fe879bd9763`:
29 of 29 stock scenarios byte-identical to the base (29 scenarios on each side, 0 failed).

## Checks (slice 2)

In one hold on the code of the final commit (`7fe879bd9763`; main merged: yes (main and slice 1)), each check's last line:

- make ref_metal: `===> LD release/ref_metal.dylib`; make: `===> LD release/quake2`.
- `make test`: `yq2test: 591 tests, 0 failed`.
- `check-ref-metal`: `check-ref-metal: ok (13162 checks, 0 failed; ray tracing 169 checks, 0 failed; G-buffer 177 checks, 0 failed; shadows 4384 checks, 0 failed; AO 283 checks, 0 failed; water 727 checks, 0 failed; sun 711 checks, 0 failed; direct light 1000 checks, 0 failed, 8 fit mutants, 18 sky mutants and 19 bounce mutants refused; RT data 170 checks, 0 failed; MetalFX 335 checks, 0 failed; ray-tracing gate 992 ch`; the hold's summary cut the line there, the rest not recorded by the hold.
- `check-cvar-docs`, `check-runbook-index`, `check-doc-links`, `check-video-menu`, `check-pin-chars`,
  `check-scenario-parsers`: each `ok` (`check-scenario-parsers`: ok, 4 tests, 10 mutants caught).

## Notes from review (slice 2)

- **Bilinear read.** The bounce samples the lightmap bilinearly, like RT6's water and the stock default
  `metal_lightmap_filter 0`. With the bicubic option on, the bounce reads slightly differently from the stock draw.
- **Grey albedo.** A texture that is not a `.wal` gets the fixed grey albedo (`RM_RTL_UNREAD_GREY`), as TASK187's
  emitters do.
- **A stale lightmap texture id on reloading the same map.** `RM_RTDir_Lights` returns early when the world
  pointer and the name match, so `brec_lmtex` could name a freed texture. The kernel then reads none, so the bounce
  goes dark and nothing is read out of bounds. This is a follow-up, not fixed here.
- **Mutant names.** The kernel mutants are caught by the whole-body pin of `rm_rtdir_bounce` and
  `rm_rtdir_tri_lm`, so their names describe what they would break, not which check refuses them.

## Left for later rows (slice 2)

| Item | Taken by |
|---|---|
| The lightmaps of faces outside the view keep their last-built styles, so a switched light out of view can still show in the bounce (the known limit above) | A later card: rebuild the style-changed lightmaps of every face under RT-primary. |
| The bounce's noise, one ray a pixel | PM8 (TASK190), the denoiser. |
| The exposure fitted against a lightmap that holds qrad3's bounces, with the bounce added after it: RT-primary may be brighter than stock where most light is bounced | The owner, from the frames (PM11): `r_rt_direct_bounce`, a fixed `r_rt_direct_exposure`, or a fit that leaves the indirect share out. |
| A per-texel albedo at the hit (RT6's texture table in this pass) instead of the texture's mean | Only if the owner's frames ask for it. |
| The bounce on alias models (a model hit gives 0) | PM9. |
