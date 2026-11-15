# The 60 fps cap at 1920x1080 windowed (FIX52)

TASK97 (#224) measured `soft`, the Metal-backed `gl1` and `ref_metal`
at 60.0 to 60.1 fps in a 1920x1080 window with `r_vsync 0`, while
`ref_gl1` ran uncapped. The three capped renderers are the three that
present through a `CAMetalLayer` (`soft` through SDL's Metal renderer,
`SDL_CreateRenderer(window, NULL)` in `sw_main.c`); `ref_gl1` presents
through OpenGL. FIX52 (#239) diagnosed it on the bare-metal host.

**The display.** An Apple Studio Display (Retina LCD, 5120x2880
pixels) on an Apple M6, macOS 27.2.1. CoreGraphics reports the mode as
2560x1440 points, 5120x2880 pixels, at 60 Hz, and
`NSScreen.maximumFramesPerSecond` is 60: the panel's maximum refresh
is 60 Hz, so the cap equals the refresh. The visible frame (below the
menu bar, above the Dock) is 2560x1320 points at backing scale 2.

**A bare probe reproduces it.** A scratch Objective-C program of about
280 lines (never committed) opens one `NSWindow`, adds a subview
backed by a `CAMetalLayer` the way SDL's Metal view does, and only
clears and presents, as fast as it can, with SDL's settings for a
window without `SDL_WINDOW_TRANSPARENT` and without high pixel
density: opaque layer, `displaySyncEnabled = NO`,
`maximumDrawableCount` 3, `contentsScale` 1, drawable the window's
size in points. It prints the screen, the display mode, the layer
state, the present rate, the mean time spent in `nextDrawable` and how
many presented drawables reached the screen. No quake2 code runs. Two
runs of 600 frames each, sizes alternated (load average 1.73 to 1.84,
no other build running):

| Window (points) | fps | mean wait in `nextDrawable` | drawables shown / dropped |
|---|---|---|---|
| 1600x900 | 341.6 to 346.5 | 2.82 to 2.87 ms | 333 to 337 / 263 to 267 |
| 1920x1080 | 60.0 | 16.56 to 16.60 ms | 600 / 0 |

So the cap is below quake2: at 1920x1080 the window server hands a
drawable back only once a refresh and shows every one (none dropped),
as if `displaySyncEnabled` were `YES`.

**Where the cap starts.** One run of 200 to 300 frames per size in
the probe (load average 1.73 to 2.52; another agent's build was
running during part of it). It is neither the area nor a single
dimension:

| Height \ width (points) | 1200 | 1600 | 1680 | 1700 | 1720 | 1740 | 1760 | 1800 | 1920 | 2400 |
|---|---|---|---|---|---|---|---|---|---|---|
| 400 | 381 | 395 | | 433 | 333 | | 359 | 376 | 311 | 296 |
| 700 | 229 | 259 | | 280 | 60 | | | 60 | 60 | 60 |
| 900 | 295 | 357 | 329 | 311 to 339 | 60 | 60 | 60 | 60 | 60 | 60 |
| 1080 | 325 | 323 to 337 | | 342 | 60 | | | 60 | 60 | 60 |
| 1200 | 312 | 316 | | 314 | 60 | | | 60 | 60 | 60 |
| 1300 | 60 | 60 | | 60 | 60 | | | 60 | 60 | 60 |

The cap starts between 1700 and 1720 points of width (about two
thirds of the 2560-point screen) once the height is 700 or more, and
at every width once the height is 1300 (taller than the 1320-point
visible frame with the title bar). 1920x900, 1760x990, 1840x1035,
1919x1080, 1920x1079, 1921x1081 and 2000x1125 are capped; 1600x1080
(the same area as 1920x900) is not. The 640x480 and 1600x900 that
TASK97 measured sit below the threshold, 1920x1080 above it. The
engine agrees: `soft` ran at 263.8 to 268.1 fps at 1700x900 and at
60.0 at 1720x900 (two runs each, alternated, load average 2.56 to
4.29).

**What lifts it: an opaque layer is the cause.** One layer property
at a time in the probe at 1920x1080, two runs each, alternated with
the default settings (which gave 60.0 fps and 16.51 to 16.60 ms in
`nextDrawable` in every one of the 20 baseline runs; load average
1.86 to 2.19):

| Change | fps | mean wait in `nextDrawable` |
|---|---|---|
| `opaque = NO` | 394.0 to 437.8 | 2.26 to 2.46 ms |
| `maximumDrawableCount` 2 | 178.6 to 191.7 | 5.12 to 5.51 ms |
| `presentsWithTransaction = YES` | 120.0 to 120.1 | 6.51 to 7.01 ms |
| `contentsScale` 2, drawable 3840x2160 (no scaling by the compositor) | 60.0 | 16.45 to 16.46 ms |
| drawable half the window, 960x540 | 60.0 | 16.54 to 16.56 ms |
| layer colorspace sRGB, or Display P3 | 60.0 | 16.52 to 16.54 ms |
| `framebufferOnly = YES` | 60.0 | 16.57 to 16.61 ms |
| `MTLPixelFormatRGBA16Float` | 60.0 | 16.47 to 16.48 ms |
| window level floating (always on top) | 60.0 | 16.51 to 16.52 ms |

The scaling from 1x to the 2x panel, the colorspace and the pixel
format do not matter. A large opaque layer is what the window server
treats this way (consistent with it being scanned out directly rather
than composited, which macOS does not report), and a layer that is
not opaque is composited like a small one and keeps
`displaySyncEnabled = NO`. In the probe at 640x480 and 1600x900 the
opacity made no difference (320.8 to 367.6 fps either way, three runs
each, alternated, load average 2.13).

**The same change in the engine, final numbers** (`timedemo` of
`demo1.dm2`, windowed, `r_vsync 0`, three runs per cell; before and
after alternated run by run inside each size's batch; `ref_gl1` runs
alongside as the reference, it has no before or after and presents
through OpenGL). The before build of `soft` is main at 1b84bb4f, the
after build is 1b84bb4f plus this card's `sw_main.c` change. The
`ref_metal` and Metal-backed `gl1` rows are scratch builds with
`opaque = NO` added next to their `displaySyncEnabled` line, never
committed. Every build here, before and after, is main at 1b84bb4f (the
card's base when it was measured) plus the change named. That `ref_metal`
draws no brush or alias-model entities (TASK90, #244, added them later),
and no sprites, beams, dynamic lights or particles (N8, TASK91, and N9,
both after 1b84bb4f), so the `ref_metal` rows and the
FIX51 paragraph's ceiling below are not a baseline for a later
`ref_metal`:

| Renderer | 1920x1080 before | after | 640x480 before | after |
|---|---|---|---|---|
| `soft` (this card's fix) | 60.0 | 258.1 to 263.1 | 309.1 to 327.5 | 381.1 to 404.6 |
| `ref_metal` (scratch) | 60.0 to 60.1 | 339.7 to 344.2 | 294.2 to 299.4 | 329.7 to 348.9 |
| Metal-backed `gl1` (scratch) | 60.1 | 324.8 to 341.8 | 249.1 to 259.4 | 321.8 to 340.1 |
| `ref_gl1` (reference) | 1882.5 to 2044.5 | | 2452.0 to 2469.5 | |

The quiet window, per batch (`uptime` load averages before and after;
no `make`, `cc`, `clang` or `quake2` process before or after; GPU
"Device Utilization %" from `ioreg` before and after, the second
reading taken right after the last engine exited):

| Batch | Load average | GPU utilization before / after |
|---|---|---|
| 1920x1080, `r_vsync 0` | 3.10, then 1.68 | 5% / 56% |
| 640x480, `r_vsync 0` | 1.62, then 1.83 | 2% / 42% |
| `soft`, `r_vsync 1`, 1920x1080 | 1.92, then 2.22 | 2% / 19% |
| `soft`, `r_vsync 1`, 640x480 | 2.22, then 2.31 | 21% / 11% |

The first batch began with the load still decaying from the builds
(3.10, 1.68 at its end); the other readings are steady. In the engine
the opaque layer also costs about 0.4 to 0.9 ms a frame below the
threshold: at 640x480 `soft` moves from about 315 to about 390 fps,
`ref_metal` from about 296 to about 340, the Metal-backed `gl1` from
about 256 to about 328 (the means of the ranges above).

How these runs were made: the engine was started as a pin run with
`pins_capture 1000000` (at most one frame captured), so that it quits
by itself at the demo's end, plus `+set r_vsync 0 +set timedemo 1
+demomap demo1.dm2`, `-portable`, `-datadir $PINS_BASEDIR`,
`s_initsound 0`, `vid_fullscreen 0`, `r_mode -1` and the size in
`r_customwidth` and `r_customheight`; the fps line is the engine's
own. A plain `timedemo 1` run does not quit at the end (see the first
run above).

**The fix (`soft`).** `RE_InitContext` in
`src/client/refresh/soft/sw_main.c` sets the layer of SDL's Metal
renderer (`SDL_GetRenderMetalLayer`) not opaque when `r_vsync` is 0,
and opaque, as SDL makes it, when `r_vsync` is 1; the file is C, so it
sends `setOpaque:` through the Objective-C runtime it finds with
`dlsym`, and nothing new is linked. Every pixel `soft` presents has
alpha 255 (the palette's alpha byte, `R_GammaCorrectAndSetPalette`,
and a streaming texture drawn over the whole output), so a composited
layer shows the same picture; the clear at context creation is now
opaque black instead of alpha 0. With `r_vsync 1` `soft` runs at 59.9
to 60.0 fps before and after, at 1920x1080 and at 640x480 (two runs of
each, alternated, load average 1.92 to 2.31). The picture was not
compared on screen. No cvar, no change to the window
(`glimp_sdl3.c`), and other platforms are untouched.

**Findings for the Metal renderer files** (not edited by FIX52; their
owner's): `rmb_attach` and `rmb_set_vsync` in
`src/client/refresh/metal/rmb_device.m`, and `mgl_layer_create` and
`mgl_layer_set_sync` in `src/client/refresh/gl1/metal/mgl_layer.m`,
leave the layer opaque, as SDL creates it. A layer that is not opaque
while `r_vsync` is 0 lifts the cap (the scratch numbers above). Before
that ships, the drawable's alpha has to be 1 everywhere: `ref_metal`
copies its offscreen color target to the drawable with a blit and the
shim draws the GL frame straight into the drawable, so blended draws
can leave alpha below 1, which an opaque layer ignores and a
composited one blends with the window's background. Whether they do
was not measured here.

**FIX51 (#238), the fixed cost of about 3 to 4 ms a frame.** What this
explains and what it does not. Two parts were measured. About 0.4 to
0.9 ms is the opaque layer (the 640x480 columns above). The rest is the time the window server takes to hand back a
drawable: the bare probe, which only clears, spends 2.26 to 3.07 ms a
frame blocked in `nextDrawable` at every size below the threshold and
with `opaque = NO` at 1920x1080, and the engine with a layer that is
not opaque tops out at 321.8 to 404.6 fps in all three renderers at
640x480. `ref_gl1` does not wait there (2452.0 to 2469.5 fps at
640x480, 1882.5 to 2044.5 at 1920x1080; those figures may be a CPU-submit
rate, as FIX51 says, since Apple's OpenGL queues frames, so the engine's
clock does not wait for the GPU).
So a Metal renderer that waits for a drawable every frame stops
at about 400 fps on this host; going past it needs a frame that does
not block in `nextDrawable` (not tried here).

**Open.** A follow-up: repeat the probe on a display above 60 Hz (an
apple7 Mac with a 120 Hz internal display), to tell whether the cap
follows the refresh rate and where the size threshold sits there. The
panel here is 60 Hz, so this host cannot tell a cap that is the
refresh rate from a fixed 60. That follow-up is TASK107 (#251). One
data point already in this file: the TASK90 section records the
`demo1.dm2` timedemo of `ref_metal` running at the build VM's display
rate of 120 fps whatever `r_vsync` and `vid_maxfps` said (window size
not given there).
