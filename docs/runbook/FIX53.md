# The opaque layer of `ref_metal` and the shim (FIX53)

FIX52 (#239) found that an opaque `CAMetalLayer` of about 1720x700 points
or more is shown once per display refresh whatever `displaySyncEnabled`
says, and that only `opaque = NO` lifts it (section "The 60 fps cap at
1920x1080 windowed (FIX52)"). FIX53 (#250) applies it to `ref_metal` and the
Metal-backed `gl1`.

**The change.** The layer's `opaque` follows display sync: `NO` with
`r_vsync 0`, `YES` with `r_vsync 1`, set where `displaySyncEnabled` is set,
at creation and at the runtime toggle (`rmb_attach` and `rmb_set_vsync` in
`src/client/refresh/metal/rmb_device.m`; `mgl_layer_create` and
`mgl_layer_set_sync` in `src/client/refresh/gl1/metal/mgl_layer.m`). No pure
helper: the rule is one assignment from the value beside it, nothing a host
test could hold that the readback below does not. The alpha precondition
needed a change (below): `rmb_frame_end` in
`src/client/refresh/metal/rmb_encode.m` (after the present blit) and
`mgl_layer_present` in `mgl_layer.m` (before the present) run a render pass
that writes alpha 1, and only alpha (colour write mask alpha, a
three-vertex triangle), over the drawable while the layer is not opaque.
Its two-function shader is in the shader libraries
(`rm_vertex_alpha` and `rm_fragment_alpha` in `rm_shaders.metal`,
`mgl_vertex_alpha` and `mgl_fragment_alpha` in `shaders.metal`), and its
pipeline is built with the other pipelines at context creation
(`rmb_resources_open` in `rmb_resource.m`; `mgl_layer_create` in the shim),
counted in `rmb_counts.pipelines` and the ledger only when it exists, and
never rebuilt. If it is nil (the build failed), a frame with a layer that is
not opaque is presented without the pass and one line says so (latched; the
latch is reset on release). `ref_metal` has 25 pipelines and the teardown line
reads `state 46/46`. With `r_vsync 1` the layer is opaque, ignores alpha,
and the pass does not run.

**The drawable's alpha was not 1.** A temporary readback (a blit of the
drawable into a shared buffer in the frame's command buffer, counted in its
completion handler, printed per frame; removed before the commit) counted
drawable pixels with alpha other than 255 at 640x480, `timedemo`
`demo1.dm2`, per scenario of `scripts/pins/scenarios/` plus the main menu
(`+menu_main`), the console (`+toggleconsole`), the console over the demo and
the cinematic (`idlog.cin`), 11 s each, one run per cell. The "before" rows
ran on main at `22b786cd` (this branch's base before its rebase onto
`33845c6c`, which adds a rules document only) plus the readback; the "after"
rows on the earlier form of the fix plus the readback (see the end of this
paragraph). Before the fix, in both renderers, on the
frames of the demo (689 of them) up to 305,449 of 307,200 pixels (304,488 to
305,449 at most across the demo scenarios) had alpha below 1 in every frame
of every demo scenario (not traced to a draw: the SrcAlpha, OneMinusSrcAlpha
blend factors of the blended draws are applied to alpha too, which cannot
keep it at 1); the menu had up to 273,358 in every frame, the console
304,530 in 274 to 275 of its 711 to 712 frames, the cinematic 304,530 in
its first frame. `ref_metal` and the shim agree to the pixel on `world` and
`skywater` (208,327,326 pixels over 689 frames each), as they draw the
same alpha. After the fix, with `r_vsync 0`:

| Scenario | Frames read, `ref_metal` / shim | Pixels with alpha below 1 |
|---|---|---|
| `world`, `hud2d`, `particles`, `models`, `dlights`, `skywater` | 2,448 to 2,610 | 0 |
| main menu, console, console over the demo | 2,368 to 2,610 | 0 |
| cinematic | 2,608 | 0 |

Every cell is 0 in both renderers (20 runs of about 2,600 frames; per-run lines are
kept outside the repository). The readback code is not in the tree: it
changes the frame. It ran on an earlier form of this change (the pass's shader
compiled from source on first use), built as `945b712f` on `22b786cd`. Its
source change is the one in this branch's commit "FIX53: the Metal layer is
not opaque when display sync is off, and the drawable's alpha is forced to 1
then" (`58ede1ab` when measured; see the measured build below for its name
now). The pass was since moved into the shader libraries
and its pipeline into context creation (above); the shader and the pipeline
state are the same, and the composited captures below, taken on the "after"
build, are what cover the pass as it is now.

