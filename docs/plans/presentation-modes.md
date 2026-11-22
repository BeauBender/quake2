# Plan: presentation modes for `ref_metal` (TASK174)

Plan anchors: [the ray-tracing design](metal-raytracing-design.md) and
[the Metal renderer plan](metal-renderer.md). Related: [the high-resolution
plan](high-resolution.md), decision records
[0011](../decisions/0011-metalfx-framework.md),
[0012](../decisions/0012-metalfx-in-the-client.md) and
[0014](../decisions/0014-no-performance-gating.md) (no feature is gated
on performance; it replaces the apple9-only limit below), and the cards TASK56
(the sun, #98), TASK59 (the denoiser, #102), TASK60 (hardware gating, #103),
TASK106 (apple7 costs, #249), TASK173 (RT variations in the harness, #475)
and TASK175 (visionOS, #477).

Status: proposed. Nothing here is built, and no card below is filed. This
document changes no engine code, no pin and no default.

Every cost below is either a number measured on the M6 (Apple GPU family 9,
hardware ray tracing) with the section or runbook it comes from, or marked
*estimate* with its basis. Nothing has been measured on an apple7 Mac (an
M1) or on an iPhone; TASK106 is the first card that will.

## 1. Why presentations

The owner, 2026-11-17: the default ray-traced look may stay close to stock,
and later the game should offer different presentations, such as the RTX
version of Quake II, which replaces the lightmaps, and a 3dfx Glide-style
retro look.

Every ray-traced effect built so far is a conservative layer on the stock
lightmap (design section 2): the sun is added only below `r_rt_sunfloor`,
the AO multiplies the lightmap term with a floor of `r_rt_ao_min` (0.4),
the dlight shadows mask only the dlights, and the reflections replace only
the water. The stock lightmap is still the main light, so it partly washes
the ray-traced terms out; TASK173 exists because the default frames barely
differ from ray tracing off. A stronger layer would not fix that. A lighting
model with no lightmap on the visible surfaces does, and that is a
different presentation, not a new default.

A **presentation** is a named set of choices about how the 3D view is lit
and finished. It never touches the 2D layer, the HUD, the console,
cinematics, gameplay, the network protocol, savegames, the game module or
the renderer API (`API_VERSION 8`).

## 2. The modes and how each is selected

### 2.1 The modes

| `r_presentation` | Working name | What it is | Needs |
|---|---|---|---|
| `0` (default) | **stock** | Today's `ref_metal`, exactly: the lightmap, and each `r_rt_*` effect cvar switching its own layer as now. With the shipped defaults (every effect off) it is the gl1 look the pins hold | any GPU `ref_metal` runs on |
| `1` | **enhanced** | The stock lighting model with today's ray-traced layers on at their defaults: dlight shadows, the sun (once TASK56 lands), AO and water reflections | ray tracing (`supportsRaytracing`) |
| `2` | **retro** | A 3dfx Glide-style look: 16-bit colour with an ordered dither, that era's texture filtering and mip selection, a low-resolution 3D view, its gamma (section 5) | any GPU `ref_metal` runs on, the VM included |
| `3` | **RT-primary** | The lightmap replaced on world surfaces by traced direct light, emissive faces, sky light and one bounce of indirect light, denoised, upscaled with MetalFX (section 4) | ray tracing (`supportsRaytracing`) and the MetalFX temporal scaler. ~~hardware ray tracing (apple9: M3, A17 Pro and later) and the MetalFX temporal scaler, until TASK106 says otherwise (INFORM P3)~~ (replaced by record 0014: capability only, no family limit) |

The values are in the order the slices land (section 7), not in order of
cost. A later mode takes the next free value; a value is never renumbered,
because `config.cfg` stores it.

### 2.2 The cvar

`r_presentation` is one archived integer cvar, read by `ref_metal` only and
documented in `doc/040_cvarlist.md` by the card that adds it. It applies at
`vid_restart` (the video menu's Apply runs one), because retro changes the
texture upload and RT-primary builds a light table at map load. A mode the
device cannot run reads back `0` with one report line, the same shape as
TASK60's forcing of the effect cvars, at start-up and in `vid_restart`
output.

**The mode is an overlay; it writes no other cvar.** For each setting a
mode decides, the renderer uses the mode's value in place of the user's
cvar while the mode is selected, and the user's own value is left as it
is. Switching back to `0` restores the user's settings exactly, because
nothing was overwritten. Settings a mode does not decide (for example
`r_rt_ao_scale` under enhanced, `vid_gamma` under every mode) keep the
user's value. The `r_rt_stats 1` line names the mode and each value it
overrides.

| Setting | stock | enhanced | retro | RT-primary |
|---|---|---|---|---|
| `r_rt_shadows`, `r_rt_ao`, `r_rt_water`, the sun | user's | on where supported | off | the dlight shadows and the sun become part of the light model; AO off (the bounce replaces it); water reflections on |
| `r_metalfx` | user's | user's | off (a temporal scaler would smear the dither) | the temporal scaler where the output is above the internal size (section 4.4) |
| `gl_texturemode` | user's | user's | the retro filter (section 5.2) | user's |
| the lightmap on world surfaces | on | on | on | off at the visible surface; read at the bounce's hit (section 4.1) |
| 2D, HUD, console, cinematics | stock | stock | stock (INFORM P6) | stock |

### 2.3 The menu

The video menu gains one list item, "presentation", with the four names.
Its rule lives in `src/client/menu/vidrules.c` with the existing ones, so
the item is always shown and greyed with its reason when it does not
apply:

- under `ref_gl1`, `ref_gl3` or `ref_soft`: "ref_metal only";
- a mode on a device without ray tracing (the VM): "needs ray tracing";
- ~~RT-primary on apple6 to apple8 (M1, M2, A13 to A16): "needs hardware ray
  tracing" (INFORM P3);~~ (replaced by record 0014: RT-primary is offered
  on every family with ray tracing, on the compute path below apple9; the
  rule above greys it where there is none);
- RT-primary where MetalFX has no temporal scaler (visionOS, decision
  0011): "needs the MetalFX temporal scaler";
- a mode the owner has not yet accepted from its frames: "awaiting review"
  (INFORM P9). The cvar still works from the console.

The effect items already in the menu (`r_rt_ao_scale` and the others) stay
shown; while a mode overrides one, it is greyed with "set by the
presentation" and shows the value in use.

## 3. Hardware classes

| Class | Examples | stock | enhanced | retro | RT-primary |
|---|---|---|---|---|---|
| No ray tracing | the build VM's paravirtual GPU | yes | greyed | yes | greyed |
| apple6 to apple8, compute ray queries on Metal's software traverser (design section 4) | M1, M2; A13 (iPhone 11) to A16 | yes | yes | yes | yes, once its slices land and the owner accepts it; ~~greyed until TASK106 and a device run (INFORM P3)~~ (replaced by record 0014) |
| apple9 and later, hardware intersection | M3 and later, the M6; A17 Pro and later | yes | yes | yes | yes, once its slices land and the owner accepts it |
| visionOS | Apple Vision Pro (TASK175) | yes | yes | yes | greyed: no MetalFX temporal scaler (decision 0011) |

## 4. RT-primary

### 4.1 What replaces the lightmap

At each visible world pixel the outgoing light is the texture's colour (the
albedo) times the sum of these terms, plus the surface's own emission:

- **Direct light from the map's point lights**: one light chosen per pixel
  per frame from those in range, weighted by its unshadowed contribution,
  with one any-hit shadow ray. The falloff is qrad3's for that light, so a
  map's light levels stay close to what its author set. Lightstyles still
  animate: each light carries its `style`, scaled by
  `refdef.lightstyles[style]` as the lightmap is today.
- **Dlights**: RT3's pass as built (TASK55, TASK131), which already traces
  a shadow ray per dlight and applies gl1's falloff; it becomes a term of
  this model instead of a layer.
- **Emissive faces**: faces with `SURF_LIGHT` are area lights, sampled like
  the point lights, and drawn at their own emission.
- **Sky light**: a ray that reaches a `SURF_SKY` face returns the sky's
  colour; the sun is TASK56's direction (`r_rt_sun_angles` and the per-map
  table `scripts/rt/sun-angles.txt`), since Quake II maps have no sun
  entity.
- **One bounce of indirect light**: one cosine-weighted closest-hit ray a
  pixel. At its hit the bounce reads the **stock lightmap** at that point,
  times the hit's albedo (INFORM P4). The lightmap holds qrad3's
  multi-bounce result, so the bounce costs one ray and no second shadow
  ray, and lightstyles and switched lights stay right in the bounce. The
  lightmap is then light arriving from elsewhere, never the light on the
  surface the player looks at.

AO is off under RT-primary: the bounce ray's occlusion is the same
information.

**Exposure.** Quake II textures are authored to be multiplied by the
lightmap (gl1's intensity and overbright). A per-map exposure scale is
fitted at map load by comparing the traced direct light with the lightmap
at a sample of luxels, so the mode is not uniformly brighter or darker than
stock. The owner may prefer a fixed exposure; that is a value in the
slice, not a design change.

**Materials.** The RTX presentation's physically based materials come from
added texture packs. No assets may be added, so RT-primary is Lambertian
on every surface, with the water reflections of RT6 kept and no normal or
roughness maps (INFORM P5).

**Models.** Alias models and the view weapon stay lit by gl1's
`R_LightPoint` (the lightmap below them) in the first stages; tracing their
light is the last slice (section 7). Particles, beams and sprites stay
stock, as the design's "out of scope" paragraph (section 6) has them.

### 4.2 What the maps give it

Everything comes from the map file and the owner's own game data; nothing
is added and nothing derived from game data is committed.

| Data | Where it is | Used for | Gap |
|---|---|---|---|
| `light` entities: `origin`, `light` (intensity), `_color`, `style`, `target` and `_cone` (spotlights) | the BSP's entity lump (`LUMP_ENTITIES`, lump 0). The game frees an untargeted light at spawn (`SP_light`), so the renderer reads the lump from the map file its own loader already opens (`Mod_LoadBrushModel` in `rm_model.c`), not from the game. `ref_metal` does not read the lump today | the point lights | the lump is untrusted input: its parser is an asset loader (CLAUDE.md, "Security-relevant boundaries") and needs its bounds tests |
| `SURF_LIGHT` and the texinfo `value` | each face's texinfo | emissive faces and their intensity | the emitted colour is the texture's average colour, as qrad3 takes it, computed at load from the owner's `.wal` files |
| `SURF_SKY` | the texinfo | sky light; the sun's hit test | no sun direction in the data (TASK56's table) |
| `SURF_WARP`, `SURF_TRANS33`, `SURF_TRANS66` | the texinfo | water and glass, as RT6 and the stock path treat them | none new |
| the textures and the palette | the owner's `.wal` and `.pcx` files | albedo | no material properties |
| the lightmap | the BSP's lighting lump | the bounce at the hit; the exposure fit | none |
| `refdef.lightstyles`, `refdef.dlights` | the renderer API as it is | animated lights, dlights | none |

### 4.3 The denoiser (TASK59)

RT-primary traces about one sample a term a pixel, which is noisy.
TASK59's temporal accumulation and 5x5 bilateral filter (design section 2)
are written for AO and the sun; RT-primary extends them to the direct,
sky and bounce terms. Each term is denoised as **irradiance**, with the
albedo divided out and multiplied back after, so the textures stay sharp.

The options: TASK59's filter extended (the call); a variance-guided
spatiotemporal filter (better under motion, more code and cost), a later
slice if the owner's frames show noise; or `MTLFXTemporalDenoisedScaler`
(option B), measured at 12.032 ms in frame from a 1080p input on the M6
(design section 8, "MetalFX upscaling to the native size"), most of a
60 Hz frame on its own, so not a default (INFORM P10). TASK59's own cost is
not measured yet.

### 4.4 The MetalFX budget

The owner counts MetalFX upscaling and denoising in the high-resolution
budgets, and RT-primary is built for option A of design section 2: the 3D
frame at an internal size, upscaled by `MTLFXTemporalScaler`. The internal
size is `r_metalfx_scale`'s family default: 1080 lines on apple9, 720 lines
below (`doc/040_cvarlist.md`).

- The temporal scaler from 1920 x 1080 to 5120 x 2880 takes 1.822 ms in
  frame, and 2.413 ms from 2560 x 1440 (design section 8).
- MetalFX is weak-linked into `ref_metal` and off by default (decision
  0011); RT-primary turns the scaler on through the overlay, never by
  writing `r_metalfx`. Where the output is the internal size, no scaler
  runs.
- visionOS has no temporal scaler (decision 0011), so RT-primary is greyed
  there (section 2.3).

Memory. At the 5120 x 2880 output, colour, depth and three drawables take
310 to 354 MB (high-resolution plan, section 3); the G-buffer adds 332 MB
there and the surface attachment 60.0 MB (TASK54, TASK131). The traced
terms live at the internal size: an `RGBA16Float` buffer is 8 bytes a
pixel, 16.6 MB at 1920 x 1080. *Estimate*: three terms with a history each
and one moments buffer, seven such buffers, about 116 MB at 1080 lines and
about 52 MB at 720 lines (basis: 8 bytes a pixel times the pixels; no
buffer layout exists yet). The M6 reports a working set of about 25 GiB
(design section 7); an iPhone's is far smaller and not measured.

### 4.5 The cost

Against the 15.0 ms target the default policy sets for the M6's 5120 x 2880
60 Hz cell (design section 2). Ray times are section 8's closed scene at the
later level (the frame rate not held), all at 1920 x 1080, the internal
size:

| Part | M6, ms | Source |
|---|---|---|
| point or emissive light, 1 any-hit ray a pixel | 0.559 | section 8, ray passes |
| sun, 1 any-hit ray a pixel | 0.559 | section 8 |
| bounce, 1 closest-hit ray a pixel | 0.880 | section 8 |
| dlights, up to 4 any-hit rays (worst case) | 0 to 1.952 | section 8; RT3 as built traces about 0.25 rays a pixel (TASK131) |
| the pass's per-pixel floor (G-buffer reads, noise, writes), once if the terms share a kernel | 0.68 | the AO's floor not held (TASK157) |
| per-frame acceleration-structure items | 0.38 | section 8, verdicts |
| G-buffer raster at the internal size | *estimate* 0.95 | 6.80 ms at 5120 x 2880 (TASK54) scaled by the pixels, as section 8 found the passes linear in pixels |
| denoiser | *estimate* 1 to 2 | none: TASK59 is not built; a 5x5 filter on three terms |
| temporal scaler to 5120 x 2880 | 1.822 | section 8 |
| **total** | *estimate* **6.8 to 9.8** | |

So RT-primary should fit the M6's 60 Hz cell with room, before the hit
shading (the lightmap read at the bounce's hit, not measured). Two
measured effects widen it: the first frames of a process run at about 3.5
times these levels (section 8), and with the frame rate held to the display
the GPU runs slower, about 3.3 times for the AO (TASK157). The slices
measure under the bench protocol.

**An M1 (apple7).** Nothing is measured; TASK106 measures. The design's
own assumption is 8 to 14 ms for about 5 rays a pixel at 1080p on apple7
(design section 2, marked assumed), 1.6 to 2.8 ms a ray a pixel.
*Estimate*: RT-primary's 3 to 4 rays a pixel at 720 lines (0.44 of the
1080p pixels) trace in 2.1 to 5.0 ms, before a raster, denoiser and scaler
on a GPU smaller than the M6's by a factor no one has measured here. It
may fit 60 Hz at 720 lines; ~~it is greyed until a measurement says so.~~
(replaced by record 0014: it is offered as on apple9, and the estimate is
information for the player's choice, not a gate).

**An older iPhone.** An iPhone 11 (A13, apple6) has ray queries on the
software traverser and a 1792 x 828 display whose render targets take 31 to
36 MB (high-resolution plan, section 3); no ray-tracing cost has been
measured on any phone. *Estimate*: ~~RT-primary is not a phone mode below
A17 Pro; stock and retro are, and enhanced at reduced effect scales where
a device run shows it holds.~~ (replaced by record 0014: every mode the
phone has the capabilities for is offered on it, and a device run is
information, not a gate).

### 4.6 What stays stock

The HUD, the 2D layer, the console, the menus and cinematics (all drawn at
the window size, as RT11 draws them); the player-setup model view (a
refdef with `RDF_NOWORLDMODEL`, which takes no effect); particles, beams
and sprites; `vid_gamma` and `cl_blend`; the game module, gameplay, the
network protocol, savegames and the renderer API.

## 5. Retro (3dfx Glide-style)

The look of Quake II's OpenGL path on a 3dfx Voodoo-class board through
3dfx's MiniGL: the gl1 lighting (bilinear lightmaps, coloured light, which
`ref_metal` already draws) finished as that hardware finished it. The traits
below come from period descriptions of the hardware, not from a
measurement; the owner judges them from frames.

### 5.1 A post-process pass

One full-screen fragment pass over the 3D view, after the world and models
and before the 2D layer:

- **16-bit colour**: each pixel quantized to RGB565 (5, 6 and 5 bits) with
  a 4 x 4 ordered (Bayer) dither, as the board dithered its 16-bit frame
  buffer;
- **the output filter** some boards applied to hide the dither, a small box
  filter, as an option the owner turns on or off from frames;
- **gamma and colour response**: one curve, picked by the owner from
  frames, on top of `vid_gamma`;
- **no fog**: Quake II drew none on that hardware (INFORM P7).

### 5.2 A texture-upload and sampling path

- **16-bit textures**: at upload (`R_Upload32` and `R_Upload8` in
  `rm_image.c`), opaque texels quantized to RGB565 and texels with alpha
  to ARGB4444, with a dither, and stored in the existing `BGRA8` textures:
  the same look, one code path, no new pixel format on any device;
- **a 256 x 256 texture cap**, the board's limit, which matters only for
  upscaled or high-colour replacements (high-resolution plan, P3 and P6);
- **filtering**: bilinear magnification with the nearest mip level, the
  era's `GL_LINEAR_MIPMAP_NEAREST`, through the existing `gl_texturemode`
  sampler classes (`R_TextureMode`), no anisotropy;
- **a low-resolution 3D view**: the 3D frame at 480 lines and the window's
  aspect, enlarged to the view's rectangle with a nearest or bilinear blit
  (no MetalFX), through the internal-size target the scaler path already
  makes. The 2D layer stays at the window size.

Not reproduced: the 16-bit depth buffer's z-fighting, which changes what is
visible rather than how it looks (INFORM P7).

### 5.3 What is cheap enough for every Apple GPU

All of it. The post-process is a few arithmetic operations and one or two
texture reads a pixel. *Estimate*: under 0.5 ms at 5120 x 2880 on the M6
(basis: the stock 3D drawing, which does far more a pixel, takes 1.09 ms at
that size, TASK54); proportionally less at an iPhone 11's 1.48 megapixels.
The 480-line 3D view rasterizes about 0.31 megapixels at 4:3 against 14.7 at
5120 x 2880, so retro costs less than stock at every output. The upload
conversion runs once at load over a median map's 3.4 MiB of textures
(high-resolution plan, section 3). Retro needs no ray tracing and no
MetalFX, so it runs on the VM, every Mac, iPhone, iPad, Apple TV and Vision
Pro.

## 6. How a mode stays reversible

1. **Stock is untouched.** `r_presentation 0` is the default and runs no
   code of another mode, shown by a counter on the `r_rt_stats 1` line. No
   existing scenario in `scripts/pins/scenarios/` sets `r_presentation`,
   so each reads `0` and keeps its pins byte for byte, under `ref_metal`
   and against gl1 (design section 5, item 1).
2. **Nothing is overwritten.** The overlay of section 2.2 writes no cvar;
   `config.cfg` gains only `r_presentation`. Switching back to `0` is the
   whole undo.
3. **Unsupported reads back `0`** with a report line (section 2.2), so a
   device never runs part of a mode.
4. **Each mode has its own scenarios**, for example `retro-world.cfg`,
   `retro-hud2d.cfg`, `rtprimary-world.cfg` and `rtprimary-dlights.cfg`,
   pinned against `ref_metal` itself at the commit where the owner accepts
   the look and compared at 0.02 after it (design section 5, item 2).
   RT-primary's run with temporal accumulation off and `PINS_SEED` fixed,
   at `r_metalfx 0` and the internal size equal to the output, so no
   scaler history enters a pin.
5. **RT-primary's own invariant**, the analogue of the design's
   double-count check: no world pixel samples the lightmap at its own
   surface. A debug view shows each term alone, and a mutant that adds the
   lightmap back must fail a test.
6. **No boundary moves**: the renderer API, the protocol, savegames and
   the game module are unchanged, and no asset is added.

## 7. The order of work, as proposed cards

Not filed. The ids are placeholders. Tiers: VM (the build VM, no ray
tracing), M6 (apple9, hardware ray tracing), device (an apple7 Mac, an
iPhone, an iPad, an Apple TV, a Vision Pro). Sizes: S under about 300
changed lines, M about 300 to 700, L about 700 to 1,000.

| Id | Title | depends_on | Tier | Size |
|---|---|---|---|---|
| PM1 | `r_presentation`, the overlay of effective values, the report line, the menu item and its `vidrules.c` rule; modes 0 and 1 (stock and enhanced); `doc/040_cvarlist.md` | TASK60 | VM (mode 0, the greying) and M6 (mode 1) | M |
| PM2 | Retro post-process: RGB565 quantize and the 4 x 4 dither, the output filter option, the gamma curve; retro scenarios | PM1 | VM | M |
| PM3 | Retro upload and sampling: 16-bit texels with dither, the 256 cap, linear-mip-nearest, the 480-line 3D view and its blit | PM2 | VM | M |
| PM4 | Retro frames for the owner through TASK173's contact sheet; the owner's choices applied, the pins recorded, the menu item ungreyed | PM3, TASK173 | M6 | S |
| PM5 | RT-primary data: the entity-lump parser (bounds-tested as an asset loader, host tests on generated maps), the `SURF_LIGHT` emitters with their average colours, the sky colour; counts on `r_rt_stats 1`; no visual change | PM1, TASK53 | VM (parser, tests) and M6 (counts) | M |
| PM6 | RT-primary direct light: the lightmap off at the visible surface, point and emissive lights with one shadow ray, lightstyles, RT3's dlights as a term, the exposure fit, the per-term debug views | PM5, TASK55, TASK59 | M6 | L |
| PM7 | RT-primary sky and bounce: sky light, the sun, the bounce reading the lightmap at its hit; AO off under the mode | PM6, TASK56 | M6 | M |
| PM8 | RT-primary denoise and upscale: TASK59's filter on demodulated irradiance for every term, the temporal scaler through the overlay; the cost table of section 4.5 measured | PM7, TASK59 | M6 | M |
| PM9 | RT-primary on models: alias models and the view weapon lit by traced light | PM8 | M6 | M |
| PM10 | RT-primary on modest hardware: the cost on an apple7 Mac and an iPhone, ~~and the gating rule of section 2.3 set from it~~ (replaced by record 0014: optional and informational, taken where such a device is available; it sets no gating rule) | PM8, TASK106 | device | S |
| PM11 | RT-primary frames for the owner; the look accepted, the pins recorded, the menu item ungreyed ~~on the classes PM10 allows~~ on every class with ray tracing and the temporal scaler (replaced by record 0014) | PM8, ~~PM10,~~ TASK173 | M6 | S |

Optional, only if the owner asks: a variance-guided denoiser (after PM8),
and a committed table of material values by texture name, the way
`scripts/rt/sun-angles.txt` holds sun angles by map name (INFORM P5).

## 8. What the owner decides

- **The names** of the modes, in the menu and in `doc/040_cvarlist.md`.
- **The default**: stock (`0`) in this plan.
- **The look of each mode**, from frames sent before the mode is offered
  in the menu (TASK173's contact sheets): for retro, which traits of
  section 5 stay, the dither, the filter and the gamma curve; for
  RT-primary, the exposure, the balance of the terms and the noise left.
- **Each INFORM-style call below**, any of which can be reversed by the
  slice that owns it.

## 9. Calls the plan makes (INFORM-style)

Each is reversible; none changes the stock look, a pin, the protocol or a
savegame.

**P1. One integer cvar, not a cvar a mode.**
Context: the card asks for one selector. Options: one integer cvar; a
string cvar holding the name; one cvar a mode. The call: one archived
integer, `r_presentation`, values never renumbered, names in the menu.
Reverse: a string alias can be added later without breaking stored values.

**P2. The mode is an overlay; it writes no other cvar.**
Context: a preset that writes `r_rt_*` would clobber the user's settings in
`config.cfg`, and switching back could not restore them. Options: write the
cvars when the mode changes (and show "custom" after any edit); overlay
effective values. The call: the overlay. Reverse: a menu action that copies
a mode's values into the user's cvars, added beside it.

**P3. RT-primary needs hardware ray tracing until measured.**
Superseded by record 0014 (2026-11-22), which takes the first option:
RT-primary is allowed wherever ray queries exist (`supportsRaytracing`)
and the temporal scaler is present, with no family limit and no
measurement it waits for. The call as it stood:
Context: apple6 to apple8 trace on Metal's software traverser and nothing
is measured there (section 4.5). Options: allow it everywhere ray queries
exist; gate to apple9; gate per measured device. ~~The call: apple9 and
later, greyed with its reason elsewhere, until PM10.~~ Reverse: one rule in
`vidrules.c` and the renderer's capability check.

**P4. The bounce reads the lightmap at its hit.**
Context: a fully traced bounce needs a shadow ray (and a light choice) at
every hit, about doubling the rays. Options: lightmap at the hit; traced
direct light at the hit; both, by a cvar. The call: the lightmap at the
hit, one ray, lightstyles correct. Reverse: a later slice adds the traced
hit behind a cvar.

**P5. Lambertian materials, no material table.**
Context: the RTX presentation's materials come from added assets, which
this repository may not add. Options: Lambertian everywhere; a committed
table of values by texture name (data, not assets). The call: Lambertian,
the water reflections kept. Reverse: the optional table slice of section 7.

**P6. Retro finishes the 3D view only.**
Context: the HUD and console are drawn crisp at large scales (high-resolution
plan, P4). Options: the 3D view only; the 3D view and the 2D. The call: the
3D view only. Reverse: the pass moves after the 2D layer.

**P7. Retro draws no fog and no depth artefacts.**
Context: Quake II drew no fog on that hardware, and the 16-bit depth
buffer's z-fighting changes visibility. Options: reproduce either.
The call: neither. Reverse: a fog term in the post-process, or a depth
quantize, each behind its own cvar.

**P8. Retro forces ray tracing and MetalFX off; RT-primary forces AO off.**
Context: no ray tracing existed on that hardware, a temporal scaler smears
the dither, and the bounce ray already carries the occlusion. The call: as
stated, through the overlay. Reverse: drop the entry from the overlay's
table.

**P9. A mode not yet accepted is greyed "awaiting review".**
Context: the owner sees frames before a mode is offered in the menu, and a
setting is never hidden. Options: hide it until accepted; grey it with a
reason. The call: grey it, the cvar still working from the console.
Reverse: the rule's reason is removed when the owner accepts.

**P10. RT-primary denoises with TASK59's filter, not MetalFX's denoised
scaler.**
Context: `MTLFXTemporalDenoisedScaler` took 12.032 ms in frame from 1080p on
the M6 (section 4.3). Options: TASK59's filter extended; a variance-guided
filter; the denoised scaler. The call: TASK59's filter first, a
variance-guided filter if frames show noise; the denoised scaler stays
selectable for comparison as `r_metalfx` already makes it. Reverse: PM8's
filter choice is one setting.

## 10. Not measured

- Anything on an apple7 Mac, an iPhone, an iPad, an Apple TV or a Vision
  Pro (TASK106, PM10).
- TASK59's denoiser, the sun pass of TASK56 as built, and the hit shading
  of RT-primary's bounce.
- RT-primary's buffers and the retro pass: their costs above are estimates.
- Whether the retro traits match a real board: they are period
  descriptions, judged by the owner from frames.
