# Runbook: the Metal port and ray-tracing loop

Plan anchor: `docs/plans/metal-renderer.md`, as revised by INFORM8 (#35: pin
at two seams, shim first, then native `ref_metal`) and INFORM9 (#36: pins
from the owner's game data, in a local store, never committed; rendered
stills and short clips may be published as pull-request evidence since
decision record 0007, the store and the data never). Parent
cards: TASK27 (#37, harness), TASK21 (#28, OpenGL-subset shim), TASK28
(#38, native `ref_metal`), TASK24 (#31) and TASK25 (#32, ray tracing). The
cards filed for it on 2026-11-12, with their ids, are in
`docs/plans/metal-port-cards.md`.

This document is the operating procedure: what the harness is, how a
renderer is judged with it, and what the first run on real data found.

## What is in the tree (the TASK27 core)

| Piece | Where | Verified |
|---|---|---|
| Command-line latch of the four pin arguments | `CL_PinsLatch` in `src/client/cl_pins.c`, called from `Qcommon_Init` | `tests/check-pins-latch.sh` green; run inside the engine on real data (TASK39) |
| Frame writer, no stb | `VID_WritePinFrame` in `src/client/vid/vid.c` | run on `soft`: 23 frames per scenario, 640x480, upright (TASK39) |
| No renderer fallback while capturing | `VID_CheckChanges` in `src/client/vid/vid.c` | compiles and links; the failure path has not been provoked |
| Capture before present | `SCR_PinsCapture` in `src/client/cl_screen.c` | run on `soft`: complete frames with the status bar (TASK39); not run on `gl1` |
| gl1 screenshot flushes the batched 2D quads before reading | `R_ScreenShot` in `src/client/refresh/gl1/gl1_misc.c` | compiles and links; the context now exists on the build VM (TASK80) but the screenshot is not yet compared with a pre-change one |
| Unattended quit at end of demo, non-zero exit on a broken demo | `CL_PinsFrame` in `src/client/cl_main.c`, `SV_DemoEnd` in `src/server/sv_send.c`, `Com_Error` in `src/common/clientserver.c` | run: every record quits by itself with status 0; a missing demo and a short demo file without its end marker were run and exit 1 within about a second; a demo cut after playback began and playback stopping without a demo end are implemented but not observed (TASK39) |
| No saved configuration in a pin run | `Qcommon_ExecConfigs` in `src/common/frame.c`, `CL_WriteConfiguration` in `src/client/cl_main.c` | run: the engine log shows no `config.cfg` exec and none is written (TASK39) |
| Driver: record, compare, list, selftest | `scripts/pins.sh` (bash 3.2) | selftest green; `record` and `compare` run against the engine on real data (TASK39) |
| Comparator | `scripts/pins/framediff.c` | selftest green; run on real frames (TASK39) |
| Scenarios, one per feature | `scripts/pins/scenarios/*.cfg` | all six run on `soft` (TASK39; `skywater` and `cinematic` TASK40) |
| Cinematic capture: every Nth cinematic frame, quit at its end, exit 1 when it cannot be opened | `SCR_PinsCapture` in `src/client/cl_screen.c`, `SCR_CinematicEnd` in `src/client/cl_cin.c`, `CL_PinsFrame` in `src/client/cl_main.c` | run on `soft` with `idlog.cin` and a missing cinematic (TASK40) |
| Argument limit raised: `MAX_NUM_ARGVS` in `src/common/argproc.c` is 128 (was 50); `record` passes 66 to 69 words and the engine refused more than 50 at startup | `scripts/pins.sh selftest` case `argcount` and `tests/check-pins-latch.sh` feed every scenario's command line to the real `COM_InitArgv` | green in `scripts/test.sh`; the engine accepts 69 words (TASK39) |
| Renderer-API record serializers and the 22 wrapper hooks in `vid.c`, recording off | `src/client/vid/refrecord.c`, `src/client/vid/header/refrecord.h`, `tests/check-refrecord.sh` | every record type round-trips through a buffer in `tests/check-refrecord.sh` (green in `scripts/test.sh`); observed on one host: the `world` scenario recorded on `soft` with a build of the base commit and with a build of the hooks gave 23 frames each, and all 23 frame files and the manifest compared byte-identical with `cmp` (TASK42) |
| Renderer-API recording behind `pins_record`: handle map, `refdef_t` deep copy, string table, frame index, header patched at stop | `RefRec_*` recorder in `src/client/vid/refrecord.c`, `VID_StartRecord` in `src/client/vid/vid.c`, `pins_record` in `CL_PinsLatch`, `pins.sh record` (`PINS_TRACE=0` for none) | `tests/check-refrecord.sh` records traces and reads them back whole, failure paths included; observed on one host: `world` and `hud2d` on `soft` each left a 707-frame trace (about 4.4 MB) with 23 `REC_CAPTURE` records for the 23 frames captured, a 162-entry string table equal to the 162 distinct names a debug listing of the registration wrappers logged (to repeat: the trace side is `refrecord-test --read <trace> --names`, one `kind name` line per entry, from the test binary `tests/check-refrecord.sh` builds; the engine side was a temporary print, not in the tree, in `R_BeginRegistration`, `R_RegisterModel`, `R_RegisterSkin`, `Draw_FindPic` and `R_SetSky` of `vid.c`, logging the kind and name of each registration; registrations that returned a NULL handle were excluded, since they get no table entry by design; the two sets of distinct kind and name pairs were compared), and frames byte-identical (`cmp`) to a run with `PINS_TRACE=0` (TASK43) |
| A scripted demo that shows water: `make-demo <name>` runs a console script in a local game and leaves `demos/<name>.dm2` beside the binary, failing a demo outside the message range the script states; a demo there adds its checksum to the pin id | `scripts/pins.sh` (`cmd_make_demo`, `check_demo_script`, `demo_sum`), `scripts/pins/demos/water.cfg` | selftest cases `make_demo_*` and `demo_sum`; run on real data, and `skywater` recorded from the demo on `soft` (TASK70) |
| Gate checks, no game data needed | `tests/check-pins.sh`, `tests/check-pins-latch.sh` | green in `scripts/test.sh` |
| Pull-request evidence: still, animated PNG and GIF clip, manifest; publishing to the `pr-evidence` pre-release with refusals | `scripts/pins.sh evidence`, `evidence-publish`, `evidence-selftest`; encoders in `scripts/pins/evidence.py`; `tests/check-evidence.sh` | self-test green in `scripts/test.sh`; synthetic and real sets published and fetched back (TASK73, below) |

The client and `ref_soft` link and run on macOS arm64 against SDL3
3.4.20 (`make client ref_soft game`, SDL 3 built from `third_party/SDL3/`). `ref_gl1`
and `ref_gl3` build there too; `gl1` references are card P3's (TASK41, below).

In the tree from the renderer-API recorder (design in
`docs/plans/refapi-record-replay.md`): the record serializers, the wrapper
hooks and recording (`pins_record`: handle map, `refdef_t` deep copy, string
table, frame index), replay (`pins_replay`, TASK44; `pins.sh replay`,
TASK46), the renderer cvar snapshot and the fixed seed (`pins_seed`,
TASK45). No reference frame or trace is in the tree; they live in a pin
store only. The sky-and-water and cinematics scenarios came with
TASK40. The user-facing page is `doc/100_renderpins.md`.

## Pin arguments

`pins_dir`, `pins_scenario`, `pins_capture` and `pins_record` look like cvars but are not:
the engine reads them once from its own command line, exactly as
`+set <name> <value>`, before any config file runs, and keeps them where
nothing else can change them. A `set` of these names from a config file,
the console or a server's stufftext only creates an unrelated cvar that is
never read, because their values name files. `doc/040_cvarlist.md` has
the user-facing description.

- `pins_dir <path>`: a screenshot is written as an uncompressed 24-bit TGA
  `<pins_scenario>-<frame>.tga` into that directory, with a line in
  `manifest.txt` (file, renderer, size). `scrnshot/` is not touched.
  Refused when empty, containing `..`, or too long for the engine's path
  buffer; a frame path that would be truncated is never opened.
- `pins_scenario <id>`: the file-name prefix, `[a-z0-9_][a-z0-9_-]*`.
- `pins_capture <N>`: while a demo plays, every Nth rendered frame is
  captured after it is complete and before it is presented, by running the
  renderer's own `screenshot` command at that point. While a cinematic
  plays, every Nth cinematic frame is captured instead (the first rendered
  frame that shows it), so the frames do not depend on the render rate.
  When playback ends the engine quits. If the requested renderer fails to load, the engine stops
  with an error instead of falling back to another one. `0`, the default,
  disables all three.
- `pins_record <name>`: the first renderer session of the run is recorded
  to `<pins_dir>/<name>`, a plain file name (`[A-Za-z0-9_][A-Za-z0-9_.-]*`
  ending in `.q2r`, no `..`, no directory). `pins.sh record` passes `trace.q2r`, so the trace
  lies beside the frames; `PINS_TRACE=0` records none. The trace embeds
  data derived from the game data (INFORM9) and is never committed or
  uploaded.

A refused value turns the harness off and suppresses every screenshot
write. With none of them given the engine behaves as before.

## How a run works

`scripts/pins.sh record <renderer> <scenario> [--run N] [--replace]`
starts the engine with `-datadir $PINS_BASEDIR -portable`, every `set`
line of the scenario as a `+set` argument, then the driver's own: the
renderer, a fixed window size, sound off and the four pin arguments
(`pins_record` left out with `PINS_TRACE=0`), and
last `+demomap $PINS_DEMO` (default `demo1.dm2`). A scenario line that is
not exactly `set <cvar> <value>`, or that sets a cvar the driver sets
itself (`pins_*`, `vid_renderer`, `vid_fullscreen`, `r_mode`,
`r_customwidth`, `r_customheight`, `s_initsound`), is refused. `PINS_DEMO`
must be a file name under `demos/` ending in `.dm2`, or under `video/`
ending in `.cin` (for the `cinematic` scenario), not starting with `-` or
`.`.

Frames land in `<store>/<scenario>/<renderer>/<id>/`, where `<id>` is
`<engine>-<settings hash>`, with `-r<N>` appended for `--run N` (N from 2),
so a second run of the same engine and scenario is kept next to the
first. `<engine>` is the commit, plus `-d<hash>` when `src/` or the
Makefile hold uncommitted or untracked changes, plus `-b<hash>` of the
binary when `PINS_BIN` names one (`compare` then needs the same
`PINS_BIN`). With the default binary, `record` refuses a build that
`make -q` reports out of date. A complete pin is never overwritten unless
`--replace` is given; an incomplete one is. `record` and `compare` refuse a
store inside this repository or any other git work tree. A watchdog kills
a run after `PINS_TIMEOUT` seconds.

Nothing is written into the game data. With `-portable` the engine writes
beside its binary (`baseq2/qconsole.log`, `console_history.txt`), and with
pin capture on it neither executes nor writes `config.cfg` or
`autoexec.cfg`, so a pin depends on its scenario and command line only,
not on the owner's saved settings (the game data may hold a `config.cfg`
of its own, which a run without pins reads). The engine re-joins its
command line unquoted, so `record` refuses a store path, `PINS_BASEDIR`,
`PINS_DEMO` or scenario value with anything but `A-Z a-z 0-9 _ . / -`.

A record is complete when the engine exited 0 before the timeout, wrote at
least one frame, and every line of `manifest.txt` names the requested
renderer; `record` then writes the file `complete` last. Otherwise it
writes `incomplete` with the reason and exits non-zero. With pin capture
on, the engine exits 1 at once when the demo cannot be opened (any
dropped error is fatal), when the demo file ends before its end marker,
when it ends before a frame of it was played, or when playback stops
without the demo ending. Of these four cases only the first two (a missing
demo, a short file without an end marker) were run on real data; the
other two are implemented in `CL_PinsFrame` and `SV_DemoEnd` and read from
the source, not observed, and nothing in the gate reaches them.

`scripts/pins.sh compare <renderer> <scenario> --ref <renderer> [--ref-id <dir>] [--run N | --cand-id <dir>] [--threshold T] [--bad-threshold B]`
compares frame by frame and prints one line per frame (score, bad
fraction, verdict) and a summary. The candidate is the directory this
checkout's engine commit and these `PINS_*` settings record to (run N with
`--run`, any directory with `--cand-id`; it dies and says to record first
when it is absent). The reference is the directory `--ref-id` names, else
the most recently modified complete one under `<scenario>/<ref>/` other
than the candidate; the chosen path is printed as `ref=`. Both must be
complete, must differ, and must hold the same number of frames. A frame is
`ok` when its mean difference is at or under `--threshold` (default 0.02)
and the fraction of its pixels that differ by more than 8/255 in some
channel is at or under `--bad-threshold` (default 0.005); the summary
prints both values, so the thresholds a pin was accepted at are on record.
It exits non-zero when any frame is over, missing, or the frame counts
differ. The cross-renderer thresholds (`soft` against `gl1`) are set by
card P3 from real frames; none are assumed here.

