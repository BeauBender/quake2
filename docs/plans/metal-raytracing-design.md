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
`MTLDevice` named **Apple M6**. Section 4's assumptions about the VM
(paravirtual device, no ray tracing, apple5-class families) are not
re-measured here; this is the physical-device column the gating needs.

| Query | Apple M6 (physical) | Section 4 / plan statement for the VM |
|---|---|---|
| `supportsFamily` apple1 to apple9, metal3 | all true | VM: apple5-class, so apple6 and later false |
| `supportsRaytracing` | true | false (forced off) |
| `supportsRaytracingFromRender` | true | false |
| `supportsPrimitiveMotionBlur` | true | not stated |
| `supportsFunctionPointers`, `...FromRender`, `supportsDynamicLibraries` | true, true, true | not stated |
| `readWriteTextureSupport` | tier 2 | not stated |
| `argumentBuffersSupport` | tier 1 (see finding 4) | not stated |
| `hasUnifiedMemory` | true | not stated |
| `recommendedMaxWorkingSetSize` | 26,800,603,136 bytes (about 25 GiB) | not stated |
| `maxBufferLength` | 20,100,448,256 bytes | not stated |
| `maxThreadsPerThreadgroup` | 1024 x 1024 x 1024 (pipeline `maxTotalThreadsPerThreadgroup` 1024, `threadExecutionWidth` 32 for the ray kernel) | not stated |
| `maxThreadgroupMemoryLength` | 32768 bytes | not stated |
| Largest 2D RGBA8 private texture created | 32768 x 32768 (8192 and 16384 also ok) | not stated |
| Stage-boundary counter sampling | supported (GPU timestamps for RT10 are possible) | not stated |
| `supportsPullModelInterpolation`, barycentric coordinates, 32-bit float filtering, 32-bit MSAA, BC compression, `supportsQueryTextureLOD` | all true | not stated |

End to end, one run: a one-triangle primitive acceleration structure
(640 bytes) was built, wrapped in a one-instance instance acceleration
structure, and a compute kernel (`intersector<triangle_data, instancing>`
over `instance_acceleration_structure`, Metal Shading Language 3.1) cast
two rays. The ray at (0.25, 0.25, -1) along +z returned hit distance
1.000000 (expected 1.0) and the ray at (5, 5, -1) returned a miss
(-1, expected); both command buffers finished with status completed and no
error. The path works on the device, not only the flag.

Build time of a primitive acceleration structure on a synthetic triangle
soup (random 64-unit triangles in a 4096 x 4096 x 1024 box, unindexed,
opaque, a choice of mine: section 1 expects 10k to 20k world triangles and
a refit population of up to about 1 M alias triangles, so 10k, 20k, 50k
and 200k were measured). Two invocations of the probe, seven builds each
(fourteen runs); GPU time from `GPUEndTime - GPUStartTime`, wall time
around commit to completion. `uptime` load averages (1, 5, 15 min) were
4.27/3.17/2.79 before and 4.65/3.28/2.84 after the first invocation and
4.36/3.24/2.83 after the second; another process was using the GPU
(frame recording) the whole time.

| Triangles | Structure bytes | Build scratch bytes | Build GPU ms, 14 runs | Build wall ms | Refit GPU ms (14 runs) |
|---|---|---|---|---|---|
| 10,000 | 937,728 | 251,584 | 2.77 to 2.94 | 3.10 to 4.44 | 0.07 to 0.25 |
| 20,000 | 1,874,816 | 904,128 | 0.67 to 2.41 | 1.04 to 3.91 | 0.09 to 0.35 |
| 50,000 | 4,762,560 | 1,857,984 | 1.33 to 4.84 | 1.66 to 5.40 | 0.16 to 0.63 |
| 200,000 | 19,048,256 | 7,430,528 | 5.02 to 10.60 | 5.63 to 11.96 | not measured |

The spread is a property of the measurement, not noise to average out: the
two invocations differ by up to 3.5 times at the same size (20k: 0.67 ms
in the first, 2.3 to 2.4 ms in the second; the 10k build is slower than
the 20k build in the first), and within the second the first three 200k
builds (10.6, 8.8, 7.1 ms) are slower than the following four (5.0 to
5.1 ms). That pattern fits GPU clock and power-state ramping and
contention with the other GPU client. Read the table as "a world-sized
structure builds in under 5 ms, and a build of up to 200k triangles stays
under about 12 ms wall, on a busy M6", not as a benchmark. The 10k to 20k
figure matches the design's "under 4 MB" assumption: about 94 bytes per
triangle on this soup (0.9 to 1.9 MB), though a real map has coherent
geometry and will differ; RT1 measures that. Refit used the same
descriptor with `MTLAccelerationStructureUsageRefit`, in place, and
completed in well under 1 ms up to 50k triangles.

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
4. **Argument buffers:** section 2 and 3 do not state a tier, but a design
   that passes the G-buffer, history buffers and material tables to the
   ray kernels through argument buffers must note that the M6 reports
   tier 1 here (not tier 2), so no code may rely on tier 2 behaviour
   (arbitrary resource arrays indexed in the shader, writable argument
   buffers). Finding for RT1/RT2 (new constraint, not a change to a gating
   decision).
5. **Memory budget (section 1):** unchanged and confirmed with large
   margin: the structure for 20k triangles is about 1.9 MB against the
   "under 4 MB" assumption, and unified memory with a 25 GiB working-set
   hint removes any pressure from 128 refit instances.
6. **Build and refit cost (sections 1 and 6, RT1/RT10):** consistent with
   "once at map load" for the world (about 1 to 5 ms) and per-frame refit
   (under 1 ms per 50k triangles here, so 128 alias instances of a few
   hundred triangles each fit comfortably). Whether refit stays cheap as
   many small instances on an apple7 device is for RT10.
7. **Metal API notes for the implementers (findings, no decision
   changes):** a refit-capable structure reports a refit scratch size of
   0 and refits in place with `destination` equal to the source;
   `useResource` on each primitive structure is required in the compute
   encoder that traces an instance structure; and on macOS 27 the
   properties `lowPower`, `headless`, `removable` and the
   `MTLGPUFamilyCommon*` constants are deprecated, so the gating code must
   key on `MTLGPUFamilyApple*` and the ray-tracing properties only (which
   section 4 already does).

No gating decision in sections 4 and 6 changes on this device. The
findings above (3, 4) are cards for the RT8 and RT1/RT2 owners, not edits
to those cards.
