# Design: hybrid ray tracing in `ref_metal` (TASK24) and render-stage ray queries (TASK25)

Plan anchor: `docs/plans/metal-renderer.md` Phases E and F; INFORM7 (#7: no
Q2RTXM import, rasterizer first, hybrid RT on real hardware); verification
through the pin harness (`docs/plans/metal-port-harness.md`, TASK27).
Prerequisite: TASK28's native `ref_metal` passes every gl1 pin with RT off.

Every number marked *assumed* is a design assumption to be replaced by a
measurement in the first sub-card (RT1); the engine limits are from the tree.

## 1. Scene representation

| Source (tree) | RT representation | Rebuild cadence |
|---|---|---|
| World `msurface_t` polys (`gl1_surf.c`; fan-triangulated `glpoly_t`, `numverts`) | one static triangle mesh per map, one primitive acceleration structure (PAS) | once at map load |
| `SURF_SKY` faces (`files.h` 0x4, not drawn, skybox) | kept as triangles with material id `sky`; a ray hitting them is "sees sky" | once |
| `SURF_WARP` water (`gl1_warp.c` `R_EmitWaterPolys`, `r_turbsin` vertex warp) | flat (un-warped) triangles, material id `water`; the warp is a shading-time normal perturbation, never geometry | once |
| `SURF_TRANS33/66` | triangles with `alpha` material; any-hit shader passes through them for shadow rays (33/66 attenuation), treats them as opaque for reflection rays | once |
| Lightstyle-animated faces (`styles[MAXLIGHTMAPS=4]`, `lightstyles[256]`) | geometry unchanged; animation is lighting only (section 2) | never |
| Inline brush models (`MAX_MAP_MODELS 1024`, doors, platforms) | one PAS per inline model, instanced with its entity transform | PAS once; instance transform per frame |
| Alias models (`gl1_mesh.c` `R_DrawAliasFrameLerp`: `oldframe`/`frame` verts, `backlerp`) | one PAS per model per visible entity, vertices lerped on the GPU into a per-instance vertex buffer, then **refit** (not rebuilt) | refit per frame per visible alias entity, ≤ `MAX_ENTITIES` 128 |
| Sprites, beams, `MAX_PARTICLES` 4096 particles | excluded from the acceleration structures; they neither cast nor receive RT effects (particles keep their stock additive look) | – |
| View weapon (`cl_gun`) | excluded from the scene (it would shadow the whole screen); receives AO and shadows as a normal alias instance only if `r_rt_gunshadow 1` | – |

Instance acceleration structure (IAS): rebuilt every frame from the visible
entity list (`refdef_t.entities`, ≤128) plus the world and the inline brush
models. Rebuilding it is a fraction of a millisecond: 0.17 ms for 141
instances later in a process and about 0.6 ms in its first frames on the
synthetic scene of section 8, and 0.28 to 0.42 ms (median) for 15 to 83
instances in `ref_metal` on real maps (below).

Memory budget: an alias model is at most 4096 triangles (MD2
`MAX_TRIANGLES`); `MAX_MAP_FACES` is 65536. Measured by RT1 (TASK53,
below) on `base1` and the two maps the stock demos play (the owner's data
has `demo1.dm2`, which plays `base2`, and `demo2.dm2`, which plays
`waste2`; there is no `demo3.dm2`): 19k to 22k world triangles, a world
PAS of 1.8 to 2.1 MB (93.7 bytes per triangle on all three maps, the
94 of section 7's soup), under the 4 MB the design assumed, and on a busy
demo frame 65 to 77 alias instances of about 100 triangles each on
average (not 300 to 800), 6.9k to 7.7k triangles refit per frame, far
inside the 1 M bound.

As built (RT1, TASK53; `rm_rt.c`, `rm_rtscene.c`, `rmb_rt.m`,
`rm_rt.metal`): the world PAS and one PAS per inline model are built at
map load from N4's retained buffers without a copy (the vertex buffer at
`RM_WVERT_STRIDE`, the uint32 index buffer at each model's range), with a
per-triangle material buffer of the ids of section 3 (a warped face is
`water` even when it is translucent, as stock water is, so RT6 finds it;
`SURF_TRANS33`/`66` otherwise `alpha`; flowing faces `world`). Each alias
model's frames and triangle list go to the GPU on its first visible frame
(the file's triangles checked against `num_xyz`, which the loader does not
do); each alias instance takes a structure from a pool keyed by model,
built on its first use and refit after, from a compute lerp of its
`oldframe`, `frame` and `backlerp` with the factors of gl1's
`R_DrawAliasFrameLerp` (`rm_alias_lerp_setup`). Shells (`RF_SHELL_*`) are
left out as well as the rows above: the client adds a shell as a second
entity over its model, which would occlude it. Entities are not frustum
culled (an entity outside the view still casts into it). The frame's work
is three encoders in the frame's command buffer before the 3D pass (the
lerps, the alias builds and refits, the IAS build), with GPU timestamps at
their boundaries. Nothing reads the structures. Building is on with
`r_rt_stats` (any value but 0), which also prints the counts; with every
`r_rt_*` cvar at its default, or on a device without `supportsRaytracing`,
nothing is built or allocated (`rm_rt_wanted`), and the teardown line
counts the ray-tracing objects as `rt` (`rt 0/0` then).

| Measured (TASK53, Apple M6, 640 x 480) | `base1` | `base2` (`demo1.dm2`) | `waste2` (`demo2.dm2`) |
|---|---|---|---|
| World triangles (world, sky, water, alpha) | 20,495 (19,561, 608, 326, 0) | 22,324 (21,188, 312, 824, 0) | 19,042 (18,205, 229, 608, 0) |
| World PAS bytes | 1,921,280 | 2,092,608 | 1,785,088 |
| Inline models with triangles, their triangles, PAS bytes in all | 34, 1,272 (1,228 world, 44 alpha), 131,456 | 59, 3,532 (all world), 350,464 | 38, 1,902 (all world), 191,424 |
| Material ids (one byte per triangle of the map) | 21,767 | 25,856 | 20,944 |
| Build scratch (freed after the build) | 942,592 | 1,047,296 | 883,456 |
| Build at map load, world and inline PASes in one command buffer (GPU ms, every run) | 2.201 to 2.567 (4 runs) | 2.124 to 2.355 (6) | 2.129 to 2.302 (3) |
| Busiest frame: instances (inline, alias), alias triangles | 15 (3, 11), 2,642 (the spawn point, no demo) | 71 (5, 65), 6,882 | 83 (5, 77), 7,701; 8,656 on another frame |
| Busiest frame's bytes: alias PASes, lerped vertices, alias models' frames and triangles, IAS, per-frame scratch | 253,440, 23,088, 41,686, 3,712, 14,336 | 720,192, 61,856, 470,238, 15,872, 28,672 | 929,536, 82,368, 888,482, 18,432, 33,024 |
| Per frame, warm (frames after the first 100): alias lerps and refits, GPU ms median (min, p95, max) | 0.322 (0.300, 0.360, 1.036), 873 frames | 0.442 (0.310, 0.562, 1.210), 588 | 0.504 (0.354, 0.849, 2.106), 517 |
| The same, IAS rebuild | 0.283 (0.264, 0.319, 0.965) | 0.351 (0.276, 0.430, 0.698) | 0.418 (0.298, 0.527, 0.661) |

The build at map load (about 2.2 ms for 21k to 26k triangles) and the
per-frame times are at or near section 8's start level, not its later
level (0.50 to 0.71 ms for a 10k to 20k build, 0.17 ms for a 141-instance
rebuild): the frames of these runs hold about 0.15 ms of raster each
(`MTL_GPU_TIME`, `base2` with every `r_rt_*` cvar at its default: median
0.142 ms per frame command buffer, 0.841 ms with the structures built), so
the GPU never does the milliseconds-long work after which section 8 saw
the level change. This is consistent with section 8's hypothesis that the
level follows the load, and was not tested further. Commands and runs:
the card file [`docs/runbook/TASK53.md`](../runbook/TASK53.md), "Acceleration structures in `ref_metal` (TASK53, RT1)".

## 2. The three effects, as compute passes

Inputs common to all passes: the G-buffer from section 3, the IAS, a
per-frame uniform block (view, previous view, frame index, `r_rt_*`
cvars, the dlight array copied from `refdef_t.dlights`, `num_dlights` ≤ 32,
each with `origin`, `color`, `intensity`).

How stock lighting is built (`gl1_light.c`): `R_BuildLightMap` sums the
static `samples` per lightstyle scaled by `lightstyles[style].rgb` (0 to
2), then `R_AddDynamicLights` adds every dlight whose plane distance is
inside `intensity` (the dlight's `intensity` **is** its radius; falloff is
linear, `add = intensity - dist`). That additive dlight term is what RT
shadows must replace, or the same light is counted twice.

| Pass | Rays per pixel | Output | Composes as |
|---|---|---|---|
| **Dlight shadows** | 1 per dlight that reaches the pixel (max `num_dlights`, typical 1 to 4; budget cap 4, nearest by `intensity - dist`) | per-dlight visibility, packed 8 bits each | `ref_metal` skips `R_AddDynamicLights` when `r_rt_shadows 1` and applies `visibility × (intensity - dist) × color` in the lighting shader instead. Static lightmap untouched, so no double count. |
| **Sun / sky shadow** | 1 toward the sun direction; a hit on a `sky` material counts as lit, any other hit as shadowed | 8-bit visibility | A new term, so no double count: `sky_color × r_rt_sunscale × visibility × max(0, N·L)`, default `r_rt_sunscale 0.35`, added only on surfaces whose stylized lightmap is below `r_rt_sunfloor` so bright indoor lightmaps are not blown out. Quake II maps have no sun entity; the direction comes from `r_rt_sun_angles` (pitch yaw, default 55 45) with a committed per-map override table `scripts/rt/sun-angles.txt` keyed by map name, no asset data. Maps whose sky faces subtend under 2 % of the world's face area skip the pass. |
| **Ambient occlusion** | 2 cosine-weighted hemisphere rays, 128-unit range (`r_rt_ao_range`), blue-noise per pixel per frame | 8-bit AO | Multiplies the **lightmap term only** (not dlight, not sun), clamped to `[r_rt_ao_min, 1]` default 0.4, so it darkens creases without turning the stock look grey. |
| **Water reflections** | 1 reflection ray per pixel whose material id is `water`, direction from the stock warp (`r_turbsin` phase as a normal perturbation, amplitude `r_rt_water_normal` default 0.08); hit shaded with lightmap + dlight terms, sky hit returns the sky texel | RGB reflection | Blended over the stock water texture at `r_rt_water_reflect` default 0.5 (the stock `SURF_TRANS` alpha stays). One bounce, no recursion. |

Temporal accumulation: shadows, sun and AO outputs are accumulated into
history buffers reprojected with the motion vectors, exponential blend
`r_rt_temporal` default 0.9, rejected where depth or material id differ
from the reprojected history (disocclusion) or where the dlight set changed
for that pixel. Denoiser: a 5×5 bilateral blur guided by depth and normal,
applied to AO and sun only, strength scaled by the pixel's accumulated
sample count (full blur at 1 sample, none past 16). Reflections get no
history (the warp moves every frame) and a 3×3 depth-guided blur.

Total budget at 1080p with everything on: about 5 rays per pixel
(4 shadow + 1 sun worst case, 2 AO, reflections only on water pixels).
*Assumed* on apple7 compute RT: 8 to 14 ms; TASK25's render-stage path on
apple9 is expected to halve it. RT1 measures.

