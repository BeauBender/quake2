# Screenshot and capture readback on a physical GPU (TASK98)

Host: the bare-metal Apple Silicon Mac (Apple M6, 12 GPU cores, Metal 4,
one 5120x2880 Retina display), not the build VM; engine log
`GL_VENDOR: Apple`, `GL_RENDERER: Apple M6` for `ref_gl1` through Apple's
OpenGL, `GL_RENDERER: Metal shim on Apple M6 (gl1-metal)` for the
Metal-backed `gl1`, and `ref_metal` (no GL string). Load average (`uptime`) 2.4
to 3.6 over the runs, another agent recording on the same machine
between them. The comparator is `scripts/pins/framediff.c`; the per-channel
statistics and the row-order and channel-order tests (the image against the
reference as it is, flipped top to bottom, and with red and blue swapped) come
from a 60-line C tool kept outside the repository; the harness scores
(6 decimals) are `framediff` output, and the `screenshot` command scores (5
decimals) came from that same throwaway tool. Nothing derived from game
data is kept in the tree. The display's screen saver started at 16:38Z during
the windowed runs; every run was validated from its artifacts (23 frames,
`exit 0`, non-black statistics), not from the screen, and none stalled.

## What was measured

1. **Harness capture** (`SCR_PinsCapture`, which runs the renderer's own
   `screenshot tga` inside the frame): `demo1.dm2` with the `hud2d` (2D) and
   `world` (3D) scenarios, 640x480, `pins_capture 30`, the engine started as
   `scripts/pins.sh` starts it (`PINS_DRYRUN=1 scripts/pins.sh record` printed the line)
   but with `vid_fullscreen` set to 0 or 1 by hand. Two windowed runs per
   renderer and scenario, one fullscreen run, 23 frames each.
2. **`screenshot` command between frames**: a console script (`map base1`, 200
   `wait`s, `screenshot tga`, `menu_main`, 60 `wait`s, `screenshot tga`,
   `quit`) with `vid_renderer`, `vid_fullscreen` and `r_mode -1` 640x480 as
   above; the first picture is the 3D map, the second the main menu (2D). Three
   windowed and three fullscreen runs per renderer.

## Results