Two runs of one renderer, as P1 and P2 require:
`pins.sh record soft world`, `pins.sh record soft world --run 2`, then
`pins.sh compare soft world --run 2 --ref soft`.

## Evidence on pull requests (TASK73)

Decision record `docs/decisions/0007-pr-evidence-frames.md`: rendered
stills and short clips may be published as pull-request evidence; the
game data, demos, cinematics, traces and the pin store never. The user
page (`doc/100_renderpins.md`, *Evidence for a pull request*) has the
commands, the files and the refusals.

Procedure for a renderer card: record and compare as above, then
`scripts/pins.sh evidence <candidate> <scenario> --ref <ref> --card <ID>`
(the same options as `compare`), look at the still, run
`scripts/pins.sh evidence-publish <manifest>` and paste the printed
section into the pull request under a heading `## Evidence`, one
section per scenario. The pre-release `pr-evidence` was created once by
hand (`gh release create pr-evidence --prerelease --latest=false`);
the script uploads only into it, never replaces an asset, and the
assets are named `<card>-<commit>-<renderer>-<scenario>-*`.

The clip shows the frames the pin captured, every `PINS_EVERY`th
rendered frame (30 by default: 23 frames over `demo1.dm2`), each for
500 ms. A smoother clip needs a denser record (`PINS_EVERY=5`, say) in
its own `--run`; that changes the settings hash, so compare it against
a reference of the same settings.

### The `screenshot` command between frames on the shim (FIX42)

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

## Frame rates on a physical GPU (TASK97)

Every earlier frame time in this runbook comes from the build VM, whose
OpenGL is a software renderer and whose Metal device is paravirtualised.
These come from a bare-metal Apple Silicon Mac, taken with `timedemo` over
the stock `demo1.dm2` (689 frames a run). Nothing here is a pin: no frame
was recorded or looked at.

