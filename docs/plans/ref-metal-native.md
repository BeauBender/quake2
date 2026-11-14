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
  image's width times height, or a cinematic frame's, N2) or a frame-ring region (an alias model's
  vertex count, N7) is checked before the buffer is sized or written.
  A texture's width and its height are each also checked on their own,
  against `RMB_MAX_TEXTURE_SIDE` (16384, the device's limit on the
  apple5 floor): Metal aborts the process on a texture descriptor with a
  longer side, where GL answers an error, so a bound on the product
  alone (a 20000 by 1 image passes it) is not enough. On
  overflow the draw or the upload is skipped (and counted in
  `metal_stats`) or the renderer stops with `ri.Sys_Error`; it never
  writes past the end of a buffer, a slice or a ring region.
- The size checks are plain functions in `rm_pure.c` or `common/` (the
  ring's own allocation check in `mtl_ring.c`), so the host test reaches
  them; each of N2, N4, N5 and N7 adds a host-test case with an oversized
  count.
- N3 (the scrap), N6 (warp subdivision), N8 (dynamic lights, shadows) and
  N9 (particles) keep gl1's batch and capacity checks, each with a
  host-test case of an oversized count.

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
| `2d` | float4 position, float2 uv, float4 colour (the shim's 48-byte layout, section 17) | texture modulate or replace, alpha test at 0.666 by function constant; untextured colour | pics, characters, fill, fade, cinematic |
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
  call through the upload command buffer. gl1 uploads the frame with the
  solid internal format, which the shim makes opaque (alpha forced to
  255), so `ref_metal` uploads cinematic frames opaque.
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
| `r_scale8bittextures` | dropped | read only by `R_Upload32` (the smoothing of 8-bit images, `SmoothColorImage`) and `R_LoadPic` (the 8-bit scale-up) in `gl1_image.c`; no pin scenario sets it (section 17) |
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
Before N6 the world has no sky, water, translucent or flowing surfaces
(gl1 scrolls the opaque flowing ones inside its lightmapped path; N4 and
N5 count them as skipped, like sky, warped and translucent surfaces, until
N6 draws them scrolled); N4 and N5
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
| N4 | World geometry: BSP registration, retained buffers, traversal, fullbright surfaces, the screen blend | M | opus | 1150 | `world-fullbright` (new), flowing surfaces skipped |
| N5 | Lightmaps: the array texture, styles, overbright, updates | M | opus | 850 | `world`, flowing surfaces skipped |
| N6 | Sky, water warp, translucent and flowing surfaces (`water warp` here is the texture-coordinate warp of water surfaces; the cvar `gl1_waterwarp`, the under-water projection wobble, is part of the view setup N4 copies) | M | sonnet | 800 | `world`, `skywater` |
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
   (#204), N1 to N10 in order. The group N issues' `Plan anchor:` names
   `ref-metal-native.md` rather than `metal-renderer.md`, because the
   design lives here (departs from filing rule 4); the parent #38 carries
   only `depends:TASK93`, since the children form one chain (departs from
   filing rule 6).
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

## 16. As built

### N1: the skeleton (TASK84)

What exists: `make ref_metal` (Darwin only, not in `all`; elsewhere it
fails with one line naming macOS) links `release/ref_metal.dylib`
against Metal, QuartzCore and Foundation, with no OpenGL and the
executable's SDL (`SDL_DYNAMIC_LOOKUP`). The files are those of
section 2 that N1 needs: `header/local.h`, `header/rmb.h`, `rm_main.c`,
`rm_sdl.c`, `rm_misc.c`, `rm_pure.c`, `rmb_device.m`, `rmb_encode.m`;
`files/common.c` and `shared.c` are linked as the other renderers link
them. Every `refexport_t` entry is filled; registration and drawing are
no-ops that return NULL handles (`DrawGetPicSize` answered 0 by 0, as
the comment in `ref.h` says for a missing pic; gl1 answers -1 by -1, and
since N2 `ref_metal` does too, section 17) and count their calls. `BeginFrame`
opens the frame's first pass, colour only, where gl1 calls `R_Clear`:
colour cleared to 1, 0, 0.5 with `r_clear 1`, loaded with `r_clear 0`
(gl1 then clears no colour and the frame starts on what the buffer
holds; here the last frame, or black on a target no pass has written).
`RenderFrame` opens the 3D pass with colour loaded, depth cleared to 1
and stencil to 0 (gl1 clears depth in `R_Clear`; no 2D draw writes
depth before the view, so the result is the same; stencil 0 is the no-shadow value:
gl1 clears stencil only in `R_Clear`, to 1 when `r_shadows`, a stencil
buffer and `gl1_stencilshadow` are all on, and to 0 only in its
interleaved stereo modes, so N8 owes the clear to 1 with stencil
shadows; nothing reads stencil before N8), and
`EndWorldRenderpass` the 2D pass (colour loaded, no depth). With
`r_clear 1` a `screenshot` in the main menu before any map is 640x480,
every pixel 255, 0, 128; with `r_clear 0` every pixel 0, 0, 0. A
readback of a colour target no pass has written (a new target in a
skipped frame) is refused. The cvars read are gl1's `r_clear`, `r_mode`, `r_customwidth`,
`r_customheight`, `r_norefresh`, `r_vsync`, `vid_fullscreen`, plus
`metal_stats`; the one command is `screenshot`.

Choices made where the design left room:

- `rm_sdl.c` is the only file that includes SDL: it creates and destroys
  the SDL Metal view and hands its `CAMetalLayer` to `rmb_attach`, and
  passes the window's pixel size to `rmb_frame_begin`. The Objective-C
  files include no SDL header.
- The device and queue are opened in `Init` (before the window), so the
  capability report and the apple5 refusal come before any window is
  made; the decision itself is `rm_caps_floor_ok` in `rm_pure.c`,
  host-tested.
- The colour target follows the window's pixel size, and the screenshot
  hands over the target's size. Outside a frame (a `screenshot` typed at
  the console) it reads the last frame from the target.
- After a readback the frame goes on with a new command buffer; nothing
  reopens the pass, the next pass a later card opens loads the target.
- The teardown line (always printed) counts creates and destroys per
  kind and, through weak references, the colour and depth targets, the
  readback buffer and the queue that are still alive after ref_metal
  dropped them ("unreleased"). The system default device is shared with
  the process and is not weak-checked. A test build that kept one
  target in a static reference printed `1 live objects ... 1 unreleased`.
- The vocabulary check looks for `gl[A-Z]` calls and `GL_` identifiers
  in code only, outside comments and string literals, so N2 can copy
  gl1's `gl_texturemode` value strings (`"GL_NEAREST"` and the rest) as
  literals; `<OpenGL/` and `gl1/` includes are looked for in the raw
  text, and a symbolic link under the directory fails the check. It
  scans `.c`, `.h`, `.m` and `.metal` files; any other file there (a
  `.mm`, `.cpp` or `.inc`) fails the check instead of going unscanned.
- `scripts/pins.sh` needed no change (section 10): `metal` is a plain
  module name.

Measured on the build VM (T0, `Apple Paravirtual device`), commit
`0da03fd3`, load averages between 7.9 and 12.2 (a shared host):

| What | Result |
|---|---|
| `MTL_DEBUG_LAYER=1 scripts/pins.sh record metal hud2d`, release build, 3 runs (`--run 2`, `--run 3`) | each: 23 frames at 640x480, every `manifest.txt` line names `metal`, every pixel of every frame 255, 0, 128; the engine quit by itself with status 0; 689 timedemo frames in 0.2 to 0.3 s (2590 to 2883 fps); no failed command buffer; teardown `0 live objects`, `0 unreleased` |
| the same record with the `ASAN=1` build (`PINS_BIN=debug/quake2`) | the same 23 uniform frames, no sanitizer line, 1837 fps |
| capability lines in `engine.log` | `Metal device: Apple Paravirtual device`; `supportsFamily (apple5 to apple9): apple5`; `supportsRaytracing: no, supportsRaytracingFromRender: no` |
| four `vid_restart` cycles `metal` to `soft` and back, `ASAN=1`, `MTL_DEBUG_LAYER=1`, 2 runs | status 0, no sanitizer line; 5 `ref_metal` sessions per run, each teardown `0 live objects ... 0 unreleased, 0 unmatched`; 0 frames without a drawable, 0 skipped, 0 failed command buffers |
| `vid_renderer metal` with `ref_metal.dylib` absent | `Library ref_metal.dylib cannot be found!`, then the existing order (gl3, gl1, soft); `soft` loaded |
| `tests/check-ref-metal.sh` | host test 36 checks, 0 failed; 12 vocabulary mutants refused and the clean file (GL_ in literals and comments) passed; build, `otool -L` and the other-OS refusal checked |

The apple5 refusal cannot be shown on this hardware (it is apple5); the
host test covers the decision (apple4 refused, apple5 and apple9
accepted). The completion handlers are code in the dylib: one that
arrives after the bounded teardown wait (a GPU hung past 5 s) would run
after the module is unloaded, as in the shim; left for N10.

The implementer did not run the full gate (`scripts/test.sh`); the
host limits of this card keep it to the orchestrator, who runs it
before merge. The change is about 2.5 times the card's estimate of 950
lines because the estimate counted the module's own code only: the
bounded frame slots, readback and object ledger of the Objective-C back
end, the host test and the gate check with its vocabulary mutants, and
the as-built and runbook text add as much again, and every `refexport_t`
entry is spelled out as a counting no-op.

Left for N2: the metallib rule (`xcrun metal`, which the gate already
requires before it builds), textures and the 2D pipeline, and reopening
a pass with a Load action after a readback when drawing continues.

## 17. As built: N2, images, the 2D pipeline and the cinematic (TASK85)

What exists:

- **The move (section 8).** `common/mtl_matrix.{c,h}` (identity,
  product, rotate, translate, scale, ortho, frustum), `common/mtl_ring.{c,h}`
  and `common/mtl_shader_math.h` (clamp, clip depth, point coverage)
  hold the shim's code with `GLfloat` and `GLdouble` spelled `float` and
  `double`. The functions are named `mtl_*`; the shim's headers keep the
  old names as macros (`mgl_mat_mul`, `mgl_ring_alloc`, `mgl_ring_t`,
  `MGL_RING_ALIGN`, `mgl_clamp01`, `mgl_gl_clip_z`,
  `mgl_point_coverage`), so its call sites and its host test are
  unchanged. `glRotatef`, `glTranslatef`, `glScalef`, `glOrtho` and
  `glFrustum` keep their GL error handling and build their matrix
  through the moved functions, which answer 0 where GL refuses the
  arguments.
- **Images (section 5).** `rm_image.c` is gl1's registry with the
  native upload path: `R_FindImage`, `R_LoadPic` (skin flood fill, the
  crosshair normalisation), `R_Upload8` and `R_Upload32` as
  `R_Upload32Native` (light scale, the gamma and intensity tables, the
  alpha scan; an image keeps its size), `R_FreeUnusedImages`,
  `R_TextureMode` and `imagelist`. Each image carries a sampler: one of
  three classes (mipmapped, gl_texturemode's filters and
  `r_anisotropic`; without mipmaps, the mode's magnifying filter for
  both; nearest, for `r_nolerp_list` and `r_2D_unfiltered`), each in a
  repeat and a clamp form. As in gl1, an image on the lists is nearest
  from its load, but a mode change gives a mipmapped one the mode's
  filters again (`rm_image_sampler`), and `BeginFrame` re-runs
  `R_TextureMode` when any of gl1's six cvars for it changes. `R_TextureMode` rebuilds the six samplers
  between frames. `DrawFindPic` and `DrawGetPicSize` load and register
  pics (gl1's answer for a missing pic, -1 by -1, replaces N1's 0 by 0);
  drawing them is N3's. Not copied: `r_scale8bittextures` (no scenario
  sets it), the scrap (N3), the paletted and power-of-two paths.
- **The back end (sections 3 and 4).** `rmb_resource.m` loads
  `ref_metal.metallib` from the dylib's directory and builds the 2D
  pipeline table (12 states: replace, modulate and untextured, each with
  and without gl1's alpha test and blending) when the context is made.
  Textures are `rgba8Unorm`, private, with the blit encoder's mip chain.
  Every upload is copied into one 64 MB shared staging ring and blitted
  on an upload command buffer that is committed before any frame command
  buffer (at the end of the frame, before a readback, before a split).
  A texture rewritten after a draw of the open command buffer used it
  splits that command buffer (`metal_stats` counts the splits; none in
  the cinematic). A full ring commits the pending uploads, waits for them
  (bounded) and starts again. 2D draws take their vertices and fan
  indices from a 4 MB frame ring per frame slot (8 MiB since N3,
  section 18).
- **2D and the cinematic.** `rm_draw.c` has `DrawFill`, `DrawFadeScreen`,
  `DrawStretchRaw` and `SetPalette`, with gl1's 2D state: the ortho
  matrix of `R_SetGL2D` times the identity modelview through
  `mtl_mat_mul`, texture environment `GL_REPLACE`, the alpha test, no
  blending but in the fade. The cinematic frame is converted at its own
  size (8-bit through `r_rawpalette`; gl1's 320 by 240 buffer, a larger
  frame allocates) and uploaded opaque into one texture that is made
  again when the size changes; it is sampled with the mode's magnifying
  filter (or nearest with `r_videos_unfiltered`) and repeat, as gl1's
  texture 0. `SetPalette` clears the colour target to 0, 0, 0, 0 as gl1
  does.
- **Bounds.** `rm_upload_bytes` (`rm_pure.c`) decides every upload: w
  and h positive, each at most `RMB_MAX_TEXTURE_SIDE` (16384, Metal's
  limit; the shim's is 8192, but the device's own limit refuses nothing
  a GL driver on this hardware takes), and w times h times 4 within the
  staging ring, checked by division before the product is formed. The front end calls it
  before it converts or scales anything (`R_Upload8` before
  `R_Convert8to32`, `R_Upload32` before the light scale, `DrawStretchRaw`
  before the palette conversion), and the back end again before it
  stages; a refused upload is skipped and counted (`uploads skipped` in
  `metal_stats`), its image slot stays free. With a 64 MB ring the
  largest upload is 4096 by 4096 texels; gl1 has no such limit, and no
  image of the game data comes near it. `mtl_ring_alloc` refuses an
  allocation larger than the rest of the ring without moving its head.
  Every failed `rmb_texture_upload` frees the id it was given, so a
  caller that forgets its id on 0 (the image slot, the cinematic's
  texture) loses none; a forced-failure build of a scratch copy showed
  5,000 failed uploads leaving no id taken. A refused image is not remembered: a later `R_FindImage` of the same
  name reads and decodes the file again (and is refused and counted
  again). gl1 has no such memory either, for a missing image or any
  other: its `R_FindImage` looks only through the loaded images before
  it calls the loader, so `ref_metal` keeps gl1's behaviour.

Choices made where the design left room:

- The 2D vertex is the shim's layout (float4 position, float2 uv, float4
  colour, 48 bytes), not section 4's float2 position and rgba8 colour, so
  the vertex function computes exactly the shim's `mvp * pos` and the
  colours reach the fragment function as the shim's floats.
- The shaders specialise the texture environment and the alpha test by
  function constants. An opaque image's texels have alpha 255, so
  `GL_REPLACE` takes the texel's alpha; the shim keeps the vertex alpha
  there, which differs only for a vertex alpha below 1 with replace,
  which gl1's 2D code does not draw.
- `metal_stats` gains draws, skipped draws, uploads, skipped uploads,
  pass splits, pipelines and pipelines created after init. Every caller's
  buffer is `RM_STATS_LINE` (512 bytes); the host test formats the
  longest line, every counter at its largest, into it.
- `Swap_Init` runs in `RI_Init` as in gl1: the loaders in `files/` call
  `LittleShort`, and the `ASAN=1` build crashed in `PCX_Decode` without
  it.
- The shim's objects have no header dependencies (the Darwin `CFLAGS`
  carry no `-MMD`), so a build from before the move must remove
  `build/release/ref_gl1_metal/` once; otherwise `mgl_draw.o` and
  `mgl_layer.o` still reference `mgl_mat_mul` and `mgl_ring_alloc` and
  the link fails.

