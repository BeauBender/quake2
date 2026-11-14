# Design: the OpenGL-subset-on-Metal shim for `ref_gl1` (TASK21)

Plan anchor: `docs/plans/metal-renderer.md` as revised by INFORM8 (#35, pin at
two seams, shim first). Oracles: the GL-call pins (INFORM8) for each entry
point, and the frame pins from `scripts/pins.sh` (`docs/plans/metal-port-harness.md`).
Rule of the card: `src/client/refresh/gl1/*.c` is not edited. Everything
below is additive and selected by one Makefile variable.

## What the shim actually has to cover (measured on this tree)

| Fact | Value | Consequence |
|---|---|---|
| Distinct `gl*` entry points called from `gl1/*.c` | 54 direct, 435 call sites, plus 5 loaded by pointer with 6 call sites (`qglActiveTexture`, `qglClientActiveTexture`, `qglPointParameterf`, `qglPointParameterfv`, `qglColorTableEXT`); see `docs/analysis/gl1-state-inventory.md` | small, finite surface; one Khronos spec per symbol |
| Immediate mode (`glBegin`/`glEnd`) | none | no per-vertex capture path; everything is `glVertexPointer` + `glDrawArrays` (19) and one `glDrawElements` |
| Primitive modes | `GL_TRIANGLE_FAN` 21, `GL_TRIANGLE_STRIP` 5, `GL_TRIANGLES` 2, `GL_POINTS` 2, `GL_LINES` 1, `GL_LINE_STRIP` 1 | fans and strips need an index expansion (Metal has no fan) |
| Fixed-function state used | blend, depth test/mask/func/range, alpha test, cull, stencil (op/func/mask/clear), scissor, polygon offset, polygon mode, color mask, shade model, point size | one PSO key tuple plus a per-draw uniform block |
| Texture environment | `GL_MODULATE` 10, `GL_REPLACE` 14, `GL_COMBINE` 5, two units (`GL_TEXTURE0`/`1`, lightmaps) | one fragment function with a `texenv` uniform, or three specializations via function constants |
| Matrix stack | `glMatrixMode`, `glLoadIdentity`, `glLoadMatrixf`, `glRotatef`, `glTranslatef`, `glScalef`, `glOrtho`, `glFrustum`, `glPushMatrix`/`glPopMatrix` (6 each), `glGetFloatv` | CPU-side stacks, uploaded per draw |
| Uploads | `glTexImage2D` 15 sites, `glTexSubImage2D` 3 (`gl1_lightmap.c:84-97` dynamic lightmaps, `gl1_misc.c` scrap), `glPixelStorei` | staging buffer + blit encoder, lightmaps per frame |
| Readback | one `glReadPixels` at `gl1_misc.c:133` (`R_ScreenShot`) | blit drawable to a shared `MTLBuffer`; feeds `ri.Vid_WriteScreenshot` and so the pin harness |
| Window attach | `gl1_sdl.c`: `RI_PrepareForWindow` returns `SDL_WINDOW_OPENGL`; `RI_InitContext` calls `SDL_GL_CreateContext`; swap is `SDL_GL_SwapWindow`; vsync via `SDL_GL_SetSwapInterval` | the shim also stands in for the `SDL_GL_*` calls (six symbols) |
| GL header | `header/qgl.h:38` `#include <OpenGL/gl.h>` on Apple | an include-path override resolves that to the shim's header |

## 1. Module layout and build selection

```
src/client/refresh/gl1/metal/
  include/OpenGL/gl.h      the subset header: enums, typedefs, the 54 prototypes (plus the 5 qgl ones)
  include/OpenGL/glext.h   empty; satisfies any transitive include
  shim_sdl.h               macro remap of the six SDL_GL_* calls and SDL_WINDOW_OPENGL -> SDL_WINDOW_METAL
  mgl_state.c              GL state machine: enable bits, blend/depth/stencil/cull, matrices, client arrays, texenv
  mgl_draw.c               glDrawArrays/glDrawElements -> encoder draws; fan/strip index expansion; PSO cache
  mgl_tex.c                texture objects, glTexImage2D/glTexSubImage2D staging, sampler cache, glPixelStorei
  mgl_frame.c              frame begin/end, drawable, glClear, scissor/viewport/depth range, glReadPixels, glFinish
  mgl_layer.m              Objective-C: CAMetalLayer from SDL_Metal_CreateView, device, queue, drawable acquisition
  shaders.metal            one vertex function; fragment variants via function constants (texenv x alphatest x units)
  mgl_internal.h           shared structs; nothing here is visible to gl1
```

