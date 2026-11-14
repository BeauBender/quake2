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
models. Rebuilding ~140 instances is microseconds on every apple6+ GPU.

Memory budget, *assumed* for a retail map: 4k to 8k faces (`MAX_MAP_FACES`
is 65536), fan triangulation gives 2.5 triangles per face, so 10k to 20k
world triangles; the PAS at Metal's typical 64 bytes per triangle plus
nodes is under 4 MB. An alias model is at most 4096 triangles (MD2
`MAX_TRIANGLES`), typically 300 to 800; 128 instances refit per frame is
under 1 M triangles, well inside a per-frame compute budget on apple7.
RT1 measures the real counts on the owner's maps and records them here.

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

## 3. What the rasterizer must expose

`ref_metal` (TASK28) renders the world and entities into a G-buffer before
lighting is resolved. Required attachments, all at the frame's resolution:

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
the view weapon over the resolved image, (7) 2D. With `r_rt_* 0` steps 1,
3 and 4 are skipped and step 5 reduces to the stock product, which is the
state that must keep every gl1 pin.

## 4. Hardware gating

| Check at startup (`MTLDevice`) | Enables |
|---|---|
| `supportsFamily(.apple6)` and `supportsRaytracing` | compute RT passes (TASK24); M1 is apple7 |
| `supportsFamily(.apple9)` and `supportsRaytracingFromRender` | render-stage intersection: shadows and AO move into the lighting-resolve fragment shader, no separate dispatches or history reprojection for shadows (TASK25); M3 and later |
| neither | `r_rt_*` cvars are forced to 0 and reported as unavailable in `vid_restart` output; the rasterizer path is byte-identical to RT off |

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
   the one `r_rt_*` cvar on, temporal accumulation disabled for the pin so
   frames are stateless, blue-noise seed fixed by `pins_capture`). They are
   pinned against **`ref_metal` itself** at the commit where the owner
   accepted the look: `scripts/pins.sh record metal rt-ao` on that commit
   becomes the reference, later commits compare against it at 0.02. The
   pin catches drift, not quality.
3. **Performance** is the fps line `pins.sh record` writes to
   `timedemo.txt`, captured with all effects on at 1080p; each card states
   its number and the machine (apple family) it came from.
4. What an agent can judge alone: pin parity with RT off, drift against an
   accepted RT look, fps, the double-count invariant (a pixel with no
   dlight in range and no sun visibility must equal the RT-off pixel within
   1/255, checked by a `framediff` run between the RT-off frame and the
   RT-on frame masked by the visibility buffer, a `--mask` option RT2 adds).
   What it cannot judge: whether the look is right. **The one owner
   checkpoint:** after RT3 to RT6 each land, the owner views the four RT
   scenarios once and either accepts (the pins are recorded) or sets the
   cvar defaults differently (the card re-records). No other person-in-the-
   loop step exists on this path.

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
| RT8 | Hardware gating and fallbacks, `vid_restart` reporting, VM build | all `r_rt_*` forced 0 on the VM with the report line; pins unchanged there |
| RT9 (TASK25) | Render-stage ray queries on apple9: shadows and AO in the resolve shader, compute path kept for apple6 to apple8 | identical pins on both paths at 0.005; fps on the M6 at native resolution ≥ display refresh with everything on, recorded in `docs/audits/rt-performance.md` |
| RT10 (TASK25) | Acceleration-structure refit vs rebuild tuning and resolution-scale defaults per family, GPU timestamps | per-pass timings table in `docs/audits/rt-performance.md` for apple7 and apple9; no pin change |

Out of scope, decided after RT10 per the plan: path tracing (Phase G),
global illumination beyond AO, RT for particles or beams.

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
   never takes the branch. The forced-off half stays VM-only, and on this
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

