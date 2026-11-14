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

## Observed on the scripted water demo (TASK70)

Host: the build VM, macOS arm64, shared with other work and busy
throughout (load averages from `uptime` given per item), `soft` at
640x480, the default `PINS_EVERY` 30.

1. The demo: `scripts/pins.sh make-demo water` runs
   `scripts/pins/demos/water.cfg`. It loads `base1`, the first map of the
   game, gives `god`, `notarget` and `noclip` before `record water`, then
   `teleport`s the player to fixed coordinates over the large pool of
   that map (the eye about 24 units above the surface, level), holds 260
   `wait`s, moves it under the surface (about 24 units below) and holds
   320. It leaves `water.dm2` in `release/baseq2/demos/`; `git status`
   stayed clean, and nothing under `$PINS_BASEDIR` changed (checked with
   `find -newer` against a marker, TASK70 first review).
2. The length is not fixed. Console `wait` holds the script for one frame
   of at least 16 ms (`Cbuf_Execute`), so a wait count is a lower bound on
   time, and the engine has nothing that holds a script for a number of
   server frames (`fixedtime` and `timedemo` change the clock the server
   and the frame loop see, not the real-time test of `wait`). The first
   version held each place for 500 waits; reviewers measured 168 to 835
   messages and 6 to 21 captured frames from it. So the script states the
   message range it must give (97 to 124) and `make-demo` fails a demo
   outside it, keeping it as `water.dm2.failed`. With the current
   script, 7 runs with nothing else of this task running (load 108 to
   232) gave 100 or 101 messages. 5 runs made while this task's own
   mutant runs of the selftest shared the host (load 88 to 195) gave 103
   and 118 (passed) and 140, 165 and 189 (failed for the range). A run
   took 21 to 47 seconds. Re-measured on a quieter host (TASK70 rework):
   15 runs at load 5.8 to 28.0 gave 99 or 100 messages in 17 to 21
   seconds; 7 runs with the engine stopped (`kill -STOP` and `-CONT` on
   its pid) for 0.1 to 0.5 s of every second from its start, at load 7.5
   to 13.2, gave 110 to 126 messages in 20 to 33 seconds (one of 126
   failed for the range).
3. Why no walking: a walk timed by waits would end in a different place
   on another host. A teleport lands on fixed coordinates. In `noclip` the
   player has no gravity, so it stays there, and the teleport's pause
   flag, which holds the pitch at 0 until its timer runs down, never
   clears in `noclip` (`Pmove` returns before the timer for
   `PM_SPECTATOR`), so the view is level at the yaw of the level start.
   `+lookdown` therefore has no effect after a teleport in `noclip`; tried,
   the pitch stayed at 0.
4. Water, relative to the capture: every `skywater` record made from a
   demo in range (from three demos of 100, 100 and 103 messages, 19
   records) captured 4 frames. The first 2 show the pool from just above:
   a rippled, warped water surface over the lower half of the frame, the
   walls and the ceiling lights above it. The last 2 show the view from
   under the surface: the whole view warped, the pool floor below and the
   underside of the surface above. The move under water falls between
   the second and the third capture, and a longer or shorter demo inside
   the range scales both holds alike, which is what the range is chosen
   for. The rework measured it over the range (the 22 runs of item 2, a
   record at `PINS_EVERY` 30 and one at `PINS_EVERY` 1 of each demo): a
   demo of N messages rendered N-5 frames in all 22, and the move under
   water fell on rendered frame 41 to 53, 0.44 to 0.45 of the frames. The
   21 demos of 99 to 122 messages each gave 4 captures, 2 from above and 2
   from under the surface; the one of 126 messages rendered 121 frames and
   gave 5 (2 and 3), the fifth falling on frame 120. Captures fall on
   rendered frames 0, 30, 60, 90 and 120, so 4 captures need 91 to 120
   rendered frames, 96 to 125 messages; the script's range, 97 to 124,
   keeps one message inside each edge against a change in the 5-message
   lead (none seen in 22 runs), and the move under water stays at least
   11 frames after the second capture and 7 before the third.
5. Run to run: records of one demo file compared at score 0 and bad
   fraction 0 in all 27 pairs measured (6 pairs at load 192 to 197 and
   15 pairs at load 182 to 184 from a 103-message demo, 6 pairs at load
   142 to 144 from a 100-message demo). The first review measured, while
   the host was loaded by several engines, a few frames per pair of
   21-frame records at score 0.0017 and bad fraction 0.018 to 0.020. The
   cause is time-dependent rendering, not the demo or the capture
   cadence: the soft renderer animates the water warp by the client
   time, and in a timedemo `CL_AddEntities` clamps that time to between
   100 ms before and the time of the frame being shown, so it equals the
   lower bound, the same in every run, while frames take less than
   100 ms, and follows the real clock on a frame that takes longer.
   Reproduced: two records with the engine stopped for 150 ms of every
   160 ms (`kill -STOP` and `-CONT` on its pid) each differed from an
   unstalled record on one frame showing the surface from above, at
   score 0.001776 and bad fraction 0.020898; the frames under water and
   the frame indices were the same. The row below records the threshold.
6. Regeneration: a demo made again is a different file of the same
   camera places, so it is not byte-identical. Because a demo beside the
   binary adds its checksum to the pin id, records of two made demos
   never share an id; compare them with `--ref-id` and `--cand-id`.
7. A script whose map is not in the game data (tried with a throwaway
   script naming a map that does not exist) fails in 4 seconds with
   `the map <map> is missing from PINS_BASEDIR`.
8. The run writes `config.cfg`, `qconsole.log`, `pins_make_water.log`,
   `save/current/`, `save/save0/` and `scrnshot/` into `release/baseq2/`
   and `console_history.txt` into `release/`, all git-ignored, as a
   portable single-player start does.

### Thresholds measured for nondeterministic scenarios

| Scenario | Worst score | Worst bad fraction | Accept with |
|---|---|---|---|
| `models` | 0.000446 | 0.000973 | the defaults (`--threshold 0.02 --bad-threshold 0.005`) |
| `particles` | 0.011556 | 0.172061 | `--threshold 0.02 --bad-threshold 0.25` |
| `skywater` with `PINS_DEMO=water.dm2` | 0.001776 | 0.020898 | `--threshold 0.02 --bad-threshold 0.03` |

Measured on `soft` over five pairs of runs (run 1 against runs 2 to 6);
`skywater` from `water.dm2` over 27 pairs at score 0 and two records made
with the engine stalled (TASK70, item 5 above).
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

### What the synthetic test established (2026-11-13)

A generated test pattern (colour bars, a moving square, frame numbers,
no game content; `scripts/pins.sh evidence --synthetic`) was published
through `evidence-publish` to `pr-evidence`: a 320x240 PNG still
(1478 bytes), a 24-frame animated PNG (49870 bytes) and the same clip as
GIF (113219 bytes).

1. GitHub's Markdown renderer (`POST /markdown`, mode `gfm`, context this
   repository, the same pipeline as pull-request bodies) keeps the
   `<img>` for all three and leaves the URL as is:
   `https://github.com/<owner>/<repo>/releases/download/pr-evidence/<name>`.
   It does not route it through the image proxy (`camo`), because the
   URL is on github.com. The GIF's `<img>` gets a `data-animated-image`
   attribute (GitHub's play and pause control); the animated PNG's does
   not, because GitHub recognises animation by the `.gif` extension.
2. Fetching that URL gives `302` to `release-assets.githubusercontent.com`
   and then `200` with the exact bytes uploaded (SHA-256 equal to the
   manifest for every file). The final response is served as
   `content-type: application/octet-stream` with
   `content-disposition: attachment`, whatever type the asset was
   uploaded with (the API lists `image/png` and `image/gif`), and
   without `x-content-type-options: nosniff`. The github.com page's
   Content-Security-Policy lists `release-assets.githubusercontent.com`
   under `img-src`, so the page allows images from there.
3. Size: an animated PNG of random noise of 3753869 bytes and one of
   9760144 bytes were published the same way, rendered to an `<img>` by
   the Markdown API and fetched back whole (status 200, digest equal),
   then deleted. No limit was met up to 9.76 MB: release assets are not
   proxied, so the proxy's limits do not apply; the evidence budget
   stays at 4 MB per file to keep a pull-request page light.

Not established, because no browser was driven: that a browser draws
the `application/octet-stream` response inside the `<img>` (browsers
determine an image's type from its bytes there, and nothing in the
headers forbids it), and that the animated PNG animates inline. The
GIF of every clip is published next to it, and the printed section
links it, so the owner can compare the two in the first pull request
and drop the one that does not animate. If release images turn out not
to render at all, the fallback is a branch of images referenced by
commit through `raw.githubusercontent.com`; it was not taken, because
anything committed stays in the history of a public repository, while a
release asset can be deleted.

### First real run (TASK73)

`world` on `soft`, reference `598e4654575f-2420249005` (run 1) and
candidate `598e4654575f-2420249005-r2` (run 2), both from one commit, so
every score is 0 and the difference panel is black: a still of
1928x500 (192319 bytes, full size), an animated PNG of 23 frames at
968x260 (1281289 bytes, half size) and the GIF (689057 bytes, half
size), all within the 4 MB budget at the first size tried. On the build
VM, loaded by other work at the time, the encoding took about three
minutes, most of it in the pure-Python GIF encoder. Published and
fetched back byte for byte.

## `gl1` references and the `soft` against `gl1` thresholds (TASK41, card P3)

Host: the build VM, macOS arm64, `ref_gl1` through Apple's software
OpenGL (engine log: `GL_VENDOR: Apple Inc.`, `GL_RENDERER: Apple Software
Renderer`, `GL_VERSION: 2.1 APPLE-24.0.2`), 640x480, the default `PINS_EVERY`
30 except `cinematic` (`PINS_DEMO=idlog.cin PINS_EVERY=7`). The `gl1` frames
are therefore references for the shim's arithmetic, not for a graphics card;
a recording on accelerated hardware has a different `GL_RENDERER` and has to
be measured again, not assumed equal to these.

Method, per scenario: four `gl1` records (`record gl1 <scenario>`, then
`--run 2` to `--run 4`) and three `soft` records, from one build, one after
the other. Every pair is compared with `compare --cand-id <dir> --ref <r>
--ref-id <dir>`: the 6 `gl1`/`gl1` pairs, the 3 `soft`/`soft` pairs and all
12 `soft`/`gl1` pairs. Scores are per frame (mean difference, and the
fraction of pixels more than 8/255 apart in some channel); the ranges below
run over all frames of all pairs of the group. Every record exited 0. Load
is the 1-minute load average from `uptime` at the end of each record and at
the start of each compare (the machine was shared with other work all the
time; the range given is over all runs of the row).
`skywater` was measured with `water.dm2` made by `make-demo water` (100
messages) and with the stock `demo1.dm2`.

| Scenario | Demo | Frames | `gl1`/`gl1` noise (score, bad) | `soft`/`soft` noise (score, bad) | `soft`/`gl1` range (score, bad), worst frame | Proposed `soft` against `gl1` threshold | Load |
|---|---|---|---|---|---|---|---|
| `world` | `demo1.dm2` | 23 | exact: 0, 0 in 6 of 6 pairs | exact: 0, 0 in 3 of 3 | 0.013180 to 0.081482; 0.132624 to 0.922676 | none: not meaningful (below) | 11.6 to 21.3 |
| `hud2d` (with `gl_polyblend 0`, TASK82) | `demo1.dm2` | 23 | exact: 0, 0 in 6 of 6 | exact in 3 of 3 | 0.461199 to 0.471755; 0.990553 to 0.998561 | none: not meaningful (below) | 23.2 to 32.1 |
| `models` | `demo1.dm2` | 23 | not exact in 6 of 6: worst 0.000826, 0.004583 | not exact in 3 of 3: worst 0.000150, 0.000345 | 0.015337 to 0.090930; 0.179834 to 0.925937 | none: not meaningful | 10.9 to 20.9 |
| `particles` | `demo1.dm2` | 23 | not exact in 6 of 6: worst 0.010232, 0.172093 | not exact in 3 of 3: worst 0.011482, 0.130306 | 0.028070 to 0.141715; 0.408568 to 0.998324 | none: not meaningful | 16.2 to 30.6 |
| `skywater` | `water.dm2` | 4 | not exact in 3 of 6 pairs: worst 0.002562, 0.022972 | exact in 3 of 3 | 0.030261 to 0.033948; 0.539687 to 0.673857 | `--threshold 0.034 --bad-threshold 0.68` (below) | 16.7 to 19.5 |
| `skywater` | `demo1.dm2` | 23 | exact: 0, 0 in 6 of 6 | exact in 3 of 3 | identical to `world`: 0.013180 to 0.081482; 0.132624 to 0.922676 | none: not meaningful | 16.2 to 26.0 |
| `cinematic` | `idlog.cin`, every 7th | 12 | exact: 0, 0 in 6 of 6 | exact in 3 of 3 | 0 to 0.018680; 0 to 0.322956 (frame 0 is black and exact) | `--threshold 0.02 --bad-threshold 0.35` | 17.2 to 23.8 |

Each cell of the `soft`/`gl1` column is "score range; bad-fraction range":
the smallest and the largest per-frame value over all frames of all 12
pairs (the largest is the worst frame). All records are from the build of
commit `885e70fa` (the base of this work), except the `hud2d` row: TASK82
added `set gl_polyblend 0` to the scenario (see "The screen flash and
`hud2d`" below) and measured the row again the same way, 4 `gl1` and 3
`soft` records of commit `74205ac9`, all 21 pairs compared. `soft` has no
such cvar, so its frames are byte-identical to the old scenario's (score 0
on all 23 frames against a `soft` record of it), and the four flash frames
now darken in `soft` only, which moves the worst `soft`/`gl1` score from
0.464962 to 0.471755.

Renderer string of every `gl1` number above: `Apple Software Renderer`.

The `models` row comes from two measurement sets (the original one at
1-minute load 10.9 to 20.0 and a re-measurement by the review of this card,
4 `gl1` and 3 `soft` records of the same build, load 11.2 to 20.9); the
worst values in it, 0.000826 for `gl1`/`gl1` and 0.090930 for `soft`/`gl1`,
come from the second set (the first had 0.000260 and 0.090400). Every
column of the table holds sample maxima of a scenario driven by the engine's
PRNG, not bounds: a further record can exceed them.

### Where an agreement threshold means something

Two scenarios have one: `cinematic` and `skywater` with `water.dm2`.

`cinematic`: its `soft`/`gl1` frames agree to score 0.0187 and bad fraction
0.323 at worst, and the proposal keeps the default score threshold 0.02 and
sets the bad fraction at 0.35, following the convention of "Thresholds
measured for nondeterministic scenarios" (the default where the worst value
is under it, otherwise the worst value rounded up, never widened to make a
pin pass). The bad fraction is loose, so both values reject a wrong picture:
black against the `gl1` frames at `--threshold 0.02 --bad-threshold 0.35`
passes frame 0 (which is black) and is over on frames 1 to 11, with score
0.049713 to 0.117941 and bad fraction 0.969580 to 1.000000 (the review of
this card, `framediff`, load 10.3). The score margin against 0.02 is small
(7 percent), so a third renderer that is smoother or sharper than either of
these may land over it; that would be a finding, not a threshold to move.

