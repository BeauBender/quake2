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
single process that is not a build using about one CPU core. Nothing else
was rendering during the batches: the 54 GPU user clients are the system's
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
invocations it ended earlier, during the 20k (b) or 50k builds (their lowest timed runs,
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
| Section 1: refit of 128 alias instances per frame, "under 1 M triangles, well inside a per-frame compute budget" | 128 x 500 (0.064 M triangles): 0.203 ms later, 0.691 ms first. Worst case 128 x 4096 (0.524 M triangles): 1.674 ms. One structure of 1,048,576 triangles: 4.041 ms. The worst-case points were timed mid-process only | Met on the M6; apple7 not measured: the worst case is a tenth of a 60 Hz frame, the 1 M bound as one structure a quarter |
| Section 1: world structure built once at map load | 10k to 20k triangles: 0.50 to 0.71 ms (later) to 1.75 to 2.49 ms (first); 50k 0.99 to 3.34 ms; 200k about 3.6 ms; 1 M about 20 ms | Met |
| Section 2: dlight shadows, cap 4 rays per pixel | 4 rpp any-hit, every pixel casting: 1.95 ms (1080p), 3.46 ms (2560 x 1440 points), 13.86 ms (5120 x 2880 native) | No time budget of its own; part of the totals below. At native pixels this pass alone is 83 % of a 60 Hz frame |
| Section 2: sun shadow, 1 ray per pixel | 0.56 ms (1080p), 0.99 ms (2560 x 1440), 3.92 ms (native), every pixel casting | No time budget of its own; part of the totals below |
| Section 2: AO, 2 rays per pixel | closest hit 2 rpp from the camera, assumed (not shown) to bound the 128-unit hemisphere rays from above: 1.75 ms (1080p), 3.10 ms (2560 x 1440), 12.41 ms (native) | No time budget of its own; part of the totals below |
| Section 2: water reflections, 1 closest-hit ray on water pixels only | at most 0.88 ms (1080p, every pixel), 1.56 ms (2560 x 1440), 6.22 ms (native), trace only; hit shading not measured | No time budget of its own; part of the totals below |
| Section 2: "about 5 rays per pixel" everything on, assumed 8 to 14 ms at 1080p on apple7 | 4 shadow + 1 sun + 2 AO + 1 reflection: 1.95 + 0.56 + 1.75 + 0.88 = 5.14 ms at 1080p, 3.46 + 0.99 + 3.10 + 1.56 = 9.11 ms at 2560 x 1440, 13.86 + 3.92 + 12.41 + 6.22 at native, which is 36.39 ms as the sum of the unrounded values, plus 0.38 ms per-frame items; trace only, no G-buffer, shading, temporal accumulation or denoise. At the start level, 3.46 times 5.14 ms (the factor measured for the 1080p primary pass and at 640 x 480; the other 1080p passes were not measured at that level) is about 17.8 ms, plus 1.31 ms | Below the estimate on the M6 at the later level (5.5 ms with the per-frame items), above it at the start level (about 19.1 ms). The 8 to 14 ms range is an estimate for apple7, not a budget, so neither is a pass or a fail; apple7 not measured. The apple9 half of section 2's "Total budget" sentence (the render-stage path "is expected to halve it", 4 to 7 ms at 1080p): the render-stage path was not measured; the compute path is already about 5.5 ms, inside the expected 4 to 7 ms |
| Section 6, RT7: everything on at least 60 fps at 1080p on apple7 | ray cost about 5.5 ms of the 16.7 ms frame on the M6 at the later level, about 19.1 ms at the start level (extrapolated as above) | Not decided by this device: apple7 not measured. On the M6 the ray cost alone fits a 60 Hz frame at the later level and not in the first frames of a process |
| Section 6, RT9 (1): native resolution at least the display refresh, everything on, tracing at native pixels | 5120 x 2880 at 60 Hz (16.7 ms): 36.39 ms of rays plus 0.38 ms per-frame items = 36.8 ms | **Not met**: the ray cost alone is 2.2 times the frame; dlight shadows plus sun alone (17.8 ms) are over it. This is the compute-dispatch path; the render-stage path RT9 specifies (resolve-shader ray queries, apple9) was not measured. "Not met" still holds under the design's expected halving (section 2): halving the ray passes gives (13.86 + 3.92 + 12.41) / 2 + 6.22 + 0.38 = about 21.7 ms, and halving the whole trace gives 36.39 / 2 + 0.38 = about 18.6 ms, both over 16.7 ms. 120 Hz (8.3 ms frame): not met either, GPU time does not depend on the display. This host's display tops out at 60 Hz, so 60 Hz is RT9's target here. Finding 2 |
| Section 6, RT9 (2): the same, tracing at a lower internal resolution and upscaling to 5120 x 2880 with MetalFX | Temporal scaler: 1080p internal 5.14 + 0.38 + 1.82 = 7.3 ms (9.3 ms as one measured frame, which also holds the 0.88 ms primary pass the design leaves to the rasterizer and the 1.00 ms gap); 2560 x 1440 internal 9.11 + 0.38 + 2.41 = 11.9 ms (15.0 ms measured). Temporal denoised scaler instead: 5.14 + 0.38 + 12.03 = 17.6 ms (18.9 measured) at 1080p, 9.11 + 0.38 + 19.50 = 29.0 ms (31.5 measured) at 2560 x 1440 | With the temporal scaler, met for the ray and upscale cost at 60 Hz: 7.4 to 9.3 ms left at 1080p internal and 1.7 to 4.8 ms at 2560 x 1440 internal for rasterization, shading, temporal accumulation and denoise. With the denoised scaler, not met at either input. The design does not use MetalFX, so this is a finding, not a decision. Compute-dispatch path, render-stage path not measured. At 120 Hz (8.3 ms frame) with the temporal scaler at 1080p internal: met by the 7.3 ms sum, not met by the 9.3 ms measured frame; GPU time does not depend on the display, and 60 Hz is the target on this host. Finding 2 |
| Section 6, RT10: resolution-scale defaults per family | The design states no numeric resolution-scale default | Nothing to compare |

The device misses one budget stated numerically in the design: RT9 at
native pixels. Findings for cards (no edit to any decision):

1. RT1: change "microseconds" in section 1 to "a fraction of a
   millisecond" (0.17 ms measured later in a process, about 0.6 ms in the
   first frames of one), and measure real world and alias triangle counts
   instead of relying on the soup.