Measured on the build VM (T0), base `4175e8cc` (its `src/` is that of
`beb6bb9e`, where the `gl1-metal` references were recorded), head
`11dfcbff` for the pins, load averages 7.7 to 31.9 (a shared host),
every record with `MTL_DEBUG_LAYER=1`:

| What | Result |
|---|---|
| `compare metal cinematic --ref gl1-metal` (`PINS_DEMO=idlog.cin PINS_EVERY=7`, tight values), three release records (`--run 2`, `--run 3`) | each: 12 frames ok, worst score 0, worst bad 0; every frame byte-identical to the reference; no validation message, no failed command buffer; teardown `0 live objects`, `0 unreleased` |
| the same record with the `ASAN=1` build (`PINS_BIN=debug/quake2`) | 12 frames ok, score 0, bad 0, no sanitizer line |
| `metal_stats 1`, the cinematic outside the harness (the harness latches the command line) | 12 captured frames, each line `12 pipelines, 0 created after init`, 0 skipped draws, 0 skipped uploads, 0 pass splits |
| `gl1-metal` `hud2d`, `cinematic`, `world` after the move against before it (`--ref-id`, `--threshold 0 --bad-threshold 0`), at the move's commit (recorded before the branch was rebased onto `4175e8cc`) and again at the head | 23, 12 and 23 frames, score 0, bad 0, every frame byte-identical |
| `tests/check-metal-shim.sh` before and after the move | `501 checks, 0 failed` both times; shim coverage unchanged (54 direct, 5 by pointer, shim defines 60) |
| `record metal hud2d` (N1's acceptance) | 23 frames, each a single colour, 255, 0, 128 (pics are N3's); 54 textures made and released |
| `tests/check-ref-metal.sh` | host test 106 checks, 0 failed; 72 vocabulary mutants refused; build, metallib, `otool -L` and the other-OS refusal checked |

