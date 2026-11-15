# The `screenshot` command between frames on the shim (FIX42)

**Defect.** With `ref_gl1` built `WITH_METAL_SHIM=yes`, `screenshot` at
the console, at the main menu and in a map wrote an all black image (640x480,
0 of 921600 bytes non-zero, one colour), while every pin frame was right.

**Cause.** The pin harness captures inside `SCR_UpdateScreen`, after the
frame's draws and before `R_EndFrame` (`SCR_PinsCapture`), so
`glReadPixels` finds a frame with passes. The `screenshot` command is run
by the command buffer between frames: the shim's frame had been presented
and closed, so the read opened a new one, took a new drawable and
`mgl_layer_ensure_pass` gave it a first pass cleared to black, which was then
what was read. Nothing of the presented image was kept.

**Fix (shim only).** At each present `mgl_layer.m` keeps a strong
reference to the frame's drawable and its texture (`mgl_last_drawable`,
`mgl_last`; the offscreen texture when the frame had no drawable). The
layer was already `framebufferOnly = NO` for the pin capture, so the
texture can be read after it was presented, and the held drawable
cannot be handed out again while it is kept. `mgl_read_uses_last`
(`mgl_raster.c`) says a read of an open frame with no pass drawn, with a
kept image of the target's size, reads that texture in place and opens
no pass; after a pass the read is of the frame itself, as before. The
reference is replaced at each present (a skipped frame keeps it, that
image is still on screen), dropped when the drawable size changes and at
teardown (`mgl_release_all`, which `vid_restart` runs). No copy is made.

**Why not a copy per frame.** The first version of this fix blitted
every presented frame into a private texture and read that. Review
measured it: `timedemo` of `gl1-metal` fell from about 1290 to 600 fps
on `hud2d` (base 1400.4, 1285.4, 1250.5, 1175.8, 1375.2, 1367.1, 1102.4,
1285.4; copy 623.5, 560.6, 594.0, 607.6) and from about 1070 to 510 on
`world` (base 971.8, 1026.8, 1187.9, 1131.4, 1025.3, 981.5, 1091.9,
1127.7; copy 495.3, 513.8, 510.4, 529.2), about +0.9 ms a frame on this
host's paravirtual GPU, load average 8 to 10. That is a cost on every
frame for a command run by hand, and it was dropped for the held
drawable, which costs nothing measurable:

| `timedemo`, fps | origin/main | held drawable |
|---|---|---|
| `hud2d` | 1241.4, 1359.0, 1295.1, 1273.6, 1259.6 (mean 1285.7) | 1257.3, 1335.3, 1389.1, 1300.0, 1248.2 (mean 1306.0) |
| `world` | 1011.7, 997.1, 1148.3, 1008.8 (mean 1041.5) | 1086.8, 1023.8, 1022.3, 1037.7 (mean 1042.7) |

The same engine binary for both, only `ref_gl1.dylib` (and its
metallib, which is identical) swapped, `scripts/pins.sh record
gl1-metal <scenario> --run N` with `PINS_BIN`, `PINS_TRACE=0`, 640x480,
689 frames a run, base and head alternated run by run, one-minute load
average 6.4 to 17.6 (another agent shared the machine). A fifth `world`
pair ran with base and head at the same time by mistake and is left
out. Every run's `engine.log` ends with "0 without a drawable,
0 skipped": holding one presented drawable of the layer's three
(`maximumDrawableCount`) did not starve `nextDrawable`.

**Before the first present.** At start-up, right after `vid_restart`
and after a resize nothing of the target's size has been presented yet,
and the read still sees the open frame cleared to black, as before the
fix (an image of the window, which shows nothing yet). This is chosen,
not left over: refusing the read with `GL_INVALID_OPERATION` would make
it worse, because gl1's `R_ScreenShot` (not changed by this card) writes
its buffer whatever `glReadPixels` did, and that buffer comes from
`malloc` uninitialised. Measured at start-up: a `screenshot tga` on the
script's first line wrote 640x480 with 0 non-zero bytes. The
after-`vid_restart` and after-resize cases are by trace and by the host
test (no kept image, or one of another size, is not read).

