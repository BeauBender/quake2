# `ref_gl1` GL state inventory (input to TASK21 and TASK28)

Measured 2026-11-06 on `metal-port-harness` (origin/main 5dbd527) over
`src/client/refresh/gl1/*.c` (10,716 lines incl. headers). Method: every
`gl[A-Z]*(` token in the `.c` files, plus the `qgl*` entry points loaded by
pointer in `gl1_main.c`. No `glBegin`/`glEnd` remain: all drawing is client
vertex arrays (`glDrawArrays` x19, `glDrawElements` x1).

## 1. Entry points: 54 direct + 5 loaded by pointer, 441 call sites

Direct `gl*` calls, 435 sites. Per file: `gl1_main.c` 181, `gl1_surf.c` 51,
`gl1_draw.c` 51, `gl1_image.c` 36, `gl1_buffer.c` 34, `gl1_mesh.c` 27,
`gl1_misc.c` 23, `gl1_warp.c` 11, `gl1_lightmap.c` 10, `gl1_light.c` 10,
`gl1_sdl.c` 1. Zero in `gl1_model.c`, `gl1_scrap.c`, `qgl.c`.

| Entry point | Sites | Files (count) |
|---|---|---|
| glDisable | 49 | main 25, surf 6, mesh 5, misc 5, draw 3, buffer 2, light 2, image 1 |
| glEnable | 42 | main 18, surf 6, mesh 5, misc 5, draw 3, buffer 2, light 2, image 1 |
| glTexParameteri | 38 | image 22, draw 6, lightmap 6, misc 4 |
| glEnableClientState | 33 | main 13, draw 8, surf 5, buffer 4, warp 2, mesh 1 |
| glDisableClientState | 32 | main 13, draw 8, surf 5, buffer 3, warp 2, mesh 1 |
| glColor4f | 28 | main 10, draw 6, surf 6, buffer 2, mesh 2, light 1, misc 1 |
| glVertexPointer | 20 | main 9, draw 5, surf 3, buffer 1, mesh 1, warp 1 |
| glDrawArrays | 19 | main 9, draw 5, surf 3, mesh 1, warp 1 |
| glTexImage2D | 15 | image 8, draw 4, lightmap 3 |
| glRotatef | 12 | main 10, mesh 1, warp 1 |
| glDepthMask | 12 | main 8, light 2, surf 2 |
| glTexCoordPointer | 10 | draw 3, buffer 2, main 2, surf 2, warp 1 |
| glMatrixMode | 10 | main 6, buffer 4 |
| glLoadIdentity | 8 | main 7, buffer 1 |
| glTexEnvi | 6 | surf 3, buffer 2, image 1 (all others go through `R_TexEnv`) |
| glPushMatrix / glPopMatrix | 6 / 6 | mesh 2, buffer 1, main 1, surf 1, warp 1 |
| glBlendFunc | 6 | surf 3, light 2, misc 1 |
| glShadeModel | 5 | buffer 2, light 1, mesh 1, misc 1 |
| glGetString | 5 | main 4, sdl 1 |
| glTranslatef | 4 | main 2, mesh 1, warp 1 |
| glStencilOp / glStencilFunc | 4 / 4 | main 3, mesh 1 |
| glCullFace | 4 | buffer 2, main 1, misc 1 |
| glColorMask | 4 | main 4 |
| glClearColor | 4 | main 4 |
| glViewport | 3 | main 3 |
| glTexSubImage2D | 3 | surf 2, lightmap 1 |
| glPolygonMode | 3 | mesh 2, misc 1 |
| glPixelStorei | 3 | surf 2, misc 1 |
| glDepthRange | 3 | buffer 2, main 1 |
| glColorPointer | 3 | main 2, buffer 1 |
| glClear | 3 | main 3 |
| glStencilMask, glPolygonOffset, glOrtho, glGetFloatv, glDrawBuffer, glDeleteTextures, glClearStencil | 2 each | main (glDeleteTextures: image) |
| glScissor, glScalef, glReadPixels, glPointSize, glLoadMatrixf, glHint, glGetError, glFrustum, glFinish, glDrawElements, glDepthFunc, glColor4ub, glBindTexture, glAlphaFunc | 1 each | main / misc / surf / buffer / image |

