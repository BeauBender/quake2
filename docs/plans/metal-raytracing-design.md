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