`skywater` with `water.dm2`: the worst `soft`/`gl1` values are score
0.033948 and bad fraction 0.673857, so the threshold is the worst value
rounded up, `--threshold 0.034 --bad-threshold 0.68`. It rejects a black
frame: made with `framediff --block <gl1 frame> <out> 0 0 640 480 0` from
each of the 4 frames of one `gl1` record and scored with the comparator,
black scores 0.044689, 0.052517, 0.052069 and 0.041041 with bad fraction
0.767340, 0.767340, 0.994681 and 0.993538, over on all four frames at both
values (1-minute load 7.4, this card's rework). The margin is small on the
score (0.041 against 0.034) and rests on 4 frames, so it is a starting
value for a candidate that is not `soft`, not a proof that a wrong picture
cannot pass.

For the others no agreement threshold is meaningful. `soft` and `gl1` draw
different pictures of the same scene: bad fractions run from 0.13 to 0.92
on `world`, 0.18 to 0.93 on `models`, 0.41 to 0.998 on `particles` and 0.99
on `hud2d`, and `skywater` with `demo1.dm2` is identical to `world`. A
black frame against the `gl1` frames, scored with the comparator by the
review of this card (load 10.3), is over the threshold on only some frames
when the threshold is wide enough to pass every `soft` frame: at
`--threshold 0.081482 --bad-threshold 0.922676` (the worst `world` values)
it passes 15 of 23 frames of `world` and is over on 8 (score 0.037498 to
0.148949, bad fraction 0.617708 to 0.998796 over the frames); at 0.090930
and 0.925937 (the worst `models` values) it passes 15 of 23 frames of
`models` and is over on 8. A record as a whole fails when any frame is over,
so an all-black record is still rejected, but such a threshold cannot tell
a wrong picture on most single frames, and the `soft`/`gl1` ranges overlap
the black-frame ranges. The `particles` black frame (score 0.0484 to
0.2121) is an estimate from a throwaway script, not the comparator, and
was not re-measured. `hud2d` is a different case: its reason is the clear
colour (below), and black scores 0.496704 to 0.499510 there (against the 23
frames of the TASK82 `gl1` record, `framediff --block`, load 14.5), above the
worst `soft`/`gl1` value 0.471755, so a threshold that passes `soft` rejects
every black frame; the `soft`/`gl1` difference there is a property of the
scenario, not of the picture. So a third renderer cannot be judged against
`soft` by these numbers, and the judge for the shim is `gl1`, not `soft`.
Causes, each checked by looking at a pair of frames (downscaled to PNG in
scratch, one pair per cause):

- Colour depth: `soft` draws from the 8-bit palette, 93 to 174 distinct
  colours per frame in the frames looked at, and `gl1` 1923 to 28318 (true
  colour). Seen in a zoomed crop of `world` frame 6: the `soft` pixels
  are blocky and speckled, the `gl1` ones smooth.
- Filtering: on `cinematic` frame 3, zoomed 5 times, `soft` shows the
  picture as hard-edged blocks and `gl1` as bilinear-smoothed ones. The
  `gl1` frame is also slightly brighter here (mean RGB 51.9, 20.5, 10.1
  against 45.0, 16.5, 7.7), the opposite of `world`.
- Lighting and tint: on every `world` frame looked at (7 of 23, and 2 of
  4 on the water demo) `gl1` is darker and redder, for example `world`
  frame 6 mean RGB 39.0, 33.6, 26.9 on `soft` against 30.6, 21.2, 9.5 on
  `gl1`, and the underwater frame of the water demo shows the `soft`
  green-grey tint as a dark red-brown on `gl1`. The cause of the tint was
  not isolated.
- Clear colour in `hud2d`: with `r_drawworld 0` `soft` leaves a uniform
  grey (26, 26, 26) and `gl1` its own clear colour (255, 0, 128, set in
  `R_ResetClearColor` in `gl1_main.c`), 304,338 and 304,308 of 307,200
  pixels. That is the whole of the 0.46 score (measured before the
  `gl_polyblend 0` change; the re-measured range of 0.461199 to 0.471755
  above includes the `soft`-only pickup darkening, see the note there, and
  the pixel counts hold for a frame without a pickup); the status bar and
  crosshair are the same size in both.
- Not isolated: on `world` frame 21 the cloud pattern of the sky and the
  top of the tower differ in shape between the renderers; the cause was
  not found.

### `gl1` against `gl1`: the floor for the Metal shim

The thresholds the shim is judged at are `gl1-metal` against `gl1`. The
starting values are in the table below, and the first shim card that draws
each scenario confirms or revises them (`hud2d` and `cinematic`: confirmed
by TASK82). The
`gl1`/`gl1` column is their lower bound: a `gl1-metal` threshold under it would fail two
records of `gl1` itself. `world`, `hud2d`, `cinematic` and `skywater` from
`demo1.dm2` are exact, so any difference there is a real one. `models`
(worst bad fraction 0.004583, close to the default 0.005 that
`models` is accepted at), `particles` and `skywater` from `water.dm2`
are not exact between two `gl1` records, so for those the floor is the
worst value in the column, and a Metal candidate is judged against at least
two `gl1` references and the noise of each.
These are numbers from Apple's software renderer; on accelerated hardware
measure them again. The worst values in the column are sample maxima of a
PRNG-driven scenario, not bounds (`models` was measured twice and the
second set was worse), so a floor is a lower bound on what to accept, not
the number to accept at.

Accept `gl1-metal` against `gl1` at (starting values; the first shim card
drawing each scenario confirms or revises them, recording the measurement
if it does):

| Scenario | Accept at | Why |
|---|---|---|
| `world` | `--threshold 0.02 --bad-threshold 0.005` (the defaults) | `gl1`/`gl1` is exact |
| `hud2d` | the defaults | exact; confirmed by TASK82 (with `gl_polyblend 0`): three `gl1-metal` records against four `gl1` records, score 0 and bad 0 on all 23 frames of all 12 pairs |
| `cinematic` | the defaults | exact; confirmed by TASK82: three `gl1-metal` records against the `gl1` reference, score 0 to 0.000191 and bad 0 on all 12 frames (bilinear rounding), the same in each record |
| `skywater` with `demo1.dm2` | the defaults | exact |
| `models` | the defaults, 0.02 and 0.005 | `gl1`/`gl1` worst score 0.000826 is far under 0.02, but the worst bad fraction 0.004583 is 0.0004 under 0.005, so `gl1-metal` has about 8 percent of room on the bad fraction before it is over; widening it is a finding for the card to record, with its measurement |
| `particles` | `--threshold 0.02 --bad-threshold 0.25` (the `// nondeterministic:` line) | `gl1`/`gl1` worst 0.010232 and 0.172093 |
| `skywater` with `water.dm2` | `--threshold 0.02 --bad-threshold 0.03` (the `// nondeterministic:` line) | `gl1`/`gl1` worst 0.002562 and 0.022972 |

The `// nondeterministic:` lines of the scenario files are unchanged:
`gl1` shows the same nondeterminism as `soft` (`models`, `particles`,
and the water demo), inside the numbers they already carry for `particles`
and `skywater` (`gl1`/`gl1` 0.010232 and 0.172093; 0.002562 and 0.022972,
under `--bad-threshold 0.03`), and just under their default for `models`.

### The screen flash and `hud2d` (TASK82)

On frames 5, 8, 11 and 16 of `hud2d` the player picks up an item and gl1
draws the screen blend (`R_PolyBlend`, a quad 10 units in front of the eye
through `glFrustum` and `glRotatef`). `cl_blend 0` does not stop it: it
clears the blend's colour, not its alpha, so the screen darkens. Apple's
software renderer draws that quad as a diagonal wedge over 6.2 percent of
the screen; `soft` and a correct GL darken the whole screen. Measured
before the decision, with the TASK82 shim (which implements those calls)
against the `gl1` reference `110a25ae2d26-2096632597` of the old scenario
(load 37.6): frames 5, 8, 11 and 16 over at score 0.054615 to 0.110522 and
bad 0.919017 to 0.928447, frame 3 (a fainter blend) at score 0.009718 and
bad 0; the rest exact. Judging those frames against `soft` instead is no
way out: there the clear colour differs on every pixel (gl1-metal against
`soft`, frames 3, 5, 8, 11, 16: score 0.351816 to 0.453720, bad 0.997259
to 0.998561). No threshold was widened.

Decision: `hud2d` sets `gl_polyblend 0`, which keeps the quad out of the GL
renderers (`soft` has no such cvar and still darkens, and stays exact run
to run), and the `gl1` and `soft` references of `hud2d` were recorded again
(the row in the table above). With it, three `gl1-metal` records of commit
`74205ac9` match all four new `gl1` records exactly (score 0, bad 0, all 23
frames, 12 pairs; record and compare load 22.7 to 64.7; three more of
commit `20014fb9`, the final shim code, gave the same at load 21.5 to
37.6), and `gl1`/`gl1`
and `soft`/`soft` stay exact. The new `gl1` frames differ from the old
reference only on frames 3, 5, 8, 11 and 16, by the wedge (score up to
0.007440, bad 0.061862).

The flash is judged in a 3D scenario from S3 on: `particles` keeps
`cl_blend 1` and the default `gl_polyblend 1`. The software renderer's
wedge will appear in those `gl1` references too, so a correct `gl1-metal`
will differ from them on the flash frames for the same reason; the card
that accepts such a scenario records the frames and their numbers, and an
accelerated `gl1` reference (INFORM9) or a recorded exception decides them,
not a wider threshold.

### The `world` scenario against the software renderer (TASK49)

Three `gl1-metal` records of `world` (commit `6b6a2910`, the S3 shim;
identical to each other, and to a record of the ASAN and UBSAN build of
`0d68aad9`) against the `gl1` reference `110a25ae2d26-2420249005` at the
defaults: in each record 0 frames exact, 7 passing (9, 12, 13, 17 to 20)
and 16 over, worst score 0.025886 and bad 0.632692 (frame 8). Load
average (1 minute) 5.5 to 43.0 over the records and compares. No
threshold or scenario was changed; the decision is the owner's.

`gl1_multitexture` was set explicitly in scratch copies of `world` (not
committed): with 1, the frames of both renderers are byte-identical to
`world`'s (the default is 1); with 0 the classic two-pass path runs.
Each copy was also recorded with `gl_polyblend 0`. `gl1` was recorded
twice per copy and `gl1-metal` three times; `gl1-metal` is exact run to
run in every copy, `gl1` too except frame 0 of the `gl1_multitexture 0`
copy with the flash (score 0.004627, bad 0.027529 between its two
records). Three faults of the reference make the difference, none of
them in the world path:

- **The screen flash** (`R_PolyBlend`), on frames 3, 5, 8, 11 and 16:
  against the same scenario with `gl_polyblend 0` it changes 1.6 to 6.2
  percent of the pixels of `gl1` (the wedge) and 39 to 98 percent of
  `gl1-metal`'s (the whole screen; frame 3 is a faint blend). With
  `gl_polyblend 0`, frames 11 and 16 pass; 3, 5 and 8 stay over for the
  next reason.
- **Polygons that cross the screen edge**: the software renderer drops a
  sliver of them (the clear colour shows: a black triangle at the bottom
  of frame 22, a black band across the floor of frame 0) or draws it
  with wrong texture coordinates (a seam and a brighter wedge of floor
  at the left of frame 3, a triangle of garbled wall in frame 10).
  `soft` draws those places as `gl1-metal` does. Splitting each frame's
  bad pixels (`gl_polyblend 0`) into 8-connected regions of 150 pixels
  or more and scattered ones: the regions are 0.005498 to 0.094684 of
  the frame on frames 0, 3 to 8, 10, 21 and 22, whose scattered rest is
  0.000309 to 0.004268 (under the default); on frames 1, 2, 14 and 15
  the scattered part alone is 0.008066 to 0.035690, on the floor right
  in front of the camera (a smooth difference field of both signs,
  mean 2 to 3 of 255 per channel on frame 1, with no period along x
  and not reduced by a one-pixel shift; those floors cross the bottom
  edge too, so the same clipping is the likely cause, not proven).
- **The classic path stripes**: with `gl1_multitexture 0` the software
  renderer's lightmap pass fails its `GL_LEQUAL` depth test along
  streaks, so the wall shows unlit; its own two paths differ by a bad
  fraction of 0.067484 to 0.294687 on every frame. `gl1-metal`'s two
  paths agree within rounding (bad 0 on 21 frames, at most 0.000029).
  `gl1-metal` against the `gl1` record of that copy: all 23 frames over
  in every pair, score up to 0.071019, bad 0.066068 to 0.696745 (0.066068
  to 0.370527 without the flash).

So `compare gl1-metal world --ref gl1` cannot pass on every frame at the
defaults against these references for any renderer without those
faults. `gl_polyblend 0` in `world`, as in `hud2d`, removes only the
first (two frames). Judging the rest needs a reference from an
accelerated OpenGL (INFORM9, T1), or a recorded exception per frame.

TASK83 (#187) is the card that judges `world` again, against references
from an accelerated OpenGL. Polygon offset and dynamic-light sub-uploads
are host-tested only, not judged on frames (`world` has `gl_zfix 0` and
`cl_lights 0`).

The review checked this independently. A three-way comparison with
`soft` on the 15 frames still over without the flash found 56 differing
regions of 150 pixels or more (108,719 pixels), with `gl1` the odd one
out in all 56 and `gl1-metal` alone in none. On passing frames the
luminance ratio of `gl1-metal` to `gl1` is 0.9996 to 1.0015; with
overbright forced to 2 and 4 both renderers brighten alike.

### Sky, water and translucent surfaces against the software renderer (TASK50)

Which frames show them. Found with a scratch build of the shim (not
committed) that painted every sky draw, warp draw and translucent draw
in a flat colour of its own, recorded into a private store, and the
painted pixels counted per frame:

- `world` (and `skywater` from `demo1.dm2`, whose frames are
  byte-identical to `world`'s): the sky covers 18.8 percent of frame 21
  (the box x 0 to 328, y 0 to 203) and 0.19 percent of frame 22 (a
  corner at x 94 to 145, y 0 to 22); a translucent surface covers 7.9
  percent of frame 21 (x 0 to 203, y 312 to 479). Frames 19 and 20 make
  sky or translucent draws that the depth test hides entirely. No frame
  shows warped water. The other 19 frames make none of these draws.
- `skywater` from `water.dm2`: the pool is a translucent warped
  surface, drawn through `buf_alpha`; it covers 41.5 to 41.7 percent of
  frames 0 to 2 (from above, y 258 to 479) and 40.4 to 40.8 percent of
  frames 3 and 4 (from below, y 0 to 211). No opaque warp, no sky.

The water-demo measurements below were made with a demo that `scripts/pins.sh make-demo water` had REFUSED (146 messages against the accepted range of 97 to 124; all 13 attempts gave 142 to 151) and that was renamed by hand for this measurement only, so they are indicative and not acceptance evidence. The message range needs recalibrating, because it was calibrated under heavy host load.

The water demo. `make-demo water` gave 142 to 151 messages in 13 runs
at 1-minute load 8.3 to 172.6 (a run took 24 seconds at load 8.3),
outside the 97 to 124 its script states, so every run failed for the
range; the cause was not looked into. The records below are of the
last of them, 146 messages (kept by renaming `water.dm2.failed` to
`water.dm2`, beside the binary only; `cksum` 1270321975, 15089
bytes). By TASK70's rule it renders 141 frames (N - 5), so it
captures 5 frames, 3 from above
the surface and 2 from below, not the 4 of the TASK70 range; the
TASK41 `gl1` reference (a 100-message demo no longer on the host) has
4 frames and is not comparable, so `gl1` and `soft` were recorded again
from this demo (commit `92302e46`): two `gl1` records are identical to
each other, and two `soft` records too. `gl1` of this commit is
identical to the TASK41 references on `world` and on `skywater` from
`demo1.dm2` (score 0 on all 23 frames of each).

Three `gl1-metal` records of each scenario (commit `aede3d66`, the
shim of TASK49 plus this card's host test; load 2.8 to 3.8 at the
records, 5.6 to 6.0 at the compares), every engine log without a
`not implemented` line and with the shim's `mgl: 707 frames` line
(157 for the water demo, which plays fewer), 0 skipped. A record of
each scenario from an `ASAN=1 UBSAN=1` build of the same commit has no
sanitizer line in its log and is identical to the release records
(score 0 on every frame; load 19.2 to 23.1):

| Scenario | `gl1-metal` run to run | Against `gl1` (per record, the same in all three) | Accept at |
|---|---|---|---|
| `world` | exact: score 0, bad 0, all 23 frames, 2 of 2 pairs; also exact against the parent's record | 0 exact, 7 passing (9, 12, 13, 17 to 20), 16 over; worst score 0.025886, bad 0.632692 (frame 8) | the defaults |
| `skywater` from `demo1.dm2` | exact in 2 of 2 pairs, and against the parent | the same as `world` frame for frame | the defaults |
| `skywater` from `water.dm2` | exact in 2 of 2 pairs, and against the parent | 0 exact, 0 passing, 5 over, against each of the two `gl1` records; score 0.003884 to 0.021403, bad 0.053896 to 0.583548 (frame 3 worst) | `--threshold 0.02 --bad-threshold 0.03` |

The water demo's known run-to-run noise (the warp follows the client
clock when a frame takes over 100 ms) did not show at this load.

No threshold or scenario was changed. The frames over are the
reference's faults, not the shim's: each region of `gl1`/`gl1-metal`
difference on the frames with sky, water or a translucent surface was
looked at three ways (`gl1`, `soft`, `gl1-metal`, the same capture,
downscaled in scratch), and in every one `gl1` is the odd one out:

- `world` 21: a notch of sky in the tower (x about 340 to 390, y 0 to
  110) only in `gl1`; `soft` and `gl1-metal` draw the wall there (a
  polygon across the top edge, dropped). `soft` draws this frame from
  a slightly different place (TASK41: "the top of the tower differ in
  shape"), so it disagrees with both elsewhere.
- `world` 22: in the sky corner, 3 pixels at the sky's edge with the
  wall (x 138 to 145, y 1 to 16), one pixel each, where `gl1` has the
  wall and `gl1-metal` the sky; an edge rule, not attributable.
- `skywater` (water) 0 to 2: a wedge of the surface from about (200,
  280) down to (80, 479) and across to x 600, where `gl1` shows the
  water over the clear colour instead of over the pool floor (the
  floor polygon crosses the bottom edge and is dropped); `soft` and
  `gl1-metal` show the floor through the water everywhere.
- `skywater` (water) 3 and 4, under water: the screen blend. With
  `cl_blend 0` gl1 still darkens the screen under water (the blend's
  alpha stays, see "The screen flash and `hud2d`"), and Apple's
  renderer draws that quad as a wedge: inside it `gl1` and `gl1-metal`
  agree (mean colour ratio 1.000), outside it `gl1-metal` is 0.590 to
  0.605 of `gl1` in every channel, which is the blend darkening the
  whole screen as `soft` does. Frame 3 also has dark triangles of
  dropped polygons in `gl1`.

Measured where the reference is sound, with framediff's score and bad
fraction restricted to the painted pixels of the region (a scratch
script that reproduces framediff's numbers on the whole frame: 0.001798
and 0.012907 on `world` 21; checked on a rectangle against `framediff
--block`, the other pixels blanked in both frames: the same largest
difference 7 and bad 0, score 0.000002 of the whole frame against
0.000021 of the rectangle, which is 0.11 of it):

| Frame | Region | Pixels | Score | Largest difference | Bad |
|---|---|---|---|---|---|
| `world` 21 | sky | 57675 | 0.000070 | 1 | 0 |
| `world` 21 | translucent surface | 24265 | 0.000700 | 12 | 0.000659 |
| `world` 22 | sky | 569 | 0.001126 | 131 | 0.005272 (the 3 edge pixels) |
| water 0, 1, 2 | the surface left of the dropped floor (x 0 to 79) | 15619 | 0.000127 to 0.000128 | 10, 14, 8 | 0.000704, 0.001345, 0 |
| water 4 | the surface inside `gl1`'s blend wedge (x 180 to 519, y 0 to 99) | 34000 | 0.000021 | 7 | 0 |
| water 3 | none: all of the surface lies outside the wedge | | | | |

So on this host S4 cannot pass against `gl1` at the defaults for the
same reasons as `world` in TASK49; an accelerated reference (TASK83)
decides it.

### The `models` scenario against the software renderer (TASK51)

The shim needed no new code for models, the view weapon, flash blobs or
stencil shadows (design section 12). Measured from one private store on
the build VM: `gl1` and `soft` built from `d1c2ed53` (main), `gl1-metal`
from `260bdf4b` (the TASK51 shim), plus one `gl1-metal` record of main
and one of an `ASAN=1 UBSAN=1` build. Besides `models` itself, three
scratch copies of it were recorded (not committed, like TASK49's):
`r_shadows 1` with `gl1_stencilshadow 0` ("shadow"), the same with
`gl1_stencilshadow 1` ("stencil"), and `cl_lights 1` with
`gl1_flashblend 1` ("flash"). Two `gl1` records of each (three of
"flash"), one `soft`, three `gl1-metal`. 1-minute load: records 3.6 to
21.7 (`gl1` and `soft` 7.7 to 21.7, `gl1-metal` 3.6 to 9.7, ASAN 5.6 to
7.5), compares 7.0 to 8.5. Every count below is recomputed from the
per-frame lines of `compare` at the defaults.

**Logs.** No "not implemented" line in any `gl1-metal` record (main,
TASK51, ASAN; all four scenarios), no `glError` line in the ASAN records
(gl1 checks `glGetError` after each draw in a DEBUG build), no
sanitizer line, every record 707 frames, none skipped, none past the
vertex ring.

**Run to run** (score, bad fraction; frames exact of 23):

| Scenario | `gl1-metal`/`gl1-metal` (3 pairs) | `gl1`/`gl1` | ASAN against release `gl1-metal` |
|---|---|---|---|
| `models` | 22 exact in each; frame 7 only, worst 0.000297, 0.000436 | 1 pair: 21 exact, worst 0.000615, 0.004583 (frame 0) | 22 exact, worst 0.000062, 0.000068 |
| shadow | 22 exact in each; frame 7, worst 0.000664, 0.000859 | 1 pair: 22 exact, 0.000068, 0.000098 | worst 0.000504, 0.000863 |
| stencil | 22, 23 and 22 exact; frame 7, worst 0.000761, 0.000967 | 1 pair: 22 exact, 0.000329, 0.000456 | worst 0.000193, 0.000244 |
| flash | 14 to 15 exact, 1 to 3 over (frames 8, 18, 20), worst 0.004754, 0.083018 | 3 pairs: 1 over in each (frame 8 or 10), 12 exact in the first, worst 0.004986, 0.018848 | 2 over, worst 0.001968, 0.007363 |

Frame 7 of the three model scenarios holds an explosion sprite whose
frame comes from the engine's PRNG (`models.cfg`). In "flash" the blobs
and the dynamic lighting of models follow the lights' timing and differ
on the same frames in both renderers (frame 8: 5,790 pixels between two
`gl1` records, 457 between two `gl1-metal` records; frame 20: 4,124 in
`gl1-metal`); the mean luminance of a lit enemy on frame 10 is 21.3 to
27.0 over the three `gl1` records and 23.3 to 28.6 over the four
`gl1-metal` ones, so the "flash" noise is the scenario's, not a
renderer's.

**Against `gl1`** (each of 3 `gl1-metal` records against each of 2 `gl1`
records, 6 pairs): `models`, shadow and stencil identical in all pairs:
0 exact, 6 passing (frames 9, 12, 17, 18, 19, 20), 17 over, worst score
0.031575 and bad 0.676068 (frame 8; shadow and stencil 0.031572,
0.675843 and 0.675830). Flash: 0 exact, 4 to 6 passing, 17 to 19 over,
worst 0.037705 and 0.740062. For scale: `soft` against `gl1` 0 of 23
passing, worst 0.090731 and 0.925937; `gl1-metal` against `soft` the
same, worst 0.086572 and 0.925459.

**Three-way look** (`gl1`, `soft`, `gl1-metal`, first record of each,
same capture index; sheets downscaled in scratch only). Regions: 8-connected
regions of 150 pixels or more where `gl1` and `gl1-metal` differ by more
than 8. "Entity mask": the pixels where a renderer's frame differs from
its own `world` frame (no entities, no gun), in either renderer: models,
the weapon, sprites, beams, blobs, shadows, and brush entities (doors,
lifts, panels). Odd one out per region by its contrast against a ring
of 6 pixels around it (mean luminance inside minus outside, which takes
out `soft`'s different overall lighting): the renderer whose contrast is
at least twice as far from `soft`'s as the other's; else no witness.
`soft` draws no screen flash and no flash blob, and draws the view
weapon in another place and shape, so it is no witness on the flash
frames (3, 5, 8, 11, 16), for blobs, or along the weapon's edges.

- `models`: 118 regions (471,561 pixels) in 22 frames: `gl1` odd 52,
  `gl1-metal` odd 24, no witness 42. Of the 24, 19 lie on the flash
  frames 5, 8 and 16, where `gl1-metal` darkens the whole screen and
  the reference a wedge (TASK49; `soft` no witness). Two (frame 2,
  299,435 to 333,458; frame 7, 462,0 to 517,22) are outside the entity
  mask and differ identically between the two renderers' `world`
  frames: the world path, judged with `world` by TASK83. Two (frame 13,
  0,1 to 44,7 and 92,331 to 106,358) lie inside the triangle that `gl1`
  draws garbled over the close brush panel of frame 13 (the upper left
  half of the screen; `soft` and `gl1-metal` draw the panel alike). One
  is in the view weapon: frame 19, 589,461 to 638,479, 354 pixels, a
  straight-edged sliver along the bottom edge where `gl1-metal` is
  darker (contrast -14.7 against 9.3 in `gl1` and 7.2 in `soft`); `soft`
  draws its weapon elsewhere there, so its agreement with `gl1` is
  chance, and the shape is that of the other bottom-edge slivers where
  `soft` is a witness and `gl1` is the odd one (frame 0, 488,431 to
  556,479: `gl1` leaves the floor black under the gun; frame 10, 295,416
  to 326,479: `gl1` drops a bright weapon triangle that `soft` and
  `gl1-metal` draw). Review decided it as a fault of the reference, not of the
  shim (see the conclusion below and design section 12, "Frame 19:
  decided").
- shadow and stencil: the same regions as `models`, plus in shadow one
  at frame 7 (203,158 to 220,182), the PRNG sprite of that frame.
  Stencil adds no region: the stencil changes 574 pixels in `gl1-metal`
  and 635 in `gl1` (design section 12).
- flash: 115 regions, `gl1` odd 53, `gl1-metal` odd 20, no witness 42;
  outside the flash frames the `gl1-metal`-odd ones are frame 2 and 7
  (world, as above), frame 13 (the garbled panel, as above), frame 10
  (the weapon triangle that `gl1` drops, as above), and frame 14,
  265,395 to 414,467, 2,165 pixels: the floor behind the weapon, which
  differs in the `world` frames too (3,024 pixels of that box), the
  floor-at-the-bottom-edge difference TASK49 recorded on frame 14.

**Entity regions where the reference is sound** (the entity mask minus
the `gl1`-odd and no-witness regions, the flash frames left out, 18
frames, pooled over their pixels): `models` 592,614 pixels, score
0.000698, bad fraction 0.006972; stencil 599,936, 0.000712, 0.006957;
shadow 602,564, 0.001044, 0.007813; flash 654,635, 0.001611, 0.010667.
Per frame on `models`: score 0.000111 to 0.008249, bad 0.000357 to
0.129759 (the worst is frame 22, whose mask is 1,287 pixels).

**Luminance** of those pixels, `gl1-metal` over `gl1`: pooled 0.9995
(`models`), 0.9994 (stencil), 1.0014 (shadow), 1.0004 (flash); per
frame on `models` 0.9855 to 1.0199. No systematic offset, so no sign of
a shading or colour-array error. On the flash frames the ratio is 0.77
to 0.89, the whole-screen blend against the wedge.

So `compare gl1-metal models --ref gl1` cannot pass at the defaults
against these references, with or without `gl1_stencilshadow 1`; no
threshold or scenario was changed. TASK83 (#187) judges `models` again
against references from an accelerated OpenGL.

Frame 19 is decided: the reference is wrong and the shim is right. In
589,461 to 638,479 `gl1` draws 328 pixels that `gl1-metal` does not and
none the other way; three witnesses say no weapon geometry is there:
`gl1` at `r_gunfov 100` (the spot is then away from every screen edge)
covers 16 of the 328, `gl1-metal` at `r_gunfov 100` covers 6, and `soft`
at `r_gunfov 96.379` (the value that matches the weapon projection of
`gl1`; `soft` reads the cvar as a horizontal field of view, `gl1` as a
vertical one) covers 19. Frames 18, 20 and 21 have no differing pixel
there, and on frame 17 the reference drops a piece of the weapon instead
(81 pixels the other way, 78 confirmed by `gl1` at fov 100).

### Particles against the software renderer (TASK52)

Which path `particles` exercises: `R_DrawParticles` with `GL_POINTS`
(`gl1_pointparameters` defaults to 1 and the engine log says `Point
parameters: Okay`), smooth points (`gl1_particle_square 0`). Two
scratch copies of the scenario (not committed) cover the rest:
`gl1_pointparameters 0` (`R_DrawParticles2`, textured triangles; log
`Point parameters: Disabled`) and `gl1_particle_square 1` (square
points). A scratch build that logged each draw by kind (not committed)
found points on captured frames 2 to 21 (6 to 852 a frame; 13 and 15
draw 13 and 6), the screen blend on frames 3, 5, 8, 11 and 16, and no
beam, sprite or line on any captured frame: `demo1.dm2` has none in
view, so beams and sprites are judged by the host test and the earlier
cards' code, not on frames.

Records of commit `0af48dd7` (the shim of this card; its gl1 is the
parent's): three `gl1`, three `soft` and three `gl1-metal` records of
each of the three scenarios, every log without a `not implemented`
line (`mgl: 707 frames ... 0 skipped`). 1-minute load 5.6 to 11.7 at
the records, 6.7 to 8.1 at the compares. All compares at the
`particles` values (`--threshold 0.02 --bad-threshold 0.25`), counts
recomputed from the per-frame lines:

| Scenario | `gl1-metal` run to run (3 pairs) | `gl1` run to run | Against `gl1` (9 pairs) |
|---|---|---|---|
| `particles` | 21 exact, 48 passing, 0 over; worst score 0.011813, bad 0.155348 | 14, 55, 0; worst 0.013428, 0.174443 | 0 exact, 162 passing, 45 over |
| `gl1_pointparameters 0` | 21, 48, 0; worst 0.009918, 0.184743 | 15, 54, 0; worst 0.009547, 0.168740 | 0, 162, 45 |
| `gl1_particle_square 1` | 21, 48, 0; worst 0.011276, 0.189528 | 15, 54, 0; worst 0.011487, 0.145524 | 0, 162, 45 |

`soft` run to run: worst 0.012270 and 0.180264 (`particles`). So the
shim's noise is the scenario's, inside the 0.25 the scenario carries.
Against `gl1` every one of the 27 pairs has the same 5 frames over,
5, 8, 11, 13 and 16, with nearly the same numbers in each (`particles`:
frame 5 score 0.131861 to 0.133294, bad 0.918815 to 0.918942; 8: 0.070971
to 0.079575, 0.929056 to 0.932077; 11: 0.038291 to 0.038393, 0.937790
to 0.938587; 13: 0.024436, 0.325430; 16: 0.069967 to 0.072065,
0.927445 to 0.930960). All five are faults of the reference, looked at
three ways (`gl1`, `soft`, `gl1-metal`, the same capture, downscaled in
scratch): on 5, 8, 11 and 16 the screen blend darkens and tints the
whole screen in `soft` and `gl1-metal` and a diagonal wedge in `gl1`
("The screen flash and `hud2d`"); frame 3 has the faintest blend
(alpha 0.02) and passes (0.006546, 0.012507). On 13 the upper left of
`gl1` shows a wrong wall over a triangle reaching the screen edge (the
clipping fault of TASK49), where `soft` and `gl1-metal` agree. At the
defaults (0.02, 0.005) 9 of 207 frames pass, frame 9 in every pair;
the others carry the world faults of TASK49 or particles, and `gl1`
against itself passes only 38 of 69 there. The inventory asked for `R_DrawParticles2` at the defaults:
it cannot be judged so here, because `gl1` against `gl1` fails them
too (bad up to 0.168740 with `gl1_pointparameters 0`).

The particles themselves, judged by blobs, not positions (the
positions are random per run): the region is the pixels more than
8/255 from the same frame of a record with `cl_particles 0` of the same
renderer. On the particle frames without a blend that show particles
(7, 10, 12, 14, 17 to 21), summed per record, `gl1-metal` against `gl1`
over the 9 pairs: coverage ratio 0.706 to 1.941, mean luminance ratio
0.795 to 1.230; `gl1` against itself 0.614 to 1.720 and 0.911 to 1.319,
so the spread is the scenario's. Pooled over the three records each,
coverage 0.7923 against 0.7629 (ratio 1.039) and luminance 34.22
against 34.71 (0.986). Frame 12, the densest (781 points), as means of
three records: coverage 0.1043 (`gl1`), 0.1031 (`gl1-metal`), 0.0794
(`soft`); mean RGB of the region 78.0, 9.1, 0.4 against 77.3, 8.8, 0.4
(`soft` 83.8, 7.7, 0.3); 71, 65 and 79 blobs. Zoomed, `gl1` and
`gl1-metal` both draw round soft-edged discs of the same sizes; `soft`
draws square blocks. No property where `gl1-metal` stands alone was
found.

Lines: a scratch copy with `gl_showtris 1`, one record each: the
outlines of `gl1-metal` lie on `gl1`'s; 18 of 23 frames pass at the
`particles` values, and the 5 over are the same 5 reference faults. An `ASAN=1 UBSAN=1` build recorded the three
scenarios with no sanitizer line.

No threshold or scenario was changed. On this host `particles` cannot
pass against `gl1` for any renderer that draws the blend over the whole
screen; TASK83 (#187) judges it against an accelerated reference.

### Closing TASK21 on the build host

The umbrella card's acceptance list, checked end to end after S1 to S6
on the build VM, with the shim of commit `0c4d6aa6` (this card's; it
adds the capability report and the command-buffer error report to the
merged shim). The Metal device is `Apple Paravirtual device`. On this
host the `gl1` references come from Apple Software Renderer ("`gl1` on
the build host (TASK80)"), so "every pin passes against `gl1`" is
pending TASK83 (#187) for the 3D scenarios; the 2D scenarios, `hud2d`
and `cinematic`, pass. No threshold or scenario was changed.

**Coverage.** `tests/metal/shim-coverage.sh`, run by
`tests/check-metal-shim.sh`, derives the 59 entry points (54 called
directly, 5 loaded by pointer under 10 names) and checks that the shim's
objects define each one, that the linked `ref_gl1.dylib` leaves no `_gl`
symbol undefined, and that the table of `docs/plans/metal-shim-design.md`
section 14 names exactly those 59: 50 implemented, 5 partial
(`glClear`, `glDrawElements`, `glPolygonMode`, `glTexEnvi`,
`glTexImage2D`), 3 no-op (`glDrawBuffer`, `glHint`, `glShadeModel`: `GL_FLAT` is
stored and not applied), 1 stub
(`glColorTableEXT`, which gl1 never loads because the shim does not
advertise `GL_EXT_shared_texture_palette`). Each of 15 mutants of its
inputs (a row removed, renamed, duplicated or given an unknown status,
the table markers removed, a proc-table entry or alias removed, a new
`gl` call without a row, a call removed from the source while the
objects still import it, a shim object missing, a dylib with undefined
`_gl` symbols, missing objects or dylib, the identifier test dropped
from the source scan) fails it, and each of the three guard lines
removed lets its bad input through. No run below logged a `not
implemented` line, and the `ASAN=1` build, whose gl1 checks
`glGetError` after its calls (`glCheckError_`, `DEBUG` only), logged no
`glError` line in any scenario: no refused or stubbed value is reached
by the traces.

**gl1's C is untouched.** Against the fork point (`9dbf6992`, the last
upstream merge), `git diff --stat 9dbf6992 HEAD --
src/client/refresh/gl1 ':(exclude)src/client/refresh/gl1/metal'` lists
11 files, all changed by TASK5, TASK13, TASK27, FIX25, TASK76 and TASK80
and none by a shim card (`git log --format=%s 9dbf6992..HEAD` on the
same paths has no TASK47 to TASK52 or TASK82 subject). Not even the
context seat changed: `shim_sdl.h`, prepended with `-include` to gl1's
files in a `WITH_METAL_SHIM=yes` build, maps the SDL OpenGL calls of
`gl1_sdl.c` onto the shim, and the include path puts the shim's
`<OpenGL/gl.h>` first. `grep -rniwE 'metal|mgl_[a-z_]*'` over gl1's `.c`
files and `header/` finds nothing.

**Validation.** Every record ran with `MTL_DEBUG_LAYER=1` (the log
says `Metal API Validation Enabled`). A positive control, a scratch
program that blits past the end of a buffer, aborts under it with
`failed assertion` on stderr, which a record keeps in `engine.log`, so
a validation failure would also end the record incomplete.
`MTL_SHADER_VALIDATION=1` does not work on this VM: under it a scratch
program completes a three-vertex draw whose positions come from the
vertex id, but hangs the GPU (`Caused GPU Hang Error`, then `Ignored
(for causing prior/excessive GPU errors)` for every later command
buffer) as soon as the draw reads a device buffer or samples a
texture; every variant completes with the variable unset. Under
it the shim's first records (commit `39a4ceec`) captured frames of
noise without a word in the log; `hud2d` scored 0.47 and bad 1.0
against `gl1` on every frame, and exact with the variable unset. That
silence was a shim defect: a command buffer that ends in error is now
logged (`mgl: a command buffer failed: ...`, the first eight) and
counted at teardown (`mgl: N command buffers failed`); the same run
then logs the hang and 736 failed command buffers. With
`MTL_DEBUG_LAYER=1` alone, at `0c4d6aa6`, three release records of each
of the six scenario runs (`skywater` from `demo1.dm2`; `cinematic` with
`PINS_DEMO=idlog.cin PINS_EVERY=7`), one of each with an `ASAN=1` build
(`PINS_BIN`), and one each of two scratch copies (not committed:
`models` with `r_shadows 1`, `gl1_stencilshadow 1` and `cl_lights 1`;
`world` with `gl1_multitexture 0`): 26 records, no validation message,
no sanitizer line, no failed command buffer, and the shim's counters
`707 frames, 0 without a drawable, 0 skipped, 0 draws past the vertex
ring` (`cinematic` 713 to 718 frames; 2454 in the `ASAN=1` build). The
1-minute load was 3.5 to 11.7 during the records. `skywater` from
`water.dm2` was not run: its pin id carries the checksum of a demo made
by `make-demo water`, and no reference of this store matches a new one.

**`timedemo`.** Every scenario is a `timedemo 1` run of `demo1.dm2`;
`world` is the stock one. The release records' lines read `689 frames,
5.7 to 5.8 seconds: 119.0 to 120.2 fps` (the `world` copy with
`gl1_multitexture 0`: 6.9 seconds, 100.2 fps; the `ASAN=1` build's
`world`: 3.5 seconds, 199.4 fps). Every record completed and quit by
itself with status 0.

**Frames against `gl1`.** Each of the three release records of a
scenario against the newest `gl1` reference of the owner's store with
the same settings hash, copied into a private store (commits
`92302e46596c` for `world` and `skywater`, `74205ac99199` for `hud2d`,
`110a25ae2d26` for the others). The three records gave the same verdicts
on the same frames:

| Scenario | Accept at | Frames passing in each of 3 records | Worst score, bad |
|---|---|---|---|
| `hud2d` | defaults | 23 of 23, all exact | 0, 0 |
| `cinematic` | defaults | 12 of 12 | 0.000191, 0 |
| `world` | defaults | 7 of 23 (over: 0 to 8, 10, 11, 14 to 16, 21, 22) | 0.025886, 0.632692 |
| `skywater`, `demo1.dm2` | defaults | 7 of 23, the same frames | 0.025886, 0.632692 |
| `models` | defaults | 6 of 23 (`world`'s and 13) | 0.031575, 0.676068 |
| `particles` | 0.02, 0.25 | 18 of 23 (over: 5, 8, 11, 13, 16) | 0.131757 to 0.132777, 0.938193 to 0.938249 |

These are the counts TASK49 to TASK52 recorded, whose faults are the
reference's; no frame changed verdict.

**Startup capability report.** Each context creation logs to the console
(and so to `engine.log`): `Metal device: Apple Paravirtual device`,
`Metal supportsFamily (apple5 to apple9): apple5`, `Metal
supportsRaytracing: no, supportsRaytracingFromRender: no`. The device
answers only the apple5 floor.

**`vid_restart` between `gl1` (shim) and `soft`.** A console script in
the build directory (not committed) loads `base1`, then switches `soft`,
`gl1`, `soft`, ... with `vid_restart` and about 1.5 seconds of frames
after each, and quits. Under the `ASAN=1` build with
`MTL_DEBUG_LAYER=1`, four cycles (four restarts each way): status 0, no
sanitizer line, five shim teardowns (`mgl: 283`, 129, 131, 133, 132
frames). Under the release build with `MallocStackLogging=1` and
`leaks --atExit`:

| Cycles | Leaks | Live at exit | Physical footprint at exit |
|---|---|---|---|
| 1 | 0 | 40500 nodes, 7431 KB | 75.4 MB |
| 2 | 0 | 41647, 7943 KB | 84.7 MB |
| 4 | 0 | 44082, 8967 KB | 96.0 MB |
| 8 | 0 | 48625, 10960 KB | 118.2 MB |

No leak, and no crash, but the live heap grows by about 1140 to 1220
nodes and 500 KB per cycle, and the footprint by 5.6 MB per cycle after
the second (9.3 MB from the first to the second); 1-minute load 9.1 to
13.6. The memory graphs after 2 and after 8 cycles (`leaks
--outputGraph`, then `heap`) say what is kept: every window the engine
ever opened (`CGSWindow` 5 and 17: one per restart, for both renderers)
with its display link, `CAContext` and layers, and with each `gl1`
window its `CAMetalLayer` and metal view (2 and 8) and the four
IOSurface-backed textures of the layer's drawables (8 and 32
`MTLDebugTexture`). None of the shim's own objects grows: at exit there
is no `MTLBuffer`, pipeline, sampler or depth-stencil state or library,
no texture beyond the drawables, and one command queue after 2 cycles as
after 8. The shim's teardown
frees what it made; the windows are kept outside it (`@autoreleasepool
content` grows by 295 KB over the six cycles, which suggests window
objects autoreleased into a pool the main loop never drains; not
proven). So "`vid_restart` without leaks" holds for the shim and not
for the process; the window retention is a finding for a card outside
the port queue: FIX41 (#193). FIX41 has since proven the causes and
corrected one point of this paragraph: the kept `CAMetalLayer`s and metal
views belonged to the `soft` windows, not the `gl1` ones (see
"`vid_restart` and the main thread's autorelease pool (FIX41)" below).

**Criterion 1 and the GL-call pins.** The card's first criterion asks
that every GL-call pin of the reference set pass. It is met here by the
coverage report, the host test (`tests/metal/mgl_host_test.c`) and the
frame pins above. No GL-call recorder, trace or GL-call pin was built or
run in this repository, so the GL-call-pin half of the criterion is not
met as worded. The enum values the section 14 table names as refused
come from reading the source and from the runs that logged no `not
implemented` or `glError` line, not from a recorded trace.

**Deviations from the card.** The shim lives in
`src/client/refresh/gl1/metal/`, not in the suggested
`src/client/refresh/glmetal/`, which keeps it next to the one renderer
it serves. It was written in this tree, not seeded from any external
seed. Nothing was promoted elsewhere.

### The `ref_metal` skeleton (TASK84, N1)

`ref_metal` (design: `docs/plans/ref-metal-native.md`, as built in its
section 16) is recorded under its own name, with no change to
`scripts/pins.sh`:

```sh
make client game ref_metal
MTL_DEBUG_LAYER=1 scripts/pins.sh record metal hud2d
```

At this stage it clears and presents and draws nothing, so a `hud2d`
record is 23 frames of the clear colour (255, 0, 128 with the
scenario's `r_clear 1`); nothing is compared against `gl1-metal` yet. A
record is accepted when it completes, every frame is that colour (a
scratch script over the TGAs, not committed), `engine.log` holds the
three capability lines (`Metal device:`, `Metal supportsFamily (apple5
to apple9):`, `Metal supportsRaytracing:`) and no `ref_metal: a command
buffer failed` line, and its last `ref_metal: teardown:` line reports
`0 live objects` and `0 unreleased`.

On the build VM, commit `0da03fd3`, load averages 7.9 to 12.2: three
release records and one `ASAN=1` record (`PINS_BIN=debug/quake2`), all
with `MTL_DEBUG_LAYER=1`, each 23 frames, every pixel 255, 0, 128,
status 0, no validation message beyond `Metal API Validation Enabled`,
no failed command buffer, no sanitizer line; timedemo 2590 to 2883 fps
(release), 1837 fps (`ASAN=1`).

The restart check is a config file in the build's `baseq2/` (not
committed) that sets `metal_stats 1`, takes a `screenshot`, and runs
`set vid_renderer soft; vid_restart` and `set vid_renderer metal;
vid_restart` four times with ten `wait`s between steps, then `quit`;
the engine is started with `vid_renderer metal`, `r_mode -1` at
640x480 and `s_initsound 0`. Two runs with the `ASAN=1` build and the
validation layer: status 0, no sanitizer line, five `ref_metal`
sessions each, every teardown `0 live objects ... 0 unreleased, 0
unmatched`. Window retention across `vid_restart` outside the module is
FIX41's (#193) and is not measured here.

With `ref_metal.dylib` missing, `vid_renderer metal` falls back through
the existing order (`gl3`, `gl1`, `soft`); while capturing, the engine
refuses the fallback as for every renderer.

### Images, 2D and the cinematic in `ref_metal` (TASK85, N2)

N2 is the first card judged against the shim on content
(`docs/plans/ref-metal-native.md`, section 17). The references are
`gl1-metal` records made at the card's base commit. `ref_metal.dylib` and
the shim build of `ref_gl1.dylib` are separate files in `release/`, so
both stay built side by side (a `make ref_gl1` without
`WITH_METAL_SHIM=yes` replaces the shim build):

```sh
make client game ref_metal && make ref_gl1 WITH_METAL_SHIM=yes
MTL_DEBUG_LAYER=1 PINS_DEMO=idlog.cin PINS_EVERY=7 scripts/pins.sh record gl1-metal cinematic   # at the base commit
MTL_DEBUG_LAYER=1 PINS_DEMO=idlog.cin PINS_EVERY=7 scripts/pins.sh record metal cinematic
PINS_DEMO=idlog.cin PINS_EVERY=7 scripts/pins.sh compare metal cinematic --ref gl1-metal \
    --ref-id <base id> --threshold 0.005 --bad-threshold 0.001
```

The `compare` needs the same `PINS_DEMO` and `PINS_EVERY` as the
records, since they enter the settings hash of the pin id. A record is
accepted when it completes, `engine.log` holds no validation message
beyond `Metal API Validation Enabled` and no `a command buffer failed`
line, and the last teardown line reports `0 live objects` and
`0 unreleased`.

Moving code the shim links (here `common/` of `ref_metal`) is proved
neutral by `gl1-metal` records of `hud2d`, `cinematic` and `world` from
before and after the change, compared with `--ref-id <before>
--cand-id <after> --threshold 0 --bad-threshold 0`, and by a byte
comparison of the TGAs (a scratch script over the two directories, not
committed). Rebuild before recording after a header edit or a rebase:
`scripts/pins.sh` refuses a binary or module older than a header under
`src/`, and the shim's objects carry no header dependencies, so remove
`build/release/ref_gl1_metal/` once after a header the shim includes
moved (`make -B` of the client, `ref_metal` and the shim build is the
plain way).

`metal_stats 1` did not reach a record: with `set metal_stats 1` in
`release/baseq2/autoexec.cfg` a record printed no `metal_stats` line, so
the setting is not taken from there while capturing (the scenario comes
on the command line). The per-frame counters were read from
the engine started by hand with the record's command line
(`PINS_DRYRUN=1` prints it) plus `+set metal_stats 1` and a scratch
`pins_dir`; each captured frame prints one `metal_stats` line.

On the build VM, base `4175e8cc` (references at `beb6bb9e`, the same
`src/`), head `11dfcbff`, load averages 7.7 to 31.9: three release
records and one `ASAN=1` record of `metal cinematic` each 12 frames ok
at score 0 and bad 0, every frame byte-identical to `gl1-metal`; the
`gl1-metal` records after the move byte-identical to those before it on
all three scenarios; every captured frame `0 created after init`. The
`hud2d` frames stay the `r_clear` colour until N3 draws the pics.

### Pics, characters and the HUD in `ref_metal` (TASK86, N3)

N3 is judged on `hud2d`, the first non-uniform scenario compared with the
shim, with `cinematic` as the regression. The references are `gl1-metal`
records made at the card's own commit (the 2D code is `ref_metal`'s;
nothing the shim links changed). Both modules stay built side by side;
a private store keeps the 1 GB traces out of the owner's:

```sh
make client game ref_metal && make ref_gl1 WITH_METAL_SHIM=yes
export PINS_STORE=<private store> PINS_BASEDIR=<game data>
MTL_DEBUG_LAYER=1 scripts/pins.sh record gl1-metal hud2d
MTL_DEBUG_LAYER=1 scripts/pins.sh record metal hud2d [--run 2]
scripts/pins.sh compare metal hud2d --ref gl1-metal [--run 2] --threshold 0.005 --bad-threshold 0.001
# cinematic as in N2: PINS_DEMO=idlog.cin PINS_EVERY=7, the same three commands
make ASAN=1 client ref_metal game
PINS_BIN=debug/quake2 PINS_TRACE=0 MTL_DEBUG_LAYER=1 scripts/pins.sh record metal hud2d
scripts/pins.sh compare metal hud2d --ref gl1-metal --cand-id <the ASAN record's id> ...
```

The `cinematic` trace is 377 MB from `gl1-metal` and 738 MB from `metal`:
delete the `trace.q2r` files when the records are accepted (`PINS_TRACE=0`
records none; the pins need no trace to compare). The `ASAN=1` pin id
carries the binary's checksum (`-b...`), so its compare names it with
`--cand-id`. `metal_stats 1` is read from the engine started by hand with
`PINS_DRYRUN=1`'s command line plus `+set metal_stats 1` and a scratch
`pins_dir`, as in N2: each captured frame prints one line, and the
accepted ones read `12 pipelines, 0 created after init`.

A record is accepted on the conditions of N2. `hud2d` draws the status
bar, the crosshair and the numbers over the `r_clear` colour, so a frame
that is one colour (N1's) or whose HUD sits in the wrong row order shows
as a large score; a frame differing in 2 of 23 appears when the batched
quads are not drawn before the frame ends (the mutant `RM_Flush2D`
removed from `EndFrame`: worst score 0.0134, worst bad 0.0315).

On the build VM, source `308d8073`, load averages 17.7 to 26.1: three
release records and one `ASAN=1` record of `metal hud2d`, each 23 frames
ok at score 0 and bad 0; `metal cinematic` 12 frames ok at score 0 and
bad 0 (one record); four `hud2d` logs holding only `Metal API Validation
Enabled` and `0 live objects`, `0 unreleased`; every captured `hud2d`
frame `0 created after init`.

### Closing TASK27 on the build host

The umbrella card's acceptance list, checked end to end after P1 to P3
(TASK39 to TASK41) and R1 to R5 (TASK42 to TASK46), at engine commit
`4175e8cc` on the build VM (macOS arm64). `gl1` there is Apple Software
Renderer (`GL_RENDERER` in every `gl1` `engine.log`); `gl1-metal` is the
shim on `Apple Paravirtual device`. Every record, replay and compare below
ran in a private store; the owner's store was only read (`pins.sh list` and
reading its files), nothing was recorded, replayed or written there. The
1-minute load average was 6.5 to 59 over the builds and runs (other work
shared the machine). No threshold, scenario or source file was changed.

**Criterion 1: a renderer replaying its own records reproduces its own
frames bit-identically.** Met with a limit: replay is exact on all three
renderers; the `soft` references of `models`, `particles` and `skywater`
from `demo1.dm2` in the owner's store are at an older settings hash, and
recording new ones is the owner's step (commands given below). For each renderer and scenario run: one live
record (`record <r> <scenario>`), one replay of that record's own trace
(`replay <r> <scenario> --ref-id <record>`), and `compare <r> <scenario>
--cand-id <replay> --ref <r> --ref-id <record> --threshold 0
--bad-threshold 0`. No `PINS_SEED`: the replay draws what the trace holds,
so `models` and `particles` are exact without it. `skywater` from
`water.dm2` uses a demo made with `make-demo water` in this checkout (98
messages), so its pin id is not the owner's.

| Scenario | Frames | `soft` | `gl1-metal` | `gl1` (Apple Software Renderer) |
|---|---|---|---|---|
| `world` | 23 | 0 differ, worst 0 | 0 differ, worst 0 | 0 differ, worst 0 |
| `hud2d` | 23 | 0, 0 | 0, 0 | 0, 0 |
| `models` | 23 | 0, 0 | 0, 0 | 0, 0 |
| `particles` | 23 | 0, 0 | 0, 0 | 0, 0 |
| `skywater`, `demo1.dm2` | 23 | 0, 0 | 0, 0 | 0, 0 |
| `skywater`, `water.dm2` | 4 | 0, 0 | 0, 0 | 0, 0 |
| `cinematic`, `idlog.cin`, every 7th | 12 | 0, 0 | 0, 0 | 0, 0 |

21 replays, all complete with exactly the live record's frame count, and
every compare exited 0 with `failed=0 worst=0 worst_bad=0`. A record took 2
to 9 s on `soft` and `gl1-metal` and 2 to 18 s on `gl1`, a replay 2 to 10 s
and 3 to 15 s (load 9.9 to 31.1 during these runs). The build was the
default one, so every replay manifest says `game-module present`; the
absent case is TASK46's measurement (`doc/100_renderpins.md`, *Replaying a
trace*), not repeated here.

The head's `soft` frames against the owner's `soft` references, copied into
the private store (`compare --ref-id`, both thresholds 0): `world`
(reference `790c2212f654`), `hud2d` (`74205ac99199`), `cinematic`
(`f40e8ae864e4`) and `skywater` from `demo1.dm2` (`f40e8ae864e4`, recorded
before TASK70 changed the scenario file) exact on every frame; `models`
(`e30ea0e1e161`) 1 of 23 frames differs, score 0.000036, and `particles`
(`e30ea0e1e161`) 16 of 23, worst score 0.011378 and bad fraction 0.156693,
the unseeded run-to-run noise these two scenarios are recorded with, inside
their `// nondeterministic:` thresholds.

**The `ref_soft` references in the owner's store.** Complete live records,
per scenario, and how many carry the settings hash the head computes (the
hash covers the whole scenario file, comment lines included, and the
`PINS_*` settings; a record of another hash is still comparable with
`--ref-id`, but `compare` without it may pick a record of other settings):

| Scenario | `soft`: complete, at the head's hash (commits) | `gl1`: complete, at the head's hash (commits) | `gl1-metal` |
|---|---|---|---|
| `world` | 19; 19 (11 commits, newest `92302e46596c` with uncommitted changes, `790c2212f654` the newest clean) | 2; 2 (`110a25ae2d26`, `92302e46596c`) | 10 |
| `hud2d` | 12; 3 (`74205ac99199`) | 5; 4 (`74205ac99199`) | 14 |
| `models` | 8; 0 (newest `e30ea0e1e161`, before TASK45) | 1; 0 (`110a25ae2d26`, before TASK45) | 0 |
| `particles` | 8; 0 (newest `e30ea0e1e161`, before TASK45) | 1; 0 (`110a25ae2d26`, before TASK45) | 0 |
| `skywater`, `demo1.dm2` | 4 (`f40e8ae864e4`, `63fff16ad3ac`, both before TASK70); 0 at the head's hash | 2; 2 (`110a25ae2d26`, `92302e46596c`) | 5 |
| `skywater`, `water.dm2` | 2 (`92302e46596c`, the same demo as the `gl1` records of that commit) | 3 (2 of `92302e46596c`, 1 of `110a25ae2d26`, two makes of the demo) | 5 |
| `cinematic` | 8; 2 (`f40e8ae864e4`) | 1; 1 (`110a25ae2d26`) | 8 |

The other 14 complete `skywater` records of `soft` are of other demos,
other `PINS_EVERY` values or scenario files before TASK70 (TASK40 and
TASK70 measurements). No record there is of the head commit, as expected: references are made
once per engine commit. `models` and `particles` changed only in comment
lines in TASK45, and `skywater` in TASK70, so those references are of an
older settings hash; their frames still match the head's as shown above.
`skywater` from `water.dm2` depends on the demo file: the pin id carries its
checksum, and a demo made again (here: 98 messages) is another pin. So
"`ref_soft` references exist for every scenario" holds for the stored
records, with `models`, `particles` and `skywater` from `demo1.dm2` short of
one at the head's hash. That is the owner's step, in the owner's store with
the owner's game data and a default (not `WITH_METAL_SHIM`) build:

```sh
make client game ref_soft ref_gl1
for s in models particles skywater; do scripts/pins.sh record soft $s; done
for s in models particles; do scripts/pins.sh record gl1 $s; done
PINS_SEED=1 scripts/pins.sh record soft models    # optional: exact run to run
PINS_SEED=1 scripts/pins.sh record soft particles
```

with `PINS_STORE` and `PINS_BASEDIR` set to the owner's store and game
data.

**Criterion 2: `ref_gl1` references for every scenario, with the threshold
at which `soft` and `gl1` agree documented per scenario.** Met with a
limit. The owner's store holds a complete `gl1` record of every scenario
(table above), each with `GL_RENDERER: Apple Software Renderer`; `models`
and `particles` only at the settings hash before TASK45. Every scenario has
its row in the table of "`gl1` references and the `soft` against `gl1`
thresholds (TASK41, card P3)": a threshold for `cinematic` (`--threshold
0.02 --bad-threshold 0.35`) and for `skywater` from `water.dm2`
(`--threshold 0.034 --bad-threshold 0.68`), and for `world`, `hud2d`,
`models`, `particles` and `skywater` from `demo1.dm2` the documented
finding that no agreement threshold is meaningful, with the reasons. None
was missing, so none was measured or added. Re-measured at the head, live
`soft` against live `gl1` of the same commit, one record each: `world` and
`skywater` from `demo1.dm2` worst 0.081482 and 0.922676, `hud2d` 0.471755
and 0.998561, the documented worst values exactly; `models` 0.090756 and
0.925937 and `particles` 0.141069 and 0.997926, inside the documented
ranges; `cinematic` worst 0.018680 and 0.322956, 12 of 12 frames passing at
its threshold, the documented values exactly. `skywater` from this
checkout's `water.dm2` (98 messages) is over on 1 of 4 frames at its
threshold (frame 2, score 0.035741, bad fraction 0.812705; the other three
0.030220 to 0.031865, 0.537233 to 0.674004): that threshold was measured on
another make of the demo and holds for that demo, not for any water demo.
It was not changed; a new make of the demo needs its own measurement, and
re-measuring it on the accelerated host is noted on TASK83 (#187).

The limit: these are references for the shim's arithmetic, not for a
graphics card (INFORM9 asks for `gl1` where it renders). Accelerated `gl1`
references are TASK83 (#187).

**Criterion 3: nothing derived from game data is committed.** Met. `git
ls-files` lists 1874 files, 1434 under `third_party/` (vendored SDL 3,
byte-identical to its release, checked by `tests/check-vendored.sh`; no
image, sound or binary data file among them by name) and 440 outside it. Of
those 440, `file` calls five binary or image files, all from upstream yquake2 and
unchanged since the fork point (`git diff 9dbf6992 HEAD` on them is empty);
`file` also labels four JSON configuration files as data, which are text:

| File | Bytes | Type |
|---|---|---|
| `stuff/icon/Quake2.ico` | 162731 | Windows icon |
| `stuff/icon/Quake2.png` | 14043 | PNG, 512x512 |
| `stuff/icon/Quake2.svg` | 5167 | SVG |
| `stuff/models/crosshair/skin.pcx` | 4279 | PCX, 128x128 |
| `stuff/models/crosshair/tris.md2` | 1152 | MD2, 12 triangles |

No path added since the fork point (147 added paths outside `third_party/`,
counted by `git diff --name-only --diff-filter=A --no-renames 9dbf6992 HEAD`) ends in a frame, image, demo, cinematic, trace, pak, map, model,
sound or log extension, or is a `manifest` or `complete` file. The committed
test fixtures are text: the 12 unit-test pins in `tests/pins/unit/` (output
over paks and maps the suite generates itself) and the two lists in
`tests/baselines/`. No trace is committed at all: the synthetic frames,
fake engines and traces of the gate are generated at run time in a
`mktemp -d` directory (`pins.sh selftest`, `evidence-selftest`,
`tests/check-refrecord.sh`). `.gitignore` covers `/release/` (the binary,
`make-demo`'s `demos/`, the engine's `qconsole.log`), `/debug/`, `/build*/`
and `/evidence/`. The store has no ignore pattern because it cannot be in
the tree: `record`, `compare` and `replay` refuse a store inside this
repository or any git work tree, and frames, traces, manifests, engine logs
and the built comparator are written into the store only.

**Criterion 4: `scripts/test.sh` green with and without game data.** Met by
reading and by running the checks; the full gate is the orchestrator's run,
not repeated here. No `tests/check-*.sh` and not `scripts/test.sh` names
the game-data path, `PINS_BASEDIR` or a pin store, except:
`tests/check-pins.sh` and `tests/check-evidence.sh` run `pins.sh selftest`
and `evidence-selftest`, which unset every `PINS_*` variable first and set
`PINS_STORE` to their own scratch directory; `tests/check-pins-latch.sh`
unsets the `PINS_*` settings and passes `PINS_STORE=/s PINS_BASEDIR=/b`
with `PINS_DRYRUN=1` (no engine starts, nothing is read there). All 16
checks were run twice, with `PINS_BASEDIR` and `PINS_STORE` unset and with
both pointing at an empty scratch directory: all 16 exited 0 both times
(2 min 53 s and about 3 min, load 5 to 15), their outputs were identical
once scratch paths were removed, and both scratch directories were still
empty afterwards. So no check's behaviour depends on those variables. The
unit suite (`make test`) takes no `PINS_*` variable and generates its own
data.

**Verdicts.** 1 met with a limit (replay is exact on all three renderers;
the `soft` references of `models`, `particles` and `skywater` from
`demo1.dm2` are at an older settings hash, and recording new ones is the
owner's step, commands given above). 2 met with a limit: software OpenGL
only, accelerated references are TASK83 (#187); the `water.dm2` threshold
is tied to the demo it was measured on; re-measuring it on the accelerated
host is noted on TASK83 (#187). 3 met. 4 met as far as the checks
go; the full gate is the orchestrator's run. Still open from the card's
scope (now held by TASK94, #209), not its acceptance list: the pin store path from configuration
(the driver reads `PINS_STORE` only), a content hash tying a pin to its
scenario beyond the settings hash in its id, and a fixed `cl_maxfps` for
capture runs (neither the driver nor a scenario sets it; `timedemo 1` and
capture every Nth frame carry the determinism measured above).

### `vid_restart` and the main thread's autorelease pool (FIX41)

"Closing TASK21 on the build host" found every window kept across
`vid_restart`. Two things keep it, one ours and one AppKit's.

**Cause 1, ours: no autorelease pool on the main thread.** The engine's
main loop (`Qcommon_Mainloop`, `src/common/frame.c`) never ran inside a
pool on macOS, and nothing else drains one: the client has no Cocoa run
loop of its own. Run with `OBJC_DEBUG_MISSING_POOLS=YES`, two `soft`
restarts log 977 `autoreleased with no pool in place - just leaking`
lines; the 3 on the main thread are the 3 windows' `SDL3_cocoametalview`.
The other 974 come from 6 or 7 threads the engine does not start
(their objects are Metal, frame-pacing and collection ones) and stay after the fix (976 in the same
run, none on the main thread). `leaks --autoreleasePools`
on the memory graph at exit lists the main thread's pool page with no
pool boundary, holding one `SDL3_cocoametalview` per window. The view is
the one `SDL_Renderer`'s Metal backend makes for `soft`; `gl1` (shim) and
`metal` destroy their own views (`SDL_Metal_DestroyView`), so for them
alone no layer is kept, and in a `gl1`/`soft` mix the kept layers are
the `soft` windows'.

**Fix.** On Darwin the client's C files are compiled as Objective-C
(Makefile, `-x objective-c`), so `frame.c` uses `@autoreleasepool`
itself, under `#if defined(__APPLE__) && defined(__OBJC__)`: one around
each `Qcommon_Frame`, one around start-up (the body of `Qcommon_Init`,
now `Qcommon_Startup`), which is drained before the first frame. That
is the public construct; `objc_autoreleasePoolPush`/`Pop` are not
declared by the SDK's headers, and a separate `.m` file would need its
own Makefile rule. `q2ded`, the unit tests and the other platforms
compile `frame.c` as C and get the code they had. An `ERR_DROP` longjmp
lands inside `Qcommon_Frame` (its `setjmp`), inside the pool.
`tests/check-autorelease-pool.sh` decides from the objects: built with
the Makefile's rules, the client's `frame.o` has two branches to
`objc_autoreleasePoolPush` and two to `Pop`, `q2ded`'s none; each of
three mutants (frame pool removed: 1 and 1; start-up pool removed: 1
and 1; guard never true: 0 and 0) is refused. It skips off macOS.

**Cause 2, AppKit's: the close animation.** The remaining window object
at exit is the `NSCGSWindow` (the window server's side; the
`SDL3Window` itself is freed). `leaks --traceTree` on it shows it held
by `_NSWindowTransformAnimation._frozenWindows`, the order-out
animation `[NSWindow close]` (in SDL's `Cocoa_DestroyWindow`) starts,
which is held by its display-link block. None of these animations ends
in the run, though the first window has seconds of frames after its
close. A pool does not release it. A scratch build that also set
`animationBehavior` to `NSWindowAnimationBehaviorNone` on each new
window (through `SDL_PROP_WINDOW_COCOA_WINDOW_POINTER`, not committed)
kept no window: `soft`, 1 and 8 restarts, 0 `NSCGSWindow`, 1 metal view
(the start-up one, that build had no start-up pool), footprint 66.1 and
66.0 MB.

**Fix for cause 2.** `CreateSDLWindow` (`src/client/vid/glimp_sdl3.c`)
fetches each new window's `NSWindow` through SDL's documented
`SDL_PROP_WINDOW_COCOA_WINDOW_POINTER` and sets its `animationBehavior`
to `NSWindowAnimationBehaviorNone` (a NULL pointer is skipped), under
the same guard as `frame.c`; SDL is not patched, `q2ded` and the other
platforms are unchanged. The only visible change is a window that
closes without animation (an opening animation, where AppKit plays one,
goes too). Why the close animation
never completes on this host (display link, paravirtual display) is not
proven, and the behaviour on a physical Mac is unmeasured.
`tests/check-autorelease-pool.sh` also builds the client's
`glimp_sdl3.o` and requires a branch to the selector stub
`_objc_msgSend$setAnimationBehavior:`; the mutants "call removed" and
"guard never true" are refused.

**Procedure.** Needs the game data and a window server, so it is a
runbook step and not a gate check. A console script in the build's
`baseq2/` (not committed) runs, after 30 `wait`s, N cycles of `set
vid_renderer R; vid_restart` and 30 `wait`s for each listed renderer,
then `quit`. The engine runs from the build directory as `./quake2
-portable +set basedir <data> +set s_initsound 0 +set r_mode -1 +set
r_customwidth 640 +set r_customheight 480 +set vid_fullscreen 0 +set
vid_renderer <first> +exec <script>` under `MallocStackLogging=1 leaks
--outputGraph=<file> --atExit --`; then `heap <file>` gives the class
counts and footprint, and `leaks --autoreleasePools <file>` the pool
entries. Configurations: `soft`, `gl1` (shim), `metal` (one restart a
cycle), mix (`soft` and `gl1`, starting at `gl1`) and mix3 (`soft`,
`gl1`, `metal`, starting at `metal`). Release builds, before (commit
`4175e8cc`) and after the fix, two runs of each, load averages 8.6 to
25.6:

| Configuration | Before, 1 cycle | Before, 8 cycles | After, 1 cycle | After, 8 cycles |
|---|---|---|---|---|
| `soft`: windows, layers | 2, 2 | 9, 9 | 2, 0 | 9, 0 |
| `soft`: footprint (MB) | 65.4 to 66.6 | 100.3 to 100.8 | 61.0 to 61.5 | 59.8 to 63.1 |
| `gl1`: windows, layers | 2, 0 | 9, 0 | 2, 0 | 9, 0 |
| `gl1`: footprint (MB) | 61.8 to 62.0 | 58.6 to 63.4 | 62.0 to 62.2 | 63.0 to 63.2 |
| `metal`: windows, layers | 2, 0 | 9, 0 | 2, 0 | 9, 0 |
| `metal`: footprint (MB) | 58.1 | 59.3 to 59.4 | 58.2 to 58.3 | 59.1 to 59.5 |
| mix: windows, layers | 3, 1 | 17, 8 | 3, 0 | 17, 0 |
| mix: footprint (MB) | 68.1 to 68.4 | 103.6 to 103.9 | 58.4 to 63.2 | 61.5 to 66.2 |
| mix3: windows, layers | 4, 1 | 25, 8 | 4, 0 | 25, 0 |
| mix3: footprint (MB) | 63.3 to 68.1 | 102.3 to 107.0 | 63.0 to 63.5 | 69.4 to 69.5 |

Windows are `NSCGSWindow`, layers `CAMetalLayer` (with each the same
number of `SDL3_cocoametalview`), at exit, after the engine destroyed
its last window. After the fix the layer count is 0 after 1 and after 8
cycles in every configuration, and the main thread's pool holds 0 or 1
entry at exit (8 to 10 before, in the mixes). Footprint growth per cycle
from 1 to 8 cycles, before: `soft` 4.8 to 5.1 MB, mix 5.0 to 5.1, mix3
4.9 to 6.2; after: `soft` -0.2 to 0.2, mix 0.4, mix3 0.9 (two and three restarts a
cycle), `gl1` and `metal` -0.5 to 0.2 either way (runs at this load spread
by up to 5 MB, so these are bounds, not costs). The live heap still grows
by about 340 to 360 nodes a restart (490 before, `soft`); the scratch
build without the close animation grew by 110 a restart, so most of the
rest is cause 2; with the pool alone the window count after 8 cycles
still equals the windows opened.

With the close animation turned off as well (fix for cause 2), same
procedure, release builds, two runs of each, load averages 13 to 15:

| Configuration | 1 cycle | 8 cycles |
|---|---|---|
| `soft`: windows, layers | 0, 0 | 0, 0 |
| `soft`: footprint (MB) | 60.5 | 57.2 to 61.6 |
| `metal`: windows, layers | 0, 0 | 0, 0 |
| `metal`: footprint (MB) | 57.4 to 57.8 | 57.8 to 58.0 |
| mix3: windows, layers | 0, 0 | 0, 0 |
| mix3: footprint (MB) | 62.5 to 62.9 | 65.2 to 65.3 |

No old window, layer or metal view is left at exit, after 1 or after 8
cycles, so the counts after 8 cycles equal those after 1. The live heap
grows by about 110 to 130 nodes a restart (`soft` 127, `metal` 130,
mix3 107); footprint per cycle from 1 to 8 cycles: `soft` -0.5 to 0.2 MB,
`metal` 0.0 to 0.1, mix3 0.3 to 0.4 (three restarts a cycle).

Under the `ASAN=1` build with `MTL_DEBUG_LAYER=1` and
`OBJC_DEBUG_MISSING_POOLS=YES`, four mix3 cycles: status 0, no sanitizer
line, no validation message, five `ref_metal` teardowns each `0 live
objects ... 0 unreleased, 0 unmatched`, and no missing-pool line on the
main thread. The same run with the close animation off: status 0, no
sanitizer line, no validation message beyond `Metal API Validation
Enabled`, five clean `ref_metal` teardowns, and the missing-pool lines
(2907) all from six threads other than the main thread.

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
