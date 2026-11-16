# Bringing the ray-traced AO pass within its share in `ref_metal` (TASK157)

TASK57 built the ray-traced ambient occlusion ([TASK57](TASK57.md); design section 2) and
measured 3.67 ms of GPU time at 1920 x 1080, over the AO's share of the design's frame budget.
This card profiles the pass, adds two cvars that cut it, `r_rt_ao_scale` and `r_rt_ao_rays`
(`doc/040_cvarlist.md`), picks new defaults, and measures them at 1920 x 1080 and 5120 x 2880,
with and without MetalFX. The GPU time is the pass's compute encoder's timestamps, the
`r_rt_stats 2` line, as in TASK57's *Cost* and the shared runbook's [shadows'
proofs](../plans/metal-port-harness.md#ray-traced-shadows-from-dynamic-lights-in-ref_metal-task55-rt3).

**Verdict.** At the new defaults (`r_rt_ao_scale 0.5`, `r_rt_ao_rays 2`, `r_rt_ao_range 128`)
the pass takes 2.10 ms at 1920 x 1080 by TASK57's method (the frame held to the display's
60 Hz), from 3.62 ms, inside the share's 1.4 to 2.4 ms (design section 2), and 0.63 ms with the
frame rate not held, the GPU level section 8's budget figures are measured at, from 1.11 ms.
Most of TASK57's 3.67 ms is the GPU's clock: held at 60 Hz the GPU idles between frames and
runs the pass about 3.3 times slower. At 5120 x 2880 with MetalFX (the default 1080-line input)
the pass is 3.19 ms held and 0.69 ms not held; without MetalFX 8.26 and 4.62 ms.
`r_rt_ao_scale 1` is TASK57's pass, frame for frame.

## What changed

