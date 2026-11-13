# Runbook: the Metal port and ray-tracing loop

Plan anchor: `docs/plans/metal-renderer.md`, as revised by INFORM8 (#35: pin
at two seams, shim first, then native `ref_metal`) and INFORM9 (#36: pins
from the owner's game data, in a local store, never committed). Parent
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
| Gate checks, no game data needed | `tests/check-pins.sh`, `tests/check-pins-latch.sh` | green in `scripts/test.sh` |

The client and `ref_soft` link and run on macOS arm64 against SDL3
3.4.20 (`make client ref_soft game`, SDL 3 built from `third_party/SDL3/`). `ref_gl1`
and `ref_gl3` build there too; `gl1` references are card P3's.

In the tree from the renderer-API recorder (design in
`docs/plans/refapi-record-replay.md`): the record serializers, the wrapper
hooks and recording (`pins_record`: handle map, `refdef_t` deep copy, string
table, frame index). Not in the tree from TASK27: replay (including
`pins.sh replay`), the cvar snapshot contents and the fixed `randk` seed, and
every reference frame. The sky-and-water and cinematics scenarios came with
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

## Observed on the first run (TASK39)

Host: the build VM, macOS arm64, `soft` at 640x480, `PINS_EVERY` 30,
`demo1.dm2`. Each record takes 5 to 7 seconds and captures 23 frames of
689 rendered.

1. The client starts with the hooks: `record soft <scenario>` exits 0 for
   all four scenarios and the engine log shows the latched values in use
   (frames named `<scenario>-NNNNNN.tga` in the store, `manifest.txt` with
   `soft 640x480` on every line).
2. `+demomap` on the command line reaches playback with `cl.attractloop`
   set: capture only runs under that condition and every record captured
   23 frames.
3. At demo end the server takes the `killserver` path (`SV_Nextserver`
   with an empty `nextserver`; the log reads `Server was killed.`), the
   client reaches `ca_disconnected` and the queued `quit` runs: every
   record quits by itself with status 0 (item (j) of the review: the
   unattended quit works with capture on). Without any `pins_*` argument
   a plain `timedemo 1` run of the same demo still did not quit (killed
   after 25 seconds), as before. The end of `demo1.dm2` is its end marker,
   not a short read.
4. `screenshot` inside `SCR_UpdateScreen` before `R_EndFrame` yields the
   complete frame on `soft`: world, view weapon and status bar are all in
   the captured frames, and a frame opened in an image viewer is upright
   (floor at the bottom, status bar at the bottom edge, numbers reading
   left to right; TASK9's acceptance). Not run on `gl1`: at the time
   `ref_gl1` could not create a GL context on the build VM (fixed by
   TASK80, below), so whether its screenshot (which
   now flushes the 2D batch first) is unchanged stays open for a host
   where `gl1` runs (card P3).
5. The owner's data holds two demos, both in `pak0.pak`: `demo1.dm2` and
   `demo2.dm2`. `PINS_DEMO` selects one; `demo1.dm2` stays the default.
6. Run to run on `soft`: `world` and `hud2d` are identical (score and bad
   fraction 0.000000 on all 23 frames) over four runs each. `models` is
   not: frame 7 differs in every pair, in a small patch of about 250
   pixels, which is consistent with temporary-entity explosions taking a
   random yaw and frame (`CL_ParseTEnt`) from the engine PRNG (not
   attributed by this record, which shows only the score and a pixel
   count); worst over five pairs
   score 0.000446, bad fraction 0.000973, which passes the default
   thresholds but is not 0. `particles` differs in 16 of 23 frames; worst
   over five pairs score 0.011556 and bad fraction 0.172061, so 10 or 11
   frames fail the default bad threshold. Both are marked
   `nondeterministic:` in their scenario files with these numbers.
   Light styles (item (i) of the review) did not differ between runs:
   `world` was identical every time.
7. `hud2d` with `r_drawworld 0` on `soft`: a uniform dark grey (RGB 26, 26,
   26) behind the status bar and the crosshair, 99.1% of the pixels. Other
   renderers are not measured yet.
8. `timedemo.txt` receives the line, for example `689 frames, 2.0 seconds:
   353.0 fps`; on the build VM `world` ran at 340 to 397 fps, `models` at
   280 to 300, `particles` at 232 to 256 and `hud2d` at 647 to 704.

Also found on the first run and fixed: the run read the `config.cfg` the
game data holds and wrote one beside the binary (now neither); a demo
that could not be opened left the engine at the console until the
timeout (now exit 1 at once); `pins_capture` accepted a leading space or
plus sign (now refused).

## Observed on sky, water and cinematics (TASK40)

Host: the build VM, macOS arm64, `soft` at 640x480.

1. Sky: `demo1.dm2` (the default) shows the sky in one stretch of its
   camera path, near the end of the demo: at `PINS_EVERY` 30 it is frame
   21 (`skywater-000021.tga`), at `PINS_EVERY` 10 frames 62 to 64. All
   other captured frames of `demo1.dm2`, and all of `demo2.dm2`, are
   indoors.
   The sky-and-water card accepts the sky on frame 21 at the default
   settings.
2. Water: no warped water surface was seen. All 23 frames of `demo1.dm2`
   and all 21 of `demo2.dm2` at `PINS_EVERY` 30 were looked at, and at
   `PINS_EVERY` 10 (69 and 62 frames) 20 further frames of each demo,
   spread over the whole run, plus frames 60 to 68 of `demo1.dm2`, the
   outdoor stretch. With the
   owner's two demos the `skywater` scenario therefore pins the sky and
   the water-warp settings, not a water surface; a demo whose camera
   passes water is needed for that, and no such demo is in this data.
3. `skywater` run to run: identical (score and bad fraction 0 on all 23
   frames) over three runs, the third from a build with TASK40's capture
   change; the change did not alter a demo capture.
4. Cinematics are reached with `+demomap <name>.cin`: `SV_Map` sees the
   `.cin` extension and spawns an `ss_cinematic` server with the attract
   loop set, the client plays it through `SCR_PlayCinematic`, and at its
   last frame `SCR_FinishCinematic` asks the server for the next map; with
   an empty `nextserver` the server is killed and the client disconnects,
   as at the end of a demo. Before TASK40 the capture hook captured
   nothing there (`cl.refresh_prepped` stays false without a map) and the
   run ended with `pins: playback stopped before the demo ended`, exit 1,
   after about 6 seconds of `idlog.cin`.
5. A cinematic shows a new picture 14 times a second by wall clock,
   whatever the render rate, so counting rendered frames would capture
   different pictures from run to run. The hook now counts decoded
   cinematic frames. `PINS_DEMO=idlog.cin PINS_EVERY=7 pins.sh record soft
   cinematic` captures 12 frames in about 6 seconds and quits with status 0;
   frame 0 is black (the start of the cinematic), the later frames show it
   fading in and out. Three runs were identical (score 0 on all 12 frames).
   The second run is recorded with `record soft cinematic --run 2` and
   compared as `compare soft cinematic --run 2 --ref soft`; `--ref-id
   <dir>` names the reference when the scenario has been recorded with
   other settings (item 8).
   The picture on screen lags the decoded-frame count by one to two
   frames, and with `PINS_EVERY=1` the capture at index 1 falls on a
   rendered frame where nothing new is drawn, so a GL reference should
   not be recorded at N = 1.
   No `Dropped frame` line was logged.
6. `PINS_DEMO=nosuch.cin` exits 1 within a few seconds with `pins: the
   cinematic could not be played`.
7. The owner's data holds the cinematics `idlog.cin`, `ntro.cin`,
   `end.cin` and `eou1_.cin` to `eou8_.cin`, loose under `baseq2/video/`;
   `idlog.cin` is the shortest and is the one recorded.
8. `compare` without `--ref-id` takes the most recently modified complete
   directory under `<scenario>/<ref>/`, which can be a record made with
   other `PINS_*` settings (here, `demo2.dm2` or another `PINS_EVERY`): it
   then reports a frame-count difference. When one scenario is recorded
   with several settings, name the reference with `--ref-id`.

### Thresholds measured for nondeterministic scenarios

| Scenario | Worst score | Worst bad fraction | Accept with |
|---|---|---|---|
| `models` | 0.000446 | 0.000973 | the defaults (`--threshold 0.02 --bad-threshold 0.005`) |
| `particles` | 0.011556 | 0.172061 | `--threshold 0.02 --bad-threshold 0.25` |

Measured on `soft` over five pairs of runs (run 1 against runs 2 to 6).
A fixed PRNG seed (record and replay group) would make both exact.

## `gl1` on the build host (TASK80)

`ref_gl1` used to stop on the build VM with "Failed creating OpenGL
pixel format", because it demands an accelerated visual and the only
OpenGL renderer there is Apple's software one. Since TASK80 it retries
once without that demand (in `RI_InitContext`, where the context is
created; a failure of the retry is reported as before) and switches
vsync off for such a context: with vsync on, the first buffer swap
waited for a display sync that never came and the engine drew no frame
in 240 s. The engine log of a pin now shows `GL_VENDOR: Apple Inc.`,
`GL_RENDERER: Apple Software Renderer`, `GL_VERSION: 2.1 APPLE-24.0.2`.

Observed on this host (machine load average 10 to 60 over the runs, other
work running): `record gl1 world` takes 17 to 24 s for 23 frames, `soft`
3 to 7 s. Three `gl1` records of `world` compare at score 0.000000 and bad
0.000000 on every frame (23 of 23), so the software OpenGL renderer is
deterministic here. `soft` against `gl1` on `world`: score 0.013 to
0.081, bad fraction 0.13 to 0.92 per frame, all 23 over the default
thresholds. The frames show the same scenes; `gl1` is smoother and
slightly darker (true colour and bilinear filtering against the software
renderer's 8-bit palette: 3589 to 41005 distinct colours per `gl1` frame
against 105 to 189 per `soft` frame), so thresholds for `soft` against
`gl1` are card P3's to set.

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

`scripts/test.sh` stays green with and without game data: the gate runs
only `tests/check-pins.sh` (synthetic frames, fake engines) and
`tests/check-pins-latch.sh` (the command-line latch, no engine). Pins on real data are run
inside a card, never by CI.

## Design documents

| Document | Feeds |
|---|---|
| `docs/plans/metal-port-cards.md` | every card filed for the port, with dependencies, acceptance and ids |
| `docs/analysis/gl1-state-inventory.md` | TASK21, TASK28: GL entry points, draw-time state, pipeline-state keys, order |
| `docs/plans/metal-shim-design.md` | TASK21: module layout, state cache, uploads, readback, present |
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