Selection, Makefile only, Darwin only:

```
WITH_METAL_SHIM ?= no
ifeq ($(YQ2_OSTYPE)/$(WITH_METAL_SHIM), Darwin/yes)
$(BINDIR)/ref_gl1.dylib : CFLAGS  += -I src/client/refresh/gl1/metal/include \
                                     -include src/client/refresh/gl1/metal/shim_sdl.h
$(BINDIR)/ref_gl1.dylib : LDFLAGS := $(filter-out -framework OpenGL,$(LDFLAGS)) \
                                     -framework Metal -framework QuartzCore -framework Foundation
REFGL1_OBJS_ += $(METAL_SHIM_OBJS_)       # the .c and .m files above, plus the compiled metallib step
endif
```

`-I .../metal/include` makes `#include <OpenGL/gl.h>` in `qgl.h` pick up the
shim header before the system framework header (framework headers are
searched after `-I` paths). `-include shim_sdl.h` prepends the macro remap
to every gl1 translation unit, so `gl1_sdl.c`'s `SDL_GL_SwapWindow(window)`
compiles as `mgl_SwapWindow(window)` without touching the file. The
`.metal` source compiles with `xcrun metal` into `ref_gl1.metallib` placed
next to the dylib and loaded with `newLibraryWithURL:`; the Makefile rule
fails loudly when `xcrun` is absent. The system OpenGL framework is never
linked, which is the point: the dylib runs where OpenGL does not.

`QGL_Init` in `qgl.c` resolves `qglClientActiveTexture` through
`RI_GetProcAddress`, which is `SDL_GL_GetProcAddress`; the remap sends it to
`mgl_GetProcAddress`, a static string-to-symbol table of the shim's own exports.

## 2. Core design

**One command buffer per frame, one render pass for the frame.** `mgl_BeginFrame`
(called from the remapped `SDL_GL_SwapWindow` on the previous frame's end,
and lazily on the first GL call of a frame) acquires the drawable, builds a
`MTLRenderPassDescriptor` with color = drawable texture, depth = a private
`depth32Float_stencil8` texture sized to the drawable, and opens one
`MTLRenderCommandEncoder`. `glClear` inside a frame (gl1 calls it in
`R_Clear` at `gl1_main.c:891`, and in stereo at `:721`/`:1947`) ends the
encoder and opens a new pass with the matching load actions; outside a frame
it sets the load actions of the next pass. Nothing is deferred beyond the frame.

**State machine.** `mgl_state.c` keeps the full GL state the 54 calls can
touch in one `mgl_state_t`: enable bitmask (blend, depth, alpha test, cull,
scissor, stencil, polygon offset, texture per unit), blend factors, depth
func/mask/range, stencil func/ref/mask/op, cull face, color mask, polygon
mode, shade model, point size, current color (`glColor4f` 28 sites,
`glColor4ub`), active and client-active unit, client array pointers
(`glVertexPointer` 20, `glTexCoordPointer` 10, `glColorPointer` 3, the enable
flags from `glEnableClientState` 33), matrix mode and two stacks (modelview,
projection; depth 32), texenv per unit, bound texture per unit. Every
`gl*` call only mutates this struct; nothing touches Metal until a draw.

**Draw path.** `glDrawArrays(mode, first, count)` and the one
`glDrawElements`:
1. Resolve the PSO from the cache keyed on the tuple `(vertex layout:
   which client arrays are enabled and their types/strides, blend enable +
   factors, depth write, alpha test enable, texenv unit0, texenv unit1,
   units enabled, polygon mode, color mask)`. Depth func, cull, scissor,
   viewport, depth bias, stencil, point size are encoder state, not PSO.