**Host test.** `tests/metal/mgl_host_test.c`, `test_read_source` (the
host-test count is 515): 22 mutants of `mgl_read_uses_last` (constant 0
and 1, each of the five terms dropped, the pass test inverted or
loosened, each emptiness test loosened or raised to 1, each size compare
turned into `<=` or `>=`, the sizes compared by area, the axes swapped,
`&&` turned into `||` at three places) are each killed by it, among them
a kept image narrower, taller or turned against the target.

**Procedure.** A console script in a scratch copy of `release/` (not
committed), run as `./quake2 -portable +set basedir <data> +set
s_initsound 0 +set r_mode -1 +set r_customwidth 640 +set r_customheight
480 +set vid_fullscreen 0 +set vid_renderer gl1 +exec <script>` with
`MTL_DEBUG_LAYER=1`; the TGA lands in `baseq2/scrnshot/` beside the
binary. Console: `screenshot tga` on the first line (the start-up case),
ten `wait`s, `toggleconsole`, 60 `wait`s, two `screenshot tga` in a row,
`quit`. Main menu: ten `wait`s, `menu_main`, 60 `wait`s, `screenshot
tga`. Map: ten `wait`s, `map base1`, 120 `wait`s, `screenshot tga`,
`vid_restart`, 120 `wait`s, `screenshot tga`.

| Screenshot | Before (origin/main) | After (held drawable) |
|---|---|---|
| start-up, first line of the script | 0 non-zero bytes, 1 colour | 0 non-zero bytes, 1 colour (see above) |
| console, first | 0, 1 colour | 921560 non-zero, 2074 colours (viewed: the console log, upright) |
| console, second in a row | 0, 1 colour | 921560 non-zero, 2074 colours |
| main menu | 0, 1 colour | 905905 non-zero, 118 colours (viewed: the menu over the console log) |
| `map base1` | 0, 1 colour | 905647 non-zero, 12394 colours |
| `map base1` after `vid_restart` | not taken | 904717 non-zero, 6256 colours (viewed: the map, HUD and weapon) |

The Before column is from the first version of this fix, taken the same
way. Its After column gave the main menu as 6018 colours; that did not
reproduce (118 here and in review, the menu is drawn over the dark
console log). Under `MTL_DEBUG_LAYER=1` each of the three runs logged
"Metal API Validation Enabled" and the shim's counters and nothing else
from Metal ("0 without a drawable, 0 skipped").

**No regression.** In private stores, `PINS_TRACE=0`, the engine
binary of one build, `ref_gl1.dylib` of origin/main and of this head:
`record gl1-metal` of `hud2d` (23 frames, 5 runs each), `cinematic`
(`PINS_DEMO=idlog.cin PINS_EVERY=7`, 12 frames, one run each) and
`world` (23 frames, 5 runs each): 242 frames a side, and `diff -rq` of
the two stores reports no frame, only `engine.log`, `timedemo.txt` and
`complete`. One more record of each at the head under
`MTL_DEBUG_LAYER=1` gave the same frames and no validation message.
`models`, `particles` and `skywater` were not recorded: the change only
decides which texture a read with no pass drawn blits from, and every
pin capture reads after the frame's passes, which is the unchanged path
(`mgl_read_uses_last` is 0 when there was a pass, host tested); the
three recorded scenarios exercise that path and the presents.

**Not run.** The implementer of this rework did not run the full gate
(`scripts/test.sh`); `tests/check-metal-shim.sh`,
`tests/check-ref-metal.sh`, `tests/check-doc-links.sh`, the client, game
and shim builds were run. No live window resize and no non-virtual GPU
were tried.