| Renderer | Case | Result |
|---|---|---|
| `ref_gl1` (Apple M6) | harness, 2D `hud2d`, windowed and fullscreen | 23 frames, 640x480; run 1 against run 2 and against fullscreen: score 0.000000, 0 differing bytes in all 23 frames |
| `ref_gl1` | harness, 3D `world`, windowed and fullscreen | run 1 against run 2: score 0.000000; windowed against fullscreen: score 0.000000 to 0.000002, bad fraction at most 0.000016 (largest channel difference 42) |
| Metal-backed `gl1` | harness `hud2d` | against `ref_gl1`: score 0.000000 in all 23 frames, windowed (2 runs) and fullscreen |
| Metal-backed `gl1` | harness `world` | against `ref_gl1`: score 0.000000 to 0.000010, bad at most 0.000033, largest channel difference 36 (2 windowed runs, same range fullscreen) |
| `ref_metal` | harness `hud2d`, windowed | against `ref_gl1`: score 0.000000 in all 23 frames, two runs |
| `ref_metal` | harness `world`, windowed | against `ref_gl1`: score 0.000000 to 0.049744, bad up to 0.264, largest channel difference 254; its two runs agree with each other exactly (score 0.000000). The `ref_metal` of these runs was partial (built before N6 and N7 merged, see Commands); this is the 3D difference, not a readback difference (see below) |
| all three | channel and row order | on every frame the as-is difference is the smallest of the three tests by a factor of 4 or more (for example `world` frame 11: as is 0.00000, flipped 0.03992, red and blue swapped 0.03259; `hud2d` frame 0: 0.00000, 0.00547, 0.33025), so no flip and no channel swap |
| all three | not black, not gamma shifted | every frame has all three channels spread over 0 to 255 or the scene's maximum (`world` frame 0: mean 27.8 20.9 14.0, maximum 211 203 203, equal to the byte on the two GL renderers; `ref_metal` has windowed harness results only here, its fullscreen harness result is in finding 1) |
| `screenshot`, windowed, 3D then menu | `ref_gl1`, shim, `ref_metal` | 640x480, not black, upright, `rgb` in order; `ref_gl1` run against run: 0.00000 to 0.00079 for map and menu together (menu alone 0.00008 to 0.00073; the map scene is live: items and monsters move, so this is scene noise); shim against `ref_gl1` 0.00028 to 0.00824 (two of three first pictures were brighter, mean 27.3 and 26.5 against 24.2, a screen flash present in some runs); `ref_metal` against `ref_gl1` 0.00765 to 0.00925 (map) and 0.00152 to 0.00172 (menu), mean 21.4 against 24.2 for the map, consistent every run (the `ref_metal` of these runs, built before N6 and N7 merged, draws no entities, view weapon, sky or translucent surfaces, which `ref_gl1` draws in this scene; particles (N9) and dynamic light (N8) were not drawn by any `ref_metal` then; TASK91 (#258) has since drawn dynamic light, sprites, beams, shadows and flash blobs, and TASK92 (#286) has since drawn the particles (N9); not a readback fault) |
| `screenshot`, fullscreen | `ref_gl1` and shim | 640x480; same statistics as windowed; run-against-run and renderer-against-renderer differences 0.00000 to 0.00140 (the split per renderer was not kept) |
| `screenshot`, fullscreen | `ref_metal` | **960x540**, see finding 1 |
| scaled drawable | all three | On macOS `vid_highdpiaware` is not honoured: `Glimp_DetermineHighDPISupport` (`src/client/vid/glimp_sdl3.c`) clears `SDL_WINDOW_HIGH_PIXEL_DENSITY` and sets it for the `wayland` driver only, so `vid_highdpiaware 0` and `1` run the same path and write the same 640x480 picture windowed (the written picture size, not the log's `Drawable size:` line, which prints the requested size). A Retina-scaled drawable cannot be produced with the engine as built; the case is **not run** for that reason |

Fullscreen with `r_mode -1 640x480` does not get that mode on this display:
`User requested 640x480@-1.000000, setting closest mode 960x540@60.000000` (the engine prints both rates with six decimals) for every
renderer (a display mode change, not a window).

## Findings

1. **`ref_metal` fullscreen picture size.** With `vid_fullscreen 1` the
   display mode is 960x540 while the engine's `viddef` and the log's
   `Drawable size:` stay 640x480. The harness capture (one fullscreen run) and
   the `screenshot` command (three fullscreen runs) of `ref_metal` are 960x540
   (whether the runs were identical to each other was not kept), while `ref_gl1` and the shim write 640x480 and the harness
   refuses nothing. A pin recorded fullscreen from `ref_metal` therefore does
   not compare with one from the GL renderers; the comparator rejects the pair
   by size. The 960x540 picture is not a crop of the 640x480 one (the
   best of four crop offsets still scores 0.0325 against a windowed frame, a
   frame against the next frame is 0.03); scaling is not confirmed by
   measurement. The source shows where the sizes come from: `ref_metal` sizes
   its target from `SDL_GetWindowSizeInPixels` (`RI_GetDrawableSize`,
   `src/client/refresh/metal/rm_sdl.c`, used at the frame start in
   `rm_main.c`), `R_ScreenShot` writes that target size (`rm_misc.c` through
   `rmb_target_size` in `rmb_encode.m`), and 2D is laid out over
   `vid.width` x `vid.height` (`rm_draw.c`), which stay 640x480. Owner of the file: the `ref_metal`
   screenshot path in `src/client/refresh/metal/` (a card should read the
   drawable size, or the view size, at the readback and say which is meant).
2. **A `wait` pending while a demo connects aborts the engine.**
   The harness launch, the line
   `PINS_DRYRUN=1 scripts/pins.sh record gl1 hud2d` prints (`pins_capture`
   set, demo `demo1.dm2`), with `+exec x.cfg` added and `x.cfg` holding one
   or more `wait` lines, ends with `Error: CM_InlineModel: bad number` (exit
   1, no frame written), for `ref_gl1` and every wait count tried (20 to 400,
   about ten runs); the same with `x.cfg` holding only `echo hi` runs the
   whole demo. The abort is fatal only under the pin harness (an error drop
   becomes fatal when a pins capture or replay is running,
   `src/common/clientserver.c`, `CL_PinsRun`); on its own the bare
   `quake2 +demomap demo1.dm2 +exec x.cfg` drops to the console instead of
   exiting. The cause was not found. Two leads from the source: the
   `ss_game` branch of `SV_Map` ends with `Cbuf_CopyToDefer()`
   (`src/server/sv_init.c`) and the `.dm2` branch does not, which is the
   visible difference between a `map base1` script that worked and the
   `demomap` one that aborted; and `Cbuf_Execute` returns at once while a
   `wait` is at most 16 ms old (`src/common/cmdparser.c`), so the
   `Cbuf_Execute()` "make sure any stuffed commands are done" in the
   `svc_serverdata` case (`src/client/cl_parse.c`) does not do that with a
   `wait` pending. Consequence for
   this card: a `screenshot` could not be queued at a chosen frame of a
   playing demo, which is how the screenshot and the harness capture of the
   same frame would be compared.
3. **Screenshot against capture of the same frame: not measured.** Because of
   finding 2 the two paths were compared through everything that does not need
   the same frame: both run the same `screenshot tga` command (the harness runs
   it inside `SCR_UpdateScreen` before `R_EndFrame`), both gave
   not-black, upright, `rgb` pictures of the same size on all three renderers
   windowed, and the command's menu pictures are stable run against run
   (0.00008 to 0.00073 on `ref_gl1`) and across renderers (0.00152 to
   0.00172, `ref_metal` against `ref_gl1`). The harness only captures a
   playing demo and never the menu, so no harness capture of the menu exists
   and this is not a harness-against-command number. A same-frame test remains open (a
   script that does not use `wait` after `demomap`, or a fix for finding 2).
4. **Windowed against fullscreen on the GL renderers** differ by at most
   0.000002 on `ref_gl1` (largest channel difference 42); for the shim the
   section has only its fullscreen against `ref_gl1` in the same range as
   windowed (largest difference 36), its own windowed against fullscreen
   figure was not kept. So fullscreen readback on `ref_gl1` is within
   0.000002 of windowed (a few pixels), not identical, and for the shim it
   was not measured.

Tracking: finding 1 is FIX45 (#230), finding 2 is FIX46 (#231). The
same-frame screenshot-against-capture comparison and the scaled drawable stay
open on TASK102 (#234).

## Not run

The Retina-scaled drawable (see the table), a window covered by another
window or on a sleeping display (the screen saver came on during
the windowed `hud2d` and `world` runs and every frame was still non-black and
identical to the earlier runs), and the 2D case in the same frame as a
harness capture.

## Commands

`scripts/pins.sh record gl1 hud2d --run 90` with `PINS_DRYRUN=1` printed the
engine line (kept out of the pin store); the runs were that line with
`vid_renderer gl1|metal`, `vid_fullscreen 0|1` and a private `pins_dir`, from
copies of `release/` built from main at `5f48807b` or earlier (before N6,
TASK89, and N7, TASK90, merged; the `ref_metal` rows describe that renderer,
not a later one) with `make`, `make ref_gl1 WITH_METAL_SHIM=yes` and
`make ref_metal`. Frames were compared with `framediff <a> <b>` for each
of the 23 frames and deleted afterwards.