**Host.** Model `Mac18,5`, GPU and Metal device "Apple M6", 32 GiB, macOS
27.2.1 (build 26B131), SDL 3.4.20 (cocoa), the display's current mode
2560x1440@60 (`Real display mode:` in every log; its list runs from
5120x2880 down). `ref_gl1` reports `GL_VENDOR: Apple`, `GL_RENDERER: Apple
M6`, `GL_VERSION: 2.1 Metal - 91.7`; `gl1-metal` reports `Metal shim on
Apple M6 (gl1-metal)`, `GL_VERSION: 1.4 mgl`. Source: `main` at 41745a59
(so `ref_metal` has the lightmapped world, sky, water and translucent
surfaces of TASK87 to TASK89 and draws no entities, dynamic lights or
particles, as `rm_main.c` at that commit says; TASK90 (#244) and TASK91
(#202) have since added brush entities, alias models and the view weapon,
then sprites, beams, stencil shadows, dynamic lights and flash blobs, so the
`ref_metal` column is of a lighter renderer than `main`'s and is not a
like-for-like comparison).

**Method.** One release build of the `quake2` binary, `ref_soft` and
`ref_metal`, and two builds of `ref_gl1.dylib`, the plain one and the
`WITH_METAL_SHIM=yes` one; between `gl1` and `gl1-metal` only that dylib was
swapped. Each run is a fresh engine started in the
foreground under `timeout 20`:

```sh
release/quake2 -datadir $PINS_BASEDIR -portable +set vid_renderer <gl1|soft|metal> \
  +set vid_fullscreen 0 +set r_mode -1 +set r_customwidth W +set r_customheight H \
  +set r_vsync 0 +set s_initsound 0 +set timedemo 1 +demomap demo1.dm2
```

The `frames, ... fps` line of the log is the number. The engine does not
quit after the demo, so every run ended at the timeout. Renderers were
alternated run by run (soft, gl1, gl1-metal, metal, then again; one such
pass is a round). Windowed only: `vid_fullscreen 1` falls back to 960x540
on this host. Every log printed `Drawable size:` equal to the requested
size. That line prints the requested size when the window is not high-DPI
aware, so it does not show whether the compositor scales the window. No
number below is a native-resolution number: 1920x1080 is the largest 16:9
size that fits the 2560x1440 display windowed (the engine's 1920x1200 and
1600x1200 modes were not tried), and the display's native size was not
measured.

**Results** (fps, sorted, `r_vsync 0`; ranges here, the values of the
larger batches below the table):

| Size | `ref_soft` | `ref_gl1` | `gl1-metal` | `ref_metal` | Runs |
|---|---|---|---|---|---|
| 640x480 | 253.6 to 318.1 | 2304.3 to 2600.0 | 273.8 to 328.1 | 237.2 to 317.7 | 6 |
| 1920x1080 | 60.0 (all six) | 1882.5 to 2126.5 | 60.1 (all six) | 60.0 (all six) | 6 |
| 1280x720 | 294.4 to 306.0 | not run | not run | 294.3 to 326.2 | 3 |
| 1600x900 | 284.7 to 287.8 | 2180.4 to 2237.0 | 245.5 to 252.7 | 277.5 to 283.4 | 3 of 5 |

Runs is the number of rounds the row is made of. 640x480: 6 rounds, plus
one earlier round left out as a warm-up. 1920x1080: 6 rounds. 1280x720: 3
rounds, `ref_soft` and `ref_metal` only. 1600x900: 5 rounds were run and
the row uses the last three; the first two were discarded as overlapping a
gate run by another lane (`scripts/test.sh`, 18:00:56Z to about 18:05Z);
the log times fit that for the first round but do not settle it for the
second. Their values are given below.

640x480 values: soft 253.6 267.5 276.6 297.1 306.2 318.1; gl1 2304.3
2426.1 2426.1 2434.6 2443.3 2600.0; gl1-metal 273.8 274.4 274.5 295.5
309.4 328.1; metal 237.2 244.4 290.7 310.1 313.3 317.7.

1920x1080 values: gl1 1882.5 1887.7 1903.3 1930.0 2032.4 2126.5; the other
three are 60.0 (`ref_soft`, `ref_metal`) or 60.1 (`gl1-metal`) in all six.

1600x900 values (used three, then the two discarded rounds): soft 284.7
286.0 287.8, discarded 277.6 280.1; gl1 2180.4 2215.4 2237.0, discarded
2187.3 2201.3; gl1-metal 245.5 246.5 252.7, discarded 202.8 255.0; metal
277.5 277.6 283.4, discarded 245.4 272.7. Including the discarded rounds
widens the ranges to 277.6 to 287.8, 2180.4 to 2237.0, 202.8 to 255.0 and
245.4 to 283.4.

1280x720 values: soft 294.4 299.8 306.0; metal 294.3 296.5 326.2.

`gl1` ran 0.3 s (0.3 to 0.4 s at 1080) for the 689 frames; the others 2.1 to 2.9 s
at 640x480 and 11.5 s at 1920x1080. The batch script waited until no
other `quake2`, `make`, `cc` or `clang` and no `scripts/test.sh` ran
before each run. The one-minute load average at each run start was printed
to the console and not kept in the logs, so it is recorded here only as
noted at the time: 1.9 to 2.6 over the 640x480 and 1920x1080 batches, 2.1
to 2.4 over 720, and 2.0 to 4.7 over the 1600x900 batches, where one stretch
went to 4.7 and another to 3.0 to 3.5 while only this lane's engines were
meant to be running, so those rows carry that spread; which round started
at which load is not recoverable.

**Not measured**, against the card's scope (as written by TASK97; TASK108
(#259) has since measured every item below, the native-resolution number
included (5120x2880 fullscreen), see "Re-measurement (TASK108)" after the
Findings, and what it still did not measure is listed there):
no native-resolution number (`vid_fullscreen 1` falls back to 960x540 on
this host and the largest 16:9 windowed size that fits is 1920x1080); the
`water` harness demo was not run (about 100 frames, too short for a stable
rate); the 1280x720 and 1600x900 rows have three runs, not five or more;
`ref_gl1` and the Metal-backed `gl1` were not run at 1280x720; fullscreen
was not run.

**What the VM cannot reproduce.** All of it: `ref_gl1` at 2000 fps and
more against a software GL, any hardware Metal rate, the 60 fps ceiling
below, and the ratio of the Metal renderers to Apple's own OpenGL. The
build VM capped once: the TASK90 section records the `demo1.dm2` timedemo
of `ref_metal` running at that display's 120 fps whatever `r_vsync` and
`vid_maxfps` said (the window size is not given there), while the TASK91
section records the same timedemo on that VM unpaced, at about 1,400 to
1,650 fps, so the cap is not constant there. It is a lead for Finding 3,
not shown to have the same cause.

**Findings** (cards filed, none fixed here; Finding 1 is FIX51 (#238),
Finding 3 is FIX52 (#239)):

1. `gl1-metal` and `ref_metal` are about 8 times slower than `ref_gl1` at
   640x480 (273.8 to 328.1 and 237.2 to 317.7 fps against 2304.3 to
   2600.0), far outside the run-to-run range, and no faster than the CPU
   `ref_soft` (253.6 to 318.1). Both sit near 3 to 4 ms a frame whatever
   the window size up to 1600x900 (3 runs; no Metal-backed `gl1` at
   1280x720), which points at a per-frame fixed cost and not at drawing.
   `ref_soft` shows the same flat 3 to 4 ms a frame up to 1600x900 (253.6
   to 318.1, 294.4 to 306.0 and 284.7 to 287.8 fps at 640x480, 1280x720 and
   1600x900), so it is not an independent CPU baseline, and the fixed cost
   is common to the `CAMetalLayer` present path on this host: a lead, not a
   diagnosis (FIX52 (#239) diagnoses part of that cost). On the build VM the
   same path runs `ref_metal` at about 1,400 to 1,650 fps (TASK91 section),
   so the cost is not inherent to it. That `ref_soft` is slower
   than `ref_gl1` is expected for
   a CPU renderer and is not a finding.
2. The `ref_gl1` number may be a CPU-submit rate: Apple's GL queues frames
   and the timer stops when the demo ends, without waiting for the GPU, so
   the effect is bounded by the frames still queued at the end unless the
   driver does not throttle. It was not checked against a GPU timestamp, so
   treat it as an upper bound. `gl_finish 1`, a switch the engine has,
   would bound it in a later run.
3. At 1920x1080 windowed, `ref_soft`, `gl1-metal` and `ref_metal` all run
   at 60.0 to 60.1 fps with `r_vsync 0`, while 1600x900 and below are
   uncapped and `ref_gl1` is not capped at all. The three capped renderers
   are exactly the three that present through a `CAMetalLayer` (`ref_soft`
   through SDL's Metal renderer, with display sync off); `ref_gl1`, the
   only uncapped one, presents through OpenGL. So the cause is likely
   common to the `CAMetalLayer` present path at that size. Not diagnosed
   here (FIX52 (#239) carries the diagnosis); until it is fixed, no 1080p
   rate for these three says anything about the renderer.

### Re-measurement (TASK108)

TASK108 (#259) repeats the open cases above on the engine source of
`462f8d3e` (the runs were built from a worktree one documentation commit
on, `7085c8f1`; `git diff --stat 462f8d3e 7085c8f1 -- src third_party
Makefile` is empty, so `462f8d3e` is the engine commit of every row). It has
FIX52 (#254) at `8065c3ec`, TASK90 (#244) and TASK91 (#258), and predates
TASK92 (#286, N9), so the measured `ref_metal` draws no particles although
`demo1.dm2` has them; the other three renderers are unaffected by TASK92. The
host, the launch line (TASK97's, except that it passes `+set basedir
/path/to/q2data` instead of `-datadir`, sets `r_vsync` as the last `+set`, and
ends each run with a 45 s bounded wait in place of `timeout 20`), the
alternation (soft, gl1, gl1-metal, metal, then again; five
rounds per row) and the `frames, ... fps` log line are as above; `ref_gl1`
is `GL_RENDERER: Apple M6`, `GL_VERSION: 2.1 Metal - 91.7`, the Metal-backed
`gl1` `Metal shim on Apple M6 (gl1-metal)`, `GL_VERSION: 1.4 mgl`,
`ref_metal` `Metal device: Apple M6`. Builds: `make -j4`, `make ref_metal`,
then `make ref_gl1 WITH_METAL_SHIM=yes` into a second copy of `release/` (so
`ref_gl1` and `gl1-metal` run from two directories, not by swapping one
dylib).

**The first batch ran with the screen locked.** The host session's screen
was locked while the first batch ran (the owner reported it; afterwards
`ioreg -n Root -d1` printed `CGSSessionScreenIsLocked`, and the lock state
was not recorded at the time). Its results are not measurements and no
number from it is used below. What it logged, for the record: all 30 runs
of each of `ref_soft`, the Metal-backed `gl1` and `ref_metal` read 119.9 to
121.2 fps at every size from 640x480 to 2560x1440 and on the `water` demo,
and every `vid_fullscreen 1` request other than the desktop mode failed
with `Couldn't switch to exclusive fullscreen: CGDisplaySwitchToMode():
Unknown Error` (seven cases, then `Reverting to windowed r_mode 4
(640x480).`). The switch failures are explained by the lock (FIX57 (#281) was filed for
them: with the screen unlocked the switches work, below); the locked
1280x720 `ref_soft` fullscreen case was not repeated unlocked (1280x720 is
not among the fullscreen sizes run here). The flat 120 is not wholly the
same: unlocked, the 640x480 and 1280x720 windowed rows still read about 120
(below), while 1600x900 and 1920x1080 windowed and every fullscreen row do
not. The `water` rows are not comparable (below).

The runs below were taken with the screen unlocked: `CGSSessionScreenIsLocked`
read 0 (`ioreg -n Root -d1 | grep -c CGSSessionScreenIsLocked` printed 0)
immediately before every run, kept in the per-run table beside the load.
Before each run no other `quake2` was running (`pgrep -x quake2`) and the
one-minute load average (`uptime`) was below 4; the load range of each row
is in the table. Every run is a fresh engine in the foreground, killed after
45 s (the engine does not quit after the demo). The logs and the per-run
table are kept outside the repository.

**Results** (fps, five runs each, `r_vsync 0` as the last `+set`,
`timedemo 1`, `+demomap demo1.dm2` unless said, `vid_fullscreen 0` unless
said; the engine commit is `462f8d3e` for every row, 689 frames a run, `Real
display mode: 2560x1440@60.00` for every windowed row; "drawable" is the
log's `Drawable size:` line, in pixels, which prints the requested size and
so does not show the size the window or display got, TASK98; the native claim
below rests on `Real display mode:`):

| Size and mode | display mode | drawable | `ref_soft` | `ref_gl1` | `gl1-metal` | `ref_metal` | Load at start (20 runs) | Lock |
|---|---|---|---|---|---|---|---|---|
| 640x480 windowed | 2560x1440@60.00 | 640x480 | 119.8 to 120.0 | 2443.3 to 2570.9 | 120.2 to 120.2 | 120.0 to 120.1 | 1.97 to 3.64 | 0 |
| 1280x720 windowed | 2560x1440@60.00 | 1280x720 | 120.0 to 120.0 | 2166.7 to 2327.7 | 120.1 to 120.2 | 120.0 to 120.1 | 2.32 to 3.69 | 0 |
| 1600x900 windowed | 2560x1440@60.00 | 1600x900 | 197.7 to 321.2 | 2087.9 to 2194.3 | 67.1 to 96.1 | 62.3 to 75.2 | 1.92 to 3.62 | 0 |
| 1920x1080 windowed | 2560x1440@60.00 | 1920x1080 | 163.1 to 225.6 | 1808.4 to 1997.1 | 60.1 to 60.1 | 60.0 to 60.0 | 1.72 to 3.75 | 0 |
| 1920x1080 `vid_fullscreen 1` | 1920x1080@60.00 | 1920x1080 | 279.6 to 284.8 | 2312.1 to 2514.6 | 273.0 to 317.2 | 313.3 to 362.3 | 2.20 to 3.67 | 0 |
| 5120x2880 `vid_fullscreen 1` | 5120x2880@60.00 | 5120x2880 | 44.3 to 45.0 | 602.8 to 661.9 | 283.7 to 402.0 | 349.0 to 389.5 | 2.61 to 3.92 | 0 |
| `water.dm2`, 1280x720 windowed, 93 frames | 2560x1440@60.00 | 1280x720 | 120.0 to 120.8 | 3206.9 to 4428.6 | 121.3 to 121.4 | 120.0 to 121.3 | 1.94 to 3.35 | 0 |

Values in run order (rounds 1 to 5):

- 640x480: soft 119.9 119.8 120.0 119.9 119.9; gl1 2443.3 2460.7 2460.7
  2469.5 2570.9; gl1-metal 120.2 in all five; metal 120.1 120.0 120.0
  120.0 120.0.
- 1280x720: soft 120.0 in all five; gl1 2281.5 2208.3 2166.7 2327.7
  2273.9; gl1-metal 120.2 120.2 120.1 120.1 120.1; metal 120.0 120.0
  120.0 120.0 120.1.
- 1600x900: soft 197.7 321.2 316.6 319.0 219.5; gl1 2087.9 2194.3 2107.0
  2094.2 2146.4; gl1-metal 67.1 88.7 73.0 96.1 93.3; metal 66.5 70.2
  75.2 62.3 74.6.
- 1920x1080 windowed: soft 163.1 225.6 183.2 206.3 199.3; gl1 1957.4
  1997.1 1847.2 1963.0 1808.4; gl1-metal 60.1 in all five; metal 60.0 in
  all five.
- 1920x1080 fullscreen: soft 283.5 282.6 284.8 279.6 284.2; gl1 2312.1
  2460.7 2505.5 2514.6 2505.5; gl1-metal 302.6 287.7 273.0 304.3 317.2;
  metal 314.5 341.9 324.5 313.3 362.3.
- 5120x2880 fullscreen: soft 44.3 44.6 44.8 45.0 44.9; gl1 649.4 652.5
  661.9 643.9 602.8; gl1-metal 393.0 313.8 283.7 402.0 396.4; metal 389.5
  366.5 379.2 349.0 356.6.
- `water` windowed: soft 120.0 120.8 120.3 120.2 120.2; gl1 3206.9 3875.0
  4428.6 4043.5 3576.9; gl1-metal 121.4 121.4 121.3 121.4 121.3; metal
  121.3 120.0 120.2 121.3 120.8.

**First-pass fullscreen probes** (one run per renderer, eight runs, taken
before the five-round rows; they are in the 28 and 28 logs counted below and
in no row above; `r_vsync 0`, `lock` 0; fps, one-minute load at start):

| Mode | `ref_soft` | `ref_gl1` | `gl1-metal` | `ref_metal` |
|---|---|---|---|---|
| 5120x2880 `vid_fullscreen 1` | 45.2 (3.68) | 667.6 (3.79) | 300.6 (3.82) | 349.9 (3.48) |
| 1920x1080 `vid_fullscreen 1` | 283.5 (3.66) | 2281.5 (3.60) | 292.9 (3.38) | 328.1 (2.88) |

Five of the eight started above a load of 3.5 (3.68, 3.79, 3.82, 3.66,
3.60). Five of the cells lie inside the five-round ranges. Three fall
outside: 5120x2880 `ref_soft` 45.2 (above the 44.3 to 45.0 range) and
`ref_gl1` 667.6 (above 602.8 to 661.9), and 1920x1080 `ref_gl1` 2281.5
(below 2312.1 to 2514.6). With the probes the six-run ranges are 44.3 to
45.2, 602.8 to 667.6 and 2281.5 to 2514.6. The rows exclude the first pass
because it is one run per cell, taken before the alternation of five rounds
began, and the probe 667.6 and 45.2 are each the maximum of the six, the
2281.5 the minimum.

Runs that started at a one-minute load above 3.5, and what they do to their
row: 5120x2880 `metal` round 1 (3.71, 389.5) is the row maximum; 5120x2880
`gl1-metal` rounds 2 (3.92, 313.8) and 5 (3.77, 396.4) and `metal` rounds 3
(3.52, 379.2) and 5 (3.74, 356.6) lie inside their rows; 1920x1080
fullscreen `soft` round 3 (3.58, 284.8) is the row maximum and `gl1` round 5
(3.67, 2505.5) is inside; 1600x900 `gl1-metal` round 4 (3.58, 96.1) is the
row maximum, round 3 (3.62, 73.0) and round 5 (3.60, 93.3) are inside, and
`metal` round 2 (3.61, 70.2) and `soft` round 3 (3.54, 316.6) are inside;
the 1920x1080 windowed `gl1-metal` round 1 (3.75, 60.1), the 640x480
`gl1-metal` round 5 (3.64, 120.2) and the 1280x720 `gl1-metal` round 5 (3.69,
120.1, equal to the row minimum, shared with rounds 3 and 4) do not move
their rows. The one-minute average is a trailing mean. The 5120x2880 loads
(2.61 to 3.92) are the highest of any row, and the largest `gl1-metal` spread
(283.7 to 402.0, the minimum from round 3 at 3.41) does not follow them.

**Probes** (one run each, so not rows; `r_vsync 1` as the last `+set`, and
`+cvarlist r_vsync` in each log, which printed `*    r_vsync "1"`; `lock`
0 before each):

| Mode | `ref_soft` | `ref_gl1` | `gl1-metal` | `ref_metal` | Load at start |
|---|---|---|---|---|---|
| 1920x1080 windowed (round 1) | 60.0 | 60.0 | 60.1 | 60.0 | 2.05 to 2.70 |
| 1920x1080 windowed (round 2) | 60.0 | 60.0 | 60.1 | 60.0 | 2.02 to 2.53 |
| 1920x1080 `vid_fullscreen 1` | 59.9 | 60.0 | 60.1 | 60.0 | 1.44 to 1.87 |
| 5120x2880 `vid_fullscreen 1` | 46.7 | 60.0 | 60.1 | 60.0 | 1.80 to 2.54 |

The first windowed round (round 1) was taken without `+cvarlist`, so its
logs hold no `r_vsync` line; round 2 repeated it with `+cvarlist r_vsync` in
the logs.
With vsync on every renderer that can reach 60 reads 59.9 to 60.1 at both
sizes. At 5120x2880 `ref_soft` stays at 46.7, below the refresh, as it was
(44.3 to 45.0) without vsync.

**The native size (Scope 1) works.** With the screen unlocked
`+set vid_fullscreen 1 +set r_mode -1 +set r_customwidth 5120 +set
r_customheight 2880` switches the display to the 5120x2880 mode, on all four
renderers, with no `Couldn't switch` or `SetVideoMode failed` line in any
of the 28 logs of it. Each log says `User requested 5120x2880@-1.000000,
setting closest mode 5120x2880@60.000000`, `Real display mode:
5120x2880@60.00` and `Drawable size: 5120x2880`. That is the card's
native-resolution number, the 5120x2880 row above (5120x2880 is the panel's
size in pixels, `Mode  0: 5120x2880@60.00`; the desktop mode is 2560x1440
points). The same holds at 1920x1080 (`Real display mode: 1920x1080@60.00`,
`Drawable size: 1920x1080`, 28 logs). The two sizes show that the earlier
run's fullscreen switch failure was the locked session.

The 640x480 request still ends at 960x540. The display lists 21 modes
(`SDL display modes:` in the log), `Mode  0: 5120x2880@60.00` to `Mode 20:
960x540@60.00`, all `@60.00` and all 16:9: 5120x2880, 4096x2304, 3840x2160,
3200x1800, 2880x1620, 2560x1440, 2048x1152, 1920x1080, 1600x900, 1440x810,
1280x720, 1024x576 and 960x540 (eight of the sizes are listed twice). There
is no 640x480, no 4:3 or 16:10 mode and nothing below 960x540. The engine
asks SDL for the closest mode, `SDL_GetClosestFullscreenDisplayMode(
displays[last_display], w, h, 0, true, &closestMode)`
(`src/client/vid/glimp_sdl3.c:238`, the branch for `vid_rate` not above 0;
line 216 is the same call with `vid_rate->value`), and prints `User
requested %ix%i@%f, setting closest mode %ix%i@%f` (245). SDL's choice is in
`third_party/SDL3/src/video/SDL_video.c`, `SDL_GetClosestFullscreenDisplayMode`
(1373): the modes are walked from the largest down; the walk ends when the
request is wider than a mode (`if (w > mode->w)`, 1403 to 1405) and a mode
is skipped when it is not tall enough (`if (h > mode->h) continue`, 1407 to
1410); of the rest, a later (smaller) mode replaces the one found unless the
found mode's aspect ratio is strictly closer to the request's (1417 to
1420). The request is 4:3 and every listed mode is 16:9, so that test never
skips, and the walk ends on the smallest mode at least 640 wide and 480
high, 960x540, which is also the smallest mode in the list. So 960x540 is
what SDL3 returns for this list; nothing in that selection is a defect.
Unlocked, the four 640x480 requests (one per renderer) logged `User
requested 640x480@-1.000000, setting closest mode 960x540@60.000000`, `Real
display mode: 960x540@60.00`, no switch error, and `Drawable size:
640x480` (that the drawable then stays at the requested size is FIX45 (#230),
TASK98 Finding 1). Those four runs are single runs from the first unlocked
pass (`ref_soft` 255.5, `ref_gl1` 2823.8, `gl1-metal` 183.9, `ref_metal`
208.0 fps, load 2.46 to 3.62), probes, not a row.

**The 60 fps cap on the `CAMetalLayer` renderers (FIX53 (#250)).** At 1920x1080
windowed with `r_vsync 0`, `gl1-metal` and `ref_metal` read 60.1 and 60.0 in
all five runs, and `+cvarlist r_vsync` printed `*    r_vsync "0"` in a
`ref_metal` 1920x1080 windowed run (`logs/cvarlist-vs0.log`) that still read
60.0. So with the cvar read as 0 the cap is still there. That is the cap
FIX53 (#250) describes, and the data here goes no further than that: the
cap holds at every windowed 1920x1080 run of the two (ten runs) and at
neither fullscreen size with `r_vsync 0` (1920x1080: 273.0 to 317.2 and
313.3 to 362.3; 5120x2880: 283.7 to 402.0 and 349.0 to 389.5). `ref_soft` is not capped
windowed at that size now: 163.1 to 225.6 fps, the effect FIX52 (#254) was for
(it read 60.0 in all six TASK97 runs). The FIX52 section at the end of this
file has the probe of where the windowed cap starts.

Two more windowed ceilings are in the table, neither explained here:
640x480 and 1280x720 read 119.8 to 120.2 for `ref_soft`, `gl1-metal` and
`ref_metal` (they read the same in the locked first batch, so the lock is
not the cause of those two rows). The ~120 is new against TASK97 (237.2 to
328.1 at 640x480 and 294.3 to 326.2 at 1280x720 for the same renderers) and
against the TASK92 section's frame-time table (`skywater`, `demo1.dm2`: 1285
to 1398 fps for `metal` and 816 to 984 for `gl1-metal`, `r_vsync 0`, taken at a
load of 7.8 to 13.4 on that section's host), both much higher. FIX51 (#238) now
carries these data, for the windowed ceilings and the 1600x900 spread.
The 1600x900 row reads neither: `ref_soft`
197.7 to 321.2 (the runs split into 197.7 and 219.5 against 316.6 to 321.2),
`gl1-metal` 67.1 to 96.1 and `ref_metal` 62.3 to 75.2, against TASK97's
284.7 to 287.8, 245.5 to 252.7 and 277.5 to 283.4 at the same size. The cause
was not found; no card yet.

**`ref_gl1`.** It is not capped: 2443.3 to 2570.9 at 640x480, 1808.4 to
1997.1 at 1920x1080 windowed and 602.8 to 661.9 at 5120x2880 fullscreen. TASK97
had 2304.3 to 2600.0 and 1882.5 to 2126.5 at the first two sizes, which these
overlap. TASK97's qualification stands (Finding 2): the number may be a
CPU-submit rate, an upper bound; it was not checked against a GPU timestamp,
and `gl_finish 1` was not tried.

**`water` spread (Scope 3).** `water.dm2` was made again by `scripts/pins.sh
make-demo water` in each of the two release copies (it printed `(98
messages, 97 to 124 wanted)` for both; the first run's demo had 124
messages), because the first run's copy was removed with its build; this one plays 93
frames in the timedemo, not 119, so it is not the same demo as the first
batch's and the rows are not comparable with it. A first attempt of the five
rounds ran before the demo existed and read `0 frames` (`ERROR: Couldn't
open demos/water.dm2`); its rows are in `results.csv` as
`water1280x720-nodemo` and are not measurements. `ref_gl1` gave 3206.9,
3875.0, 4428.6, 4043.5 and 3576.9 fps, range 3206.9 to 4428.6; the log
prints `0.0 seconds`. The timer counts whole milliseconds
(`src/client/cl_network.c`, the timedemo end), so the five runs took 29, 24,
21, 23 and 26 ms (a 1 ms step is 3 to 5 percent of a run), and a `ref_gl1`
rate from this demo is good to that and no finer. The three `CAMetalLayer`
renderers gave 120.0 to 121.4 (their runs last 0.8 s, so the 1 ms step is
about 0.1 percent). No run of the row started above a load of 3.5 (1.94 to
3.35). The demo is long enough to show the ceiling and too short to say more
about `ref_gl1` than the range above.

**Not measured, now.** The 1600x900 windowed spread, the 120 fps windowed
ceiling at 640x480 and 1280x720, and the windowed 60 fps cap itself are
seen, not explained. `ref_gl1` against a GPU timestamp (`gl_finish 1`). A
windowed timedemo above 1920x1080, or at the non-16:9 modes 1920x1200,
1600x1200 and 2560x1080 (booted only). Any fullscreen mode other than the
desktop, 1920x1080 and 5120x2880 modes and the 640x480 request (one run each
for the last). The `r_vsync 1` rows are one run each. Runs with no
`r_vsync` readback in the log (no `+cvarlist`): the 1920x1080 windowed row
(20 runs), the 5120x2880 fullscreen row (20 runs), the first windowed `r_vsync
1` probe round (4) and the twelve first-pass fullscreen probes (5120x2880,
1920x1080, 640x480); every other run's log has the `r_vsync` line, with the
value set. The desktop-size fullscreen
row of the first batch (2560x1440, which switches no mode) was not repeated
unlocked. The `water` demo is 93 frames, not the first batch's 119. The
lock state was read once per run before launch, not during it, and the owner
may have been at the machine.

## Screenshot and capture readback on a physical GPU (TASK98)

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

### What was measured

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

### Results

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

### Findings

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

### Not run

The Retina-scaled drawable (see the table), a window covered by another
window or on a sleeping display (the screen saver came on during
the windowed `hud2d` and `world` runs and every frame was still non-black and
identical to the earlier runs), and the 2D case in the same frame as a
harness capture.

### Commands

`scripts/pins.sh record gl1 hud2d --run 90` with `PINS_DRYRUN=1` printed the
engine line (kept out of the pin store); the runs were that line with
`vid_renderer gl1|metal`, `vid_fullscreen 0|1` and a private `pins_dir`, from
copies of `release/` built from main at `5f48807b` or earlier (before N6,
TASK89, and N7, TASK90, merged; the `ref_metal` rows describe that renderer,
not a later one) with `make`, `make ref_gl1 WITH_METAL_SHIM=yes` and
`make ref_metal`. Frames were compared with `framediff <a> <b>` for each
of the 23 frames and deleted afterwards.

## Judging a renderer

Prerequisites on the Mac: the owner's game data at `$PINS_BASEDIR/baseq2`,
a built client and renderer modules, and a pin store outside the repository
(`$PINS_STORE`, default `~/.quake2-pins`).

1. References, once per engine commit and scenario (INFORM9):
   `scripts/pins.sh record gl1 <scenario>` for each scenario on a machine
   where `ref_gl1` renders, and the same with `soft`.
2. Build the candidate.
3. `scripts/pins.sh record <candidate> <scenario>`, then
   `scripts/pins.sh compare <candidate> <scenario> --ref gl1`.
4. Fix the one feature the failing scenario names, rebuild, repeat 3. The
   thresholds (`--threshold`, `--bad-threshold`) are never widened to make
   a pin pass. A scenario marked
   `nondeterministic:` has its own recorded threshold.
5. A card's pins pass when every frame of every scenario it names is `ok`.

`scripts/test.sh` stays green with and without game data: of its checks,
the harness ones are `tests/check-pins.sh` (synthetic frames, fake engines),
`tests/check-pins-latch.sh` (the command-line latch, no engine),
`tests/check-refrecord.sh` (traces it writes itself) and
`tests/check-evidence.sh` (a generated test pattern), and none reads the
game data or a pin store ("Closing TASK27 on the build host" above). Pins on
real data are run inside a card, never by CI.

### Judging an effect's footprint (TASK133)

A pin says a frame is the same; it does not say how much an effect moved it,
or whether it moved it where it should. For a card that adds an effect
(shadows, ambient occlusion, reflections, the denoiser, the scalers):

1. Record the scenario with the effect off and on: the cvar that switches
   it, or two replays of one trace (`replay`), which keep the camera and the
   entities identical. Do it for the scenario and for its companion, the
   same scene with entities on (`scripts/pins/scenarios/README.md`, "Content"):
   an effect judged only with `cl_entities 0` is judged on a picture a player
   does not see. A scenario that declares a band and sets `cl_entities 0` has a
   companion that `check-content` holds to this: it sets `cl_entities 1`,
   declares the same band and sets every other cvar of the scenario to the same
   value, bar the content cvars and the demo and frame settings the README
   lists. For a scenario with no band the companion is nominal, and every
   scenario in the tree is one.
2. `scripts/pins.sh footprint <scenario> --off <record> --on <record>` prints the
   share of pixels changed per frame (darker and brighter apart), the largest
   change and the mean change. Set the band in the cfg header
   (`// footprint darker 0..0.05`), a small multiple of the worst frame without
   entities, on the scenario and on its companion; the command then names every
   frame outside it and fails. A misspelled scenario, a frame the on record
   lacks or has extra, an empty off record and a link where an output file
   goes are refused, not read as a pass.
3. `scripts/pins.sh where <scenario> --off ... --on ... --out <dir outside the
   repository>` writes, for the frames that changed most, a highlight (the off
   frame grey, darker red, brighter green) and a zoom of the busiest window
   (off and on side by side) as TGA. Only the owner opens them (Preview).
4. A card reports the table (not the images) and names the band it set.

TASK55's records, on the round-1 build (`darker 0..0.05`): without entities 23
frames of `demo1` and 89 of `demo2`, none outside the band, the worst frame
0.66 % and 1.44 % of the picture darker; with entities 4 of 23 and 8 of 89
frames outside it, the worst 52.6 % and 46.9 % darker.

## Design documents

| Document | Feeds |
|---|---|
| `docs/plans/metal-port-cards.md` | every card filed for the port, with dependencies, acceptance and ids |
| `docs/analysis/gl1-state-inventory.md` | TASK21, TASK28: GL entry points, draw-time state, pipeline-state keys, order |
| `docs/plans/metal-shim-design.md` | TASK21: module layout, state cache, uploads, readback, present |
| `docs/plans/ref-metal-native.md` | TASK28: native `ref_metal`, its layout, passes, thresholds against `gl1-metal`, cards N1 to N10 |
| `docs/plans/metal-raytracing-design.md` | TASK24, TASK25: acceleration structures, effects, G-buffer, gating |
| `docs/plans/refapi-record-replay.md` | TASK27: recorder and replayer behind the `vid.c` wrappers |

## Ray tracing and the pins

Frame pins cannot judge whether lighting looks better. For TASK24 and
TASK25 the harness is used three ways: with every ray-traced effect off,
`ref_metal` still passes all pins against `gl1`; each effect has its own
scenario, pinned against `ref_metal` at the commit whose look the owner
accepted; and `timedemo.txt` in each record holds the performance number
the card reports.

## Hardware tiers (from the plan)

T0, any Mac or the build VM: the harness, `soft` references, builds. T1, a
bare-metal Mac where `ref_gl1` renders: the `gl1` references, shim and
rasterizer parity, ray tracing in compute. T2, an M3 or later:
render-stage ray queries and the performance numbers.

## Open decisions for the owner

- INFORM7, INFORM8 and INFORM9 were closed as accepted by the owner on
  2026-11-12. Groups P, R and S in the manifest no longer wait on them. The
  unattended run prompt is [`metal-port-agent-prompt.md`](metal-port-agent-prompt.md).
- Whether a freely distributable demo may be downloaded for a CI-side pin
  job (INFORM9, option C). Until then CI runs synthetic frames only.

## The 60 fps cap at 1920x1080 windowed (FIX52)

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

## Bicubic lightmaps in `ref_metal` (TASK120)

`metal_lightmap_filter 1` filters each lightmap sample of the lightmapped
world with a cubic B-spline built from four bilinear taps, every tap clamped
half a luxel inside the face's rectangle of the slice (the plan,
[P5](high-resolution.md)). The default `0` is the bilinear tap as before.
Host: the build VM (a paravirtualised Metal device), at this card's base
`c01bd7d2` and the branch.

**Host test.** `tests/metal/rm_host_test.c` runs the shader's own formulas
(`mtl_shader_math.h`): the weights are positive and sum to 1; the two taps
of an axis weigh 1 together; away from an edge the four taps equal the
direct 4 by 4 sum; a constant lightmap stays constant; no result leaves the
range of its 4 by 4 neighbourhood of luxels (clamped to the rectangle) over
20,000 random samples including a margin past the edges; no luxel outside
the rectangle carries weight at an edge, a corner or past it, for six
rectangles including ones one luxel wide in one axis; a rectangle narrower
than a luxel is total (both taps at its low bound plus a half); and
`mtl_lm_rect_valid` is what tells the shader that a vertex has no
rectangle. Mutants of every new pure function (each operand of the
compound guards and each clamp bound): 88 run, 76 killed outright, 9 the
compiler refused for an unused operand (re-run in a form that compiles: all
9 killed) and 3 equivalent (`v <= lo` and `v >= hi` in `mtl_clamp_range`,
and `>=` for `>` in the guard of the upper bound, each differing only where
both branches give the same value). Two mutants of the first run survived
(the upper bound without its guard against a rectangle narrower than a
luxel, in two spellings); the narrow-rectangle cases were added for them and
kill both. The shader
function `rm_lm_sample_cubic` itself has no host test (it is MSL); the
pins below and the validation layer are its check.

**Pins at `metal_lightmap_filter 0`** (`MTL_DEBUG_LAYER=1`, 640x480, every
23 frames of the record; the reference is a record of `metal` built at
`c01bd7d2`, named with `--ref-id`; `dlights`, `models` and `particles` seeded
with `PINS_SEED=1` and the trace on, one seed for the reference and the
candidate; `--threshold 0.005 --bad-threshold 0.001`):

| Scenario | Frames | Failed | Worst score | Worst bad |
|---|---|---|---|---|
| `world` | 23 | 0 | 0 | 0 |
| `skywater` | 23 | 0 | 0 | 0 |
| `dlights` (seed 1) | 23 | 0 | 0 | 0 |
| `models` (seed 1) | 23 | 0 | 0 | 0 |
| `particles` (seed 1) | 23 | 0 | 0 | 0 |

The records have no validation message. The vertex grew by a member and
the vertex output by a flat one, and no pixel moved.

**At value 1** (rework round: the scenarios `world-lmcubic` and
`dlights-lmcubic`, `world` and `dlights` plus `set metal_lightmap_filter 1`,
are in the tree; `MTL_DEBUG_LAYER=1`, 640x480, `dlights-lmcubic` with
`PINS_SEED=1` and the trace on). Run against run (`--threshold 0
--bad-threshold 0`): `world-lmcubic` 23 frames, 0 failed, worst 0, bad 0;
`dlights-lmcubic` (seed 1) the same. Against the filter-0 record of
`world` (the frames copied under the scenario's frame names, since a
compare pairs frames by name; `--threshold 0.005 --bad-threshold 0.001`):
all 23 frames differ (score 0.000345 to 0.002014, so the worst is above 0),
bad at most 0.002816, 3 of 23 over the bad value. `dlights-lmcubic` against
the seed-1 `dlights` record: score 0.000345 to 0.002014, bad at most
0.001940, 2 of 23 over. A filter-1 record that is not the filter-0 one is
what the scenarios are for; the owner does not gate the look.
**Validation reaches the log**: a scratch build with the index buffer
offset of the lightmapped draw made 2 bytes odd aborted `world-lmcubic`
under `MTL_DEBUG_LAYER=1` (status 134, `engine.log`:
`-[MTLDebugRenderCommandEncoder validateDrawIndexedPrimitives:...]: failed
assertion`); the edit was reverted. The real records (7 `engine.log`)
hold only `Metal API Validation Enabled`.

**GPU time (VM numbers).** A measuring aid, `MTL_GPU_TIME=1` in the
environment, prints one line `ref_metal: gpu_ms <ms>` per command buffer
that completes (`GPUEndTime - GPUStartTime`, from `rmb_watch`). A frame is
one command buffer, so it is the 3D pass and the 2D pass of a frame; the
`world` scenario draws no entities and almost no 2D, so the 3D pass is
most of it. Five runs of each value, alternated, the `world` scenario's
engine command line (`PINS_DRYRUN=1 scripts/pins.sh record metal world`
prints it) with `r_vsync 0`, `pins_capture 100000` (so nothing is read
back), 715 command buffers a run; each run's median and 90th percentile:

| Size | Value | Median (ms), range of 5 runs | 90th percentile (ms), range |
|---|---|---|---|
| 1280x720 | 0 | 1.042 to 1.063 | 1.270 to 1.298 |
| 1280x720 | 1 | 1.037 to 1.074 | 1.263 to 1.370 |
| 1920x1080 | 0 | 1.102 to 1.186 | 1.353 to 1.653 |
| 1920x1080 | 1 | 1.062 to 1.099 | 1.295 to 1.357 |

The two values are not told apart on this host: the ranges overlap, and the
frame time does not follow the pixel count either (the paravirtualised
device is not shading-bound at these sizes), so no cost is read from these
numbers.

**On the bare-metal M1-family Mac, 2560x1600 (open; the card stays open for
it).** Build `client game ref_metal`, then for value in `0` and `1`, five
runs each, alternated, from the repository root (the engine ends itself
after the demo; `PINS_BASEDIR` is the game data):

```sh
mkdir -p "${PINS_STORE:?}/gpu"
MTL_GPU_TIME=1 release/quake2 -datadir "${PINS_BASEDIR:?}" -portable \
  +set timedemo 1 +set scr_conspeed 99999 +set cl_showfps 0 +set viewsize 100 \
  +set r_drawworld 1 +set crosshair 0 +set cl_gun 0 +set cl_entities 0 \
  +set cl_particles 0 +set cl_lights 0 +set cl_blend 0 \
  +set vid_renderer metal +set vid_fullscreen 0 +set r_mode -1 \
  +set r_customwidth 2560 +set r_customheight 1600 +set r_vsync 0 \
  +set s_initsound 0 +set metal_lightmap_filter VALUE \
  +set pins_dir "${PINS_STORE:?}/gpu" +set pins_scenario world +set pins_capture 100000 \
  +demomap demo1.dm2 2> "${PINS_STORE:?}/gpu/VALUE.log"
grep gpu_ms "${PINS_STORE:?}/gpu/VALUE.log" | awk '{print $3}' | sort -n \
  | awk '{a[NR]=$1} END {print NR, a[int((NR+1)/2)], a[int(NR*0.9)]}'
```

The printed line is the command buffers, the median and the 90th
percentile in milliseconds; record the range of the five medians per value,
and the host's model, display mode and macOS build. A window of 2560x1600
needs a display that holds it (windowed); if it does not fit, say which size
ran. The VM's two values being equal, the expected finding is a cost below
the noise; a median that rises by more than a tenth of a millisecond at 1
is a finding to report on the card.

## Crisp 2D in `ref_metal` (TASK119)

Three switches, all off by default (the plan, [P4](high-resolution.md)):
`r_scale8bittextures` as gl1 takes it, `metal_2d_sharp` (sharp-bilinear
sampling of the filtered 2D pictures) and the client's `con_backaspect` (the
console background at 4:3, cropped). Host: the build VM (a paravirtualised
Metal device), base `1701a052`; `MTL_DEBUG_LAYER=1` on every `metal` record.

**Host tests** (`tests/metal/rm_host_test.c`, `tests/unit/test_cl_conback.c`).
`mtl_sharp_coord` runs the shader's own formula: at every integer scale 1
to 8 the sample at each pixel centre of a bilinear sampler equals the nearest
texel's value; a texel centre keeps its coordinate; an edge is the half-way
blend; at one texel per pixel or fewer it is the plain bilinear coordinate;
a zero, negative or NaN derivative leaves it alone; at the non-integer scales
1.5, 2.5, 3.7, 5.25 and 7.9 only pixels within half a pixel of an edge are
blended. `rm_scale8_factor`, `rm_scale8_dims` (gl1's factor and its overflow
check, at the limit and one past it) and `rm_2d_sharp` are pinned, and
`Con_BackRect` (the console background's rectangle, in `cl_conback.c`
because `cl_console.c` cannot be linked into the unit binary) at 4:3, wide,
narrow, degenerate and int-limit sizes. The shader function
`rm_fragment_2d` is MSL and has no host test; the pins and stills below are
its check.

**Mutants** (each operand of every new pure function and compound guard):
host functions 46, 44 killed, 2 survive: `d >= 1` for `d > 1` in
`mtl_sharp_coord` (equivalent: both give 1) and `h < 0` for `h <= 0` in
`rm_scale8_dims` (a division by zero that arm64 answers with 0, so it is
equivalent there and undefined behaviour elsewhere; the check's UBSAN build
is the guard). `Con_BackRect` 34, 33 killed, 1 equivalent (`>=` for `>` at
exactly 4:3, where both branches give the same rectangle). Two first-run
survivors of `Con_BackRect` (a guard that could not fire, a fallback of a
side that was already right) led to the simpler code, and the exact-limit
case was added for a third; `rm_2d_sharp` first had redundant index bounds
whose mutants survived, and is now the two sampler values it accepts.

Rework mutants (round 1): `mtl_sharp_coord` (the offset of the blend, the
shape of the ramp, the `d` clamp) and the over-size case, 6 mutants, 6 killed;
the monotonic check now runs and a slow decrease of 2e-5 per texel is killed
(together with the other checks, which also fire). The scrap smoothing:
removing the call is killed (`hud2d-scale8` against `gl1-metal` then differs
by 55 bytes), making its guard always true is killed (`hud2d-scale8` then
equals `hud2d`). The fallback of `R_LoadPic` to an unscaled upload is now the
pure `rm_scale8_plan` (drop, unscaled or scaled), pinned in `test_scale8`:
(3000, 3000) unscaled, (320, 240) scaled to 640x480, the overflow dropped,
the cvar off unscaled, the limit exactly and one under. Round 2 mutants over
its guards, 9 in all, all killed except one equivalent (the explicit zeroing of
the sides on the drop path, which `rm_scale8_dims` already does; the line was
removed): the factor guard (never, or also at 2), the dims guard, the
staging guard (off, inverted, the unscaled sides not restored), the `nolerp`
and `enabled` operands. Not killed by any committed test: `vid_loaded_renderer`
(a name set and cleared in `vid.c`) and the call site in `R_LoadPic`
(which needs a picture of that size; the helper it calls is tested).

**Default pins unchanged.** The reference is a record of `metal` built at
`1701a052`; the candidate is this branch with every switch at its default,
`--threshold 0 --bad-threshold 0`, 640x480, `PINS_SEED=1` for `dlights`,
`models` and `particles`, `idlog.cin` every 7th frame for `cinematic`:

| Scenario | Frames | Failed | Worst score | Worst bad |
|---|---|---|---|---|
| `hud2d` | 23 | 0 | 0 | 0 |
| `world` | 23 | 0 | 0 | 0 |
| `skywater` | 23 | 0 | 0 | 0 |
| `dlights` (seed 1) | 23 | 0 | 0 | 0 |
| `models` (seed 1) | 23 | 0 | 0 | 0 |
| `particles` (seed 1) | 23 | 0 | 0 | 0 |
| `cinematic` | 12 | 0 | 0 | 0 |

**`r_scale8bittextures 1` against gl1.** `world-scale8` and `hud2d-scale8`
from `metal` against `gl1-metal` (`make client game ref_gl1 ref_metal
WITH_METAL_SHIM=yes`) with the same setting, `--threshold 0.005
--bad-threshold 0.001`: `world-scale8` at 640x480 (factor 2) 23 frames, 0
failed, worst score 0 (a frame has 5 pixels that differ, by at most 4 of
255); at 1280x720 (factor 3) 23 frames, 0 failed, worst 1e-06, worst bad
9e-06. The setting acts: a `world-scale8` frame differs from a `world`
frame in 125,000 pixels of 307,200. `hud2d-scale8` is smoothed, not
scaled: the HUD pictures are in the scrap, and gl1 smooths the scrap copy
(`Scrap_Update` reaches `R_Upload32`), as `rm_scrap.c` now does (the
first version of this branch did not, and the comparison at 0.005 could not
see a step of 2 in a channel). Rework: `hud2d-scale8` from `metal` against
`gl1-metal` at `--threshold 0 --bad-threshold 0`, 23 frames, 0 failed,
worst 0, worst bad 0; and `metal` `hud2d-scale8` against `metal` `hud2d`
differs in all 23 frames, 55 bytes in all, the same 55 as `gl1-metal`'s
pair. The console background and the menus are the other pictures it
changes, and no pin plays them (a scenario is a list of `set` lines, and
the demo's console is shut), so those are stills.

**`metal_2d_sharp 1`.** At 2560x1440 (this VM opens it; the window is the
drawable) the 2D scale is 4, an integer: all 23 frames of `hud2d-sharp`
equal the same demo with `r_2D_unfiltered 1` (every 2D picture nearest)
pixel for pixel, and differ from `hud2d` (39,627 pixels in frame 10). The
record against itself (`--run 2`): 23 frames, score 0, bad 0. The console
background (320x240 over 2560x1440, 8 by 6, also an integer scale) equals
the nearest still but for the 576 pixels of the clock. A switch that does not
apply is not hidden: the cvar is the `ref_metal` one, the other renderers do
not register it, and there is no menu item for it.

**`con_backaspect 1`.** At 1024x768 (4:3) a still with the full console equals
the default but for the clock (46 pixels in a 15 by 6 box at the bottom
right); at 1920x1080 it differs (the picture is 1920x1440, anchored at the
console's lower edge, so the top 360 lines are cropped). The pin harness
cannot play the console, so `hud2d` with the console down is a still, taken
at the disconnected full console (`+wait` and `screenshot tga`, the config
reset between runs: the switches archive).

**Validation reaches the log**: a scratch build without the fragment bytes
the 2D draw now binds aborted a start under `MTL_DEBUG_LAYER=1`
(`-[MTLDebugRenderCommandEncoder validateCommonDrawErrors:]` ... `failed
assertion`); the edit was reverted. The records here (`engine.log`) hold
only `Metal API Validation Enabled`.

**Not measured here.** The bare-metal M1-family Mac at 5K and the cost of
the sharp sample (one `fwidth` and a few operations a fragment, in 2D only).

## Particle size cap relative to the resolution in `ref_metal` (TASK122)

`metal_particle_cap 1` scales the largest particle size (`gl1_particle_max_size`,
the value the start-up took) by `height / 480` each view, the factor the size
already has (the plan, [P13](high-resolution.md)); the default `0` leaves it as
it was. The formula is `rm_particle_cap` in `rm_pure.c`. Host: the build VM (a
paravirtualised Metal device), at base `1701a052` and the branch.

**Host test** (`test_particle_cap`): the cap equals `max` at height 480 for every
mode and at every height for a mode other than 1; at mode 1 it is `max * height /
480` (40 gives 120 at 1440, 240 at 2880, 20 at 240, 90 at 1080); a huge height,
a huge `max` or an infinite height is held to `RM_POINT_ALIASED_MAX`; a height
not above 0 or NaN, and a `max` that is NaN or infinite, leave `max` alone; and
through `rm_point_size` the 1440-line size of a near particle is 120 with the
cap and 40 without.

**Pins** (`MTL_DEBUG_LAYER=1`, `PINS_SEED=1`, trace on, 23 frames each,
`--threshold 0 --bad-threshold 0`; the reference a record of `metal` built from
`1701a052`): at `metal_particle_cap 0` the scenarios `world`, `world-fullbright`,
`skywater`, `dlights`, `models`, `hud2d` and `particles` (640x480) all 23 frames,
0 failed, worst 0, bad 0. `particles-cap` at 640x480 against the `particles`
record (frames copied under the scenario's frame names): 23 frames, 0 failed,
worst 0, bad 0. At 2560x1440 (the window opened at that size, frames of 2560x1440)
`particles-cap` run against run: 23 frames, 0 failed, worst 0, bad 0. `particles-cap`
against `particles` at 2560x1440 (`--threshold 0.005 --bad-threshold 0.001`): 19 of
23 frames identical, 4 differ (frames 5, 8, 12 and 16: worst score 0.000841, bad
0.007824); the owner does not gate the look. That last comparison is the one that
tests the switch: at the default height of 480 the factor is 1, and a run against
run compares a build with itself, so both also pass without the wiring in
`rm_effects.c`. Repeat it with `PINS_WIDTH=2560 PINS_HEIGHT=1440 PINS_SEED=1` for
`particles` and `particles-cap`; the two must differ. The records have one validation line,
`Metal API Validation Enabled`, and no message.

**Mutants** of `rm_particle_cap` (17 run, each operand of the compound guard and
each arithmetic and bound operator): 15 killed outright (the mode test inverted or
disarmed, the height test in three spellings or dropped, the finite test dropped or
inverted, `480` as 481 or a product, the product as a sum or a square, the upper
bound dropped, each return replaced by `max`), 2 equivalent (`cap >` for `!(cap <`,
and `!(cap <=` for `!(cap <`: they differ only where `cap` is exactly
`RM_POINT_ALIASED_MAX`, where both branches give that value). One form of the mode
test the compiler refused for an unused operand was re-run in a form that compiles
and is killed.

## The opaque layer of `ref_metal` and the shim (FIX53)

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

## Card results

- [FIX41](../runbook/FIX41.md): `vid_restart` and the main thread's autorelease pool
- [TASK21](../runbook/TASK21.md): Closing TASK21 on the build host
- [TASK27](../runbook/TASK27.md): Closing TASK27 on the build host
- [TASK39](../runbook/TASK39.md): Observed on the first run
- [TASK40](../runbook/TASK40.md): Observed on sky, water and cinematics
- [TASK41](../runbook/TASK41.md): `gl1` references and the `soft` against `gl1` thresholds (card P3)
- [TASK49](../runbook/TASK49.md): The `world` scenario against the software renderer
- [TASK50](../runbook/TASK50.md): Sky, water and translucent surfaces against the software renderer
- [TASK51](../runbook/TASK51.md): The `models` scenario against the software renderer
- [TASK52](../runbook/TASK52.md): Particles against the software renderer
- [TASK53](../runbook/TASK53.md): Acceleration structures in `ref_metal` (RT1)
- [TASK54](../runbook/TASK54.md): The G-buffer and the lighting resolve in `ref_metal` (RT2)
- [TASK55](../runbook/TASK55.md): Ray-traced shadows from dynamic lights in `ref_metal` (RT3)
- [TASK70](../runbook/TASK70.md): Observed on the scripted water demo
- [TASK73](../runbook/TASK73.md): Evidence on pull requests: the synthetic test and the first real run
- [TASK80](../runbook/TASK80.md): `gl1` on the build host
- [TASK82](../runbook/TASK82.md): The screen flash and `hud2d`
- [TASK83](../runbook/TASK83.md): `gl1` references from an accelerated OpenGL
- [TASK84](../runbook/TASK84.md): The `ref_metal` skeleton (N1)
- [TASK85](../runbook/TASK85.md): Images, 2D and the cinematic in `ref_metal` (N2)
- [TASK86](../runbook/TASK86.md): Pics, characters and the HUD in `ref_metal` (N3)
- [TASK87](../runbook/TASK87.md): The world without lightmaps in `ref_metal` (N4)
- [TASK88](../runbook/TASK88.md): Lightmaps in `ref_metal` (N5)
- [TASK89](../runbook/TASK89.md): Sky, water, translucent and flowing surfaces in `ref_metal` (N6)
- [TASK90](../runbook/TASK90.md): Brush entities, alias models and the view weapon in `ref_metal` (N7)
- [TASK91](../runbook/TASK91.md): Sprites, beams, shadows, dynamic lights and flash blobs in `ref_metal` (N8)
- [TASK92](../runbook/TASK92.md): Particles, the parity sweep and frame time in `ref_metal` (N9)
- [TASK93](../runbook/TASK93.md): `ref_metal` on the build host (N10)
- [TASK111](../runbook/TASK111.md): MetalFX scalers for the 3D view in `ref_metal`
- [TASK118](../runbook/TASK118.md): HighDPI windows on the bare-metal host