Measured for the dlight shadows (RT3, TASK55; the card file [`docs/runbook/TASK55.md`](../runbook/TASK55.md), "Ray-traced shadows from dynamic
lights in `ref_metal`"). The pass as built:

- one ray from the eye to find the pixel's surface, cast wherever a light reaches its plane (on
  every lit world pixel of a frame with a moved brush entity, whose surfaces gl1 lights in their
  model's space);
- gl1's falloff on that surface's texture axes at its four luxels, in its model's space;
- up to four any-hit shadow rays; the ray of a light that a model carries (a rocket's light
  inside it, a muzzle flash's) also takes a second query over the carriers' instance mask,
  which passes that model's triangles.

On the M6 (apple9, compute path), seeded `rt-shadows` over `demo1.dm2`, warm frames, three runs
each, without and with entities (`rt-shadows-entities`):

- 1920 x 1080: median 2.55 ms (p10 1.61 to 1.62, p90 3.48 to 3.49); with entities 3.58 ms (p10
  2.01 to 2.03, p90 4.79 to 4.96);
- 5120 x 2880: median 8.53 ms (p10 5.4 to 5.5, p90 11.6 to 11.9); with entities 9.05 ms (p10 6.1
  to 6.3, p90 12.9 to 13.6);
- about 0.86 rays a pixel without entities and 1.09 with them; 23 % of pixels trace, 31 % have a
  light in range;
- the rule that a model does not shadow the light it carries costs 0.02 ms at 1080p without
  entities and 0.20 ms with them, against the pass without it.

That is above the 1.952 ms the closed table gives 4 any-hit rays a pixel on every pixel. The
surface ray (closest hit), the 32-light loops and the term's writes cost more than the shadow
rays they serve. The surface ray is the first to cut: it could be traced only where a light is in
range, or the raster could store the surface. `timedemo` reads the display's 60 fps at 1080p with
the effect on and off.

TASK131 makes the raster store the surface. While the shadows trace, the G-buffer has a surface
attachment (section 3). Each world pixel's surface comes from it, through the map's
vertex-to-surface table. A brush entity's model space comes from the frame's entity table. The
lights are tested against the surface's own plane in that space before the falloff. The eye ray
is left only where the attachment names no surface: a dynamic world draw (the flowing batches,
material 1) and a brush model the raster cannot name. Measured on the M6 the same way, against
the pass on `main` before it (`718e59d4`). The runs were three of each build, alternated, on the
same warm frames ([TASK131](../runbook/TASK131.md)):

- 1920 x 1080: median 1.66 ms (p10 1.35 to 1.37, p90 2.07 to 2.14), from 2.58 ms; with entities
  1.73 ms (p10 1.35 to 1.37, p90 2.97 to 3.34), from 3.23 ms;
- 5120 x 2880: median 7.30 ms (p10 4.6 to 5.3, p90 8.8 to 9.1), from 8.81 ms; with entities
  7.28 ms (p10 4.2 to 4.4, p90 10.1 to 10.5), from 9.57 ms;
- about 0.25 rays a pixel, down from 0.86 and 1.09;
- the attachment takes 4 bytes a pixel while the shadows trace: 8.5 MB allocated at 1080p and
  60.0 MB at 5120 x 2880.

At 1080p the median is now under the 1.952 ms of 4 rays on every pixel; the p90, the frames lit
over most of the picture, is not. What is left is mostly the pass's floor. On a frame with no
light in range it costs 1.3 ms at 1080p and about 6 ms at 5120 x 2880, before and after alike.
There the kernel still reads the G-buffer, tests the lights and writes its two outputs at every
pixel; which of those costs most is not measured. That floor, not the rays, is the next thing to
cut at the native size.

Measured for the ambient occlusion (RT5, TASK57; `docs/runbook/TASK57.md`): two closest-hit rays
a traced world pixel (about 99 % of the pixels of `demo1.dm2`), 128 units long, passing sky, water
and translucent triangles. They cannot be any-hit rays: the falloff needs the nearest occluder's
distance (`hit.distance`), which an intersection accepted first does not give. On the M6, seeded `rt-ao` over `demo1.dm2`, warm frames, three runs
each, without and with entities (`rt-ao-entities`):

- 1920 x 1080: median 3.67 ms (p10 3.10 to 3.17, p90 4.60 to 4.72); with entities 4.10 ms (p10
  3.07 to 3.22, p90 4.80 to 5.14);
- 5120 x 2880: median 10.96 ms (p10 9.4 to 9.5, p90 11.7 to 11.9); with entities 10.97 ms (p10
  9.35 to 9.37, p90 11.8 to 11.9).

The AO has no time budget of its own (section 8's verdicts: part of the total). Against the
total above (about 5 rays a pixel with everything on, assumed 8 to 14 ms on apple7 and about
half that on apple9), the AO's two rays alone take 3.7 to 4.1 ms on apple9 at 1080p, 92 % of
the halved range's 4 ms and 52 % of its 7 ms, and at 5120 x 2880 two thirds of a 60 Hz frame,
so on that output the effect needs the internal resolution of the next subsection. Against the
AO's share of the total by section 8's proportions (1.75 of 5.14 ms, so 1.4 to 2.4 ms of 4 to 7)
the 1080p median is over by 1.3 to 2.3 ms. Where it goes: 5120 x 2880 has 7.1 times the pixels
and takes 3.0 times the time, so at most 1.54 ms of the 1080p 3.67 ms grows with the pixels (the
rays, the noise, the light's copy) and at least 2.1 ms does not; within a 1080p run a frame's
time follows the rays that meet an occluder (about 0.9 ms of it). The split between the rays,
the noise and the copy was not measured (runbook, rework 1). `timedemo` reads the display's 60
fps at 1080p with the effect on and off.

TASK157 profiled the pass and cut it ([TASK157](../runbook/TASK157.md)). Most of the 1080p
time TASK57 measured is the GPU's clock, not the work: the cost scenario holds the frame to the
display's 60 Hz, the GPU idles between frames and runs the pass at a low level. With the frame
rate not held (`r_vsync 0`, the level section 8's tables are measured at) the same pass takes
1.11 ms at 1080p, 3.3 times less, the factor section 8 found between a process's start level and
its later one. Where the time goes, at 1080p with the rate held (not held in brackets): the
128-unit traversal 1.36 ms (0.43), measured as the pass with 1-unit rays; the per-pixel floor at
the render size (the G-buffer reads, the noise, the light's copy, the rays' launches) 2.26 ms
(0.68); the entities 0.08 ms (0.02); no denoise yet. The cuts, each a cvar
(`doc/040_cvarlist.md`) and shown in the video menu: `r_rt_ao_scale` traces the rays at a
reduced size, one pixel of each 2 x 2 or 3 x 3 block, and gives each world pixel the bilinear
mean of the four traced pixels about it that lie on its own surface (the normal within about 26
degrees, the point within 2 units plus 0.4 % of its distance of the pixel's plane); its default
is 0.5. `r_rt_ao_rays` 1 traces one ray a pixel; `r_rt_ao_range` is kept at 128. Scale 1 is
TASK57's pass and look, frame for frame. On the M6 at the defaults, three runs a case:

- 1920 x 1080: 2.10 ms held (from 3.62), inside the AO's share of 1.4 to 2.4 ms above; 0.63 ms
  not held (from 1.11), under the share and under section 8's 1.75 ms for two rays on every
  pixel. `r_rt_ao_scale 1/3` takes 1.62 ms (0.48), one ray at scale 1 2.24 ms (0.67);
- 5120 x 2880 with MetalFX (the default 1080-line input): 3.19 ms held (from 3.77), 0.69 ms not
  held (from 1.34), against 2.13 ms for the AO at 1080 traced rows in the primary cell (the
  options' tables below);
- 5120 x 2880 native: 8.26 ms held (from 9.79), 4.62 ms not held (from 7.84).

The frame-level floor check passes on all 247 frames of the AO records, effect off is main's
frames byte for byte, and against the current AO the new default changes the darkened pixels
by about 6 levels on average (8 % of them by more than 8, 0.2 % by more than 32).

### Internal resolution and upscaling with MetalFX (TASK105)

This subsection describes option A (TASK105): with a ray-traced effect
on, the 3D frame is rendered and traced at an internal resolution and
upscaled to the output size with `MTLFXTemporalScaler`. Tracing at the
output size does not fit the primary cell of the target matrix. On the
M6 at its native 5120 x 2880 pixels, the ray passes and the per-frame
items cost 36.8 ms against the 16.7 ms frame of its 60 Hz display
(section 8, verdicts table, row RT9 (1)). Tracing at 1920 x 1080 and
upscaling to 5120 x 2880 costs 7.3 ms as a sum and 9.3 ms as one
measured frame (row RT9 (2); the scaler is 1.822 ms in frame, section 8,
table "MetalFX upscaling to the native size"). A is one of the options
the next subsection ("Targets per GPU family and display, and the
options") costs over the whole matrix, beside option E, which RT13
builds so that both can be measured. The decision is INFORM12 (#262),
open: the owner put it on hold on 2026-11-14 until image-quality data
exists.

Every measured cost in this subsection is quoted from section 8: the M6,
the compute-dispatch ray queries, the synthetic closed scene, synthetic scaler
inputs, the later level of a process. Section 8 did not measure the
render-stage path, an apple7 device, the G-buffer raster, shading, the
temporal accumulation and denoiser above, or image quality. Where this
design needs one of those, it names the card that measures it.

**What runs at the internal resolution.** The whole 3D frame of the world
refdef: the `refdef_t` without `RDF_NOWORLDMODEL` that `V_RenderView`
(`src/client/cl_view.c`) passes to `R_RenderFrame`. That is the G-buffer
raster (section 3), the ray passes, the temporal accumulation and denoiser,
the lighting resolve and the forward pass for sprites, particles, beams and
the view weapon.

**The output is the world refdef's rectangle, not the window.** The client
sets the refdef's `x`, `y`, `width` and `height` from `viewsize`
(`SCR_CalcVrect`, `src/client/cl_screen.c`): the whole window at
`viewsize 100`, a centred sub-rectangle below it, down to 40 % of the
window's width and height. In this subsection the output size is the size
of that rectangle, and the internal size is a fraction of it. The scaler
writes the rectangle at its place in the frame, either through its
`outputOffsetX` and `outputOffsetY` (declared from macOS 27 in the SDK's
`MTLFXTemporalScaler.h`) or into a texture of the rectangle's size that is
copied there; RT11 picks one. 2D (HUD, console, menus, cinematics) draws
after it at the window size, unscaled. With every `r_rt_*` effect off there
is no internal resolution and no scaler: the frame renders at the refdef's
rectangle, as before, so the gl1 pins are untouched.

**A refdef without a world.** In the client, the refdef with
`RDF_NOWORLDMODEL` is the player-setup menu's model view
(`PlayerConfig_ModelDraw`, `src/client/menu/menu.c`): a 144 x 168
rectangle times the menu scale, `fov_x` 40. With that menu open in game it
is a second `R_RenderFrame` in the same frame, drawn after the world refdef
and after 2D (`SCR_UpdateScreen` calls `M_Draw` after `V_RenderView` and
the 2D draws). It renders at its own rectangle
as with every effect off: no internal resolution, no jitter, no ray pass
and no scaler. It touches neither the scaler's history, RT7's history nor
the last-frame state the cut test compares (below). Only the world refdef
drives them.

**Pass order.** Section 3's order, with the scaler added as step 7:

| Step | Pass | Size |
|---|---|---|
| 1 | IAS rebuild and alias refits | – |
| 2 | G-buffer raster, projection jittered | internal |
| 3 | Ray passes: dlight shadows, sun, AO, reflections | internal |
| 4 | Temporal accumulation and denoise (RT7), as above: shadow, sun and AO history, the 5x5 blur on AO and sun, the 3x3 blur on reflections | internal |
| 5 | Lighting resolve | internal |
| 6 | Forward pass | internal |
| 7 | `MTLFXTemporalScaler`: lit colour, depth and motion vectors in, colour out | internal to output |
| 8 | 2D | window |

**The scaler and RT7.** The scaler keeps its own history at the output
size. It accumulates the lit colour over frames and is designed to use the
jitter to recover detail finer than an internal pixel. It does not replace
RT7. RT7 accumulates and denoises each effect before the resolve, where the
effects are still separate: AO multiplies the lightmap term only and is clamped to
`r_rt_ao_min`, and the dlight visibility replaces the term of
`R_AddDynamicLights`. The scaler sees only their sum. So RT7 removes the
noise per effect at the internal resolution, and the scaler upscales a
colour that is already denoised. The two histories are separate. RT7's is
at the internal resolution, reprojected with the motion vectors and
rejected on depth, material id and dlight-set changes, as above. The
scaler's is at the output size and kept by MetalFX. Both reset on the same
events (below). RT7's passes cost less at the internal resolution than at
the output size; section 8 traces only, so their cost is not measured.
RT10's per-pass table records it.

The G-buffer of each frame is offset by the jitter, and RT7 reprojects
with the unjittered motion vectors, so its history is sampled up to half an
internal pixel off. Whether that softens AO and shadows visibly is for the
image-quality card (RT12), not decided here.

**What the scaler needs from the renderer.**

| Input | Source | Note |
|---|---|---|
| Colour | the forward pass's output at the internal size | `RGBA16Float`, the format section 8 measured |
| Depth | section 3's `depth32float` attachment | `Depth32Float` measured; the scaler's reversed-depth setting follows the renderer's depth convention |
| Motion vectors | section 3's `rg16float` attachment, from the unjittered current and previous view, each with its own projection, so a change of the field of view is carried by them | `RG16Float` measured. The scaler's motion-vector scale matches section 3's units, which RT2 fixes. RT7 already needs them, so the scaler adds no attachment. Their raster cost is part of RT2's G-buffer and not measured; RT10's per-pass table records it |
| Jitter | the Halton (2, 3) sequence minus 0.5, in internal pixels, added to the projection and passed to the scaler each frame | the sequence section 8 drove the scaler with |
| Exposure | auto exposure off, fixed exposure | the resolve writes the stock display-referred product, not scene light; section 8 also ran with auto exposure off |
| Reset | set for one frame on a map load, a `vid_restart`, a camera cut, a teleport, a change of `r_rt_scale` or of the world refdef's rectangle, and the first world refdef with an effect on after one with every effect off | the same frame resets RT7's history |

The renderer detects these events from what it already receives; the
renderer API (`ref.h`, `API_VERSION`) does not change. A map load is
`BeginRegistration`. A `vid_restart`, a change of `r_rt_scale`, a
change of the world refdef's rectangle and an effect turned back on
after a world refdef with every effect off create the scaler anew (with
every effect off nothing writes either history, so what they hold is
stale); the rectangle changes with `viewsize` and with the window size. A camera cut
or a teleport is a world refdef whose view origin moved further than a
threshold from the last world refdef's, or the first world refdef after a
frame that drew none (a cinematic, the console with no map). The last world
refdef is the state the cut test compares; a refdef with `RDF_NOWORLDMODEL`
does not replace it. RT11 sets the threshold and records it here. A change
of the field of view is not a cut: the client interpolates `fov` across a
server frame (`cl_entities.c`), so a zoom changes it a little on every
rendered frame, and the motion vectors already carry it. While a loading
plaque is up the renderer receives no frame at all; the map load that
normally ends it is a reset event of its own.

**The scale: `r_rt_scale`.** Named and described here only; no cvar is
added to the code by TASK105. RT11 adds it, with its entry in
`doc/040_cvarlist.md`, which every new cvar needs.

- Archived. Default 0, which picks the family default below.
- Any other value is the internal width and height as a fraction of the
  output's, applied only while a ray-traced effect is on. 1 renders at the
  output size and creates no scaler, and the projection is not jittered.
- The fraction is clamped so that the ratio of output to internal size
  stays inside the input content scale the scaler reports (1.0 to 3.0 on
  the M6, section 8, text before the MetalFX table). The aspect ratio is
  kept.
- A change takes effect on the next frame, through the path a window
  resize takes, and resets both histories.
- It reads 1, with a line in the `vid_restart` output, where the scaler is
  unavailable or the device has no ray tracing (section 4, "neither"; table
  below). Unlike the effect cvars, section 4 does not force it to 0,
  because 0 here means the family default.

**Default internal size per family (option A).** A height in rows, so that the
default follows the output; the fraction is that height over the output's.
A ratio above the scaler's maximum is clamped to it. With the M6's maximum
of 3.0, a default of 720 rows on a 5120 x 2880 output would become 960.
The 720-row default is for apple6 to apple8, whose maximum is not measured;
TASK106 (#249) measures apple7.

| Family | Ray tracing (section 4) | Default internal height | Basis |
|---|---|---|---|
| apple9 and later (M3 and later; the M6 measured) | hardware intersection; render-stage path | 1080 rows | Measured on the M6 at a 5120 x 2880 output on the compute path, the only device and output measured: the 1080p trace with the temporal scaler is 7.3 ms as a sum and 9.3 ms as one measured frame, which leaves 7.4 to 9.3 ms of the 60 Hz frame for raster, shading and RT7 (section 8, verdicts table, row RT9 (2)). An internal 2560 x 1440 leaves 1.7 to 4.8 ms (same row), too little for work that is not measured yet. For other apple9 devices and outputs this is a default to be measured (RT10) |
| apple6 to apple8 (M1 is apple7, M2 apple8) | compute path, Metal's software traverser | 720 rows, *assumed* | Not measured: section 8 is the M6 only. Section 2 assumes 8 to 14 ms for the 1080p trace on apple7, a large part of the 60 Hz frame before the scaler, so the default sits below apple9's. TASK106 (#249) measures apple7, the scalers included; RT10 sets the default from it |
| a device without ray tracing (section 4, "neither"), or one where `MTLFXTemporalScalerDescriptor` `supportsDevice:` is NO | none, or as section 4 | output size; `r_rt_scale` reads 1 | reported in the `vid_restart` output |

These are option A's design defaults, by family only. The target matrix
(next subsection) gives each cell, a family and a display class, a
budget that does depend on the refresh rate: at 120 Hz (an 8.3 ms frame)
the 1080p path on the M6 is met by the 7.3 ms sum and not by the 9.3 ms
measured frame (row RT9 (2)). RT10 sets the default per cell by the
default policy of the next subsection, replacing this table; until then
a lower `r_rt_scale` is the player's choice. Section 8's host has a 60 Hz
display, so a 120 Hz class's presented rate is measured only on the T1
host's built-in display, for apple7 (TASK106, #249, and RT10). For
apple9 at the 120 Hz classes RT10 takes GPU times on T2 at those sizes
as windows, and no card measures the presented rate (the next
subsection, unmeasured costs and default policy item 4).

**Other MetalFX scalers.** `MTLFXSpatialScaler` costs 1.668 ms in frame at
1080p input and 1.653 ms at 2560 x 1440, against the temporal scaler's
1.822 and 2.413 ms (section 8, MetalFX table, "In frame, scaler"). It
keeps no history and takes no jitter, so it is designed only to
interpolate the internal pixels. Not used by option A: it saves 0.15 ms at 1080p and 0.76 ms at 2560 x 1440. TASK111 (#265) makes it selectable, and TASK117 (#272) measures it on rasterized frames.

`MTLFXTemporalDenoisedScaler` against the bilateral denoiser above, on
section 8's numbers (M6, 5120 x 2880 output):

| | `MTLFXTemporalScaler` after RT7's accumulation and denoiser (option A) | `MTLFXTemporalDenoisedScaler` in place of both (option B, not a default) |
|---|---|---|
| Scaler in frame (MetalFX table, "In frame, scaler") | 1.822 ms at 1080p input, 2.413 ms at 2560 x 1440 | 12.032 ms at 1080p, 19.497 ms at 2560 x 1440 |
| Trace, per-frame items and scaler (verdicts table, row RT9 (2)) | 7.3 ms sum, 9.3 ms measured (1080p); 11.9 and 15.0 ms (2560 x 1440) | 17.6 ms sum, 18.9 ms measured (1080p); 29.0 and 31.5 ms (2560 x 1440) |
| Left of the 16.7 ms frame for raster, shading and denoise | 7.4 to 9.3 ms (1080p), 1.7 to 4.8 ms (2560 x 1440) | none: over the frame at both inputs before any raster or shading |
| Cost of the denoise it relies on | RT7's accumulation and the 5x5 and 3x3 blurs: not measured, part of the time left | inside the scaler |
| Inputs beyond colour, depth, motion and jitter | none | diffuse albedo, specular albedo, normals, roughness, the world-to-view and view-to-clip matrices (section 8, MetalFX setup) |
| From section 3's G-buffer | depth and motion vectors, already there | also albedo (`rgba8`) and normal (`rgb10a2`), measured only as `RGBA16Float`; Quake II materials have no specular albedo or roughness, so those are constants, as section 8 used |
| What is denoised | each effect, before the resolve | the lit colour, all terms summed |
| What it would replace | – | RT7's accumulation and both blurs, and the temporal scaler |

Outcome for the defaults (TASK105; part of INFORM12, which is open): the
denoised scaler is not a default, and section 2's accumulation and
bilateral denoiser stay; TASK113 (#267) makes it selectable for the
comparison (option B in the next subsection). The deciding reason is
cost: with the trace it is
over the 60 Hz frame at both measured inputs on the M6, before
rasterization or shading (verdicts table, row RT9 (2): "With the denoised
scaler, not met at either input"). Two further reasons hold without a
number. Section 2's denoiser is needed anyway at `r_rt_scale 1` and on a
device without the scaler, so the denoised scaler would be a second
denoising path, not a replacement. And it denoises the summed colour, where
section 2 denoises each effect before it is composed. It was not measured
below a 1080p input or on apple7; a measurement that puts it inside the
frame, with an image-quality result that favours it, would reopen this.

**Frame interpolator.** `MTLFXFrameInterpolator` reports support on the M6
and was not timed (section 8, text before the MetalFX table). It is
option D of the next subsection: not a way to hold a cell's refresh,
not a default and not an RT row; TASK111 (#265) adds it as a development
option, off by default, and measures the delay it adds. It builds a
frame between two rendered frames, so
each rendered frame reaches the screen later than without it, and the frames
it adds do not respond to input. In a game that is added latency.

**RT7 and RT9.** RT7's acceptance does not change. It lands before RT11,
so its 60 fps at 1080p on apple7 is measured at a 1920 x 1080 output,
which traces 1080 rows with no scaler. In the target matrix that is one
cell: apple7, 1920 x 1080 at 60 Hz, every effect on, option C. RT9's
target is the T2 host's cell (apple9, 5120 x 2880 at 60 Hz) in
configuration A (the default `r_rt_scale`, every effect scale at 1) and
in configuration E (every effect scale at 1/2, `r_rt_scale 1`), as the
next subsection names them, held with the default policy's headroom,
not tracing at native pixels (section 6).

**Not measured here, and where it is measured.** Image quality of the
upscaled frame on real frames is RT12, which measures A, E and the
hybrids by the image-quality program of the next subsection; no quality
is claimed before it.
The cases it names: particles, sprites and beams, which write no motion
vectors of their own; water reflections, which RT7 keeps without history
but the scaler accumulates; lightstyle changes and muzzle flashes, which
change the lighting from one frame to the next; the view weapon; the jitter in RT7's
history. A lead for RT11 and RT12, not a decision: the scaler takes a
reactive mask (`reactiveMaskTexture`, with `reactiveMaskTextureEnabled` on
its descriptor, in the SDK's `MTLFXTemporalScaler.h`), a per-pixel value
from 0 to 1 that tells it to favour the current frame over its history,
meant for alpha-blended or fast-moving content whose motion vectors are
inaccurate. The forward pass could write it for particles, sprites, beams
and water; RT12's frames show whether that is needed. The cost of the
motion vectors, the G-buffer raster, shading and RT7 is RT10's per-pass
table. The scaler's cost on apple7 is TASK106
(#249). The first frames of a process run at the start level (section 8,
"Levels at the start of a process"), so a frame-time test excludes them.

### Targets per GPU family and display, and the options (TASK116)

RT9's single criterion, every effect on at the native output at the
display's refresh, is replaced by a matrix. A cell is a GPU family, a
display class and an effect configuration; each cell has a frame budget
and the cost of each option in it. INFORM12 (#262) is on hold by the
owner's direction (2026-11-14) until image-quality data exists. The
subsection above describes option A as RT11 would build it; this one
costs every option (A to E, the hybrids and dynamic resolution) in every
cell the measurements allow, defines the image-quality program, and
keeps the decision open (the end of this subsection).

**Sources.** Every cost below is quoted from section 8 (the M6, the
compute-dispatch ray queries, the synthetic closed scene, the later level
of a process, trace only) with its table, row and column, or computed
from those numbers by model R below, with the computation shown and the
number marked as modelled. The TASK97 card file
(`docs/runbook/TASK97.md`, "Frame rates on a physical GPU
(TASK97)") gives no cost used here: its numbers are `timedemo` frame
rates, not GPU times; none is at a native size; the three renderers that
present through a `CAMetalLayer` run at 60.0 to 60.1 fps at 1920x1080
(its Finding 3, diagnosed in the FIX52 section) and at a per-frame fixed
cost of 3 to 4 ms up to 1600x900 (Finding 1); and the `ref_gl1` rate may
be a CPU-submit rate (Finding 2). TASK108 (#259), which takes native-size
frame rates of today's renderers, has not merged. No image quality is
measured anywhere yet, so nothing below ranks the options by how they
look.

**Display classes.** Each is a pixel size and a refresh rate, not a Mac
model.

| Class | Pixels (millions) | Frame | Policy target (90 %, below) | Host in the plan |
|---|---|---|---|---|
| **5120 x 2880 at 60 Hz, the primary cell** | 14,745,600 (14.75) | 16.7 ms | 15.0 ms | the T2 host: the M6 drives this display at 60 Hz (section 8, "Display") |
| 1920 x 1080 at 60 Hz | 2,073,600 (2.07) | 16.7 ms | 15.0 ms | none as a display |
| 2560 x 1440 at 60 Hz | 3,686,400 (3.69) | 16.7 ms | 15.0 ms | none as a display |
| 3840 x 2160 at 60 Hz | 8,294,400 (8.29) | 16.7 ms | 15.0 ms | none as a display; T2 as a window (RT10) |
| 3024 x 1964 at 120 Hz | 5,939,136 (5.94) | 8.3 ms | 7.5 ms | the owner's apple7 Mac has a built-in 120 Hz display of one of these two classes; TASK106 (#249) records which. No apple9 host has a 120 Hz display: T2 makes the size as a window, for GPU time only (RT10) |
| 3456 x 2234 at 120 Hz | 7,720,704 (7.72) | 8.3 ms | 7.5 ms | as above |
| 6016 x 3384 at 60 Hz | 20,358,144 (20.36) | 16.7 ms | 15.0 ms | none |

GPU time does not depend on the display (section 8, verdicts table, row
RT9 (1)), so a window or render target of a class's size measures that
class's cost on any host that can make one; the budget is the class's.

**GPU families.** apple7 (M1) and apple8 (M2): the compute path on
Metal's software traverser (section 4). apple9 and later (M3 on):
hardware intersection and the render-stage path. Only apple9 is
measured, on the M6 (section 8).

**Effect configurations.** Each of the four passes of this section's
table alone (dlight shadows at the cap of 4 rays per pixel, sun, AO,
water reflections), and every effect on.

**Model R: ray cost by traced pixels, and its check.** Before it is
used, the per-pixel scaling is checked against section 8's closed table,
1920 x 1080 and 2560 x 1440 columns:

| Pass (closed table row) | 1920 x 1080 column, ms | ms per million pixels | 2560 x 1440 column, ms | ms per million pixels | 2560 x 1440 against 1920 x 1080 | 5120 x 2880 against 1920 x 1080 | 640 x 480 against 1920 x 1080 |
|---|---|---|---|---|---|---|---|
| Dlight shadows: any-hit shadow, every pixel casting, 4 rpp | 1.952 | 0.941 | 3.459 | 0.938 | -0.3 % | -0.2 % | +3.4 % |
| Sun: any-hit shadow, every pixel casting, 1 rpp | 0.559 | 0.270 | 0.986 | 0.267 | -0.8 % | -1.5 % | +12.3 % |
| AO: closest hit, 2 rpp (section 8's stand-in for the hemisphere rays) | 1.750 | 0.844 | 3.104 | 0.842 | -0.2 % | -0.3 % | +3.8 % |
| Reflections: closest hit, 1 rpp on every pixel (an upper bound for water pixels) | 0.880 | 0.424 | 1.557 | 0.422 | -0.5 % | -0.7 % | +7.4 % |
| Every effect on (sum of the four) | 5.141 | 2.479 | 9.106 | 2.470 | -0.4 % | -0.5 % | +5.2 % |

Each ms-per-million value is the column's time over its pixels (1.952 /
2.0736 = 0.941; 3.459 / 3.6864 = 0.938), and each percentage is a
column's value per million pixels against the 1920 x 1080 one. Result of
the check: 2560 x 1440 has 1.778 times the pixels of 1920 x 1080 and its
passes cost 1.764 to 1.774 times as much, so per million pixels the two
rows agree within 0.8 %; the 5120 x 2880 column agrees within 1.5 %
(section 8 also finds native pixels "linear in pixels"). Linear scaling
holds from 2.07 to 14.75 million pixels. Below that it holds less well:
the 640 x 480 column (0.31 million) costs 3 to 12 % more per pixel. So
model R is: rays = k x traced pixels, with k the 1920 x 1080 column's
value per million pixels above (the largest of the three rows, so a
modelled cost above 2.07 million pixels errs high), plus the per-frame
items. The per-frame items are the instance rebuild and the 128 x 500
refit, 0.174 + 0.203 = 0.377 ms (acceleration-structure table, "later"
rows); they do not depend on pixels and are added once to every cell.
Model R holds only for what section 8 measured: apple9 on the M6, the
compute path, the closed scene with every pixel casting, the later level
of a process. The first frames of a process run about 3.5 times slower
(section 8, "Levels at the start of a process").

Marks in the tables: **M** measured (the closed table's column of that
traced size, plus the measured 0.377 ms); **+** modelled below 2.07
million traced pixels, where the 640 x 480 column puts the cost up to
5 % higher with every effect on (12 % for the sun alone); **r**
modelled from 2.07 to 14.75 million traced pixels, the range the check
covers, where model R errs high (by at most 1.5 % on the check); **x**
modelled outside the sizes measured, so extrapolated: below 0.31
million, the smallest, or above 14.75 million, the largest (only C on
6016 x 3384, 20.36 million). Every modelled cost carries its mark.

**The options as costed.**

- **C**: every effect traced at the output; no scaler, no upsample. C is
  A at `r_rt_scale 1` and E with every effect at scale 1: the three are
  one frame at scale 1.
- **A** (TASK105): the whole 3D frame at an internal size, upscaled by
  `MTLFXTemporalScaler`, RT7 at the internal size (the subsection above).
  Costed at the family default of the table above, 1080 rows on apple9,
  clamped to the scaler's maximum ratio of 3.0 (section 8, text before
  the MetalFX table), which gives 1128 rows on 6016 x 3384; and at 1440
  rows in the primary cell.
- **B**: as A, with `MTLFXTemporalDenoisedScaler` in place of RT7 and the
  scaler. Measured in the primary cell only.
- **D**: `MTLFXFrameInterpolator` on top of another option. Not timed
  (section 8, text before the MetalFX table). The default policy counts
  its rendered frames, not the generated ones (item 1).
- **E**: the 3D frame rasterized at the output; each effect traced at its
  own fraction of the output's width and height, accumulated and
  denoised by RT7 at that size, and upsampled to the output by a joint
  bilateral upsample guided by the output's depth, normal and material
  id; no whole-frame scaler. Costed with every effect at 1/2 (a design
  point, not a default: every effect scale's design default is 1, which
  leaves E off, "The effect scales" below) and, in the primary cell, at
  1/3. A mixed
  configuration costs the sum of its effects' rays, each at its own
  scale, plus the per-frame items once.
- **H** (a hybrid of A and E): the 3D frame at 1/2 of the output's width
  and height, upscaled by the temporal scaler (a ratio of 2.0, inside its
  1.0 to 3.0), with each effect traced at 1/2 of that internal size and
  upsampled to it as in E. It traces (1/2 x 1/2) squared, 1/16, of the
  output's pixels.
- **Dynamic resolution**: a controller that sets A's, E's or H's scale
  each frame (below). At a given scale it costs that scale's row plus the
  controller, which is not measured.

**Configurations a test names.** A row, a pin or a measurement names its
configuration by the two scales, not by "the default", so that the
hybrid is never left implicit:

- **A**: `r_rt_scale` at its default, 0 (the family default, 1080 rows
  on apple9 until RT10), and every effect scale at 1.
- **E**: every effect scale at 1/2, the design point, and `r_rt_scale 1`
  (no scaler).
- **H**: `r_rt_scale 0.5` and every effect scale at 1/2.
- **C**: `r_rt_scale 1` and every effect scale at 1.

Before RT13 there are no effect scales, and A is the default
`r_rt_scale` alone. With both design defaults (`r_rt_scale` 0 and every
effect scale 1, "The effect scales" below) a frame with an effect on is
configuration A: the scaler runs wherever the default internal size is
below the output, and no effect is traced below the internal size, so no
hybrid is on by default.

On model R the ray cost depends only on the traced pixel count, so A, E
and H tracing the same count cost the same in rays. They differ in the
scaler, the upsample and the size the 3D frame is rasterized at, and of
those only the scaler is measured, at a 5120 x 2880 output.

**The primary cell: apple9, 5120 x 2880 at 60 Hz (the M6).** Every effect
on, every cost item of each option. "Not measured" names the card that
measures the item (list below the matrix). The measured frame of A and B
("as one frame" below) is section 8's one command-buffer sequence
(MetalFX table, column "Frame, trace start to scaler end"; verdicts
table, row RT9 (2)), which also holds a primary pass and a gap before
the scaler. At 1080 rows: the 0.878 ms primary pass (closed table, row
"Primary", column 1920 x 1080) and a gap of 1.00 ms for A (MetalFX
table, row "Temporal, 1920 x 1080", column "Frame, trace start to
scaler end") or 0.46 ms for B (row "Temporal denoised, 1920 x 1080",
same column). At 1440 rows: the 1.550 ms primary pass (closed table,
row "Primary", column 2560 x 1440) and a gap of 1.50 ms for A (MetalFX
table, row "Temporal, 2560 x 1440", same column) or 0.84 ms for B (row
"Temporal denoised, 2560 x 1440", same column).

| Option | 3D raster at | Traced at | Rays | Per-frame items | Upscale or upsample | Raster, G-buffer, RT7, resolve, forward | Known total | Left of the 16.7 ms frame | Left of the 15.0 ms target |
|---|---|---|---|---|---|---|---|---|---|
| C | 5120 x 2880 | 5120 x 2880 | 36.39 M | 0.38 M | none | not measured, at 5120 x 2880 | 36.77 M (row RT9 (1): 36.8) | over by 20.1 | over by 21.8 |
| A, 1080 rows | 1920 x 1080 | 1920 x 1080 | 5.14 M | 0.38 M | temporal scaler 1.82 M | not measured, at 1920 x 1080 | 7.34 M as a sum, 9.25 M as one frame (8.876 + 0.377) | 9.3 to 7.4 | 7.7 to 5.7 |
| A, 1440 rows | 2560 x 1440 | 2560 x 1440 | 9.11 M | 0.38 M | temporal scaler 2.41 M | not measured, at 2560 x 1440 | 11.90 M as a sum, 15.02 M as one frame (14.638 + 0.377) | 4.8 to 1.7 | 3.1 as a sum; over by 0.02 as one frame |
| B, 1080 rows | 1920 x 1080 | 1920 x 1080 | 5.14 M | 0.38 M | denoised scaler 12.03 M, in place of RT7 and the scaler | not measured, at 1920 x 1080 (no RT7) | 17.55 M as a sum, 18.93 M as one frame (18.553 + 0.377) | over by 0.9 to 2.3 | over by 2.6 to 3.9 |
| B, 1440 rows | 2560 x 1440 | 2560 x 1440 | 9.11 M | 0.38 M | denoised scaler 19.50 M | not measured | 28.98 M as a sum, 31.45 M as one frame (31.077 + 0.377) | over by 12.3 to 14.8 | over by 14.0 to 16.5 |
| D | as the option it is added to | | | | interpolator not timed | | not measured | | |
| E, every effect at 1/2 | 5120 x 2880 | 2560 x 1440 | 9.11 M | 0.38 M | joint bilateral upsample, not measured (RT13) | not measured, raster at 5120 x 2880, RT7 at 2560 x 1440 | 9.48 M | 7.2 | 5.5 |
| E, every effect at 1/3 | 5120 x 2880 | 1707 x 960 (1.64 million) | 2.479 x 1.6387 = 4.06 + | 0.38 M | as above | as above, RT7 at 1707 x 960 | 4.44 + | 12.2 + | 10.6 + |
| H | 2560 x 1440 | 1280 x 720 (0.92 million) | 2.479 x 0.9216 = 2.28 + | 0.38 M | temporal scaler 2.41 M (from 2560 x 1440) and the upsample to 2560 x 1440, not measured | not measured, at 2560 x 1440, RT7 at 1280 x 720 | 5.07 + | 11.6 + | 9.9 + |

The sums: A 5.14 + 0.38 + 1.82; A at 1440 rows 9.11 + 0.38 + 2.41; B
5.14 + 0.38 + 12.03 and 9.11 + 0.38 + 19.50; E 9.11 + 0.38; H 2.28 + 0.38
+ 2.41. "Left" is 16.67 or 15.0 ms minus the known total, as a sum and as
one frame where both exist. Every "left" is what remains for the items
not measured, so no option is shown to hold this cell. A at 1080 rows
leaves 7.4 to 9.3 ms (measured) and E at 1/2 7.2 ms (measured rays);
E at 1/3 and H leave more, 12.2 and 11.6 ms (modelled), because they
trace fewer pixels. What the "left" must pay for differs: E rasterizes,
resolves and draws the forward pass at 7.1 times the pixels of A at 1080
rows (14,745,600 / 2,073,600) and adds an upsample per effect; H
rasterizes at 2560 x 1440 and adds an upsample; A's scaler is already in
its total. C is over the frame before any raster; B is over it before
any raster or resolve (row RT9 (2): "With the denoised scaler, not met
at either input").

Each effect alone in the primary cell, rays plus the per-frame items (ms):

| Option | Traced at | Dlight shadows | Sun | AO | Reflections (upper bound) | Plus |
|---|---|---|---|---|---|---|
| C | 5120 x 2880 | 14.23 M | 4.29 M | 12.78 M | 6.59 M | – |
| A, 1080 rows | 1920 x 1080 | 2.33 M | 0.94 M | 2.13 M | 1.26 M | the scaler, 1.82 M |
| E at 1/2 | 2560 x 1440 | 3.84 M | 1.36 M | 3.48 M | 1.93 M | the upsample, not measured |
| E at 1/3 | 1707 x 960 | 1.92 + | 0.82 + | 1.76 + | 1.07 + | the upsample, not measured |
| H | 1280 x 720 | 1.24 + | 0.63 + | 1.15 + | 0.77 + | the scaler, 2.41 M, and the upsample |

Each measured cell is the pass's closed-table value at that column plus
0.377 (13.855 + 0.377 = 14.23); each modelled cell is k x traced
millions + 0.377 (0.941 x 1.6387 + 0.377 = 1.92).

**The matrix, apple9, the other display classes.** Rays plus per-frame
items, ms, marks as above; the options add what the primary cell lists.
On 1920 x 1080, A at 1080 rows is the output size, so it is C, with no
scaler.

| Class, frame | Option | Traced at (millions) | Dlight shadows | Sun | AO | Reflections | Every effect |
|---|---|---|---|---|---|---|---|
| 1920 x 1080, 60 Hz, 16.7 ms | C and A | 1920 x 1080 (2.07) | 2.33 M | 0.94 M | 2.13 M | 1.26 M | 5.52 M |
| | E at 1/2 | 960 x 540 (0.52) | 0.86 + | 0.52 + | 0.81 + | 0.60 + | 1.66 + |
| | H | 480 x 270 (0.13) | 0.50 x | 0.41 x | 0.49 x | 0.43 x | 0.70 x |
| 2560 x 1440, 60 Hz, 16.7 ms | C | 2560 x 1440 (3.69) | 3.84 M | 1.36 M | 3.48 M | 1.93 M | 9.48 M |
| | A, 1080 rows | 1920 x 1080 (2.07) | 2.33 M | 0.94 M | 2.13 M | 1.26 M | 5.52 M |
| | E at 1/2 | 1280 x 720 (0.92) | 1.24 + | 0.63 + | 1.15 + | 0.77 + | 2.66 + |
| | H | 640 x 360 (0.23) | 0.59 x | 0.44 x | 0.57 x | 0.47 x | 0.95 x |
| 3840 x 2160, 60 Hz, 16.7 ms | C | 3840 x 2160 (8.29) | 8.19 r | 2.61 r | 7.38 r | 3.90 r | 20.94 r |
| | A, 1080 rows | 1920 x 1080 (2.07) | 2.33 M | 0.94 M | 2.13 M | 1.26 M | 5.52 M |
| | E at 1/2 | 1920 x 1080 (2.07) | 2.33 M | 0.94 M | 2.13 M | 1.26 M | 5.52 M |
| | H | 960 x 540 (0.52) | 0.86 + | 0.52 + | 0.81 + | 0.60 + | 1.66 + |
| 3024 x 1964, 120 Hz, 8.3 ms | C | 3024 x 1964 (5.94) | 5.97 r | 1.98 r | 5.39 r | 2.90 r | 15.10 r |
| | A, 1080 rows | 1663 x 1080 (1.80) | 2.07 + | 0.86 + | 1.89 + | 1.14 + | 4.83 + |
| | E at 1/2 | 1512 x 982 (1.48) | 1.77 + | 0.78 + | 1.63 + | 1.01 + | 4.06 + |
| | H | 756 x 491 (0.37) | 0.73 + | 0.48 + | 0.69 + | 0.53 + | 1.30 + |
| 3456 x 2234, 120 Hz, 8.3 ms | C | 3456 x 2234 (7.72) | 7.64 r | 2.46 r | 6.89 r | 3.65 r | 19.52 r |
| | A, 1080 rows | 1671 x 1080 (1.80) | 2.08 + | 0.86 + | 1.90 + | 1.14 + | 4.85 + |
| | E at 1/2 | 1728 x 1117 (1.93) | 2.19 + | 0.90 + | 2.01 + | 1.20 + | 5.16 + |
| | H | 864 x 558 (0.48) | 0.83 + | 0.51 + | 0.78 + | 0.58 + | 1.57 + |
| 6016 x 3384, 60 Hz, 16.7 ms | C | 6016 x 3384 (20.36) | 19.54 x | 5.87 x | 17.56 x | 9.02 x | 50.85 x |
| | A, 1128 rows (clamped) | 2005 x 1128 (2.26) | 2.51 r | 0.99 r | 2.29 r | 1.34 r | 5.98 r |
| | E at 1/2 | 3008 x 1692 (5.09) | 5.17 r | 1.75 r | 4.67 r | 2.54 r | 13.00 r |
| | H | 1504 x 846 (1.27) | 1.57 + | 0.72 + | 1.45 + | 0.92 + | 3.53 + |

What is left, every effect on (ms): the frame minus the "every effect"
column, before every unmeasured item; for A and H also before the
scaler, which is measured only at a 5120 x 2880 output (1.82 ms from
1920 x 1080 and 2.41 ms from 2560 x 1440 there).

| Class | C | A | E at 1/2 | H |
|---|---|---|---|---|
| 1920 x 1080, 60 Hz | 11.1 (M) | as C | 15.0 + | 16.0 x, before the scaler |
| 2560 x 1440, 60 Hz | 7.2 (M) | 11.1 (M), before the scaler | 14.0 + | 15.7 x, before the scaler |
| 3840 x 2160, 60 Hz | over by 4.3 r | 11.1 (M), before the scaler | 11.1 (M) | 15.0 +, before the scaler |
| 3024 x 1964, 120 Hz | over by 6.8 r | 3.5 +, before the scaler | 4.3 + | 7.0 +, before the scaler |
| 3456 x 2234, 120 Hz | over by 11.2 r | 3.5 +, before the scaler | 3.2 + | 6.8 +, before the scaler |
| 6016 x 3384, 60 Hz | over by 34.2 x | 10.7 r, before the scaler | 3.7 r | 13.1 +, before the scaler |

What the matrix shows, and no more: C, tracing at the output, leaves
time on 1920 x 1080 and 2560 x 1440 at 60 Hz (measured) and is over the
frame on every larger class and on both 120 Hz classes (modelled). On
the 120 Hz classes A, E and H leave 3.2 to 7.0 ms (modelled) before any
raster, and A and H also before the scaler, so they are the tightest cells
besides the primary one. On 6016 x 3384 E at 1/2 leaves 3.7 ms
(modelled) for a raster of 20.36 million pixels, less than any option
but C leaves there; a smaller effect scale leaves more.

**apple7 and apple8.** No cell is costed: nothing below apple9 is
measured. This section's "8 to 14 ms" at 1080p on apple7 is an
assumption, not a measurement. TASK106 (#249) measures apple7 at 1920 x
1080 and at its display's native size (one of the 120 Hz classes),
scalers included; model R is used for apple7 only after the same check
on TASK106's columns, with apple7's own k. No card measures apple8 and
no apple8 device is named in the plan (a finding for the orchestrator).
The traced pixel counts are geometry and hold for every family: E and H
as in the apple9 table; A at the 720 rows *assumed* for apple6 to apple8
(table above) traces 1280 x 720 (0.92 million) on 1920 x 1080, 2560 x
1440 and 3840 x 2160, and 1109 x 720 (0.80 million) on 3024 x 1964. The
ratio of output to internal height is 1.5, 2.0 and 2.73 on 1920 x 1080,
2560 x 1440 and 3024 x 1964 (1080 / 720, 1440 / 720, 1964 / 720), and
exactly 3.0 on 3840 x 2160 (2160 / 720): the top of the 1.0 to 3.0 the
M6 reports (section 8, text before the MetalFX table). On 5120 x 2880,
3456 x 2234 and 6016 x 3384 it exceeds 3.0 (4.0, 3.10 and 4.70).
apple7's range is not measured (TASK106), so the classes at risk are
3840 x 2160, 5120 x 2880, 3456 x 2234 and 6016 x 3384: if apple7's
maximum is below a class's ratio, the clamp raises the internal height
there and the traced pixels with it.

**Unmeasured costs and the card that measures each.**

| Item | Options it costs | Measured by |
|---|---|---|
| The G-buffer raster with its motion vectors, the lighting resolve and the forward pass at the internal size | A, B, H | RT10's per-pass table (TASK62, #105); RT2 (TASK54, #96) builds the G-buffer |
| The same at the output size | C, E | RT13 (per-pass time at the T1 and T2 outputs) and RT10's table. Today's renderers at native size: TASK108 (#259), not merged; TASK97 bounds none of it (Findings 1 to 3) |
| The joint bilateral upsample, per effect, at the output size (E) or the internal size (H) | E, H | RT13 |
| RT7's accumulation and the 5x5 and 3x3 blurs, at the internal or effect size | A, C, E, H | RT10's per-pass table |
| The temporal scaler at outputs other than 5120 x 2880 | A, H | RT11 (the T1 and T2 outputs), TASK111 (#265, the scaler's GPU time per mode and scale) |
| The interpolator | D | TASK111 (#265), as a development option |
| Every cost on apple7 | all | TASK106 (#249), then RT10 on the T1 host |
| Every cost on apple8 | all | none |
| apple9 at the 120 Hz classes: the presented rate at 120 Hz (the default policy's `timedemo` condition, item 1) | all | none (no apple9 host with a 120 Hz display; a finding for the orchestrator, as apple8's row is). The GPU time at those two sizes is RT10's, as windows on T2, judged against the cell's own 7.5 ms target (policy item 4) |
| The render-stage path | all | RT9 |
| Hit shading of reflections (section 8 traced only) | all | RT6 (TASK58, #100), RT10 |
| A real map's geometry instead of the soup | all | RT1 (TASK53, #95), RT10 |
| The controller's own cost | dynamic resolution | the card that builds it; none yet (below) |

**Option E, from first principles.** E keeps the stock raster at the
output and lowers only the resolution of the ray-traced terms. Pass order
under E (section 3's steps, renumbered for E):

| Step | Pass | Size |
|---|---|---|
| 1 | IAS rebuild and alias refits | – |
| 2 | G-buffer raster, no jitter | output (the world refdef's rectangle) |
| 3 | Each ray pass at its own scale, each effect pixel taking the G-buffer sample of one output pixel in its block | effect |
| 4 | RT7 per effect: accumulation and denoise | effect |
| 5 | Joint bilateral upsample of each effect's result: for each output pixel the nearest effect samples, weighted by distance and by how closely each sample's depth, normal and material id match the output pixel's; a pixel with no matching sample (thin geometry, a silhouette) falls back to the sample nearest in depth and is counted on the `r_rt_stats 1` line | effect to output |
| 6 | Lighting resolve | output |
| 7 | Forward pass: sprites, particles, beams, the view weapon | output |
| 8 | 2D | window |

What is reduced and what is not: only the visibility and AO factors and
the reflection colour come from the effect size. The resolve evaluates
the rest per output pixel: the dlight falloff `(intensity - dist) x
color`, the sun's `max(0, N·L)`, the lightmap and the albedo; the
forward pass draws sprites, particles, beams and the view weapon at the
output, as with every effect off. That is what E computes; how it looks
is RT12's to measure. Its unmeasured costs: the raster, the resolve and
the forward pass at the output size, and the upsample (table above). Its
known risks, each a case for RT12, not a finding: silhouettes and thin
geometry, where no effect sample lies on the surface; the view weapon's
edges against the world; reflections on warped water, whose normal moves
every frame (`r_turbsin`) and which keep no history (above).

Per-frame rules under E, traced through the client: the output is the
world refdef's rectangle, which `V_RenderView` (`cl_view.c`) sets from
`scr_vrect` (`SCR_CalcVrect`, `cl_screen.c`), so each effect's size is
its scale times that rectangle. The refdef with `RDF_NOWORLDMODEL`
renders as with every effect off, as under A. RT7's histories reset on
this section's events that do not concern the scaler (a map load, a
`vid_restart`, a camera cut, a teleport, a change of the rectangle, an
effect turned back on after a world refdef with every effect off), and
an effect's history also on a change of that effect's scale. At every
effect scale 1, E is C and A at `r_rt_scale 1`: one frame, so the four
effect scenarios' pins at scale 1 cover E, and section 5's double-count
invariant applies to E's output pixels. The render-stage path (section 4)
puts the shadow and AO queries in the lighting-resolve fragment shader;
under E the resolve runs at the output size, so queries there would trace
the output's pixels, C's cost. Under E the render-stage path issues them
from a fragment pass at each effect's size instead (RT9).

**The effect scales (RT13), and their design defaults.** RT13 adds one
archived cvar per effect, names them, and gives each its entry in
`doc/040_cvarlist.md`. A value is that effect's traced width and height
as a fraction of the output's, or of the internal size while RT11's
scaler is on (the hybrid). The design default of every effect scale is
1: E is off, and each effect is traced at the size the 3D frame is
rasterized at, so with `r_rt_scale` at its default the frame is
configuration A (above). The 1/2 at which the matrix costs E is a
design point, not a default. RT10 sets the effect scales' defaults per
cell by the default policy (below), replacing these, as it replaces
option A's per-family design defaults of `r_rt_scale` (the subsection
above); until then a scale below 1 is the player's choice. Where the
device has no ray tracing (the VM) the effect scales do nothing, with a
report line (RT13).

**Hybrids.** In general a hybrid rasterizes at a fraction s_A of the
output, upscales with the temporal scaler and traces each effect at a
fraction s_E of the internal size, so it traces (s_A x s_E) squared of
the output's pixels and adds the scaler (from s_A) and the upsample (to
the internal size). H is (1/2, 1/2). Another is A with only some effects
below the internal size, for example AO. A hybrid is RT13's effect
scales applied to RT11's internal size, so RT13 builds it once RT11 has
landed.

**Dynamic resolution.** A controller sets the scale, A's internal height,
E's effect scales or H's pair, for the next world refdef from the
measured GPU time, to hold the frame target. Traced through how the
client feeds the renderer:

- What a frame is: `SCR_UpdateScreen` (`cl_screen.c`) draws one presented
  frame between `R_BeginFrame` and one `R_EndFrame`. With `gl1_stereo`
  set it calls `R_BeginFrame` and `V_RenderView` twice before that one
  `R_EndFrame`; `ref_metal` ignores the separation (`RI_BeginFrame`, "no
  stereo in ref_metal"), so the controller measures the presented frame,
  both world refdefs included. A frame with no world refdef (the loading
  plaque, a cinematic, the console with no map: `V_RenderView` returns
  before `R_RenderFrame` when `cls.state` is not `ca_active` or the
  refresh is not prepped) leaves the controller's state unchanged. The
  model view with `RDF_NOWORLDMODEL` counts in the frame's time but is
  never scaled.
- Its input: each presented frame's GPU time from its command buffers
  (`GPUEndTime - GPUStartTime`, section 8's timer). That is known only
  when the frame completes, so the scale of a frame is set from frames
  that completed before it, one or more frames old.
- Its target: the client's pacing (`src/common/frame.c`): with `r_vsync`
  on, the display's refresh, or `vid_maxfps` when that is set below the
  refresh minus 2. With `r_vsync` off the target is still the refresh
  (the matrix's budget), or `vid_maxfps` when lower. The renderer has no
  API entry for the refresh (`GLimp_GetRefreshRate` is the client's), so
  it reads the display's refresh itself and `vid_maxfps` through
  `ri.Cvar_Get`; the renderer API does not change.
- A paused client re-renders the previous refdef (`V_RenderView` keeps it
  while `cl_paused` is set), so the GPU time is steady and the controller
  settles.
- Histories: this section resets both histories on a change of
  `r_rt_scale`. A controller's step is not a change of the cvar. Section
  8 reports a supported input content scale of 1.0 to 3.0 for the
  temporal scaler; whether a per-frame change of the input size inside
  that range keeps the scaler's history was neither measured nor read
  here, and RT11 establishes it. RT7's history at the internal or effect
  size would be resampled to the new size or reset. This subsection does
  not change any reset rule; a card that builds the controller would
  narrow them, in every place they are stated (the reset row and the
  `r_rt_scale` bullets above, RT11's row and card).
- Pins and measurements run with the controller off, at a fixed scale:
  otherwise a `timedemo` rate measures the controller, not the option.

Fixed per-family defaults against the controller:

| | Fixed default per cell | Controller |
|---|---|---|
| Image | the same scale on every frame and every run; pinnable at the scale | the scale follows the load, so the resolution itself changes over time, which the image-quality program measures as part of its stability |
| Frames heavier than the one the default was set on | missed: more dlights in range (up to the cap of 4 shadow rays, this section's table), more water pixels, and the first frames of a process at about 3.5 times (section 8) | held, by a lower scale |
| Scene dependence in section 8 | the open scene at 1920 x 1080 costs 1.324 + 0.464 + 1.697 + 0.851 = 4.336 ms with every effect on (open table, same rows), against 5.141 closed: 16 % less with 57 % of pixels casting shadow rays. A default set on a closed scene leaves that margin unused in an open one | uses it |
| Cost | none beyond the cell | the controller and any history resampling, not measured |

Which is used is the default policy's choice (below): the controller is
one more option, judged on frame time and on image quality, its
stability included. RT10's frame-time spread at the fixed default over
the scenarios shows whether fixed defaults miss the target; a card for
the controller follows only if they do.

**The image-quality program.** It compares each option against one
reference, by region and by case, and turns the numbers into defaults.

*Reference.* Every effect traced at the output (scale 1, no scaler, no
upsample) with RT7's accumulation converged: the same world refdef is
fed again and again with the per-frame noise index advancing, as a
paused client feeds the previous refdef again (`V_RenderView`, above);
the history is a running mean instead of the 0.9 blend, and the denoiser
is off. It is converged when doubling the frames changes no pixel by
more than 1/255 (section 5's tolerance). For a moving camera each frame
of the replay is converged the same way in turn. It is not real time: in
the primary cell every effect costs 36.8 ms a frame (row RT9 (1)), so 64
frames of accumulation take 64 x 36.8 = 2.4 s per reference frame.

*Candidates.* Each option the host supports, run through the same replay
in sequence as in play, each in a named configuration ("Configurations a
test names", above): A (the default `r_rt_scale`, every effect scale at
1) and A at one other `r_rt_scale`, stated, with every effect scale at
1; E (every effect scale at 1/2, `r_rt_scale 1`); H (`r_rt_scale 0.5`,
every effect scale at 1/2); B and D where the host has them (TASK111
and TASK113, #265 and #267, make them selectable), each with the scales
it runs at stated; and the controller if it is built. Frames are captured at the output size, and the 2D layer
separately.

*Frame rate of the replay.* The replay is recorded at the class's rate,
60 and 120 frames a second, because the histories see an event for a
number of frames that depends on it: a muzzle flash's light dies at
`cl.time` (`CL_AddMuzzleFlash`, `cl_effects.c`) and is kept 32 ms after
(`CL_RunDLights`, `cl_lights.c`), about 2 frames at 60 Hz and 4 at 120
Hz; a lightstyle steps every 100 ms (`CL_RunLightStyles`, `ofs = cl.time
/ 100`), every 6 frames at 60 Hz and 12 at 120 Hz.

*Metrics.* PSNR and SSIM (luminance) per region, on the 8-bit frames the
capture writes. A perceptual metric (FLIP is one) only if it can be
implemented from its published description with the toolchain alone, no
install; otherwise the report says it is absent. Temporal stability: on
a still camera, after the candidate has run a stated number of frames,
the mean absolute frame-to-frame difference per region over the next
frames (the converged reference's is 0 within its tolerance). Response to
an event: after a muzzle flash or a lightstyle step, the frames until a
region's error is back to its level before the event. The harness's
`framediff` score and bad fraction are reported beside them.

*Regions*, from masks the renderer writes for the reference frame at the
output size (a debug output, not derived from the image): textured
surfaces (world and alias material ids, less the silhouette mask);
silhouettes and thin geometry (pixels within one output pixel of a depth
step above a stated ratio or of a material-id change); particles,
sprites and beams (pixels the forward pass writes); water (material id
water); sky (material id sky); the view weapon (the entity with
`RF_WEAPONMODEL`, `cl_entities.c`); 2D and the HUD (pixels 2D draws; it
draws after the 3D frame at the window size in every option, so it is
reported apart, as TASK117 does).

*Cases*, each named with its frames, picked from the trace's data, not
by eye: the four effect scenarios (`rt-shadows`, `rt-sun`, `rt-ao`,
`rt-water`) and `world`; for each, a still camera (one frame repeated), a
moving camera (the demo as recorded), fast turns (frames whose view yaw
changes by more than a stated angle from the previous frame), disocclusion
(frames where RT7's reprojection rejects more than a stated share of
pixels, from the `r_rt_stats 1` line), muzzle flashes (frames with a
muzzle-flash dlight in the refdef) and lightstyle changes (frames where a
lightstyle value changes). Together with the regions they cover the cases
option A's subsection lists (particles, sprites and beams, water
reflections, lightstyle changes and muzzle flashes, the view weapon, and
under A the jitter in RT7's history, seen on a still camera in `rt-ao`
and `rt-shadows`).

*The default policy*, per cell (family and display class) and effect
configuration:

1. An option holds a cell when its GPU time per presented frame at the
   95th percentile over the cases' frames, the first frames of a process
   excluded (section 8), is at most 90 % of the frame: 15.0 ms at 60 Hz,
   7.5 ms at 120 Hz; and when its `timedemo` rate with `r_vsync 1` is
   the refresh rate, because the presented rate also depends on CPU time
   and presentation (TASK97's Finding 1 saw a fixed cost of 3 to 4 ms a
   frame on the `CAMetalLayer` path). Under D (the frame interpolator)
   the frames counted are the rendered ones, not the presented ones: the
   GPU time is per rendered frame, the interpolator's work for the
   frames it generates included, and the rate is the rendered rate,
   because generated frames do not respond to input ("Frame
   interpolator", option A's subsection). Where no host of the family
   shows the class's refresh, item 4 says what is judged instead. The
   10 % headroom is a design choice, not a measurement; the owner may
   set another.
2. An option below the quality floor in any region of any case (its SSIM,
   or its still-camera instability) is not offered as a default there. No
   floor value exists yet, since no image quality is measured: RT12
   proposes one from the distributions it measures and records it here;
   the owner may set another on INFORM12, and RT10 applies the floor
   recorded here when it runs (no card waits for a person, record 0004).
3. Among the options that hold the cell and pass the floor, the default
   is the one with the highest worst-region SSIM (mean over the cases),
   ties broken by the mean PSNR. Where none does, no ray-traced effect is
   on by default in that cell; the effects stay selectable.
4. A default needs a frame time measured on that family at the class's
   output size. A class no host can output keeps the default of the
   largest measured class below it, flagged as modelled, and a modelled
   cell can only rule an option out. A class whose size a host can make
   as a window but whose refresh no host of that family shows (apple9 at
   the two 120 Hz classes: T2's display is 60 Hz) does not take a 60 Hz
   class's default, which was chosen against 15.0 ms. Its options are
   judged by item 1's GPU-time test alone, on GPU times taken at the
   class's size as a window (RT10), against that cell's own target, 7.5
   ms; the `timedemo` condition is recorded as not measurable there, and
   the default is flagged as set without it. apple8 takes apple7's
   defaults until it is measured; nothing measured says whether they
   hold there.

*Stages and cards.*

- Now: TASK117 (#272) measures, on real rasterized frames, what the
  stock raster loses when rendered below the native size and upscaled
  (`MTLFXSpatialScaler`, bilinear, Lanczos; PSNR, SSIM, the regions).
  That is option A's raster cost for still frames, a pessimistic bound
  because the temporal scaler is not measured there (its scope, item 4).
  E rasterizes at the output, so that loss does not apply to it; E's own
  loss, in the effect terms, is not measured by TASK117. Its frames,
  metrics by region and upscaler GPU times are in section 9.
- During development: TASK111 to TASK114 (#265 to #268) make the scalers,
  the interpolator and the denoisers selectable and build the owner's
  local comparison report.
- After RT11 and RT13: RT12 runs the whole program on ray-traced frames
  on the T2 host and records the scores here.
- RT10 applies the policy per cell from RT12's scores, the per-pass
  timings on T1 and T2, and TASK106's apple7 costs.

**INFORM12: options A to E (decision open).** The owner put INFORM12
(#262) on hold on 2026-11-14: TASK105's decision weighed cost only, at
one cell. Restated over the matrix:

| Option | Primary cell (5120 x 2880, 60 Hz, every effect on) | Elsewhere in the matrix | Built by | Image quality from |
|---|---|---|---|---|
| A: whole 3D frame at an internal size, temporal scaler, RT7 kept (TASK105's choice) | 7.3 ms sum, 9.3 ms frame (M); 7.4 to 9.3 ms left for unmeasured work | rays measured or modelled in every apple9 cell; the scaler measured only at this output | RT11 | TASK117 (now: a pessimistic bound on A's raster loss, from the spatial scaler), RT12 |
| B: as A, the denoised scaler in place of RT7 and the scaler | 17.6 ms sum, 18.9 ms frame (M): over the frame before raster or resolve | not measured | TASK113 (#267), selectable for comparison | RT12, where selectable |
| C: trace at the output, accept a rate below the refresh | 36.8 ms (M), over by 20.1 | leaves time at 1920 x 1080 and 2560 x 1440 at 60 Hz (M); over on every larger class and both 120 Hz classes | C is A or E at scale 1; no card of its own | the reference itself |
| D: the frame interpolator on another option | not timed; presents generated frames between rendered ones. The policy counts only the rendered frames under D (item 1): the GPU time per rendered frame, the interpolator's work included, and the rendered rate. So D holds a cell only where the option under it does, with the interpolator's cost added, and each rendered frame reaches the screen later | not measured | TASK111 (#265), a development option, off by default | TASK114's local report |
| E: native raster, effects at reduced sizes, joint bilateral upsample | at 1/2: 9.48 ms (M) of rays and items, 7.2 ms left for the native raster and the upsample (not measured); at 1/3: 4.44 ms (modelled), 12.2 ms left (modelled) | rays measured or modelled in every apple9 cell; raster at the output and the upsample not measured | RT13 | RT12 |
| Hybrids (H and others) | H: 5.07 ms (modelled rays, measured scaler), 11.6 ms left (modelled) | as A plus E | RT11 with RT13 | RT12 |
| Dynamic resolution | as the scale it settles at, plus the controller | as the scale | a card only if RT10 shows fixed defaults miss | RT12, if built |

Decision: open. It settles on image-quality data, which RT12 (TASK110,
#264) produces on ray-traced frames once RT11 (TASK109, #263) and RT13
have landed, with TASK117 (#272) giving now a pessimistic bound on A's
raster loss from the spatial scaler and TASK114 (#268) the owner's local
side-by-side report from the selectable modes of TASK111 to TASK113.
Until then the subsection above describes A as RT11 builds it, and RT13
builds E, so that both can be measured; neither
is the decision, and nothing is recorded in `docs/decisions/`.

## 3. What the rasterizer must expose

`ref_metal` (TASK28) renders the world and entities into a G-buffer before
lighting is resolved. Required attachments, all at the frame's resolution
(the internal resolution of option A or a hybrid while a ray-traced
effect is on; the world refdef's rectangle under option E, section 2):

| Attachment | Format | Written by |
|---|---|---|
| depth | depth32float | every opaque draw |
| world normal | rgb10a2 | world: plane normal (`msurface_t.plane`, flipped when `SURF_PLANEBACK` is set, defined in `gl1 headers`); alias: lerped normal from `dtrivertx_t.lightnormalindex` through the 162-entry table in `src/client/refresh/constants/anorms.h` |
| material id | r8 (0 none, 1 world, 2 alias, 3 water, 4 sky, 5 trans) | per draw |
| motion vector | rg16float | world: from previous view only; alias: previous-frame lerped position |
| lightmap color | rgba8 (the stylized static lightmap sample, no dlights) | world draws |
| albedo | rgba8 | every draw |

Pass order per frame: (1) IAS rebuild and alias refits (compute),
(2) G-buffer raster, (3) RT passes in the order dlight shadows, sun, AO,
reflections (each a compute dispatch; reflections last because it reads
the lit result of the others for hit shading), (4) temporal + denoise,
(5) lighting resolve: `albedo × (lightmap × AO + dlight × vis + sun × vis)`
plus reflection blend, (6) forward pass for sprites, particles, beams and
the view weapon over the resolved image, (7) `MTLFXTemporalScaler` from the
internal to the output size (section 2, RT11), (8) 2D. Under option E
there is no step 7: each effect is traced and denoised at its own size
and upsampled to the G-buffer's size before step 5 (section 2, RT13).
With every effect
cvar at 0 steps 1, 3, 4 and 7 are skipped and step 5 reduces to the stock product,
which is the state that must keep every gl1 pin.

### The G-buffer as RT2 built it (TASK54)

**Three paths.** A world view takes one of three paths (`rm_gb_path`,
`rm_gbuf.c`; a view without the world, the menu's player model, always
takes the first):

- *forward*: the renderer before RT2, unchanged;
- *forward plus depth and motion* (`r_rt_motion 1`, or a scaler's request
  once TASK111 makes one): the stock passes and pipelines' colour, with a
  kept `depth32Float` attachment and one `rg16Float` motion-vector
  attachment, and none of the other four;
- *G-buffer* (`r_rt_gbuffer 1`, or any ray-traced effect on once RT3 adds
  one): step 2 (the raster), step 5 (the resolve), step 6 (the forward
  pass), then the 2D as before.

The raster draws the world (lightmapped and `r_fullbright` surfaces,
material 1; opaque warped water, 3; the sky box, 4), the opaque brush
models (1, and 3 for their warped faces) and the opaque alias models but
the view weapon and shells (2). Everything else is the forward pass's
(`rm_gb_entity_split`, held by the host test), in the stock order within
that pass: the view weapon, sprites, beams, null models and the stencil
shadows of the raster's alias models, then the translucent entities, the
flash blobs, the particles, the translucent surfaces (material 5 is not
written: they are blended over the resolved image) and the screen blend.
The forward pass loads the raster's depth, so it is occluded as before.
The stock order holds within the forward pass only: every rastered model
is drawn before the forward pass starts, so the beams and the stencil
shadows, which the stock view interleaves with the models in list order,
now come after all of them. A beam that writes no depth, followed in the
list by a model behind it, is overdrawn by that model in the stock view
and blended over it here.

The light attachment holds the other factor of each pixel's stock
product: the lightmap sample as `rm_fragment_world_lm` reads it (bilinear
or `metal_lightmap_filter`'s bicubic), the alias model's CPU vertex light,
the warp's colour, or white for the sky and `r_fullbright`. While no
ray-traced dynamic light is on, the lightmap holds gl1's dynamic lights
(`R_BuildLightMap`), so the attachment does too and the resolve's dlight
term is 0; RT3 stops adding them to the lightmap when its effect is on,
and from then the attachment is the static lightmap. The resolve
(`mtl_gb_resolve`, `common/mtl_gbuf_math.h`, held by the host test) takes
`mtl_world_lm` with the frame's lightmap environment (`R_FlushChain`'s)
for material 1 and `GL_MODULATE` for 2 to 4, and keeps the target's colour
where no draw wrote (material 0). The world normal is the per-vertex copy
of the surface's (`rm_world_t.normals`, `rm_surf_normal`), computed at map
load and uploaded by the first G-buffer view of the map (`RM_WorldNormals`),
so the default path makes no buffer for it and does not depend on one;
without it the raster draws no static world. An alias normal
is the two frames' `anorms.h` entries lerped by `backlerp` and normalised
(`rm_gb_alias_normal`), both turned into world space by the model's
rotation.

**The G-buffer contract (for TASK111).** The back end's accessors are in
`header/rm_gbuf.h` (`rmb_gbuf.m`):

| Attachment | Texture | Format, size | Written by |
|---|---|---|---|
| depth | `rmb_gbuf_depth()` | `MTLPixelFormatDepth32Float`, the render size (`rmb_gbuf_size`) | every opaque draw (raster and forward pass; on the motion path the forward pass) |
| motion vector | `rmb_gbuf_motion()` | `MTLPixelFormatRG16Float`, the render size | the raster's draws and the forward pass's opaque draws; blended draws leave it |
| world normal, material id, light, albedo | internal (`rmb_gbuf.m`) | `RGB10A2Unorm` (n x 0.5 + 0.5, alpha 1, 0 where none), `R8Uint`, `RGBA8Unorm`, `RGBA8Unorm` | the raster |
| surface (TASK131) | internal (`rmb_gbuf.m`), colour attachment 5 of the raster | `R32Uint`, the render size, only while RT3's shadows trace | the raster's draws from the world's vertex buffer: `(entity << 24) \| (vertex + 1)`; 0 elsewhere |

- *Frame.* The current frame's world view, rasterized with the jittered
  projection (`r_rt_jitter_x`, `r_rt_jitter_y`, pixels, +x right, +y down,
  default 0: `rm_gb_jitter` moves a point by that much and leaves its
  depth); the motion vectors come from the unjittered current and
  previous projections, so they exclude the jitter (MetalFX's convention).
- *Valid* from the end of the raster (the motion path: of the forward
  pass) until the frame's command buffer ends; the next frame's view
  writes them again. They are made again when the render size or the path
  changes, so a consumer holds no reference across frames.
- *Motion units and sign.* Pixels of the render-size texture, where the
  point was minus where it is (previous minus current), +x right, +y down
  (the texture's rows): `MTLFXTemporalScaler`'s `motionVectorScaleX` and
  `motionVectorScaleY` are 1 (a scaler that wants current minus previous
  takes -1). A pixel no draw wrote has (0, 0); so does a point behind
  either camera's plane.
- *Depth.* GL's clip depth mapped to [0, 1] (`mtl_gl_clip_z`): 0 at the
  near plane (`gl_znear`), 1 at the far plane, not reversed
  (`depthReversed` NO), cleared to 1; the view weapon's `RF_DEPTHHACK`
  draws keep their 0 to 0.3 depth range.
- *Render size.* Every attachment and the 3D pass take their size from
  the render size (`rm_gb_render_size`), today the frame's;
  `r_rt_gbuffer_scale` (development) sets a smaller one, and the view is
  then stretched bilinearly into the frame's view rectangle (a stand-in
  for the scaler) before the 2D, which stays at the output size. Every
  size of the 3D pass in frame pixels follows the render size: the
  particles' point size, its bounds and the pixel grid they are snapped
  to (`rm_gb_pixel_scale`, `rm_gb_point_scale`, `RM_GB_TargetSize`), so a
  particle covers as much of the view as at the frame's size once it is
  stretched. Nothing else in the 3D pass is sized in pixels: beams, flash
  blobs, the sky, shadows and the screen blend are in world or view
  units, and no draw sets a line width.
- *Surface (TASK131).* The raster gets the surface attachment only while
  RT3's shadows trace (`RM_RTS_Active` when the view begins,
  `rmb_gbuf_set_surface`). Its raster pipelines are a second set, built
  with function constant 5 (`rm_gb_surf`) at the first view that needs
  them. With the shadows off, the raster, its five attachments and its
  pipelines are TASK54's, and the forward and motion paths never have
  the attachment. It is cleared to 0. A draw from the world's static
  vertex buffer writes `mtl_rts_gb_value` (`common/mtl_rts_math.h`):
  - the low 24 bits hold the first vertex of the triangle (Metal's
    provoking vertex, `[[vertex_id]]`) plus one. The vertex is an index
    of the world's vertex buffer, and its surface is found through
    `rm_rts_vertex_map`;
  - the top 8 bits hold the entity that draws it: 0 for the world, i + 1
    for the refdef's brush entity i (`RM_GB_EntityMotion`), and 255 for a
    brush model that is not one of the refdef's entities.

  Every other draw writes 0: alias models, and dynamic world draws (the
  sky, the warped and flowing batches) are not that buffer's vertices.
  `rmb_gbuf_bind_world` passes the draw's tag at vertex buffer 4. A map
  with more than `MTL_RTS_GB_VMAX` (2^24 - 2) vertices gets no vertex
  table, and every pixel takes the eye ray. The attachment costs 4 bytes
  a pixel: 8,294,400 bytes at 1920 x 1080 and 58,982,400 (56.25 MiB) at
  5120 x 2880, by the format. The allocated size is logged as `G-buffer
  surface attachment`. The tables it reads cost 4 bytes per world vertex
  for the vertex table, and 3 x 8 KiB for the entity table in the frame
  ring.

*As consumed by TASK111.* The scalers take the depth and motion as above:
`motionVectorScaleX` and `motionVectorScaleY` 1, `depthReversed` NO, auto exposure off.
On a scaled view the render size is the scaler's input, the view drawn at the target's
origin (`rm_fx_input_viewport`), not `r_rt_gbuffer_scale`'s, and the scaler writes the
frame's rectangle in place of the stretch. The jitter of the temporal modes is the Halton
(2, 3) pair minus 0.5 from index 1, a cycle of 32, started again on each reset
(`rm_fx_jitter`). The reset events are those of section 2, with the client's teleport
distance, 256 units on an axis, as the cut threshold (`RM_FX_CUT_DIST`). See the card file
[MetalFX scalers for the 3D view](../runbook/TASK111.md).

**Previous-frame transforms.** `rm_gb_hist_t` (`header/rm_gbuf.h`;
`rm_gbufscene.c` holds one) keeps the last world view's modelview,
unjittered projection, the view weapon's unjittered projection, origin and
time, and, per entity, its model, origin, old origin, angles, frames,
`backlerp` and flags. `entity_t` carries no stable id, so each entity of
this frame is matched, in list order, to the nearest unmatched previous
entity with the same model and the same `RF_WEAPONMODEL` bit whose origin
lies within 256 units (`rm_gb_match`). An alias model's previous
positions are its matched entity's frames lerped with that entity's
`backlerp`, origin and angles (`R_AliasMotion`, `rm_mesh.c`); a moving brush
model's previous matrix is its matched entity's; the camera's is the
previous view's. Left out: an entity without a match (new this frame, a
teleport past 256 units) takes its own state as the previous one, so its
pixels carry the camera's motion alone; the first frame after a map load
or a `vid_restart` (`RM_GB_Reset`) has no previous view, so every vector
is 0; a camera cut or the player's teleport is not detected here (RT11
adds those reset events); sprites and beams, drawn with the world's
matrices, carry the camera's motion, and a null model and the particles
carry none.

**The off state: measured, and the choice (an owner decision, reversible).**
Timed on engine commit `4bf077a4`, before the card was rebased onto
`aa5ab641`. What came in after it does not touch the timed draws:

- TASK119 adds 2D options, all off by default.
- FIX61 sizes the frame by the client's rule, with no change in behaviour.
- The rework sizes particles in render pixels only below the frame's size
  (the runs were at scale 1).
- The world normals buffer is now made by the first G-buffer view, not at
  map load. That is one upload, before the warm frames.

The runs were taken with every effect off, against `origin/main`'s
`ref_metal` at the time (`d9e76e40`, with the same `MTL_GPU_TIME` measuring
aid), alternated run by run, three rounds, the `particles` scenario (world,
entities, view weapon, particles, dynamic lights, blend), on the M6
(apple9). GPU time of the frame's drawing
per frame, warm (each run's frames 101 to the last 20), the median of the
three runs' medians, with the range of their 10th and 90th percentiles:

| Path | 640 x 480 | 1920 x 1080 | 5120 x 2880 | Attachments added at 5120 x 2880 |
|---|---|---|---|---|
| base | 0.105 ms (0.086 to 0.132) | 0.200 ms (0.178 to 0.268) | 1.091 ms (0.933 to 1.619) | none |
| forward | 0.107 ms (0.087 to 0.137) | 0.200 ms (0.178 to 0.268) | 1.103 ms (0.980 to 1.800) | none |
| forward + depth and motion | 0.135 ms (0.109 to 0.165) | 0.304 ms (0.272 to 0.434) | 1.728 ms (1.502 to 4.935) | 132.7 MB by the formats, 136.2 MB allocated |
| G-buffer | 0.243 ms (0.211 to 0.295) | 0.904 ms (0.755 to 1.066) | 6.796 ms (5.493 to 8.852) | 324.4 MB by the formats, 332.4 MB allocated |

At 640 x 480 the motion path's attachments are 3.0 MB allocated and the
G-buffer's 7.3 MB; at 1920 x 1080, 19.3 MB and 47.1 MB. `timedemo` ran at
the display's rate on every path (120 fps at 640 x 480, 60 at the other
two, whatever `r_vsync` said, as earlier runs on this host found), so it
does not tell the paths apart. **Choice:** the G-buffer path is not free
with everything off (6.2 times the forward path's GPU time and 332 MB at
5120 x 2880), so the forward path stays, unchanged, when every effect is
off; the G-buffer path is taken when any effect is on or `r_rt_gbuffer` is
set, and the forward path with depth and motion when `r_rt_motion` (or,
later, a scaler) asks for it. Reversing it is one line in `rm_gb_path`
(an effects-off view returning `RM_GB_PATH_GBUFFER`). The numbers and how
they were taken are in `docs/runbook/TASK54.md`.

## 4. Hardware gating

| Check at startup (`MTLDevice`) | Enables |
|---|---|
| `supportsFamily(.apple6)` and `supportsRaytracing` | compute RT passes (TASK24); M1 is apple7 |
| `supportsFamily(.apple9)` and `supportsRaytracingFromRender` | render-stage intersection: shadows and AO move into the lighting-resolve fragment shader, no separate dispatches or history reprojection for shadows (TASK25); M3 and later. Under option A the resolve runs at the internal size; under option E it runs at the output size, so the queries go to a fragment pass at each effect's size instead (section 2, RT9) |
| neither | the effect cvars (the `r_rt_*` cvars that switch an effect on, such as `r_rt_shadows`) are forced to 0 and reported as unavailable in `vid_restart` output; `r_rt_scale`, once RT11 adds it, reads 1 (section 2); the rasterizer path is byte-identical to RT off |

The VM's paravirtual GPU reports no ray-tracing support: TASK24 builds
there but validates on the M1 Max (T1); TASK25 validates on the M6 (T2).
Hardware intersection (apple9) is used automatically when present for the
compute path too; the apple6 to apple8 fallback is the software traverser
Metal provides, no separate code path.

## 5. Verification with the pin harness

1. **RT off keeps every gl1 pin.** The four scenarios in
   `scripts/pins/scenarios/` are compared with `scripts/pins.sh compare
   metal <scenario> --ref gl1` at the threshold TASK28 accepted. This is a
   gate on every RT card: the G-buffer refactor in section 3 is where
   regressions would appear.
2. **One scenario per effect**, committed as `rt-shadows.cfg`,
   `rt-sun.cfg`, `rt-ao.cfg`, `rt-water.cfg` (each the base scenario plus
   the one effect cvar on, temporal accumulation disabled for the pin so
   frames are stateless, blue-noise seed fixed by `pins_capture`; RT11 adds
   `r_rt_scale 1` and RT13 sets each effect's scale to 1, so no scaler or
   upsample runs and no MetalFX history enters a pin). They are
   pinned against **`ref_metal` itself** at the commit where the owner
   accepted the look: `scripts/pins.sh record metal rt-ao` on that commit
   becomes the reference, later commits compare against it at 0.02. The
   pin catches drift, not quality.
3. **Performance** is the fps line `pins.sh record` writes to
   `timedemo.txt`, captured with all effects on; each card states its
   number and the machine (apple family) it came from. Before RT11 it is
   captured at 1080p. From RT11 on it is captured at the host's native
   output size in one configuration, A of section 2 ("Configurations a
   test names"): `r_rt_scale 0` (its default, the family default) with
   every effect scale at 1 once RT13 has landed, both set explicitly for
   the capture. RT13 does not change it, since every effect scale's design default is 1.
   The number also states the internal, effect and output sizes.
4. What an agent can judge alone: pin parity with RT off, drift against an
   accepted RT look, fps, the double-count invariant (a pixel with no
   dlight in range and no sun visibility must equal the RT-off pixel within
   1/255, checked by a `framediff` run between the RT-off frame and the
   RT-on frame masked by the visibility buffer, a `--mask` option RT2 adds).
   What it cannot judge: whether the look is right. **The owner
   checkpoints:** after RT3 to RT6 each land, the owner views the four RT
   scenarios once and either accepts (the pins are recorded) or sets the
   cvar defaults differently (the card re-records). After RT12 the owner
   may also set another image-quality floor than the one RT12 proposes,
   and settles INFORM12 (section 2, the default policy); RT10 does not
   wait for either. No other person-in-the-loop step exists on this path.

## 6. Sub-cards

Filing: `docs/plans/metal-port-cards.md` is the authoritative card list; the
list below is the detailed scope behind its rows.

Dependency order is the listed order unless stated. Each is one PR, gate
green, pins as stated.

| Card | Scope | Acceptance |
|---|---|---|
| RT1 | Measure and build: world and alias triangle extraction, PAS per map and per model, IAS per frame, counts and memory logged by `r_rt_stats 1`; no visual change | all gl1 pins unchanged; `r_rt_stats` prints triangle and byte counts for base1 and the three demo maps, recorded in this document |
| RT2 | G-buffer refactor in `ref_metal` (section 3) and the lighting-resolve shader that reproduces the stock product; `framediff --mask` | all gl1 pins unchanged at the accepted threshold (this is the risky card: byte-identical RT-off is the goal, 0.005 the cap) |
| RT3 | Dlight shadows, replacing `R_AddDynamicLights` when on | RT off: pins unchanged; double-count invariant holds; `rt-shadows` scenario recorded after owner acceptance; fps at 1080p on apple7 stated |
| RT4 | Sun/sky shadow with the direction table and the sky-area skip | same shape as RT3 with `rt-sun`; maps under the sky-area threshold render byte-identical to RT off |
| RT5 | Ambient occlusion with blue noise and the lightmap-only multiply | same shape with `rt-ao`; AO never drops a pixel below `r_rt_ao_min × lightmap` |
| RT6 | Water reflections, one bounce, warp-perturbed normal | same shape with `rt-water`; non-water pixels byte-identical to RT off |
| RT7 | Temporal accumulation and denoiser (depends RT3 to RT5) | pins unchanged (pins run with temporal off); fps with everything on ≥ 60 at 1080p on apple7, number recorded |
| RT8 | Hardware gating and fallbacks, `vid_restart` reporting, VM build | every effect cvar forced 0 on the VM with the report line; pins unchanged there (`r_rt_scale` is not RT8's: RT11 adds it, and it reads 1 there, section 4) |
| RT11 (TASK25) | Option A: internal resolution and `MTLFXTemporalScaler` (section 2, TASK105): the world refdef's 3D frame at `r_rt_scale` with a jittered projection, upscaled to that refdef's rectangle, 2D at the window size, a refdef with `RDF_NOWORLDMODEL` at its own size with no scaler; the cvar with its `doc/040_cvarlist.md` entry; the reset events for the scaler and RT7's history; option A's per-family design defaults of section 2 (depends RT7, RT8) | every effect off: all gl1 pins unchanged and no scaler created; the four effect scenarios carry `r_rt_scale 1` and their pins are unchanged; with an effect on and the scale below 1, each reset event (map load, `vid_restart`, camera cut, teleport, a change of the scale or of the world refdef's rectangle through `viewsize` or the window size, an effect turned back on after a frame with every effect off) resets the scaler and RT7's history on that frame, shown by a `r_rt_stats 1` line, and the player-setup menu's model view open in game and a `fov` change from 90 to 30 reset neither, shown by the same line; at `viewsize` below 100 the upscaled view fills the same centred rectangle as with every effect off; where `supportsDevice:` is NO or the device has no ray tracing (the VM), `r_rt_scale` reads 1 with a report line; for each effect alone and every effect on at the default scale on T1 and T2, the GPU time per presented frame and the `timedemo` rate, the scaler's GPU time at each host's output (cells section 2's target matrix leaves unmeasured) and the internal and output sizes, recorded in `docs/audits/rt-performance.md` |
| RT13 (TASK25) | Option E and the hybrids (section 2, "Targets per GPU family and display, and the options", TASK116): the world refdef's 3D frame rasterized at its rectangle; each effect traced, accumulated and denoised (RT7) at its own scale of that rectangle, one archived cvar per effect, its default as section 2 states ("The effect scales": 1 for every effect, E off, until RT10), with its `doc/040_cvarlist.md` entry; a joint bilateral upsample of each effect to the rectangle on depth, normal and material id, with a counted fallback where no sample matches; with RT11's scaler on, the effect scales apply to the internal size (the hybrid); a refdef with `RDF_NOWORLDMODEL` takes no effect (depends RT7, RT8, RT11) | each effect scale's default as section 2 states (1 for every effect), so that with `r_rt_scale` and every effect scale at their defaults the frame is configuration A; every effect off: all gl1 pins unchanged; every effect scale at 1: the four effect scenarios' pins unchanged, and the frame byte-identical to RT11's at `r_rt_scale 1`; with an effect scale below 1, section 5's double-count invariant holds at the output pixels; the upsample's weights are host-tested with mutants killed (a sample across a depth step or a material-id change gets no weight; the fallback is counted on the `r_rt_stats 1` line); each effect's history resets on section 2's events that do not concern the scaler and on a change of that effect's scale, shown by the same line, and the model view and a `fov` change from 90 to 30 reset nothing; where the device has no ray tracing (the VM) the effect scales do nothing and the report line says so; on T1 and T2 at each host's output, the GPU time of the raster, resolve and forward pass at the output size and of each effect's upsample, and for each effect alone and every effect on in section 2's configuration E (every effect scale at 1/2, `r_rt_scale 1`) and configuration H (`r_rt_scale 0.5`, every effect scale at 1/2), the GPU time per presented frame and the `timedemo` rate, recorded in `docs/audits/rt-performance.md` |
| RT9 (TASK25) | Render-stage ray queries on apple9, compute path kept for apple6 to apple8, at the traced size of the option in use: under A and the hybrids, shadows and AO in the resolve shader at the internal size; under E, in a fragment pass at each effect's size, since E's resolve runs at the output size (section 4) (depends RT7, RT11, RT13) | identical pins on both paths at 0.005 (pins run at scale 1, `r_rt_scale 1` and every effect scale 1, temporal accumulation off); in the T2 host's cell of the target matrix (apple9, 5120 x 2880 at 60 Hz, section 2), for each effect alone and every effect on, in section 2's configuration A (the default `r_rt_scale`, every effect scale at 1) and configuration E (every effect scale at 1/2, `r_rt_scale 1`), both paths' GPU time per presented frame at the 95th percentile and `timedemo` rate with `r_vsync 1`, the first frames of a process excluded, recorded against the cell's 16.7 ms frame and the default policy's 15.0 ms target; the render-stage path holds the cell by the default policy (section 2) with every effect on in at least one of configurations A and E; the times, the option and the internal, effect and output sizes recorded in `docs/audits/rt-performance.md`, the compute path beside them. Tracing at native pixels (option C) is not the target: 36.8 ms on the M6 (section 8, verdicts table, row RT9 (1)) |
| RT12 (TASK25) | Image quality of options A, E and the hybrids on real ray-traced frames, by section 2's image-quality program (depends RT11, RT13 and the owner's acceptance of the RT look, section 5) | for the four effect scenarios and `world`, each case of the program (still and moving camera, fast turns, disocclusion, muzzle flashes, lightstyle changes) is named with its frames from the trace, replayed at 60 and at 120 frames a second; the candidates (each in a configuration section 2 names, "Configurations a test names", with its scales recorded: A (the default `r_rt_scale`, every effect scale at 1) and A at one other `r_rt_scale` with every effect scale at 1; E (every effect scale at 1/2, `r_rt_scale 1`); H (`r_rt_scale 0.5`, every effect scale at 1/2); and B and D where TASK111 and TASK113, #265 and #267, make them selectable) run at the T2 host's output and are compared with the converged reference at the same output from the same replay; PSNR, SSIM, the perceptual metric if it can be built with the toolchain alone (or its absence stated), still-camera stability and event response, per region (textured surfaces, silhouettes and thin geometry, particles, sprites and beams, water, sky, the view weapon, 2D and the HUD), recorded in this document per option, case and region (they include the cases option A's subsection lists); a quality floor proposed from the distributions and recorded here, which the owner may change on INFORM12; nothing derived from game data is committed; a default that should change is a finding for RT10 |
| RT10 (TASK25) | Acceleration-structure refit vs rebuild tuning, GPU timestamps, and the default option and scale per cell of the target matrix (depends RT9, RT12 and TASK106, #249) | per-pass timings table in `docs/audits/rt-performance.md` for apple7 (T1) and apple9 (T2) at each host's output and at the 60 Hz classes it can show as a window, including the G-buffer raster with its motion vectors at the internal and at the output size, RT7's accumulation and denoiser, the scaler and the upsample; on T2 also at the two 120 Hz classes' sizes (3024 x 1964 and 3456 x 2234) as windows, the same table and the GPU time per presented frame at the 95th percentile, with the default policy's `timedemo` condition recorded as not measurable there (T2's display is 60 Hz; GPU time does not depend on the display) and those cells judged against their own 7.5 ms target by section 2's policy item 4; model R checked again on each family's own columns, as section 2 checks it on section 8's, before it fills a cell no host can output; the default option and scale per cell (family and display class, each effect configuration) set by section 2's default policy from those timings, RT12's scores and the quality floor recorded in section 2, replacing section 2's design defaults (option A's per-family `r_rt_scale` heights and every effect scale at 1); the frame-time spread at the fixed defaults over the scenarios, and whether it misses the target (a finding for a dynamic-resolution card if it does); no pin change |

Out of scope, decided after RT10 per the plan: path tracing (Phase G),
global illumination beyond AO, RT for particles or beams. No RT row and
not a default (section 2): `MTLFXFrameInterpolator` (option D), which adds
latency in a game, and `MTLFXTemporalDenoisedScaler` in place of section
2's denoiser (option B), which is over the 60 Hz frame on the M6; TASK111
and TASK113 (#265, #267) make them selectable for the comparison. A
dynamic-resolution controller has no row until RT10 shows that fixed
defaults miss the target.

## 7. Measured device capabilities (TASK99)

Measured with a throwaway Objective-C program (Metal and Foundation
frameworks, built with `xcrun clang`, no window, no game data, not
committed) on the bare-metal host: macOS 27.2.1 (26B131), arm64, default
`MTLDevice` named **Apple M6**. The assumptions about the VM
(paravirtual device, no ray tracing, apple5-class families) in
[the Metal renderer plan](metal-renderer.md) are not
re-measured here; this is the physical-device column the gating needs.

| Query | Apple M6 (physical) | Section 4 / plan statement for the VM |
|---|---|---|
| `supportsFamily` apple1 to apple10, metal3 | all true | apple1 to apple5 and mac2 true; apple6, apple7, apple8, apple9, metal3 false |
| `supportsRaytracing` | true | false (forced off) |
| `supportsRaytracingFromRender` | true | false |
| `supportsPrimitiveMotionBlur` | true | not stated |
| `supportsFunctionPointers`, `...FromRender`, `supportsDynamicLibraries` | true, true, true | `supportsFunctionPointers` false; the other two not stated |
| `readWriteTextureSupport` | tier 2 (`MTLReadWriteTextureTier2`) | not stated |
| `argumentBuffersSupport` | tier 2 (`MTLArgumentBuffersTier2`; raw value 1) | tier 2 |
| `hasUnifiedMemory` | true | true |
| `recommendedMaxWorkingSetSize` | 26,800,603,136 bytes (about 25 GiB) | about 5.4 GB |
| `maxBufferLength` | 20,100,448,256 bytes | not stated |
| `maxThreadsPerThreadgroup` | 1024 x 1024 x 1024 (pipeline `maxTotalThreadsPerThreadgroup` 1024, `threadExecutionWidth` 32 for the ray kernel) | 1024 x 1024 x 1024 |
| `maxThreadgroupMemoryLength` | 32768 bytes | not stated |
| Largest 2D RGBA8 private texture created | 32768 x 32768 (8192 and 16384 also ok) | not stated |
| Stage-boundary counter sampling | supported (GPU timestamps for RT10 are possible) | not stated |
| `supportsPullModelInterpolation`, barycentric coordinates, 32-bit float filtering, 32-bit MSAA, BC compression, `supportsQueryTextureLOD` | all true | 32-bit MSAA true; the others not stated |

End to end, one run: a one-triangle primitive acceleration structure
(640 bytes, built without refit usage) was built, wrapped in a one-instance instance acceleration
structure, and a compute kernel (`intersector<triangle_data, instancing>`
over `instance_acceleration_structure`, Metal Shading Language 3.1) cast
two rays. The ray at (0.25, 0.25, -1) along +z returned hit distance
1.000000 (expected 1.0) and the ray at (5, 5, -1) returned a miss
(-1, expected); both command buffers finished with status completed and no
error. The path works on the device, not only the flag.

Sizes of a primitive acceleration structure on a synthetic triangle soup
(n triangles, each a right triangle with 64-unit legs at a random integer
position in a 4096 x 4096 x 1024 box, fixed seed, unindexed, opaque; sizes
chosen because section 1 expects 10k to 20k world triangles and a refit
population of up to about 1 M alias triangles, so 10k, 20k, 50k and 200k
were measured). The soup structures were built with
`MTLAccelerationStructureUsageRefit`, so their sizes are not comparable
with the one-triangle 640-byte structure above, which was built without
refit usage.

| Triangles | Structure bytes | Build scratch bytes |
|---|---|---|
| 10,000 | 937,728 | 251,584 |
| 20,000 | 1,874,816 | 904,128 |
| 50,000 | 4,686,336 | 2,258,880 |
| 200,000 | 18,743,488 | 9,033,728 |

Build and refit times were observed in the low milliseconds but varied
several-fold between process invocations while another GPU client was
active, so no figure is recorded here; they are measured on a quiet GPU
under TASK101 (#228).

The structure size is about 94 bytes per triangle on this soup (0.9 to
1.9 MB for 10k to 20k triangles), though a real map has coherent geometry
and will differ; RT1 measures that. The measured figure of about 94 bytes
per triangle is above section 1's assumed 64 bytes per triangle plus
nodes, and the "under 4 MB" total still holds at the world size section 1
states. A structure with refit usage refits in place, using the same
descriptor, with a refit scratch size of 0.

Per-decision verdicts, section 4 and section 6, for this device:

1. **apple6 and `supportsRaytracing` gates the compute path (TASK24):**
   unchanged; true here. The M6 also satisfies the apple9 check, so this
   host cannot exercise "compute path selected, render-stage not" (an M1
   Max, apple7, does that). TASK24's T1 validation still needs an M1 Max.
2. **apple9 and `supportsRaytracingFromRender` gates render-stage
   intersection (TASK25):** unchanged; both true. This host is a valid
   T2 device for TASK25.
3. **Neither forces `r_rt_*` to 0 (TASK60 / RT8):** unchanged; this device
   never takes the branch. (Since TASK105, section 4 forces only the effect
   cvars to 0 there, and `r_rt_scale` reads 1.) The forced-off half stays VM-only, and on this
   host it can only be tested by an override cvar that pretends the
   capability is absent. Finding for the RT8 card: add such an override
   (or a way to cap the family) so the fallback and the apple6 to apple8
   compute selection are testable on a device that supports everything.
4. **Argument buffers:** section 2 and 3 do not state a tier. The M6
   reports `MTLArgumentBuffersTier2` (compared by constant; the raw value
   1 is Tier 2, Tier 1 is 0), so passing the G-buffer, history buffers
   and material tables to the ray kernels through argument buffers is
   available (the build host is also tier 2 per that plan). No change to any gating decision.
5. **Memory budget (section 1):** unchanged and confirmed with large
   margin: the structure for 20k triangles is about 1.9 MB against the
   "under 4 MB" assumption, and unified memory with a 25 GiB working-set
   hint removes any pressure from 128 refit instances.
6. **Build and refit cost (sections 1 and 6, RT1/RT10):** the cost is
   measured under TASK101 (#228). Nothing observed here contradicts "build
   once at map load, refit per frame". Whether refit stays cheap as many
   small instances on an apple7 device is for RT10.
7. **Metal API notes for the implementers (findings, no decision
   changes):** a refit-capable structure reports a refit scratch size of
   0 and refits in place with `destination` equal to the source;
   `useResource` on each primitive structure is required in the compute
   encoder that traces an instance structure; and on macOS 27 the
   properties `lowPower`, `headless`, `removable` and the
   `MTLGPUFamilyCommon*` constants are deprecated, so the gating code must
   key on `MTLGPUFamilyApple*` and the ray-tracing properties only (which
   section 4 already does).

No gating decision in sections 4 and 6 changes on this device, and this
section records capabilities and sizes only; build and refit timings are
measured under TASK101 (#228). The finding above (3) is a card for the RT8 owner, not an edit to that card.

## 8. Ray-query throughput and acceleration-structure cost (TASK101)

Measured with a throwaway Objective-C++ program (Foundation, Metal,
MetalFX and CoreGraphics frameworks, built with `xcrun clang++`, kernels in
Metal Shading Language 3.1 compiled at run time, no window, no game data,
not committed) on the bare-metal host: macOS 27.2.1 (26B131), default
`MTLDevice` **Apple M6** (the device of section 7). All times are GPU time
from the command buffer's `GPUEndTime - GPUStartTime`, one command buffer
per run, waited on before the next. Each configuration ran one discarded
warm-up and then 8 timed runs. A cell gives the median of the
per-invocation medians, with the minimum to maximum over all timed runs of
all invocations (the union of the per-invocation ranges); where the
invocations differ, the text gives them.

**Display.** `CGDisplayCopyDisplayMode(CGMainDisplayID())` returns a mode
of 2560 x 1440 points (`CGDisplayModeGetWidth`, `CGDisplayModeGetHeight`),
5120 x 2880 pixels (`CGDisplayModeGetPixelWidth`,
`CGDisplayModeGetPixelHeight`) and 60.00 Hz (`CGDisplayModeGetRefreshRate`).
`system_profiler SPDisplaysDataType` names the display a Studio Display
(5120 x 2880 Retina). Its maximum refresh is 60 Hz, so 60 Hz is the RT9
target on this host and a higher-refresh case cannot be measured on it.
The native resolution is the pixel size, 5120 x 2880; 2560 x 1440 is the
point size and is labelled so below. A 60 Hz frame is 16.7 ms.

**Quiet window.** Builds and gates were held while measuring. Before and
after each batch: `uptime`; `ps -axo comm= | grep -E -x -c
'(make|cc|clang|quake2)'`, a basename variant of it and a search of the
process command lines for `scripts/test.sh`, `make`, `q2ded`, `quake2`,
`tests/unit` and `coverage.sh`, all 0 at every check; the GPU's `"Device
Utilization %"` from `ioreg -r -c IOAccelerator -d 1 -w0`; and the number
of `"IOUserClientCreator"` lines from `ioreg -r -c IOAccelerator -l -w0`
(GPU driver user clients, held by every process that has opened the GPU,
rendering or not). The program's own build never ran during a batch. The
after check ran 5 s after the batch's last invocation (from b2 on), and
invocations were 5 s apart, so every process started on an idle GPU. The
1-minute load average was 1.5 to 3.0; the one process list taken showed a
single process that is not a build using about one CPU core. No other GPU
work was seen at any check: the 54 GPU user clients are the system's
standing clients, the same count at every check, and the utilization at
the checks of the batches used was 0 to 2 %.

| Batch | Mode, invocations | Load (1 min) before, after | GPU utilization before, after | GPU clients before, after | Used |
|---|---|---|---|---|---|
| b1 | build and refit, 5 | 2.29, 2.83 | 2 %, 66 % | 54, 54 | Discarded: the after check ran as the last invocation exited and saw the program's own work (2 % five seconds later) |
| b2 | build and refit, 5 | 3.02, 2.74 | 2 %, 1 % | 54, 54 | Yes |
| b3, b4 | rays open, rays closed, 3 each | 2.60, 1.80 | 0 to 2 %, 1 % | 54, 54 | Superseded by b7 and b8 (same kernels, no second pass); their 36 shared cells agree with b7 and b8 within 0.2 % |
| b5 | MetalFX, 3 | 1.97, 1.53 | 1 %, 1 % | 54, 54 | Superseded by b9: the in-frame scaler overlapped the trace |
| b6 | build and refit, 5 | 1.63, 1.48 | 1 %, 2 % | 54, 54 | Yes |
| b7 | rays open, 3 | 1.48, 1.62 | 0 %, 1 % | 54, 54 | Yes |
| b8 | rays closed, 3 | 1.65, 1.70 | 2 %, 1 % | 54, 54 | Yes |
| b9 | MetalFX, 3 | 1.70, 1.52 | 0 %, 1 % | 54, 54 | Yes |
| b10 | build and refit with the later builds, 5 | 1.58, 1.60 | 2 %, 0 % | 54, 54 | Yes |

**Scene.** Synthetic, so the numbers are throughput of this device, not of
a real map. Quake units, z up.

- World: 20,000 right triangles with 64-unit legs, each in an axis-aligned
  plane chosen uniformly from xy, xz and yz, its right-angle vertex at a
  uniform random integer position such that the triangle lies in the box
  [0, 4096] x [0, 4096] x [0, 1024] (section 1's upper world size). One
  primitive structure, opaque, no face culling. The **open** scene is this
  soup alone. The **closed** scene adds an enclosure of 1,536 triangles,
  the box [-64, 4160] x [-64, 4160] x [-64, 1088] with floor and ceiling
  split into 16 x 16 quads and each wall into 16 x 4 quads (21,536 world
  triangles), so every camera ray hits something, as in a Quake map.
- Alias models: 128 primitive structures of 500 triangles each, the same
  recipe with 16-unit legs in a 64-unit box. All primitive structures are
  built with `MTLAccelerationStructureUsageRefit`.
- Instances: one instance structure of 141 instances (default instance
  descriptors, mask 0xFF, default usage). Instance 0 is the world with the
  identity transform; instances 1 to 140 use alias structure (j mod 128)
  for j = 0 to 139, so 12 structures appear twice (standing in for inline
  brush models), each rotated about z by 2 pi j / 140 and translated to a
  random integer position in [0, 4032] x [0, 4032] x [0, 960].
- Random numbers: xorshift64* with fixed seeds on the CPU; on the GPU a
  PCG hash of the pixel, the ray and the run index.
- Camera at (2048, 2048, 512) looking along +z, x to the right, y up.
  Vertical field of view 73.74 degrees at every aspect (tangent of the
  half angle 0.75, Quake II's `fov 90` at 4:3), horizontal from the
  aspect: 90 degrees at 640 x 480, 106.3 degrees at 16:9.
- Kernels: `intersector<triangle_data, instancing>`, one thread per pixel,
  8 x 8 threadgroups, one dispatch per command buffer, no shading, no
  denoise, no G-buffer reads. **Primary pass**: 1 closest-hit ray per
  pixel through the pixel centre, range 0 to infinity; it stores the hit
  point moved 0.5 units back along the ray, or a miss. **Closest-hit
  pass**: `rpp` rays per pixel through uniform random points of the pixel,
  range 0 to infinity. **Shadow pass**: from each stored hit point `rpp`
  any-hit rays (`accept_any_intersection(true)`) toward a light sample
  uniform in a 256-unit cube centred on (2048, 2048, 912), 400 units ahead
  of the camera, range 0 to the sample's distance; a pixel whose primary
  ray missed casts no shadow ray. The primary pass is timed as its own row;
  the shadow-pass times do not include it.
- Rays cast: a variant of each kernel with an atomic counter (selected by
  a function constant) ran once, untimed, after the timed runs with the
  seed of the first timed run, and counted rays cast and hits or
  occlusions. Open scene: 54.6 % of camera rays hit at 640 x 480 and
  57.3 % at 16:9, so the shadow passes cast rays from that fraction of
  pixels, and 48 % (4:3) or 52 % (16:9) of the cast shadow rays are
  occluded. Closed scene: 100 % of camera rays hit, every pixel casts its
  shadow rays, and 35 % (4:3) or 39 % (16:9) of them are occluded.
- Refit: every refit alternates between two vertex buffers of the same
  topology, the second being the first with every coordinate moved by an
  integer from -2 to 2 (a stand-in animation frame). The 128-structure
  refits are one command buffer with a scratch region per structure. The
  worst-case 128 x 4096 structures use the alias recipe; the 10k to
  1,048,576-triangle soups use the world recipe.
- Order in a process. Build and refit mode: untimed setup (the world and
  the 128 alias builds in one command buffer, then the instance build);
  the instance rebuild and the 128 x 500 refit (**first**); builds of 20k
  (a), 10k, 20k (b), 50k and 200k; refits of 10k, 20k, 50k and 200k;
  untimed builds of 128 x 4096; build of 1,048,576; refits of 128 x 4096
  and of 1,048,576; in b10 only, builds of 20k (a), 10k, 20k (b) and 50k
  again (**later**); the instance rebuild and the 128 x 500 refit again
  (**later**). Ray mode: the same untimed setup; pass 1 at 640 x 480,
  1920 x 1080, 2560 x 1440 and 5120 x 2880, each as primary, closest hit
  1, 2, 4, 8 rpp and shadow 1, 2, 4, 8 rpp; pass 2 at 640 x 480 and
  1920 x 1080 again; the instance rebuild and the refit (later). MetalFX
  mode: the closed scene's setup, every scaler alone, then every scaler
  after a traced frame.

### Ray passes (GPU time in ms, 3 process invocations per scene)

Closed scene, the one the verdicts use. The 640 x 480 and 1920 x 1080
columns are pass 2 (later in the process); 2560 x 1440 and 5120 x 2880 are
pass 1, which had reached the same level by then (pass 1 at 640 x 480 and
1080p is below the tables).

| Pass | rpp | 640 x 480 | 1920 x 1080 | 2560 x 1440 (point size) | 5120 x 2880 (native) |
|---|---|---|---|---|---|
| Primary | 1 | 0.139 (0.138 to 0.140) | 0.878 (0.876 to 1.055) | 1.550 (1.548 to 1.620) | 6.193 (6.186 to 6.394) |
| Closest hit | 1 | 0.140 (0.139 to 0.142) | 0.880 (0.878 to 0.881) | 1.557 (1.555 to 1.558) | 6.216 (6.212 to 6.761) |
| Closest hit | 2 | 0.269 (0.266 to 0.273) | 1.750 (1.747 to 1.756) | 3.104 (3.101 to 3.108) | 12.405 (12.397 to 12.410) |
| Closest hit | 4 | 0.523 (0.518 to 0.573) | 3.476 (3.471 to 3.481) | 6.176 (6.167 to 6.181) | 24.718 (24.707 to 24.923) |
| Closest hit | 8 | 1.032 (1.026 to 1.041) | 6.921 (6.912 to 6.930) | 12.303 (12.293 to 13.135) | 49.307 (49.294 to 50.164) |
| Any-hit shadow, every pixel casting | 1 | 0.093 (0.091 to 0.095) | 0.559 (0.557 to 0.561) | 0.986 (0.984 to 0.990) | 3.917 (3.911 to 3.943) |
| Any-hit shadow, every pixel casting | 2 | 0.160 (0.158 to 0.163) | 1.009 (1.006 to 1.012) | 1.783 (1.780 to 1.786) | 7.114 (7.105 to 7.883) |
| Any-hit shadow, every pixel casting | 4 | 0.299 (0.297 to 0.305) | 1.952 (1.947 to 2.054) | 3.459 (3.454 to 3.724) | 13.855 (13.840 to 13.864) |
| Any-hit shadow, every pixel casting | 8 | 0.580 (0.576 to 0.585) | 3.827 (3.818 to 3.830) | 6.801 (6.795 to 7.537) | 27.284 (27.256 to 28.249) |

Open scene, same columns. The shadow rows cast rays from 54.6 % (640 x
480) or 57.3 % (16:9) of the pixels.

| Pass | rpp | 640 x 480 | 1920 x 1080 | 2560 x 1440 (point size) | 5120 x 2880 (native) |
|---|---|---|---|---|---|
| Primary | 1 | 0.129 (0.128 to 0.130) | 0.851 (0.849 to 0.854) | 1.496 (1.494 to 2.140) | 5.952 (5.943 to 5.957) |
| Closest hit | 1 | 0.129 (0.128 to 0.130) | 0.851 (0.848 to 0.856) | 1.500 (1.498 to 1.506) | 5.968 (5.955 to 6.556) |
| Closest hit | 2 | 0.250 (0.249 to 0.253) | 1.697 (1.690 to 1.704) | 2.993 (2.986 to 3.000) | 11.920 (11.900 to 11.929) |
| Closest hit | 4 | 0.491 (0.488 to 0.493) | 3.370 (3.362 to 3.386) | 5.961 (5.950 to 5.971) | 23.795 (23.759 to 24.110) |
| Closest hit | 8 | 0.971 (0.967 to 0.977) | 6.718 (6.702 to 7.155) | 11.906 (11.880 to 12.259) | 47.570 (47.486 to 47.842) |
| Any-hit shadow, 55 to 57 % of pixels casting | 1 | 0.073 (0.072 to 0.074) | 0.464 (0.462 to 0.568) | 0.824 (0.820 to 1.092) | 3.360 (3.347 to 3.379) |
| Any-hit shadow, 55 to 57 % of pixels casting | 2 | 0.115 (0.113 to 0.116) | 0.734 (0.732 to 0.736) | 1.300 (1.298 to 1.304) | 5.276 (5.266 to 5.290) |
| Any-hit shadow, 55 to 57 % of pixels casting | 4 | 0.204 (0.202 to 0.206) | 1.324 (1.322 to 1.327) | 2.347 (2.343 to 2.355) | 9.438 (9.426 to 10.116) |
| Any-hit shadow, 55 to 57 % of pixels casting | 8 | 0.387 (0.383 to 0.394) | 2.532 (2.529 to 2.538) | 4.489 (4.485 to 4.498) | 17.963 (17.938 to 18.367) |

Throughput per cast ray, 1080p, 8 rpp: closest hit 16.6 M rays in 6.92
ms is 2.40 M rays per ms (closed; open 2.47); any-hit shadow 16.6 M rays
in 3.83 ms is 4.33 M rays per ms (closed). In the open scene the shadow
pass cast 9.5 M rays (57.3 % of the 16.6 M pixel rays) in 2.53 ms, 3.75 M
rays per ms; dividing all 16.6 M pixel rays by the time would give 6.6,
counting rays that were never cast. Native pixels cost 3.97 to 4.08 times
2560 x 1440 (4.0 times the pixels), linear in pixels, and the throughput
at 5120 x 2880 is the same as at 1080p within 2 %.

**Pass 1 (first in the process).** Every 640 x 480 cell of both scenes
was 3.4 to 3.6 times its pass-2 value (closed: primary 0.479, closest 1,
2, 4, 8 rpp 0.483, 0.927, 1.806, 3.566, shadow 0.334, 0.562, 1.044, 2.011
ms), except that the level began to change in the last shadow cells of
two invocations (open shadow 2 rpp invocation median 0.354 ms, closed
shadow 8 rpp 1.491 ms). At 1080p the primary
pass was at the same slow level (3.46 times) in 4 of 6 invocations (open
2.95 ms in 3 of 3, closed 3.03 ms in 1 of 3), and the next cells stepped
down through intermediate levels between about 2.6 and 1.2 times the
pass-2 value (open closest 1 rpp invocation medians 2.21, 1.77, 2.21 ms
and 2 rpp 2.94, 2.50, 2.51 ms against 0.85 and 1.70; closed primary 2.27
and 1.82 ms, closest 1 rpp 1.83, 1.52, 1.30 ms against 0.88). From the
1080p closest-hit 8 rpp configuration on, all six invocations were at the
pass-2 level.

### Acceleration-structure cost (GPU time in ms)

First: 15 invocations of the build and refit mode (b2, b6, b10). The
instance rebuild and the refit later in a process: those 15 plus the 6
ray-mode invocations (21). Later builds: the 5 invocations of b10. "Mid"
rows ran between, after the level had changed (below). Each cell: median
of the invocation medians (lowest to highest invocation median); then
the minimum to maximum over all runs.

| Operation | Timed | Median (invocation medians) | All runs |
|---|---|---|---|
| Instance rebuild, 141 instances | first | 0.617 (0.590 to 0.652) | 0.588 to 0.656 |
| Instance rebuild, 141 instances | later | 0.174 (0.168 to 0.175) | 0.168 to 0.555 |
| Refit 128 x 500 (64,000 triangles), one command buffer | first | 0.691 (0.676 to 0.712) | 0.649 to 0.724 |
| Refit 128 x 500 (64,000 triangles), one command buffer | later | 0.203 (0.202 to 0.206) | 0.200 to 0.436 |
| Build 20k (a) | first | 1.750 (1.677 to 1.770) | 1.672 to 2.547 |
| Build 10k | first, between the two 20k builds | 2.485 (2.452 to 2.508) | 1.832 to 3.544 |
| Build 20k (b) | first | 1.780 (1.372 to 1.799) | 1.281 to 3.030 |
| Build 50k | first | 3.341 (2.385 to 3.397) | 1.925 to 4.291 |
| Build 200k | mid | 3.581 (3.572 to 7.563) | 3.534 to 11.120 |
| Refit 10k | mid | 0.076 (0.075 to 0.130) | 0.075 to 0.239 |
| Refit 20k | mid | 0.084 (0.081 to 0.134) | 0.081 to 0.140 |
| Refit 50k | mid | 0.171 (0.168 to 0.280) | 0.167 to 0.310 |
| Refit 200k | mid | 0.633 (0.612 to 0.851) | 0.605 to 1.099 |
| Build 1,048,576 | mid | 19.842 (19.807 to 20.537) | 19.758 to 22.651 |
| Refit 128 x 4096 (524,288 triangles), one command buffer | mid | 1.674 (1.663 to 1.711) | 1.653 to 2.404 |
| Refit 1,048,576 | mid | 4.041 (4.004 to 4.214) | 3.951 to 5.101 |
| Build 20k (a) | later | 0.502 (0.499 to 0.503) | 0.495 to 0.506 |
| Build 10k | later, between the two 20k builds | 0.714 (0.714 to 0.716) | 0.709 to 0.808 |
| Build 20k (b) | later | 0.508 (0.506 to 0.511) | 0.504 to 0.514 |
| Build 50k | later | 0.988 (0.987 to 0.990) | 0.977 to 0.999 |

The 10k build is slower than the 20k build in the same position: 2.49
against 1.75 and 1.78 ms first, 0.71 against 0.50 and 0.51 ms later
(same recipe, half the triangles in the same box); the cause was not
investigated. The 200k build's first timed run took 4.4 to 6.5 ms and the
rest 3.53 to 3.80 ms in 11 of 15 invocations; in the other 4 its median
was 4.9 to 7.6 ms and the mid refits after it were 5 to 70 % slower than
in the rest (b2 invocations 3 and 5, b6 and b10 invocation 3). Their
batches' checks were clean; the cause was not found.

### Levels at the start of a process

Only process start was observed. In every invocation, the GPU work at the
start of the process ran about 3.5 times slower than the same work later
in the same process: the ray passes (3.45 to 3.6 times), the builds (20k
1.75 against 0.50 ms, 10k 2.49 against 0.71, 50k 3.34 against 0.99) and
the per-frame items (instance rebuild 0.617 against 0.174 ms, 128-alias
refit 0.691 against 0.203). Between the two, the ray passes show a ramp of
intermediate levels (pass 1 above). The start level lasted about 0.1 s of
GPU work made of short dispatches with the CPU waiting between them (the
whole 640 x 480 set in the ray mode, the 20k to 50k builds in the build
mode) and ended during the first configurations whose dispatches take
several milliseconds (the first 1080p ray passes, the 200k build); in some
invocations it ended earlier, during the 20k (b) or 50k builds (their lowest invocation medians,
1.372 and 2.385 ms, are below the medians of 1.780 and
3.341 ms). Each
process started after at least 5 s of GPU idle.

Two hypotheses, neither tested: that this is the GPU's clock or
performance state following the load (no clock was read), and that it
recurs after a map load or any other pause in GPU work (no map load or
idle gap inside a process was measured).

**State the frame budget should plan with.** Plan the steady-state frame
with the later level, the one every configuration reached once the GPU had
done some milliseconds-long dispatches, and allow about 3.5 times for the
first frames of a process (measured) and possibly after a map load
(hypothesis). The one-time map-load build of section 1's 10k to 20k world
triangles costs 0.50 to 0.71 ms at the later level and 1.75 to 2.49 ms at
the start level; 200k triangles cost about 3.6 ms and 1 M about 20 ms.

### MetalFX upscaling to the native size (GPU time in ms, 3 invocations)

`supportsDevice:` is YES on the M6 for `MTLFXSpatialScalerDescriptor`,
`MTLFXTemporalScalerDescriptor`, `MTLFXTemporalDenoisedScalerDescriptor`
and `MTLFXFrameInterpolatorDescriptor` (the frame interpolator was not
timed). The temporal and denoised scalers report a supported input
content scale of 1.0 to 3.0; 1920 x 1080 to 5120 x 2880 is 2.67. Each
scaler: input 1920 x 1080 or 2560 x 1440, output 5120 x 2880; color
`RGBA16Float`, depth `Depth32Float` (a vertical gradient from 0.5 to 0.9,
copied by a blit), motion `RG16Float` (a constant (0.5, -0.25) pixel),
output `RGBA16Float`, all private storage with the usage each scaler
reports; auto exposure off; jitter from the Halton (2, 3) sequence minus
0.5; history reset on the warm-up frame only; the spatial scaler in its
perceptual color mode. The denoised scaler also takes diffuse and
specular albedo (`RGBA16Float`, constant 0.5 and 0.04), normals
(`RGBA16Float`, constant (0, 0, -1)), roughness (`R16Float`, 0.5), the
identity world-to-view matrix and a view-to-clip projection with the
camera's field of view, near 4, far 8192. The inputs are synthetic (a
noise and gradient color, not a traced image), so image quality is not
assessed; only cost is.

"Alone" times each scaler's own command buffer, which holds only its
encode, with the GPU idle between runs. "In frame" commits a traced frame
at the input size first (one command buffer, closed scene: primary,
shadow 4 rpp, shadow 1 rpp, closest hit 2 rpp, closest hit 1 rpp, then a
write of the scaler's color input, so the scaler depends on it) and times
the scaler's own command buffer, the trace's, and the frame from the
trace's start to the scaler's end.

| Scaler, input | Alone | In frame, scaler | Trace before it | Frame, trace start to scaler end |
|---|---|---|---|---|
| Spatial, 1920 x 1080 | 2.191 (1.694 to 4.428) | 1.668, frame minus trace (its command buffer starts with the trace and waits inside) | 6.055 | 7.723 (7.716 to 7.730) |
| Temporal, 1920 x 1080 | 2.004 (1.794 to 2.345) | 1.822 (1.784 to 1.853) | 6.056 | 8.876 (8.834 to 8.928), with a 1.00 ms gap before the scaler |
| Temporal denoised, 1920 x 1080 | 12.734 (12.067 to 15.829) | 12.032 (11.964 to 12.475) | 6.054 | 18.553 (18.475 to 18.985), 0.46 ms gap |
| Spatial, 2560 x 1440 | 1.656 (1.650 to 1.707) | 1.653, frame minus trace | 10.732 | 12.385 (12.373 to 12.419) |
| Temporal, 2560 x 1440 | 2.413 (2.366 to 2.751) | 2.413 (2.377 to 2.481) | 10.732 | 14.638 (14.596 to 15.539), 1.50 ms gap |
| Temporal denoised, 2560 x 1440 | 19.607 (19.373 to 22.121) | 19.497 (19.343 to 20.017) | 10.733 | 31.077 (30.931 to 32.297), 0.84 ms gap |

The traced frame (6.05 ms at 1080p, 10.73 ms at 2560 x 1440, all runs
6.05 to 6.06 and 10.72 to 11.92) matches the sum of the same passes timed
one by one in the closed table within 1 %. The gap between the trace's end
and the temporal and denoised scalers' start (0.45 to 1.55 ms in every
run) was not investigated; the frame column includes it. The alone runs
of the first scaler in each process vary most (spatial 1080p, 1.69 to
4.43 ms), as at every process start above.

### Verdicts against the design

All comparisons are for this device (Apple M6, apple9, hardware ray
tracing), compute-dispatch ray queries on the synthetic closed scene at the
later level unless stated, not for an apple7 device, which this host
cannot measure. "Per-frame items" is the 128 x 500 refit plus the instance
rebuild, 0.38 ms (1.31 ms at the start level).

| Design statement | Measured on the M6 | Met? |
|---|---|---|
| Section 1: IAS rebuild of about 140 instances "is microseconds on every apple6+ GPU" | 0.174 ms later in a process (invocation medians 0.168 to 0.175), 0.617 ms first in a process (0.590 to 0.652) | Met in substance (under a millisecond at both levels), but hundreds of microseconds, not single microseconds. Wording finding for RT1 |
| Section 1: refit of 128 alias instances per frame, "under 1 M triangles, well inside a per-frame compute budget" | 128 x 500 (0.064 M triangles): 0.203 ms later, 0.691 ms first. Worst case 128 x 4096 (0.524 M triangles): 1.674 ms. One structure of 1,048,576 triangles: 4.041 ms. The worst-case points were timed mid-process only | Met on the M6: the worst case is a tenth of a 60 Hz frame, the 1 M bound as one structure a quarter; apple7 not measured |
| Section 1: world structure built once at map load | 10k to 20k triangles: 0.50 to 0.71 ms (later) to 1.75 to 2.49 ms (first); 50k 0.99 to 3.34 ms; 200k about 3.6 ms; 1 M about 20 ms | Met |
| Section 2: dlight shadows, cap 4 rays per pixel | 4 rpp any-hit, every pixel casting: 1.95 ms (1080p), 3.46 ms (2560 x 1440 points), 13.86 ms (5120 x 2880 native) | No time budget of its own; part of the totals below. At native pixels this pass alone is 83 % of a 60 Hz frame |
| Section 2: sun shadow, 1 ray per pixel | 0.56 ms (1080p), 0.99 ms (2560 x 1440), 3.92 ms (native), every pixel casting | No time budget of its own; part of the totals below |
| Section 2: AO, 2 rays per pixel | closest hit 2 rpp from the camera, assumed (not shown) to bound the 128-unit hemisphere rays from above: 1.75 ms (1080p), 3.10 ms (2560 x 1440), 12.41 ms (native) | No time budget of its own; part of the totals below |
| Section 2: water reflections, 1 closest-hit ray on water pixels only | at most 0.88 ms (1080p, every pixel), 1.56 ms (2560 x 1440), 6.22 ms (native), trace only; hit shading not measured | No time budget of its own; part of the totals below |
| Section 2: "about 5 rays per pixel" everything on, assumed 8 to 14 ms at 1080p on apple7 | 4 shadow + 1 sun + 2 AO + 1 reflection: 1.95 + 0.56 + 1.75 + 0.88 = 5.14 ms at 1080p, 3.46 + 0.99 + 3.10 + 1.56 = 9.11 ms at 2560 x 1440, 13.86 + 3.92 + 12.41 + 6.22 at native, which is 36.39 ms as the sum of the unrounded values, plus 0.38 ms per-frame items; trace only, no G-buffer, shading, temporal accumulation or denoise. At the start level, 3.46 times 5.14 ms (the factor measured for the 1080p primary pass and at 640 x 480; the other 1080p passes were not measured at that level) is about 17.8 ms, plus 1.31 ms | Below the estimate on the M6 at the later level (5.5 ms with the per-frame items), above it at the start level (about 19.1 ms). The 8 to 14 ms range is an estimate for apple7, not a budget, so neither is a pass or a fail; apple7 not measured. The apple9 half of section 2's "Total budget" sentence (the render-stage path "is expected to halve it", 4 to 7 ms at 1080p): the render-stage path was not measured; the compute path is already about 5.5 ms, inside the expected 4 to 7 ms |
| Section 6, RT7: everything on at least 60 fps at 1080p on apple7 | ray cost about 5.5 ms of the 16.7 ms frame on the M6 at the later level, about 19.1 ms at the start level (extrapolated as above) | Not decided by this device: apple7 not measured. On the M6 the ray cost alone fits a 60 Hz frame at the later level and not in the first frames of a process |
| Section 6, RT9 (1): native resolution at least the display refresh, everything on, tracing at native pixels | 5120 x 2880 at 60 Hz (16.7 ms): 36.39 ms of rays plus 0.38 ms per-frame items = 36.8 ms | **Not met**: the ray cost alone is 2.2 times the frame; dlight shadows plus sun alone (17.8 ms) are over it. This is the compute-dispatch path; the render-stage path RT9 specifies (resolve-shader ray queries, apple9) was not measured. "Not met" still holds under the design's expected halving (section 2): halving the three passes RT9 moves into the resolve shader (dlight shadows, sun and AO) gives (13.86 + 3.92 + 12.41) / 2 + 6.22 + 0.38 = about 21.7 ms, and halving the whole trace gives 36.39 / 2 + 0.38 = about 18.6 ms, both over 16.7 ms. 120 Hz (8.3 ms frame): not met either, GPU time does not depend on the display. This host's display tops out at 60 Hz, so 60 Hz is RT9's target here. Finding 2 |
| Section 6, RT9 (2): the same, tracing at a lower internal resolution and upscaling to 5120 x 2880 with MetalFX | Temporal scaler: 1080p internal 5.14 + 0.38 + 1.82 = 7.3 ms (9.3 ms as one measured frame, which also holds the 0.88 ms primary pass the design leaves to the rasterizer and the 1.00 ms gap); 2560 x 1440 internal 9.11 + 0.38 + 2.41 = 11.9 ms (15.0 ms measured). Temporal denoised scaler instead: 5.14 + 0.38 + 12.03 = 17.6 ms (18.9 measured) at 1080p, 9.11 + 0.38 + 19.50 = 29.0 ms (31.5 measured) at 2560 x 1440 | With the temporal scaler, met for the ray and upscale cost at 60 Hz: 7.4 to 9.3 ms left at 1080p internal and 1.7 to 4.8 ms at 2560 x 1440 internal for rasterization, shading, temporal accumulation and denoise. With the denoised scaler, not met at either input. The design does not use MetalFX, so this is a finding, not a decision. Compute-dispatch path, render-stage path not measured. At 120 Hz (8.3 ms frame) with the temporal scaler at 1080p internal: met for the ray and upscale cost only by the 7.3 ms sum, with 1.0 ms left for everything else, and not met by the 9.3 ms measured frame; 2560 x 1440 internal (11.9 ms) is not met at 120 Hz; GPU time does not depend on the display, and 60 Hz is the target on this host. Finding 2 |
| Section 6, RT10: resolution-scale defaults per family | The design states no numeric resolution-scale default | Nothing to compare |

The device misses one budget stated numerically in the design: RT9 at
native pixels. Findings for cards (no edit to any decision):

1. RT1, TASK53 (#95): change "microseconds" in section 1 to "a fraction of a
   millisecond" (0.17 ms measured later in a process, about 0.6 ms in the
   first frames of one), and measure real world and alias triangle counts
   instead of relying on the soup.
2. RT9, TASK105 (#248): tracing everything at the native 5120 x 2880
   pixels costs 36.8 ms against the 16.7 ms frame at the display's 60 Hz,
   so RT9 is not met that way. This is the compute-dispatch path; the
   render-stage path RT9 specifies was not measured, and "not met" still
   holds under the design's expected halving (about 21.7 ms halving the
   ray passes, about 18.6 ms halving the whole trace, as in the RT9 (1)
   row). The design (sections 2 and 6) does not use
   MetalFX. That card decides tracing at an internal resolution and
   upscaling to the native size with MetalFX's temporal scaler (measured
   above: 7.3 to 9.3 ms of ray and upscale cost at 1080p internal, 11.9 to
   15.0 ms at 2560 x 1440), and evaluates `MTLFXTemporalDenoisedScaler`
   (12.0 to 19.5 ms in frame here, the "In frame, scaler" column) against section 2's own 5x5
   bilateral denoiser and temporal accumulation on these costs; its outcome
   may change a design decision. Image quality, which this program does not
   assess, is a later measurement on real frames.
3. Repeat on an apple7 Mac, TASK106 (#249): repeat this program on an
   apple7 device (M1 family), including the MetalFX scalers, for RT7 and
   RT10. The scene and order are stated above so it can be rebuilt in
   kind, not bit for bit (the seeds and hash constants are not given), and
   open-scene hit rates may differ by several points; the closed scene's
   100 % camera hit rate, which the verdicts use, is unaffected, though its
   shadow occlusion fraction and times depend on the same geometry and
   seeds. Until then the
   resolution-scale default per family has no measured basis below apple9,
   and the design holds no numeric default to check.
4. RT7 and RT9, TASK59 (#102) and TASK61 (#104): allow for the start-of-process level (about 3.5 times,
   about 0.1 s of GPU work made of short dispatches) so a frame-time test
   does not count the first frames, and test whether it recurs after a
   map load, which is a hypothesis here.

## 9. Image-quality cost of rendering below the native size and upscaling (TASK117)

TASK117 (#272) measures what the stock raster loses when the frame is
rendered below the display's native size and upscaled to it, on real
frames of `ref_gl1` (accelerated OpenGL, the reference renderer) on the
bare-metal host: the Apple M6 of sections 7 and 8, macOS 27.2.1 (26B131).
Every log says `GL_VENDOR: Apple`, `GL_RENDERER: Apple M6` and
`GL_VERSION: 2.1 Metal - 91.7`. Engine source: `main` at `9a38a4e9` (the
build's checkout differed from it only in this document), release build,
`make -j4 client game ref_gl1`. Nothing here is ray traced.

**What this does not measure (the card's scope, item 4).** The frames are
upscaled with `MTLFXSpatialScaler`. Option A (section 2) uses
`MTLFXTemporalScaler`, which needs a jittered projection, depth and motion
vectors from the engine; it is RT11's (TASK109, #263) to measure on real
frames, and this probe does not run it. On a still scene the temporal
scaler usually recovers more detail than the spatial one (the card's
statement, not measured here), so every spatial-scaler number below is a
pessimistic bound for still frames, not a measurement of option A.
Bilinear and Lanczos are baselines. Option E rasterizes at the output, so
this loss does not apply to it; E's own loss, in the effect terms, is not
measured here.

### Frames

The display's pixel size is 5120 x 2880; its desktop size in points is
2560 x 1440 (section 8, "Display"). Every capture is a fresh engine with
`vid_fullscreen 1` at a mode the display lists, `r_mode -1` with
`r_customwidth` and `r_customheight` at the request, the route TASK108
(#259) found for the native size (its re-measurement of the
TASK97 card file (`docs/runbook/TASK97.md`), on TASK108's branch). Each of the 28 logs holds, once
each, the three lines for its size, for example at the native size:

```
User requested 5120x2880@-1.000000, setting closest mode 5120x2880@60.000000
Real display mode: 5120x2880@60.00
Drawable size: 5120x2880
```

and the same lines with `3840x2160`, `2560x1440` or `1920x1080` in the
logs of those sizes; no log holds `Couldn't switch`, `SetVideoMode failed`
or `Reverting to windowed`. All 196 manifest lines name `gl1` and the
run's requested size.

The capture is the pin harness's (`scripts/pins.sh`): the engine
arguments that `PINS_DRYRUN=1 scripts/pins.sh record gl1 <scenario>`
prints, run by hand with `vid_fullscreen 1` in place of 0 and the size
set, as TASK98 did (`docs/runbook/TASK98.md`). So each run plays
`demo1.dm2` from `$PINS_BASEDIR` with the scenario's lines (each sets
`timedemo 1`), with `pins_capture 100` (a frame every 100th rendered
frame), `pins_seed 1` (a fixed PRNG seed at each map registration, so
particles and explosions draw the same numbers in every run) and
`pins_record trace.q2r`. Every run exited 0 with 7 frames and no `FAILED`
line. A display-sleep assertion (`caffeinate -d -i`) was held during each
run.

Scenes: `world` (world geometry, lightmaps, sky and water, no entities),
`models` (the world plus entities: alias models, sprites, beams, the view
weapon) and `particles` (everything on: particles, dynamic lights and the
screen blend over the world and entities), as their scenario files
describe them.

**The screen blend.** `world` and `models` set `cl_blend 0`
(`scripts/pins/scenarios/world.cfg:12`, `models.cfg:16`; `particles.cfg:20`
sets `cl_blend 1`), and that does not turn the blend off: `V_RenderView`
clears only its colour, not the alpha in `blend[3]`.

```
src/client/cl_view.c:569:		if (!cl_add_blend->value)
src/client/cl_view.c:571:			VectorClear(cl.refdef.blend);
src/common/header/shared.h:258:#define VectorClear(a) (a[0] = a[1] = a[2] = 0)
```

`ref_gl1` draws the blend over the 3D frame whenever that alpha is not 0
(`gl_polyblend` is 1 in every trace's cvars), with the colour scaled by
the gamma and the alpha as it is:

```
src/client/refresh/gl1/gl1_main.c:600:	if (!v_blend[3])
src/client/refresh/gl1/gl1_main.c:615:	glColor4f( v_blend[0], v_blend[1], v_blend[2], v_blend[3] );
src/client/refresh/gl1/gl1_main.c:713:		v_blend[i] = r_newrefdef.blend[i] * gl_state.sw_gamma;
src/client/refresh/gl1/gl1_main.c:715:	v_blend[3] = r_newrefdef.blend[3];
```

So in `world` and `models` it draws a black overlay at the alpha the frame
carries. `frametimes.tsv`, whose `blend_a` column is that alpha from each
trace's refdef, gives `blend_a 0.247` on frame 4 and 0.000 on frames 0 to
3, 5 and 6, in every run of the three scenes (9 of `world`, 9 of `models`,
10 of `particles`; the "Blend alpha" column of the table below). The
reader of the sprite counts (Regions, below) also prints the colour: on
frame 4 it is 0.000, 0.000, 0.000 in every `world` and `models` run, and
0.847, 0.698, 0.298 in every `particles` run, where the blend is on. So
frame 4 carries an overlay in all three scenes, a black one in `world` and
`models`. The `particles` colour is, to the demo's byte precision, that
of the pickup flash (`src/game/player/view.c:621`, `SV_AddBlend(0.85,
0.7, 0.3, ent->client->bonus_alpha,`); that is an inference from the
colour. The `hud2d` scenario's comment states the same behaviour
(`scripts/pins/scenarios/hud2d.cfg:15`, "cl_blend 0 clears the colour of
the screen blend but not its alpha, so a pickup still darkens") and that
scenario sets `gl_polyblend 0`; these three do not.

The frame index and the demo time of each frame come from each run's
trace: for every `REC_CAPTURE` record, the refdef of the `REC_RENDERFRAME`
before it (`time`, `vieworg`, `viewangles`), read by a short reader of the
format in `src/client/vid/refrecord.c` (kept with the frames). For each
scene, every run (9 of `world` and of `models`, 10 of `particles`, at
every size and with the 2D layer on and off) gives the same 7 tuples of
capture index, render number, demo time, view origin and view angles, so
the frame numbers match across the sizes. The frames, from the native
2D-on runs (entities, particles and dynamic lights are the refdef's
counts):

| Frame | Render number | Demo time (s) | View origin | View angles (pitch, yaw, roll) | Entities `world` / `models` / `particles` | Particles in the refdef (`particles`) | Dlights (`particles`) | Blend alpha |
|---|---|---|---|---|---|---|---|---|
| 0 | 1 | 19.400 | 832.188, 2292.188, -209.812 | 0.000, -180.000, 0.000 | 0 / 24 / 24 | 0 | 0 | 0.000 |
| 1 | 101 | 29.400 | 602.938, 2353.438, -209.188 | 2.637, -139.081, -1.500 | 0 / 41 / 41 | 60 | 0 | 0.000 |
| 2 | 201 | 39.400 | -74.188, 2313.812, -144.438 | 6.938, 205.735, 0.250 | 0 / 45 / 45 | 40 | 0 | 0.000 |
| 3 | 301 | 49.400 | 379.562, 1909.688, -145.312 | 4.046, -88.923, -1.250 | 0 / 29 / 29 | 400 | 1 | 0.000 |
| 4 | 401 | 59.400 | 168.688, 1983.688, -129.062 | 5.370, 89.275, -1.000 | 0 / 20 / 20 | 17 | 0 | 0.247 |
| 5 | 501 | 69.400 | 128.938, 1270.188, -144.688 | 6.797, 85.320, 1.000 | 0 / 32 / 32 | 430 | 1 | 0.000 |
| 6 | 601 | 79.400 | 240.938, 982.688, 47.062 | 17.405, -6.422, -0.500 | 0 / 28 / 28 | 420 | 1 | 0.000 |

The native 2D-off capture of each scene was taken twice: all 21 frames
are byte-identical between the two runs (`cmp`).

### The 2D layer

A real renderer would draw the 2D layer at the native size after the
upscale, so every frame was captured a second time with the 2D layer off,
at every size, with three of the engine's own cvars, read in the source:
`r_hudscale 0`, which `SCR_GetHUDScale` (`src/client/cl_screen.c`) turns
into a scale of 0 ("HACK: allow scale 0 to hide the HUD"), the scale of
the status bar and layouts (`SCR_ExecuteLayoutString`) and the inventory
(`src/client/cl_inventory.c`); `scr_centertime 0`, the time a centre
print stays up (`SCR_CheckDrawCenterString`); and `con_notifytime -1`, so
`Con_DrawNotify` (`src/client/cl_console.c`) skips every notify line. The
scenarios already set `crosshair 0`, `cl_showfps 0` and `scr_conspeed
99999`. With the 2D layer on, `r_hudscale` is at its default of -1, so the
HUD scale is `SCR_GetDefaultScale`: the smaller of width / 640 and height
/ 240 as an integer, 3, 4, 6 and 8 at 1920 x 1080, 2560 x 1440, 3840 x
2160 and 5120 x 2880 pixels, the same fraction of the frame at all four
sizes. On `world` frame 3 the pixels that differ between the 2D-on and
2D-off captures lie in x 1425 to 3838, y 2689 to 2878 at 5120 x 2880
(179,288 pixels) and in x 534 to 1439, y 1008 to 1079 at 1920 x 1080
(25,883 pixels).

### Regions

No engine buffer (depth, material id) is available for these frames, so
each mask is made from the native captures; every mask is a 5120 x 2880
image kept with the frames.

- **2D and HUD**: the pixels where the native 2D-on and 2D-off captures
  differ in any channel. It is scored on the frames with the 2D layer on.
- **Silhouettes and thin geometry (edge)**: the pixels whose 3 x 3 Sobel
  gradient magnitude on the native 2D-off frame's 8-bit luma is above 64;
  an axis-aligned one-pixel step of 16 luma levels gives exactly 64. The
  one-pixel border is left out and the mask is not dilated. It is a
  gradient mask, not a geometry mask: a texture or lightmap step that
  strong counts as well as a silhouette.
- **Particles**: in the `particles` scene, the pixels where the native
  2D-off capture differs in any channel from a third native capture, also
  with the 2D layer off, with `cl_particles 0`. `V_RenderView`
  (`src/client/cl_view.c`) sets the refdef's particle count to 0 when
  `cl_particles` is 0, after the client has run the particles, so the
  seeded run is otherwise the same. Particle pixels are taken out of the
  edge and interior regions.
- **Textured interiors (interior)**: every other pixel of the 3D frame.
- **Sprites and beams**: none in these frames. The traces record each
  entity's model id and flags (`src/client/vid/refrecord.c:474`,
  `W_U32(w, e->model_id);`, and `:498`, `W_I32(w, e->flags);`), and the
  `REC_MODEL` records name each model id. A second short reader, kept with
  the frames, counts in every captured frame of every run the refdef's
  entities with a sprite model (a name ending in `.sp2`) and those with
  `RF_BEAM` in their flags (`src/common/header/shared.h:763`, `#define
  RF_BEAM 128`): 0 sprites and 0 beams in each of the 196 frames of the 28
  runs. The one sprite model the runs register, `sprites/s_bfg2.sp2`, is
  in no captured frame. The 219 entities of the 7 frames of `models` (and
  of `particles`) are 188 MD2 models and 31 inline brush models (`*1`,
  `*24` and the like), in every run; `world` has none. So the region is
  empty here and is not reported. A frame that had sprites or beams would
  need engine output to isolate them: no cvar removes them alone
  (`cl_entities 0` removes every entity, alias models included). Sky,
  water and the view weapon are not separate regions either.

The 3D regions are scored on the frames with the 2D layer off. Pixels
per mask (the edge, interior and particle counts add up to the frame):

| Scene, frame | Demo time (s) | Edge | Interior | Particles | 2D |
|---|---|---|---|---|---|
| world 0 | 19.400 | 42,675 (0.289 %) | 14,702,925 (99.711 %) | 0 | 122,563 (0.831 %) |
| world 1 | 29.400 | 34,923 (0.237 %) | 14,710,677 (99.763 %) | 0 | 172,246 (1.168 %) |
| world 2 | 39.400 | 141,958 (0.963 %) | 14,603,642 (99.037 %) | 0 | 229,212 (1.554 %) |
| world 3 | 49.400 | 29,809 (0.202 %) | 14,715,791 (99.798 %) | 0 | 179,288 (1.216 %) |
| world 4 | 59.400 | 0 | 14,745,600 (100.000 %) | 0 | 222,844 (1.511 %) |
| world 5 | 69.400 | 12,039 (0.082 %) | 14,733,561 (99.918 %) | 0 | 189,591 (1.286 %) |
| world 6 | 79.400 | 5,013 (0.034 %) | 14,740,587 (99.966 %) | 0 | 178,420 (1.210 %) |
| particles 0 | 19.400 | 96,222 (0.653 %) | 14,649,378 (99.347 %) | 0 | 122,563 (0.831 %) |
| particles 1 | 29.400 | 74,676 (0.506 %) | 14,667,089 (99.468 %) | 3,835 (0.026 %) | 172,246 (1.168 %) |
| particles 2 | 39.400 | 153,322 (1.040 %) | 14,592,278 (98.960 %) | 0 | 229,212 (1.554 %) |
| particles 3 | 49.400 | 54,721 (0.371 %) | 14,685,979 (99.596 %) | 4,900 (0.033 %) | 179,296 (1.216 %) |
| particles 4 | 59.400 | 2,219 (0.015 %) | 14,743,381 (99.985 %) | 0 | 222,844 (1.511 %) |
| particles 5 | 69.400 | 48,579 (0.329 %) | 14,675,812 (99.527 %) | 21,209 (0.144 %) | 189,596 (1.286 %) |
| particles 6 | 79.400 | 50,806 (0.345 %) | 14,654,640 (99.383 %) | 40,154 (0.272 %) | 178,420 (1.210 %) |
| models 0 | 19.400 | 96,222 (0.653 %) | 14,649,378 (99.347 %) | 0 | 122,563 (0.831 %) |
| models 1 | 29.400 | 74,676 (0.506 %) | 14,670,924 (99.494 %) | 0 | 172,246 (1.168 %) |
| models 2 | 39.400 | 153,322 (1.040 %) | 14,592,278 (98.960 %) | 0 | 229,212 (1.554 %) |
| models 3 | 49.400 | 48,002 (0.326 %) | 14,697,598 (99.674 %) | 0 | 179,287 (1.216 %) |
| models 4 | 59.400 | 2,209 (0.015 %) | 14,743,391 (99.985 %) | 0 | 222,844 (1.511 %) |
| models 5 | 69.400 | 26,634 (0.181 %) | 14,718,966 (99.819 %) | 0 | 189,591 (1.286 %) |
| models 6 | 79.400 | 25,312 (0.172 %) | 14,720,288 (99.828 %) | 0 | 178,420 (1.210 %) |

`world` frame 4 has no pixel above the threshold (above 8, 0.108 % of its
pixels), so its edge cells are empty and the edge region has 20 frames.
It is a frame with the black overlay at alpha 0.247 ("The screen blend",
above). An inference, not tested: with the blend function `ref_gl1` sets
by default and restores after its lightmap pass
(`src/client/refresh/gl1/gl1_misc.c:207` and `gl1_surf.c:383`,
`glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);`), the overlay scales
every channel of the 3D frame by 1 - 0.247 = 0.753, and every Sobel
magnitude about as much, so a step of 64 without it is about 48 with it;
0.0006 % of the frame's pixels (under a hundred) are above 48 (the Sobel
output kept with the frames). So the overlay may be why the frame has no
edge pixel rather than a few. Frame 4 of `models` and of `particles`, under
the same alpha, has the fewest edge pixels of its scene (2,209 and 2,219). The particle
region has 4 frames: frames 2 and 4 of `particles` hold 40 and 17
particles in the refdef, but no captured pixel changes with them.

### The probe

A standalone Objective-C program, as TASK101's was (section 8): built with
`xcrun clang` (Apple clang 21.0.0) against the SDK's Foundation, Metal,
MetalFX, MetalPerformanceShaders, CoreGraphics and ImageIO frameworks, no
install, not committed; its source, binary and build line are kept with the
frames. Default `MTLDevice`: Apple M6.

- Each capture's 8-bit RGB goes into an `RGBA8Unorm` private texture (by a
  blit); the output is an `RGBA8Unorm` private texture of 5120 x 2880
  pixels, read back by a blit. All three upscalers resample the stored
  8-bit values (no sRGB decode).
- `MTLFXSpatialScaler`: one per input size, color and output formats
  `RGBA8Unorm`, `MTLFXSpatialScalerColorProcessingModePerceptual`, input
  content size equal to the input size.
- `MPSImageBilinearScale` and `MPSImageLanczosScale`: no `scaleTransform`,
  so "the source image fits exactly into the destination texture"
  (`MPSImageResampling.h`); `edgeMode` `MPSImageEdgeModeClamp`.
- Metrics, against the native capture of the same frame and 2D setting:
  luma Y = round(0.299 R + 0.587 G + 0.114 B) of the 8-bit frames. PSNR =
  10 log10(255² / MSE) over the region's pixels. SSIM as Wang et al.
  (2004) define it: an 11 x 11 Gaussian window with sigma 1.5, K1 0.01,
  K2 0.03, L 255, no downsampling; the map is computed where the window
  lies inside the frame (a 5-pixel border is left out: 14,665,700 of the
  14,745,600 pixels), and a region's SSIM is the mean of the map over the
  window centres inside the region.
- GPU time: `GPUEndTime - GPUStartTime` of a command buffer that holds one
  upscale and nothing else, waited on before the next.

**Self-tests of the metrics** (the probe's `selftest` mode on the native
2D-off frame 3 of each scene; each cell is PSNR, SSIM and the MSE, as
printed):

| Test | `world` frame 3 | `particles` frame 3 | `models` frame 3 |
|---|---|---|---|
| Image against itself | inf, 1.000000000 (MSE 0.000000) | inf, 1.000000000 (MSE 0.000000) | inf, 1.000000000 (MSE 0.000000) |
| Against itself shifted one pixel right (first column repeated) | 50.2455 dB, 0.995359786 (MSE 0.614516) | 44.5192 dB, 0.993084169 (MSE 2.296982) | 46.2451 dB, 0.993873521 (MSE 1.543725) |
| Against itself shifted one pixel down (first row repeated) | 50.1820 dB, 0.995109921 (MSE 0.623557) | 44.6085 dB, 0.993204997 (MSE 2.250257) | 46.1556 dB, 0.993951407 (MSE 1.575863) |
| Luma against luma plus 1 | 48.1308 dB, 0.986518391 (MSE 1.000000) | 48.1308 dB, 0.987023667 (MSE 1.000000) | 48.1308 dB, 0.986711498 (MSE 1.000000) |
| Against the second capture of the same frame | inf, 1.000000000 (MSE 0.000000) | inf, 1.000000000 (MSE 0.000000) | inf, 1.000000000 (MSE 0.000000) |

An image against itself and against a second capture of the same frame
gives infinite PSNR and an SSIM of exactly 1; a one-pixel shift scores
lower on both. Raising every luma value below 255 by 1 gives the MSE of
1.000000 and the 48.1308 dB (10 log10(255²), rounded) that the formula
predicts.

### Results

Every value is computed from the probe's per-frame CSV (972 rows: 21
frames, 3 input sizes, 3 upscalers, the 3D regions and the 2D region; it
also holds the whole frame with the 2D layer on, which no table here
uses). Each upscaled frame goes to 5120 x 2880 pixels.

**Per input size and upscaler, the range over the frames** (PSNR in dB;
SSIM; the number of frames). The spatial-scaler rows are a pessimistic
bound for still frames, not a measurement of option A, which uses the
temporal scaler (RT11; "What this does not measure" above).

| Input (pixels) | Upscaler | 3D, whole frame | 3D, edge | 3D, interior | 3D, particles | 2D and HUD |
|---|---|---|---|---|---|---|
| 3840 x 2160 | spatial | 47.14 to 58.93; 0.9891 to 0.9987; 21 | 23.82 to 35.54; 0.8861 to 0.9750; 20 | 48.41 to 58.93; 0.9892 to 0.9987; 21 | 32.58 to 48.32; 0.9670 to 0.9869; 4 | 35.60 to 44.95; 0.9796 to 0.9870; 21 |
| 3840 x 2160 | bilinear | 49.56 to 63.50; 0.9954 to 0.9995; 21 | 25.20 to 36.01; 0.9030 to 0.9797; 20 | 52.96 to 63.50; 0.9956 to 0.9995; 21 | 32.91 to 50.93; 0.9607 to 0.9929; 4 | 34.39 to 47.55; 0.9841 to 0.9942; 21 |
| 3840 x 2160 | Lanczos | 49.28 to 63.30; 0.9954 to 0.9995; 21 | 25.10 to 36.37; 0.9085 to 0.9837; 20 | 52.90 to 63.30; 0.9956 to 0.9995; 21 | 34.23 to 51.27; 0.9741 to 0.9931; 4 | 34.79 to 47.70; 0.9844 to 0.9941; 21 |
| 2560 x 1440 | spatial | 45.17 to 58.53; 0.9864 to 0.9985; 21 | 23.88 to 32.73; 0.8622 to 0.9467; 20 | 47.23 to 58.53; 0.9868 to 0.9985; 21 | 30.01 to 47.99; 0.9309 to 0.9856; 4 | 27.44 to 39.36; 0.9373 to 0.9709; 21 |
| 2560 x 1440 | bilinear | 46.38 to 61.62; 0.9911 to 0.9993; 21 | 23.95 to 32.09; 0.8319 to 0.9286; 20 | 49.57 to 61.62; 0.9918 to 0.9993; 21 | 29.78 to 49.23; 0.8983 to 0.9896; 4 | 28.37 to 41.23; 0.9482 to 0.9815; 21 |
| 2560 x 1440 | Lanczos | 46.38 to 61.41; 0.9915 to 0.9993; 21 | 23.80 to 32.62; 0.8479 to 0.9462; 20 | 49.74 to 61.41; 0.9921 to 0.9993; 21 | 30.31 to 49.87; 0.9302 to 0.9908; 4 | 28.14 to 41.41; 0.9498 to 0.9836; 21 |
| 1920 x 1080 | spatial | 43.74 to 57.81; 0.9814 to 0.9983; 21 | 22.67 to 30.34; 0.8159 to 0.8984; 20 | 45.39 to 57.81; 0.9823 to 0.9983; 21 | 26.19 to 47.19; 0.8391 to 0.9831; 4 | 33.28 to 41.78; 0.9432 to 0.9619; 21 |
| 1920 x 1080 | bilinear | 44.63 to 61.10; 0.9870 to 0.9992; 21 | 22.85 to 30.90; 0.7749 to 0.8865; 20 | 46.95 to 61.10; 0.9881 to 0.9992; 21 | 26.81 to 47.79; 0.8151 to 0.9859; 4 | 32.46 to 56.52; 0.9724 to 0.9897; 21 |
| 1920 x 1080 | Lanczos | 44.69 to 60.61; 0.9876 to 0.9991; 21 | 22.80 to 30.79; 0.7980 to 0.9024; 20 | 47.40 to 60.61; 0.9885 to 0.9991; 21 | 26.57 to 48.22; 0.8449 to 0.9870; 4 | 32.62 to 48.55; 0.9663 to 0.9852; 21 |

What the CSV shows, and no more:

- Input size: with every upscaler and in every 3D region, every frame
  scores higher from 3840 x 2160 than from 2560 x 1440, and higher from
  2560 x 1440 than from 1920 x 1080, in PSNR and in SSIM (whole frame and
  interior 21 of 21 frames, particles 4 of 4, edge 20 of 20, except the
  spatial scaler's edge PSNR, 19 of 20).
- Regions: the edge region scores below the interior in every comparison
  (180 of 180, PSNR and SSIM): edge 22.67 to 36.37 dB, interior 45.39 to
  63.50 dB over all sizes and upscalers.
- Upscalers, 3D: on the whole frame and the interior, the spatial scaler
  scores below bilinear and below Lanczos in every comparison (63 of 63,
  PSNR and SSIM). On the edge region it does not: its SSIM is above
  bilinear's in 44 of 60 and below Lanczos's in 40 of 60, and Lanczos's
  edge SSIM is above bilinear's in 60 of 60. On the particle region
  Lanczos's SSIM is above bilinear's in 12 of 12 and the spatial scaler's
  below Lanczos's in 11 of 12. Why the spatial scaler scores lower on the
  whole frame was not investigated. PSNR and SSIM measure the difference
  from the native frame pixel by pixel; they do not say which upscaled
  frame looks better, and nothing here ranks the upscalers by look.
- 2D and HUD: the region does not follow the input size. 2560 x 1440
  scores below 1920 x 1080 in PSNR in 19 of 21 frames with each upscaler
  (in SSIM: bilinear 21 of 21, Lanczos 19 of 21, the spatial scaler 3 of
  21). The cause was not investigated.
- Particles: read in the source, not measured: `ref_gl1` draws particles
  as points of `gl1_particle_size` (40) times the frame height divided by
  480 (`src/client/refresh/gl1/gl1_main.c`), attenuated with distance and
  clamped to `gl1_particle_min_size` (2) and `gl1_particle_max_size` (40)
  pixels (`gl1_misc.c`; every log reports `Point parameters: Okay`). An
  inference: a point at the 40-pixel clamp covers the same number of
  pixels at every frame size, so after the upscale it is larger than at
  the native size, and part of the particle region's error may be that
  size difference rather than the upscaler.

**GPU time of one upscale to 5120 x 2880 pixels** (ms, timing set c
below: three process invocations 5 s apart, each with at least 3 s of
untimed upscales first, then per configuration one discarded run and 20
timed runs; the median of the three invocation medians, the lowest to
highest invocation median, then the minimum to maximum of all 60 runs;
the input is the 2D-on `world` frame 3 at each size; "again" is the
3840 x 2160 input timed a second time at the end of each process):

| Input (pixels) | Upscaler | Median | Invocation medians | All runs |
|---|---|---|---|---|
| 3840 x 2160 | spatial | 1.428 | 1.428 to 1.430 | 1.425 to 1.462 (60 runs) |
| 3840 x 2160 | bilinear | 0.192 | 0.191 to 0.193 | 0.190 to 0.199 (60 runs) |
| 3840 x 2160 | Lanczos | 1.236 | 1.235 to 1.238 | 1.229 to 1.293 (60 runs) |
| 2560 x 1440 | spatial | 1.423 | 1.423 to 1.423 | 1.419 to 1.534 (60 runs) |
| 2560 x 1440 | bilinear | 0.182 | 0.182 to 0.183 | 0.181 to 0.184 (60 runs) |
| 2560 x 1440 | Lanczos | 0.861 | 0.860 to 0.861 | 0.854 to 0.885 (60 runs) |
| 1920 x 1080 | spatial | 1.422 | 1.421 to 1.422 | 1.418 to 1.456 (60 runs) |
| 1920 x 1080 | bilinear | 0.180 | 0.180 to 0.180 | 0.179 to 0.181 (60 runs) |
| 1920 x 1080 | Lanczos | 0.993 | 0.992 to 0.995 | 0.991 to 1.131 (60 runs) |
| 3840 x 2160, again | spatial | 1.428 | 1.428 to 1.429 | 1.426 to 1.477 (60 runs) |
| 3840 x 2160, again | bilinear | 0.193 | 0.189 to 0.194 | 0.189 to 0.205 (60 runs) |
| 3840 x 2160, again | Lanczos | 1.236 | 1.236 to 1.238 | 1.229 to 1.360 (60 runs) |

Section 8 timed the spatial scaler on `RGBA16Float` textures with a
synthetic input (its MetalFX table, column "Alone": 2.191 ms from 1920 x
1080, 1.656 ms from 2560 x 1440), so its numbers are not the same
configuration as these.

**GPU time per frame.** The table above times one input, the 2D-on
`world` frame 3. The upscale's GPU time is set by the input and output
sizes and the upscaler, not by the frame's content, to within a few
percent for all but a few frames (below). The per-frame CSV holds one GPU time per upscale (`gpu_ms_once`,
one per frame, input size, upscaler and 2D setting, repeated on the rows
of its regions). Over the 21 frames (ms: minimum to maximum, and the
median; "within 3 %" counts the frames within 3 % of the row's median; r
is the Pearson correlation over the 21 frames between the time and the
share of the native 2D-off frame's pixels with a Sobel magnitude above 8,
from the Sobel output):

| Input (pixels) | Upscaler | 2D off | Within 3 % | r | 2D on | Within 3 % | r |
|---|---|---|---|---|---|---|---|
| 3840 x 2160 | spatial | 4.893 to 5.089 (4.975) | 21 | 0.799 | 4.791 to 4.812 (4.793) | 21 | 0.263 |
| 3840 x 2160 | bilinear | 0.621 to 0.644 (0.627) | 21 | 0.872 | 0.624 to 0.673 (0.634) | 19 | 0.871 |
| 3840 x 2160 | Lanczos | 4.069 to 4.115 (4.073) | 21 | 0.253 | 4.071 to 4.650 (4.073) | 19 | -0.257 |
| 2560 x 1440 | spatial | 4.791 to 4.798 (4.793) | 21 | 0.302 | 4.790 to 4.834 (4.793) | 21 | 0.037 |
| 2560 x 1440 | bilinear | 0.623 to 0.684 (0.635) | 20 | 0.410 | 0.620 to 0.704 (0.636) | 19 | 0.824 |
| 2560 x 1440 | Lanczos | 2.796 to 2.864 (2.800) | 21 | 0.078 | 2.797 to 3.005 (2.800) | 20 | 0.160 |
| 1920 x 1080 | spatial | 4.790 to 4.952 (4.795) | 20 | 0.119 | 4.790 to 5.028 (4.793) | 20 | 0.052 |
| 1920 x 1080 | bilinear | 0.619 to 1.422 (0.619) | 18 | -0.060 | 0.619 to 0.688 (0.619) | 17 | -0.239 |
| 1920 x 1080 | Lanczos | 3.393 to 3.471 (3.394) | 21 | 0.278 | 3.393 to 3.511 (3.396) | 20 | -0.004 |

The frames differ widely (0 to 45 entities, 0 to 430 particles, 0.108 % to
35.170 % of the native pixels with a Sobel magnitude above 8), and 360 of
the 378 times lie within 3 % of their row's median; of the other 18, the
farthest from its row's median (2.3 times it) is 1.422 ms, `models` frame 5 from 1920 x 1080 bilinear with the
2D layer off, 0.619 ms with it on. The same capture
upscaled in two runs (frames 0 and 2 of `models` and of `particles` are
byte-identical at every size, `cmp`) gives times up to 0.159 ms apart with
the 2D layer off and 0.208 ms with it on. Four rows rise with the frame's
detail, within their spread above (r 0.799 to 0.872: 3840 x 2160 spatial
with the 2D layer off, 3840 x 2160 bilinear off and on, 2560 x 1440
bilinear on); why was not investigated. The 2D-off 3840 x 2160 spatial
upscale is the first of each probe process (the probe's loop order), and
its median is above the other spatial rows'. These single times ran in the
metrics batch ("Lock state and load", below), one upscale each between
the CPU's metric computations of the same process and with no warm-up,
under the load that made them unusable as levels and led to the timing
sets: the row medians are 3.25 to 3.49 times set c's. So only their spread
is used here, not their level; the level is set c's, in the table above.

**Per frame.** Each cell is PSNR in dB / SSIM; each scene's "range" row is
the minimum to maximum over its frames, with the number of frames.

3D only (2D off), whole frame:

| Scene, frame | 3840 x 2160, spatial | 3840 x 2160, bilinear | 3840 x 2160, Lanczos | 2560 x 1440, spatial | 2560 x 1440, bilinear | 2560 x 1440, Lanczos | 1920 x 1080, spatial | 1920 x 1080, bilinear | 1920 x 1080, Lanczos |
|---|---|---|---|---|---|---|---|---|---|
| world 0 | 48.10 / 0.9894 | 50.97 / 0.9954 | 50.89 / 0.9954 | 47.05 / 0.9871 | 48.49 / 0.9918 | 48.65 / 0.9922 | 45.53 / 0.9830 | 46.60 / 0.9882 | 46.74 / 0.9888 |
| world 1 | 50.63 / 0.9933 | 54.23 / 0.9974 | 54.07 / 0.9974 | 49.57 / 0.9919 | 51.55 / 0.9954 | 51.64 / 0.9955 | 48.32 / 0.9897 | 49.77 / 0.9935 | 49.97 / 0.9938 |
| world 2 | 47.80 / 0.9894 | 51.98 / 0.9965 | 52.14 / 0.9965 | 46.32 / 0.9868 | 47.93 / 0.9931 | 48.45 / 0.9935 | 44.38 / 0.9821 | 45.28 / 0.9893 | 45.93 / 0.9903 |
| world 3 | 52.69 / 0.9958 | 55.71 / 0.9982 | 55.69 / 0.9982 | 51.20 / 0.9948 | 52.75 / 0.9969 | 52.99 / 0.9970 | 50.16 / 0.9936 | 51.60 / 0.9960 | 51.75 / 0.9961 |
| world 4 | 58.93 / 0.9987 | 63.50 / 0.9995 | 63.30 / 0.9995 | 58.53 / 0.9985 | 61.62 / 0.9993 | 61.41 / 0.9993 | 57.81 / 0.9983 | 61.10 / 0.9992 | 60.61 / 0.9991 |
| world 5 | 52.66 / 0.9953 | 56.40 / 0.9981 | 56.18 / 0.9980 | 51.66 / 0.9942 | 54.12 / 0.9970 | 54.06 / 0.9969 | 50.79 / 0.9932 | 52.95 / 0.9962 | 52.88 / 0.9961 |
| world 6 | 52.36 / 0.9946 | 56.97 / 0.9983 | 56.55 / 0.9981 | 51.49 / 0.9936 | 54.68 / 0.9972 | 54.44 / 0.9970 | 50.53 / 0.9926 | 53.20 / 0.9966 | 53.06 / 0.9964 |
| world range | 47.80 to 58.93 / 0.9894 to 0.9987 (7) | 50.97 to 63.50 / 0.9954 to 0.9995 (7) | 50.89 to 63.30 / 0.9954 to 0.9995 (7) | 46.32 to 58.53 / 0.9868 to 0.9985 (7) | 47.93 to 61.62 / 0.9918 to 0.9993 (7) | 48.45 to 61.41 / 0.9922 to 0.9993 (7) | 44.38 to 57.81 / 0.9821 to 0.9983 (7) | 45.28 to 61.10 / 0.9882 to 0.9992 (7) | 45.93 to 60.61 / 0.9888 to 0.9991 (7) |
| particles 0 | 47.14 / 0.9900 | 49.56 / 0.9955 | 49.44 / 0.9955 | 45.17 / 0.9864 | 46.38 / 0.9911 | 46.38 / 0.9915 | 43.74 / 0.9819 | 44.63 / 0.9870 | 44.69 / 0.9876 |
| particles 1 | 48.66 / 0.9925 | 51.10 / 0.9969 | 50.93 / 0.9968 | 47.31 / 0.9906 | 48.53 / 0.9943 | 48.56 / 0.9945 | 45.85 / 0.9878 | 46.73 / 0.9919 | 46.83 / 0.9922 |
| particles 2 | 47.25 / 0.9891 | 50.67 / 0.9962 | 50.64 / 0.9962 | 45.79 / 0.9864 | 47.11 / 0.9926 | 47.44 / 0.9930 | 43.89 / 0.9814 | 44.66 / 0.9887 | 45.14 / 0.9896 |
| particles 3 | 48.97 / 0.9943 | 50.35 / 0.9976 | 50.10 / 0.9975 | 47.40 / 0.9930 | 47.97 / 0.9959 | 47.84 / 0.9959 | 46.12 / 0.9908 | 46.74 / 0.9946 | 46.60 / 0.9945 |
| particles 4 | 55.34 / 0.9978 | 60.25 / 0.9995 | 59.99 / 0.9994 | 54.95 / 0.9975 | 58.73 / 0.9992 | 58.57 / 0.9991 | 54.34 / 0.9973 | 57.78 / 0.9991 | 57.40 / 0.9990 |
| particles 5 | 48.38 / 0.9932 | 50.04 / 0.9972 | 49.76 / 0.9970 | 47.10 / 0.9916 | 47.98 / 0.9954 | 47.81 / 0.9953 | 45.81 / 0.9893 | 46.64 / 0.9939 | 46.50 / 0.9937 |
| particles 6 | 47.66 / 0.9919 | 49.61 / 0.9972 | 49.28 / 0.9969 | 46.00 / 0.9902 | 47.02 / 0.9954 | 46.78 / 0.9951 | 44.73 / 0.9878 | 45.56 / 0.9940 | 45.41 / 0.9938 |
| particles range | 47.14 to 55.34 / 0.9891 to 0.9978 (7) | 49.56 to 60.25 / 0.9955 to 0.9995 (7) | 49.28 to 59.99 / 0.9955 to 0.9994 (7) | 45.17 to 54.95 / 0.9864 to 0.9975 (7) | 46.38 to 58.73 / 0.9911 to 0.9992 (7) | 46.38 to 58.57 / 0.9915 to 0.9991 (7) | 43.74 to 54.34 / 0.9814 to 0.9973 (7) | 44.63 to 57.78 / 0.9870 to 0.9991 (7) | 44.69 to 57.40 / 0.9876 to 0.9990 (7) |
| models 0 | 47.14 / 0.9900 | 49.56 / 0.9955 | 49.44 / 0.9955 | 45.17 / 0.9864 | 46.38 / 0.9911 | 46.38 / 0.9915 | 43.74 / 0.9819 | 44.63 / 0.9870 | 44.69 / 0.9876 |
| models 1 | 48.66 / 0.9925 | 51.10 / 0.9969 | 50.93 / 0.9968 | 47.31 / 0.9906 | 48.53 / 0.9943 | 48.56 / 0.9945 | 45.85 / 0.9878 | 46.73 / 0.9919 | 46.83 / 0.9922 |
| models 2 | 47.25 / 0.9891 | 50.67 / 0.9962 | 50.64 / 0.9962 | 45.79 / 0.9864 | 47.11 / 0.9926 | 47.44 / 0.9930 | 43.89 / 0.9814 | 44.66 / 0.9887 | 45.14 / 0.9896 |
| models 3 | 50.13 / 0.9950 | 51.81 / 0.9978 | 51.53 / 0.9977 | 48.65 / 0.9939 | 49.39 / 0.9962 | 49.25 / 0.9963 | 47.55 / 0.9921 | 48.29 / 0.9951 | 48.12 / 0.9951 |
| models 4 | 57.36 / 0.9985 | 60.27 / 0.9994 | 60.03 / 0.9994 | 56.91 / 0.9984 | 58.76 / 0.9991 | 58.60 / 0.9991 | 55.86 / 0.9981 | 57.80 / 0.9990 | 57.46 / 0.9989 |
| models 5 | 50.84 / 0.9946 | 53.21 / 0.9976 | 52.92 / 0.9975 | 49.68 / 0.9933 | 51.10 / 0.9961 | 50.93 / 0.9960 | 48.61 / 0.9916 | 49.82 / 0.9948 | 49.67 / 0.9947 |
| models 6 | 50.24 / 0.9938 | 52.85 / 0.9977 | 52.48 / 0.9975 | 48.66 / 0.9925 | 50.12 / 0.9962 | 49.86 / 0.9960 | 47.52 / 0.9909 | 48.71 / 0.9952 | 48.53 / 0.9949 |
| models range | 47.14 to 57.36 / 0.9891 to 0.9985 (7) | 49.56 to 60.27 / 0.9955 to 0.9994 (7) | 49.44 to 60.03 / 0.9955 to 0.9994 (7) | 45.17 to 56.91 / 0.9864 to 0.9984 (7) | 46.38 to 58.76 / 0.9911 to 0.9991 (7) | 46.38 to 58.60 / 0.9915 to 0.9991 (7) | 43.74 to 55.86 / 0.9814 to 0.9981 (7) | 44.63 to 57.80 / 0.9870 to 0.9990 (7) | 44.69 to 57.46 / 0.9876 to 0.9989 (7) |

3D only, edge region (`world` frame 4 has none):

| Scene, frame | 3840 x 2160, spatial | 3840 x 2160, bilinear | 3840 x 2160, Lanczos | 2560 x 1440, spatial | 2560 x 1440, bilinear | 2560 x 1440, Lanczos | 1920 x 1080, spatial | 1920 x 1080, bilinear | 1920 x 1080, Lanczos |
|---|---|---|---|---|---|---|---|---|---|
| world 0 | 29.50 / 0.9195 | 29.90 / 0.9294 | 29.80 / 0.9317 | 28.21 / 0.9104 | 28.31 / 0.8985 | 28.31 / 0.9122 | 26.82 / 0.8473 | 27.19 / 0.8429 | 27.09 / 0.8587 |
| world 1 | 32.64 / 0.9533 | 32.84 / 0.9569 | 32.67 / 0.9583 | 30.79 / 0.9242 | 30.98 / 0.9161 | 30.83 / 0.9248 | 29.74 / 0.8984 | 30.03 / 0.8865 | 29.98 / 0.9024 |
| world 2 | 35.54 / 0.9750 | 36.01 / 0.9797 | 36.37 / 0.9837 | 32.73 / 0.9467 | 32.09 / 0.9286 | 32.62 / 0.9462 | 30.34 / 0.8934 | 29.73 / 0.8628 | 30.38 / 0.8953 |
| world 3 | 33.37 / 0.9571 | 33.91 / 0.9600 | 33.80 / 0.9662 | 30.40 / 0.9232 | 30.84 / 0.9092 | 30.82 / 0.9345 | 29.76 / 0.8678 | 30.16 / 0.8556 | 30.18 / 0.8841 |
| world 4 | – | – | – | – | – | – | – | – | – |
| world 5 | 32.25 / 0.9434 | 33.02 / 0.9505 | 32.79 / 0.9523 | 30.80 / 0.9094 | 31.63 / 0.9092 | 31.52 / 0.9192 | 30.04 / 0.8821 | 30.90 / 0.8807 | 30.79 / 0.8936 |
| world 6 | 30.71 / 0.9320 | 30.67 / 0.9254 | 30.47 / 0.9266 | 29.75 / 0.9014 | 29.66 / 0.8827 | 29.47 / 0.8901 | 28.94 / 0.8797 | 28.94 / 0.8513 | 28.77 / 0.8657 |
| world range | 29.50 to 35.54 / 0.9195 to 0.9750 (6) | 29.90 to 36.01 / 0.9254 to 0.9797 (6) | 29.80 to 36.37 / 0.9266 to 0.9837 (6) | 28.21 to 32.73 / 0.9014 to 0.9467 (6) | 28.31 to 32.09 / 0.8827 to 0.9286 (6) | 28.31 to 32.62 / 0.8901 to 0.9462 (6) | 26.82 to 30.34 / 0.8473 to 0.8984 (6) | 27.19 to 30.90 / 0.8429 to 0.8865 (6) | 27.09 to 30.79 / 0.8587 to 0.9024 (6) |
| particles 0 | 29.72 / 0.9394 | 30.16 / 0.9468 | 30.11 / 0.9497 | 26.80 / 0.8917 | 27.34 / 0.8898 | 27.19 / 0.9020 | 25.72 / 0.8245 | 26.24 / 0.8271 | 26.13 / 0.8451 |
| particles 1 | 30.12 / 0.9464 | 30.36 / 0.9490 | 30.20 / 0.9529 | 28.38 / 0.9088 | 28.37 / 0.8964 | 28.31 / 0.9101 | 27.06 / 0.8659 | 27.08 / 0.8499 | 27.07 / 0.8708 |
| particles 2 | 33.57 / 0.9713 | 33.56 / 0.9749 | 33.58 / 0.9786 | 31.34 / 0.9432 | 30.61 / 0.9241 | 30.86 / 0.9410 | 29.28 / 0.8915 | 28.65 / 0.8599 | 29.04 / 0.8911 |
| particles 3 | 27.71 / 0.9470 | 27.29 / 0.9445 | 27.09 / 0.9491 | 25.69 / 0.9149 | 25.25 / 0.8952 | 25.09 / 0.9142 | 24.87 / 0.8712 | 24.40 / 0.8497 | 24.29 / 0.8731 |
| particles 4 | 23.82 / 0.8861 | 25.20 / 0.9058 | 25.10 / 0.9115 | 23.88 / 0.8718 | 24.39 / 0.8584 | 24.63 / 0.8781 | 22.67 / 0.8318 | 23.13 / 0.8092 | 23.19 / 0.8382 |
| particles 5 | 26.77 / 0.9362 | 26.58 / 0.9378 | 26.37 / 0.9408 | 25.42 / 0.9070 | 24.95 / 0.8932 | 24.83 / 0.9067 | 24.35 / 0.8689 | 23.99 / 0.8522 | 23.90 / 0.8720 |
| particles 6 | 26.27 / 0.9325 | 26.14 / 0.9334 | 25.95 / 0.9377 | 24.30 / 0.8797 | 23.95 / 0.8605 | 23.80 / 0.8763 | 23.23 / 0.8254 | 22.85 / 0.7989 | 22.80 / 0.8209 |
| particles range | 23.82 to 33.57 / 0.8861 to 0.9713 (7) | 25.20 to 33.56 / 0.9058 to 0.9749 (7) | 25.10 to 33.58 / 0.9115 to 0.9786 (7) | 23.88 to 31.34 / 0.8718 to 0.9432 (7) | 23.95 to 30.61 / 0.8584 to 0.9241 (7) | 23.80 to 30.86 / 0.8763 to 0.9410 (7) | 22.67 to 29.28 / 0.8245 to 0.8915 (7) | 22.85 to 28.65 / 0.7989 to 0.8599 (7) | 22.80 to 29.04 / 0.8209 to 0.8911 (7) |
| models 0 | 29.72 / 0.9394 | 30.16 / 0.9468 | 30.11 / 0.9497 | 26.80 / 0.8917 | 27.34 / 0.8898 | 27.19 / 0.9020 | 25.72 / 0.8245 | 26.24 / 0.8271 | 26.13 / 0.8451 |
| models 1 | 30.12 / 0.9464 | 30.36 / 0.9490 | 30.20 / 0.9529 | 28.38 / 0.9088 | 28.37 / 0.8964 | 28.31 / 0.9101 | 27.06 / 0.8659 | 27.08 / 0.8499 | 27.07 / 0.8708 |
| models 2 | 33.57 / 0.9713 | 33.56 / 0.9749 | 33.58 / 0.9786 | 31.34 / 0.9432 | 30.61 / 0.9241 | 30.86 / 0.9410 | 29.28 / 0.8915 | 28.65 / 0.8599 | 29.04 / 0.8911 |
| models 3 | 28.69 / 0.9467 | 28.48 / 0.9454 | 28.25 / 0.9499 | 26.76 / 0.9143 | 26.42 / 0.8956 | 26.22 / 0.9152 | 26.03 / 0.8721 | 25.63 / 0.8507 | 25.47 / 0.8743 |
| models 4 | 25.25 / 0.9171 | 25.21 / 0.9030 | 25.12 / 0.9085 | 24.89 / 0.8888 | 24.41 / 0.8531 | 24.64 / 0.8715 | 23.23 / 0.8400 | 23.16 / 0.8018 | 23.23 / 0.8270 |
| models 5 | 28.57 / 0.9318 | 28.47 / 0.9312 | 28.27 / 0.9337 | 27.14 / 0.8994 | 26.92 / 0.8846 | 26.78 / 0.8972 | 26.29 / 0.8679 | 26.13 / 0.8470 | 26.04 / 0.8653 |
| models 6 | 27.57 / 0.9260 | 27.42 / 0.9198 | 27.21 / 0.9233 | 25.47 / 0.8622 | 25.20 / 0.8319 | 25.01 / 0.8479 | 24.62 / 0.8159 | 24.37 / 0.7749 | 24.26 / 0.7980 |
| models range | 25.25 to 33.57 / 0.9171 to 0.9713 (7) | 25.21 to 33.56 / 0.9030 to 0.9749 (7) | 25.12 to 33.58 / 0.9085 to 0.9786 (7) | 24.89 to 31.34 / 0.8622 to 0.9432 (7) | 24.41 to 30.61 / 0.8319 to 0.9241 (7) | 24.64 to 30.86 / 0.8479 to 0.9410 (7) | 23.23 to 29.28 / 0.8159 to 0.8915 (7) | 23.16 to 28.65 / 0.7749 to 0.8599 (7) | 23.23 to 29.04 / 0.7980 to 0.8911 (7) |

3D only, interior:

| Scene, frame | 3840 x 2160, spatial | 3840 x 2160, bilinear | 3840 x 2160, Lanczos | 2560 x 1440, spatial | 2560 x 1440, bilinear | 2560 x 1440, Lanczos | 1920 x 1080, spatial | 1920 x 1080, bilinear | 1920 x 1080, Lanczos |
|---|---|---|---|---|---|---|---|---|---|
| world 0 | 49.10 / 0.9896 | 52.96 / 0.9956 | 52.90 / 0.9956 | 48.13 / 0.9873 | 50.04 / 0.9921 | 50.27 / 0.9925 | 46.58 / 0.9834 | 47.85 / 0.9886 | 48.08 / 0.9892 |
| world 1 | 51.32 / 0.9934 | 55.93 / 0.9975 | 55.78 / 0.9974 | 50.41 / 0.9920 | 52.90 / 0.9956 | 53.10 / 0.9957 | 49.13 / 0.9899 | 50.86 / 0.9938 | 51.14 / 0.9940 |
| world 2 | 48.53 / 0.9896 | 54.02 / 0.9967 | 54.06 / 0.9966 | 47.35 / 0.9872 | 49.88 / 0.9937 | 50.41 / 0.9940 | 45.55 / 0.9830 | 47.08 / 0.9905 | 47.72 / 0.9912 |
| world 3 | 53.50 / 0.9959 | 57.29 / 0.9983 | 57.31 / 0.9982 | 52.40 / 0.9950 | 54.38 / 0.9971 | 54.75 / 0.9971 | 51.24 / 0.9938 | 53.03 / 0.9963 | 53.23 / 0.9963 |
| world 4 | 58.93 / 0.9987 | 63.50 / 0.9995 | 63.30 / 0.9995 | 58.53 / 0.9985 | 61.62 / 0.9993 | 61.41 / 0.9993 | 57.81 / 0.9983 | 61.10 / 0.9992 | 60.61 / 0.9991 |
| world 5 | 53.07 / 0.9953 | 57.25 / 0.9982 | 57.02 / 0.9981 | 52.11 / 0.9943 | 54.79 / 0.9970 | 54.74 / 0.9970 | 51.23 / 0.9933 | 53.55 / 0.9963 | 53.49 / 0.9962 |
| world 6 | 52.58 / 0.9946 | 57.65 / 0.9983 | 57.20 / 0.9981 | 51.71 / 0.9937 | 55.18 / 0.9973 | 54.93 / 0.9971 | 50.75 / 0.9926 | 53.61 / 0.9967 | 53.47 / 0.9965 |
| world range | 48.53 to 58.93 / 0.9896 to 0.9987 (7) | 52.96 to 63.50 / 0.9956 to 0.9995 (7) | 52.90 to 63.30 / 0.9956 to 0.9995 (7) | 47.35 to 58.53 / 0.9872 to 0.9985 (7) | 49.88 to 61.62 / 0.9921 to 0.9993 (7) | 50.27 to 61.41 / 0.9925 to 0.9993 (7) | 45.55 to 57.81 / 0.9830 to 0.9983 (7) | 47.08 to 61.10 / 0.9886 to 0.9992 (7) | 47.72 to 60.61 / 0.9892 to 0.9991 (7) |
| particles 0 | 49.05 / 0.9903 | 53.19 / 0.9959 | 52.97 / 0.9958 | 47.73 / 0.9870 | 49.57 / 0.9918 | 49.74 / 0.9921 | 46.03 / 0.9829 | 47.21 / 0.9881 | 47.40 / 0.9885 |
| particles 1 | 50.59 / 0.9927 | 55.06 / 0.9971 | 54.87 / 0.9970 | 49.47 / 0.9910 | 51.75 / 0.9948 | 51.87 / 0.9949 | 47.92 / 0.9884 | 49.44 / 0.9926 | 49.65 / 0.9928 |
| particles 2 | 48.41 / 0.9892 | 53.95 / 0.9965 | 53.87 / 0.9963 | 47.23 / 0.9868 | 49.78 / 0.9934 | 50.19 / 0.9936 | 45.39 / 0.9823 | 46.95 / 0.9901 | 47.49 / 0.9906 |
| particles 3 | 52.06 / 0.9945 | 56.69 / 0.9978 | 56.18 / 0.9977 | 51.02 / 0.9933 | 53.43 / 0.9963 | 53.31 / 0.9963 | 49.37 / 0.9913 | 51.54 / 0.9952 | 51.32 / 0.9950 |
| particles 4 | 56.38 / 0.9978 | 63.09 / 0.9995 | 62.70 / 0.9995 | 55.88 / 0.9975 | 61.02 / 0.9992 | 60.59 / 0.9992 | 55.43 / 0.9973 | 60.28 / 0.9991 | 59.60 / 0.9990 |
| particles 5 | 51.26 / 0.9934 | 56.06 / 0.9974 | 55.43 / 0.9972 | 50.11 / 0.9919 | 53.04 / 0.9958 | 52.64 / 0.9957 | 48.70 / 0.9897 | 51.07 / 0.9944 | 50.75 / 0.9942 |
| particles 6 | 50.49 / 0.9921 | 56.11 / 0.9975 | 55.26 / 0.9972 | 49.16 / 0.9906 | 52.43 / 0.9959 | 51.91 / 0.9956 | 47.70 / 0.9885 | 50.18 / 0.9948 | 49.83 / 0.9945 |
| particles range | 48.41 to 56.38 / 0.9892 to 0.9978 (7) | 53.19 to 63.09 / 0.9959 to 0.9995 (7) | 52.97 to 62.70 / 0.9958 to 0.9995 (7) | 47.23 to 55.88 / 0.9868 to 0.9975 (7) | 49.57 to 61.02 / 0.9918 to 0.9992 (7) | 49.74 to 60.59 / 0.9921 to 0.9992 (7) | 45.39 to 55.43 / 0.9823 to 0.9973 (7) | 46.95 to 60.28 / 0.9881 to 0.9991 (7) | 47.40 to 59.60 / 0.9885 to 0.9990 (7) |
| models 0 | 49.05 / 0.9903 | 53.19 / 0.9959 | 52.97 / 0.9958 | 47.73 / 0.9870 | 49.57 / 0.9918 | 49.74 / 0.9921 | 46.03 / 0.9829 | 47.21 / 0.9881 | 47.40 / 0.9885 |
| models 1 | 50.59 / 0.9927 | 55.06 / 0.9971 | 54.87 / 0.9970 | 49.47 / 0.9910 | 51.75 / 0.9948 | 51.87 / 0.9949 | 47.92 / 0.9884 | 49.44 / 0.9926 | 49.65 / 0.9928 |
| models 2 | 48.41 / 0.9892 | 53.95 / 0.9965 | 53.87 / 0.9963 | 47.23 / 0.9868 | 49.78 / 0.9934 | 50.19 / 0.9936 | 45.39 / 0.9823 | 46.95 / 0.9901 | 47.49 / 0.9906 |
| models 3 | 52.74 / 0.9952 | 57.01 / 0.9979 | 56.65 / 0.9979 | 51.67 / 0.9941 | 53.86 / 0.9965 | 53.86 / 0.9966 | 50.22 / 0.9925 | 52.24 / 0.9956 | 52.08 / 0.9955 |
| models 4 | 58.58 / 0.9985 | 63.10 / 0.9995 | 62.74 / 0.9994 | 58.09 / 0.9984 | 61.04 / 0.9992 | 60.63 / 0.9991 | 57.26 / 0.9981 | 60.29 / 0.9991 | 59.65 / 0.9990 |
| models 5 | 52.41 / 0.9947 | 56.57 / 0.9977 | 56.16 / 0.9976 | 51.37 / 0.9934 | 53.87 / 0.9963 | 53.67 / 0.9962 | 50.20 / 0.9918 | 52.20 / 0.9951 | 52.00 / 0.9950 |
| models 6 | 51.89 / 0.9939 | 56.81 / 0.9979 | 56.21 / 0.9977 | 50.58 / 0.9927 | 53.41 / 0.9965 | 53.08 / 0.9962 | 49.29 / 0.9912 | 51.43 / 0.9955 | 51.19 / 0.9953 |
| models range | 48.41 to 58.58 / 0.9892 to 0.9985 (7) | 53.19 to 63.10 / 0.9959 to 0.9995 (7) | 52.97 to 62.74 / 0.9958 to 0.9994 (7) | 47.23 to 58.09 / 0.9868 to 0.9984 (7) | 49.57 to 61.04 / 0.9918 to 0.9992 (7) | 49.74 to 60.63 / 0.9921 to 0.9991 (7) | 45.39 to 57.26 / 0.9823 to 0.9981 (7) | 46.95 to 60.29 / 0.9881 to 0.9991 (7) | 47.40 to 59.65 / 0.9885 to 0.9990 (7) |

3D only, particle region (`particles`, the frames that have one):

| Scene, frame | 3840 x 2160, spatial | 3840 x 2160, bilinear | 3840 x 2160, Lanczos | 2560 x 1440, spatial | 2560 x 1440, bilinear | 2560 x 1440, Lanczos | 1920 x 1080, spatial | 1920 x 1080, bilinear | 1920 x 1080, Lanczos |
|---|---|---|---|---|---|---|---|---|---|
| particles 1 | 48.32 / 0.9869 | 50.93 / 0.9929 | 51.27 / 0.9931 | 47.99 / 0.9856 | 49.23 / 0.9896 | 49.87 / 0.9908 | 47.19 / 0.9831 | 47.79 / 0.9859 | 48.22 / 0.9870 |
| particles 3 | 32.58 / 0.9670 | 32.91 / 0.9607 | 34.23 / 0.9741 | 30.01 / 0.9309 | 29.78 / 0.8983 | 30.31 / 0.9302 | 26.19 / 0.8391 | 26.81 / 0.8151 | 26.57 / 0.8449 |
| particles 5 | 39.70 / 0.9849 | 38.62 / 0.9826 | 40.74 / 0.9882 | 36.26 / 0.9690 | 35.04 / 0.9572 | 36.79 / 0.9698 | 33.17 / 0.9439 | 32.69 / 0.9325 | 33.86 / 0.9471 |
| particles 6 | 43.54 / 0.9824 | 43.54 / 0.9842 | 45.15 / 0.9885 | 40.69 / 0.9687 | 39.83 / 0.9623 | 41.25 / 0.9720 | 38.45 / 0.9469 | 37.89 / 0.9408 | 39.01 / 0.9525 |
| particles range | 32.58 to 48.32 / 0.9670 to 0.9869 (4) | 32.91 to 50.93 / 0.9607 to 0.9929 (4) | 34.23 to 51.27 / 0.9741 to 0.9931 (4) | 30.01 to 47.99 / 0.9309 to 0.9856 (4) | 29.78 to 49.23 / 0.8983 to 0.9896 (4) | 30.31 to 49.87 / 0.9302 to 0.9908 (4) | 26.19 to 47.19 / 0.8391 to 0.9831 (4) | 26.81 to 47.79 / 0.8151 to 0.9859 (4) | 26.57 to 48.22 / 0.8449 to 0.9870 (4) |

2D and HUD region (2D on):

| Scene, frame | 3840 x 2160, spatial | 3840 x 2160, bilinear | 3840 x 2160, Lanczos | 2560 x 1440, spatial | 2560 x 1440, bilinear | 2560 x 1440, Lanczos | 1920 x 1080, spatial | 1920 x 1080, bilinear | 1920 x 1080, Lanczos |
|---|---|---|---|---|---|---|---|---|---|
| world 0 | 42.00 / 0.9842 | 42.40 / 0.9908 | 42.78 / 0.9910 | 34.92 / 0.9575 | 36.08 / 0.9695 | 36.03 / 0.9719 | 40.51 / 0.9530 | 55.02 / 0.9828 | 47.12 / 0.9785 |
| world 1 | 44.95 / 0.9870 | 47.55 / 0.9942 | 47.70 / 0.9941 | 39.36 / 0.9703 | 41.23 / 0.9815 | 41.41 / 0.9836 | 41.02 / 0.9619 | 56.25 / 0.9897 | 47.45 / 0.9852 |
| world 2 | 40.77 / 0.9824 | 40.40 / 0.9888 | 40.85 / 0.9891 | 33.30 / 0.9515 | 34.39 / 0.9638 | 34.32 / 0.9657 | 40.65 / 0.9512 | 52.09 / 0.9811 | 46.31 / 0.9755 |
| world 3 | 41.81 / 0.9843 | 41.69 / 0.9902 | 42.13 / 0.9904 | 34.65 / 0.9589 | 35.74 / 0.9699 | 35.69 / 0.9720 | 40.79 / 0.9544 | 52.19 / 0.9828 | 46.53 / 0.9778 |
| world 4 | 40.62 / 0.9865 | 39.47 / 0.9939 | 39.09 / 0.9926 | 35.05 / 0.9709 | 33.38 / 0.9784 | 33.54 / 0.9787 | 33.28 / 0.9613 | 32.46 / 0.9816 | 32.62 / 0.9771 |
| world 5 | 44.16 / 0.9862 | 45.30 / 0.9923 | 45.57 / 0.9921 | 37.63 / 0.9638 | 38.92 / 0.9739 | 38.91 / 0.9759 | 41.78 / 0.9599 | 56.52 / 0.9854 | 48.55 / 0.9817 |
| world 6 | 43.44 / 0.9856 | 44.52 / 0.9924 | 44.79 / 0.9924 | 37.04 / 0.9643 | 38.19 / 0.9751 | 38.17 / 0.9770 | 41.26 / 0.9589 | 55.66 / 0.9859 | 47.93 / 0.9822 |
| world range | 40.62 to 44.95 / 0.9824 to 0.9870 (7) | 39.47 to 47.55 / 0.9888 to 0.9942 (7) | 39.09 to 47.70 / 0.9891 to 0.9941 (7) | 33.30 to 39.36 / 0.9515 to 0.9709 (7) | 33.38 to 41.23 / 0.9638 to 0.9815 (7) | 33.54 to 41.41 / 0.9657 to 0.9836 (7) | 33.28 to 41.78 / 0.9512 to 0.9619 (7) | 32.46 to 56.52 / 0.9811 to 0.9897 (7) | 32.62 to 48.55 / 0.9755 to 0.9852 (7) |
| particles 0 | 42.02 / 0.9843 | 42.41 / 0.9909 | 42.79 / 0.9910 | 34.97 / 0.9579 | 36.09 / 0.9696 | 36.05 / 0.9720 | 40.52 / 0.9530 | 55.02 / 0.9829 | 47.13 / 0.9786 |
| particles 1 | 44.66 / 0.9868 | 47.16 / 0.9941 | 47.26 / 0.9940 | 39.00 / 0.9698 | 40.78 / 0.9811 | 40.88 / 0.9831 | 40.99 / 0.9617 | 56.24 / 0.9895 | 47.40 / 0.9850 |
| particles 2 | 40.96 / 0.9832 | 40.73 / 0.9898 | 41.11 / 0.9899 | 33.58 / 0.9553 | 34.69 / 0.9676 | 34.58 / 0.9694 | 40.76 / 0.9531 | 52.02 / 0.9821 | 46.35 / 0.9772 |
| particles 3 | 35.60 / 0.9796 | 34.39 / 0.9841 | 34.79 / 0.9844 | 27.44 / 0.9395 | 28.37 / 0.9502 | 28.14 / 0.9515 | 40.01 / 0.9432 | 46.78 / 0.9731 | 42.89 / 0.9665 |
| particles 4 | 39.48 / 0.9821 | 38.42 / 0.9877 | 38.53 / 0.9872 | 32.42 / 0.9509 | 32.45 / 0.9592 | 32.51 / 0.9603 | 35.04 / 0.9476 | 34.91 / 0.9724 | 34.97 / 0.9663 |
| particles 5 | 37.10 / 0.9800 | 36.16 / 0.9846 | 36.58 / 0.9850 | 29.01 / 0.9373 | 29.91 / 0.9482 | 29.70 / 0.9498 | 41.23 / 0.9454 | 51.83 / 0.9729 | 45.94 / 0.9676 |
| particles 6 | 39.24 / 0.9819 | 38.50 / 0.9873 | 38.96 / 0.9878 | 31.56 / 0.9475 | 32.35 / 0.9581 | 32.22 / 0.9601 | 40.93 / 0.9489 | 52.20 / 0.9781 | 46.42 / 0.9730 |
| particles range | 35.60 to 44.66 / 0.9796 to 0.9868 (7) | 34.39 to 47.16 / 0.9841 to 0.9941 (7) | 34.79 to 47.26 / 0.9844 to 0.9940 (7) | 27.44 to 39.00 / 0.9373 to 0.9698 (7) | 28.37 to 40.78 / 0.9482 to 0.9811 (7) | 28.14 to 40.88 / 0.9498 to 0.9831 (7) | 35.04 to 41.23 / 0.9432 to 0.9617 (7) | 34.91 to 56.24 / 0.9724 to 0.9895 (7) | 34.97 to 47.40 / 0.9663 to 0.9850 (7) |
| models 0 | 42.02 / 0.9843 | 42.41 / 0.9909 | 42.79 / 0.9910 | 34.97 / 0.9579 | 36.09 / 0.9696 | 36.05 / 0.9720 | 40.52 / 0.9530 | 55.02 / 0.9829 | 47.13 / 0.9786 |
| models 1 | 44.66 / 0.9868 | 47.16 / 0.9941 | 47.26 / 0.9940 | 39.00 / 0.9698 | 40.78 / 0.9811 | 40.88 / 0.9831 | 40.99 / 0.9617 | 56.24 / 0.9895 | 47.40 / 0.9850 |
| models 2 | 40.96 / 0.9832 | 40.73 / 0.9898 | 41.11 / 0.9899 | 33.58 / 0.9553 | 34.69 / 0.9676 | 34.58 / 0.9694 | 40.76 / 0.9531 | 52.02 / 0.9821 | 46.35 / 0.9772 |
| models 3 | 38.48 / 0.9824 | 37.59 / 0.9879 | 37.98 / 0.9882 | 30.60 / 0.9513 | 31.55 / 0.9620 | 31.36 / 0.9637 | 40.52 / 0.9501 | 49.73 / 0.9795 | 44.76 / 0.9735 |
| models 4 | 40.57 / 0.9864 | 39.45 / 0.9939 | 39.07 / 0.9926 | 34.97 / 0.9708 | 33.35 / 0.9784 | 33.51 / 0.9787 | 33.28 / 0.9613 | 32.46 / 0.9816 | 32.62 / 0.9771 |
| models 5 | 43.85 / 0.9860 | 44.76 / 0.9920 | 45.08 / 0.9919 | 37.23 / 0.9629 | 38.46 / 0.9729 | 38.43 / 0.9748 | 41.74 / 0.9595 | 56.50 / 0.9851 | 48.50 / 0.9813 |
| models 6 | 43.40 / 0.9856 | 44.43 / 0.9923 | 44.75 / 0.9924 | 37.05 / 0.9642 | 38.14 / 0.9748 | 38.14 / 0.9768 | 41.25 / 0.9588 | 55.65 / 0.9859 | 47.93 / 0.9821 |
| models range | 38.48 to 44.66 / 0.9824 to 0.9868 (7) | 37.59 to 47.16 / 0.9879 to 0.9941 (7) | 37.98 to 47.26 / 0.9882 to 0.9940 (7) | 30.60 to 39.00 / 0.9513 to 0.9708 (7) | 31.55 to 40.78 / 0.9620 to 0.9811 (7) | 31.36 to 40.88 / 0.9637 to 0.9831 (7) | 33.28 to 41.74 / 0.9501 to 0.9617 (7) | 32.46 to 56.50 / 0.9795 to 0.9895 (7) | 32.62 to 48.50 / 0.9735 to 0.9850 (7) |

### Lock state and load

Before every capture, every metrics run and every timing invocation,
`ioreg -n Root -d1 | grep -c CGSSessionScreenIsLocked` printed 0,
`pgrep -x quake2` listed no engine (none of the runs overlapped another),
and `uptime` was noted. Before and after each timing invocation the
number of `make`, `cc`, `clang`, `quake2` and `q2ded` processes was also
read (0 at every check) and, from timing set b on, the GPU's
`"Device Utilization %"` (`ioreg -r -c IOAccelerator -d 1 -w0`). The
screen was never locked. Times are UTC on 2026-11-15 (for the captures
and the metrics runs, the first and last run's start); load is the
one-minute average.

| Batch | Time | Runs | Lock before, after | Load before | GPU utilization before, after | Used |
|---|---|---|---|---|---|---|
| Captures | 04:41:54 to 04:49:17 | 28 engine runs | 0, 0 in every run | 2.46 to 3.77 | not read | Yes (the frames) |
| Upscale and metrics | 04:53:33 to 04:55:22 | 21 probe runs, one per frame | 0 before each | 2.80 to 8.29, rising during the batch, whose probe runs computed the metrics on the CPU (their `time` output was not kept) | not read | Yes (PSNR and SSIM); of the single GPU time per upscale it records, only the spread over the frames is used, not the level ("GPU time per frame", above) |
| Timing a | 04:55:43 to 04:56:10 | 3 invocations, 10 warm-up rounds | 0, 0 | 6.30, 5.41, 4.89 | not read | Superseded by c: it started at load 6.30, right after the metrics batch |
| Timing b | 04:58:01 to 04:58:28 | 3, 10 warm-up rounds | 0, 0 | 1.40, 1.67, 1.65 | 8, 7 and 8 %; 7, 0 and 0 % | Superseded by c: the first configuration of each process, 3840 x 2160 spatial, had invocation medians 2.821, 1.622 and 1.427 ms |
| Timing c | 04:59:16 to 04:59:52 | 3, at least 3 s of warm-up (305, 294, 305 rounds of 9) | 0, 0 | 1.85, 2.37, 2.28 | 8, 8 and 8 %; 7, 8 and 0 % | Yes |

In a and b every 2560 x 1440 and 1920 x 1080 row has a median within
0.005 ms of the same invocation's in c. The 3840 x 2160 rows, the first
three of each process, are not: spatial is 0.097 to 1.391 ms above c in
four of the six invocations, bilinear up to 0.046 ms above and Lanczos
up to 0.072 ms above (both in b's first). The slow first row of b looks like section 8's start-of-process
level ("Levels at the start of a process"); that is an inference, no
clock was read. With c's longer warm-up, the first row and its repeat at
the end of the process have the same median (1.428 ms).

### Artifacts

Kept on the bare-metal host, outside the repository, where the owner can
view them: the 196 captured frames (TGA, named
`<scene>-<width>x<height>-<hud|no2d|no2drep|nopart>-<frame>.tga`, one
directory per run with its `manifest.txt` and `trace.q2r`), the 28 logs,
the run table, the frame-time table, the 49 masks and the 378 upscaled
frames (PNG, named `<scene>-f<frame>-5120x2880-mask-<edge|2d|particles>.png`
and `<scene>-f<frame>-<width>x<height>-<upscaler>-<hud|no2d>.png`), the
per-frame CSV, the self-test, Sobel and timing outputs, the probe's source,
binary and build line, the two trace readers and their outputs, and the
scripts that drove the runs and made these tables. No frame, demo, trace or game data is in the repository.

### Not measured

- The temporal scaler, the denoised scaler and the frame interpolator
  (RT11, TASK111 to TASK113): this measures the spatial scaler and two
  baselines only, so it is not a measurement of option A (above).
- Sprites and beams as a region: none in these frames (Regions, above),
  and a frame with them would need engine output to isolate them. Sky,
  water and the view weapon as regions.
- A moving camera, temporal stability and the response to an event: still
  frames only. No perceptual metric (FLIP was not implemented).
- The 2D layer drawn at the native size over the upscaled 3D frame as one
  composite: the 3D and 2D regions are scored apart instead.
- The upscalers inside a frame: each was timed alone.
- Other renderers: `ref_gl1` only, the card's reference.
- Ray-traced frames, and option E's loss in the effect terms.
- The causes of the 2D region's order, of the spatial scaler's lower
  whole-frame scores and of the four rows of single GPU times that rise
  with the frame's detail.
