# HighDPI windows on the bare-metal host (TASK118)

From TASK118 the window code asks for `SDL_WINDOW_HIGH_PIXEL_DENSITY` on the `cocoa` and
`uikit` drivers at `vid_highdpiaware 1` (the default), and the pin harness passes `+set vid_highdpiaware 0` unless `PINS_HIDPI=1`.
The harness records the value in `settings.txt` (`record`) and `replay.txt` (`replay`), as
`doc/100_renderpins.md` says: the card's "manifest" is these two files, because `manifest.txt` is
the engine's frame list, which the harness counts against the frames. Engine:
`bdda9812` (no file under `src` or the Makefile changes after it on the branch; the pin ids read
`bdda98128905`); base: `3103b1f3` (`main`; `git diff c01bd7d2 3103b1f3 -- src Makefile` is empty),
built from `git archive` in a scratch directory with `make client game ref_soft ref_metal` and
`make ref_gl1 WITH_METAL_SHIM=yes`, and run as `PINS_BIN=<scratch>/release/quake2` (pin ids
`bdda98128905-b2705498477-*`). The display: 5120x2880 pixels, 2560x1440 points (the client's
`Real display mode: 2560x1440@60.00`), 60 Hz. Every batch with the screen unlocked
(`CGSSessionScreenIsLocked` absent) and no other `quake2` running; one-minute load 2.1 to 3.8.
Scripts, logs, screenshots and the measurement outputs are in `<artifacts>/TASK118/`, the pins in
`<pin store>`.

*Pins with `PINS_HIDPI` unset.* Every scenario recorded at the base and at the card for `metal`,
`gl1-metal` and `soft` (`PINS_DEMO=idlog.cin PINS_EVERY=7` for `cinematic`, `PINS_SEED=1` for
`models`, `dlights` and `particles`, `demo1.dm2` for the rest), then `pins.sh compare <r> <s> --ref
<r> --ref-id <base> --cand-id <card> --threshold 0 --bad-threshold 0`:

| Renderer | `world`, `world-fullbright`, `hud2d`, `skywater`, `models`, `dlights`, `particles` | `cinematic` |
|---|---|---|
| `metal` | 23 of 23 each, `worst=0 worst_bad=0`, byte-identical | 12 of 12, 0, 0, byte-identical |
| `gl1-metal` | 23 of 23 each, `worst=0 worst_bad=0`, byte-identical | 12 of 12, 0, 0, byte-identical |
| `soft` | 23 of 23 each, `worst=0 worst_bad=0`, byte-identical | 12 of 12, 0, 0, byte-identical |

Every frame is 640x480 (`manifest.txt`), every record's `settings.txt` reads `hidpi 0`, and the
pin names are the ones made before `PINS_HIDPI` existed (`world` `-2420249005`, `hud2d`
`-506029871`, `cinematic` `-2483540229`, `world-fullbright` `-2060017375`, the same hashes as the
older records in the store).

*Sizes.* A console script beside the binary (deleted after): `developer 1`, `map base1`, 120
`wait`s, `screenshot`, 30 `wait`s, `quit`; run as `./quake2 -datadir <game data> -portable +set
vid_renderer R +set vid_fullscreen 0 +set r_mode 4 +set s_initsound 0 +set vid_highdpiaware V +exec
<script>`; the size from the TGA header (bytes 12 to 15). The harness captures: `PINS_HIDPI=1
pins.sh record R hud2d` and `world`, against the records above:

| Renderer | `vid_highdpiaware 1`: `Drawable size`, screenshot, capture | `vid_highdpiaware 0`: `Drawable size`, screenshot, capture |
|---|---|---|
| `metal` | 1280x960, 1280x960, 23 frames 1280x960 each | 640x480, 640x480, 23 frames 640x480 each |
| `gl1-metal` | 1280x960, 1280x960, 23 frames 1280x960 each | 640x480, 640x480, 23 frames 640x480 each |
| `soft` | 1280x960, 1280x960, 23 frames 1280x960 each | 640x480, 640x480, 23 frames 640x480 each |