After the review rework (source at `2f9c58b9`, with the per-side bound, the freed
ids, the six texture-mode flags of gl1's `RI_BeginFrame`, gl1's
mode-change filter for a mipmapped image on `r_nolerp_list` as
`rm_image_sampler`, one `RMB_FRAMES_IN_FLIGHT`): `tests/check-ref-metal.sh`
129 checks, 0 failed; `tests/check-metal-shim.sh` 501 checks, 0 failed;
`MTL_DEBUG_LAYER=1 PINS_DEMO=idlog.cin PINS_EVERY=7` records of `gl1-metal`
and `metal` at that source, `compare metal cinematic --ref gl1-metal
--threshold 0.005 --bad-threshold 0.001`: 12 frames, 0 failed, worst 0,
worst bad 0; the `metal` log holds only `Metal API Validation Enabled`
and teardown `0 live objects` (texture 19/19, buffer 5/5).

The implementer did not run the full gate (`scripts/test.sh`): the host
limits of this card keep it to the orchestrator, who runs it before
merge; the checks above are its parts that this change touches. The
change is about 2.6 times the card's estimate of about 1,000 lines
(about 2,900 lines added at the first review; less the roughly 350 of
the move of section 8, about 2,600): the estimate counted the 2D pipeline and the cinematic,
not the registry that `DrawFindPic` needs (`rm_image.c`, gl1's
`R_FindImage`, `R_LoadPic`, the upload paths and `R_TextureMode`), the
Objective-C back end's staging ring, texture table, split and twelve
pipeline states (`rmb_resource.m`), the counters of `metal_stats`, and
the host-test cases and as-built text for each bound.

