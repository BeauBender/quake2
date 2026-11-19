# Plan: HDR output in `ref_metal` on EDR displays (TASK196)

Plan anchors: [the Metal renderer plan](metal-renderer.md),
[the presentation modes](presentation-modes.md) (section 5, Retro) and
[the high-resolution plan](high-resolution.md) (P7, the layer's colour
space, and P17, extended dynamic range, which this plan takes up). Decision
records [0011](../decisions/0011-metalfx-framework.md) and
[0012](../decisions/0012-metalfx-in-the-client.md) (MetalFX). Cards: TASK196
(#553, this plan), TASK195 (#552, the live tuning harness that shows the
switch and saves it in a preset), TASK183 (#530, `r_presentation`), TASK184
(#531, the Retro pass), TASK121 (`metal_colorspace`), TASK111 (MetalFX),
TASK59 (the denoiser) and TASK175 (visionOS).

Status: proposed. Nothing here is built, and no slice below is filed. This
document changes no engine code, no pin and no default.

Every number below is either a fact read from the tree or Apple's
documentation, with its source, or marked *estimate* with its basis. No
display's headroom has been measured yet; slice HD1 is the first that does.

## 1. Goal

The owner, 2026-11-18: "I also want you to add HDR to the harness". The
harness is TASK195; this plan is the HDR output it exposes: highlights
above SDR white on displays that can show them (Apple's extended dynamic
range, EDR), switched live, with a peak-brightness setting.

What stays stock:

- `r_hdr 0`, the default, is today's renderer: the same pixel formats, the
  same pipelines, the same layer settings. Every existing pin is
  byte-identical, which each slice proves with the full base-against-branch
  pin comparison.
- With `r_hdr 1`, every value at or below SDR white is shown as it is
  today (the tone map is the identity up to 1.0, section 2.3). Only what is
  clipped today, the part of a pixel above white, becomes visible.
- No change to the renderer API (`API_VERSION` in
  [`ref.h`](../../src/client/vid/header/ref.h)), the protocol or the
  savegame format.

## 2. The pipeline

### 2.1 What exists

- The 3D and 2D passes draw into one offscreen colour target,
  `MTLPixelFormatBGRA8Unorm`, made by `rmb_new_target` in
  [`rmb_encode.m`](../../src/client/refresh/metal/rmb_encode.m); at the
  end of the frame it is copied to the drawable by a blit, or by
  `rmb_present_pass` (`rm_fragment_present` in
  [`rm_shaders.metal`](../../src/client/refresh/metal/rm_shaders.metal))
  when the layer is not opaque.
- The layer is set up by `rmb_attach` in
  [`rmb_device.m`](../../src/client/refresh/metal/rmb_device.m):
  `pixelFormat` `BGRA8Unorm`, `framebufferOnly` NO, its colour space as SDL
  left it unless `metal_colorspace 1` tags it `kCGColorSpaceSRGB` (TASK121;
  [its runbook](../runbook/TASK121.md) found the layer already reads sRGB on
  the bare-metal host's display).
- The stock shading is display-referred 8-bit arithmetic that matches gl1:
  `mtl_world_color` on bytes, every fragment output saturated. Nothing in
  the stock path exceeds 1.0.
- `vid_gamma` is not a pass: `R_InitImages`
  ([`rm_image.c`](../../src/client/refresh/metal/rm_image.c)) bakes
  `gammatable` into textures, lightmaps, the palette, particles and flashes
  at `vid_restart`, as gl1 does.
- MetalFX ([`rmb_metalfx.m`](../../src/client/refresh/metal/rmb_metalfx.m))
  takes and gives `BGRA8Unorm` (`RMB_FX_COLOR`); the spatial scaler runs in
  its `perceptual` mode and the temporal scaler with auto exposure off. It
  writes the view's rectangle of the frame target at the G-buffer seam
  (`rmb_fx_upscale`, called from
  [`rmb_gbuf.m`](../../src/client/refresh/metal/rmb_gbuf.m)).
- The ray-tracing terms are written to the G-buffer's `t_light`
  (`RGBA8Unorm`) and resolved into `t_rcolor` (`BGRA8Unorm`), so any
  radiance above 1.0 is clipped there today.
- TASK184 (branch `task184`, not on main) runs the Retro pass at the end of
  `RI_RenderFrame`, on the view's rectangle of the frame target, before the
  2D draws: RGB565 with a dither, an optional box filter and `r_retro_gamma`.
- The 2D layer (HUD, console, menus, cinematics) is drawn after the 3D view
  into the same target, by `rmb_draw_2d`, with saturated outputs.

### 2.2 Formats and colour space

Under `r_hdr 1`:

- the frame target becomes `MTLPixelFormatRGBA16Float`, and every pipeline
  that draws into it is made for that format (made on first use, as the
  Retro and MetalFX pipelines are, so a stock run builds nothing more);
- the layer gets `pixelFormat` `RGBA16Float`, `colorspace`
  `kCGColorSpaceExtendedLinearSRGB` and `wantsExtendedDynamicRangeContent`
  YES. Apple: with that property set, "all rendered content is clamped to
  the screen's maximumExtendedDynamicRangeColorComponentValue value rather
  than 1.0"
  ([CAMetalLayer](https://developer.apple.com/documentation/quartzcore/cametallayer/wantsextendeddynamicrangecontent);
  macOS 10.11, iOS and iPadOS 16, visionOS 1; not listed for tvOS);
- the target keeps the stock, display-referred (sRGB-encoded) values, so
  every blend and every 8-bit term composes as it does today; the present
  pass converts to linear once, with the sRGB transfer function extended
  past 1.0, sign-preserving, as it writes the drawable. So the present under
  `r_hdr 1` is always the fragment pass, never the blit.

Values in the drawable then mean what Apple's EDR model means: 1.0 is SDR
white, 2.0 is twice its luminance, and the display shows up to its current
headroom.

The colour space, extended linear sRGB against extended linear Display P3:

| | Extended linear sRGB (the call) | Extended linear Display P3 |
|---|---|---|
| Stock colours | Same chromaticities as the 1997 art, which is sRGB | Every colour must be converted to P3 first, or it is shown oversaturated (TASK121 measured that tagging P3 changes the captured values) |
| Wider gamut | Reachable later through negative components, which float16 holds | Native |
| Cost | One transfer function in the present pass | The same, plus a 3x3 matrix |

The 1997 content has no colour outside sRGB, so P3 buys nothing today and
costs a conversion that is one more thing to get wrong (call H2).

Rejected: a `BGR10A2Unorm` drawable in PQ (`kCGColorSpaceITUR_2100_PQ`)
with `CAEDRMetadata.hdr10` metadata, which hands tone mapping to the
system. It is the right shape for mastered video, not for a game whose peak
follows the user's setting and the display's live headroom, and its
encoding would need the 2D layer re-encoded into PQ too.

### 2.3 Where the extended range comes from, and the tone map

Nothing in the stock path exceeds 1.0 (section 2.1), so `r_hdr 1` makes
values above white only where they exist:

1. **Additive effects**: particles, flashes, beams, explosions. Their
   blending sums into the target; a float16 target does not clamp the sum
   as `BGRA8Unorm` does, so overlapping effects get brighter cores. This
   needs no new code beyond the format.
2. **The ray-tracing terms**: the sun, the bounce and RT-primary's direct
   light (presentation-modes section 4) are radiance that is clipped in
   `t_light` and `t_rcolor` today. Under `r_hdr 1` those two become
   `RGBA16Float` (slice HD6).
3. **Emissive surfaces** (`SURF_LIGHT` faces, the sky, lava): a gain above
   white would be a new look under the stock presentation. Not in this
   plan's slices unless the owner asks (section 8).

The tone map runs once, on the view's rectangle at the output size, at the
end of the 3D view, after MetalFX (section 2.6): the seat TASK184 gives the
Retro pass, before the 2D draws. It decodes to linear, maps, and encodes back, in place.

The candidates:

| Operator | Below white | Above white | Notes |
|---|---|---|---|
| Clip at the peak | identity | hard clip | Bands at the peak; flat fire |
| Reinhard, extended | compresses | rolls off | Changes every SDR value: not stock |
| ACES fitted (Narkowicz, Hill) | compresses, shifts hue | rolls off | A filmic look on every pixel: not stock |
| Hable (Uncharted 2) | compresses | rolls off | Same objection |
| AgX | compresses | rolls off | Same objection |
| BT.2390 EETF knee | identity below a knee | Hermite roll-off | The display-adaptation curve for HDR10; its knee sits below white |
| **A Reinhard shoulder above white** | identity | rolls off to the peak | The call |

The call (H3): identity up to 1.0, and above it
`f(x) = 1 + e / (1 + e / (P - 1))` with `e = x - 1` and `P` the peak in
force, applied to the largest of the three linear components and the
pixel scaled by `f(m) / m`, so hue is kept. The curve has slope 1 at white
(no visible knee), is monotonic, and never reaches `P`. Every SDR pixel is
untouched, which is the stock rule. The arithmetic goes in a shared header
under `src/client/refresh/metal/common/`, as `mtl_retro_math.h` does for
Retro, so the host test checks the shader's numbers.

### 2.4 `r_hdr_peak` and the display's headroom

The peak in force each frame is `P = min(r_hdr_peak, headroom)`, at least
1.0, where headroom is:

- macOS: `NSScreen.maximumExtendedDynamicRangeColorComponentValue` of the
  window's screen, which changes with brightness and conditions;
  `maximumPotentialExtendedDynamicRangeColorComponentValue` (fixed when the
  `NSScreen` is made; 1.0 means no EDR) decides the greying
  ([Apple](https://developer.apple.com/documentation/appkit/nsscreen/maximumpotentialextendeddynamicrangecolorcomponentvalue));
- iOS and iPadOS: `UIScreen.currentEDRHeadroom` and `potentialEDRHeadroom`
  ("the ratio of the luminance of the screen's brightest white to the
  luminance of standard dynamic range (SDR) white",
  [Apple](https://developer.apple.com/documentation/uikit/uiscreen/potentialedrheadroom);
  iOS 16, tvOS 16).

The headroom is read once a frame on the main thread, where `ref_metal`
already runs, and the peak is a uniform of the tone-map pass, so a change
of either applies on the next frame. Clamping to the headroom is not only a
nicety: the system clamps to it anyway, and mapping past it would flatten
the roll-off into a clip.

### 2.5 The 2D layer stays at SDR white

The 2D draws come after the tone map, into the same target, with saturated
outputs, so an opaque 2D pixel is at most 1.0 and the present pass's
transfer function maps it to the same displayed colour as today. A
translucent 2D pixel (the console background, a menu backdrop) over a
highlight blends with it, as it blends with any scene; that is the scene
showing through, not the 2D layer above white. Cinematics are 2D and stay
SDR.

### 2.6 The order in a frame

```
3D view (internal size, RGBA16Float, stock encoded values, additive sums unclamped)
  -> RT resolve into t_rcolor (RGBA16Float under r_hdr 1, HD6)
  -> MetalFX upscale (float16 in and out, HD4)
  -> tone map to [0, P] (the Retro seat; HD3)
  -> 2D layer (saturated)
  -> present pass: sRGB-encoded to extended linear sRGB, into the RGBA16Float drawable
```

The tone map runs after MetalFX, not before, so the scaler sees the
untouched view and its history is not built on a curve that moves with the
headroom. If HD4 finds the temporal scaler unstable on the unbounded input
(fireflies from additive cores), the tone map moves before it and the
spatial scaler's mode becomes `hdr`; that is the fallback, decided by
HD4's frames.

### 2.7 Interactions

- **`vid_gamma`**: unchanged. It is baked into the 8-bit inputs before
  anything here, so it moves the SDR part of the picture as today, and the
  tone map acts after it. `r_hdr` applies no gamma of its own.
- **MetalFX**: the spatial scaler's colour processing has a `linear`, a
  `perceptual` and an `hdr` mode
  ([Apple](https://developer.apple.com/documentation/metalfx/mtlfxspatialscalercolorprocessingmode));
  under `r_hdr 1` it gets `RGBA16Float` in and out and the `hdr` mode. The
  temporal scaler's descriptor takes any `MTLPixelFormat` for colour and
  output; Apple's page does not list which formats it accepts
  ([Apple](https://developer.apple.com/documentation/metalfx/mtlfxtemporalscaler)),
  so whether it accepts `RGBA16Float` is *to be verified* by HD4, which
  makes one and records the result. If it refuses, `r_hdr 1` with the
  temporal scaler falls back to MetalFX off with the existing note
  (`rmb_fx_note`), never silently. Auto exposure stays off;
  `preExposure` stays 1.0, since the view is not exposure-scaled.
- **The denoiser** (TASK59, `MTLFXTemporalDenoisedScaler`): its output
  format follows the frame target, float16 under `r_hdr 1`, so the
  denoised radiance is not clipped. Same verification as the temporal
  scaler.
- **Retro** (`r_presentation 2`): the 3D view is quantized to RGB565, an
  SDR look by definition. Under Retro, `r_hdr` is greyed with "Retro is a
  16-bit SDR look" and the renderer runs the stock formats (call H5). The
  presentation overlay of TASK183 carries this, so no cvar is written.
- **Enhanced and RT-primary**: the main consumers. RT-primary's resolve is
  linear radiance with an exposure fit (presentation-modes section 4); under
  `r_hdr 1` its exposure keeps SDR white where it is today and the tone map
  takes what is above it.
- **`metal_colorspace`**: under `r_hdr 1` the colour space is the extended
  linear one and `metal_colorspace` is greyed with "set by HDR"; under
  `r_hdr 0` it works as today.
- **The present inset** (`rmb_present_inset_set`, the safe area) and
  display sync are unchanged; the present pass already handles the inset.

## 3. Platforms and device classes

| Class | Examples | `r_hdr` | Reason text when greyed |
|---|---|---|---|
| Mac, built-in XDR display | MacBook Pro 14 and 16 (2021 on), and the Pro Display XDR | works | |
| Mac, a standard display with backlight to spare | Studio Display, iMac, MacBook Air below full brightness | works where the screen reports a potential headroom above 1.0: macOS offers EDR on such panels (to be measured by HD1 on the M6's display) | "this display reports no extended range" when it reads 1.0 |
| Mac, an external HDR display | HDR10 monitors and TVs with "High Dynamic Range" on in Displays settings | works | "this display reports no extended range" when HDR is off in Displays settings |
| Mac, SDR display at full brightness, or the build VM | | greyed | "this display reports no extended range" |
| iPhone and iPad, XDR or OLED | iPhone 12 and later with OLED, iPad Pro with mini-LED or OLED | works, on a device; the simulator cannot show it | |
| iPhone and iPad, LCD | iPhone 11, iPhone SE, iPad Air with LCD | greyed (`potentialEDRHeadroom` 1.0, to be read) | "this display reports no extended range" |
| Apple TV | Apple TV 4K | greyed | "not built for Apple TV yet": `wantsExtendedDynamicRangeContent` is not listed for tvOS, and the system's HDR10 output follows the box's video format setting, which a game layer does not drive. `UIScreen.potentialEDRHeadroom` exists on tvOS 16; a device slice can find out (call H7) |
| visionOS | Apple Vision Pro (TASK175) | greyed | "no headroom reading on visionOS": the layer property exists (visionOS 1) but `UIScreen` does not, so the peak cannot be clamped; a device slice after TASK175's later slices |
| Other renderers | gl1, gl3, soft | greyed | "HDR is a Metal renderer feature" |

A greyed item is shown, never hidden (the menu rule in
[high-resolution.md](high-resolution.md)). The console still accepts
`r_hdr 1` on a display with no headroom: the frame is then drawn in float16
and shown at SDR (peak 1.0), and the report line says so. That is what
lets the pins and the VM exercise the float16 path (section 5).

## 4. Cvars, commands and the menu

| Name | Kind | Default | Range | Applies |
|---|---|---|---|---|
| `r_hdr` | cvar, `CVAR_ARCHIVE` | 0 | 0 off, 1 on; anything else counts as 0 | live: the next frame remakes the target, the pipelines it needs and the layer's three properties (call H4) |
| `r_hdr_peak` | cvar, `CVAR_ARCHIVE` | 4.0 | 1.0 to 16.0, clamped; the peak as a multiple of SDR white, held to the display's headroom each frame | live, a uniform |
| `metal_hdr_info` | command | | | prints the layer's state (pixel format, colour space name, the EDR property), the headroom now and its potential, and the peak in force |
| `metal_hdr_probe <x> <y>` | command | | | reads one pixel of the last drawable before its present (the drawable is not `framebufferOnly`) and prints its four float16 values; the acceptance's "measured pixel above 1.0" |

All four are documented in `doc/040_cvarlist.md` and `doc/050_commands.md`
by the slice that adds them. The report line, printed when either cvar
changes: `ref_metal: hdr: on (r_hdr 1), peak 4.0 (r_hdr_peak 4.0, display 3.2 now, 16.0 potential)`,
or `off`, or `on, shown at SDR: this display reports no extended range`.

The menu:

- Video options gets "HDR" (off, on) and "HDR peak" (a slider from 1.0 to
  16.0 in steps of 0.5, showing the display's headroom beside it), after
  the colour-space item, because HDR is display output, not ray tracing.
- The TASK195 Tuning screen shows the same two items, so the owner can
  flip HDR in motion with the rest; its A/B toggle includes them.
- `rt_preset save` writes `r_hdr` and `r_hdr_peak` with the other values;
  loading a preset with `r_hdr 1` on a display with no headroom sets it
  (shown at SDR, as above).
- Greying, with the reasons in section 3, plus "Retro is a 16-bit SDR look"
  under `r_presentation 2`. "HDR peak" is greyed with "HDR is off" while
  `r_hdr` is 0.

## 5. Verification

- **The pins at `r_hdr 0`**: the full base-against-branch comparison on
  every slice; byte-identical or the slice is not done. Runs on the VM and
  with a locked screen.
- **The pins at `r_hdr 1`**: the harness reads 8-bit frames (`rmb_read` in `rmb_encode.m`
  copies `w * 4` bytes a row, and the frames are TGA). Under `r_hdr 1` the
  readback converts the float16 target to what an SDR display shows (the
  tone map with `P = 1`, then the encoded value rounded to a byte) in one
  compute or fragment pass into a scratch `BGRA8Unorm` texture, and reads
  that. So the recorded HDR scenario is the SDR fallback, and it differs
  from stock only where additive effects or RT terms exceeded white (where
  the stock path clipped the sum, the fallback clips after the tone map at
  1.0). An `hdr` scenario under `scripts/pins/scenarios/` records it. Runs
  on the VM (no headroom needed: section 3) and with a locked screen.
- **The measured pixel above 1.0**: `metal_hdr_probe` on a pixel in an
  explosion core or a sunlit surface prints a value above 1.0, and
  `metal_hdr_info` shows `RGBA16Float`, the extended linear sRGB colour space
  and the EDR property on. A value above 1.0 in the drawable needs no
  headroom (the drawable holds what was written; the display clamps), so
  the probe runs with the screen locked. The headroom reading itself needs
  the display on and awake: what `maximumExtendedDynamicRangeColorComponentValue`
  reads while the display sleeps is *to be measured* by HD1, and recorded in
  its runbook with the M6's display model.
- **The owner's frames**: a few HDR on and off pairs. An SDR PNG cannot
  show the difference, so they are either an HDR capture
  (ScreenCaptureKit's HDR capture of the engine's own window, the in-process
  path the harness already uses, saved as a 16-bit HEIC; *to be verified*
  that it carries the extended range), or the owner looking at the running
  game on the HDR display. Sent directly, never committed or uploaded, with
  the display they were shown on named. Needs the display on and unlocked.
- **The tone map**: a host test of the shared header: identity at and
  below 1.0, slope 1 at white, monotonic, below `P` for every input, hue
  kept (the ratio of components), `P = 1` gives the clamp. VM.

What needs the display on: the headroom readings (HD1), the greying on a
real display (HD5's walk on the M6), the owner's frames (HD3, HD6). The
rest runs on the VM or with the screen locked.

## 6. Cost estimate

*Estimate.* Basis: bytes a pixel times the pixels, for the frame target
and the three drawables (`maximumDrawableCount`, `RMB_FRAMES_IN_FLIGHT`
in `rmb_device.m`, taken as 3); `BGRA8Unorm` is 4 bytes a pixel,
`RGBA16Float` 8. Bandwidth counts one store of the target per pass that
draws into it (Apple's GPUs blend in tile memory, so overdraw adds no
DRAM traffic), one read by the present, one write of the drawable, and the
tone-map pass's read and write: 5 full-frame touches under `r_hdr 1`
against 3 today. At 60 frames a second.

| Size | Pixels | Target + 3 drawables, 8-bit | Same, float16 | Per frame, 8-bit (3 touches) | Per frame, float16 (5 touches) | At 60 Hz, 8-bit | At 60 Hz, float16 |
|---|---|---|---|---|---|---|---|
| 1920x1080 | 2.07 M | 33 MB | 66 MB | 25 MB | 83 MB | 1.5 GB/s | 5.0 GB/s |
| 5120x2880 | 14.7 M | 236 MB | 472 MB | 177 MB | 590 MB | 10.6 GB/s | 35.4 GB/s |

The tone map runs at the output size (section 2.6); if HD4 moves it
before MetalFX, it runs at the internal size and costs less. For scale, the M1's
memory bandwidth is 68 GB/s (Apple's published figure), so float16 at 5K
and 60 Hz is about half of an M1's bandwidth by this count: the 5K HDR case
on an M1 is likely bandwidth-bound and is measured, not assumed, by HD3.
The MetalFX history textures double with the format too (HD4 counts them
with the ledger, `rmb_ledger_note`).

## 7. The order of work, as proposed slices

Not filed. Tiers as in [presentation-modes.md](presentation-modes.md)
section 7: VM, M6, device. Sizes: S under about 300 changed lines, M about
300 to 700.

| Id | Title | depends_on | Tier | Size |
|---|---|---|---|---|
| HD1 | The headroom readings (macOS, iOS) and `metal_hdr_info`; the M6 display's readings in the runbook, display awake and asleep; no visual change | | VM (build), M6 with the display on | S |
| HD2 | `r_hdr`: the float16 frame target and its pipelines, the layer's format, extended linear sRGB colour space and EDR property, the linearizing present, the live switch, the SDR-fallback readback; `r_hdr 0` byte-identical; `doc/040` | HD1 | VM (pins, both values) and M6 (layer state) | M |
| HD3 | The tone map (shared header, host test) at the Retro seat, `r_hdr_peak` clamped to the headroom, the report line, `metal_hdr_probe`, the `hdr` scenario; the first owner frames; the 5K cost measured | HD2, TASK184 | VM (test, pins), M6 with the display on (probe, frames, timing) | M |
| HD4 | MetalFX and the denoiser under `r_hdr 1`: float16 formats, the spatial `hdr` mode, the temporal scaler's acceptance of `RGBA16Float` recorded, the fallback note if refused | HD3 | M6 | S |
| HD5 | The menu: Video options items, the Tuning screen items and A/B, preset keys, every greying reason, Retro's override; the menu walk | HD3, TASK195 | VM (walk) and M6 with the display on (a real EDR display ungreys) | M |
| HD6 | The RT terms in float16 under `r_hdr 1` (`t_light`, `t_rcolor`), so the sun and RT-primary go above white; owner frames | HD4, TASK59 | M6 with the display on | M |
| HD7 | iPhone and iPad: the readings and the greying on devices, one XDR and one LCD | HD5 | device | S |

Apple TV and visionOS stay greyed by HD5; a slice for each waits for the
owner (section 8) and a device.

## 8. Calls the plan makes, and what the owner decides

Each call is reversible, changes no stock pixel, pin, protocol or savegame,
and is the default the slices build unless the owner says otherwise.

**H1. The target keeps encoded values; the present linearizes.**
Context: the stock look is 8-bit arithmetic in the encoded space. Options:
shade in linear light from the start (changes every blend, every pin);
keep encoded values and convert once at the end. The call: the second.
Reverse: a linear-light 3D view can come later behind its own switch.

**H2. Extended linear sRGB, not Display P3.**
Context and options: section 2.2. The call: sRGB, because the art is sRGB.
Reverse: one colour-space constant and a matrix in the present pass.

**H3. A Reinhard shoulder above white, on the largest component.**
Context and candidates: section 2.3. The call: identity to 1.0, roll-off
to the peak, hue kept. Per-component mapping (which whitens hot cores, a
look some prefer for fire) is the one-line alternative. Reverse: the curve
is one function in a shared header.

**H4. `r_hdr` switches live.**
Context: the card asks for live or a stated restart. The call: live, by
remaking the target and the pipelines that draw into it on the next frame,
as MetalFX remakes its scaler. If HD2 finds a frame that cannot be remade
mid-run, the cvar says "takes effect at vid_restart" and the menu offers
one. Reverse: either way the stored value is the same.

**H5. Retro greys HDR.**
Context: RGB565 is an SDR look. The call: greyed, stock formats under
Retro. Reverse: the tone map could run after Retro if the owner wants
Retro's glare above white.

**H6. The console accepts `r_hdr 1` without headroom.**
Context: the pins and the VM must reach the float16 path. The call: drawn
in float16, shown at SDR, said in the report line; only the menu greys.
Reverse: a refusal is one test in the decide function.

**H7. Apple TV and visionOS greyed until a device slice.**
Context: section 3. The call: greyed with their reasons. Reverse: a slice
per platform when there is a device.

The owner decides:

1. Whether emissive surfaces (`SURF_LIGHT` faces, sky, lava) go above white
   under the stock presentation. The plan says no: only additive effects
   and the ray-tracing terms, so `r_hdr 1` on stock changes nothing that is
   not clipped today. A yes adds one slice and a gain setting.
2. The tone map's look, from HD3's frames: H3's shoulder, the per-component
   variant, or a clip.
3. The default `r_hdr_peak` (the plan says 4.0).
4. Where the switch lives: Video options and the Tuning screen (the plan),
   or the Tuning screen only.
5. Whether Apple TV and visionOS get their device slices, and when.
6. Whether a default of `r_hdr 1` on EDR displays is ever wanted. The plan
   keeps 0, so stock is unchanged.
