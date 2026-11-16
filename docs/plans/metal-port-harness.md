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
and `ref_gl3` build there too; `gl1` references are card P3's (TASK41, [`../runbook/TASK41.md`](../runbook/TASK41.md)).

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
game data or a pin store ([`TASK27`](../runbook/TASK27.md), "Closing TASK27 on the build host"). Pins on
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

## Card results

- [FIX41](../runbook/FIX41.md): `vid_restart` and the main thread's autorelease pool
- [FIX42](../runbook/FIX42.md): The `screenshot` command between frames on the shim
- [FIX52](../runbook/FIX52.md): The 60 fps cap at 1920x1080 windowed
- [FIX53](../runbook/FIX53.md): The opaque layer of `ref_metal` and the shim
- [FIX58](../runbook/FIX58.md): `tests/check-pre-push-hook.sh` and a temporary directory that holds the login name
- [FIX66](../runbook/FIX66.md): CodeQL analysis skipped for documentation-only pull requests
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
- [TASK97](../runbook/TASK97.md): Frame rates on a physical GPU
- [TASK98](../runbook/TASK98.md): Screenshot and capture readback on a physical GPU
- [TASK108](../runbook/TASK108.md): Re-measurement
- [TASK111](../runbook/TASK111.md): MetalFX scalers for the 3D view in `ref_metal`
- [TASK118](../runbook/TASK118.md): HighDPI windows on the bare-metal host
- [TASK119](../runbook/TASK119.md): Crisp 2D in `ref_metal`
- [TASK120](../runbook/TASK120.md): Bicubic lightmaps in `ref_metal`
- [TASK122](../runbook/TASK122.md): Particle size cap relative to the resolution in `ref_metal`
- [TASK131](../runbook/TASK131.md): The dynamic-light shadow pass finds each pixel's surface without an eye ray
- [TASK132](../runbook/TASK132.md): `soft`'s window captured opaque and not opaque