2. Copy the referenced client-array ranges into the per-frame vertex ring
   (one `MTLBuffer` of 8 MB, `storageModeShared`, triple-buffered by frame
   index, bump allocated; `gl1_buffer.c` already batches, so copies are
   few). Set them as vertex buffers 0..2.
3. Fans and strips: `GL_TRIANGLE_FAN` and `GL_LINE_STRIP` are expanded
   into an index list in the per-frame index ring and drawn with
   `drawIndexedPrimitives`; `GL_TRIANGLE_STRIP`, `GL_TRIANGLES`, `GL_LINES`,
   `GL_POINTS` map 1:1 to `MTLPrimitiveType`.
4. Upload the per-draw uniform block (modelview*projection, modelview,
   current color, alpha test ref, texenv modes, combine scale, point size)
   from the uniform ring; set textures and samplers for the enabled units.
5. `drawPrimitives`/`drawIndexedPrimitives`.

**Shaders.** One vertex function: position, up to two texcoords, color;
`shadeModel` is irrelevant (gl1 never relies on flat shading for
correctness; pins decide). One fragment function with function constants
`TEXENV0`, `TEXENV1` in {modulate, replace, combine}, `ALPHA_TEST`,
`UNITS` in {0,1,2}: at most 3 x 3 x 2 x 3 = 54 variants, created lazily and
cached by the PSO key. `GL_COMBINE` here is the lightmap path
(`gl1_surf.c:268-371`, `gl1_buffer.c:168-197`): modulate with
`GL_RGB_SCALE` set from `gl1_overbrightbits` (1, 2 or 4); the scale is a
per-draw uniform and the constant selects that exact formula, not general
combiners. `GL_POINTS` for particles (2 sites)
draw as points with `[[point_size]]` from the uniform; `GL_LINES` for
`r_showtris`-style debug keep line primitives.

**Textures.** `glGenTextures`/`glBindTexture`/`glDeleteTextures` map a GL
name to a `MTLTexture` (`rgba8Unorm`, private storage) created on the first
`glTexImage2D` for level 0, mipmaps on the levels gl1 uploads (gl1 uploads
its own mip chain). Uploads go through a 16 MB shared staging ring and a
blit encoder opened on the frame's command buffer before the render encoder
(or, outside a frame, on a one-off command buffer committed immediately).
`glTexSubImage2D` (dynamic lightmaps, `gl1_lightmap.c:84-97`, up to a few
per frame; the scrap at `gl1_misc.c`) is the same staging path with a
region. Because a texture written this frame is also read this frame, the
blit is encoded before the render pass and the render pass waits on it
implicitly (same command buffer, encoder order). `glTexParameteri` (38 sites)
sets the sampler state key (min/mag filter, mip filter, wrap per axis,
anisotropy from `gl_anisotropic`); samplers are cached by key. `glPixelStorei`
unpack alignment is honoured in the staging copy.

**Readback.** `glReadPixels(0,0,w,h,GL_RGB,GL_UNSIGNED_BYTE)` at
`gl1_misc.c:133`: end the current encoder, blit the drawable (or the
offscreen color texture when the drawable is not readable) into a shared
`MTLBuffer`, commit, `waitUntilCompleted`, convert BGRA to RGB bottom-up
into the caller's buffer. Expensive by design and only used for screenshots,
which is exactly what the pin harness calls.

**Other calls.** `glViewport`, `glScissor`, `glDepthRange` are encoder state
with the GL-to-Metal y flip (`gl1` uses bottom-left origin, Metal top-left:
flip `y = drawableHeight - y - h`). `glPolygonOffset` is
`setDepthBias:slopeScale:clamp:`. `glPolygonMode` line is
`setTriangleFillMode:`. `glColorMask` is part of the PSO key (color write
mask). `glDrawBuffer` is a no-op (single color attachment).
`glGetString` returns constant strings naming the shim (`gl1` prints them
at init). `glGetError` returns the shim's sticky error (set when a call
hits an unimplemented path; the pins read it). `glFinish` commits and waits.
`glHint` no-op. `glGetFloatv` serves `GL_MODELVIEW_MATRIX`/`GL_PROJECTION_MATRIX`
from the stacks (the two call sites).