Loaded by pointer (`qgl*`, `gl1_main.c:1549-1675`, `qgl.c`): `glActiveTexture`
(1 site, `gl1_image.c:131`), `glClientActiveTexture` (1, `:132`),
`glPointParameterf` (2, `gl1_misc.c:215-216`), `glPointParameterfv` (1,
`:217`), `glColorTableEXT` (1, paletted textures; dead on Metal). Multitexture
is optional at runtime (`gl_config.multitexture`, 16 branch sites); the
classic single-texture path is still complete, which is the shim's fallback.

Never used: shaders, VBOs, FBOs, display lists, fog, lighting (`GL_LIGHTING`),
`glTexGen`, `glFog*`, `glColorMaterial`, `glDrawRangeElements`.

## 2. Draw-time state space

20 draw sites. 1 is the batched path (`R_ApplyGLBuffer`, `gl1_buffer.c:222`,
`glDrawElements(GL_TRIANGLES, GL_UNSIGNED_SHORT)`), which carries world
surfaces, alias models, 2D pics and more via `gl_buf.type`; the other 19 are
direct `glDrawArrays` with fan/strip/triangles/points/lines.

Caps ever toggled (`glEnable`/`glDisable` histogram): `GL_BLEND` 12/16,
`GL_TEXTURE_2D` 13/12, `GL_ALPHA_TEST` 5/4, `GL_DEPTH_TEST` 2/5,
`GL_CULL_FACE` 2/5, `GL_STENCIL_TEST` 2/2, `GL_POINT_SMOOTH` 2/2,
`GL_SCISSOR_TEST` 1/1, `GL_POLYGON_OFFSET_FILL` 1/1, `GL_MULTISAMPLE` 1/1,
`GL_SHARED_TEXTURE_PALETTE_EXT` 1/0. Blend factors used: `SRC_ALPHA,
ONE_MINUS_SRC_ALPHA` (default), `ONE, ONE` (saturated lightmaps, dlights),
`ZERO, SRC_COLOR` (classic lightmap multiply). Depth func: one `glDepthFunc`
site (`gl1_main.c:893`), `GL_LEQUAL` by default (`:833`), `GL_GEQUAL` when
the reversed-depth branch at `:868` is taken. Alpha test: one func, `GL_GREATER 0.666` (`gl1_misc.c:182`). Cull:
`GL_FRONT` normally (`gl1_misc.c:178`, `gl1_main.c:798`), swapped to
`GL_BACK` for the left-handed weapon (`gl1_buffer.c:119/265`). Shade model:
`GL_FLAT` default, `GL_SMOOTH` for alias and dlight blobs. Polygon offset
`(0.05,1)` or `(-0.05,-1)` for the world (`gl1_main.c:900-904`). Stencil: stereo
eye masks (`gl1_main.c:1021-1071`) and stencil shadows
(`gl1_mesh.c:258-259`, `GL_EQUAL 1 mask 2`, `KEEP KEEP INCR`).