2. RT9, to become a card: tracing everything at the native 5120 x 2880
   pixels costs 36.8 ms against the 16.7 ms frame at the display's 60 Hz,
   so RT9 is not met that way. This is the compute-dispatch path; the
   render-stage path RT9 specifies was not measured, and "not met" still
   holds under the design's expected halving (about 21.7 ms halving the
   ray passes, about 18.6 ms halving the whole trace, as in the RT9 (1)
   row). The design (sections 2 and 6) does not use
   MetalFX. A card would study tracing at an internal resolution and
   upscaling to the native size with MetalFX's temporal scaler (measured
   above: 7.3 to 9.3 ms of ray and upscale cost at 1080p internal, 11.9 to
   15.0 ms at 2560 x 1440), and evaluate `MTLFXTemporalDenoisedScaler`
   (12.0 to 19.5 ms in frame here, the "In frame, scaler" column) against section 2's own 5x5
   bilateral denoiser and temporal accumulation, including image quality,
   which this program does not assess. It does not redesign either one.
3. Repeat on an apple7 Mac (to become a card): repeat this program on an
   apple7 device (M1 family), including the MetalFX scalers, for RT7 and
   RT10. The scene and order are stated above so it can be rebuilt in
   kind, not bit for bit (the seeds and hash constants are not given), and
   open-scene hit rates may differ by several points; the closed scene,
   which the verdicts use, is unaffected. Until then the
   resolution-scale default per family has no measured basis below apple9,
   and the design holds no numeric default to check.
4. RT7 and RT9: allow for the start-of-process level (about 3.5 times,
   about 0.1 s of GPU work made of short dispatches) so a frame-time test
   does not count the first frames, and test whether it recurs after a
   map load, which is a hypothesis here.
