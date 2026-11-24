# RT-primary direct light from the map's point lights and emissive faces, one shadow ray each (TASK188, PM6 slices 1, 2 and 3a)

Card: TASK188 (#535). Plan: `docs/plans/presentation-modes.md`, section 4.1 and row PM6 of section 7.
Under `r_presentation 3` (*Ray traced*) the lightmap is off at the visible world surface, and each
lightmapped world pixel takes the direct light of the map's light entities (TASK187's data) in its place,
one shadow ray a light. The mode stays greyed in the menu "awaiting review" (the existing `vidrules.c`
rule) and runs from the console.

## The dependency on TASK59

The plan lists PM5, TASK55 and TASK59 for PM6. TASK59 (temporal accumulation and the denoiser) is open.
The plan needs it for the sampled terms: section 4.1 has "one light chosen per pixel per frame" and section
4.3 says "RT-primary traces about one sample a term a pixel, which is noisy"; the denoiser is applied to
every term in PM8 ("TASK59's filter on demodulated irradiance for every term"). This slice samples
nothing: every light in range is traced at every pixel with one any-hit ray, so the term is exact for its
model, hard-shadowed and noise-free, and reads no history. Nothing here consumes TASK59's output. The
record below shows it: three runs of `rtprimary-world` give the same 23 frames byte for byte.

## What it is

- **The mode.** `rm_pres_decide` chooses RT-primary wherever the gate's path is not none (decision 0014,
  capability only), and reads 0 with "no ray tracing on this device" elsewhere. Its overlay
  (`pres_rtprimary`, `rm_rtgate.c`) reads `r_rt_direct` and `r_rt_water` as 1 (the plan's table in
  section 2.2 keeps the water reflections), and `r_rt_shadows`, `r_rt_sun` and `r_rt_ao` as 0: AO is
  replaced by PM7's bounce, the sun and the dlight shadows become terms in PM7 and slice 2, and no layer
  may add the lightmap's light back meanwhile. No cvar is written. The report line:
  `ref_metal: presentation: ray traced (r_presentation 3): the lightmap off at the visible world surface,
  the direct light of the map's lights in its place; r_rt_direct and r_rt_water read 1, r_rt_shadows,
  r_rt_sun and r_rt_ao read 0`.
- **The pass** is a new row of the effect table (`rm_rtfx.c`, `RM_RTFX_DIRECT`), first in the order, since
  the other effects start from the light it writes. Its files: `rm_rtdir.c` (pure C), `rm_rtdirscene.c`
  (the engine side), `rmb_rtdir.m` (Metal), `rm_rtdir.metal` and `common/mtl_rtdir_math.h` (the per-pixel
  arithmetic, shared with the host test). `rm_rtdir_on` and `RM_RTDir_Wanted` are 0 under every other
  presentation, so stock opens, builds and allocates nothing for it.
- **The light.** For each light in range, qrad3's `GatherSampleLight`: `(intensity - dist) x N.L`, nothing
  at N.L of 0.001 or below; a spotlight lights only where minus the dot of the direction to the point and
  its direction is above `cos(_cone)`. Times the light's normalized `_color` and its lightstyle, the
  refdef's rgb times `r_modulate` as the lightmap takes it. The sum is in the lightmap's units (a byte of
  255 reads 1), so it is what the direct part of a qrad3 lightmap holds, before the 196 clamp and without
  the bounces and the surface lights.
- **The bounds on a pixel's work** (slice 1's; slice 3a chooses the 16 among the visible lights, under a
  bound of 32 rays, see there). A pixel with more than `MTL_RTDIR_MAX_TRACED` (16) lights in range
  takes the 16 strongest (the largest `(intensity - dist) x N.L`, `mtl_rtdir_keep`), summed in the
  buffer's order, so it traces at most 16 shadow rays of at most 5 intersections each, whatever the map
  holds (round 0 traced every light in range: up to 2048 a pixel). The intensity is packed at most
  `RM_RTDIR_MAX_INTENSITY` (16384): qrad3 bounds no light value (`rm_rtlights.c` takes any finite one, as
  `CreateDirectLights` does) and its falloff is linear, so a light reaches as far as its intensity. The
  +-4096 on each axis is the stock tools' and the network's bound (`MSG_WriteCoord`, a short of 1/8 units),
  not the BSP's (neither the loader nor `collision.c` holds a vertex to it); within it no two points are
  farther apart than 8192 x sqrt(3), about 14189, so a light of 16384 already reaches every point of such a
  map. Above 16384 a light is dimmer than qrad3's wherever it reaches, (16384 - dist) against
  (intensity - dist), not only shorter-reaching. qrad3's default is 300.