| # | Site (function, file:line) | Primitive, vertex format | TexEnv | Blend | Depth test / mask | Alpha test | Cull | Other |
|---|---|---|---|---|---|---|---|---|
| B1 | `R_ApplyGLBuffer` `buf_2d` | TRIANGLES idx, v2f t2f | MODULATE or REPLACE per `R_TexEnv` set before | off (set by `R_SetGL2D`) | off / - | on | off | ortho |
| B2 | `R_ApplyGLBuffer` `buf_singletex` (world, classic) | TRIANGLES idx, v3f t2f | REPLACE (MODULATE for turb) | off | LEQUAL / on | off | FRONT | polygon offset |
| B3 | `R_ApplyGLBuffer` `buf_mtex` (world + lightmap) | v3f t2f t2f (unit 1) | unit0 REPLACE, unit1 COMBINE `RGB_SCALE`=overbright | off | LEQUAL / on | off | FRONT | 2 texture units |
| B4 | `R_ApplyGLBuffer` `buf_alpha` (translucent surfaces) | v3f t2f, constant color `inverse_intensity, alpha` | MODULATE | on, SRC_ALPHA/1-SRC_ALPHA | LEQUAL / off | off | FRONT | drawn last |
| B5 | `R_ApplyGLBuffer` `buf_alias` (models) | v3f t2f c4ub | MODULATE, SMOOTH | on only if `RF_TRANSLUCENT` | LEQUAL / on; `RF_DEPTHHACK` range 0..0.3 | off | FRONT or BACK (lefthand) | texture off for `RF_SHELL_*`; weapon projection push/pop |
| B6 | `R_ApplyGLBuffer` `buf_flash` (dlight blobs, `gl1_light.c:85-103`) | v3f c4ub, no texture | - | on, ONE/ONE | LEQUAL / off | off | FRONT | SMOOTH |
| B7 | `R_ApplyGLBuffer` `buf_shadow` (`gl1_mesh.c:250-262`) | v3f, no texture | - | on | LEQUAL / on | off | FRONT | stencil EQUAL 1, INCR |
| D1 | `RDraw_PicScaled` `gl1_draw.c:243`, `RDraw_PicScaledCol :293`, `RDraw_StretchRaw :632` | FAN 4, v2f t2f | MODULATE / REPLACE | off | off | on | off | scrap atlas or cinematic texture |
| D2 | `RDraw_Fill :403`, `RDraw_FadeScreen :429` | FAN 4, v2f, constant color | texture off | Fill off, Fade on | off | on | off | |
| D3 | `R_DrawSpriteModel` `gl1_main.c:240` | FAN 4, v3f t2f | MODULATE | on if alpha<1 | LEQUAL / on | on if alpha==1 | FRONT | |
| D4 | `R_DrawNullModel :289,:305` | FAN 6, v3f | texture off | off | LEQUAL / on | off | FRONT | debug |
| D5 | `R_DrawParticles2 :501` | TRIANGLES 3n, v3f t2f c4ub | MODULATE | on | LEQUAL / off | off | FRONT | particle texture |
| D6 | `R_DrawParticles :565` | POINTS n, v3f c4ub | texture off | on | LEQUAL / off | off | FRONT | `glPointSize`, point params; POINT_SMOOTH set once at init (`:1839-1845`) |
| D7 | `R_PolyBlend :620` | FAN 4, v3f, constant color | texture off | on | off | off | FRONT | full-screen tint |
| D8 | `R_RenderView :1029` stereo mask | FAN 4, v2s | texture off | off | - | - | - | colormask off, stencil REPLACE/INVERT |
| D9 | `R_RenderView :1063` (`gl1_showtris`-style lines) | LINES, v2f | texture off | - | - | - | - | debug |
| D10 | `R_DrawBeam :2030` | STRIP 4*segs, v3f, constant color | texture off | on | LEQUAL / off | off | FRONT | |
| D11 | `R_DrawAliasModel :704` bbox | STRIP 8, v3f | texture off | - | - | - | off | `glPolygonMode LINE`, debug |
| D12 | `R_DrawTriangleOutlines` `gl1_surf.c:123` | LINE_STRIP, v3f | texture off | - | - | - | - | debug |
| D13 | `R_DrawGLPolyChain :151,:196` (classic lightmap pass, `:215-240`) | FAN n, v3f t2f stride `VERTEXSIZE*4` | REPLACE | on, ONE/ONE or ZERO/SRC_COLOR | LEQUAL / off | off | FRONT | lightmap textures |
| D14 | `R_DrawSkyBox` `gl1_warp.c:702` | FAN 4, v3f t2f | REPLACE | off | LEQUAL / on | off | FRONT | 6 faces, rotated |

Distinct pipeline-state keys for the shim (the Metal render-pipeline-state
and depth-stencil-state cache needs no more than these):