**The measured build.** Before is main at `33845c6c`, built in a scratch
worktree. After is this branch's commit "FIX53: build the alpha pass
pipeline at init, count it, latch a failure", which was `68edb493` on
`33845c6c` when measured; every later commit of this branch changes only
documents. Commit ids below that name a FIX53 commit (`68edb493`,
`58ede1ab`) are those measured ids. The branch was rebased twice after the
review, so the ids changed while the change did not:

- onto `9039f602` (TASK129: how a sky face's image is chosen, in
  `src/client/refresh/files/image.c`, and its tests): there the two commits
  are `f42611e1` and `7d5b06d5`;
- onto `a6f8532e` (TASK111, the MetalFX scalers): there they are `a25bbd0d`
  and `c1b075f3`. TASK111 changes 12 files under `src/client/refresh/metal`
  (`git diff --stat 9039f602 a6f8532e -- src`): five new MetalFX files,
  `rmb_frame_cut` and an `rmb_fx_release` call in `rmb_encode.m`, a per-frame
  `RM_FX_EndFrame` in `rm_main.c`, and small edits to `rmb_gbuf.m`,
  `rm_gbufscene.c`, `rm_model.c`, `rm_pure.c` (the ledger line's new
  `metalfx` kind) and `rmb.h`. None of them is in `rmb_frame_end`'s present
  blit, the layer or the vsync path; the scalers are off by default and
  these runs used none.

The FIX53 source change itself is the same at every step: the added and
removed lines of `git diff 33845c6c 68edb493 -- src` and of
`git diff a6f8532e a25bbd0d -- src` are identical. A squash merge gives the
change one more id; its subject lines identify it. Scratch builds, never committed:
the pass disabled (the two `*_force_alpha` calls commented out at `68edb493`),
and a probe build (`68edb493` plus `ScreenCaptureKit` captures, GPU
timestamps in the shim and environment overrides of the layer's `opaque` and
of the pass).

**Before and after** (`timedemo` of `demo1.dm2`, FIX52's launch line: a pin
run with `pins_capture 1000000` that quits by itself at the demo's end, plus
`+set timedemo 1 +demomap demo1.dm2`, `-portable`, `s_initsound 0`,
`vid_fullscreen 0`, `r_mode -1`, the size in `r_customwidth` and
`r_customheight`, `vid_highdpiaware 0`, `pins_seed 1`; the `fps` line of the
engine; three runs per cell, before and after alternated run by run). The
timing lock was held for every batch, and before each run the screen was
unlocked and no other `quake2` was running. The quiet-window table, per batch
(`uptime` one-minute load before the first and after the last run; processes
among `make`, `cc`, `clang` and `quake2` found with `pgrep -x`; GPU "Device
Utilization %" from `ioreg` before and after, the second reading right after
the last engine exited). The host was never as quiet as FIX52's: the load
decayed from other lanes' builds and stayed above 1.8. No batch had a
listed process before or after, except one `clang` after the batch marked:

| Batch | Load before, after | Processes | GPU % before, after |
|---|---|---|---|
| `ref_metal` 640x480 `r_vsync 0` | 2.09, 2.36 | none | 6, 13 |
| `ref_metal` 640x480 `r_vsync 1` | 2.36, 2.10 | none | 0, 0 |
| `ref_metal` 1920x1080 `r_vsync 0` | 2.10, 1.87 | none | 0, 55 |
| `ref_metal` 1920x1080 `r_vsync 1` | 1.87, 1.81 | none | 0, 20 |
| `gl1` 640x480 `r_vsync 0` | 1.81, 1.96 | none | 0, 12 |
| `gl1` 640x480 `r_vsync 1` | 1.96, 2.16 | none | 0, 14 |
| `gl1` 1920x1080 `r_vsync 0` | 2.16, 3.07 | none | 0, 53 |
| `gl1` 1920x1080 `r_vsync 1` | 3.07, 3.49 | `clang` after | 18, 15 |
| 5120x2880 fullscreen, `ref_metal` | 3.25, 3.55 | none | 0, 78 |
| 5120x2880 fullscreen, `gl1` | 3.55, 3.65 | none | 0, 87 |

Windowed, fps:

| Renderer | Size | `r_vsync` | Before (fps) | After (fps) |
|---|---|---|---|---|
| `ref_metal` | 1920x1080 | 0 | 60.0 | 330.8 to 346.9 |
| `ref_metal` | 1920x1080 | 1 | 60.0 | 60.0 |
| `ref_metal` | 640x480 | 0 | 120.0 | 120.0 to 120.1 |
| `ref_metal` | 640x480 | 1 | 120.0 to 120.1 | 120.0 to 120.1 |
| Metal-backed `gl1` | 1920x1080 | 0 | 60.1 | 238.2 to 287.1 |
| Metal-backed `gl1` | 1920x1080 | 1 | 60.0 to 60.1 | 60.1 |
| Metal-backed `gl1` | 640x480 | 0 | 120.1 to 120.2 | 120.2 |
| Metal-backed `gl1` | 640x480 | 1 | 120.2 | 120.1 to 120.2 |