## 3. Frame sync and present

`SDL_WINDOW_OPENGL` is remapped to `SDL_WINDOW_METAL`; `RI_InitContext`'s
`SDL_GL_CreateContext(window)` becomes `mgl_CreateContext(window)`:
`SDL_Metal_CreateView(window)`, `SDL_Metal_GetLayer(view)` gives the
`CAMetalLayer` (`mgl_layer.m`), `MTLCreateSystemDefaultDevice`, one command
queue, `layer.pixelFormat = bgra8Unorm`, `maximumDrawableCount = 3`,
`framebufferOnly = NO` (so `glReadPixels` can blit from it),
`drawableSize` from `SDL_GetWindowSizeInPixels` (the HighDPI flag gl1 reads
stays correct). `SDL_GL_SetAttribute` calls record the request (stencil 8,
depth 24, MSAA samples) into the layer config: MSAA becomes a multisampled
color texture plus resolve to the drawable, stencil is always present in the
`depth32Float_stencil8` attachment. `SDL_GL_SetSwapInterval(v)` sets
`layer.displaySyncEnabled = (v != 0)`; `SDL_GL_GetSwapInterval` reads it back.

`SDL_GL_SwapWindow` is the frame boundary: end the encoder, `presentDrawable`,
`commit`, signal the frame semaphore in `addCompletedHandler`, advance the
ring index (3 in flight; the semaphore waits when three frames are
outstanding), and mark "no frame open" so the next GL call opens the next
one. `RI_BeginFrame`/`RI_EndFrame` in `gl1_main.c` are unchanged; the shim
sees them only as the GL calls they make.

## 4. Growth order, driven by the pins

The GL-call pins (INFORM8) are grown in trace-frequency order; the frame
pins (`scripts/pins.sh`) accept each stage. Suggested stages, each a
`DrawArrays` family the shim must handle before the stage's scenario can pass:

| Stage | Entry points brought up | Frame scenario that gates it |
|---|---|---|
| 0 | context, swap, clear, viewport, `glGetString`, `glGetError` | engine boots to the console with `vid_renderer gl1`, `WITH_METAL_SHIM=yes`; no pins yet |
| 1 | 2D: ortho matrices, `glColor4f`, textures + samplers, `GL_REPLACE`/`GL_MODULATE`, blend, alpha test, `GL_TRIANGLE_FAN` expansion, `glReadPixels` | `hud2d` (console and pics are 2D, drawn over a world frame: run it with `r_drawworld 0` first as a 2D-only sub-check, then as filed) |
| 2 | World: frustum matrix, depth test/mask, cull, two units + `GL_COMBINE` lightmaps, `glTexSubImage2D` dynamic lightmaps, polygon offset, strips | `world` |
| 3 | Models and view weapon: `glDepthRange` (weapon), stencil (shadows), `glColorPointer`, `glPushMatrix`/`PopMatrix` | `models` |
| 4 | Particles, dlights, beams: `GL_POINTS` with point size, `GL_LINES`, additive blend | `particles` (looser threshold, nondeterministic) |
| 5 | Sky and water (`gl1_warp.c`): warped texcoords are CPU-side in gl1, so no new GL surface; cinematics: `glTexImage2D` per frame | `world` already covers sky/water; a `cinematic` scenario is filed when a demo with one exists in the owner's data |

A stage is done when every GL-call pin for its entry points passes and the
scenario's frames pass at the recorded threshold against `gl1` on real
OpenGL (T1 references, INFORM9).

## 5. Risks and unknowns