1. **Vertex layouts (5):** `v2f t2f`, `v3f t2f`, `v3f t2f c4ub`, `v3f c4ub`, `v3f` (plus one-off `v2s` for the stereo mask and `v3f t2f t2f` for `buf_mtex`).
2. **Fragment variants (5):** untextured constant color; textured REPLACE; textured MODULATE by vertex or constant color; two-texture COMBINE with RGB scale 1/2/4 (overbright); alpha-tested MODULATE (`> 0.666`, a `discard`).
3. **Blend states (4):** off; `SRC_ALPHA, 1-SRC_ALPHA`; `ONE, ONE`; `ZERO, SRC_COLOR`.
4. **Depth-stencil states (6):** test off, write off (2D); LEQUAL + write; LEQUAL no write; LEQUAL + write + stencil shadow (EQUAL 1 / INCR); stencil stereo mask write (NEVER + REPLACE/INVERT, colormask off); stencil stereo test (EQUAL eye, KEEP). `GEQUAL` variant only when the depth flip is on.
5. **Rasterizer bits:** cull FRONT (default), BACK (left-hand weapon), none (2D, debug); polygon offset on for the world pass; viewport/scissor from `refdef`; wireframe only for debug cvars.
6. **Depth range hack:** `RF_DEPTHHACK` 0..0.3 of the range (weapon), one extra viewport depth setting.

Primitive types to emulate: TRIANGLES (indexed), TRIANGLE_FAN (dominant: 12
sites), TRIANGLE_STRIP (2), POINTS (1, needs point sprites), LINES and
LINE_STRIP (debug only). Fans and strips become indexed triangle lists in
the shim's ring buffer.

## 3. Texture paths

- **Uploads:** 15 `glTexImage2D`, all `GL_RGBA`/`GL_UNSIGNED_BYTE` with
  internal format `gl_tex_solid_format`/`gl_tex_alpha_format` (`comp`, chosen
  at init: `GL_RGB`/`GL_RGBA` or the `3`/`4` legacy values), except the
  `GL_COLOR_INDEX8_EXT` paletted variants (`gl1_image.c:673,703,744,809`,
  `gl1_draw.c:617`), which depend on `GL_SHARED_TEXTURE_PALETTE_EXT` and are
  dead on any modern driver: the shim implements only the RGBA path.
  Mip chains are uploaded level by level in `R_Upload8/32`
  (`gl1_image.c:744-751`, `miplevel` loop) or via `GL_GENERATE_MIPMAP`
  (`glTexParameteri` 2 sites). Anisotropy via `GL_TEXTURE_MAX_ANISOTROPY_EXT`
  (2 sites). Wrap: only `GL_REPEAT` set explicitly (1+1 sites), everything
  else is the default repeat; the shim can use one repeat sampler per filter.
- **Samplers:** min/mag pairs seen: `(gl_filter_min, gl_filter_max)` world,
  `(NEAREST, NEAREST)` pics and cinematics (4+4), `(LINEAR, LINEAR)` lightmaps
  (3+3), `(filter, filter)` runtime cvar. That is 3 sampler states plus
  anisotropy on/off.
- **Lightmaps:** `BLOCK_WIDTH x BLOCK_HEIGHT` (128x128, `header/local.h:50-51`) `GL_LIGHTMAP_FORMAT`
  (`GL_RGBA`, `:57`) textures, up to `MAX_LIGHTMAPS` 128, `gl_state.lightmap_textures + n`; created in
  `LM_UploadBlock` (`gl1_lightmap.c:97-117`, full `glTexImage2D`, partial
  `glTexSubImage2D` for the dynamic block); dynamic lights rewrite a
  sub-rectangle per surface at `gl1_surf.c:452` and the regen path at `:723`
  uses `GL_UNPACK_ROW_LENGTH` (`:712/:730`). The shim needs
  `replaceRegion` with a source row stride.
- **Scrap atlas:** `Scrap_AllocBlock` packs small pics into `MAX_SCRAPS` (4)
  atlas textures (`gl1_scrap.c:83`, `header/local.h:45`); `Scrap_Update` (`gl1_draw.c:46`)
  re-uploads a dirty scrap before 2D draws (6 call sites). One RGBA upload
  per dirty scrap per frame at most.
- **Cinematics:** `RDraw_StretchRaw` uploads a 256x256 (or `cols x rows`)
  RGBA frame each call (`gl1_draw.c:504-582`).