Left for N3: drawing pics and characters (`DrawPicScaled`,
`DrawPicScaledCol`, `DrawStretchPic`, `DrawCharScaled`, `DrawTileClear`),
the scrap and its uploads, `Draw_InitLocal` (conchars), gl1's buffered
2D quads (`R_Buffer2DQuad`) and with them the texture environment
`GL_MODULATE` for coloured pics; the built-in textures (`r_notexture`,
the particle texture) with N4 and N9 (section 18). After a split the next pass is a
2D pass; reopening a 3D pass there (with its depth kept) is owed by the
first card that rewrites a texture inside the 3D pass (N5).

## 18. As built: N3, pics, characters, the scrap and the HUD (TASK86)

What exists:

- **The 2D draws.** `rm_draw.c` has `DrawCharScaled`, `DrawStretchPic`,
  `DrawPicScaled`, `DrawPicScaledCol` and `DrawTileClear` with `DrawFindPic`
  and `DrawGetPicSize` moved from `rm_main.c`, and `RM_InitDraw` loads
  `conchars` (gl1's `Draw_InitLocal`). The quad arithmetic is plain C in
  `rm_pure.c` (`rm_char_quad`, `rm_pic_quad`, `rm_tile_quad`,
  `rm_tint_color`), each function the body of gl1's, host-tested against
  hand-worked values.
- **The batch.** gl1 queues characters, stretched pics, tiles and the
  atlas pics of `DrawPicScaled` in `gl_buf` (`R_Buffer2DQuad`) and draws
  it when the texture changes, when it is full and where it calls
  `R_ApplyGLBuffer`. `ref_metal` keeps that order, because it is visible
  in the frames: `DrawFill` and the cinematic do not flush gl1's buffer,
  so a fill drawn after queued characters appears under them. The batch
  (`RM_Flush2D`) is flushed at the start of `RenderFrame`, in
  `EndFrame`, before a screenshot's readback, in `DrawFadeScreen`, and
  before a non-atlas `DrawPicScaled` and a `DrawPicScaledCol` draw at
  once. It holds `RM_BATCH_QUADS` (1024, gl1's 4096) quads of one
  texture and sampler; `rm_batch_fits` decides every add (a full batch is
  drawn first, never written past) and `rm_quad_indices` makes the
  indices (the 16-bit limit, the index capacity). `rmb_draw_2d_quads` in
  `rmb_encode.m` draws a batch as one indexed draw through the same
  ring path as `rmb_draw_2d` (a shared static function; no new pipeline
  class, the `2d` pipelines of N2 are used: replace with the alpha test,
  and modulate for `DrawPicScaledCol`).
- **The scrap.** `rm_scrap.c` is gl1's four 256 by 256 atlases (the first
  two without filtering) with `Scrap_AllocBlock` and `Scrap_Update`. The
  allocator (`rm_scrap_alloc`, gl1's `CommonAllocBlock`: same spots,
  including that a block never starts in the last w columns) and the copy
  (`rm_scrap_copy`) are in `rm_pure.c` and bounded: a block wider than
  the atlas, an empty or negative one, a copy that leaves the atlas, is
  refused and writes nothing (gl1 computes `alloc_width - w` unsigned).
  Each atlas is a back end texture made when the first block is placed in
  it; a dirty atlas is light-scaled (gamma only, as gl1's `R_Upload32` of
  an image without mipmaps) and rewritten through the upload command
  buffer when a draw finds it dirty (made again when a failed upload
freed it, see Bounds below). An atlas's sampler is nearest for
  the first two and with `r_2D_unfiltered`, flat otherwise, decided at
  draw time (gl1 decides it at upload; the two differ only when
  `r_2D_unfiltered` changes after a pic is queued and nothing rewrites
  the atlas).
- **Changes in N2's `rm_image.c`** (the card said not to touch it; gl1
  puts the scrap in `R_LoadPic`, so the minimum is there): the scrap
  branch of `R_LoadPic` (`R_LoadScrap`, for `it_pic` of at most 128 by
  128, on the same conditions as gl1, including `r_nolerp_list` and
  `r_2D_unfiltered` choosing the unfiltered atlases), the coordinates
  left to the atlas, `R_ShutdownImages` not freeing an atlas's texture
  per image, `Scrap_Init` in `R_InitImages`, and `R_LightScaleTexture`
  no longer `static`. The card is therefore M, not L.
