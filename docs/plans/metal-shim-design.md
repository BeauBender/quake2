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

## 8. As built by TASK48 S2a (the 2D path): departures from section 2

- **What draws.** `glDrawArrays` and `glDrawElements` (`GL_UNSIGNED_SHORT`)
  in `GL_TRIANGLES`, `GL_TRIANGLE_STRIP` and `GL_TRIANGLE_FAN`, textured on
  unit 0 with `GL_REPLACE` or `GL_MODULATE` or untextured, with blending,
  the colour mask and the alpha test; `glOrtho`; RGBA and RGB
  `glTexImage2D` of level 0 (with `GL_GENERATE_MIPMAP`), `glBindTexture`,
  `glDeleteTextures`, `glTexParameteri` (filters, wrap). That is every
  call the `hud2d` frames make. A texture with no storage draws as if the
  unit were off (GL's incomplete texture).
- **No vertex descriptor, no function constants.** The client arrays are
  assembled on the CPU into one interleaved 48-byte vertex
  (`mgl_vertex_t`: position, two texture coordinates, colour; a disabled
  array is filled from the current colour or a zero coordinate), which the
  vertex function reads by `vertex_id`. The texture environment and the
  alpha test are a uniform (`mgl_uniforms_t`) and a branch, not function
  constants; the pipeline key is only blend enable, the two factors and
  the colour mask (13 bits). The uniforms go by `setVertexBytes` /
  `setFragmentBytes` (80 bytes), not a ring.
- **Fans and strips** are expanded on the CPU into a 16-bit triangle list
  (strips keep GL's winding: every odd triangle swaps its first two
  vertices) and drawn with `drawIndexedPrimitives`. One draw holds at most
  65536 vertices and 196608 indices (`MGL_MAX_DRAW_*`); a larger one sets
  `GL_INVALID_VALUE` and draws nothing.
- **Textures.** A fixed table of 4096 names (`MGL_MAX_TEXTURES`; gl1's
  names run to about 2500 and it never calls `glGenTextures`), a name past
  it is refused with `GL_INVALID_VALUE`. Storage is `rgba8Unorm`, private,
  with the whole mip chain; an RGB internal format stores alpha 255 and
  makes `GL_REPLACE` keep the fragment's alpha. Each upload is copied into
  its own shared staging buffer and blitted: inside a frame on the frame's
  command buffer after ending the render encoder (the next draw opens a
  Load pass, which also answers the S1 note on reopening after a flush);
  outside a frame into one pending command buffer committed before the
  next frame's. `GL_CLAMP` samples as clamp to edge.
- **Per-draw buffers.** Vertices and indices go into a new shared
  `MTLBuffer` per draw; the 8 MB per-frame ring of section 2 is S2b.
- **Readback.** The row and channel conversion of `glReadPixels` is
  `mgl_convert_readback` in `mgl_raster.c`, host-tested on a 3 x 2 image
  that differs in every row, column and channel at alignments 1, 4 and 8,
  padding included. On real frames the row order is now proven too: 18 of
  the 23 `hud2d` frames are byte-identical to `gl1`'s, status bar at the
  bottom.
- **Not yet (S2b):** the depth test and depth state, culling, stencil,
  polygon offset and mode, unit 1 and `GL_COMBINE`, points and lines,
  `glTexSubImage2D`, levels above 0, anisotropy, the per-frame vertex
  ring, `glFrustum`, `glRotatef`, `glTranslatef`, `glScalef`. A draw that
  needs one of the states sets the sticky error, logs once and draws
  without it; points and lines are not drawn.

### Measured on `hud2d` (TASK48 S2a)

Three `gl1-metal` records of commit `e0c57896` against the `gl1`
reference `110a25ae2d26-2096632597` at the defaults (0.02, 0.005),
1-minute load 105 to 134: 18 frames exact (score 0, bad 0), frame 3 at
score 0.000647 and bad 0, and frames 5, 8, 11 and 16 over at score
0.003639 to 0.007440 and bad 0.061862 each, identical in all three
records. The three records are identical to each other, and to the
record of the commit before the S2b cuts.

The four frames over are the screen blend of `R_PolyBlend` (an item
pickup flash): a quad 10 units in front of the eye drawn through
`glFrustum` and `glRotatef`. The `gl1` reference, made with Apple's
software renderer, dims only a diagonal wedge of the screen (about 6
percent of it, the bad fraction above), which looks like a clipping
fault of that renderer: a correct GL dims the whole screen, as `soft`
does. The shim does not implement those matrix calls yet, so it draws no
blend, and the wedge is the whole difference. A shim that implements
them correctly would dim the whole screen and differ from this reference
on about 94 percent of those frames instead. So `hud2d` cannot pass at
the defaults against this reference on frames 5, 8, 11 and 16 for any
correct implementation; the choice (a reference from an accelerated GL,
`gl_polyblend 0` in the scenario, or a recorded exception) is the
owner's, not this card's, and no threshold was changed.

Section 9 supersedes this: TASK82 chose `gl_polyblend 0` in the scenario and `hud2d` now passes at the defaults.

## 9. As built by TASK82 (S2b): departures from section 2

- **Matrices.** `glFrustum`, `glRotatef`, `glTranslatef` and `glScalef`
  multiply the top of the current stack (GL 1.4, section 2.11.2), so
  `R_SetupGL` and `R_PolyBlend` get their matrices; the arithmetic is in
  double and stored as float. A rotation about the zero vector, which GL
  leaves undefined, leaves the matrix alone.
- **Depth, stencil, cull, offset, fill.** No longer reported as missing:
  the draw carries a packed depth and stencil key (34 bits,
  `mgl_depth_stencil_key`; GL writes depth only where it tests it, and
  without the stencil test the stencil buffer is neither tested nor
  changed), which `mgl_layer.m` turns into a cached
  `MTLDepthStencilState`, and the encoder gets the cull mode, the
  counter-clockwise front face, the stencil reference (clamped to 0..255),
  `setDepthBias:slopeScale:clamp:` from `glPolygonOffset` (units, factor;
  0 while `GL_POLYGON_OFFSET_FILL` is off) and the line fill mode, on every
  draw. `GL_FRONT_AND_BACK` culling draws nothing. That Metal's
  counter-clockwise winding is GL's (both judge it in normalized device
  coordinates with y up) was checked with a scratch program that culls a
  triangle counter-clockwise in NDC. Metal has one fill mode for both
  faces and no point fill: a different mode per face, or `GL_POINT`, sets
  the sticky error and draws with the front face's mode (filled for
  `GL_POINT`). None of this is drawn by `hud2d` or `cinematic`; `world`
  (S3) is the first scenario that judges it on frames.
