# Plan: high resolutions and modest hardware

Plan anchor for the cards that make the game scale from an older iPhone to
a 5K display, a 4K TV and Vision Pro. Cards cite it as
[`docs/plans/high-resolution.md`](high-resolution.md), with the section and the proposal number
(P1 to P18).

Status (2026-11-14): proposed by the loop, decided by the owner. INFORM2
(#2) is redirected to ImageIO ([decision 0010](../decisions/0010-imageio-decoder.md)).
The first wave, chosen for return on effort, is filed: P1, P2 (as TASK11,
#18, rewritten), P4, P5, P6 (two cards), P7 and P13. The rest stays here as
later work and is not filed.

Rules every proposal follows:

- The original look stays: it is the default, or the feature is behind a
  switch that is off by default. With the feature off, every frame pin is
  unchanged.
- Nothing changes the renderer API (`API_VERSION 8` in
  [`ref.h`](../../src/client/vid/header/ref.h)), the network protocol or
  the savegame format.
- Game data is never committed, uploaded or attached anywhere. Assets
  derived from it are made on the player's machine (section 6).
- Every card is outside the port queue and waits for N10 (TASK93) under
  [decision 0006](../decisions/0006-port-first.md), unless stated.
- No setting is hidden (owner rule, 2026-11-14). A menu item that does not
  apply, for example under another renderer, is shown disabled
  (`QMF_GRAYED` in [`qmenu.h`](../../src/client/menu/header/qmenu.h)) with a
  one-line reason. A card that adds a switch to a menu follows this.

## 1. How the baseline was measured

The numbers in section 2 were read from the 1997 release and the 2023
re-release data by small throwaway programs that parse pak directories and
file headers (WAL, PCX, TGA, PNG, JPEG, MD2, MD5, SP2, BSP, CIN, Theora) in
memory. Nothing was extracted and nothing from the data is in this
repository; only counts and sizes are reported. Section 3 is arithmetic on
those numbers.

## 2. The measured baseline

### 2.1 The game data

| Asset | 1997 release (`baseq2`, `pak2` over `pak1` over `pak0`) | 2023 re-release |
|---|---|---|
| World textures (WAL) | 2,188. Sizes: 64x64 748, 128x128 440, 32x32 308, 64x128 227, 16x16 127. Sides 16 to 320, median 4,096 texels, 28 with a side of 256 or more. 14.6 M texels, 74.5 MiB as RGBA8 with mips | 4,466 WAL in the same size classes (105 at 256x256). 1,610 PNG or TGA textures without a WAL beside them (new content), mostly 32x32 and 64x64. Only 3 textures have both, at 1x and 0.5x: **no 1997 texture is upscaled** |
| Texel density | 399,379 of 407,481 non-sky faces (98.0 %) at one texel per world unit | |
| Textures per map | 24 to 192 (median 105); 0.15 to 1.27 M texels (median 0.68 M); 0.7 to 6.4 MiB (median 3.4) as RGBA8 with mips | |
| Skies | 10 skies of 6 faces, each face both a 256x256 TGA and a 256x256 PCX | 114 faces at 256x256, 30 at 512x512 (new skies only) |
| Maps | 47 BSPs (`IBSP` 38). 3,282 to 13,410 faces (median 9,391), 8,974 to 35,880 world triangles (median 25,459), 3 to 184 inline models, horizontal extent 1,632 to 7,432 units (median 3,776) | 222 BSPs (210 `IBSP`, 12 `QBSP`). The 47 1997 maps are recompiled: 0 of 47 have an identical face, vertex, plane or entity lump (`base1`: 7,905 faces, then 16,787) |
| Lightmaps | One luxel per 16 units, at most 17 luxels on a face side. 76 k to 357 k luxels per map (median 240 k), at least 4.7 to 21.8 slices of 128x128 (median 14.6; `ref_metal` allows 126). 95.2 % of lit faces have one light style | BSPX `DECOUPLED_LM` in 177 maps, 8 units per luxel on most faces; median 794 k luxels per map. `LIGHTGRID_OCTREE` in 169, `FACENORMALS` in 179 |
| Models (MD2) | 119, plus 3 player models of 581 to 592 triangles. Triangles 7 to 1,686 (median 188); monsters median 610; view weapons 240 to 406. Vertices are 8 bits per axis per frame: a step of 0.06 to 0.28 units on view weapons (most near 0.1), 0.06 to 0.79 on monsters | 441 MD2 and 199 MD5 meshes, 138 beside an MD2 (83 on 1997 paths). Triangles 0.86 to 17.7 times the MD2's (median 2.3), up to 3,089 vertices and 47 joints, float vertices. All 138 MD5 animations have the MD2's frame count; 76 of the 83 on 1997 paths have the 1997 frame count, and for the other 7 every 1997 frame name exists in the re-release MD2. 332 glow maps |
| Skins | 154 PCX, most about 250 to 310 by 160 to 195; largest 512x256 | MD5 skins 256x256 to 512x512, up to 512x1024 |
| Sprites | 8 SP2, frames 16x16 to 144x144 | |
| Console font | `conchars` 128x128, 8x8 glyphs | `conchars.png` 128x128 |
| HUD | 22 digits 16x24, 44 icons 24x24, crosshairs 8x8 | crosshairs at 64x64; 160 new UI images |
| Menus and screens | 39 menu pictures (banners 275x32, `m_main` 224x136, cursor 22x29); `conback`, `quit`, `victory` 320x240; `help`, `inventory` 256x192 | cursors unchanged |
| Cinematics | 11 `.cin`, 320x240 at 14 fps, 6 to 210 s | 23 Theora `.ogv`, 1920x1080 at 30 fps |
| Archives | `.pak` | `.pak` (1.6 GiB) and `Q2Game.kpf`, a zip of shaders, fonts and UI images |

### 2.2 The engine

| Fact | Where |
|---|---|
| The high-pixel-density window flag is set for the `wayland` driver only. Without it SDL's Cocoa Metal view keeps `contentsScale` 1 and a point-sized drawable, and its UIKit view uses scale 1.0 (visionOS 2.0 only with the flag). A Retina drawable cannot be produced on macOS | `Glimp_DetermineHighDPISupport` in [`glimp_sdl3.c`](../../src/client/vid/glimp_sdl3.c); [`SDL_cocoametalview.m`](../../third_party/SDL3/src/video/cocoa/SDL_cocoametalview.m), [`SDL_uikitmetalview.m`](../../third_party/SDL3/src/video/uikit/SDL_uikitmetalview.m); [runbook](metal-port-harness.md), TASK98 section; #234 |
| Default 2D scale: the integer `min(width / 640, height / 240)`, at least 1 | `SCR_GetDefaultScale` in [`cl_screen.c`](../../src/client/cl_screen.c) |
| The console background is stretched to the window | [`cl_console.c`](../../src/client/cl_console.c) |
| Only `conchars` and the crosshairs are unfiltered by default; other 2D pictures are bilinear | `r_nolerp_list` in [`rm_main.c`](../../src/client/refresh/metal/rm_main.c) |
| `ref_metal` dropped `r_scale8bittextures` and refuses MSAA | [`ref-metal-native.md`](ref-metal-native.md) section 7; `scale2x`, `scale3x` in [`stb.c`](../../src/client/refresh/files/stb.c); [`gl1_image.c`](../../src/client/refresh/gl1/gl1_image.c) |
| Anisotropy and trilinear filtering exist (defaults: `r_anisotropic 0`, `GL_LINEAR_MIPMAP_NEAREST`) | [`rmb_resource.m`](../../src/client/refresh/metal/rmb_resource.m), [`videomenu.c`](../../src/client/menu/videomenu.c) |
| Lightmaps are 128x128 slices of an array texture, sampled bilinearly | [`ref-metal-native.md`](ref-metal-native.md) section 5 |
| Particle size is scaled by `height / 480`, then clamped at 40 pixels | `R_DrawParticles` in [`gl1_main.c`](../../src/client/refresh/gl1/gl1_main.c), [`gl1_misc.c`](../../src/client/refresh/gl1/gl1_misc.c), the shim's [`mgl_draw.c`](../../src/client/refresh/gl1/metal/mgl_draw.c) |
| The 3D pass renders into an offscreen colour and depth-stencil target, blitted to one of three drawables | [`rmb_encode.m`](../../src/client/refresh/metal/rmb_encode.m), [`rmb_device.m`](../../src/client/refresh/metal/rmb_device.m) |
| The layer's colour space is never set (nil: no colour matching) | `rmb_attach` in [`rmb_device.m`](../../src/client/refresh/metal/rmb_device.m); [Apple](https://developer.apple.com/documentation/quartzcore/cametallayer/colorspace) |
| High-colour replacements (`.tga`, `.png`, `.jpg` beside a `.wal` or `.pcx`) load when at least as large, keeping the original's logical size; the stock skies load as TGA through the same decoder | `LoadHiColorImage`, `GetSkyImage` in [`stb.c`](../../src/client/refresh/files/stb.c) |
| The model loaders read MD2 and SP2 only | [`models.c`](../../src/client/refresh/files/models.c) |
| `ref_metal` ran at 237 to 318 fps at 640x480 on the M6, about 3 to 4 ms a frame whatever the size, not yet diagnosed | FIX51 (#238) |

## 3. What the numbers mean at each target

`horplus` at `fov 90` (vertical 73.7 degrees), the default 2D scale, a 1x
texel 128 units away, the view weapon at `r_gunfov 80` about 15 units from
the eye with a vertex step of 0.1 units. Millimetres and pixels per degree
depend on the viewing distance given; they are estimates.

| Target | Pixels | 2D scale | Console glyph | `conback` stretch | Cinematic | Sky texel | Texel at 128 u | Luxel at 128 u | Gun vertex step | Pixels per degree |
|---|---|---|---|---|---|---|---|---|---|---|
| 1080p desktop, 60 cm | 1920x1080 | 3 | 24 px (6.6 mm) | 6 x 4.5 | 4.5x | 5.6x | 5.6x | 90 px | 4.3 px | 40 |
| M1 MacBook Air, 50 cm | 2560x1600 | 4 | 32 px (3.6 mm) | 8 x 6.7 | 6.7x | 8.3x | 8.3x | 133 px | 6.4 px | 80 |
| 5K at point size: what the engine draws today | 2560x1440 | 4 | 32 px (7.5 mm) | 8 x 6 | 6.0x | 7.5x | 7.5x | 120 px | 5.7 px | 48 |
| 5K native, 60 cm | 5120x2880 | 8 | 64 px (7.5 mm) | 16 x 12 | 12.0x | 15.0x | 15.0x | 240 px | 11.4 px | 97 |
| iPhone 11, 30 cm | 1792x828 | 2 | 16 px (1.2 mm) | 5.6 x 3.5 | 3.5x | 4.3x | 4.3x | 69 px | 3.3 px | 69 |
| iPhone 15 Pro, 30 cm | 2556x1179 | 3 | 24 px (1.3 mm) | 8 x 4.9 | 4.9x | 6.1x | 6.1x | 98 px | 4.7 px | 97 |
| iPad Pro 12.9-inch, 40 cm | 2732x2048 | 4 | 32 px (3.1 mm) | 8.5 x 8.5 | 8.5x | 10.7x | 10.7x | 171 px | 8.1 px | 75 |
| 65-inch 4K TV, 2.5 m | 3840x2160 | 6 | 48 px (18 mm) | 12 x 9 | 9.0x | 11.2x | 11.2x | 180 px | 8.6 px | 120 |
| Vision Pro, default window | 2560x1440 | 4 | 32 px | 8 x 6 | 6.0x | 7.5x | 7.5x | 120 px | 5.7 px | 34 to 44 |

| Target | 1x texel is one pixel beyond | 2x texture | 4x texture | Particles capped nearer than | Colour, depth and 3 drawables |
|---|---|---|---|---|---|
| 1080p | 720 units | 360 | 180 | 22 units | 43 to 50 MB |
| M1 MacBook Air | 1,067 | 533 | 267 | 33 | 85 to 98 MB |
| 5K native | 1,920 | 960 | 480 | 60 | 310 to 354 MB |
| iPhone 11 | 552 | 276 | 138 | 17 | 31 to 36 MB |
| iPhone 15 Pro | 786 | 393 | 196 | 25 | 63 to 72 MB |
| 4K TV | 1,440 | 720 | 360 | 45 | 174 to 199 MB |

What follows from the tables:

- Most of every view is magnified. A 4x texture is pixel-matched at room
  distance (140 to 480 units), so 4x is the useful target and 2x already
  halves the blur.
- The 2D layer is drawn in 8x8 blocks on a 5K and 6x6 on a TV; the HUD
  pictures are also blurred by bilinear filtering, and the console
  background is stretched by a third on every 16:9 screen.
- Physical text size is uneven: a 7.5 mm glyph on a 5K, 1.3 mm on an
  iPhone 15 Pro.
- A luxel spans 240 pixels at 128 units on a 5K, so the bilinear pattern of
  the luxel grid shows.
- The 8-bit model vertices move the view weapon in steps of about 11 pixels
  at 5K, against about 2 at 640x480.
- Near particles are capped at 40 pixels and so shrink, relative to the
  screen, by up to six times at 5K.
- At native size the render targets (up to 354 MB at 5K) outweigh the
  textures (3.4 MiB for a median map at 1x, 55 MiB at 4x).
- Above 60 pixels per degree (5K at 60 cm, MacBook Air, iPhones, TV) the
  3D view can render below native and be upscaled with little visible loss,
  while 2D gains from native pixels. Below 60 (1080p, 1440p, Vision Pro)
  anti-aliasing pays most.

## 4. Proposals, in rank order

Size is changed lines, copied lines included. "Filed" marks the first wave.

### P1. A Retina-native drawable on every Apple platform (filed)

The game draws at the display's pixel size. Where: set
`SDL_WINDOW_HIGH_PIXEL_DENSITY` for the `cocoa` and `uikit` drivers when
`vid_highdpiaware 1` in [`glimp_sdl3.c`](../../src/client/vid/glimp_sdl3.c);
`ref_metal` already reads the pixel size (`RI_GetDrawableSize` in
[`rm_sdl.c`](../../src/client/refresh/metal/rm_sdl.c)); scale the menu
cursor's point-based motion; the harness sets `vid_highdpiaware 0` so every
existing pin is unchanged. One card, about 300 lines. Depends on N10
(TASK93) and FIX45 (#230); comes before TASK111 (#265) so the MetalFX cards
measure at native size. Priority 1: every other item is invisible without
it.

### P2. Decode high-colour images with ImageIO (filed as TASK11)

PNG, JPEG and TGA are decoded by ImageIO on Apple platforms and the
vendored `stb_image` is removed ([decision 0010](../decisions/0010-imageio-decoder.md)),
so texture packs, P6's textures and the stock TGA skies keep loading.
Where: [`stb.c`](../../src/client/refresh/files/stb.c) and
[`cl_image.c`](../../src/client/cl_image.c); a fixed list of accepted
types, the existing size bounds, host tests with generated files. One card,
about 500 lines, tier H. Depends on N10 (TASK93) and this plan's card
(TASK115). Priority 2: it decides whether any texture work is possible.

### P3. The re-release MD5 models, from the player's own copy

With `r_md5 1` (default 0): more triangles (median 2.3 times), 256 to 512
skins, glow maps, skeletal animation and no vertex wobble; the server's
frame numbers and bounding boxes are unchanged. Where: a bounded
`md5mesh`/`md5anim` parser in [`src/client/refresh/files/`](../../src/client/refresh/files/), CPU skinning in
`ref_metal` into the frame ring that N7's MD2 lerp writes (and the
ray-tracing refit reads), a frame-name remap for the 7 models whose 1997
frames differ, and an import command that copies an allowlist from the
player's re-release pak into the user's write directory so its maps,
textures and MD2s never override the 1997 ones. Three cards (about 800, 900
and 500 lines). After N10, ideally before RT1 (TASK53). Priority 3: the
largest gain in geometry, from licensed data, and it removes a defect
native resolution makes six times worse.

### P4. Crisp 2D at large scales (filed)

HUD digits, icons, menu pictures and the crosshair drawn sharp instead of
blurred; optional scale2x/scale3x smoothing of 8-bit 2D pictures (gl1's
`r_scale8bittextures`, repeated up to the 2D scale); a sharp-bilinear
sample for non-integer scales
([pixel-art filtering](https://jorenjoestar.github.io/post/pixel_art_filtering));
the console background kept at 4:3 and cropped. Where:
[`rm_image.c`](../../src/client/refresh/metal/rm_image.c),
[`rm_draw.c`](../../src/client/refresh/metal/rm_draw.c),
[`rm_shaders.metal`](../../src/client/refresh/metal/rm_shaders.metal),
[`cl_console.c`](../../src/client/cl_console.c). One card, about 500
lines. After N10. Priority 4: the cheapest change seen in every frame.

### P5. Bicubic lightmap filtering (filed)

With `metal_lightmap_filter 1`: smooth light gradients and shadow edges
without the bilinear diamond pattern. Where: the `world_lm` fragment
function in [`rm_shaders.metal`](../../src/client/refresh/metal/rm_shaders.metal),
a cubic B-spline from four bilinear taps
([GPU Gems 2, chapter 20](https://developer.nvidia.com/gpugems/gpugems2/part-iii-high-quality-rendering/chapter-20-fast-third-order-texture-filtering)),
clamped to the face's lightmap rectangle, the weights in
[`mtl_shader_math.h`](../../src/client/refresh/metal/common/mtl_shader_math.h)
for the host test. One card, about 400 lines. After N10, ideally before RT2
(TASK54). Priority 5: cheap, on every world pixel, no assets.

### P6. A local texture upscaler on the player's machine (filed as two cards)

A console command makes 2x or 4x PNG versions of the world textures, skies
and skins from the player's own paks into the user's write directory, where
`r_retexturing` finds them; tiles are wrap-padded before scaling and cropped
after. First card: the command, the pak walk, the in-tree scale2x/scale3x
backend and the path checks (about 800 lines). Second card: VideoToolbox's
super-resolution scaler
([Apple](https://developer.apple.com/documentation/videotoolbox/vtsuperresolutionscalerconfiguration))
and MetalFX's spatial scaler as backends, and a local comparison report
that never leaves the machine (about 700 lines). After TASK11. Memory per
map: 14 MiB median at 2x, 55 MiB at 4x. Priority 6: the only licence-clean
source of sharper 1997 textures, quality to be measured.

### P7. Tag the layer as sRGB (filed)

Colours as authored on Apple's P3 displays. Where: `rmb_attach` in
[`rmb_device.m`](../../src/client/refresh/metal/rmb_device.m), behind
`metal_colorspace`; the shim stays as it is. Pins cannot see it (readback
is before composition), so the card measures a synthetic test card on a P3
display. One card, about 150 lines plus the measurement. After N10.
Priority 7: very cheap; the measurement decides the default.

### P8. MSAA in `ref_metal` (parked)

Parked by the owner. Until then the video menu's "multisampling" item
([`videomenu.c`](../../src/client/menu/videomenu.c)) stays and is shown
disabled under `vid_renderer metal`, with its reason, by TASK112 (#266);
it is not hidden or removed.

If unparked: `r_msaa_samples` works with the Metal renderer, with
memoryless multisample targets (stored 4x targets at 5K would take about
530 MB), resolved at the end of the 3D pass; it excludes the temporal
MetalFX modes. Where:
[`rmb_resource.m`](../../src/client/refresh/metal/rmb_resource.m),
[`rmb_encode.m`](../../src/client/refresh/metal/rmb_encode.m). One card,
about 700 lines. After TASK111. Priority 8 when unparked: the only
anti-aliasing with MetalFX off; matters most below 60 pixels per degree.

### P9. Sharp texels

With `metal_texfilter 1`: hard texel blocks (the `GL_NEAREST` look
[`doc/030_configuration.md`](../../doc/030_configuration.md) recommends)
with anti-aliased edges that do not crawl. Where: the world and alias
fragment functions, one fetch, sharing P4's function. One card, about 350
lines. After P4. Priority 9.

### P10. A 2D layout rectangle and a per-device 2D scale

The HUD inside the safe area (iPhone corners, camera housing, home
indicator; TV overscan) and a minimum physical text size on phones. The
single-player status bar is anchored at the bottom edge and deathmatch's
frag count in the top right corner ([`g_spawn.c`](../../src/game/g_spawn.c)).
Where: client only, [`cl_screen.c`](../../src/client/cl_screen.c) (unit
tested), [`cl_console.c`](../../src/client/cl_console.c), the menus,
`SDL_GetWindowSafeArea`. One card, about 500 lines, shared by TASK22 and
TASK23. Priority 10.

### P11. A console font from a system font

With `metal_confont 1`: text from a monospaced system font rendered by
CoreText at the current 2D scale; the special glyphs stay from `conchars`.
Where: `ref_metal`, a small Objective-C file and
[`rm_draw.c`](../../src/client/refresh/metal/rm_draw.c). No font file is
shipped. One card, about 600 lines. After P4. Priority 11.

### P12. Frame pacing on 120 Hz displays

Even 120 Hz frames on ProMotion screens, an optional two-drawable mode, and
a present-interval histogram in `metal_stats`
([`CAMetalDisplayLink`](https://developer.apple.com/documentation/quartzcore/cametaldisplaylink)).
Three drawables can hold 25 ms of frames at 120 Hz. One card, about 500
lines. After FIX53 (#250) and TASK107 (#251). Priority 12: needs a 120 Hz
Mac to judge.

### P13. Particle size cap relative to the resolution (filed)

With `metal_particle_cap 1`: near particles keep their 480-line
proportions (the 40-pixel cap scaled by `height / 480`). Where:
[`rm_effects.c`](../../src/client/refresh/metal/rm_effects.c). One card,
about 150 lines. After N9 (TASK92). Priority 13: small and correct.

### P14. Dynamic resolution

The 3D view's internal size follows the GPU time and the thermal state
within bounds, through TASK111's scaler; 2D stays native. A controller in
[`rm_pure.c`](../../src/client/refresh/metal/rm_pure.c), host tested. One
card, about 400 lines. After TASK111 and TASK112. Priority 14.

### P15. Sharper cinematics

The 320x240 cinematics through VideoToolbox's low-latency super-resolution
scaler, bicubic where it is missing. The `DrawStretchRaw` path of
[`rm_draw.c`](../../src/client/refresh/metal/rm_draw.c). One card, about
500 lines. Priority 15.

### P16. Fewer full-screen copies and memoryless depth on phones

The offscreen-to-drawable copy (12 MB a frame on an iPhone 15 Pro) and an
unread depth target, removed when no readback is pending. One card, about
400 lines, after FIX51 (#238) and an iPhone GPU trace. Priority 16.

### P17. Extended dynamic range output

Highlights above SDR white on XDR, OLED and HDR-TV screens, ordinary
surfaces unchanged; needs RT2's resolve and a float readback in the
harness. Two cards, after TASK54 and TASK59. Priority 17.

### P18. Denser lightmaps by relighting (parked)

The 1997 maps relit at 8 units per luxel with ericw-tools
(GPL-2.0-or-later, [repository](https://github.com/ericwa/ericw-tools)),
read as a BSPX `DECOUPLED_LM` lump
([Valve wiki](https://developer.valvesoftware.com/wiki/BSPX)). Parked: an
external tool, and a different light compiler changes the authored light;
P5 removes most of the bilinear pattern for free.

## 5. Ideas dropped

- The re-release maps: recompiled geometry and entities change play and
  break saves.
- The re-release lightmaps on the 1997 maps: the face lumps differ on all
  47 maps.
- Texture compression for phones: 4x textures (55 MiB median, 103 MiB at
  worst) cost less than the render targets; revisit if a phone measurement
  shows trouble.
- Better mipmaps: at 5K the mip chain is used only beyond about 1,900 units.
- Anisotropic and trilinear filtering: already in `ref_metal` and the menu.
- Frame interpolation: rejected by INFORM12 (#262) for latency.
- Tessellated MD2 models: new silhouettes, and the vertex wobble stays.
- Generated normal or material maps: embossed noise; per-pixel lighting is
  out of scope ([`ref-metal-native.md`](ref-metal-native.md) section 6).
- Detail textures: change the look's character.
- Panini or cylindrical projection: Apple's displays are 16:9, 16:10 and
  4:3, and `horplus` handles ultrawide monitors.
- Immersive visionOS: two views a frame do not fit one `refdef_t`, and
  head-decoupled aim changes play.
- The re-release's 1080p Theora cinematics: no Apple decoder, and libtheora
  would be a new vendored dependency.
- Supersampling: the 5K, phones and TV are above 60 pixels per degree.
- The re-release's bloom, fog and post effects: part of neither look.
- The re-release's 512x512 skies: they are new skies.

## 6. Licensing and distribution

- The 1997 release and the 2023 re-release data are id Software's
  copyright. They are never committed, uploaded, attached to an issue or a
  pull request, or sent to CI. Pin stores and comparison reports stay
  outside every git work tree ([`pins.sh`](../../scripts/pins.sh) and
  TASK114 already require it).
- Derived assets (P4's smoothed pictures, P6's textures, P11's atlas, P18's
  relit maps) are made on the player's machine from the player's own data,
  in memory or in the user's write directory. The project distributes the
  code that makes them, never the result. A command that writes refuses a
  path with `..` ([`CLAUDE.md`](../../CLAUDE.md)) and an output inside a
  git work tree.
- P3 reads the re-release from the player's own copy; the tests write their
  own synthetic MD5 files, as the [unit suite](../../tests/unit/) does for
  paks and maps.
- Community texture packs state no licence and derive from id's textures.
  The fork may link them, as [`doc/030_configuration.md`](../../doc/030_configuration.md)
  does, but never bundles, mirrors or pins against them. They ship as zips,
  which the fork no longer reads ([decision 0001](../decisions/0001-remove-optional-dependencies.md)).
- Fonts: P11 uses system fonts through CoreText and ships none. A bundled
  font would come from its own upstream under its own licence, with the
  owner's approval.
- System frameworks (ImageIO, CoreText, VideoToolbox, MetalFX) add no
  vendored code; ImageIO is approved by [decision 0010](../decisions/0010-imageio-decoder.md),
  the others need the same approval when their card is filed.
- External tools (ericw-tools for P18) are installed by the player, and only
  if the owner approves the feature.
- New original art may be committed under a GPL-compatible licence if it is
  not traced from id's art.

## 7. Card order

The port queue finishes first: N9 (TASK92, #203), then N10 (TASK93, #204).

| Step | Cards | Filed |
|---|---|---|
| 1 | This plan and [decision 0010](../decisions/0010-imageio-decoder.md) (TASK115) | yes |
| 2 | N9 (TASK92), then P13 | P13 yes |
| 3 | N10 (TASK93) and FIX45 (#230), then P1, then P7 | P1, P7 yes |
| 4 | TASK111 (#265, after P1), TASK112 (#266), TASK114 (#268); beside them P4, P5, then P9 | P4, P5 yes |
| 5 | TASK11 (#18, P2), then P6 (two cards) | yes |
| 6 | P3 (three cards), the renderer card before TASK53 if possible | no |
| 7 | P11 (after P4); P8 parked (the menu item is disabled under `metal` by TASK112) | no |
| 8 | P10 with TASK22 and TASK23; P12 after FIX53 and TASK107 | no |
| 9 | P14, P15, P16, P17; P18 parked | no |

## 8. Not measured

- The GPU cost of every proposal: no renderer ran for this plan.
- Whether the untagged layer visibly oversaturates colours on a P3 display
  (P7), how well the upscalers treat 64x64 tiles (P6, P15), whether ImageIO
  reads TGA on iOS and tvOS (P2), frame pacing above 60 Hz (P12), and the
  frame order of the 4 reordered MD5 animations (P3, checked through the
  MD2 frame names only).
- Pixel sizes on Apple TV and in visionOS come from SDL's code and Apple's
  default window size, not a device; Vision Pro's 34 to 44 pixels per degree
  are published estimates.