| Risk | Mitigation |
|---|---|
| Apple's OpenGL is deprecated and unverified on the VM's paravirtual GPU, so T0 cannot produce `gl1` references | references come from a bare-metal Mac (T1, INFORM9); on T0 the shim is judged against `soft` frames and the synthetic GL-call pins only |
| `GL_TRIANGLE_FAN` has no Metal equivalent (21 sites) | index expansion in the draw path; cost is one index write per vertex per frame, trivial at Quake II sizes |
| `glClear` mid-frame splits the render pass | encoder restart with explicit load actions; stereo paths (`gl1_main.c:721`, `:1947`) are the only extra splits |
| Dynamic lightmap `glTexSubImage2D` into a texture read later in the same frame | blit encoder before the render encoder on the same command buffer; if a sub-image arrives after the encoder opened, end and reopen the encoder (rare: gl1 updates lightmaps in `R_DrawWorld` before drawing them) |
| `GL_POINTS` particle size: GL point size is in pixels at the raster stage, Metal's `[[point_size]]` likewise, but `gl_particle_*` cvars may scale by distance in the vertex path in gl1 | pass the computed size through the uniform; `particles` scenario is accepted at the looser threshold anyway |
| Fixed-function fog | not used by this gl1 (no `GL_FOG` sites); nothing to do |
| `glPolygonMode` line (wireframe debug) and `glShadeModel` flat | implement line fill mode; ignore flat shading, document it as a known difference the pins must tolerate |
| Apple family floor | `MTLGPUFamilyApple5` per the plan; nothing in this design needs more than family apple1 features except `depth32Float_stencil8` and `framebufferOnly = NO`, both universal |
| Performance of per-draw uniform upload and PSO lookup | PSO key is a 64-bit packed tuple with an open-addressing table; uniforms are bump-allocated in a shared ring, 256-byte aligned; expect gl1's draw count (hundreds per frame) to be far below any limit |
| Readback coordinate conventions (GL bottom-up vs Metal top-down) | handled once in `glReadPixels`; the pin harness compares shim frames against real-GL frames, so a flip error fails every pin on stage 1 and is caught immediately |

## 6. Size and the first three cards

Filing: `docs/plans/metal-port-cards.md` is the authoritative card list; the
list below is the detailed scope behind its rows.

| Component | Estimate |
|---|---|
| `include/OpenGL/gl.h` subset header | 300 lines |
| `mgl_state.c` | 700 |
| `mgl_draw.c` incl. PSO cache and fan expansion | 600 |
| `mgl_tex.c` | 450 |
| `mgl_frame.c` incl. readback | 350 |
| `mgl_layer.m` | 200 |
| `shaders.metal` | 150 |
| Makefile and metallib rule | 40 |
| Total | about 2.8k lines, against gl1's 10k untouched |

Cards to file, in order (each `depends:TASK27`, `epic:metal-renderer`):

1. **TASK21a — Shim skeleton: build selection, context, swap, clear, state
   struct, `glGetString`/`glGetError`.** Acceptance: on a Mac with
   `WITH_METAL_SHIM=yes`, `ref_gl1.dylib` links without the OpenGL framework
   (`otool -L` shows Metal and no OpenGL), the engine boots to the console
   and quits cleanly under `pins_capture`; every GL-call pin for the stage-0
   entry points passes.
2. **TASK21b — 2D path: textures, samplers, replace/modulate, blend, alpha
   test, fan expansion, `glReadPixels`.** Acceptance: GL-call pins for the
   stage-1 entry points pass; `scripts/pins.sh compare gl1-metal hud2d --ref gl1`
   passes at the threshold recorded for `hud2d` with the world disabled, and
   the console screenshot is pixel-identical to real GL except for text
   anti-aliasing (score below 0.005).
3. **TASK21c — World and lightmaps: perspective matrices, depth, cull, two
   units with combine, dynamic lightmap sub-uploads, polygon offset.**
   Acceptance: stage-2 GL-call pins pass; `scripts/pins.sh compare gl1-metal
   world --ref gl1` passes at 0.02 on every frame; the dynamic-lightmap
   frames (a dlight passing over a wall in `demo1`) are within the same
   threshold, which proves the same-frame staging path.