- **Uploads.** `glTexSubImage2D` writes a region of a level that exists
  (`GL_INVALID_OPERATION` for a level the storage does not have,
  `GL_INVALID_VALUE` for a region past it, both checked without
  overflow); a sub-image of level 0 regenerates the chain under
  `GL_GENERATE_MIPMAP`. `glTexImage2D` above level 0 fills the chain of
  level 0 when its size is the level's; a level of another size, or one
  before level 0 exists, would make a GL texture incomplete, which the
  shim does not model, so it is reported and not uploaded. Levels keep
  level 0's alpha (an RGB level 0 makes every level opaque). The
  cinematic path needed nothing new: it is a level-0 `glTexImage2D` per
  frame, already complete in S2a.
- **Anisotropy.** `GL_EXT_texture_filter_anisotropic` is advertised;
  `glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT)` answers 16, Metal's
  limit; `glTexParameteri` takes 1 and up (below 1 is
  `GL_INVALID_VALUE`, above 16 is clamped) into bits 8-11 of the sampler
  key and `maxAnisotropy`. gl1 sets it only when `r_anisotropic` is not 0
  (the default is 0), so the pins do not exercise it.
- **The vertex ring.** One shared 8 MB buffer per frame in flight
  (three), bump-allocated at 256 bytes (`mgl_ring_alloc` in
  `mgl_raster.c`, host-tested); the slot advances only when a frame takes
  a slot of the frame semaphore, so the buffer it reuses belongs to a
  frame whose command buffers have completed. A draw the rest of the ring
  cannot hold gets buffers of its own, as in S2a, and is counted in the
  line the shim prints at shutdown. The overflow path was run on frames
  once: a build with a 4096-byte ring drew 190 draws past it over the
  707 frames of `hud2d`, and its 23 frames were identical (score 0) to
  the 8 MB build's. The uniforms still go by `setVertexBytes`.