Every run: status 0, `Setting mode 4: 640x480 (vid_fullscreen 0)`; the capture's `engine.log` has
the same `Drawable size` line and its `settings.txt` reads `hidpi 1`.

*The 2D scale.* `SCR_GetDefaultScale` (`cl_screen.c`) is `min(width / 640, height / 240)` of the
client's size, 2 at 1280x960; the unit test `hidpi_2d_scale_follows_the_size_taken` runs the real
`SCR_GetHUDScale`, `SCR_GetConsoleScale` and `SCR_GetMenuScale` on that size and gets 2 (1 at
640x480). On the frames, `scale2d.py` (in the artifacts) takes the bounding box of every pixel that
is not the clear colour in each `hud2d` frame (world off, only 2D drawn): on all 23 frames of each
renderer every edge of the box at `PINS_HIDPI=1` is exactly twice the edge at `PINS_HIDPI` unset
(middle frame: (356, 474, 960, 960) against (178, 237, 480, 480)), and the 1280x960 frame
box-filtered down by 2 differs from the 640x480 one by a mean of at most 0.000363 (`metal`,
`gl1-metal`) and 0 (`soft`).

*The mouse.* The engine draws no mouse pointer: menus are driven by keys, and mouse buttons act as
Enter (`Key_GetMenuKey`, `menu.c`). SDL reports mouse motion in points at both values: in relative
mode the Cocoa driver sends `[event deltaX]`/`[event deltaY]` unscaled (`Cocoa_HandleMouseEvent`,
`SDL_cocoamouse.m`; `SDL_mouse.c` scales it only by the relative-speed and system-scale hints,
neither of which the engine sets), and outside it positions are window coordinates, which are
points. The client adds the motion only in the game (`SDL_EVENT_MOUSE_MOTION` in
`input/sdl3.c`, `key_dest == key_game`) and turns the view by its count (`IN_Move`), which no size
enters; with a menu or the console open in a window the mouse is released (`windowed_mouse 0`,
`CL_UpdateWindowedMouse`; `in_grab 2`) and the system pointer moves in points. So the same mouse
travel crosses the window, and turns the view, by the same amount at both values, and no input
code changed. The card's menu cursor line is therefore closed by analysis of that code path, not
by a measurement: the menu cursor is the selection marker that keys move (`M_DrawCursor`,
`menu.c`), and `input/sdl3.c` counts mouse motion only while `cls.key_dest == key_game`. No
physical mouse was moved, no input was synthesized, and no unit test stands in for it (the one the
first round called the measurement modelled the drawable itself and could not fail; rework 1
removed it).

*Restarts under `ASAN=1`.* `make ASAN=1 client game ref_metal ref_soft` and `make ASAN=1 ref_gl1
WITH_METAL_SHIM=yes`; a console script (deleted after): `developer 1`, `map base1`, 120 `wait`s,
`screenshot`, then N cycles of `set vid_highdpiaware V` (0, 1, 0, ...), `vid_restart`, 120
`wait`s, `screenshot`, then `quit`, started at `vid_highdpiaware 1` in a 640x480 window:

| Renderer, cycles | Status | Screenshots and `Drawable size` lines | Sanitizer lines | `ref_metal` teardowns |
|---|---|---|---|---|
| `metal`, 10 | 0 | 11, alternating 1280x960 and 640x480 | 0 | 11, each `0 live objects ... 0 unreleased, 0 unmatched` |
| `soft`, 4 | 0 | 5, alternating 1280x960 and 640x480 | 0 | none (not loaded) |
| `soft`, 10 (rework 1, engine `d7aa5540`) | 0 | 11, alternating 1280x960 and 640x480; the 10 `RE_InitContext: buffer` lines alternate 640x480 and 1280x960 with them | 0 | none (not loaded) |
| `gl1-metal`, 4 | 0 | 5, alternating 1280x960 and 640x480 | 0 | none (not loaded) |