- **Not done here.** The built-in textures (`r_notexture`, the particle
  texture) are not made: nothing draws them before the world (N4) and
  particles (N9), and a texture nothing samples would only be an object
  to account for.

Bounds: the scrap allocation and copy, the batch capacity and the index
count are plain functions with host-test cases of oversized counts (an
atlas block wider than 256 or of 1,000,000, a quad count of 16,385 and
`INT_MAX` of a batch, a copy at `x = INT_MAX`); overflow is a flush or a
skipped draw (`draws skipped` in `metal_stats`), never a write past the
end. A failed atlas upload frees its id and leaves the atlas dirty. A
scrap image keeps its atlas index (`scrapnum`, its `texnum` is
`RM_TEXNUM_SCRAP`), not the atlas's texture id, and every draw looks the
texture and sampler up through the scrap (`Scrap_Texture`,
`Scrap_Sampler`), so an id freed by a failed upload and taken by the next
texture made is never drawn for a pic: the pics on that atlas draw
nothing (a pic whose atlas has no texture is a counted skip, enforced in
the back end by `rm_pipe_2d_textured`: a textured pipeline without a
texture is never encoded, and `Scrap_Update` flushes the pending batch
before it rewrites any atlas) until the next `Scrap_Update`, which makes a
dirty atlas without a texture again (an upload with id 0;
`rm_scrap_needs_upload`) instead of passing over it, so a transient
failure heals at the next draw of a scrap pic. `R_ShutdownImages` keys
on the image's scrap flag, not its id; the atlases are freed once, by
`Scrap_Shutdown`. Proved with a scratch build (not committed) whose first
atlas upload is made to fail and whose next upload, a red 4 by 4
texture, takes the freed id (1): `hud2d` 23 of 23 at score 0 and bad 0
against `gl1-metal`, the log showing the atlas made again; the same
injection into the code before this fix (images holding the id, the
dirty atlas skipped) draws the console characters of atlas 0 in the red
texture in 5 of 23 frames (worst score 0.000841, bad 0.002917) and never
makes the atlas again.

The frame ring (`RMB_RING_BYTES`, now in `header/rmb.h`) was 4 MiB and
is 8 MiB per frame in flight (24 MiB for the three). A batched quad takes
4 vertices of 48 bytes and 6 16-bit indices, 204 bytes; a full batch of
1024 quads takes 196,608 bytes of vertices and 12,288 of indices
(both multiples of the ring's 256-byte alignment), 208,896 bytes, so
4 MiB held 20 full batches (20,480 quads) and 8 MiB holds 40 (40,960);
a draw that does not fit what is left is skipped whole and counted. The
most characters one frame queues is a full console: its text is at most
`CON_TEXTSIZE` (32,768) characters at any resolution and console scale
(`con.totallines` is `CON_TEXTSIZE / con.linewidth`), plus the input
line, the download bar and the backscroll arrows (a line each,
`con.linewidth` at most `MAXCMDLINE - 2`, 1022) and 28 characters of
version and date: at most 35,862 quads. At 4K with `r_consolescale 1`
the line is 478 characters and the text 68 lines, 33,966 quads, which
the 4 MiB ring did not hold (the last 13,486 characters of such a frame
would have been skipped). The 8 MiB ring holds the worst console with
5,098 quads (about five batches) to spare for the status bar, the
centre print and the notify lines, all of them on one sheet or atlas
and batched. `test_ring_console` in the host test holds these numbers
through `mtl_ring_alloc`, the ring's own allocator (a 4 MiB ring fails
it), and that the quads past the ring's end are skipped, not written.

Measured on the build VM (T0), source `308d8073` (the pins; later commits
change tests and documents only), load averages 17.7 to 26.1 (a shared
host), every record with `MTL_DEBUG_LAYER=1`, references recorded at that
commit in a private store:

