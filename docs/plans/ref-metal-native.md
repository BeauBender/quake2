# Design: native `ref_metal` (TASK28)

Plan anchor: `docs/plans/metal-renderer.md`, Phase A step 2 (INFORM8, #35).
Card: TASK28 (#38). Per-concept reference: the OpenGL-subset shim of
TASK21 (`src/client/refresh/gl1/metal/`, design in
`docs/plans/metal-shim-design.md`, GL concepts in
`docs/analysis/gl1-state-inventory.md`). Acceptance: the frame pins of
`scripts/pins.sh` (`docs/plans/metal-port-harness.md`). The cards this
design decomposes into are group N of `docs/plans/metal-port-cards.md`.

`ref_metal` is a renderer module of its own: `ref_metal.dylib`, selected
with `vid_renderer metal`, implementing `API_VERSION 8`
(`src/client/vid/header/ref.h`, unchanged) directly on Metal. It is the
renderer the ray-tracing cards (TASK24, TASK25, rows RT1 to RT10) build on.

## 1. Starting point

Three candidates were weighed.

| Candidate | Fidelity to the pinned gl1 look | Size | What ray tracing later needs |
|---|---|---|---|
| Fork gl1's front end, replace its GL calls with a small explicit Metal back end | Same algorithms, same CPU arithmetic, same draw order as the code the `gl1` and `gl1-metal` pins were recorded from; the shim already proved each GL concept on Metal with those algorithms | gl1 is about 10k lines; without the classic two-pass lightmaps, tile rendering, stereo, paletted textures and GL probing the front end is about 6k, plus about 1.5k of Objective-C and 0.4k of shaders | The world can be retained in static buffers at registration (gl1 rebuilds it per frame, but the vertex values are the same); the lightmap build already separates the style term from the dynamic-light term (`R_BuildLightMap`, `R_AddDynamicLights`), which is the split RT3 needs |
| Fork gl3's shader-based structure | A different look: per-pixel dynamic lights in the fragment shader (`gl3_shaders.c`, `dynLights[32]`), four lightmaps per surface weighted by `lmScales` in the shader, a gamma and intensity postprocess. Every pin would become a look change to argue about | About 9.8k lines, structured around OpenGL 3.2 VBOs and UBOs, which is closer to Metal's shape | Shader-side lighting is closer to a G-buffer resolve, but RT2 must reproduce the stock (gl1) product, so it would be rebuilt anyway |
| New, from a blank file | Untestable until most of it exists; the card and INFORM8 reject it | Unknown | Free to choose |

**Decision: fork gl1's front end and replace its GL calls with direct Metal
through a back end of a few explicit calls.** The pins judge gl1's look, and
gl1's own algorithms are the shortest path to matching it pixel for pixel;
gl3's structure is borrowed only where it does not change a pixel (a
per-frame streaming vertex ring, uniform blocks per pass), and the data the
ray-tracing design asks for (section 9) is added as retained buffers beside
gl1's algorithms, not by changing them.

"Fork" means copy and convert, one concept per card: a card copies the gl1
functions its concept needs into `src/client/refresh/metal/`, removes their
GL types and calls, and names the gl1 function each came from in a comment.
gl1's own files are never edited (they and the shim are the two references).
The copied lines count toward a card's size.

## 2. Module layout and language split

```
src/client/refresh/metal/
  header/local.h      front-end types and globals; no GL type, no GL header
  header/model.h      model, surface and poly types, with retained-buffer offsets
  header/rmb.h        the back-end API in C: the only door to Metal
  rm_main.c           GetRefAPI, cvars, Init/Shutdown, RenderFrame, view setup, entity list,
                      the screen blend (R_PolyBlend)                                        (from gl1_main.c)
  rm_sdl.c            window seat: PrepareForWindow, InitContext, drawable size, vsync     (from gl1_sdl.c)
  rm_image.c          image registry, LoadPic, uploads, palette, gamma and intensity tables (from gl1_image.c)
  rm_scrap.c          the scrap atlas                                                      (from gl1_scrap.c)
  rm_draw.c           the 2D entry points                                                  (from gl1_draw.c)
  rm_model.c          registration, BSP faces and leafs, retained world buffers           (from gl1_model.c)
  rm_surf.c           world traversal, texture chains, brush entities, alpha surfaces      (from gl1_surf.c)
  rm_lightmap.c       lightmap blocks as slices of one array texture, update staging       (from gl1_lightmap.c)
  rm_light.c          R_BuildLightMap, styles, dynamic lights, R_LightPoint, flash blobs   (from gl1_light.c)
  rm_warp.c           subdivision, sky clipping and box, water warp                        (from gl1_warp.c)
  rm_mesh.c           alias models: lerp, shading, shells, shadows                         (from gl1_mesh.c)
  rm_buffer.c         the draw batch: gl1's buffered world, alias and 2D vertices
                      (R_UpdateGLBuffer, R_Buffer2DQuad, R_SetBufferIndices) written into
                      the frame ring and flushed as rmb_draw calls in gl1's order          (from gl1_buffer.c)
  rm_effects.c        sprites, beams, null models, particles                               (from gl1_main.c)
  rm_misc.c           screenshot, built-in textures, the metal_stats line                  (from gl1_misc.c)
  rm_pure.c           helpers with neither SDL nor Metal (index expansion, readback
                      conversion, pipeline and state keys, the object ledger): host-tested
  rmb_device.m        device, queue, CAMetalLayer, capability report, command-buffer watch, frame slots
  rmb_resource.m      textures, samplers, buffers, staging, pipeline and depth-stencil tables
  rmb_encode.m        passes, draws, upload ordering, readback, present
  rm_shaders.metal    the shader set (section 4)
  common/             shared with the shim, moved there by N2 (section 8)
tests/metal/rm_host_test.c   host test of rm_pure.c and the moved common code
tests/check-ref-metal.sh     the gate check (section 10)
```

Language split, as in the shim: C everywhere except the three `rmb_*.m`
files, which are Objective-C with ARC (`-fobjc-arc`) and expose only the C
functions declared in `header/rmb.h`. No Objective-C type appears in a
header the C files include. The shaders are Metal Shading Language;
`common/mtl_shader_math.h` is written in the common subset of C99 and MSL so
the host test runs the very formulas the shaders run (the shim's
`mgl_texenv.h` pattern). The files whose names end in `_model.c`,
`_image.c` and `_mesh.c` parse untrusted data and fall under
`risk.mediumPaths` (record 0006), so cards touching them get the security
review pass.

### Inputs and bounds

Model, image and BSP data come from paks and downloads and are untrusted.
`ref_metal` keeps the checks of the shared loaders in `files/` unchanged
and adds these rules for what it builds from their output:

- It copies no model, image or BSP bytes into a fixed-size buffer without
  a bound checked against that buffer's size.
- Every count taken from file data that sizes a retained buffer (the world
  vertex, index and surface-record buffers of N4), the lightmap array (its
  slice count, at most `MAX_LIGHTMAPS`, N5), a staging-ring upload (an
  image's width times height, N2) or a frame-ring region (an alias model's
  vertex count, N7) is checked before the buffer is sized or written. On
  overflow the draw or the upload is skipped (and counted in
  `metal_stats`) or the renderer stops with `ri.Sys_Error`; it never
  writes past the end of a buffer, a slice or a ring region.
- The size checks are plain functions in `rm_pure.c` or `common/` (the
  ring's own allocation check in `mtl_ring.c`), so the host test reaches
  them; each of N2, N4, N5 and N7 adds a host-test case with an oversized
  count.

The limits `ref_metal` inherits at the renderer entry points (the counts in
`refdef_t` and the entity, particle and light lists the engine hands over)
are gl1's and the other renderers' as well; their bounds are tracked on
FIX40 (#185), not in group N.

## 3. Frame structure and the back end

The shim opened one render pass per frame and split it on every mid-frame
upload, clear or read, because GL calls arrive in any order. `ref_metal`
owns its passes:

1. **Frame slot.** `BeginFrame` takes one of three frame slots (a semaphore
   created at 0 and raised to 3, a 2 s bounded wait, a skipped frame on
   timeout, as in the shim), and a command buffer.
2. **Uploads first.** Every texture write (image registration, lightmap
   style and dynamic-light updates, the scrap, cinematic frames) is copied
   into the staging ring and blitted on an upload command buffer that is
   committed before the frame's command buffer, so the frame reads the new
   contents. Metal's hazard tracking orders it after earlier frames that
   read the same texture. If a texture already referenced by a draw of the
   current command buffer is written again (gl1 semantics: draws before the
   write saw the old contents), the frame's command buffer is ended and
   committed first and a new one is opened with a Load pass. gl1 writes
   lightmaps before the world and the scrap only into unused regions, so
   this is expected to be rare; `metal_stats` counts it.
3. **The 3D pass** renders into an offscreen colour texture (`bgra8Unorm`,
   the drawable's size) with a `depth32Float_stencil8` attachment, in gl1's
   draw order: world (with sky), opaque entities, translucent entities,
   flash blobs, particles, alpha surfaces, the screen blend. Clears are
   load actions: colour per `r_clear` (gl1's `glClearColor(1, 0, 0.5)`),
   depth 1, stencil 1 when stencil shadows are on (gl1's `R_Clear`). The
   back-end API names two sub-stages inside it, *opaque* (world, brush and
   alias models) and *forward* (the rest), so RT2 can split the pass there
   for the G-buffer and the lighting resolve without moving front-end code.
4. **The 2D pass** (after `EndWorldRenderpass`) loads the colour texture,
   has no depth, and draws in call order.
5. **Present.** `EndFrame` acquires the drawable (one second timeout,
   `allowsNextDrawableTimeout`), blits the offscreen texture to it,
   presents and commits. Without a drawable nothing is presented and the
   frame is still complete for readback.
6. **Readback** (the renderer's `screenshot` command, which the pin hook
   runs before present): end the open pass, blit the offscreen texture into
   a shared buffer, commit, wait at most 5 s, convert BGRA to RGB rows top
   down (what gl1's `R_ScreenShot` hands to `ri.Vid_WriteScreenshot` after
   its row swap), and reopen the pass with a Load action if drawing
   continues. Rendering offscreen makes readback independent of the
   drawable and of `framebufferOnly`, and is the target RT2 needs anyway.

Draws are encoded immediately into the open pass; nothing is deferred past
the pass. A draw is `rmb_draw(pipeline id, depth-stencil id, raster state,
vertex range, index range, uniforms)`.

**Matrices.** The shim computes each draw's model-view-projection product
on the CPU in single precision (`mgl_mat_mul`, a `float` sum per element);
the rotate matrix is built in double and stored as float (as are the
frustum and ortho matrices, from their `GLdouble` parameters), and the
translate and scale matrices are built in float. Its vertex function maps
GL clip depth with `mgl_gl_clip_z`. `ref_metal` uses the same arithmetic,
moved verbatim into `common/` (section 8), so its depth and position values
are the shim's bit for bit; it does not split the product into per-pass and
per-draw halves on the GPU, which would change the rounding. Viewports are
set in Metal's top-left convention directly from the `refdef_t` rectangle;
the host test checks them against the shim's `mgl_compute_raster` for the
same rectangles.

## 4. Shaders and pipeline states

One vertex layout per draw class, each read by vertex id from the ring or a
static buffer:

| Class | Vertex | Fragment | Used by |
|---|---|---|---|
| `2d` | float2 position, float2 uv, rgba8 colour | texture modulate or replace, alpha test at 0.666 by function constant; untextured colour | pics, characters, fill, fade, cinematic |
| `world_lm` | float3 position, float2 uv, float2 lightmap uv (static buffer) | diffuse times lightmap slice times the overbright scale, clamped (the shim's `GL_COMBINE` formula) | lightmapped world and brush surfaces |
| `world_tex` | as `world_lm` | diffuse, optionally modulated by a constant colour | `r_fullbright`, sky, warped water, translucent surfaces |
| `alias` | float3 position, float2 uv, rgba8 colour (CPU-lerped into the ring) | modulate; untextured for shells | alias models, the view weapon |
| `color` | float3 position, rgba8 colour | untextured colour | shadows, beams, flash blobs, null models, the screen blend |
| `particle` | float4 clip position, rgba8 colour, float3 point offset and radius | coverage times alpha (the shim's `mgl_point_coverage`), alpha test | particles drawn as CPU-computed quads, as the shim does |

**Pipeline states are built at load time.** The front end selects a
pipeline by an enum (class, alpha-test constant, blend: off,
`SRC_ALPHA/ONE_MINUS_SRC_ALPHA`, `ONE/ONE`; colour write mask), never by a
hash of loose state. `InitContext` builds the whole table (about 24 entries,
from the inventory's section 2 counts) after loading `ref_metal.metallib`;
`metal_stats` reports pipelines created after init, which must be 0. The six
depth-stencil states (none; `LEQUAL` with and without writes; the stencil
shadow `EQUAL 1, mask 2, INCR`, with and without writes) and the samplers
(nearest, linear, the `gl_texturemode` mip filters, anisotropy from
`r_anisotropic`, clamp and repeat) are built at the same time; a change of
`gl_texturemode` or `r_anisotropic` rebuilds the sampler table between
frames. The metallib is compiled with the same `xcrun metal` flags as the
shim's, since parity with it is the acceptance.

## 5. Textures and lightmaps

- **Images.** `rgba8Unorm`, private storage, uploaded through the staging
  ring; mip chains with the blit encoder's `generateMipmaps`, which is what
  the shim does for gl1's `GL_GENERATE_MIPMAP`, so the sampled texels are
  the shim's. gl1's upload decisions as the reference takes them are
  copied unchanged: the shim advertises `GL_ARB_texture_non_power_of_two`,
  so gl1 uploads through `R_Upload32Native` (no rounding to powers of two,
  no resampling), with `R_LightScaleTexture`, the gamma and intensity
  tables and the skin flood fill. gl1's power-of-two path
  (`R_Upload32Soft`) never runs on the reference and is not copied.
  Paletted (`GL_COLOR_INDEX8_EXT`) uploads are dead on Metal and are not
  copied.
- **The scrap** keeps gl1's four 256 by 256 atlases and allocator.
- **Cinematics** (`DrawStretchRaw`): one texture per size, rewritten each
  call through the upload command buffer.
- **Lightmaps** are one `texture2d_array` of 128 by 128 slices, one slice
  per gl1 block (`BLOCK_WIDTH`, `BLOCK_HEIGHT`, up to `MAX_LIGHTMAPS`), so
  lightmap coordinates, texel layout and linear sampling are gl1's and the
  world binds one lightmap texture per pass instead of one per chain.
  Lightstyle and dynamic-light changes rebuild the surface's rectangle on
  the CPU with gl1's `R_BuildLightMap` and upload the joined dirty
  rectangle per slice (gl1's `R_JoinAreas`) before the 3D pass, which is
  the order gl1's multitexture path already uses.

## 6. Dynamic lights

As gl1 with `gl1_multitexture 1` (the default): `R_PushDlights` marks
surfaces, `R_BuildLightMap` adds `R_AddDynamicLights` into the rebuilt
rectangle, and the update is staged as in section 5. `gl1_flashblend 1`
draws gl1's flash blobs instead. `R_AddDynamicLights` stays a separate step
behind one front-end flag, because RT3 replaces exactly that term and must
not count a light twice (`docs/plans/metal-raytracing-design.md`,
section 2). Per-pixel dynamic lights, as gl3 does them, are out of scope:
they change the look.

## 7. Cvars, commands and what is left out

`ref_metal` reads gl1's cvars by gl1's names wherever they change a pixel
(`gl1_overbrightbits`, `gl1_waterwarp`, `gl_polyblend`, `gl1_stencilshadow`,
`r_shadows`, `gl1_pointparameters`, `gl1_particle_*`,
`gl1_flashblend`, `gl1_minlight`, `r_modulate`, `r_fullbright`,
`r_lightmap`, `gl_texturemode`, `r_anisotropic`, `vid_gamma` and the rest
of gl1's list in `R_Register`), so a scenario file
means the same to `gl1-metal` and to `metal`, and its settings hash is the
same. Cvars that exist only for Metal are named `metal_*`; the first is
`metal_stats` (0 or 1: per captured frame and at shutdown, one line with
draw, pipeline, upload, pass-split and skipped-surface counts and the
retained-buffer sizes). Each card that adds one documents it in
`doc/040_cvarlist.md`. The commands are gl1's names where they exist
(`screenshot`, `imagelist`, `modellist`); no new command is planned.

Left out of `ref_metal`, each a gl1 feature no pin scenario exercises:
the classic two-pass lightmap path (`gl1_multitexture 0`; the shim measured
it within rounding of the multitexture path), lightmap copies for tile
renderers (`gl1_tilerendering`), stereo (`gl1_stereo` and its
`gl1_stereo_*` cvars), paletted textures, MSAA (refused, as the shim does:
`r_msaa_samples` reads 0), and the debug lines of `gl_showtris` and
`gl_showbbox`. Any of them is a parked card if wanted.

The other gl1 cvars that change a pixel but that no scenario sets, checked
against gl1's source:

| Cvar | In `ref_metal` | Why |
|---|---|---|
| `gl1_ztrick` | dropped (depth is cleared every frame, gl1's default 0) | `R_Clear` (`gl1_main.c`) alternates depth ranges to skip the depth clear; on Metal the depth clear is a load action, so the trick saves nothing and the default path is kept |
| `gl_zfix` | copied | polygon offset around brush models (`R_Clear`, `R_DrawBrushModel` in `gl1_surf.c`), raster state the back end has |
| `gl1_dynamic` | copied | it gates the dynamic-lightmap update in `gl1_surf.c`, code N5 and N8 copy anyway |
| `gl1_saturatelighting` | dropped | read only by `R_BlendLightmaps` (`gl1_surf.c`), the classic two-pass lightmap path that is left out |
| `gl1_picmip`, `gl1_round_down` | dropped | read only by `R_Upload32Soft` (`gl1_image.c`), the power-of-two path gl1 does not take on the shim (section 5) |
| `gl1_texturealphamode`, `gl1_texturesolidmode` | dropped | they pick the GL internal format; `ref_metal` stores every image as `rgba8Unorm`, as the shim does whatever internal format gl1 asks for (`mgl_layer.m`), so the reference ignores them too |
| `r_retexturing` | copied | `R_FindImage` (`gl1_image.c`) passes it to the shared loader in `files/` |

`gl1_buffer.c`, gl1's draw batch, is copied as `rm_buffer.c` (section 2),
without its tile-rendering lightmap copy (`cur_lm_copy`).

## 8. What is shared with the shim

The shim stays in the tree and in the build as the same-machine reference
(`gl1-metal`); this design does not remove it (see "Decisions taken at filing"). Its
code is GL-typed throughout (`GLenum`, `mgl_state_t`), so most of it cannot
be shared without rewriting the reference. What is GL-free is moved, once,
in N2, into `src/client/refresh/metal/common/`:

| Moved | From | Why it must be one copy |
|---|---|---|
| `mtl_matrix.c`, `mtl_matrix.h`: multiply (single precision), rotate, frustum and ortho (built in double, stored as float), translate and scale (float), moved verbatim; the only change is the type names, `GLfloat` becomes `float` and `GLdouble` becomes `double` (the shim's typedefs of the same types, so no value changes) | `mgl_raster.c` (`mgl_mat_mul`) and the matrix arithmetic of `mgl_state.c` | parity of every position and depth value depends on identical rounding |
| `mtl_ring.c`, `mtl_ring.h`: the bump-allocated frame ring | `mgl_raster.c` (`mgl_ring_alloc`) | one tested allocator |
| `mtl_shader_math.h` (C99 and MSL subset): clip-depth mapping, clamp, point coverage | `mgl_texenv.h` (`mgl_gl_clip_z`, `mgl_clamp01`, `mgl_point_coverage`) | the shaders of both renderers run the same source |

The shim's GL entry points then call the moved functions; `mgl_texenv.h`
includes `mtl_shader_math.h`. The move is proved neutral by the shim's host
test (same check count, all pass) and by `gl1-metal` records of `hud2d`,
`cinematic` and `world` from after the move that compare at score 0 and bad
0 against records from before it. The direction is shim to `common/`, so
nothing under `src/client/refresh/metal/` includes a shim header.

Copied rather than moved, because the shim's versions are shaped around GL
state: the device and layer code of `mgl_layer.m` (the capability report,
the command-buffer watch, the bounded waits, the drawable timeout), the
point-quad computation of `mgl_draw.c`, the readback conversion. Each copy
is short and names its source. A later card may fold them into `common/`
once both renderers are pinned; nothing in group N depends on it.

## 9. What ray tracing will need, and where it comes from

From `docs/plans/metal-raytracing-design.md` sections 1 and 3:

| RT need | Provided by group N as |
|---|---|
| One static triangle mesh per map, AS-friendly | N4: a static vertex buffer (float3 position packed at offset 0, then the two uv pairs; one stride) and a `uint32` index buffer of the fan-expanded world polys, built at registration, private storage, sizes logged per map. RT1 builds the primitive acceleration structure from these buffers without a copy |
| Inline brush models as separate meshes | N4: each submodel is a contiguous index range in the same buffers; RT1 builds one structure per range |
| Per-surface material and normal | N4: a per-surface record buffer: plane normal flipped for `SURF_PLANEBACK`, material class (world, sky, water, translucent, flowing), texinfo flags, image index, lightmap slice and rectangle, styles. RT2 writes material id and normal from it |
| Unwarped water | N6: water stays unwarped in the static buffers; the warp is applied to texture coordinates per frame on the CPU, as gl1 does |
| Lightmap term without dynamic lights | N5 and N8: the style term and the dynamic-light term are separate steps into the same slice; RT2 can write a second, styles-only array from the first step |
| Lerped alias vertices per instance | N7: the CPU lerp (gl1's arithmetic, for parity) writes each visible model's vertices into a per-entity region of the frame ring with a fixed layout; RT1 refits from that region |
| Explicit pass boundary for the G-buffer and the resolve | section 3: the *opaque* and *forward* stages of the 3D pass |
| Capability gating | N1: a capability struct (families apple5 to apple9, `supportsRaytracing`, `supportsRaytracingFromRender`) that RT8 reads |

None of the G-buffer, acceleration structures or `r_rt_*` cvars is built in
group N.

## 10. Build, gate and other platforms

- **Makefile.** A `ref_metal` target inside the Darwin branch: objects in
  `$(BUILDDIR)/ref_metal/`, the `.m` files with `-fobjc-arc`, linked with
  `-framework Metal -framework QuartzCore -framework Foundation` and the
  executable's SDL by `$(SDL_DYNAMIC_LOOKUP)`, as the other renderer dylibs
  are; `ref_metal.metallib` written next to the dylib by a rule that fails
  loudly without `xcrun` or the Metal compiler. No OpenGL framework is
  linked. On every other OS `make ref_metal` fails with one line saying the
  target is for macOS. `ref_metal` is not added to `all` in group N (see
  "Decisions taken at filing"), so `make`, CI and every other platform build exactly
  what they build today.
- **Gate.** `tests/check-ref-metal.sh`, run by `scripts/test.sh` like every
  `tests/check-*.sh`: (a) on every OS, compile and run
  `tests/metal/rm_host_test.c` against `rm_pure.c` and `common/` with ASAN
  and UBSAN where available; (b) on Darwin with `xcrun -f metal` and the
  vendored SDL 3 headers, `make ref_metal` in a scratch copy (the pattern of
  `tests/check-metal-shim.sh`), with `otool -L` showing Metal and no
  OpenGL; otherwise one `skipped: <reason>` line; (c) on every OS, the
  vocabulary check: no `gl[A-Z]` call, no `GL_` identifier outside string
  literals and comments, no `<OpenGL/` include and no include of `gl1/`
  under `src/client/refresh/metal/`, with a mutant self-test (a `GL_`
  identifier in code fails it; the same text in a string literal does
  not). String literals are exempt because `gl_texturemode` takes gl1's
  value strings (`"GL_NEAREST"` to `"GL_LINEAR_MIPMAP_LINEAR"`,
  `gl1_image.c`), which `rm_image.c` must keep as strings to parse the cvar
  by gl1's names (section 7); comments are exempt because each copied
  function names its gl1 source. No CI workflow change.
- **The engine side.** `vid_renderer metal` needs no engine change:
  `VID_LoadRenderer` loads `ref_<name>` from the binary's directory. The
  fallback order and the video menu entry are N10's, guarded to macOS.
- **The harness side.** `scripts/pins.sh record metal <scenario>` needs no
  change: the renderer name is the module name, the stale-build check asks
  `make -q release/ref_metal.dylib`, and the manifest must say `metal`.

## 11. Acceptance method

On this host (the build VM, T0):

```
make client game ref_metal && make ref_gl1 WITH_METAL_SHIM=yes
MTL_DEBUG_LAYER=1 scripts/pins.sh record gl1-metal <scenario>          # reference, at the card's base commit
MTL_DEBUG_LAYER=1 scripts/pins.sh record metal <scenario>
scripts/pins.sh compare metal <scenario> --ref gl1-metal [thresholds]
```

The shim is the same-machine reference: same GPU, same arithmetic by
design (section 3), so `ref_metal` is held tighter than the shim was held
against `gl1`:

| Scenario | Against `gl1-metal` at | Why |
|---|---|---|
| `hud2d`, `cinematic`, `world`, `world-fullbright` (N4), `skywater` from `demo1.dm2` | `--threshold 0.005 --bad-threshold 0.001` | `gl1-metal` records of the existing four are identical to each other (`gl1` against itself is exact on them too), and N4 shows the same for `world-fullbright`; the shim's own two lightmap paths differ by at most 0.000417 and 0.000029 |
| `models` | the defaults, 0.02 and 0.005, against two `gl1-metal` records | nondeterministic (engine PRNG); the scenario's line |
| `particles` | `--threshold 0.02 --bad-threshold 0.25`, against two records | the scenario's `// nondeterministic:` line |
| `skywater` from `water.dm2` | `--threshold 0.02 --bad-threshold 0.03`, against two records | the scenario's line |
| `dlights` (N8) | as measured by N8 from `gl1-metal` against itself, recorded in the scenario file | new scenario |

Every record runs with `MTL_DEBUG_LAYER=1`, and a record with a validation
message or a failed command buffer is not accepted. A threshold is never
widened to make a pin pass (runbook, "Judging a renderer"); a card that
cannot meet its value records the measurement and stops for a decision.
When record and replay are complete (TASK45, TASK46), a replayed trace
makes the nondeterministic scenarios comparable at the tight values; group
N does not wait for it.

On a machine with accelerated OpenGL (TASK83, #187):
`scripts/pins.sh compare metal <scenario> --ref gl1` at the values of the
runbook table "Accept `gl1-metal` against `gl1` at". This is the parent
card's criterion against `ref_gl1`, and it is pending there for the 3D
scenarios exactly as it is for TASK21; it does not gate any card of group N.

A card's pins pass when every frame of every scenario it names is `ok`.
Before N6 the world has no sky, water or translucent surfaces; N4 and N5
name the frames on which `metal_stats 1` reports such surfaces skipped and
are judged on the others. From N6 on nothing is excluded.

The screen blend (gl1's `R_PolyBlend`: one `color` quad over the view, the
3D pass's last draw) comes with N4, not with the effects of N9. The 3D
scenarios set `cl_blend 0` but leave `gl_polyblend` at 1, and `cl_blend 0`
clears only the blend's colour (`cl_view.c`), so gl1 draws the quad on
every frame whose blend alpha is not 0. The runbook measured it on the
reference: on frames 3, 5, 8, 11 and 16 of `world` it changes 39 to 98
percent of `gl1-metal`'s pixels (the whole screen), and it darkens the
whole screen on the under-water frames of `skywater` from `water.dm2`.
Without it in N4 every world frame with a blend would be unjudgeable
until N9, and the exclusions above would not cover it.

## 12. The cards

Group N of `docs/plans/metal-port-cards.md`. A stack: each card is cut from
its predecessor's branch (record 0006, `prs.stacking`). Sizes are estimates
of changed lines, copied lines included. Every card also keeps the pins of
the cards before it passing (regression), and ends with `scripts/test.sh`
green.

| Key | Title | Risk | Model | Size | Judged by |
|---|---|---|---|---|---|
| N1 | Skeleton: build target, window seat, clear, present, capture, capability report, gate check | M | opus | 950 | `record metal hud2d` completes with uniform frames |
| N2 | Images, textures, the 2D pipeline and the cinematic; the shared code moved to `common/` | M | opus | 1000 | `cinematic`; `gl1-metal` unchanged by the move |
| N3 | Pics, characters, the scrap and the HUD | L | sonnet | 600 | `hud2d` |
| N4 | World geometry: BSP registration, retained buffers, traversal, fullbright surfaces, the screen blend | M | opus | 1150 | `world-fullbright` (new) |
| N5 | Lightmaps: the array texture, styles, overbright, updates | M | opus | 850 | `world` |
| N6 | Sky, water warp, translucent and flowing surfaces | M | sonnet | 800 | `world`, `skywater` |
| N7 | Brush entities, alias models, the view weapon, model lighting | M | opus | 1000 | `models` |
| N8 | Sprites, beams, null models, stencil shadows, dynamic lights, flash blobs | M | opus | 900 | `models` complete, `dlights` (new) |
| N9 | Particles, the full parity sweep and frame time | L | sonnet | 550 | `particles`, every scenario |
| N10 | Lifecycle, selection and the docs: `vid_restart`, resize, vsync, gamma, fallback order, menu | M | opus | 800 | restart soak, leaks, all pins |

The deviation from the order the card suggests: frame capture comes with
the skeleton (N1), because every later card is judged by captured frames;
the late card (N10) is the window lifecycle. Risk follows consequence and
record 0006's paths: build, gate script, scenario files, the menu and every
`*_model.c`, `*_image.c` and `*_mesh.c` make a card M; nothing in group N
changes an API, the protocol or a savegame, so none is H (the parent,
TASK28, stays H for the whole).

## 13. What stays out

Ray tracing (TASK24, TASK25, rows RT1 to RT10), the G-buffer, iOS, tvOS and
visionOS (TASK22, TASK23), static linking (TASK20), path tracing, any change
to `ref.h` or another module API, to the protocol or to savegames, any
change to gl1's own files, gl3, soft or `files/`, the vendored SDL, CI
workflows, and new dependencies. The window retention across `vid_restart`
is FIX41 (#193), outside the port queue; N10 measures `ref_metal`'s own
objects against it.

## 14. The Mac with real hardware, and this virtual machine

This virtual machine presents an apple5-class paravirtual GPU with no ray
tracing; Apple's software OpenGL renderer gives `ref_gl1` references with
faults of their own (runbook, TASK49 to TASK52). It can finish every card
of group N: the shim runs here, so the same-machine reference exists, the
Metal validation layer works (`MTL_SHADER_VALIDATION` does not: it hangs
the paravirtual GPU), and ASAN, UBSAN and `leaks` run.

The bare-metal Mac (T1, M1 Max class) is for what this machine cannot
give:

- `gl1` references from an accelerated OpenGL (TASK83), against which
  `metal` and `gl1-metal` are judged with `--ref gl1`, closing the parent
  criteria of TASK21 and TASK28 for the 3D scenarios;
- frame-time numbers worth comparing (real GPU timings, Xcode GPU frame
  capture); the VM's numbers are recorded by N9 and repeated there;
- the ray-tracing implementation in compute (RT1 to RT7: apple6 or later
  and `supportsRaytracing`).

The M3 or later Mac (T2) is for render-stage ray queries and hardware
intersection performance (RT9, RT10) only.

## 15. Decisions taken at filing

1. **Dependencies.** Group N cards depend only on one another in sequence
   (N1 first); they do not wait for TASK27. TASK21 is closed. TASK28 (#38)
   closes when N10 (TASK93) lands. The cards are TASK84 (#195) to TASK93
   (#204), N1 to N10 in order.
2. **The shim after TASK28.** Departs from the card: TASK28's goal says
   the shim "remains in the tree only as the per-concept reference until
   this card closes, then is removed from the build". The GL-subset shim
   stays in the tree and in the build: it is the same-machine reference
   for `ref_metal` and keeps `gl1` working on macOS (which the card's
   scope also asks for "until the owner decides otherwise"). Removing it
   from the build is a later decision of the owner, not part of group N.
3. **Build and CI.** `ref_metal` is not part of `make all` and is not
   built in CI for now; `tests/check-ref-metal.sh` in the local gate builds
   it where the Metal toolchain exists. Adding it to CI is a later card.
4. **Fallback order (N10).** On macOS `metal` goes ahead of `gl1` only; the
   default `vid_renderer` does not change.
5. **The reference on this host.** Departs from the card: TASK28's goal
   says every frame pin passes "against the `ref_gl1` references". On
   this host the `gl1` references come from Apple's software OpenGL
   renderer, whose faults the runbook records (TASK49 to TASK52), so
   group N is accepted against `gl1-metal`, the shim on the same GPU, at
   the values of section 11. The half against `gl1` (`--ref gl1`) is
   deferred to TASK83 (#187) on a machine with accelerated OpenGL, where
   it is judged as it is for TASK21; it gates no card of group N.
6. **Starting point.** Departs from the card: TASK28's scope says the
   module is "started as a copy of `gl1/` plus the shim". `ref_metal`
   starts as a copy of gl1's front end only, converted one concept per
   card (section 1); the shim is not copied. Its GL-free arithmetic is
   moved to `common/` (decision 10) and a few pieces of its device code
   are copied with their source named (section 8). The card's
   per-concept order is kept, with the deviation of section 12.
7. **Cvar names.** `ref_metal` reads gl1's `gl1_*` and `gl_*` cvar names;
   Metal-only cvars are `metal_*`.
8. **Dropped gl1 features.** Departs from the card: TASK28's scope
   converts all of gl1. Classic two-pass lightmaps (with
   `gl1_saturatelighting`), tile rendering, stereo, paletted textures,
   MSAA, `gl_showtris`/`gl_showbbox`, `gl1_ztrick`, gl1's power-of-two
   upload path (with `gl1_picmip` and `gl1_round_down`) and the internal
   format cvars `gl1_texturealphamode`/`gl1_texturesolidmode` are left
   out of `ref_metal` (section 7): no pin scenario exercises them. Each is
   a possible later card.
9. **Thresholds.** The tight thresholds of section 11 (0.005 / 0.001 on
   deterministic scenarios against `gl1-metal`) stand; a card that cannot
   meet one reports the measurement rather than loosening it.
10. **Shared arithmetic.** N2 moves the shim's GL-free arithmetic into
   `src/client/refresh/metal/common/` rather than copying it, proven
   neutral by byte-identical `gl1-metal` frames and an unchanged shim
   host-test count.