Measured with a throwaway Objective-C program (Metal and Foundation
frameworks, built with `xcrun clang`, Metal Shading Language 3.1, no
window, no game data, not committed) on the bare-metal host: macOS 27.2.1
(26B131), default `MTLDevice` **Apple M6** (the device of section 7). All
times are GPU time from the command buffer's `GPUEndTime - GPUStartTime`.
Each configuration ran one discarded warm-up and then 8 timed runs; the
build and refit table was repeated in 6 separate process invocations, the
ray table in 3. `uptime` at the start and end of every batch showed load
averages of 2.2 to 2.4 (1 minute) and 1.95 to 2.0 (5 minutes), unchanged
across the batch. Before each batch, `pgrep` found no `scripts/test.sh`,
`make`, `cc`, `clang` or `quake2`; the program itself opened no window.
The load average of about 2 came from processes that were not rendering (a
git push script was the only other process seen), but this agent cannot
inspect other GPU clients, so "nothing else was rendering" is stated as
"no process of the build or game was running", not as a measurement of the
GPU. No batch was discarded.

**Scene.** Synthetic, so the numbers are throughput of this device, not of
a real map. The world is one 20,000-triangle structure (a triangle soup,
64-unit right triangles at random integer positions in a 4096 x 4096 x
1024 box, section 1's upper world size); 128 alias-model structures of 500
triangles each (16-unit legs in a 64-unit box) stand in for a busy frame.
The instance structure has 141 instances: the world, 128 alias instances
and 12 more instances of the same alias structures (standing in for inline
brush models). The camera is at the centre of the box looking along +z;
the shadow light is 400 units ahead of the camera, jittered by 256 units
per ray. Each pass is a compute kernel, `intersector<triangle_data,
instancing>`, one thread per pixel, 8 x 8 threadgroups, no shading, no
denoise, no G-buffer reads. The closest-hit pass casts `rpp` jittered
camera rays per pixel; the shadow pass reads a per-pixel primary hit
position (produced by an untimed primary pass) and casts `rpp` any-hit
rays from it (`accept_any_intersection`) toward the jittered light.
Because the world is a sparse soup, about 65 % of closest rays hit
something and about 22 % of all shadow rays (counting pixels whose primary
ray missed, which cast no ray) are occluded; a real closed map differs,
so use these as a scale, not as a prediction.

### Ray passes (GPU time, 3 process invocations, median of 8 runs)

At 1920 x 1080 and 2560 x 1440 the three invocations agreed within 1 %
(the ranges below are the spread of the three medians). 2560 x 1440 is
the display mode of this host; its refresh rate was not measured and is
not stated here.

| Pass | rpp | 640 x 480 | 1920 x 1080 | 2560 x 1440 |
|---|---|---|---|---|
| Closest hit | 1 | 0.114 ms | 0.685 to 0.686 ms | 1.211 to 1.215 ms |
| Closest hit | 2 | 0.215 ms | 1.362 to 1.364 ms | 2.413 to 2.414 ms |
| Closest hit | 4 | 0.420 ms | 2.702 to 2.703 ms | 4.794 to 4.796 ms |
| Closest hit | 8 | 0.825 to 0.826 ms | 5.380 to 5.381 ms | 9.558 to 9.561 ms |
| Any-hit shadow | 1 | 0.077 ms | 0.474 to 0.475 ms | 0.837 ms |
| Any-hit shadow | 2 | 0.127 ms | 0.774 to 0.777 ms | 1.369 to 1.374 ms |
| Any-hit shadow | 4 | 0.229 to 0.230 ms | 1.428 to 1.432 ms | 2.529 to 2.534 ms |
| Any-hit shadow | 8 | 0.437 ms | 2.764 to 2.768 ms | 4.890 to 4.904 ms |

The 640 x 480 closest-hit rows are the fast clock state in invocations 2
and 3. In invocation 1 the first configurations ran in the slow state
(untimed primary pass 0.385 ms, closest 1 rpp 0.389 ms, 2 rpp 0.739 ms,
about 3.4 times the fast state) and the state changed during the 4 rpp
configuration (min 0.536, median 0.554 ms; 8 rpp median 1.059 ms, min
0.821, against 0.420 and 0.825 ms in the other two invocations). All 1080p
and 2560 x 1440 rows were in the fast state in all three invocations. The
untimed primary pass (1 camera ray per pixel) cost 0.112 (fast) ms at 640
x 480, 0.683 to 0.686 ms at 1080p and 1.208 to 1.210 ms at 2560 x 1440.

Throughput, fast state, 1080p, 8 rpp: closest hit 16.6 M rays in 5.38 ms
is 3.1 M rays per ms; any-hit shadow 16.6 M rays in 2.76 ms is 6.0 M rays
per ms. Closest-hit cost is linear in ray count and in pixels.

### Acceleration-structure cost (GPU time, 6 process invocations)

Structures built with `MTLAccelerationStructureUsageRefit`, the 10k, 20k,
50k and 200k triangle soups of section 7. Each cell: median of 8 timed
runs, with (min to max) where the spread exceeded 5 %. Times in
milliseconds.

| Invocation | Build 10k | Build 20k | Build 50k | Build 200k | Refit 10k | Refit 20k | Refit 50k | Refit 200k |
|---|---|---|---|---|---|---|---|---|
| 1 | 2.513 | 1.791 | 3.335 | 3.549 (3.53 to 6.42) | 0.239 | 0.269 | 0.542 | 0.620 |
| 2 | 2.521 (0.725 to 2.538) | 0.507 | 0.969 | 3.536 | 0.240 (0.066 to 0.242) | 0.077 | 0.159 | 0.618 |
| 3 | 1.401 (0.737 to 2.537) | 0.524 | 0.959 | 3.533 | 0.069 (0.068 to 0.239) | 0.079 | 0.160 | 0.617 |
| 4 | 2.516 (1.263 to 2.534) | 0.508 | 0.967 | 3.549 | 0.240 (0.067 to 0.242) | 0.078 | 0.160 | 0.621 |
| 5 | 0.723 (0.715 to 2.523) | 0.508 | 0.967 | 3.539 | 0.068 (0.066 to 0.239) | 0.077 | 0.160 | 0.619 |
| 6 | 2.499 (1.053 to 2.509) | 0.509 | 0.964 | 3.544 | 0.234 (0.066 to 0.242) | 0.077 | 0.159 | 0.625 |

Per-frame costs of the dynamic part, same invocations:

| Item | Median per invocation (6) | Range, min to max over all runs |
|---|---|---|
| Refit of 128 alias structures x 500 triangles, one command buffer | 0.200 to 0.202 ms | 0.198 to 0.205 ms |
| Instance structure rebuild, 141 instances | 0.167 to 0.170 ms | 0.167 to 0.171 ms |

Two states are visible, as in TASK99. Invocation 1 ran every configuration
in the slow state (build 20k 1.79 ms, 50k 3.34 ms, refit 20k 0.269 ms,
50k 0.542 ms). Invocations 2 to 6 ran the 20k and 50k configurations in
the fast state (build 0.51 and 0.96 ms, refit 0.077 and 0.160 ms), about
3.5 times faster for both operations; the 10k configuration, the first of
each process, changed state during its timed runs in invocations 2 to 6
(the min to max in the table). The 200k configuration was the same in
both states (build about 3.5 ms, refit about 0.62 ms), and the 128-alias
refit and the instance rebuild showed one value in all six invocations
(under 2 % spread). The slow state appeared at the start of a process, in
the first configuration(s) timed, and ended after some tens of
milliseconds of GPU work in 5 of 6 invocations; invocation 1 stayed slow
for all of the build and refit table. Why it ended there was not
investigated.

**State the frame budget should plan with.** Plan the steady-state
per-frame costs with the fast-state numbers (the common state: the 1080p
and 2560 x 1440 ray passes in 3 of 3 invocations, 20k and 50k builds in 5
of 6, the 128-alias refit and the instance rebuild in 6 of 6) and allow a
factor of about 3.5 for the first frames of a process or after a map
load, where the slow state recurs. The one-time map-load build (20k
triangles) costs 0.5 ms (fast) to 1.8 ms (slow); even 200k triangles cost
3.5 ms in both states.

### Verdicts against the design

All comparisons are for this device (Apple M6, apple9, hardware ray
tracing) on the synthetic scene above, not for an apple7 device, which
this host cannot measure.

| Design statement | Measured on the M6 | Met? |
|---|---|---|
| Section 1: IAS rebuild of about 140 instances "is microseconds on every apple6+ GPU" | 0.167 to 0.171 ms (167 to 171 microseconds) | Met in substance (well under a millisecond), though hundreds of microseconds, not single microseconds. Wording finding for RT1 |
| Section 1: refit of 128 alias instances per frame fits a per-frame compute budget | 0.20 ms for 128 x 500 triangles (0.64 M triangles) | Met |
| Section 1: world structure built once at map load | 0.5 ms (20k, fast) to 1.8 ms (slow); 3.5 ms at 200k | Met |
| Section 2: dlight shadows, cap 4 rays per pixel | 4 rpp any-hit: 1.43 ms (1080p), 2.53 ms (2560 x 1440) | Met |
| Section 2: sun shadow, 1 ray per pixel | 0.47 ms (1080p), 0.84 ms (2560 x 1440) | Met |
| Section 2: AO, 2 rays per pixel | closest-hit 2 rpp (an upper bound, AO rays are range-limited): 1.36 ms (1080p), 2.41 ms (2560 x 1440) | Met |
| Section 2: water reflections, 1 closest-hit ray on water pixels only | at most 0.69 ms (1080p, all pixels), 1.21 ms (2560 x 1440), trace only | Met for the trace; hit shading not measured |
| Section 2: "about 5 rays per pixel" everything on, assumed 8 to 14 ms at 1080p on apple7 | 4 shadow + 1 sun + 2 AO + 1 reflection passes: 1.43 + 0.47 + 1.36 + 0.69 = 3.95 ms at 1080p, 2.53 + 0.84 + 2.41 + 1.21 = 7.0 ms at 2560 x 1440, plus 0.37 ms for the refit and instance rebuild; trace only, no G-buffer, shading, temporal accumulation or denoise | Met on the M6 in the fast state (below 8 ms at 1080p). In the slow state, scaling by the 3.4 times seen at 640 x 480 (an extrapolation, not measured at 1080p) gives about 13 ms at 1080p, still inside 8 to 14 ms. Apple7 not measured |
| Section 6, RT7: everything on at least 60 fps at 1080p on apple7 | about 4.3 ms of the 16.7 ms frame is ray tracing on the M6 (about 14 ms in the extrapolated slow state) | Met on the M6 for the ray cost alone; apple7 not measured, so RT7 is not decided by this device |
| Section 6, RT9: native resolution at least the display refresh, everything on, on the M6 | ray cost about 7.4 ms at 2560 x 1440 (fast state). A 60 Hz frame is 16.7 ms, a 120 Hz frame 8.3 ms | Met for the ray cost at 60 Hz. At 120 Hz the rays alone take most of the frame, leaving about 1 ms for rasterization, shading and denoise, which is likely not enough. The display's refresh rate was not measured, so which case applies is unknown |
| Section 6, RT10: resolution-scale defaults per family | The design states no numeric resolution-scale default | Nothing to compare |

No budget stated numerically in the design is missed on this device.
Findings for cards (no edit to any decision):

1. RT1: change "microseconds" in section 1 to "a fraction of a
   millisecond" (0.17 ms measured), and measure real world and alias
   triangle counts instead of relying on the soup.
2. RT9: record the display refresh rate next to the number, since at 120
   Hz the ray cost alone is most of the frame at native resolution.
3. RT10: repeat this program on an apple7 device (M1 Max); until then the
   resolution-scale default per family has no measured basis below
   apple9, and the design holds no numeric default to check.
4. RT7 and RT9: allow for the slow clock state (about 3.5 times) in the
   first frames after a map load, so a frame-time test does not count them.