- **Points and lines.** The 2D path draws neither, so they stay reported
  and undrawn until S6. gl1 does draw `GL_LINES` under the 2D projection
  for the interleaved-stereo stencil mask (`gl1_main.c`, `R_RenderView`,
  near line 1070); the shim reports that call and draws nothing, so the
  mask is empty in those stereo modes. They are not the default and no
  pin scenario uses them. Lines and points land with S6.
- **Still not drawn:** texture unit 1 and `GL_COMBINE` (S3).
- **The reference flaw.** `hud2d` now sets `gl_polyblend 0`
  (`docs/plans/metal-port-harness.md`, "The screen flash and `hud2d`"):
  with the matrices in place the shim darkens the whole screen on the
  pickup frames, where Apple's software renderer darkens a wedge. Against
  the re-recorded references `gl1-metal` is exact on all 23 frames.
- **Gate.** The host test links `-lm` (the rotation needs `sin`, `cos`).
- **Sanitizers.** An `ASAN=1 UBSAN=1` build of the shim recorded `hud2d`
  and `cinematic` with no sanitizer line in the engine log, and both
  records pass against `gl1` with the same numbers as the release build
  (the implementer's run). A reviewer also ran the host test under ASAN
  and UBSAN: 327 checks, 0 failed.

## 10. As built by TASK49 (S3, the world): departures from section 2

- **What was left to draw.** S2b had brought up the frustum, depth,
  cull, polygon offset and `glTexSubImage2D` with
  `GL_UNPACK_ROW_LENGTH`; S3 adds texture unit 1 and `GL_COMBINE`. A draw
  now binds a texture and a sampler on each of the two units
  (`mgl_draw_t.texture1`, `sampler1`; the unbound 1 x 1 texture when a
  unit is off) and carries `texenv1`, `tex_opaque1` and the two
  `GL_RGB_SCALE` values in the uniform block, which is 96 bytes. The
  vertex function passes the unit-1 coordinate on.
- **One texture-environment function for both sides.** The arithmetic of
  a unit is `mgl_texenv_stage` in `mgl_texenv.h`, written in the common
  subset of C99 and the Metal shading language and included by
  `shaders.metal` and by `mgl_internal.h`, so the host test runs the very
  source the fragment function runs: `GL_REPLACE` (an RGB texture keeps
  the previous alpha), `GL_MODULATE`, and `GL_COMBINE` as gl1 sets it
  (GL's default sources and operands, `GL_MODULATE` for RGB and alpha:
  texel times previous, RGB times `GL_RGB_SCALE` clamped to [0, 1],
  alpha unscaled). Unit 0 works on the fragment's colour, unit 1 on unit
  0's result. Still a uniform and a branch, not function constants
  (section 8). `GL_DECAL`, `GL_ADD` and `GL_COMBINE` with another
  function, which gl1 never sets, set the sticky error, log once and draw
  as `GL_MODULATE`. The Makefile rebuilds the metallib when the header
  changes.
- **The classic path needs nothing of its own.** `gl1_multitexture 0`
  draws the wall with `GL_REPLACE` and then the lightmap over it with
  `GL_ZERO, GL_SRC_COLOR` (or `GL_ONE, GL_ONE` with
  `gl1_saturatelighting`), `GL_LEQUAL` and no depth write: blending,
  the depth state and the per-unit environment cover it. On frames the
  two paths of the shim agree: `gl1-metal` with `gl1_multitexture 0`
  against `gl1-metal` with 1 (both with `gl_polyblend 0`), bad fraction
  0 on 21 of 23 frames and at most 0.000029, score at most 0.000417,
  largest channel difference 1 on 21 frames (the first pass is rounded
  to 8 bits before the second multiplies it); on frames 17 and 21 one
  and nine pixels differ by more than 8, up to 20 and 26, not looked
  into.
- **Dynamic lightmaps and polygon offset** need no new code: the
  sub-image upload ends the render encoder and the next draw opens a
  Load pass (section 8). `glPolygonOffset` units go to Metal's depth
  bias unchanged; for the 32-bit float depth buffer a unit there is
  2^(e - 23) for the exponent e of the primitive's largest depth, which
  is 2^-24 where most of a perspective depth range lies (0.5 to 1), the
  same step as one unit of a 24-bit depth buffer. gl1 enables the offset
  only with `gl_zfix 1` (default 0) and the `world` scenario has no
  dynamic lights (`cl_lights 0`), so both are host-tested, not judged on
  frames.
- **Against the `gl1` references** see `docs/plans/metal-port-harness.md`,
  "The `world` scenario against the software renderer (TASK49)": the
  references from Apple's software renderer have faults of their own
  that no correct renderer reproduces, so `world` does not pass there
  at the defaults; no threshold was changed.
- **Not covered.** No static assert ties the C uniform struct to the
  shader's (follow-up), and no scenario pins `GL_COMBINE` because
  overbright defaults to off.

## 11. As built by TASK50 (S4, sky, water and translucent surfaces)

- **No new GL surface, as section 4 expected.** The draws of this stage
  are the sky (`R_DrawSkyBox`, `gl1_warp.c`: `glPushMatrix`,
  `glTranslatef` to the eye, `glRotatef` by time times `skyrotate`
  about `skyaxis`, one `GL_TRIANGLE_FAN` of four per face through
  `glDrawArrays`, `GL_REPLACE`, depth test and writes on), the warped
  water (`R_EmitWaterPolys` into gl1's buffer, then `R_ApplyGLBuffer`
  with `SURF_DRAWTURB`: unit 1 off, `GL_MODULATE` with the colour
  `inverse_intensity`, or with `gl1_overbrightbits` set `GL_COMBINE` at
  `GL_RGB_SCALE` 1 and a white colour) and the translucent surfaces
  (`R_DrawAlphaSurfaces`, then `buf_alpha`: blending
  `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA`, `GL_MODULATE`, the colour
  `inverse_intensity, inverse_intensity, inverse_intensity, alpha` with
  alpha 0.33, 0.66 or 1, drawn after the opaque world and the sky;
  translucent water goes through `R_EmitWaterPolys` there too). Every
  call and state in them was already implemented by S2b and S3: records
  of the parent (`world`, `skywater` from `demo1.dm2` and from
  `water.dm2`) logged no `not implemented` line, and neither do the
  records of this card. So S4 changes no shim code. The draw order is
  gl1's: the shim encodes draws in the order gl1 makes them, into one
  render pass per frame.
- **Depth writes stay on for translucent surfaces.** The card and
  inventory B4 say depth writes off; gl1 never calls `glDepthMask` in
  `R_DrawAlphaSurfaces` or for `buf_alpha` (each `glDepthMask(GL_FALSE)`
  in gl1 is undone with `GL_TRUE` before its function returns; see
  `src/client/refresh/gl1/gl1_surf.c`, `R_DrawAlphaSurfaces`, lines 477
  to 521, which only has `glEnable(GL_BLEND)` at 485 and
  `glDisable(GL_BLEND)` at 520), so the shim, which follows the calls,
  draws them with depth writes, as a GL driver does.
- **What the shim computes on these paths.** gl1 computes the warp
  (`r_turbsin`, the scroll of `SURF_FLOWING`) and the sky's texture
  coordinates (clamped to 1/512 and 511/512, t flipped) on the CPU; the
  shim copies them into the vertex unchanged (`mgl_assemble`). The shim
  itself computes the sky's matrix (the rotation in double, about the
  normalised axis, multiplied on the right of the world matrix, popped
  after), the fan's six indices, the vertex colour from the current
  colour (the alpha of a translucent surface rides there), the texture
  environment (`mgl_texenv_stage`: an RGB texture under `GL_MODULATE`
  keeps the colour's alpha) and the pipeline and depth keys; Metal does
  the blend. `test_sky_water_alpha` in the host test replays gl1's
  call sequence for each of the three and checks those values against
  hand-worked ones, including the blends of alpha 0.33 and 0.66 over a
  known destination.
- **No new table or bound.** Nothing in this card adds a fixed table,
  so there is no new overflow path.
- **Tests and sanitizers.** Each check of `test_sky_water_alpha` fails
  against at least one mutant of a shim line it covers (37 mutants of
  `mgl_state.c`, `mgl_draw.c` and `mgl_texenv.h`, run with only that
  test in `main`, all 37 killed). An `ASAN=1 UBSAN=1` build recorded
  the three scenarios below with no sanitizer line, frames identical to
  the release build's.
- **On frames:** see `docs/plans/metal-port-harness.md`, "Sky, water
  and translucent surfaces against the software renderer (TASK50)".
  Where the `gl1` reference is sound the shim matches it to within
  rounding (the sky of `world` frame 21: largest channel difference 1);
  every region where they differ by more is one of the reference's
  known faults, with `gl1` the odd one out against `soft` and
  `gl1-metal`.

## 12. As built by TASK51 (S5, models): departures from section 2

- **Nothing was left to draw.** `gl1-metal` recorded on main (`d1c2ed53`,
  before any TASK51 change) of `models` and of three scratch copies of it
  (shadows with `gl1_stencilshadow` 0 and 1, and `cl_lights 1` with
  `gl1_flashblend 1`) printed no "not implemented" line, and the
  `ASAN=1 UBSAN=1` build, where gl1 reads `glGetError` after every draw,
  texture call and pointer call (`glCheckError_` in `gl1_main.c`, DEBUG
  builds only), printed no `glError` line. Every call of the model path
  was already there from S1 to S3:
  - `buf_alias` (`R_ApplyGLBuffer`): `glColorPointer(4, GL_UNSIGNED_BYTE)`
    is normalised by `mgl_fetch` (byte / 255) into the vertex colour,
    which `GL_MODULATE` multiplies with the skin; `RF_SHELL_*` turns the
    unit off (the colours alone), `RF_TRANSLUCENT` blends them.
  - The view weapon: the projection pushed, `glScalef(-1, 1, 1)` for
    `gl_lefthand 1`, the weapon frustum and the pop are the CPU stacks
    (`glScalef` and `glFrustum` multiply the top, so P = S F); the cull
    swap to `GL_BACK` reaches `setCullMode:`; `RF_DEPTHHACK`'s
    `glDepthRange(gldepthmin, gldepthmin + 0.3 (gldepthmax - gldepthmin))`
    (gl1 computes 0 .. 0.3 on the CPU) is the Metal viewport's `znear`
    and `zfar` as given, because the vertex function maps GL's clip depth
    z in [-w, w] to (z + w) / 2 in [0, w], which gives the same window
    depth n + (f - n) (z / w + 1) / 2 for any range.
  - `buf_flash` (`R_RenderDlights`): untextured, the colour array,
    `GL_ONE, GL_ONE`, `glDepthMask(GL_FALSE)`: pipeline and depth keys.
  - `buf_shadow` (`R_DrawAliasShadow`): the current colour (0, 0, 0, 0.5)
    blended; with `gl1_stencilshadow 1`, `R_Clear` clears the stencil to
    1 (`glClearStencil(1)`, the pass's `clearStencil`), and
    `glStencilFunc(GL_EQUAL, 1, 2)` with `glStencilOp(GL_KEEP, GL_KEEP,
    GL_INCR)` becomes a cached `MTLDepthStencilState` on the
    `depth32Float_stencil8` attachment (both faces, read mask 2, write
    mask 255, `MTLStencilOperationIncrementClamp`) with reference 1. The
    first shadow fragment on a pixel passes ((1 & 2) == (1 & 2)) and
    makes it 2, a second fails ((1 & 2) != (2 & 2)), so overlapping
    shadow triangles darken once. `SDL_GL_GetAttribute(SDL_GL_STENCIL_SIZE)`
    answers 8, so gl1 keeps `gl_state.stencil` on.
  - The debug bounding box (`gl_showbbox`): `glPolygonMode(GL_LINE)` is
    `setTriangleFillMode:` lines, the strip expanded in GL's order.
- **Shared clip depth.** The mapping moved from `shaders.metal` into
  `mgl_gl_clip_z` in `mgl_texenv.h`, so the host test runs the source the
  vertex function runs; the arithmetic is unchanged.
- **Stencil applied on frames.** Switching `gl1_stencilshadow` from 0 to
  1 (with `r_shadows 1`) changes 574 pixels over 11 of 23 frames in
  `gl1-metal` and 635 over 11 frames in `gl1`, in the same boxes, each
  about twice as bright (luminance 9.8 to 20.0 in `gl1-metal` and 9.8 to
  19.9 in `gl1` on frame 0): the double-darkened overlaps of the shadow
  become single. Frame 7 is left out of these counts (an explosion
  sprite with a random frame differs run to run there). `r_shadows 1`
  itself darkens 8,998 pixels over 17 frames in `gl1-metal`.
- **Flat shading is not modelled** (section 5): `glShadeModel` is
  checked and stored, and every draw interpolates. Its call sites are
  `gl1_buffer.c` 123 (`GL_SMOOTH`, alias) and 256 (`GL_FLAT`),
  `gl1_light.c` 87 (`GL_SMOOTH`, flash blobs), `gl1_mesh.c` 692 and
  `gl1_misc.c` 195 (`GL_FLAT`). It changes no pixel gl1 draws: the alias
  models and the blobs are drawn under `GL_SMOOTH`; under `GL_FLAT` gl1
  draws only constant-colour geometry (shadows, sprites, beams, the
  world) and particles whose three vertices carry one colour. Nothing
  else of the model path is deferred.
- **Host test.** `test_models` in `tests/metal/mgl_host_test.c` follows
  gl1's calls for the left-handed weapon (an asymmetric frustum, so the
  order of mirror and frustum shows; clip coordinates and the 0.225
  window depth worked by hand), the right-handed model, shells and
  translucency, the blobs, the shadow without and with the stencil (a
  GL 1.4 stencil interpreter of the key: darkening 0.8 to 0.4 to 0.2
  without it, 0.8 to 0.4 with it, the depth-fail case and the clamp at
  255), the bounding box, and the projection stack to its last push and
  the first overflow. Each mutant below is one changed line in a scratch
  copy; the host test is built as `tests/check-metal-shim.sh` builds it
  (ASAN, UBSAN), once whole and once with `main` calling `test_models`
  alone, and all 28 are killed both ways. The cells name the
  failing `CHECK`s of `test_models` by what they check, not by line
  number (lines move with every edit of the file).

| Mutant | File | `test_models` alone fails at (check) |
|---|---|---|
| clip depth not shifted | mgl_texenv.h | weapon clip depth; weapon window depth; clip depth end points |
| clip depth not halved | mgl_texenv.h | weapon clip depth; weapon window depth; clip depth end points |
| depth range far ignored | mgl_raster.c | weapon window depth |
| glScalef drops x | mgl_state.c | weapon clip x y w; model clip x y |
| matrix product reversed | mgl_state.c | weapon clip x y w; model clip x y |
| push depth of the other stack | mgl_state.c | push depth and first draw; weapon clip x y w; weapon clip depth; weapon window depth |
| push overflow one late | mgl_state.c | push overflow error; matrix after overflow; pop to base |
| pop of the other stack | mgl_state.c | pop restores projection; push to last level; pop to base; pop underflow error |
| pop underflow not caught | mgl_state.c | pop underflow error |
| glCullFace not stored | mgl_state.c | mirrored weapon cull front |
| byte colour array refused | mgl_state.c | colour array accepted, then a sanitizer report at the draw |
| stencil reference not stored | mgl_state.c | stencil reference |
| depth mask always on | mgl_state.c | depth and stencil key |
| polygon mode front from GL_BACK | mgl_state.c | bounding box state |
| byte colours over 256 | mgl_draw.c | byte colours; colour array vertex colours |
| colour array ignored | mgl_draw.c | byte colours; colour array vertex colours |
| strip odd triangles not swapped | mgl_draw.c | strip indices |
| blend destination in the source field | mgl_draw.c | blend key; blend destination; shadow pipeline key; translucent result |
| depth write without the mask | mgl_draw.c | depth and stencil key |
| stencil test bit lost | mgl_draw.c | stencil key; stencil darkening; stencil test fails; stencil test passes |
| stencil pass op in the depth-fail field | mgl_draw.c | stencil key; stencil darkening; stencil test fails; stencil test passes |
| stencil read mask shifted | mgl_draw.c | stencil key; stencil darkening; stencil test after pass |
| texture unit enable ignored | mgl_draw.c | shadow texture unit off; shadow texenv; bounding box state |
| cull always on | mgl_draw.c | bounding box state |
| cull swap lost | mgl_draw.c | weapon culling back and key |
| line fill lost | mgl_draw.c | bounding box state |
| mvp reversed | mgl_draw.c | weapon clip x y w; weapon clip depth; weapon window depth; model clip x y |
| mvp from the bottom of the projection stack | mgl_draw.c | model clip x y |

- **Against the `gl1` references** see `docs/plans/metal-port-harness.md`,
  "The `models` scenario against the software renderer (TASK51)": the
  faults of Apple's software renderer that TASK49 found (the screen
  flash drawn as a wedge, polygons crossing the screen edge dropped or
  garbled) reach the view weapon and close brush entities too, so
  `models` does not pass there at the defaults; no threshold was changed.

- **Frame 19: decided.** The one region first suspected as a shim defect
  (`models` frame 19, 589,461 to 638,479, a sliver of the view weapon at
  the bottom edge) is decided by review: the reference is wrong and the
  shim is right. In that rectangle `gl1` draws 328 pixels that
  `gl1-metal` does not and none the other way. Three witnesses say no
  weapon geometry is there: `gl1` itself at `r_gunfov 100`, where the
  spot is away from every screen edge, covers 16 of the 328;
  `gl1-metal` at `r_gunfov 100` covers 6; `soft` at `r_gunfov 96.379`,
  which matches the weapon projection of `gl1` (`soft` reads the cvar as
  a horizontal field of view, `gl1` as a vertical one), covers 19.
  Frames 18, 20 and 21 have no differing pixel there. On frame 17 the
  reference drops a piece of the weapon instead (81 pixels the other
  way, 78 of them confirmed by `gl1` at fov 100).