*The fullscreen window* (not an acceptance line of the card), `vid_fullscreen 2`,
`vid_highdpiaware 1`, `ASAN=1`, the size script: at `r_mode -2` every renderer printed `Setting
mode -2: 2560x1440` and `Drawable size: 5120x2880` and its screenshot is 5120x2880, the native
size; at `r_mode 4` `metal` and `gl1-metal` keep 640x480 (drawable line and screenshot) and `soft`
853x480, which the base's `soft` prints and writes too. The soft renderer's buffer takes the
drawable only where the client's `viddef` does (rework 1 below: both call
`GLimp_DrawableInPixels` on the `fullscreen` value `SWimp_SetMode` passes): before TASK118, a
HighDPI fullscreen window at any mode but `-2` got a buffer of the window's pixels under a `vid`
of the mode's size. Status 0 and no sanitizer line in every run.

*Rework 1: the soft buffer and the client's size from one rule.* The first round sized the `soft`
buffer (`RE_InitContext`) and `vid` (the tail of `SWimp_SetMode`) from the `vid_fullscreen` cvar,
while the client sizes `viddef` (`GLimp_InitGraphics`) from the `fullscreen` value `SWimp_SetMode`
passes. Two inputs set them apart, both archived cvars a server can also send with `stufftext`:
`vid_fullscreen 2` with `r_mode 99` (the mode is invalid, `RE_SetMode` retries `r_mode 4` with
fullscreen 0 while the cvar stays 2: a HighDPI 640x480-point window), and `vid_fullscreen 2.5`
(passed as 2, compared as 2.5). Since rework 1 `soft` calls the client's own function,
`GLimp_DrawableInPixels` (`glimp_hidpi.c`, now linked into `ref_soft` as well), on the value it
passed (`SWimp_DrawableInPixels`), and `RE_RenderFrame` clamps `r_newrefdef`'s rectangle to
`vid_buffer_width` x `vid_buffer_height` before anything draws (`R_ClampViewToBuffer`), a no-op
whenever the two sizes agree. The window's `SDL_WINDOW_FULLSCREEN` flag cannot stand in for the
value: a first attempt read it, and on this host a window made fullscreen does not report it yet
when `RE_InitContext` runs (`vid_fullscreen 2.5` then got a 5120x2880 buffer under a 5333x3000
`viddef` again, held inside the buffer by the clamp). The one case left where the client decides on
another value is its own fallback when SDL cannot make a `vid_fullscreen 2` window at all (it then
makes a 640x480 window with fullscreen 0); the clamp was meant to cover it, and rework 2 below
shows it does not. Every run below: `make ASAN=1
client game ref_soft` (plus `ref_metal`, and `ref_gl1 WITH_METAL_SHIM=yes`), `vid_highdpiaware 1`,
`+set developer 1` on the command line, `map base1`, 120 `wait`s, `screenshot`, 30 `wait`s, `quit`; screen
unlocked, no other `quake2`; `RE_InitContext: buffer WxH` is a `developer 1` line rework 1 added.
The base is `d7e230a4` (the first round's head) from `git archive`, with only that one print added:

| Build, input | Status | `Drawable size` | `soft` buffer | Screenshot | Sanitizer |
|---|---|---|---|---|---|
| base, `vid_fullscreen 2`, `r_mode 99` | 134 | 1280x960 | 640x480 | none | `heap-buffer-overflow`, `WRITE of size 7680 ... 0 bytes after 5120-byte region`, in `R_BeginEdgeFrame sw_edge.c:99` (`newedges`, one entry per buffer column, cleared for 960 rows), allocated in `SWimp_CreateRender` |
| base, `vid_fullscreen 2.5`, `r_mode -1` at 2560x3000 | 134 | 5333x3000 | 5120x2880 | none | `SEGV on unknown address ... caused by a WRITE memory access` in `D_DrawSpan sw_scan.c:520`, from `D_DrawSpansPow2` (`ASAN_OPTIONS=allow_user_segv_handler=0`; with the engine's own handler the run dies on signal 11 in `D_DrawSpansPow2`) |
| head `d7aa5540`, the same two | 0, 0 | 1280x960, 5333x3000 | 1280x960, 5333x3000 | 1280x960, 5333x3000 | none |
| head without the clamp | 0, 0 | 1280x960, 5333x3000 | 1280x960, 5333x3000 | 1280x960, 5333x3000 | none |
| the clamp, the decision reverted to the cvar | 0, 0 | 1280x960, 5333x3000 | 640x480, 5120x2880 | 640x480, 5120x2880 | none |

A 3000-row mode is used for the second input because only a `viddef` taller than the panel's 2880
pixels exceeds the buffer there: at `vid_fullscreen 2` `SWimp_SetMode` widens the mode to the
desktop's aspect (2560x3000 to 5333x3000), and the buffer of the 2.5 run is the drawable of the
fullscreen window, 5120x2880. ASAN reports the second as a `SEGV`, not a `heap-buffer-overflow`:
the two frames are one allocation of 5120 x 2880 x 2 = 29,491,200 bytes, exactly 1800 pages of
16 KiB, so no redzone follows it: rows past 2880 run from one frame into the other or past the
end of the allocation, where the write faults. The last row is the property the clamp alone gives: a wrong
picture (the view in the buffer's part), not a write past it.

The same two inputs on the renderers this card does not change, `MTL_DEBUG_LAYER=1` (`Metal API
Validation Enabled`, no other validation line), status 0 and no sanitizer line each:

| Renderer, input | `Drawable size` | Screenshot |
|---|---|---|
| `metal`, `vid_fullscreen 2`, `r_mode 99` | 1280x960 | 640x480 |
| `metal`, `vid_fullscreen 2.5`, 2560x3000 | 2560x3000 | 5120x2880 |
| `gl1-metal`, `vid_fullscreen 2`, `r_mode 99` | 1280x960 | 640x480 |
| `gl1-metal`, `vid_fullscreen 2.5`, 2560x3000 | 2560x3000 | 5120x2880 |

Both mismatch the client the same way, on the GPU side: `src/client/refresh/metal/rm_main.c:188`
and `src/client/refresh/gl1/gl1_main.c:1400` decide the frame size from the `vid_fullscreen` cvar,
not the value passed (left for their owner: the metal and shim trees are another card's, gl1's own
files are out of this card's scope). `metal`'s teardown stays at 0 live objects in both runs.
Rework 2 below closes this from the client's side, with no change to those files.

The re-run at engine `d7aa5540` (pin ids `d7aa55409e0d-*`; after it no file under `src` or the
Makefile changes on the branch), `PINS_HIDPI` unset, against the same base records as above:
`soft` in every scenario 23 of 23 frames byte-identical (`cinematic` 12 of 12), `worst=0
worst_bad=0`, 640x480; `metal` and `gl1-metal` `world` 23 of 23 byte-identical. The size script on
`soft`: `vid_highdpiaware 1` `Drawable size: 1280x960` and a 1280x960 screenshot, `0` 640x480 and
640x480, status 0; `PINS_HIDPI=1 pins.sh record soft world`: 23 frames of 1280x960,
`settings.txt` `hidpi 1`. Ten `vid_restart` cycles on `soft` under `ASAN=1`: the restarts table
above. Scripts, logs, screenshots and the two proof diffs are in `<artifacts>/TASK118/rework1/`
(`fallback-final.tsv`, `compare-summary.tsv`, `sizes-summary.tsv`, `restarts/asan-soft.log`).

*Rework 2: one fullscreen value per window.* Rework 1 left one case where `soft` decided on
another value than the client: when SDL cannot make the `vid_fullscreen 2` window,
`GLimp_InitGraphics` falls back to a 640x480 window with fullscreen 0 on its own (it writes
`vid_fullscreen 0` and `r_mode 4`), while `soft` kept the 2 it had passed: a 640x480 buffer under a
1280x960 `viddef`. The clamp covers only the 3D view, and `RE_Draw_CharScaled` bounded y but not
x, so a full console row drawn while disconnected went past the end of `swap_buffers` (the rework
1 runs loaded a map and never drew the full console). `metal` and `gl1-metal` read the cvar, so
they disagreed with `viddef` in the invalid-mode fallback and at 2.5 (the table above: the top-left
quarter of the picture, no HUD; filed as FIX61). The rule now: one fullscreen value per window,
the one the client uses, and every renderer decides from it. `GLimp_InitGraphics` writes its
`fullscreen` argument into `vid_fullscreen` on entry, and its own fallback writes 0 as before, so
the cvar holds the value the window is made with when `re.InitContext` runs and when the
renderer's `SetMode` returns. `soft` passes the cvar to `GLimp_DrawableInPixels`
(`SWimp_DrawableInPixels`, `sw_fullscreen` is gone); `metal` and `gl1-metal` compare it in their
own copy of the rule, unchanged. Second guard: `RE_Draw_CharScaled` clips its columns to the buffer
as `RE_Draw_Fill` clips, and a character inside the buffer is drawn exactly as before.
`R_ClampViewToBuffer` moved to `src/client/refresh/soft/sw_view.c`, which needs no SDL, and the
unit test links it (`tests/unit/test_sw_view.c`; with the clamp's body removed two of its three
tests fail).

What the write does not do, by reading the code. It starts no restart: `vid_fullscreen->modified`
is read only by `VID_CheckChanges` while `restart_state` is `RESTART_UNDEF`, every renderer's
`GetRefAPI` sets `RESTART_NO` before `re.Init` reaches `SetMode`, and `VID_CheckChanges` keeps it
there; every run below has one `----- refresher initialization -----` line, the ten restarts 11.
It does not split the window-reuse path: the early return of `GLimp_InitGraphics` (a fullscreen
window of the same size) skips both `re.InitContext` and the `viddef` code, so the buffer and
`viddef` keep the sizes of the last window made, decided on the same value; `SWimp_SetMode`'s tail
recomputes only `soft`'s `vid`, which sizes nothing but the triangle spans there, as on the base.
What `config.cfg` saves changes in two cases, documented under `vid_fullscreen` in
`doc/040_cvarlist.md`: after a renderer's invalid-mode fallback it saves `0` (before: `2`, the
next start went fullscreen with the fallback's `r_mode`), and a fraction is saved whole (`2`, not
`2.5`). After the client's own fallback it saves `0`, as before.

The client fallback needs SDL to fail, so the runs use a **test patch**, scratch only and never
committed (`<artifacts>/TASK118/rework2/test-patch.diff`): with `TASK118_FAIL_FS` set in the
environment, the first fullscreen `CreateSDLWindow` call prints `Creating window failed` and
returns false, as when `SDL_CreateWindowWithProperties` returns NULL; without it the patch does
nothing. Every run: `make ASAN=1`, `vid_highdpiaware 1`, `+set developer 1`, no map at start (the
full console, disconnected), 120 `wait`s, `screenshot`, `map base1`, 120 `wait`s, `screenshot`,
`quit`; screen unlocked, no other `quake2`. The client fallback input is `vid_fullscreen 2`,
`r_mode 4` with `TASK118_FAIL_FS=1`. The base is `2ec88921` (rework 1's head) with the test patch;
the halves are scratch copies:

| Build, client fallback, `soft` | Status | `Drawable size` | Buffer | Screenshots | Sanitizer |
|---|---|---|---|---|---|
| base `2ec88921` | 134 | 1280x960 | 640x480 | none | `heap-buffer-overflow`, `WRITE of size 1 ... 2 bytes after 614400-byte region`, in `RE_Draw_CharScaled sw_draw.c:134` from `Con_DrawConsole cl_console.c:716`, allocated in `SWimp_CreateRender` |
| head `4b492a35` | 0 | 1280x960 | 1280x960 | 1280x960, 1280x960 | none |
| the single value without the column clip | 0 | 1280x960 | 1280x960 | 1280x960, 1280x960 | none |
| the column clip, the decision as at `2ec88921` | 0 | 1280x960 | 640x480 | 640x480, 640x480 | none |

The last row is the clip alone: a wrong picture, no write past the buffer. The three renderers at
the head (the round-1 inputs from the head's own build, the client fallback from the head with the
test patch; `metal` with `MTL_DEBUG_LAYER=1`, no validation line), status 0 and no sanitizer line
in every run, and the same inputs at `2ec88921`:

| Renderer, input | `Drawable size` | Screenshots, head | Screenshots, `2ec88921` | `vid_fullscreen` saved, head (`2ec88921`) |
|---|---|---|---|---|
| `soft`, `vid_fullscreen 2`, `r_mode 99` | 1280x960 | 1280x960 | 1280x960 | `0` (`2`) |
| `soft`, `vid_fullscreen 2.5`, 2560x3000 | 5333x3000 | 5333x3000 | 5333x3000 | `2` (`2.5`) |
| `soft`, client fallback | 1280x960 | 1280x960 | overflow, above | `0` (`0`) |
| `metal`, `vid_fullscreen 2`, `r_mode 99` | 1280x960 | 1280x960 | 640x480 | `0` (`2`) |
| `metal`, `vid_fullscreen 2.5`, 2560x3000 | 2560x3000 | 2560x3000 | 5120x2880 | `2` (`2.5`) |
| `metal`, client fallback | 1280x960 | 1280x960 | not run | `0` |
| `gl1-metal`, `vid_fullscreen 2`, `r_mode 99` | 1280x960 | 1280x960 | 640x480 | `0` (`2`) |
| `gl1-metal`, `vid_fullscreen 2.5`, 2560x3000 | 2560x3000 | 2560x3000 | 5120x2880 | `2` (`2.5`) |
| `gl1-metal`, client fallback | 1280x960 | 1280x960 | not run | `0` |

Both screenshots of a run have the size given. Every row's screenshot now equals its `Drawable
size`, so `metal` and `gl1-metal` agree with the client in every row, which closes the mismatch
FIX61 was filed for. The `soft` buffer line equals the drawable in each `soft` row. The
pins at engine `4b492a35` (pin ids `4b492a35ffee-*`; after it no file under `src` or the Makefile
changes on the branch), `PINS_HIDPI` unset, against the same base records as above: `soft` in
every scenario 23 of 23 frames byte-identical (`cinematic` 12 of 12), `worst=0 worst_bad=0`,
640x480; `metal` and `gl1-metal` `world` 23 of 23 byte-identical. Ten `vid_restart` cycles on
`soft` under `ASAN=1`: status 0, no sanitizer line, 11 screenshots and `Drawable size` lines
alternating 1280x960 and 640x480, and the 10 buffer lines of the restarts equal to them.
Scripts, logs, screenshots, saved configs and diffs are in `<artifacts>/TASK118/rework2/`
(`summary.tsv`, `compare-summary.tsv`, `restarts/asan-soft.log`, `n1-probe.txt`).

| Acceptance line (TASK118) | Verdict |
|---|---|
| `PINS_HIDPI` unset: pins unchanged against the base, score 0, bad 0 | pass, 24 of 24 pairs byte-identical |
| `vid_highdpiaware 1`, 640x480 window: screenshot, capture and `Drawable size` 1280x960, 2D scale 2 | pass on all three renderers |
| `vid_highdpiaware 0`: 640x480 | pass on all three renderers |
| The menu cursor crosses the screen in the same mouse travel | closed by analysis of the code path above, not measured: no pointer is drawn, mouse motion counts only in the game, nothing to scale |
| Ten `vid_restart` cycles switching the value under `ASAN=1` | pass: status 0, 11 teardowns at 0 live objects (`metal`); `soft` 10 cycles at status 0 in rework 1 |
| (rework 1) `soft` buffer equal to the client's size in the invalid-mode fallback and at `vid_fullscreen 2.5` | pass under `ASAN=1`: the base overflows, the head and each half alone do not |
| (rework 2) one fullscreen value per window: `soft`, `metal` and `gl1-metal` take the client's size in the client's own fallback, the invalid-mode fallback and at 2.5 | pass under `ASAN=1`: the base overflows in `RE_Draw_CharScaled`, the head and each half alone do not; every screenshot equals `Drawable size` |

*FIX61: one rule in every renderer.* `SetMode_impl` in `rm_main.c` and in `gl1_main.c` (both
builds of `ref_gl1`) no longer keeps its own copy of the rule. Each calls `GLimp_DrawableInPixels`
(`glimp_hidpi.c`, now linked into `ref_metal` and `ref_gl1` as into `ref_soft`) with the arguments
`soft`'s `SWimp_DrawableInPixels` passes, `IsHighDPIaware, (int)vid_fullscreen->value,
r_mode->value`, read after `GLimp_InitGraphics` returned. `tests/check-mode-size.sh` keeps the
three calls equal; it fails at the base `8d63d071` and refuses its 12 mutants. The fullscreen
input stays the cvar, as for `soft`: only the cvar carries the client's own fallback to a window
(it writes `vid_fullscreen 0` and `r_mode 4`). After that fallback from `vid_fullscreen 2`, `r_mode
4`, the value passed, `r_mode` and the size handed back are the same as for the fullscreen window
made as asked, so a decision on the value passed would keep 640x480 under a 1280x960 `viddef`.
Nothing else changes: TASK118's write already made the cvar hold the value the window is made with.

On the build host (Apple Paravirtual device; a 640x480 window is 1280x960 pixels; desktop
1333x888 points), engine `bd0a456b` and the base `8d63d071` from `git archive`, each built with
`make client game ref_metal` and `make ref_gl1 WITH_METAL_SHIM=yes`, `MTL_DEBUG_LAYER=1` for both
renderers (`Metal API Validation Enabled` in every log, no other validation line),
`vid_highdpiaware 1`, the size script above (`developer 1`, `map base1`, 120 `wait`s, `screenshot
tga`, `quit`):

| Input | `Drawable size` | `metal` | `gl1-metal` |
|---|---|---|---|
| `vid_fullscreen 2`, `r_mode 99` (renderer's fallback) | 1280x960 | 1280x960 | 1280x960 |
| `vid_fullscreen 2.5`, `r_mode -1`, 2560x3000 | 2560x3000 | 2560x3000 | 2560x3000 |
| windowed, `r_mode 4` | 1280x960 | 1280x960 | 1280x960 |
| windowed, `r_mode 6` | 1600x1200 | 1600x1200 | 1600x1200 |
| `vid_fullscreen 1`, `r_mode 10` (client's own fallback: `CGDisplaySwitchToMode(): Unknown Error`, `Reverting to windowed r_mode 4`) | 1280x960 | 1280x960 | 1280x960 |
| `vid_fullscreen 2`, `r_mode -2` | 2666x1776 | 2666x1776 | 2666x1776 |
| `vid_fullscreen 2`, `r_mode 4` | 640x480 | 640x480 | 640x480 |

Status 0 in every run, and the base gives the same sizes in every row. Pins with `PINS_HIDPI`
unset, every scenario at the base (`PINS_BIN`) and at the engine, `PINS_SEED=1` for `models`,
`dlights`, `dlights-lmcubic`, `particles` and `particles-cap`, `PINS_DEMO=idlog.cin PINS_EVERY=7`
for `cinematic`, `compare --ref-id <base> --cand-id <engine> --threshold 0 --bad-threshold 0`:
all 11 scenarios on `metal` and on `gl1-metal` byte-identical (23 of 23 frames, `cinematic` 12 of
12), `worst=0 worst_bad=0`, 640x480, `hidpi 0`. Logs and the tables are in `<artifacts>/FIX61/`.

Left for the bare-metal host: the client's own fallback from `vid_fullscreen 2`, the one fallback
where the value used changes the rule's answer. SDL makes that window on this host, so it needs
TASK118's scratch test patch (`TASK118_FAIL_FS=1`, rework 2 above) on this card's engine,
`vid_fullscreen 2`, `r_mode 4`, `vid_highdpiaware 1`, `metal` and `gl1-metal`, `MTL_DEBUG_LAYER=1`,
the size script with no map at start. Expected: `Drawable size: 1280x960`, both screenshots
1280x960, no validation line. Repeat there the first two rows of the table, where the 5K panel
showed the mismatch.