- **Screenshots:** `glPixelStorei(GL_PACK_ALIGNMENT,1)` then
  `glReadPixels(0,0,w,h,GL_RGB,GL_UNSIGNED_BYTE)` (`gl1_misc.c:132-133`),
  rows bottom-up, then `ri.Vid_WriteScreenshot`. The shim reads the
  drawable back after `endEncoding` and flips; the pin harness consumes it.
- **Deletion:** 2 `glDeleteTextures` (`gl1_image.c`), on level change.

## 4. Matrices and viewport

Modes used: `GL_PROJECTION` and `GL_MODELVIEW` only. 3D setup in `R_SetupGL`
(`gl1_main.c:790-811`): `glViewport`, projection = `glFrustum` from
`R_MYgluPerspective` (`:731-758`, `zNear = max(gl_znear, 0.1)`, `zFar` 4096 or 8192 with `r_farsee`), modelview =
identity, `glRotatef(-90,1,0,0)`, `glRotatef(90,0,0,1)`, three `glRotatef`
for the view angles, `glTranslatef(-vieworg)`, then
`glGetFloatv(GL_MODELVIEW_MATRIX, r_world_matrix)` which `gl1_surf.c:483`
reloads with `glLoadMatrixf` before brush entities. Entities push/pop and
apply `R_RotateForEntity` (`glTranslatef` + 3 `glRotatef`). Alias models
additionally scale (`glScalef`, `gl1_buffer.c`) and the weapon pushes a
narrower-FOV projection (`gl1_buffer.c:104-115`, `R_SetPerspective`). 2D:
`R_SetGL2D` (`:936-947`) and the init path (`:1823-1828`) use
`glOrtho(0, vid.width, vid.height, 0, -99999, 99999)`. `glScissor` once
(`:718`) for the 3D viewport clear. `glDepthRange` 3 sites (weapon depth
hack and restore). Shim: a 2-deep matrix stack per mode in software, the
product uploaded as one `float4x4` per draw; `glGetFloatv(MODELVIEW)` reads
the software stack.

## 5. Implementation order for the shim, by scenario

Frequency-ordered: get `hud2d` passing, then `world`, then `models`,
`particles`, and sky/water/cinematics inside `world`/`hud2d`.

| Stage | Scenario | Entry points it makes real | Metal counterpart |
|---|---|---|---|
| 1. Context + clear | all | glViewport, glClear, glClearColor, glScissor, glDrawBuffer, glFinish, glGetString, glGetError | `CAMetalLayer` drawable, one render pass per frame, load action clear |
| 2. Textures | hud2d | glTexImage2D (RGBA), glTexSubImage2D, glTexParameteri, glBindTexture, glDeleteTextures, glPixelStorei | `MTLTexture` + `replaceRegion`, 3 sampler states, mip levels uploaded per level |
| 3. 2D batch | hud2d | glEnableClientState/glDisableClientState, glVertexPointer, glTexCoordPointer, glColorPointer, glDrawArrays (FAN), glDrawElements, glColor4f, glMatrixMode, glLoadIdentity, glOrtho, glEnable/glDisable(ALPHA_TEST, BLEND, TEXTURE_2D, DEPTH_TEST, CULL_FACE) | vertex ring buffer (fans to indexed lists), per-draw pipeline key, ortho matrix as a uniform, alpha test as `discard` |
| 4. World, classic path | world | glFrustum, glRotatef, glTranslatef, glPushMatrix, glPopMatrix, glLoadMatrixf, glGetFloatv, glCullFace, glDepthFunc, glDepthMask, glDepthRange, glPolygonOffset, glBlendFunc (ONE/ONE, ZERO/SRC_COLOR), glTexEnvi (REPLACE/MODULATE) | software matrix stack; depth-stencil cache; `setDepthBias`; lightmap multiply pass = second draw with blend |
| 5. World, multitexture | world | qglActiveTexture, qglClientActiveTexture, glTexEnvi COMBINE + RGB_SCALE | one fragment function sampling two textures, scale as a constant |
| 6. Models | models | glShadeModel, glScalef, glColorMask, glStencilFunc/Op/Mask, glClearStencil, glPolygonMode (debug) | per-vertex color in the alias layout; stencil shadow depth-stencil state |
| 7. Particles and effects | particles | glPointSize, qglPointParameterf/fv, glEnable(POINT_SMOOTH) | point sprites via instanced quads sized in the vertex function (Metal has no point attenuation) |
| 8. Sky, water, cinematics | world, hud2d | (no new entry points: REPLACE env, per-frame RGBA upload) | covered by stages 2 to 4 |
| 9. Extras | none | glHint, glEnable(MULTISAMPLE), glColorTableEXT, GL_SHARED_TEXTURE_PALETTE_EXT, stereo stencil masks | no-ops or deferred; paletted textures stay unsupported |