At 1920x1080 the cap is gone with `r_vsync 0` and `r_vsync 1` stays at 60.

**The 640x480 ceiling.** With FIX52's own launch line, 640x480 still reads
about 120 fps (120.0 to 120.2) in both renderers, with `r_vsync 0` and with
`r_vsync 1`, on the build before this change as well as after it. The ceiling
is therefore there before the change and is not the opaque layer. FIX52's
329.7 to 348.9 at 640x480 on the same host is not reproduced by this launch
line on either build, and why is not explained here: FIX51 (#238) carries
the ceiling. The "no loss at 640x480" criterion is judged on what is
readable: the pass's GPU time per frame at 1920x1080 and 5120x2880, below.
No 640x480 row was taken.

**Fullscreen, 5120x2880, `r_vsync 0`** (fps; the log says `Setting mode -1:
5120x2880 (vid_fullscreen 1)` and `Drawable size: 5120x2880`):

| Renderer | Before | After |
|---|---|---|
| `ref_metal` | 360.0, 339.4, 438.3 | 394.2, 386.4, 400.3 |
| Metal-backed `gl1` | 319.9, 368.1, 468.1 | 378.4, 370.6, 385.8 |

No loss that these runs separate from their own spread.

**The alpha pass's own cost.** At 1920x1080 windowed and at 5120x2880
fullscreen with `r_vsync 0` the fps is bounded by the wait for the drawable
(about 240 to 440), so command-buffer GPU timestamps were used: `ref_metal`'s
own `MTL_GPU_TIME` line (the mean over command buffers; 1,724 buffers for 689
frames, so about 2.5 buffers per frame, and the pass is in one of them), and
the `gpu_ms` of the frame's command buffer printed by a probe in the shim (one
per frame). Mean GPU milliseconds, pass disabled against pass on, three runs
alternated, the timing lock held and no `make`, `cc`, `clang` or `quake2`
process after each row (the one-minute load at the end of each row, 5.2 to
5.9, includes the batch's own runs and the builds before it):

| Renderer, size | Pass off | Pass on | Difference |
|---|---|---|---|
| `ref_metal` 1920x1080 | 0.334, 0.398, 0.432 | 0.532, 0.558, 0.520 | about 0.15 per buffer |
| `ref_metal` 5120x2880 | 0.728, 0.708, 0.640 | 0.891, 0.890, 0.932 | about 0.2 per buffer |
| `gl1` 1920x1080 | 1.293, 1.106, 1.179 | 1.644, 1.522, 1.581 | about 0.4 per frame |
| `gl1` 5120x2880 | 2.284, 2.310, 2.036 | 4.088, 3.944, 3.928 | about 1.8 per frame |

The pass costs about 0.4 ms of GPU time a frame in the shim at 1920x1080 and
about 1.8 ms at 5120x2880, and about 0.4 to 0.5 ms a frame in `ref_metal` (the
per-buffer difference times its buffers per frame). That is under the 8.3 ms
of the 120 fps ceiling at 640x480, so it cannot be read in fps there. The fps
of the same pass-off and pass-on builds overlaps within the spread of the
runs for `ref_metal` (1920x1080 windowed 296.1 to 310.6 off, 249.5 to 318.2
on; fullscreen 379.2 to 382.6 off, 368.3 to 432.2 on); the shim reads lower
with the pass on (1920x1080 259.8 to 313.2 off, 236.8 to 263.8 on;
fullscreen 432.5 to 486.2 off, 371.4 to 374.9 on).

**The runtime toggle.** One session each, 1920x1080 windowed, started with
`r_vsync 1`, `+toggleconsole +exec` a cfg that sets `r_vsync 0`, waits about
1000 frames, sets `r_vsync 1`, waits 400, sets `r_vsync 0`, waits 1000 more
(no `vid_restart`; the cfg is run with no demo loading). A temporary print
once a second of the layer's `opaque`, `displaySyncEnabled` and the present
rate (removed before the commit). These runs used the same earlier form as the
readback above (`945b712f` on `22b786cd`, source equal to this branch's
`58ede1ab`) plus the print; `rmb_set_vsync` and `mgl_layer_set_sync` have not
changed since:

| Phase | `opaque` / `displaySync` | `ref_metal` (fps, per second) | Metal-backed `gl1` |
|---|---|---|---|
| `r_vsync 0` | 0 / 0 | 205.0 to 272.8 (35 s) | 238.9 to 283.6 (35 s) |
| `r_vsync 1` | 1 / 1 | 60.0 (9 s) | 60.0 (8 s) |
| `r_vsync 0` | 0 / 0 | 267.7 to 284.9 (12 s) | 242.2 to 268.6 (13 s) |

The first second after each switch is left out (it straddles the change);
both flips took effect at once, in both directions.

**Pins.** All six scenarios of the table above except the cinematic
(`world`, `hud2d`, `particles`, `models`, `dlights`, `skywater`; `PINS_SEED=1`;
the default 640x480, `r_vsync` as the harness leaves it) were recorded for
`metal` and `gl1-metal` with the renderer modules of main (`33845c6c`) and of
this branch (`68edb493`) under one engine executable, and compared at
`--threshold 0 --bad-threshold 0`: 12 comparisons, 23 frames each, 0 failed,
worst score 0, worst bad 0. A pin captures the offscreen target and runs at
the harness's default sync, where the new pass does not run; the composited
captures below are what cover the pass.

**Validation.** One `ref_metal` run under `MTL_DEBUG_LAYER=1` at 1920x1080
with `r_vsync 0` (the pass running), on `68edb493`: 689 frames at 340.8 fps,
`Metal API Validation Enabled`, no validation message; teardown `0 live
objects (... state 46/46 ...), 0 unreleased, 0 unmatched`.

**The pipeline that does not build.** Run with the metallib of main (which has
no alpha functions) beside the new dylibs, at 1920x1080 `r_vsync 0`: each
renderer prints its one line (`no alpha pass (its pipeline was not built)`)
once, the 689 frames run, and `ref_metal`'s teardown reads `state 45/45`, 0
unreleased, 0 unmatched. There is no retry and no message per frame.

**The composited window, captured from inside the engine.** `screencapture`
fails here without the Screen Recording permission (`could not create image
from window`, `could not create image from display`), and no setting was
changed. A scratch probe build (never committed, linked with
`ScreenCaptureKit` in that build only) calls
`+[SCShareableContent getCurrentProcessShareableContentWithCompletionHandler:]`
(macOS 14.4 and later) and `SCScreenshotManager` on the engine's own window
after frame 300 of `timedemo demo1.dm2` (half a second after the present, so
the compositor shows that frame), 960x540 windowed, `r_vsync 0` unless said
otherwise, and writes the 960x572 pixels (title bar included) to a raw file.
It needed no permission and asked for none: the call listed the process's
windows and returned the image. The captures stay outside the repository.
The diff is a pixel by pixel comparison of the four channels (a small C
program: differing pixels, largest channel difference):

| Comparison, same frame | `ref_metal` | Metal-backed `gl1` |
|---|---|---|
| opaque against opaque (two runs) | 0 differing, max 0 | 0 differing, max 0 |
| not opaque against not opaque (two runs) | 0, max 0 | 0, max 0 |
| **opaque against not opaque** (`layer.opaque` forced by environment, `r_vsync 0`, the pass runs when not opaque) | **0 differing, max 0** | **0 differing, max 0** |
| `r_vsync 1`, opaque forced NO against `r_vsync 1` opaque | 0, max 0 | 0, max 0 |
| negative control: not opaque with the pass disabled, against opaque | 255,332 differing, max 9 | 255,332 differing, max 9 |
| `r_vsync 1` against `r_vsync 0`, both opaque (not a layer difference) | 516,880 differing, max 196 | 516,880 differing, max 196 |

So with the pass the composited window is pixel-identical whether the layer is
opaque or not, in both renderers, and the same capture does see the
difference without the pass (the negative control). The last row is a
different demo frame (what `r_vsync` changes in the frames the demo plays at
the same call index), unrelated to the layer: opaque against not opaque at
the same `r_vsync` is 0 in both. `soft` was not captured: its layer is
set by `sw_main.c` (C, no Objective-C in the file), so the probe does not
fit there without more than a scratch edit; the `soft` window comparison is
not made. `soft`'s layer change is FIX52's, not this card's; its capture is
TASK132 (#319).

**For FIX51 (#238).** At 1920x1080 with the layer not opaque `ref_metal`
runs 330.8 to 346.9 fps (2.9 to 3.0 ms a frame) and the shim 238.2 to 287.1
(3.5 to 4.2 ms) in the timedemo; 5120x2880 fullscreen reads 386 to 400
(`ref_metal`) and 371 to 386 (the shim).
The 3 to 4 ms of fixed cost is not the opaque layer. At 640x480 the 120 fps
ceiling is present with FIX52's launch line on the build before this change
too, so it is not the layer and not lifted by it. The alpha pass costs about
0.4 ms of GPU time a frame in the shim at 1920x1080 and 1.8 ms at 5120x2880
(`ref_metal` about 0.4 to 0.5 ms a frame); the fps there is capped by the
drawable wait, so only the GPU time shows it.