- `r_rt_ao_scale` (archived, default `0.5`; `1`, `0.5` or `1/3`, the nearest divisor of the
  view): below 1 the rays are traced from one pixel of each 2 x 2 (or 3 x 3) block
  (`rm_rtao_trace_lo`, into an `R8Unorm` texture at the reduced size), and `rm_rtao_upsample`
  gives each world pixel the bilinear mean of the four traced pixels about it that lie on its
  own surface (normals within about 26 degrees, the traced point within `2 + 0.004 d` units of
  the pixel's plane, `d` its distance from the eye); where none does, the mean of the world
  pixels among them; where none is a world pixel, 1. Where the four hold one 8-bit AO the
  pixel takes it without reading their surfaces (the host test holds that this is the same
  8-bit value). A mean of values in `[0, 1]`, so the factor's floor `r_rt_ao_min` holds by
  construction. At `1` the pass is TASK57's kernel, unchanged (`rm_rtao_trace`).
- `r_rt_ao_rays` (archived, default `2`; `1` or `2`): one ray traces ray 0's direction with
  its azimuth's number doubled, so it covers the whole circle (`rm_rtao_trace1` at scale 1).
- Both in the video menu's renderer options ("AO trace size", "AO rays"), always shown, greyed
  with "Metal renderer only" on other renderers and "needs r_rt_ao 1" while the AO is off.
  Neither can show anything the AO does not (the factor only darkens, never below the floor),
  so neither is in the multiplayer cheat reset.
- The stats line keeps its text; below scale 1 its rays and hits are the traced size's.
- Host test (`tests/metal/rm_rtao_test.c`, 281 checks, written first and failing on the
  missing functions): the two cvars' readings and defaults, the reduced size and source pixel
  of every traced pixel for divisors 1 to 3 and views of 1 to 70 pixels, the pairs and weights
  of every full pixel, the surface test, the mean (its fallbacks, a traced pixel taking its own
  AO bit for bit, 20,000 random cases within the inputs' range and above the floor, the
  four-equal shortcut), one ray's AO and its directions over both halves of the circle, the
  view carrying both. The menu rules: `tests/unit/test_vidrules.c` (two tests) and
  `tests/menu/videomenu_test.c` (both items in the greyed-item check).

## Method

Engine commit measured: `2949d1d8` (pin ids `2949d1d8d3af-*`), the branch's last source
commit; `origin/main` is `cfec997d` (an export built in scratch, pin ids `nogit-*`). Host: the
bare-metal Apple M6. Every build, check and engine run went through the host's lock wrapper;
before each run the batch checked that the screen was unlocked and that no other `quake2` ran,
and logged the one-minute load: 1.2 to 4.3 over the timing runs. Cost runs: TASK57's `t57cost`
scenario (`demo1.dm2` whole, seeded, `PINS_EVERY=300`, no validation layer) with the case's
cvars appended, each run's lines past its first 100 (587 frames; 586 not held), three runs a
case alternated in rounds (two for some not-held cases at 1080p, which agree within 0.01 ms).
5120 x 2880 is `PINS_HIDPI=1` at 2560 x 1440. "Held" is the scenario as TASK57 ran it, the
display's vsync on; "not held" adds `r_vsync 0`. MetalFX is `r_metalfx 2` at the default scale
(1080 lines on apple9: the 5K output traced at 1920 x 1080, the 1080p output at scale 1).

**Baselines.** The stored AO baseline (`48628e50`) is the code before TASK57's review round 1.
One drift run of `origin/main` read 3.616 ms against its 3.689 to 3.701, so main was measured
three times (3.609 to 3.616, median 3.612 ms) and stored as main's baseline (`cfec997d5c39`).
The head at `r_rt_ao_scale 1` reads 3.617 (3.607 to 3.714), main's kernel. At 5120 x 2880 one
run of main read 9.862 ms, the head at scale 1 9.789 (9.778 to 9.888).

## The profile at 1920 x 1080

| Case | Rays a frame | Held: median (run medians) | p90 | Not held: median |
|---|---|---|---|---|
| main (`cfec997d`) | 4.11 M | 3.612 (3.609 to 3.616) | 4.15 to 4.22 | |
| scale 1, TASK57's pass | 4.11 M | 3.617 (3.607 to 3.714) | 4.15 to 4.55 | 1.106 (1.096 to 1.112) |
| scale 1, range 1 (the rays end at once) | 4.11 M | 2.256 (2.254 to 2.330) | 2.74 to 2.96 | 0.680 |
| scale 1, range 64 | 4.11 M | 2.973 (2.959 to 2.985) | 3.50 to 3.59 | 0.897 |
| scale 1, one ray | 2.06 M | 2.241 (2.238 to 2.256) | 2.52 to 2.70 | 0.673 |
| scale 1, with entities | 4.04 M | 3.699 (3.692 to 3.699) | 4.18 to 4.22 | 1.129 |
| **scale 0.5 (new default)** | 1.03 M | **2.103 (2.080 to 2.103)** | 2.25 to 2.56 | **0.625 (0.623 to 0.625)** |
| scale 0.5, with entities | 1.01 M | 2.116 (2.114 to 2.125) | 2.26 to 2.27 | 0.623 |
| scale 1/3 | 0.46 M | 1.617 (1.616 to 1.630) | 1.69 to 1.75 | 0.477 |
| scale 1, MetalFX at 1080p | 4.11 M | 3.091 (2.980 to 3.192) | 3.95 to 4.20 | |
| scale 0.5, MetalFX at 1080p | 1.03 M | 1.748 (1.736 to 1.751) | 2.27 to 2.30 | |

Where the time goes:

- *The clock.* Held, the same work takes 3.3 to 3.4 times as long as not held (3.617 against
  1.106, 2.103 against 0.625), the factor section 8 found between a process's start level and
  its later level (3.45 to 3.6). MetalFX at 1080p adds GPU work to the frame and the pass reads
  15 % less. So TASK57's "2.1 ms that does not grow with the pixels" is the clock, not work.
- *The rays' length and hits.* At range 1 the rays end at once; the 128-unit traversal is the
  rest: 1.36 ms held, 0.43 ms not held, about a third of the pass. Range 64 saves 0.64 ms held.
- *Rays per pixel.* One ray saves 1.38 ms held (0.43 not held): a ray costs about the same
  whatever its length up to 128 units once launched, so halving the rays halves the traversal
  and the launches.
- *The floor.* The rest, 2.26 ms held and 0.68 not held, is the per-pixel work at the render
  size: the G-buffer reads, the noise, the light's copy at `RGBA16Unorm` and the launches.
- *The structure.* The entities add 0.08 ms held (0.02 not held) at scale 1 and nothing
  measurable at 0.5: the alias models and brush entities are not a lever.
- *The hit loop.* The loop passes sky, water and translucent triangles; `demo1.dm2` has few, and
  no run timed it apart. *Resolve and denoise.* There is no denoise yet (RT7); the resolve is
  the light's copy, in the floor.

So the levers are the traced size (rays and launches with it) and the rays; the floor stays
at the render size. Scale 0.5 takes 1.51 ms off held (0.48 not held), scale 1/3 2.00 (0.63).

## At 5120 x 2880

| Case | Rays a frame (traced at) | Held: median (run medians) | p90 | Not held |
|---|---|---|---|---|
| main (`cfec997d`), one run | 29.2 M | 9.862 | 10.67 | |
| scale 1 | 29.2 M | 9.789 (9.778 to 9.888) | 10.54 to 10.71 | 7.839 (7.838 to 7.849) |
| scale 1, range 64 | 29.2 M | 9.335 (9.258 to 9.380) | 10.12 to 10.29 | |
| scale 1, one ray | 14.6 M | 8.350 (8.338 to 8.476) | 8.91 to 9.26 | |
| scale 1, with entities | | 9.855 (9.817 to 9.912) | 10.65 to 10.68 | |
| **scale 0.5** | 7.3 M | **8.261 (8.233 to 8.356)** | 8.77 to 8.99 | **4.622 (4.536 to 4.625)** |
| scale 0.5, with entities | | 8.172 (8.118 to 8.316) | 8.64 to 9.04 | |
| scale 1/3 | 3.2 M | 7.122 (7.101 to 7.205) | 7.72 to 8.13 | |
| scale 1, MetalFX (1920 x 1080) | 4.11 M | 3.772 (3.743 to 3.783) | 4.06 to 4.16 | 1.335 (1.328 to 1.346) |
| **scale 0.5, MetalFX** | 1.03 M | **3.185 (3.143 to 3.343)** | 3.52 to 3.60 | **0.691 (0.672 to 0.692)** |
| scale 1/3, MetalFX | 0.46 M | 2.889 (2.873 to 2.966) | 3.15 to 3.20 | 0.512 (0.508 to 0.513) |

Held, the 5K frame does not keep the GPU busy either, so these too are at a lowered clock.
With MetalFX the pass traces the 1080-line input and is a fraction of the native pass; the
MetalFX scaler itself is about 5 ms at this output ([TASK111](TASK111.md)).

## Image quality: the new default against the current AO

Records at `2949d1d8`, seeded, under `MTL_DEBUG_LAYER=1`: `rt-ao-entities` at 1920 x 1080
(`demo1.dm2`, 23 frames), `rt-ao` and `rt-ao-entities` at 640 x 480 (`demo1.dm2`, 23 frames;
`demo2.dm2` every 7th, 89), each with an off view (`r_rt_ao 0`, `r_rt_gbuffer 1`), the new
default and, at `demo1.dm2`, `r_rt_ao_scale 1`.

- *The floor check* (`aofloor off new 0.4`, TASK57's tool and derived allowance): **all 247
  frames pass**, no channel under and none brighter than the off view.
- *Scale 1 is the current look.* The `r_rt_ao_scale 1` records of `rt-ao` and `rt-ao-entities`
  at `demo1.dm2` are pixel-identical to `origin/main`'s stored records (23 of 23 frames each).
- *The masked comparison* (`aocmp`, local: the mask is the pixels the AO changes in either view,
  the current or the new one differing from the off view):

  | Record | Masked pixels | Changed | By more than 8 levels | By more than 32 | Largest |
  |---|---|---|---|---|---|
  | `rt-ao-entities`, 1920 x 1080 | 29,306,217 | 26,565,904 (90.6 %) | 2,447,595 (8.4 %) | 57,071 (0.19 %) | 100 |
  | `rt-ao`, 640 x 480 | 4,204,949 | 3,804,896 (90.5 %) | 301,542 (7.2 %) | 6,957 (0.17 %) | 98 |
  | `rt-ao-entities`, 640 x 480 | 4,213,470 | 3,819,899 (90.7 %) | 336,986 (8.0 %) | 7,050 (0.17 %) | 98 |

  Almost every darkened pixel changes, and by few levels: the mean change over the mask is
  about 6 levels at 1080p, and darker and brighter pixels are near even (14.3 M and 12.2 M at
  1080p). Each pixel now takes four traced pixels' noise, so the grain is finer and lower; the
  larger changes are single grainy pixels of the current AO and edges where the traced pixels
  lie on another surface.
- *Frames for the owner.* Four pairs at 1920 x 1080, `rt-ao-entities` at `demo1.dm2` frames 7,
  8, 19 and 13 (TASK57's frames with the most occluded pixels), `<n>-current.png` (scale 1) and
  `<n>-new.png`, converted with `sips`, in `<artifacts>/TASK157/frames/`, never committed.
- *Validation.* Every image record's log holds the validation banner and no other validation
  line and ends in `teardown: 0 live objects`.

## Effect off

The 23 records of TASK57's list (the 18 default scenarios, `dlights` at `demo2.dm2` every 7th,
`rt-shadows` and `rt-shadows-entities` at both demos) recorded at `2949d1d8` with
`MTL_DEBUG_LAYER=1`, against main's stored records (`baselines/ddbfb6ff88b8/pins`, whose `src/`
is `cfec997d`'s): **716 frames, every one byte-identical.** `rt 0/0` for the 19 records without
an effect, `rt 77/77`, `56/56`, `320/320` and `474/474` for the shadows; the validation banner
only, `teardown: 0 live objects`.

## The cost against the share

Design section 2 gives the AO no budget of its own; TASK57 derived its share from section 8's
proportions: 1.75 of 5.14 ms, so 1.4 to 2.4 ms of the 4 to 7 ms total at 1080p. Section 8's
figures are measured at the GPU's later level, which the not-held runs match.

- 1920 x 1080: 2.10 ms held, inside the share (from 3.62, over it); 0.63 ms not held, a third
  of the share's low end and of section 8's 1.75 ms for two rays on every pixel (from 1.11).
- 5120 x 2880 with MetalFX: 3.19 ms held, 0.69 ms not held, against the primary
  cell's 2.13 ms for the AO at 1080 traced rows (design, "Each effect alone in the primary
  cell").
- 5120 x 2880 native: 8.26 ms held, 4.62 ms not held, against 12.78 ms modelled for
  the AO at native pixels (option C); not a target (option C is over the frame, design section 2).

## Checks

On `2949d1d8`'s source with these documents, every one through the host's lock wrapper:

- `make test`: `yq2test: 165 tests, 0 failed` (the two new menu-rule tests among them).
- `tests/check-ref-metal.sh`: ok (AO 281 checks, 0 failed; `ref_metal` built).
- `tests/check-pins.sh`: `pins selftest: ok`. Its first run, straight after a batch of engine
  runs, failed five of the selftest's parts (thresholds, replace, hidpi, replay_compare,
  demo_sum); run again alone it passed. This card changes nothing the selftest runs.
- `tests/check-cvar-docs.sh`, `tests/check-doc-links.sh`, `tests/check-no-local-identity.sh`
  and `tests/check-video-menu.sh` (`videomenu_test: 2594 checks, 0 failed`): ok.
- Builds: `make client`; `make -B ref_metal` with and without `METAL_SHADERS=no`; `make ASAN=1
  client game ref_metal`; `make STATIC_MODULES=1 STATIC_RENDERER=metal client`: no error, and
  no warning but the vendored `stb_vorbis.h`'s and the `sprintf` deprecations outside this
  card's files.

Every log, script, record and tool (`cost.sh`, `iq.sh`, `aocmp.c`, the stores) is in the lane's
scratch space and `<artifacts>/TASK157/`; main's measured baseline is in
`<artifacts>/baselines/cfec997d5c39/`. Nothing derived from game data is in the repository.