| What | Result |
|---|---|
| `compare metal hud2d --ref gl1-metal --threshold 0.005 --bad-threshold 0.001`, three release records (`--run 2`, `--run 3`) | each: 23 frames ok, worst score 0, worst bad 0 (the 23 frames differ from each other; the HUD, the crosshair and the status numbers are drawn over the `r_clear` colour) |
| the same record with the `ASAN=1` build (`PINS_BIN=debug/quake2`, `PINS_TRACE=0`) | 23 frames ok, score 0, bad 0, no sanitizer line |
| `compare metal cinematic --ref gl1-metal` (`PINS_DEMO=idlog.cin PINS_EVERY=7`) | 12 frames ok, worst score 0, worst bad 0 |
| validation and teardown, the four `hud2d` records | `engine.log` holds only `Metal API Validation Enabled`; teardown `0 live objects` (texture 6/6, buffer 5/5), `0 unreleased`, `0 unmatched` |
| `metal_stats 1`, `hud2d` outside the harness | every one of the 24 lines `12 pipelines, 0 created after init`, `0 draws skipped`, `0 uploads skipped`; 2,121 draws and 10 uploads over 707 frames, 0 pass splits |
| mutants of the host test | 28 mutants of the pure code (the allocator's loop bound, strictness, height check, raised columns and result; the copy's bounds, pitch and sign; the character mask, space test, top clip, column, scale and row ratio; the pic factor; the tile's clip, ratios and offset bound; the batch's overflow-safe sum, zero add and cap; the indices' capacity, 16-bit bound and second triangle; the tint's alpha and blue) all fail it; two cases (a non-square atlas block, a non-square tile) were added after the first run left one mutant alive |
| mutants of the atlas state (review rework) | 15 mutants of `rm_scrap_needs_upload` (always, never, the old skip of a missing texture), `rm_scrap_texture` (each bound, the count against the table size, the result) and `rm_scrap_sampler` (each bound, the no-filter count, the cvar, both classes) all fail the host test without sanitizers (an unrefused index -1 reads a guard entry) |
| a mutant of the front end, `RM_Flush2D` removed from `EndFrame`, one `hud2d` record | 2 of 23 frames over the threshold (worst score 0.0134, bad 0.0315), so the pin does see the order of the flush |
| `tests/check-ref-metal.sh` | host test 201 checks, 0 failed; 72 vocabulary mutants refused; `ref_metal` built (after the review rework: 227 checks, and the flush points below) |
| the flush points, by source inspection (review rework) | `tests/check-ref-metal.sh` (d) requires `RM_Flush2D();` as a statement, outside comments, in `RI_RenderFrame` before `rmb_pass_3d`, in `RI_EndFrame` before `rmb_frame_end` and in `R_ScreenShot` before `rmb_read`, and fails when a function is not found; its 12 scratch mutants (each flush removed, commented out, moved after the call, the function renamed) are refused, and scratch copies of the tree with each of the three flushes deleted fail the check, naming the function |

Menu and console screens have no pin. The shim's `screenshot` command
(`ref_gl1` built with `WITH_METAL_SHIM=yes`) writes an all-black image on
a frame that draws 2D only (four of four attempts at review: the
console, the main menu, the options menu and the quit screen; card
FIX42), so it gives no reference there. Those screens were judged by
viewing `ref_metal`'s own screenshots of the same four states (the
console background and text, the faded console under the menu, the menu
pics and the cursor), not by a comparison against the shim.

The implementer did not run the full gate (`scripts/test.sh`): the host
limits of this card keep it to the orchestrator; the checks above are
its parts that this change touches. The change is about 1,170 lines
added against the card's estimate of about 600: the estimate counted
`rm_draw.c` and the scrap, not the bounded pure functions with the
host-test cases and mutant rework (about 490 of the lines are the pure
functions, their declarations and `rm_host_test.c`), the batch draw in
`rmb_encode.m`, and the `rm_image.c` hooks.

Left for N4 and later: the built-in textures with the world and the
particles, and the sampler of an atlas after a change of `r_2D_unfiltered`
(here it follows the cvar at once; gl1 sets an atlas's filters when the
atlas is uploaded and in `R_TextureMode`'s loop over the images, and no
scenario changes the cvar).

## 19. As built: N4, world geometry, fullbright surfaces and the screen blend (TASK87)

What exists:

- **Registration** (`rm_model.c`, `header/model.h`, from `gl1_model.c`):
  `Mod_ForName`, `Mod_LoadBrushModel` with `Mod_LoadFaces`,
  `Mod_LoadLeafs`, `Mod_LoadMarksurfaces`, `Mod_LoadSubmodels`,
  `Mod_CalcSurfaceExtents`, the shared loaders of `files/models.c` for the
  rest, `RI_BeginRegistration`, `RI_RegisterModel`, `RI_EndRegistration`
  (gl1's rule: models and images are freed only when either table runs
  short; N2 freed the unused images at every registration) and
  `modellist`. Alias and sprite models load through `files/models.c` and
  are not drawn (N7). The warp subdivision (N6) and the lightmaps (N5) are
  not built; a surface has no `glpoly_t`.
- **The retained buffers** (section 9), built once per map after the faces
  are loaded: a vertex buffer of gl1's `LM_BuildPolygonFromSurface`
  values for every surface, sky and warped ones included (unwarped), a
  `uint32` index buffer of the fans in gl1's `R_SetBufferIndices` order,
  and one 48-byte record per surface (normal flipped for `SURF_PLANEBACK`,
  material class, texinfo flags, image slot, lightmap slice -1 and an empty
  rectangle until N5, styles, index range), all in surface order, so each
  submodel's index range (`firstindex`, `numindices` of its `model_t`) is
  contiguous. The buffers are private, written by a blit on the upload
  command buffer from a shared copy (`rmb_buffer_static`); the CPU copies
  stay (the frame's indices are copied from them, and N6 needs the
  vertices). The lightmap coordinates use `light_s` and `light_t` of 0
  until N5 places the surfaces.
- **The view** (`rm_main.c`): `R_SetupFrame`, `R_SetFrustum` of
  `files/surf.c`, and gl1's `R_SetupGL` and `R_SetPerspective` as pure
  functions of `rm_pure.c` (`rm_view_rect`, `rm_view_matrix`,
  `rm_projection` with `gl1_waterwarp`'s under-water wobble), each matrix
  built through `common/mtl_matrix.c` in the shim's order (each product on
  the right, the projection on the identity), the draw's product
  `projection * modelview` as the shim's `mgl_mat_mul`.