`gl1-metal` above is the renderer name the harness records under when the
dylib was built with the shim (the pin directory `<scenario>/gl1-metal/`).
The pin manifest names the module the engine loaded, `gl1`; the build is
named by `glGetString(GL_RENDERER)`, which ends in `(gl1-metal)` because
the Makefile sets `-DMGL_RENDERER_SUFFIX` (section 7, "The renderer name").

## 7. As built by TASK47 (S1): departures from sections 1 to 3

TASK47 built the skeleton under `src/client/refresh/gl1/metal/`. Where it
differs from the text above, this list is what the tree does.

- **Eight SDL calls, not six.** `gl1_sdl.c` calls `SDL_GL_SetAttribute`,
  `SDL_GL_GetAttribute`, `SDL_GL_CreateContext`, `SDL_GL_DestroyContext`,
  `SDL_GL_SwapWindow`, `SDL_GL_SetSwapInterval`, `SDL_GL_GetSwapInterval`
  and `SDL_GL_GetProcAddress`; `shim_sdl.h` remaps all eight and
  `SDL_WINDOW_OPENGL`. It includes `<SDL3/SDL.h>` first, so SDL's own
  declarations keep their names and only gl1's uses are renamed.
- **Makefile mechanics.** No target-specific `CFLAGS` on the dylib: they
  would reach the shim's own objects too, and both builds would share one
  object directory. The shim build compiles into
  `$(BUILDDIR)/ref_gl1_metal/` with its own rules (gl1 and the shared
  files with `-I .../metal/include -include .../shim_sdl.h`; the shim's
  sources without the remap; `mgl_layer.m` with `-fobjc-arc`). The
  OpenGL framework is left out by an `ifeq`, not `filter-out` (which
  works per word and would drop every `-framework`). A stamp per build
  (`$(BUILDDIR)/ref_gl1.{gl,metal}.mode`) relinks `ref_gl1.dylib` when
  `WITH_METAL_SHIM` changes, so `make -q` tells the two builds apart.
  `WITH_METAL_SHIM=yes` on another OS is a make error.
- **What is implemented now.** Beyond the frame calls, every entry point
  that only records state does so, with GL's argument checks: enables,
  blend, depth, alpha, stencil, cull, colour mask, polygon mode and
  offset, shade model, point size and parameters, current colour,
  active and client-active unit, `glTexEnvi`, client arrays,
  `glMatrixMode`, `glLoadIdentity`, `glLoadMatrixf`, `glPushMatrix`,
  `glPopMatrix`, `glGetFloatv` (modelview and projection only),
  `glPixelStorei`, `glHint`, `glDrawBuffer`. Stubs that set
  `GL_INVALID_OPERATION` and log their name once: `glDrawArrays`,
  `glDrawElements`, `glBindTexture`, `glDeleteTextures`, `glTexImage2D`,
  `glTexSubImage2D`, `glTexParameteri`, `glRotatef`, `glTranslatef`,
  `glScalef`, `glOrtho`, `glFrustum`, `glColorTableEXT`.
- **Extensions.** `GL_ARB_multitexture GL_ARB_point_parameters
  GL_ARB_texture_non_power_of_two`. Anisotropy is not advertised until
  `glGetFloatv` answers `GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT`; paletted
  textures never are.
- **MSAA.** A multisample request is refused (gl1 then turns
  `r_msaa_samples` off); the multisampled colour texture with resolve is
  left for a later card.
- **Clears.** The first pass of every frame clears what the frame did not
  ask to clear (black, depth 1, stencil 0) instead of leaving it
  undefined, so frames are reproducible. A clear a load action cannot
  express (a partial colour or stencil write mask, a scissor that does
  not cover the target) sets the sticky error and leaves that attachment
  as it is; the draw path can do those as a quad.
- **Bounded waits.** The frame semaphore is created at 0 and raised to 3
  (libdispatch traps when a semaphore is released below its initial
  value). A frame whose semaphore wait passes 2 s is skipped (clears do
  nothing, reads set the sticky error). `nextDrawable` returns nil after
  one second (`allowsNextDrawableTimeout`); the frame is then drawn into
  an offscreen texture of the same size, which `glReadPixels` reads, and
  nothing is presented. `glFinish` and readback wait at most 5 s.