Concept map: `glBegin`-era vertex arrays -> one `MTLBuffer` ring per frame,
triple-buffered; texenv REPLACE/MODULATE/COMBINE -> three fragment
functions; `glEnable(GL_ALPHA_TEST)` -> fragment variant with `discard`;
`glBlendFunc` -> colour attachment blend descriptor in the pipeline key;
`glDepthMask`/`glDepthFunc`/stencil -> `MTLDepthStencilState` cache (6
entries); `glCullFace`/`glPolygonOffset` -> encoder calls; matrices ->
software stack, uploaded per draw; `glReadPixels` -> blit from drawable to
a shared buffer; `GL_POINTS` -> expanded quads.

## 6. Sub-cards for TASK21 (each verifiable by a pin)

Filing: `docs/plans/metal-port-cards.md` is the authoritative card list; the
list below is the detailed scope behind its rows.

1. **TASK21a, context and clear.** Stage 1 entry points. Acceptance: `scripts/pins.sh compare gl1 hud2d` on the shim renders the cleared frame and the console background with score 0 against an all-black-plus-conback reference captured on real GL.
2. **TASK21b, RGBA textures and samplers.** Stage 2. Acceptance: pic uploads round-trip bit-exact through `glReadPixels` of a 1:1 `RDraw_PicScaled`; `hud2d` frames within threshold except text (scrap) regions.
3. **TASK21c, 2D batch and the ring buffer.** Stage 3 (`buf_2d`, `RDraw_*`). Acceptance: every `hud2d` frame within threshold; the stereo mask path untested but compiles.
4. **TASK21d, matrices and the classic world pass.** Stage 4, single-texture world + lightmap multiply pass (`D13`, `B2`). Acceptance: `world` scenario within threshold with `gl_config.multitexture` forced off.
5. **TASK21e, multitexture world.** Stage 5 (`B3`, COMBINE + overbright). Acceptance: `world` within threshold with multitexture on, and the frames match TASK21d's within the lightmap threshold.
6. **TASK21f, translucent surfaces, water and sky.** `B4`, `D14`, turb MODULATE path. Acceptance: `world` frames that contain water/sky (named by index in the pin store) within threshold.
7. **TASK21g, alias models and the weapon.** `B5` incl. `RF_DEPTHHACK`, left-hand cull, shell colours, weapon FOV push/pop. Acceptance: `models` scenario within threshold.
8. **TASK21h, dlights, flash and stencil shadows.** `B6`, `B7`, stencil states. Acceptance: `models` frames with `gl1_stencilshadow 1` within threshold; shadow pixels present where the reference has them.
9. **TASK21i, particles, beams, sprites, polyblend.** `D3`, `D5`, `D6`, `D7`, `D10`. Acceptance: `particles` scenario within its looser threshold; `R_DrawParticles2` path within the normal threshold.
10. **TASK21j, screenshots and the pin hook on the shim.** `glReadPixels` orientation and `PACK_ALIGNMENT`. Acceptance: a shim frame and the same frame via `pins_dir` are byte-identical; every scenario recorded through the shim without a person.

Notes outside this directive: `gl1_draw.c:617` and the paletted texture
path, and `glHint(GL_MULTISAMPLE_FILTER_HINT_NV)`, can be deleted in the
trust-boundary work with no effect on Metal.