- **The world** (`rm_surf.c`, from `gl1_surf.c`): `R_MarkLeaves` (the
  bounded PVS below), `R_RecursiveWorldNode`, the texture chains of gl1's
  multitexture path (which gl1 takes on the reference, with the lightmap
  unit left off under `r_fullbright`), `R_DrawWorld`. A chain is one
  indexed draw of the vertex buffer, its surfaces' fans copied in chain
  order into the frame ring; the `world_tex` pipeline returns the texel
  (gl1's `GL_REPLACE` at colour 1), depth `LEQUAL` with writes, GL's
  front faces culled with `r_cull` (counter-clockwise front, as the shim
  sets it), the viewport from the refdef. Sky and translucent surfaces
  (at the walk), warped surfaces (the chains' second loop) and flowing
  surfaces (opaque, which gl1 scrolls) are not drawn and are counted.
- **The screen blend** (`R_PolyBlend`): `rm_polyblend` decides as gl1
  (`gl_polyblend`, the alpha) and gives the colour times `sw_gamma`; one
  quad of gl1's four vertices through the blend modelview and the view's
  projection, the blended `color` pipeline, no depth test, the 3D pass's
  last draw.
- **`r_notexture`** (`rm_misc.c`, gl1's `R_InitParticleTexture` without the
  particle texture, N9): made at init, marked in use by
  `R_FreeUnusedImages` as gl1 does, handed to `Mod_LoadTexinfo` as the
  fallback of a texture that does not load, freed with the other images at
  shutdown.
- **`metal_stats`**: the per-frame line ends with the skipped surfaces of
  the last view by class (`skipped surfaces: N sky, N warped, N
  translucent, N flowing`); a per-map line at registration gives the
  vertex, index and record counts and bytes. `RM_STATS_LINE` is 768
  bytes (the longest line is host-tested). 14 pipeline states (12 2D,
  `world_tex`, `color`) and two depth-stencil states are built with the
  context.

Bounds (section 2, "Inputs and bounds"). What the shared loaders already
check is not repeated: a lump's size against its record size, a node's
plane and children, `Mod_LoadNodes`' leaf numbers; gl1's own checks of a
face's plane and texinfo, a leaf's marksurfaces and a marksurface's
surface are kept. Added, before a loader or the buffer builder reads the
data, each through a plain function of `rm_pure.c`:

- every lump inside the file, and the header inside it (`rm_lump_ok`);
- the counts the loaders size arrays by within `files.h`'s limits (nodes,
  planes, vertices, edges, surfedges, leaf faces, leafs, faces, texinfos,
  models; lighting and visibility bytes), so `Mod_LoadNodes`' arrays of
  `MAX_MAP_LEAFS` and the hunk size cannot overflow;
- a texinfo's texture name terminated within its 32 bytes and every
  `nexttexinfo` chain inside the lump and ending or returning to its start
  (`rm_texinfo_chains_ok`; `Mod_LoadTexinfo` follows them unchecked and
  counts each chain in a loop that never ends on a cycle that misses its
  start);
- the visibility header's offsets inside the lump before
  `Mod_LoadVisibility` swaps them in place (`rm_vis_header_ok`), and every
  PVS and PHS offset inside it after; the PVS row is decompressed by
  `rm_decompress_vis`, bounded at both ends, instead of `files/pvs.c`'s
  `Mod_DecompressVis`, which reads and writes unbounded; a leaf's
  cluster below the cluster count and its area inside the client's area
  bits (`rm_leaf_ok`);
- a face's edges (at least 3, inside the surfedges), each surfedge's edge
  and both of its vertices (`rm_face_edges_ok`, `rm_surfedge_ok`), before
  `Mod_CalcSurfaceExtents`;
- a node's surfaces inside the map and in no other node (`rm_mark_once`:
  a surface in two nodes would make its texture chain a loop), a
  submodel's faces inside the map (`rm_range_ok`); a negative headnode is
  a leaf and is accepted as gl1 accepts it (base2's `*15`; N8 must not
  mark lights from it);
- the buffers' counts and bytes (`rm_world_add`: surfaces, vertices and
  indices against `MAX_MAP_FACES`, `MAX_MAP_SURFEDGES` and three times
  that; `rm_world_bytes`: each product against `RMB_STATIC_MAX_BYTES`, 32
  MB, by division) before anything is allocated, and each fan against
  the index buffer's capacity (`rm_world_fan`) as it is written;
- a lighting offset outside the lump gives no samples (gl1 points past
  the lump), for N5 to bound.

A failed check frees the model (hunk, file, buffers) and drops with
`ri.Sys_Error(ERR_DROP)`. Not prevented here, because it happens inside a
shared loader before any check of this module can run: a node graph with a
cycle recurses without end in `Mod_SetParent` (`files/models.c`), as in
gl1; that is a defect of `files/` for a parked card.

Choices made where the design left room:

- **The vertex is 32 bytes, not packed.** The float3 position is at offset
  0 with w = 1 stored after it, then the two coordinate pairs. With the
  packed 28-byte vertex the vertex function widened the position with a w
  of 1 (a constant or a uniform, both tried), and 21 of the 23 frames of
  `world-fullbright` differed from `gl1-metal` (score up to 0.000017, bad
  up to 0.000046): texels moved on 0.1 to 1 percent of the pixels,
  scattered along the bands where the mip level changes, from clip
  coordinates differing in the last bits. Reading one float4 from the
  buffer, as the shim's vertex function does, brought 15 of the 19 judged
  frames to score 0 and bad 0. An acceleration structure reads the float3
  at offset 0 with a vertex stride of 32 bytes; the screen blend's
  positions are float4 too.
- **A frame `r_clear` does not clear starts black**, as on the shim (its
  first pass of a frame clears to black, "so a frame is reproducible"),
  not on the last frame as N1 chose (section 16): the world's T-junction
  cracks show what is under them, which made a pixel of frame 16 differ.
  `rm_color_load` takes the frame's first pass.
- The skipped-surface counts end the existing per-frame line rather than
  adding a line per frame; the per-map line is a line of its own.

Measured on the build VM (T0), source `04c99db2` (the pins; later commits
change documents and comments only), load averages 2.7 to 10.9 (a shared host), every
record with `MTL_DEBUG_LAYER=1` and `PINS_TRACE=0`, references recorded at
that commit in a private store:

