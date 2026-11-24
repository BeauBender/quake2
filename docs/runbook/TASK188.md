# RT-primary direct light from the map's point lights, one shadow ray each (TASK188, PM6 slice 1)

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
- **The shadow ray**, one a light in range and pixel: from the surface point offset along its normal
  (TASK57's `mtl_rtao_bias`) to a unit short of the light's origin, against the frame's instance structure
  with `MTL_RTAO_MASK` (the world, the brush entities, the alias models, not the view weapon). Water and
  translucent faces let it through, up to four of them; any other face shadows, the sky's included.
- **The lightmap never lights the pixel it is on** (the plan's section 6, item 5). The kernel reads the
  stylized lightmap once, in the exposure fit's sample, which feeds only the frame's sum. A world pixel's
  light is `mtl_rtdir_light(raw, exposure)`, which has no lightmap argument; every other pixel keeps the
  light it had, bit for bit.
- **The exposure, the stated rule.** `exposure = sum of the stock lightmap / sum of the traced light` over
  the same sampled world pixels (every fourth pixel a row and a column) of the latest completed frame,
  clamped to 0.25 to 16; 1 at a map's first frame and kept while no traced light reaches a sample. Each
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
- **Lines.** `r_rt_stats 1`: `ref_metal: rt direct: <map>: N lights uploaded (...)`. `r_rt_stats 2`, a frame:
  world pixels, lights in range, shadow rays, rays that reached their light, the fit's sums, the exposure
  and the pass's GPU time.

New cvars: `r_rt_direct` (the effect switch, default 0, read 1 by the mode), `r_rt_direct_exposure` and
`r_rt_direct_show`, in `doc/040_cvarlist.md` and classified in `rm_rtgate.c`. New scenarios:
`rtprimary-world`, `rtprimary-direct`, `rtprimary-vis`, `rtprimary-noshadow` (not pinned: they are
recorded against `ref_metal` itself once the owner accepts the look, PM11).

## Host tests

`tests/metal/rm_rtdir_test.c` (in `tests/check-ref-metal.sh`, with ASAN and UBSAN): the mode's switch under
each presentation, qrad3's falloff and cone, the shadow ray's step, the light and its clamps, the fit's
samples and their order-free sums, the fit and the fixed exposure, the packing (a missing target, dark
lights, the cap), the style table, the view and the lines; and the kernel's text for the no-lightmap
invariant. Red first: before `rm_pres_decide` chose the mode, `rm_rtdir_test: 355 checks, 5 failed`.
Then three mutants of `rm_rtdir.metal` that add the lightmap back, each failing:

```
real: rm_rtdir_test: 366 checks, 0 failed
add-lm: FAIL tests/metal/rm_rtdir_test.c:216: count(text, "lm.read(") == 1 rm_rtdir_test: 366 checks, 1 failed
reuse-sample: FAIL tests/metal/rm_rtdir_test.c:218: count(text, "lms") == 4 rm_rtdir_test: 366 checks, 1 failed
world-from-base: FAIL tests/metal/rm_rtdir_test.c:221: count(text, "base.read(") == 1 && count(text, "light_out.write(base.read(px), px);") == 1 rm_rtdir_test: 366 checks, 1 failed
```

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
frames completed in another order could differ in its first frames; none did here.

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

## Left for slice 2 (and later rows)

- **Emissive faces** (`SURF_LIGHT`, TASK187's emitters) as area lights, and their colour: TASK187's raw texel
  mean or qrad3's normalized and doubled one, and an unread texture at 0.5 or 0.
- **Lightstyles** are applied here per light (a light's `style` scales it each frame); slice 2 checks the
  switched and animated styles on a map that has them (`demo2.dm2`, the `dlights` scenario's second run).
- **RT3's dlights as a term** of the light model, in place of the layer this mode turns off.
- **A finer exposure fit**: at map load from a sample of luxels (the plan's wording), or a fixed value the
  owner picks from frames, and a fit that leaves out the near-black parts slice 2 will light.
- **The menu**: under RT-primary the Ray tracing screen does not yet grey the rows the mode decides ("set by
  the presentation"), as it does for Enhanced and Retro; the mode itself is greyed "awaiting review".
- **TASK187's open choices** stand as they were (empty values, a short `_color`, a fractional `_style`); a
  spotlight with a missing target now lights nothing, as in qrad3.
- Sky light, the sun and the bounce (PM7), the denoiser and the MetalFX scaler through the overlay (PM8, where
  the scaler's absence on visionOS starts to matter), models (PM9).

## Checks

In one hold on the final commit: `make ref_metal`, `make`, `make test`, the stock `metal` pin run above
against the base's records, the RT-primary frame record, `tests/check-ref-metal.sh`,
`tests/check-cvar-docs.sh`, `tests/check-runbook-index.sh` and `tests/check-doc-links.sh`; their last
lines are in the card's report.