- **Readback.** `glReadPixels` validates before it reads: format
  `GL_RGB`, `GL_RGBA` or `GL_BGRA`, type `GL_UNSIGNED_BYTE`, the
  rectangle inside the target, the pack alignment; otherwise the sticky
  error and nothing written. It also exports the bounded
  `glReadnPixels`. With `MGL_READBACK_DUMP` set (see
  `doc/100_renderpins.md`) every read is also written to that directory,
  which is how the pins_dir frames were checked against the shim's own
  readback. The hook creates every file new (`O_CREAT | O_EXCL |
  O_NOFOLLOW`), stops for the run when the directory already holds a
  `mgl-readback.txt` or a link in its place, and writes at most 1000
  reads per run (`MGL_DUMP_MAX` in `mgl_frame.c`).
- **The renderer name.** The engine writes `vid_renderer` (`gl1`) into
  the pin manifest; the build is named by the `GL_RENDERER` string, which
  ends in `(gl1-metal)` (`-DMGL_RENDERER_SUFFIX`). `scripts/pins.sh record
  gl1-metal` starts the engine with `vid_renderer gl1`, checks the
  manifest against `gl1` and requires that string in the first
  `GL_RENDERER: ` line of `engine.log`; `record gl1` refuses a log whose
  first such line has it. Only the first line counts: the renderer prints
  it at init, before the demo starts, and a demo's `svc_print` can put
  later lines of the same shape into the log.
- **Encoder state.** Viewport, scissor and depth range are set on every
  new pass and whenever they change; an empty GL scissor has no Metal
  form (the encoder gets a 1x1 rectangle and `mgl_raster_t.scissor_empty`),
  so the draw path must skip draws while it holds.
- **Pure C apart.** `mgl_compute_raster` and `mgl_read_contract` live in
  `mgl_raster.c`, which needs neither SDL nor Metal, so the gate can
  compile them on any host.
- **Gate and CI.** `tests/check-metal-shim.sh`, run by `scripts/test.sh`,
  compiles a host test (`tests/metal/mgl_host_test.c`) of `mgl_raster.c`,
  `mgl_state.c` and `glPixelStorei` with the system compiler on every OS,
  and builds `make ref_gl1 WITH_METAL_SHIM=yes` only on Darwin with
  `xcrun -f metal` and SDL 3 configured, printing `skipped: <reason>`
  otherwise. No CI workflow builds the shim: the switch is off by
  default, so the macOS job is unaffected; building it there needs the
  Metal toolchain on the runner and a workflow change, a follow-up card.
- **Switching back.** The OpenGL build of `ref_gl1` removes
  `release/ref_gl1.metallib` when its mode stamp is made, so the shaders
  of a shim build do not stay beside a dylib that does not use them.

### Notes for the next card (TASK48)

- **Criterion 3 is orientation-blind until something draws.** Every frame
  the skeleton produces is one colour, so the byte-identity of the
  pins_dir frames and the `MGL_READBACK_DUMP` reads proves the channel
  order (a `glClearColor(1, 0, 0.5)` read is `ff 00 80`) but cannot see a
  row flip; the bottom-up row order of `glReadPixels` is traced only.
  TASK48, the first card that draws, must re-check the row order with a
  frame that is not uniform.
- **Reopen a Load pass after a mid-frame flush.** After `glReadPixels` or
  `glFinish` in the middle of a frame the encoder is nil while
  `frame_passes > 0`, so `mgl_layer_ensure_pass` answers 1 with no open
  encoder (`mgl_layer.m`). The draw path must open a new pass with a
  Load action itself before it encodes.
- **A completion handler after unload.** The completion-handler blocks are
  code inside `ref_gl1.dylib`. `mgl_layer_destroy` stops waiting after
  `MGL_GPU_WAIT_MS` and the engine then unloads the module, so a
  completion that arrives later would run unmapped code. It needs a GPU
  hung past the timeout and was not reproduced; a fix must keep the
  handlers' code mapped (or the wait unbounded only on that path) without
  an unbounded wait on the normal one.