| What | Result |
|---|---|
| two `gl1-metal` records of `world-fullbright` (`--run 2`), `--threshold 0 --bad-threshold 0` | 23 frames, score 0, bad 0 |
| frames excluded by section 11 (`metal_stats 1`: a sky, warped, translucent or flowing surface skipped) | 19 (9 sky, 3 translucent), 20 (3 translucent), 21 (31 sky, 32 translucent), 22 (9 sky, 3 translucent); frames 0 to 18 skip none |
| `compare metal world-fullbright --ref gl1-metal --threshold 0.005 --bad-threshold 0.001`, three release records | each the same: frames 0 to 20 ok (19 judged, 2 excluded but passing); 15 at score 0 and bad 0, frames 0, 1, 10 and 16 at score 0.000000 and bad 0.000003, 0.000010, 0.000013 and 0.000003; frames 21 (0.055006, bad 0.266624) and 22 (0.000631, bad 0.001859) over, both excluded |
| the same with the `ASAN=1` build (`PINS_BIN=debug/quake2`) | the same numbers, no sanitizer line |
| frames 21 and 22, viewed | 21: the sky (black where `gl1-metal` draws the sky box) and the translucent water at the bottom left (the floor under it shows); 22: the sky in the top left corner. Both N6's |
| the residual pixels of frames 0, 1, 10, 16, viewed | 2, 5, 6 and 3 pixels, 2 by 2 quads and short runs inside surfaces, colour differences of 2 to 16 levels; unchanged by drawing the chains in reverse order (a scratch build), so not the draw order; not explained further |
| the screen blend, by a scratch run of `metal` with `+set gl_polyblend 0` against the same references | frames 5, 8, 11 and 16 over (bad 0.84 to 0.98 of the pixels), frame 3 at score 0.004944, frames 2, 6, 9, 12, 14, 15 and 17 differing by up to 8 levels: 13 of the 19 judged frames carry a blend, and with it drawn all of them pass; no frame is excluded for it |
| `compare metal hud2d` and `compare metal cinematic` (`PINS_DEMO=idlog.cin PINS_EVERY=7`) against `gl1-metal` at the same commit | 23 and 12 frames, score 0, bad 0 |
| validation and teardown, every record above | `engine.log` holds only `Metal API Validation Enabled`; teardown `0 live objects` (texture 223/223, buffer 8/8 for `world-fullbright`), `0 unreleased`, `0 unmatched` |
| `metal_stats 1`, `world-fullbright` outside the harness | every one of the 24 lines `14 pipelines, 0 created after init`, `0 draws skipped`, `0 uploads skipped`, `0 pass splits`; 19,235 draws over 707 frames |
| the per-map line, the map of `demo1.dm2` | `maps/base2.bsp`: 44,502 vertices (1,424,064 bytes), 77,568 indices (310,272 bytes), 9,323 surface records (447,504 bytes) |
| a map change (`map base1`, then `map base2`, a scratch config) | base1: 37,577 vertices (1,202,464 bytes), 65,301 indices (261,204 bytes), 7,905 records (379,440 bytes); `imagelist` after each map lists ` W   8   8 RGB: ***r_notexture*** (8x8)`; teardown `0 live objects` |
| timedemo, the release records | `metal` 1092 to 1113 fps, `gl1-metal` 444 to 462 fps (689 frames); `ASAN=1` `metal` 462 fps |
| `tests/check-ref-metal.sh` | host test 359 checks, 0 failed (with N3's rework cases) |
| mutants of the world's pure code and of what it takes from `files/surf.c` | 45 mutants (the material order, the normal flip, each limit and division of the size arithmetic, the fan's order, capacity and 32-bit bound, the range, lump, face, surfedge, leaf, area and texinfo-chain checks, the PVS decompression's run cut, input end and output size, the viewport flip, each rotation, the translation, the wobble, `gl_znear`'s floor, `r_farsee`, the blend's alpha test and gamma, the per-map line, the skipped counts, the black first pass; `R_SetFrustum`'s angles and distances, `R_CullBox`'s side and planes, `R_TextureAnimation`'s step and negative frame) all fail the host test; five cases were added after the first run left five alive; two mutants are equivalent and were dropped (`ceil` for `floor` in gl1's viewport formula, which is the identity on integer rectangles, and the `gl1_waterwarp` guard, whose wobble is 0 when it is 0) |

The texture used for a surface whose image did not load is `r_notexture`
by `Mod_LoadTexinfo`'s fallback argument; the game data has no such
surface, so no frame shows it.

The implementer did not run the full gate (`scripts/test.sh`): the host
limits of this card keep it to the orchestrator; the checks above are its
parts that this change touches. The change is about 3,700 lines added
against the card's estimate of about 1,150 (earlier cards ran about 2.5
times theirs). About 1,000 of them are gl1's registration, walk and view
code copied into `rm_model.c`, `rm_surf.c` and `rm_main.c`; the bounds
the shared loaders leave open (the lump, count, texinfo, visibility, edge
and node checks with their refusal path) add about 350 to `rm_model.c`
and about 250 to `rm_pure.c`; the back end's static buffers, 3D pipelines,
depth states and two draws about 340; the host test about 550 (one or
more cases per check, the shim's raster and `files/surf.c` linked in);
`rmb.h` about 270 of declarations with their contracts. The card was not
cut into two slices: the measurement that set the vertex layout needed
the drawing, and both halves were done when the size was counted.

Left for N5 and later: the lightmaps (`light_s`, `light_t`, the slice and
rectangle of the record, the samples' bounds), the warp subdivision and
the sky, warped, translucent and flowing surfaces (N6, which may also
mutate gl1's translucent texinfo images as gl1's walk does), brush
entities and alias models (N7), the clear of the `RDF_NOWORLDMODEL`
rectangle (N7, the player model of the menu), `R_SetLightLevel` (N5), the
particle texture (N9).