- **The shadow ray**, one a light in range and pixel: from the surface point offset along its normal
  (TASK57's `mtl_rtao_bias`) to a unit short of the light's origin, against the frame's instance structure
  with `MTL_RTAO_MASK` (the world, the brush entities, the alias models, not the view weapon). Water and
  translucent faces let it through, up to four of them; any other face shadows, the sky's included.
- **The lightmap never lights the pixel it is on** (the plan's section 6, item 5), by the kernel's
  structure. `rm_rtdir_direct` computes the traced light and takes no texture. The kernel keeps it in a
  `const float3 raw`. `rm_rtdir_fit_sample(lm, px, raw)` is the one read of the lightmap: it takes `raw` by
  value and returns the two quantized values the frame's sums take. The world branch is
  `light_out.write(rm_rtdir_out(raw, v.exposure), px);`, `rm_rtdir_out` being `mtl_rtdir_light` of the
  traced light alone; only the other branch writes `base`, so every other pixel keeps the light it had,
  bit for bit.
- **The exposure, the stated rule.** `exposure = sum of the stock lightmap / sum of the traced light` over
  the same sampled world pixels (every fourth pixel a row and a column) of the latest completed frame of
  the map (slices 1 and 2; from slice 3a the frame three before, a fixed lag, see there), clamped to 0.25 to 16; 1 at a map's first frame and kept while no traced light reaches a
  sample. The back end hands over the frames completed since the last read in slot order;
  `rm_rtdir_fit_latest` applies the one with the largest frame number only, and ignores every frame at or
  before the map's reset (`RM_RTDir_Lights` records the last frame traced before it), so the old map's
  frames still in flight do not overwrite the reset to 1. Round 0 applied them all in slot order, the
  last one winning, which could be an older frame or the old map's. Each
  sample is the channel mean, clamped to 16, in steps of 1/64, so the sums are integers, exact in any
  order, and cannot wrap below 8K. So the traced frame's mean light is the stock frame's.
  `r_rt_direct_exposure` above 0 is a fixed exposure instead (plan: "the owner may prefer a fixed
  exposure; that is a value in the slice").
- **The debug views**, `r_rt_direct_show`: 1 the direct term alone (the light on white albedo), 2 the shadow
  visibility (grey by the share of the lights in range whose ray reached them, dark blue where none is in
  range), 3 the direct term with no shadow ray traced. Black off the lightmapped world.
- **The data** (`rm_rtdata.c`) is gathered at map load under RT-primary as well as under `r_rt_stats`,
  read through `RM_RTD_Lights`. A spotlight whose target is missing is left out (qrad3 leaves its normal at
  zero, so it lights nothing), as are lights of intensity 0 or less.
- **Lines.** `r_rt_stats 1`: `ref_metal: rt direct: <map>: N lights uploaded (...)`, with the lights held to
  the intensity cap. `r_rt_stats 2`, a frame: world pixels, lights in range, shadow rays, rays that reached
  their light, the pixels past the 16 lights, the fit's sums, the exposure and the pass's GPU time. The
  lights in range, the rays and the lit ones are counted in 64 bits on the GPU (a low and a high word,
  the carry added when the low word wraps), so they do not wrap at 4K with hundreds of lights a pixel.

New cvars: `r_rt_direct` (the effect switch, default 0, read 1 by the mode), `r_rt_direct_exposure` and
`r_rt_direct_show`, in `doc/040_cvarlist.md` and classified in `rm_rtgate.c`. New scenarios:
`rtprimary-world`, `rtprimary-direct`, `rtprimary-vis`, `rtprimary-noshadow`: pins of `ref_metal` itself
from slice 3a, whose fixed lag makes them the same run to run (see there); the look is the owner's to accept
(PM11).

## Host tests

`tests/metal/rm_rtdir_test.c` (in `tests/check-ref-metal.sh`, with ASAN and UBSAN): the mode's switch under
each presentation, qrad3's falloff and cone, the shadow ray's step, the light and its clamps, the fit's
samples and their order-free sums, the fit, the frame it is taken from and the fixed exposure, the lights
a pixel takes past the cap, the packing (a missing target, dark lights, the cap, the intensity clamp),
the style table, the view and the lines; and the kernel's text for the no-lightmap invariant. Red first:
before `rm_pres_decide` chose the mode, `rm_rtdir_test: 355 checks, 5 failed`.

**The no-lightmap rule** (`kernel_broken`, over `rm_rtdir.metal` with its comments taken out) holds the
structure above: `rm_rtdir_direct` has no texture parameter and no `.read(`, `lm`, `base` or `fit` in its
body; the word `lm` occurs four times (the kernel's parameter, its one use in
`rm_rtdir_fit_sample(lm, px, raw)`, the fit's parameter and its `lm.read(px)`); `rm_rtdir_fit_sample` and
`rm_rtdir_out` are pinned whole; the kernel names `raw` three times (its const definition from
`rm_rtdir_direct`, the fit's call, the world's write) and `fit` three (its definition and the two
`simd_sum`s); the world branch is pinned whole, and `base` occurs in the kernel's parameter and the other
branch only. Round 0's test counted needles and could not fail its `test_no_lightmap` (its helper threw
the lightmap away); that test is gone. Red first: round 0's kernel against the new rule,
`rm_rtdir.metal: rm_rtdir_direct is not defined once`, `rm_rtdir_test: 363 checks, 8 failed`.

Round 2 closes the shapes that add a texture read after the trace's call. The kernel's parameter list is
pinned whole (its seven textures, its buffers, its position), as are the whole `const float3 raw = ...;`
statement and the kernel body's reads: `mat`, `depth`, `normal` and `base`, once each, and no other
`.read(` (the lightmap's one read stays in `rm_rtdir_fit_sample`). The file may not paste tokens (`##`),
and its only macros are the eleven count slots. On the host, `test_host_binding` holds `rmb_rtdir.m`'s
compute textures to the seven known `[ce setTexture:<t> atIndex:<i>];` pairs in `rmb_rtdir_trace`, once
each, and no other `setTexture` or `setTextures` in the file. A mismatch in whitespace alone still fails,
and its message ends "(reformatted? the pin is exact)". This is a structural lint against accidental
regressions, not a parser: a macro from an included header, for one, is outside what it reads. Red first:
round 1's rule passed the three new kernel mutants, `rm_rtdir_test: 384 checks, 3 failed`.

The test builds ten mutants of the real file in memory and requires the rule to refuse each. Each one
gives a world pixel the lightmap, and each compiles with `xcrun metal` (written out by hand with
`RM_RTDIR_MUTANTS=<dir>`):

```
mutant add-lm: the kernel's body reads beyond the material, depth, normal and base
mutant reuse-sample: the fit's values go somewhere but the frame's sums
mutant world-from-base: the kernel's body reads beyond the material, depth, normal and base
mutant branch-swap: the world branch is not the traced light's single write
mutant fit-leak: the kernel's raw is not the traced light alone
mutant fit-sample-leak: rm_rtdir_fit_sample is not the fit's sample alone
mutant direct-reads-lm: rm_rtdir_direct reaches a texture or the fit
mutant paste-macro: the file pastes tokens (##)
mutant new-binding: the kernel's parameters are not its known seven textures and buffers
mutant lmx: the kernel's parameters are not its known seven textures and buffers
rm_rtdir_test: 392 checks, 0 failed
```

`branch-swap` is the correctness review's (the two branches swapped). `fit-leak` is the spec review's
`raw += float3(float(fit_lm)) * 0.001f;` in the new shape: `raw` made non-const and the fit's value added
to it after the sample. `fit-sample-leak` takes `raw` by reference into the fit's sample and adds the
lightmap there; `direct-reads-lm` hands the lightmap to the trace. `paste-macro` is the correctness
review's round-1 shape, `#define RM_CAT(a, b) a##b` and `+ RM_CAT(l, m).read(px).rgb` after the trace's
call; `new-binding` adds a parameter `q [[texture(7)]]` and `+ q.read(px).rgb`, and its host half,
`[ce setTexture:lm atIndex:7];` in `rmb_rtdir.m` (which compiles), is refused by `test_host_binding`, as
is the same binding through `setTextures:withRange:`; `lmx` is the spec review's, the same with
`lmx [[texture(7)]]`.

**No GPU run of the kernel.** No harness in `tests/check-ref-metal.sh` runs a shader on the GPU against
fixture textures: the device probe (`tests/metal/rmb_cs_probe.m`) drives the colour space and the MetalFX
scalers with no shader library, and the build step compiles the library without running it. The kernel
would also need an instance acceleration structure bound. So the rule is held by the kernel's text and structure
above, not by a pixel read back from the GPU.

**The fit's frame** (`test_fit_latest`): frames `{6, 5}` in that order, and `{5, 6}`, give frame 6's fit;
an entry at or before the reset gives 1, and the new map's first completed frame is then applied. Red
first, with `rm_rtdir_fit_latest` written as round 0 behaved (every entry in the order given):
`FAIL tests/metal/rm_rtdir_test.c:606: near(rm_rtdir_fit_latest(s, 2, &after, 1), 3.0, 1e-6) && (after == 6)`,
`rm_rtdir_test: 381 checks, 8 failed`.

**The intensity clamp** (`test_pack`): lights of `1e30` and `3.4e38` pack as 16384 and are counted. Red
first, with the clamp taken out: `FAIL tests/metal/rm_rtdir_test.c:759: out[0].pos[3] ==
RM_RTDIR_MAX_INTENSITY && out[1].pos[3] == RM_RTDIR_MAX_INTENSITY`, `rm_rtdir_test: 381 checks, 1 failed`.
**The cap** (`test_keep`, on `mtl_rtdir_keep`, which the kernel runs): 16 lights kept in order; a weaker
or equal one past the cap dropped; a stronger one replaces the weakest; of 2048 rising lights the last 16.

`rm_rtgate_test` and `rm_rtfx_test` follow the mode, the overlay, the fifth effect switch and the new order.

## The terms are live (M6, apple9, 640 x 480, `demo1.dm2`)

`rtprimary-world` with `r_rt_stats 2` (a scratch copy of the scenario, not committed), and the same with
`r_rt_direct_show 3`. The map is `base2` (248 lights, 4 styled, none left out):

```
ref_metal: rt direct: maps/base2.bsp: 248 lights uploaded (0 dark, 0 spotlights without a target, 0 past the cap left out)
ref_metal: rt direct: frame 18: 307200 pixels, 307200 world, 248 lights, 521201 in range, 521201 shadow rays (1.70 a world pixel), 489574 lit, fit 376127/258792 over 19200 samples, exposure 1.0000 (fit), 2.191 ms
ref_metal: rt direct: frame 19: 307200 pixels, 307200 world, 248 lights, 521201 in range, 521201 shadow rays (1.70 a world pixel), 489574 lit, fit 376127/258792 over 19200 samples, exposure 1.4534 (fit), 2.195 ms
ref_metal: rt direct: frame 704: 307200 pixels, 306572 world, 248 lights, 426934 in range, 426934 shadow rays (1.39 a world pixel), 413123 lit, fit 550117/218976 over 19156 samples, exposure 2.5122 (fit), 2.094 ms
```

With no shadow ray (`r_rt_direct_show 3`), frame 18: `521201 in range, 0 shadow rays (0.00 a world pixel),
0 lit, fit 376127/268206`. So at frame 18, 31627 of 521201 rays (6.1 %) were shadowed, and the traced sum
fell from 268206 to 258792. The fit moves from 1 at the first frame to the measured ratio at the next and
follows the view (1.45 to 2.51 over the demo). The timings are information (decision 0014).

The harness's comparator (`framediff`, threshold 0) over the 23 frames of each record:

| Pair | Frames that differ | Mean score (max) | Differing pixels |
|---|---|---|---|
| `rtprimary-direct` against `rtprimary-noshadow` (the shadow term) | 23 of 23 | 0.0155 (0.0506) | 8.1 % |
| `rtprimary-direct` against `rtprimary-vis` | 23 of 23 | 0.4686 (0.6294) | 93.8 % |
| `rtprimary-world` against stock `world` | 23 of 23 | 0.0352 (0.0969) | 56.0 % |

The level of the frames (mean of the 8-bit channels over the 23 frames; near-black: brightest channel at
most 4):

| Record | Mean level | Near-black | Near-white |
|---|---|---|---|
| stock `world` | 19.3 (9.6 to 38.0) | 3.3 % | 0.04 % |
| `rtprimary-world` | 17.7 (6.4 to 34.3) | 33.9 % | 0.05 % |

The fit holds the mean within 10 % of stock and blows nothing out. A third of the pixels are near black:
the parts of the map no light entity reaches, which qrad3 lit by its emissive faces and bounces (slice 2
and PM7). The owner judges the look from the frames.

Determinism: `rtprimary-world` recorded three times (two commits, and `--run 2`) gives one hash of its
frames' hashes, `000b429ec38e`; `rtprimary-direct` `dd451ee5650d`, `rtprimary-vis` `be8d223522ca` and
`rtprimary-noshadow` `6495fa432ad0`, each the same twice. The fit reads earlier frames, so a run whose
frames completed in another order could differ in its first frames; none did here. That held for slice 1's
light only: slice 2's records varied run to run, until slice 3a fixed the fit's frame (see there).

**Round 1** (the kernel's new structure, the cap, the intensity clamp, the 64-bit counts and the fit's
frame, at `c18902fd`): the four records give the same hashes, `000b429ec38e`, `dd451ee5650d`,
`be8d223522ca` and `6495fa432ad0`, so every frame is byte-identical to round 0's. Neither bound bites on
`demo1.dm2`: the map line reads `248 lights uploaded (0 dark, 0 spotlights without a target, 0 past the
cap left out, 0 brighter than 16384 held to it)`, and every one of the 686 frame lines of the
`r_rt_stats 2` run reads `0 pixels past 16 lights`; frame 18 reads as in round 0
(`521201 in range, 521201 shadow rays (1.70 a world pixel), 489574 lit, 0 pixels past 16 lights, fit
376127/258792`). The pass's GPU time read 2.6 ms there against 2.2 ms in round 0, in a window that was not
held quiet; it is information, not a measurement (decision 0014).

**Round 2** (the kernel and host pins, the clamp's comment) changes the host test, a header comment and
this runbook only. `rm_rtdir.metal` and `rmb_rtdir.m` are as in round 1, so no record was re-run.

No frame was looked at by the agent; the frames stay in a local store outside the repository.

## Stock is unchanged

Every committed `metal` scenario but the four new ones, recorded seeded (`PINS_SEED=1`) at 640 x 480 by the
base (main at `4b47414b`, built in a scratch copy, run by `PINS_BIN`) and by the branch (`6dbfc180`), into
the pin store, the water scenarios on one `water.dm2` copied to both, `cinematic` on `idlog.cin`
at `PINS_EVERY=7`. Compared by the hash of the frames' sha256s:

| Scenario | Frames | Hash | Scenario | Frames | Hash |
|---|---|---|---|---|---|
| `world` | 23 | `d7a53e13f172` | `dlights` | 23 | `5552278ed108` |
| `world-fullbright` | 23 | `f610843d31ef` | `dlights-lmcubic` | 23 | `2735787823cc` |
| `world-lmcubic` | 23 | `3c5fd75eec22` | `models` | 23 | `f8115c5acd17` |
| `world-scale8` | 23 | `c308e3a65dda` | `particles`, `particles-cap` | 23 | `a3cc7d63b3f4` |
| `world-metalfx1` | 23 | `589987bfa76e` | `hud2d`, `hud2d-sharp` | 23 | `b006e4e885b7` |
| `world-metalfx2`, `world-metalfx3` | 23 | `63ee99460726` | `hud2d-scale8` | 23 | `4ac8aae28645` |
| `world-metalfx-interp` | 23 | `d7a53e13f172` | `rt-ao` | 23 | `541f53e6e716` |
| `hdr` | 23 | `065e37ee63c3` | `rt-ao-entities` | 23 | `f69c19f6c472` |
| `hdr-metalfx2` | 23 | `c38e32a25a56` | `rt-shadows` | 23 | `2b8d11a25392` |
| `retro` | 23 | `0534a957ccbe` | `rt-shadows-entities` | 23 | `04e115786187` |
| `retro-filter` | 23 | `ffbd65e5764c` | `rt-sun` | 23 | `b7e0212dfda8` |
| `skywater` | 4 | `c662983be7b4` | `rt-water`, `rt-water-entities` | 4 | `241b5a706145` |
| `cinematic` | 12 | `4f7259768d21` | | | |

29 of 29 byte-identical: at `r_presentation 0` the branch changes no pixel. No existing record was replaced.

**Slice 2's own run** (head `20ea6cbf`, the code of the final commit; base main at `526638e9`, built in a
scratch copy and run by `PINS_BIN`; the same method): 29 of 29 stock scenarios byte-identical to the base,
no failures. Twenty-six of the hashes are those of the table above, to the digit. The three that use the
generated `water.dm2` (`rt-water`, `rt-water-entities`, `skywater`) hash the frames of a demo `make-demo`
makes afresh each time, so they differ from the table's by the file and are compared with the base's on
one copy of it (`e5809628905e`): `rt-water` and `rt-water-entities` `929e43a01e45` (4 frames), `skywater`
`e82e7bb55913` (4 frames), the same from the base and from the branch. Round 0's run, on another copy of the
demo, read the same way (`76c2932ae4e9`, `76c2932ae4e9` and `c662983be7b4`, base and branch alike).

## Slice 2: the emissive faces, the lightstyles, the Ray tracing screen

### The emissive faces as area lights

The world's `SURF_LIGHT` faces (TASK187's emitters) join the point lights in the same buffer, after them,
so the 16 lights a pixel traces are the strongest of either kind. Each face is one qrad3 surface light
(`emit_surface`), built by `rm_rtdir_emitter` (`rm_rtdir.c`) from the face's polygon in the world's
retained vertices (`rm_rtdirscene.c`, `RM_RTDir_Face`), its plane normal flipped for `SURF_PLANEBACK`:

- **Where and which way**: the points' mean (qrad3's `WindingCenter`) a unit in front of the face
  (`MakePatchForFace`), the face's normal in the record's direction word, `MTL_RTDIR_EMITTER` (-3) in place
  of a cone's cosine. Only the world model's faces emit, as in qrad3 (`fn < dmodels[0].numfaces`); a brush
  entity's light face is counted `not the world's`.
- **How bright**: qrad3's patch light, the reflectivity times the texinfo value (`BaseLightForFace`) times
  the normalized reflectivity, so `value x refl x refl / max(refl)`; its colour that light normalized; its
  intensity the light's largest channel times the polygon's area times 0.4 (`CreateDirectLights`, the
  default `-direct` scale). A face below 3 in every channel sends no direct light (qrad3's `DIRECT_LIGHT`).
  Style 0: qrad3's surface lights have none.
- **The falloff** (`mtl_rtdir_emit_scale`, `common/mtl_rtdir_math.h`): `intensity / dist^2 x N.L x
  facedot`, `facedot` minus the dot of the direction to the light and the face's normal, nothing where
  either cosine is at most 0.001 (`GatherSampleLight`). qrad3's inverse square never ends, so a face's
  range is where `intensity / dist^2` falls under one lightmap step (`MTL_RTDIR_EMIT_CUT`, 1): `sqrt(intensity)`.
  The kernel's light loop calls `mtl_rtdir_reach` (the cheap test) and `mtl_rtdir_contrib` (the
  dispatch) for every record; a point or spot light takes slice 1's arithmetic unchanged.
- **The bounds stay slice 1's** (until slice 3a): at most 16 lights a pixel (`mtl_rtdir_keep`, the same deterministic choice:
  the strongest, summed in the buffer's order; of equal strengths which one is kept is by position, see the 32 below), one shadow ray each. A face's intensity
  is held to `16384^2` (`RM_RTDIR_MAX_EMIT_INTENSITY`), so it reaches no farther than the strongest point
  light, and at most 2048 faces are packed (`RM_RTDIR_MAX_EMITTERS`, the rest counted); the buffer holds
  at most `RM_RTDIR_MAX_UPLOAD` (4096) records. The shadow ray stops a unit short of the light, which is a
  unit in front of the face, so it ends above the face's plane and never meets the face itself.

**The choices.** The plan (section 4.2) says the emitted colour is the texture's average colour "as qrad3
takes it"; TASK187's runbook left the two readings open. This slice takes qrad3's, so the light matches the
light baked into the stock lightmap the exposure fit compares against: the reflectivity is the texel mean,
doubled when its largest channel is under 0.5 (`CalcTextureReflectivity`, "because the textures are so
dim"), and a texture that cannot be read is 0 and lights nothing (qrad3 skips it with "Couldn't load").
Each other reading is one constant in `header/rm_rtdir.h`: `RM_RTDIR_EMIT_QRAD3_COLOUR 0` takes TASK187's
raw mean, and `RM_RTDIR_EMIT_UNREAD` set to `RM_RTL_UNREAD_GREY` takes its 0.5 grey. qrad3's exception
for texinfo 0 (0.5 when unread) is not followed: an unread texture lights nothing here whatever its index.

**Differences from qrad3, knowingly.** qrad3 chops a face into patches of 64 units and gives each its own
light; this slice takes the face whole, one light at its centre with the whole area. qbsp already cuts a
lightmapped face to at most 240 units, so far from the face the two agree; near it the light comes from
the centre, not spread over the face. And qrad3 limits a light to the clusters its PVS sees, where this
range is the cut above. The plan's "one light chosen per pixel per frame, weighted" is not followed: as in
slice 1, every light the pixel keeps, up to the 16, is traced, so the term is noise-free and no denoiser
(TASK59) is needed. It is not exact for its model once the cap bites, because the 16 are chosen before
visibility (next paragraph).

**The 16 are chosen before the shadow test** (slice 2; slice 3a chooses them among the visible ones). The kernel keeps the 16 strongest contributions at a pixel
(`mtl_rtdir_keep`, before the colour and the lightstyle, which only enter the sum) and only then traces
them, so an occluded light, or one whose style is dark, can hold a place while a visible one is dropped
without a ray. The choice is deterministic (a light replaces the weakest kept one only when strictly
stronger, and it takes the place of the first weakest by position in the kept set, not of the lowest index) and stable in time, so it does not flicker. Seams can show
where an occluded light's strength crosses a visible one's at neighbouring pixels: the sum steps by the
difference between the two. How often the cap bites is in the lines below: on `base2` a mean of 40 % of
the world pixels over 686 frames (6 % at frame 18, 34 % at frame 704), and about 30 to 37 % of the shadow
rays reach their light; on `waste2` 303436 of 307200 pixels at frame 447, with 9674097 lights in range
against 4912217 rays and 30 % of the rays lit. A choice among the visible lights, under a fixed ray
bound, or a PVS cull of emitters as qrad3 has, is left for slice 3. The 2048-emitter cap keeps the
faces in the map's face order, so on a map with more world light faces which ones light depends on that
order (the maps measured have 417 and 280).

**Not yet**: a face is not drawn at its own emission. qrad3 adds the face's light to its own lightmap
("the texture itself should still be full bright"); under RT-primary a light fixture's pixel takes only
the light other lights send it. The kernel cannot tell an emitter's pixel from the G-buffer (its material
is the world's), so this needs a material bit or a per-triangle table; it is slice 3's.

**Lines.** `r_rt_stats 1` adds the emitters' map line:
`ref_metal: rt direct: <map>: N emissive faces uploaded (B not the world's, S shapeless, D below qrad3's
direct light, U of an unread texture, O past the cap of 2048 left out, C held to the intensity cap)`.

### The lightstyles

Slice 1 already scales each light by its style each view (`rm_rtdir_styles`: the refdef's
`lightstyles[style].rgb` times `r_modulate`, as `R_BuildLightMap` takes them); the kernel reads
`styles[style & 255]`. A switched light (`style` 32 and up, set by the game when it is targeted) and an
animated one take the same path; an emissive face has style 0, as qrad3's. The check below records
RT-primary on `demo2.dm2` (the `dlights` scenario's second run, every 7th frame), whose frames 51, 61, 63,
67 and 69 show an animated style.

### The Ray tracing screen under RT-primary

`vidrules.c` follows Enhanced and Retro: under `r_presentation 3` on Metal, wherever the device traces rays
(the renderer runs the mode there although its menu item is greyed "awaiting review"), the four switches
`pres_rtprimary` decides are greyed "set by the presentation" and show the value in use (shadows, sun and
AO off, the water on), as are the sliders of the three effects it reads off; the gun shadow and the
water's sliders stay the user's, and "reset to defaults" writes no greyed row. The tuning screen's rows
of the sun and AO say "set by the presentation" too, not "turn on ... first", which the user cannot do
there. The status bar reads "Ray traced reads shadows, sun and AO as off, the water on". Nothing is
hidden; without ray tracing the screen is greyed for the device, as before.

### Tests

`tests/metal/rm_rtdir_test.c`: `test_emit_scale` (the falloff, both cosines, the range, the reach test,
the dispatch, and a distance of `1e-20` that stays finite, the distance taken as at least 1 as qrad3's;
red first: 3 checks failed with `inf`), `test_reflectivity` (qrad3's doubling, the unread texture, values that are not
numbers), `test_emitter` (a 64 x 32 face: the centre a unit out, the normal, the marker, the intensity
`300 x 2048 x 0.4`, a coloured texture's light, the `DIRECT_LIGHT` threshold, the cap, the shapeless
faces, the light the kernel's dispatch gives below it), `test_pack_emitters` (the counts of each reason,
the order, the same records twice, the cap of 2048, the line) and `test_mixed_choice` (points and faces in
one buffer: the 16 strongest, the same set for the same input, in the buffer's order; a near face takes
the weakest point's place). Red first, scratch copies of `rm_rtdir.c` with one guard changed each:

```
raw mean colour: FAIL tests/metal/rm_rtdir_test.c:1172: near(r[0], 0.8, 1e-6) && near(r[1], 0.4, 1e-6) && near(r[2], 0.2, 1e-6) | rm_rtdir_test: 457 checks, 2 failed
unread at grey: FAIL tests/metal/rm_rtdir_test.c:1175: (r[0] == RM_RTDIR_EMIT_UNREAD) && ... | rm_rtdir_test: 457 checks, 6 failed
no direct-light threshold: FAIL tests/metal/rm_rtdir_test.c:1222: rm_rtdir_emitter(&f, 23, faint, &l, &cl) == RM_RTDIR_EMIT_DIM | rm_rtdir_test: 457 checks, 6 failed
no intensity cap: FAIL tests/metal/rm_rtdir_test.c:1231: rm_rtdir_emitter(&f, 2000000000, white, &l, &cl) == RM_RTDIR_EMIT_KEPT && cl == 1 | rm_rtdir_test: 457 checks, 5 failed
centre on the face: FAIL tests/metal/rm_rtdir_test.c:1209: near(l.pos[0], 32, 1e-4) && ... | rm_rtdir_test: 457 checks, 1 failed
no direct_scale: FAIL tests/metal/rm_rtdir_test.c:1213: near(l.pos[3], 300.0 * 2048.0 * 0.4, 1e-1) | rm_rtdir_test: 457 checks, 4 failed
```

**The kernel and host pins** cover the new paths. `kernel_broken` now pins `rm_rtdir_direct`'s parameter
list whole (no texture, no buffer but the scene's and the lights), its two reads of the light buffer
(`lights[i]` and `lights[k.i[j]]`, no other), the reach test and the dispatch; `binding_broken` pins the
light buffer bound once, at the kernel's index 9, and the upload's cap `(n > RM_RTDIR_MAX_UPLOAD)`. Five
new mutants, each refused: `emit-buffer` (a new buffer into the trace, added to its light), `emit-third-read`
(a third read of the light buffer added to `raw`), `emit-bypass` (the reach test replaced by `dist > 0`),
and on the host `lights-cap` (the old cap of 2048) and `lights-twice` (a second buffer at index 9). The
three kernel mutants compile with `xcrun metal`. Red first, slice 1's rules against them:
`emit-third-read`, `emit-bypass`, `lights-cap` and `lights-twice` pass them, `rm_rtdir_test: 462 checks,
4 failed`. Green: `rm_rtdir_test: 462 checks, 0 failed`.

`tests/unit/test_vidrules.c` (`vidrules_rt_screen_under_rtprimary`) and `tests/menu/videomenu_test.c`
(`test_rt_screen_under_rtprimary`) hold the screen. Red first against the old rules:
`vidrules_rt_screen_under_rtprimary` failed (`VidRules_RTEffective(3, "metal", 1, rtp, 0.0f)` among
them), `videomenu_test: 3343 checks, 13 failed` (the AO sliders "applies from the next frame", the water's
sliders greyed, the reset writing a greyed row), and the tuning rows "turn on sun first" (`3348 checks, 3
failed`). Green: `yq2test: 30 tests, 0 failed` (the vidrules tests), `videomenu_test: 3382 checks, 0 failed`.

### The terms are live (M6, apple9, 640 x 480)

Recorded at `66fa02ed` (the code as in the final commit). `rtprimary-world` with `r_rt_stats 2` (a scratch
copy of the scenario, not committed; its frames hash as `rtprimary-world`'s, so the stats change no pixel),
on `demo1.dm2`, the map `base2`:

```
ref_metal: rt-data: maps/base2.bsp: lights 248 (spot 0, styled 4, untargeted 0) emitters 422 (textures 11, unread 0) sky 97,29,8 refused 0 (past the cap 0)
ref_metal: rt direct: maps/base2.bsp: 248 lights uploaded (0 dark, 0 spotlights without a target, 0 past the cap left out, 0 brighter than 16384 held to it)
ref_metal: rt direct: maps/base2.bsp: 417 emissive faces uploaded (5 not the world's, 0 shapeless, 0 below qrad3's direct light, 0 of an unread texture, 0 past the cap of 2048 left out, 0 held to the intensity cap)
ref_metal: rt direct: frame 18: 307200 pixels, 307200 world, 665 lights, 3088729 in range, 3045707 shadow rays (9.91 a world pixel), 901839 lit, 19406 pixels past 16 lights, fit 376127/274712 over 19200 samples, exposure 1.0000 (fit), 10.501 ms
ref_metal: rt direct: frame 19: 307200 pixels, 307200 world, 665 lights, 3088729 in range, 3045707 shadow rays (9.91 a world pixel), 901839 lit, 19406 pixels past 16 lights, fit 376127/274712 over 19200 samples, exposure 1.3692 (fit), 7.151 ms
ref_metal: rt direct: frame 704: 307200 pixels, 306572 world, 665 lights, 4174875 in range, 3971019 shadow rays (12.95 a world pixel), 1474682 lit, 105803 pixels past 16 lights, fit 550117/238515 over 19156 samples, exposure 2.3064 (fit), 6.828 ms
```

Against slice 1's frame 18 (`521201 in range, 521201 shadow rays (1.70 a world pixel), 489574 lit, 0 pixels
past 16 lights, fit 376127/258792`): the 417 faces put 2.5 million more lights in range, 16 % of whose rays
reach them (a face's range, up to `sqrt(intensity)`, runs far past the walls between); the traced sum
rises 6 % there and 9 % at frame 704, so the fit falls from 2.51 to 2.31. The cap now bites: over the 686
frames a mean of 123903 world pixels (40 %) have more than 16 lights in range and take the 16 strongest.
The pass's GPU time read 6.5 to 10.5 ms against slice 1's 2.2 ms, in a window not held quiet; it is
information, not a measurement (decision 0014).

The level of the frames (the slice 1 tool over the 23 frames; near-black: brightest channel at most 4), and
the harness's comparator (`framediff`, threshold 0):

| Record | Mean level | Near-black | Near-white | Against stock `world` (mean score, max) |
|---|---|---|---|---|
| stock `world` | 19.3 (9.6 to 38.0) | 3.3 % | 0.04 % | |
| `rtprimary-world`, slice 1 | 17.7 (6.4 to 34.3) | 33.9 % | 0.05 % | 0.0352 (0.0969) |
| `rtprimary-world`, slice 2 | 18.5 (8.8 to 36.3) | 16.0 % | 0.04 % | 0.0186 (0.0587) |

The emissive faces halve the near-black share (the parts no light entity reaches, which qrad3 lit by its
faces and bounces) and halve the distance to stock; the rest is the bounce (PM7) and the faces' own
emission (slice 3). Slice 2 against slice 1: 23 of 23 frames differ, mean score 0.0220 (max 0.0909). The
hashes of the four records: `rtprimary-world` `3e45a102fc0a`, `rtprimary-direct` `a38ba393ebf6`,
`rtprimary-vis` `8eab56606653`, `rtprimary-noshadow` `62207ce09aaa`, at `66fa02ed`. Recorded again at
`20ea6cbf` (the same kernel, the finite emitter term changing no frame of these) they read
`3e45a102fc0a`, `896c04dfc674`, `8eab56606653` and `d58e06da3c7f`: two of the four differ.

**Slice 2's RT-primary records varied run to run** (by a few levels in some frames), because the
exposure fit read whichever frame completed last (`rm_rtdir_fit_latest`), and slice 2's heavier pass
made the completion order vary. Slice 1's determinism paragraph held for slice 1's lighter pass. Slice 3a
takes the fit from a fixed frame, and its records are pins.

**The lightstyles** (`demo2.dm2`, every 7th frame, 89 frames, the map `waste2`, with a scratch text table
for the demo's opening text in captures 2 to 6, removed after the run):

```
ref_metal: rt-data: maps/waste2.bsp: lights 214 (spot 0, styled 2, untargeted 0) emitters 281 (textures 5, unread 0) sky 90,33,15 refused 0 (past the cap 0)
ref_metal: rt direct: maps/waste2.bsp: 214 lights uploaded (0 dark, 0 spotlights without a target, 0 past the cap left out, 0 brighter than 16384 held to it)
ref_metal: rt direct: maps/waste2.bsp: 280 emissive faces uploaded (1 not the world's, 0 shapeless, 0 below qrad3's direct light, 0 of an unread texture, 0 past the cap of 2048 left out, 0 held to the intensity cap)
ref_metal: rt direct: frame 447: 307200 pixels, 307200 world, 494 lights, 9674097 in range, 4912217 shadow rays (15.99 a world pixel), 1483298 lit, 303436 pixels past 16 lights, fit 192985/122266 over 19200 samples, exposure 1.1862 (fit), 6.695 ms
ref_metal: rt direct: frame 487: 307200 pixels, 307200 world, 494 lights, 8223498 in range, 4911930 shadow rays (15.99 a world pixel), 1277093 lit, 302006 pixels past 16 lights, fit 105691/62847 over 19200 samples, exposure 1.6671 (fit), 6.527 ms
```

Its two styled lights are kept with their styles (the packing keeps `style`, `test_pack` and
`test_styles` hold the table), and the record's captures 61 and 67, which the `dlights` scenario names as
frames an animated style changes, are the two style states the owner reviews. No style is lost on the
way: the record holds 89 frames (hash `d54969fcd1a3` at `20ea6cbf`, `065e6a52034e` at `66fa02ed`, before
slice 3a's fixed lag) and nothing needed fixing. The style state of a
capture is not in the lines (the stats line has no style column), so the check that the light follows
the style is the owner's, from those frames.

## Slice 3a: a deterministic exposure fit, and the 16 lights chosen among the visible ones

Card comment: "Slice 3a pulled: a deterministic exposure fit, so RT-primary records become pins, and the 16 lights
chosen among the visible ones under a fixed ray bound." The rest of "Left for slice 3" stays below.

### The exposure fit at a fixed lag

Slices 1 and 2 took the exposure from whichever frame completed last (`rm_rtdir_fit_latest`), so a frame's
exposure depended on the GPU's completion order, and slice 2's heavier pass made the records vary by a few
levels run to run. Now the frame numbered `f` takes the fit of frame `f - 3`, exactly:

- The back end keeps `RMB_FRAMES_IN_FLIGHT` (3) count slots, frame `f` in slot `f % 3`, so the slot frame `f`
  takes last held frame `f - 3`. The frame semaphore has seen that frame complete before frame `f` began
  (the clearing of the slot already relies on it). `rmb_rtdir_trace` reads the slot's fit sums there, before
  it clears them, and `rm_rtdir_fit_lag` (`rm_rtdir.c`) applies them when the slot's frame is exactly
  `f - RM_RTDIR_FIT_LAG` and after the map's reset; else the exposure in use is kept. `RM_RTDIR_FIT_LAG` is
  held to `RMB_FRAMES_IN_FLIGHT` by a `_Static_assert` in `rmb_rtdir.m`.
- The assumption under "frame `f - 3` is complete" is that every begun frame signals the semaphore only when
  its command buffer completes. In `rmb_encode.m` a begun frame whose target, depth target, command buffer
  or ring buffer failed to allocate still counts as begun and signals at once, so after such a failure a frame
  can begin while the frame three before it is in flight. It is an older gap (the ring buffer and the slot
  clearing rest on the same rule), reachable only when an allocation fails, and not changed here.
- A map's lights (`rmb_rtdir_set_lights`) start the fit again at 1, and its reset is the last frame encoded
  before them, so a frame of the old map still in its slot is not fitted from. So the first three frames of a
  map read 1, and a frame whose frame three before traced nothing (a menu, a skipped frame) keeps the
  exposure in use.
- `r_rt_direct_exposure` above 0 still fixes the exposure: the view carries it, and 0 there means "the back
  end's fit". The fit itself runs on either way (its sums are the lightmap's and the traced light's, before
  the exposure).
- The `r_rt_stats 2` lines still print in completion order (they are information); only the exposure was
  bound to it. The scene (`rm_rtdirscene.c`) no longer holds a fit, and `rmb_rtdir_last_frame` is gone.

So the frames are a function of the demo alone, and the RT-primary records are pins (below).

### The 16 lights chosen among the visible ones

Slice 2 kept the 16 strongest contributions and only then traced them, so an occluded light could hold a
place a visible one went without (a seam where their strengths cross). Now (`rm_rtdir_direct`,
`common/mtl_rtdir_math.h`):

- Of the lights in range, the **N = 32** strongest are the pixel's candidates (`MTL_RTDIR_MAX_RAYS`;
  `mtl_rtdir_keep`, the same deterministic choice as before: a light replaces the weakest kept one only when
  strictly stronger, and it evicts the first weakest by position in the kept set, not the one with the
  lowest index). So at equal strength the choice is deterministic but not "the earlier light": with lights
  0 to 31 kept, light 0 at 1, light 1 at 0.5 and the rest at 5, light 32 at 1 takes light 1's place, and a
  later light at 5 then evicts light 0, so light 32 is kept over the earlier light 0.
- Once kept, they are ranked strongest first, ties to the earlier light in the buffer (`mtl_rtdir_keep_rank`; the
  ranking is exact, which 32 are kept is as above), and
  traced in that order until 16 of them (`MTL_RTDIR_MAX_TRACED`) reach the pixel (`mtl_rtdir_walk`). Those 16
  are summed, in that order; an occluded light takes no place. So a pixel traces at most 32 shadow rays of at
  most 5 intersections each, whatever the map holds.
- **Past the bound**: a pixel with more than 32 lights in range of which fewer than 16 are seen does not try
  the weaker ones. Its sum is the visible lights among the 32 strongest, so a visible light ranked 33rd or
  lower is left out. Such a pixel is counted as capped (`mtl_rtdir_capped`) and the stats line reads
  `<n> pixels past the bound (32 rays, 16 lights)`. A pixel that finds its 16 is not capped, however many
  lights were in range. Before this slice the count was the pixels with more than 16 lights in range.
- It stays deterministic: the same view gives the same candidates, the same order and the same rays.
- Under `r_rt_direct_show 3` (no shadow ray) the first 16 ranked are summed, the 16 strongest as before.
- The choice is still made before the lightstyle: a switched-off light that is visible can take a place.

### Tests

`tests/metal/rm_rtdir_test.c`, written first against stubs that kept the old behaviour (the fit applying any
record it is given, the candidates in the buffer's order, every candidate walked, `MTL_RTDIR_MAX_RAYS` 16):
`rm_rtdir_test: 492 checks, 18 failed`. Green: `rm_rtdir_test: 496 checks, 0 failed` with the kernel pins
below.

- `test_fit_lag`: a record exactly the lag behind is applied; one nearer, older or two lags behind is not;
  one at or before the reset is not; no record, a slot never used, a frame before the lag and no sample keep
  the exposure.
- `test_fit_lag_runs`: a simulated back end of 3 slots over 200 frames, its frames in flight completing in
  another order each of four runs, a map change at frame 100 and every seventh frame tracing nothing: every
  run gives every frame the same exposure, frame 4 takes frame 1's fit, frame 10 keeps frame 9's (frame 7
  traced nothing), and frames 100 to 102 of the new map read 1.
- `test_keep`: the 32 candidates, the weaker and equal ones past the bound dropped, a stronger one taking the
  weakest's place, of 2048 rising lights the 32 strongest, ranked strongest first, ties by the buffer's order.
- `test_visible_choice`, a host model of the kernel's two loops over the same header functions: 20 lights
  whose 4 strongest are occluded sum the next 16 in 20 rays; with every other light occluded the first 16
  visible in 31 rays; with none visible 32 rays and the pixel capped; a visible light ranked 33rd is not tried
  and one ranked 32nd is; 10 lights with 3 occluded sum 7, not capped; 2048 visible lights take 16 rays, 2048
  occluded ones 32; equal lights give the buffer's first 16, the same twice.
- `test_mixed_choice` follows the ranking (the 20 points, then the 12 nearest faces).

**The kernel pin** (`kernel_broken`) holds the walk whole (`KERNEL_WALK`: the loop head with
`mtl_rtdir_walk(j, k.n, seen)`, the shadow ray, `continue` on a blocked one, then `seen++` and the sum), the
one `mtl_rtdir_keep(` and one `mtl_rtdir_keep_rank(` in the trace, `t.capped = mtl_rtdir_capped(k, seen);`,
the word `seen` four times there, and `rm_rtdir_seen(` twice in the file (its definition and the walk's one
call), so no ray is traced before the bound. Four new mutants, each refused, each compiling with
`xcrun metal`:

```
mutant trace-in-range: rm_rtdir_direct's walk is not its candidates traced strongest first, one ray each, under the bound
mutant walk-unbounded: rm_rtdir_direct's walk is not its candidates traced strongest first, one ray each, under the bound
mutant seen-before-ray: rm_rtdir_direct's walk is not its candidates traced strongest first, one ray each, under the bound
mutant vis-ignored: rm_rtdir_direct's walk is not its candidates traced strongest first, one ray each, under the bound
```

`trace-in-range` is the bound's: a shadow ray for every light in range in the first loop (up to 4096 a
pixel). `seen-before-ray` is the visibility test's: a light counted toward the 16 before its ray, which is
slice 2's choice. `walk-unbounded` drops the stop at 16 seen; `vis-ignored` lets a blocked ray count. The
`emit-third-read` mutant follows the new loop head. Red first: slice 2's kernel against the new rule,
`rm_rtdir.metal: rm_rtdir_direct's walk is not its candidates traced strongest first, one ray each, under the
bound`, `rm_rtdir_test: 496 checks, 5 failed` (the new mutants do not apply to its text).

### The records are pins (M6, apple9, 640 x 480)

Recorded at `cfd8d3ba` (the code of the final commit), three runs each (`--run 2`, `--run 3`), hashed as the
hash of the frames' sha256s:

| Record | Frames | Run 1 | Run 2 | Run 3 |
|---|---|---|---|---|
| `rtprimary-world` (`demo1.dm2`, `base2`) | 23 | `e6caa48ee013` | `e6caa48ee013` | `e6caa48ee013` |
| `rtprimary-direct` | 23 | `a3a554b71cdf` | `a3a554b71cdf` | `a3a554b71cdf` |
| `rtprimary-vis` | 23 | `469ce42fec8c` | `469ce42fec8c` | `469ce42fec8c` |
| `rtprimary-noshadow` | 23 | `b9206ff8cb45` | `b9206ff8cb45` | `b9206ff8cb45` |
| `rtprimary-world`, `PINS_DEMO=demo2.dm2 PINS_EVERY=7` (`waste2`, the lightstyles) | 89 | `0c678f1b2ea1` | `0c678f1b2ea1` | `0c678f1b2ea1` |

The `r_rt_stats 2` runs below (scratch copies of `rtprimary-world`, the stats changing no pixel) are a fourth
run of each map and give the same hashes, `e6caa48ee013` and `0c678f1b2ea1`. So each record is one set of
frames, byte for byte, and these hashes are the RT-primary pins from here (the scenarios README), provisional:
the plan (`docs/plans/presentation-modes.md` section 6, item 4) pins a mode's scenarios at the commit where the
owner accepts the look (PM11) and compares at 0.02 after it, so these are recorded again then; a later
slice that changes the look says so and records them again. The harness cannot carry a scenario's threshold:
`--threshold` and `--bad-threshold` are options of `pins.sh compare` (default 0.02 and 0.005, per run),
and no scenario file sets one, so the 0.02 is the default of the compare and needs no change; these records
are compared by hash for now, which is stricter. The demo's own text in the `demo2.dm2` captures
2 to 6 is declared in `scripts/pins/text/demo2.dm2-every7.txt`, committed now (slice 2 used a scratch copy).
The look itself is still the owner's to accept (PM11), from the `base2` capture 12 and the `waste2` captures
61 and 67, which are for the owner and not in the repository.

### Before and after (`r_rt_stats 2`, M6, apple9, 640 x 480, in the hold)

The base is main at `9f1d6d64`, built in a scratch copy and run by `PINS_BIN`; the branch is `cfd8d3ba`. Its
"pixels past" counts the pixels with more than 16 lights in range (slice 2's line); the branch's counts the
pixels past the bound (more than 32 in range and fewer than 16 seen). The means are over every frame line of
the run (687 on `demo1.dm2`, 617 on `demo2.dm2` every 7th):

| Map | Side | Pixels past (mean share) | Shadow rays a world pixel (mean) | Pass GPU time (mean) |
|---|---|---|---|---|
| `base2` | base | 40.6 % (past 16 in range) | 12.86 | 6.09 ms |
| `base2` | branch | 5.5 % (past the bound) | 15.69 | 7.81 ms |
| `waste2` | base | 88.4 % (past 16 in range) | 15.73 | 6.46 ms |
| `waste2` | branch | 34.5 % (past the bound) | 26.10 | 10.75 ms |

The lines at frame 447 of `base2` show the trade:

```
base:   frame 447: 307200 pixels, 307200 world, 665 lights, 6916616 in range, 4800299 shadow rays (15.63 a world pixel), 1024409 lit, 227989 pixels past 16 lights, fit 159204/71321 over 19200 samples, exposure 2.2663 (fit), 6.985 ms
branch: frame 447: 307200 pixels, 307200 world, 665 lights, 6916616 in range, 6841409 shadow rays (22.27 a world pixel), 1417470 lit, 35474 pixels past the bound (32 rays, 16 lights), fit 159204/71879 over 19200 samples, exposure 2.2396 (fit), 9.570 ms
```

The same lights are in range. The branch traces 43 % more rays, 38 % more of them reach their light
(1417470 against 1024409), and the traced sum rises 0.8 % (71879 against 71321); the exposures the two frames used (each the fit of its frame
three before, or in the base the latest completed) read 2.27 and 2.24. On `waste2` at frame 447 the branch reads `9674097 in range, 9078373 shadow rays (29.55 a world
pixel), 1996346 lit, 126870 pixels past the bound`, against the base's `4912217 shadow rays (15.99 a world
pixel), 1483298 lit, 303436 pixels past 16 lights`. A third of the `waste2` pixels still find fewer than 16
visible lights among their 32 strongest: the faces' ranges run far past the walls between (slice 2), and a
PVS cull of emitters is what would let those pixels reach their visible lights within the bound. The GPU
times were read in the hold, on a quiet host. They are information, not a gate (decision 0014).

### Stock is unchanged

The same method as slice 2 (every committed `metal` scenario but RT-primary's, seeded, the water scenarios on
one `water.dm2` copied to both, `cinematic` on `idlog.cin` at `PINS_EVERY=7`), base main at `9f1d6d64` and the
branch at `cfd8d3ba`: 29 of 29 stock scenarios byte-identical, no failures. At `r_presentation 0` the slice
changes no pixel.

### Checks

In one hold on the code of the final commit (`cfd8d3ba`): `make ref_metal`, `make`, `make test`
(`yq2test: 591 tests, 0 failed`), `tests/check-ref-metal.sh` (`direct light 496 checks, 0 failed`),
`tests/check-cvar-docs.sh`, `tests/check-runbook-index.sh`, `tests/check-doc-links.sh`,
`tests/check-video-menu.sh` and `tests/check-pin-chars.sh`, all ok; the stock pin compare (29 of 29) and the
RT-primary records, three runs each with one hash. No frame was looked at by the agent.

## Left for slice 3b (and later rows)

- **An emissive face drawn at its own emission** (see above): a material bit in the G-buffer or a
  per-triangle table, under RT-primary only.
- **A face cut into qrad3's 64-unit patches**, if the frames near a fixture call for it.
- **RT3's dlights as a term** of the light model, in place of the layer this mode turns off.
- **A PVS cull of emitters**, as qrad3 has, if the share of pixels past slice 3a's bound calls for it.
- **A finer exposure fit**: at map load from a sample of luxels (the plan's wording), or a fixed value the
  owner picks from frames, and a fit that leaves out the near-black parts the emissive faces now light.
- **The water reflections** (`rmb_rtwater.m`) still shade the reflected world from the lightmap array
  under RT-primary: that is a water pixel's light, not a world pixel's, and is slice 3 or PM7.
- **TASK187's open choices** stand as they were (empty values, a short `_color`, a fractional `_style`); a
  spotlight with a missing target now lights nothing, as in qrad3.
- Sky light, the sun and the bounce (PM7), the denoiser and the MetalFX scaler through the overlay (PM8, where
  the scaler's absence on visionOS starts to matter), models (PM9).

## Checks

Slice 2, in one hold on the code of the final commit (`20ea6cbf`): `make ref_metal`, `make`, `make test`
(`yq2test: 563 tests, 0 failed`), `tests/check-ref-metal.sh` (`rm_rtdir_test` 465 checks, 0 failed),
`tests/check-cvar-docs.sh`, `tests/check-runbook-index.sh`, `tests/check-doc-links.sh` and
`tests/check-video-menu.sh`, all ok; the stock `metal` pin run (29 of 29, above) and slice 2's RT-primary
frame records, which varied run to run until slice 3a.
