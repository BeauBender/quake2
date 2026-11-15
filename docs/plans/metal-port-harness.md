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

### Sprites, beams, shadows, dynamic lights and flash blobs in `ref_metal` (TASK91, N8)

N8 is judged on the new scenario `dlights` (`world` plus `cl_lights 1`),
on `models` (complete: sprites, beams and null models drawn) and its
scratch copies with stencil shadows, and on N7's set unchanged
(`docs/plans/ref-metal-native.md`, section 23). References are
`gl1-metal` records in a private store; `dlights` and `models` are
recorded with `PINS_SEED`, which needs the trace (about 4 to 6 MB a
record, so leave `PINS_TRACE` unset):

```sh
make client game ref_metal && make ref_gl1 WITH_METAL_SHIM=yes
export PINS_STORE=<private store> PINS_BASEDIR=<game data> PINS_SEED=1   # one seed for every record
MTL_DEBUG_LAYER=1 scripts/pins.sh record gl1-metal dlights               # and again with --run 2
MTL_DEBUG_LAYER=1 scripts/pins.sh record metal dlights
scripts/pins.sh compare gl1-metal dlights --run 2 --ref gl1-metal --ref-id <the first> --threshold 0 --bad-threshold 0
scripts/pins.sh compare metal dlights --ref gl1-metal --ref-id <each reference> --threshold 0.005 --bad-threshold 0.001
# the same with PINS_DEMO=demo2.dm2 PINS_EVERY=7 (89 frames, the animated lightstyles), and for models
# world, skywater, world-fullbright, hud2d as in N6; cinematic as in N2
# scratch copies, deleted after: models plus "set r_shadows 1" and "set gl1_stencilshadow 1" (and 0),
# dlights plus "set gl1_flashblend 1" (or by hand, below), dlights plus "set gl1_dynamic 0" (the control, recorded seeded)
```

`dlights` is not deterministic unseeded: the muzzle-flash and
explosion lights take a random radius from the engine PRNG (`randk() &
31` in `cl_effects.c`), which is seeded from OS entropy. Unseeded, two
`gl1-metal` records differ on the frames such a light reaches (on
`demo1.dm2` frames 5, 8, 10, 14, 16, 18, 19, 20, 21; on `demo2.dm2`
every 7th, 22 of 89), by up to score 0.009399 and 0.012701, bad
0.176774 and 0.305941 over five records (ten pairs) of each demo, and
`metal` differs from any reference on exactly those frames by as much
as the references differ among themselves. No unseeded threshold
guards the lightstyle update: the accept value first committed,
`--threshold 0.02 --bad-threshold 0.5`, passes the `gl1_dynamic 0`
control below on all five lightstyle frames. So the scenario is
recorded with `PINS_SEED` and judged only seeded, at `--threshold
0.005 --bad-threshold 0.001`; the seed is part of the pin id, so the
reference and the candidate are recorded with the same seed and the
reference is named with `--ref-id`. The first measurement, made
without a trace, fixed the PRNG with a scratch copy of the tree whose
`Qcommon_Init` (`src/common/frame.c`) called `randk_seed_fixed(91)`
instead of `randk_seed()` (its pin ids begin with `nogit`; never
committed); `PINS_SEED` gives the same result without a copy.

Seeded, at `8852cfc9` (`PINS_SEED=1`, every record with
`MTL_DEBUG_LAYER=1` and its trace): the two `gl1-metal` records of
`dlights` were identical byte for byte, 23 of 23 frames of `demo1.dm2`
and 89 of 89 of `demo2.dm2` every 7th, and so were the two of
`models` (23 of 23). `metal` against each of the two references, at
`--threshold 0.005 --bad-threshold 0.001`: `dlights` on `demo1.dm2` 23
of 23 ok, score 0.000000 and bad 0.000000 on every frame (14 frames
not byte-identical, by 5 levels at most); on `demo2.dm2` every 7th 89
of 89 ok, worst score 0.000001 and bad 0.000013 (frames 12, 15 and 61
hold 1 to 4 pixels more than 8 levels off; 42 frames not
byte-identical); `models` 23 of 23 ok, worst score 0.000039, bad
0.000589 (N7's residue).

The animated lightstyles: on `demo2.dm2` at `PINS_EVERY=7` frames 51,
61, 63, 67 and 69 show one (N5) and no dynamic light at all (the
trace's refdef holds none), so they are identical between `gl1-metal`
records with or without a seed. The control is `metal` with
`gl1_dynamic 0`, recorded seeded from a scratch copy of `dlights` plus
that line, against the seeded reference, compared frame by frame with
`scripts/pins/framediff.c` (the two records are of different
scenarios): over at the tight values on 32 of 89 frames: 29 of the 37
frames whose refdef holds a dynamic light (score 0.000492 to 0.062769;
on the other 8 the light lights nothing in view, 3 levels at most), and
three of the five lightstyle frames: 61 (score 0.000105, bad
0.001130: 3,934 pixels differ, by up to 27 levels), 67 (0.000595,
0.006882: 27,473
pixels, up to 24) and 69 (0.000447, 0.004303: 20,850 pixels, up to
32), N5's numbers. Frames 51 and 63 stay under: without the update 184
pixels of 51 (score 0.000001) and 80,449 of 63 (score 0.000467) differ,
each by exactly one level, and the bad fraction counts only pixels
more than 8 levels off. Without the update a lightmap keeps its first
build, every style at 1.0 (`LM_BeginBuildingLightmaps`, as gl1's), and
the styles of the surfaces in view on those two captures light them
within one level of it; `metal` with the update against `metal`
without it gives the same one-level difference (178 and 80,449
pixels), so that is the update's whole effect there, not a renderer
difference. At these values the update is not visible on 51 and 63:
the bad fraction never counts a one-level change, and the scores are
under 0.005 by a factor of ten or more. At the old accept value the
control is over on 13 frames, all with a dynamic light (score 0.020
to 0.063), none of the five.

Which frames are judged: all of them. N5's rule that excluded a frame
whose `metal_stats` line counted surfaces "drawn without their dynamic
light" is gone with the count (the line now ends with the dynamic lights
left out for a float that is not finite or out of bounds, 0 on every
frame of the game's demos); `dlights` and `models` draw no particle, so
only `particles` waits for N9. The counts come from a hand run of
`metal` per scenario, as in N2: `PINS_DRYRUN=1`'s command line of the
record without `+set pins_record trace.q2r`, a scratch `pins_dir`, plus
`+set metal_stats 1`, under `MTL_DEBUG_LAYER=1`. At `9a47f6e0` on
`demo1.dm2` both `models` and `dlights` printed one line per captured
frame, 23 each (and one more at shutdown), every one reading `0 draws
skipped`, `23 pipelines, 0 created after init` and `skipped surfaces: 0
sky, 0 warped, 0 translucent, 0 flowing; 0 dynamic lights left out;
skipped entities: 0 sprites, 0 beams, 0 null models, 0 translucent brush
models, 0 refused`: no frame skips a surface, an entity or a light.

The flash-blob copy needs no scenario file: run the engine by hand with
the seeded command line of `dlights` (`PINS_SEED=1 PINS_DRYRUN=1
scripts/pins.sh record <renderer> dlights`, the trace kept, as the seed
needs it) plus `+set gl1_flashblend 1` and a scratch `pins_dir`, once
for `gl1-metal` and once for `metal`, and compare the frames pairwise
with `framediff` (`cc -O2 -o <scratch>/framediff
scripts/pins/framediff.c -lm`) at `--threshold 0.005 --bad-threshold
0.001`; delete the traces after. At `9a47f6e0`: 23 of 23 ok, score
0.000000 and bad 0.000000 on every frame (5 levels off at most), and the
`metal` frames differ from a seeded `metal` record without
`gl1_flashblend` on the 10 frames that hold a light (score up to
0.045524).

The stencil shadows: the scratch `models` copies with `r_shadows 1` show
the shadows (up to 6,609 pixels of a frame against `models`); with
`gl1_stencilshadow 1` against `0` both renderers change the same number
of pixels on every frame but the PRNG's frame 7, which shows that the
stencil test and the clear to 1 work as on the shim.

Map changes and `vid_restart` under the validation layer, with the
states N8 adds: a config beside the binary (in `release/baseq2/`,
deleted after) holding `set r_shadows 1`, `set gl1_stencilshadow 1`,
`map base1`, `wait 80`, `screenshot`, `vid_restart`, `wait 80`, `set
gl1_flashblend 1`, `map base2`, the same, both off, `map base1`, the
same, `quit`, run with `MTL_DEBUG_LAYER=1 release/quake2 -datadir <game
data> -portable +set vid_renderer metal ... +set metal_stats 1 +exec
<the config>`: four teardowns at `0 live objects`, no validation
message.

The timedemo, `demo1.dm2`: the same engine binary copied into two
scratch directories, one beside `ref_metal` of the card's base and one
beside this card's, the pin harness's command line (`PINS_DRYRUN=1`)
with a scratch `pins_dir`, runs alternated; the frame rate is the line
`689 frames, ... fps` of the log. The display did not pace these runs on
this host (about 1,400 to 1,650 fps), unlike N7's.

Measured on the build VM, sources `28d1c8f8` to `a48d01a8` (the same
code), load averages 4 to 9, and the seeded records at `8852cfc9`: the
numbers of the design document's table in section 23; every
`engine.log` holds only `Metal API Validation Enabled` and, for
`metal`, the teardown with `0 live objects`.

### Particles, the parity sweep and frame time in `ref_metal` (TASK92, N9)

References: `gl1-metal` records at the card's base, `e5fb3969`, in a private store, unseeded
without a trace and seeded (`PINS_SEED=1`) with one. The scratch copies `particles-pp0` and
`particles-sq` are `particles.cfg` plus `set gl1_pointparameters 0` or `set gl1_particle_square 1`,
put in `scripts/pins/scenarios/` for the records and deleted after (never committed):

```sh
make client game ref_metal && make ref_gl1 WITH_METAL_SHIM=yes
export PINS_STORE=<private store> PINS_BASEDIR=<game data>
PINS_SEED=1 MTL_DEBUG_LAYER=1 scripts/pins.sh record gl1-metal particles   # and --run 2; the same for the two copies
PINS_SEED=1 MTL_DEBUG_LAYER=1 scripts/pins.sh record metal particles
PINS_SEED=1 scripts/pins.sh compare metal particles --ref gl1-metal --ref-id <each reference> --threshold 0.005 --bad-threshold 0.001
# the control: PINS_BIN=<an engine copy beside the base's ref_metal.dylib>, record metal, compare with --cand-id
# every other scenario as in N8 (models and skywater from water.dm2 against two records)
```

The sweep, one source (`77c46ced` plus the change, pin id `77c46ced6e72-d3752008510`; that source is the one of commit
`786790e9`: `git diff 77c46ced 786790e9 -- src Makefile | cksum` gives 3752008510), 17:58 to
17:59 local, every record with the validation layer, every `engine.log` holding only `Metal API
Validation Enabled` and, for `metal`, `teardown: 0 live objects`, `0 unreleased`, `0 unmatched`:

| Scenario | Value (design section 11) | `metal` against each reference |
|---|---|---|
| `hud2d` | 0.005, 0.001 | 23 of 23, score 0, bad 0 |
| `cinematic` (`idlog.cin`, every 7th) | 0.005, 0.001 | 12 of 12, 0, 0 |
| `world` | 0.005, 0.001 | 23 of 23, 0, 0 |
| `world-fullbright` | 0.005, 0.001 | 23 of 23, score 0, bad at most 0.000013 |
| `skywater`, `demo1.dm2` | 0.005, 0.001 | 23 of 23, 0, 0 |
| `skywater`, `water.dm2`, two records | 0.02, 0.03 | 4 of 4 each, 0, 0 |
| `models`, two records | 0.02, 0.005 | 23 of 23 each (worst 0.000728, bad 0.001019; 0.000457, 0.001051); at 0.005, 0.001 22 of 23 each, frame 7 (the PRNG's explosion; the references differ there by 0.000312, bad 0.000456) |
| `models` seeded, two records | 0.005, 0.001 | 23 of 23 each, worst 0.000039, bad 0.000589 |
| `dlights` seeded, `demo1.dm2`, two records | 0.005, 0.001 | 23 of 23 each, 0, 0 |
| `dlights` seeded, `demo2.dm2` every 7th, two records | 0.005, 0.001 | 89 of 89 each, worst 0.000001, bad 0.000013 |
| `particles` seeded, two records | 0.005, 0.001 | 23 of 23 each, worst 0.000039, bad 0.000589 (frame 2, N7's residue) |
| `particles-pp0` seeded, two records | 0.005, 0.001 | 23 of 23 each, the same |
| `particles-sq` seeded, two records | 0.005, 0.001 | 23 of 23 each, the same |
| `particles` unseeded, two records | 0.02, 0.25 (the card's) | 23 of 23 each (worst 0.009542, bad 0.148773; 0.009659, 0.141947); at 0.005, 0.001 14 of 23 over each; the two references differ by up to 0.010331, bad 0.174274 |
| control: the base's `ref_metal` (no particles), seeded | 0.005, 0.001 | over on 11 (`particles`), 10 (`-pp0`) and 12 (`-sq`) of 23 against each reference, worst 0.009926, 0.006995, 0.011398; at 0.02, 0.25 all 23 pass |
| `gl1-metal` seeded record 2 against record 1 | 0, 0 | `particles`, `-pp0`, `-sq`: 23 of 23 byte-identical |

The two copies are drawn differently from `particles`: `metal` frames of each differ from the
`metal` `particles` frames on 16 of 23.

`metal_stats 1` (a hand run with the seeded record's command line, a scratch `pins_dir`, `+set
metal_stats 1`, the validation layer), `particles` and both copies: 23 frame lines and the one at
shutdown, every one `0 draws skipped`, `0 failed command buffers`, `24 pipelines, 0 created after
init`, `0 particles refused`, no surface, light or entity skipped; teardown `0 live objects`.

The particle texture: a config beside the binary (deleted after) with `map base1`, `imagelist`,
`set gl1_particle_square 1`, `imagelist`, `map base2`, `imagelist`, `vid_restart`, `imagelist`,
`set gl1_particle_square 0`, `map base1`, `imagelist`, `quit`, run with `+set gl1_pointparameters
0` and with `1` under the validation layer: `***particle***` in every list, two teardowns at `0
live objects` per run, every `metal_stats` line `24 pipelines, 0 created after init`.

The frame time: the record's command line (`PINS_DRYRUN=1`, unseeded, no trace), a scratch
`pins_dir`, no validation layer, `+set r_vsync 0` for both renderers (with the default the display
held both to 60 fps while it was in use), `metal` then `gl1-metal`, three rounds per scenario; the
`fps` of `timedemo.txt`'s line:

| Scenario | `metal` fps | `gl1-metal` fps |
|---|---|---|
| `hud2d` | 1398 to 1555 | 1097 to 1213 |
| `world` | 1281 to 1528 | 799 to 982 |
| `world-fullbright` | 987 to 1338 | 696 to 944 |
| `skywater`, `demo1.dm2` | 1285 to 1398 | 816 to 984 |
| `skywater`, `water.dm2` (93 frames) | 894 to 1453 | 750 to 869 |
| `models` | 823 to 1065 | 668 to 709 |
| `dlights`, `demo1.dm2` | 873 to 1102 | 632 to 777 |
| `dlights`, `demo2.dm2` every 7th | 411 to 804 | 385 to 553 |
| `particles` | 767 to 942 | 537 to 581 |
| `particles-pp0` | 770 to 968 | 485 to 629 |
| `particles-sq` | 954 to 1139 | 552 to 679 |
| `cinematic` | 6.0 s each (the cinematic's own clock; no frame rate) | 6.0 s each |

One-minute load 7.8 to 13.4 (the cinematic rows 11.9 to 25.1). `metal` was faster in each of the
33 pairs. A first set at a load of 13 to 63 (an iOS simulator and other work on the host): `metal`
222 to 334 and `gl1-metal` 125 to 309 fps, `metal` slower in 11 of 30 pairs (`hud2d` 2, `world` 1,
`skywater` 1, `skywater` from `water.dm2` 3, `models` 2, `dlights` 1, `particles-sq` 1), by up to
24 percent (`water.dm2`, 0.5 s runs); at that load the two ranges overlap on every scenario.

### `ref_metal` on the build host (TASK93, N10)

N10 closes group N: the lifecycle (restarts, sizes, vsync, gamma), the selection on macOS and
the sweep again (`docs/plans/ref-metal-native.md`, section 25). FIX45 (#230) is folded in: the
frame is the mode's size, not the window's.

**The sweep.** References: `gl1-metal` at the card's base, `9a38a4e9`, built from a detached
checkout of that commit (the Makefile has no header dependencies on macOS, so `ref_gl1.dylib`
was relinked and every `ref_metal` object rebuilt after each checkout; `stale_engine` refuses a
module older than a header otherwise). Candidates: `metal` at `93be24f9` (pin ids
`93be24f93eea-*`); the commits up to `1e53d562` change no file under `src` or the Makefile
(`git diff 93be24f9 1e53d562 -- src Makefile` is empty); `f1e23000` changes `rmb.h` and
`rmb_encode.m` (the black clear of a drawable larger than the frame), covered by the records
made again below and by the fullscreen-window run under **Sizes and FIX45**. `particles` was recorded again at `fe97ef2e`, whose
scenario file has the new header comment (a comment changes the pin id, not the engine's input).
A scratch `world-gamma` (`world.cfg` plus `set vid_gamma 1.6` and `set gl1_intensity 3`) was put
in `scripts/pins/scenarios/` for the records and deleted after. Every record with
`MTL_DEBUG_LAYER=1`; the seeded ones with `PINS_SEED=1` and the trace; `water.dm2` made once by
`make-demo water` at the base and deleted after. Recorded 19:35 to 19:38 and 20:16 local, load
about 10:

```sh
export PINS_STORE=<private store> PINS_BASEDIR=<game data> MTL_DEBUG_LAYER=1
# at the base: make client game ref_soft && make ref_gl1 WITH_METAL_SHIM=yes
scripts/pins.sh record gl1-metal <scenario> [--run 2]        # PINS_SEED=1 for models (second pair), dlights, particles
# at the card: make client game ref_metal
scripts/pins.sh record metal <scenario>
scripts/pins.sh compare metal <scenario> --ref gl1-metal --ref-id <reference> --cand-id <candidate> <values>
```

| Scenario | Value (design section 11) | `metal` against each reference |
|---|---|---|
| `hud2d` | 0.005, 0.001 | 23 of 23, score 0, bad 0 |
| `cinematic` (`idlog.cin`, every 7th) | 0.005, 0.001 | 12 of 12, 0, 0 |
| `world` | 0.005, 0.001 | 23 of 23, 0, 0 |
| `world-fullbright` | 0.005, 0.001 | 23 of 23, score 0, bad at most 0.000013 |
| `skywater`, `demo1.dm2` | 0.005, 0.001 | 23 of 23, 0, 0 |
| `skywater`, `water.dm2`, two records | 0.02, 0.03 | 4 of 4 each, 0, 0 |
| `models`, two records | 0.02, 0.005 | 23 of 23 each (worst 0.000103, bad 0.000589; 0.000152, 0.000589); also at 0.005, 0.001 |
| `models` seeded, two records | 0.005, 0.001 | 23 of 23 each, worst 0.000039, bad 0.000589 |
| `dlights` seeded, `demo1.dm2`, two records | 0.005, 0.001 | 23 of 23 each, 0, 0 |
| `dlights` seeded, `demo2.dm2` every 7th, two records | 0.005, 0.001 | 89 of 89 each, worst 0.000001, bad 0.000013 |
| `particles` seeded, two records | 0.005, 0.001 | 23 of 23 each, worst 0.000039, bad 0.000589; the `fe97ef2e` record byte-identical to the `93be24f9` one |
| `world-gamma` (scratch) | 0.005, 0.001 | 23 of 23, 0, 0; its frames differ from `world`'s on 99 percent of the pixels |
| `gl1-metal` record 2 against record 1 | 0, 0 | seeded `models`, `dlights` (both demos), `particles` and `skywater` from `water.dm2`: identical; unseeded `models` over on 1 frame (0.000116) |

Every `engine.log` holds only `Metal API Validation Enabled` beside the game's output and, for
`metal`, `teardown: 0 live objects`, `state 45/45`, `0 unreleased`, `0 unmatched`.

At `f1e23000` (the present clears a drawable larger than the frame; nothing else under `src`
changed) every `metal` record but `skywater` from `water.dm2` and the scratch `world-gamma` was
made again: each passes against each reference as above (unseeded `models` worst 0.000233 and
0.000123, bad 0.000589), and `hud2d`, `cinematic`, `world`, `world-fullbright`, `skywater`,
seeded `models`, both `dlights` and `particles` are byte-identical to the `93be24f9` records.
The two left out read the same target and present path as the others (pin frames are windowed,
the frame the drawable's size, so the new clear never runs).

**Restarts under `ASAN=1`.** `make ASAN=1 client game ref_metal ref_soft` and `make ASAN=1
ref_gl1 WITH_METAL_SHIM=yes` (in `debug/`); a console script beside the binary (deleted after):
`set r_shadows 1`, `set gl1_stencilshadow 1`, `map base1`, then eight cycles of `set vid_renderer
R`, `vid_restart`, a wait, `screenshot`, for `gl1`, `soft` and `metal`, with `gl1_flashblend 1`
and `map base2` before cycle 3, the three off and `map base1` before cycle 5, shadows on and `map
base3` before cycle 7, then `quit`; run as `MTL_DEBUG_LAYER=1 ./quake2 -datadir <game data>
-portable +set vid_renderer metal +set r_mode 4 +set s_initsound 0 +exec <script>`. `wait` waits
one frame of at least 16 ms whatever its argument (`Cmd_Wait_f`), so the wait is an alias of 120
`wait`s; with single `wait`s restarts given in quick succession were lost (one `vid_restart`
taking effect for two `set vid_renderer` lines), which is the engine's command timing, not a
renderer's. Result: status 0, 25 loads in the order asked (`metal`, then `gl1`, `soft`, `metal`
eight times), 25 screenshots, no sanitizer line, no validation message beyond `Metal API
Validation Enabled`, nine `ref_metal` teardowns each `0 live objects ... 0 unreleased, 0
unmatched`.

**`leaks --atExit`.** FIX41's procedure ("`vid_restart` and the main thread's autorelease pool"):
release build, 30 `wait`s, N cycles of `set vid_renderer R; vid_restart` and 30 `wait`s, `quit`,
under `MallocStackLogging=1 leaks --outputGraph=<file> --atExit --`; `leaks <file>` for the
count, `heap <file>` for the footprint, the node count and the classes. Two runs of each, load
about 10:

| Configuration | 1 cycle: footprint, nodes | 8 cycles: footprint, nodes | per restart, 1 to 8 cycles |
|---|---|---|---|
| `soft` | 62.3, 60.5 MB; 37,782, 37,669 | 61.4, 61.5 MB; 38,515, 38,515 | 105 to 121 nodes |
| `gl1` (shim) | 62.2, 62.0 MB; 37,598, 37,615 | 61.7, 61.7 MB; 38,528, 38,547 | 133 nodes |
| `metal` | 61.7, 61.6 MB; 37,631, 37,590 | 61.7, 61.9 MB; 38,457, 38,463 | 118 to 125 nodes |
| mix3 (`metal`, then `gl1`, `soft`, `metal` a cycle), one run | 64.8 MB; 38,079 | 67.8 MB; 40,390 | 110 nodes (330 a cycle) |

Every run: status 0, `0 leaks for 0 total leaked bytes`, 0 `NSCGSWindow`, 0 `CAMetalLayer`, 0
`SDL3_cocoametalview` at exit, every `ref_metal` teardown (2 after 1 cycle, 9 after 8) at `0 live
objects`. A `metal` restart grows the heap by less than a `gl1` one and within the spread of
`soft`'s; the footprint moves by -0.13 to 0.14 MB a cycle in every configuration but mix3 (0.4 MB
a cycle of three restarts, FIX41's 0.3 to 0.4), within the run-to-run spread at this load.

**Sizes and FIX45.** A console script beside the binary (deleted after) with `developer 1`, `map
base1`, then for each mode a `vid_restart`, a wait and `screenshot`; the size of each TGA from its
header. The same script for this card's `metal`, the base's (`ref_metal.dylib` built from `git
archive 9a38a4e9` in a scratch directory, next to a copy of the engine) and `gl1-metal`:

| Mode | Client's `Drawable size` | `metal` | `metal` at the base | `gl1-metal` |
|---|---|---|---|---|
| `r_mode 4`, windowed | 640x480 | 640x480 | 640x480 | 640x480 |
| `r_mode 6`, windowed | 800x600 | 800x600 | 800x600 | 800x600 |
| `r_mode -1`, 1024x640, windowed | 1024x640 | 1024x640 | 1024x640 | 1024x640 |
| `r_mode 10`, `vid_fullscreen 1` | 640x480 (reverted) | 640x480 | 640x480 | 640x480 |
| `r_mode -2`, `vid_fullscreen 2` | 1333x888 | 1333x888 | 1333x748 | 1333x888 |
| `r_mode 4`, `vid_fullscreen 2` | 640x480 | 640x480 | 1333x748 | 640x480 |

`vid_fullscreen 1` cannot change the display mode on this host (`SDL SetVideoMode failed:
CGDisplaySwitchToMode(): Unknown Error`, then the client's `Reverting to windowed r_mode 4
(640x480)`), so TASK98's case, a display mode other than the one asked for, does not appear here
and is to be repeated on the bare-metal host (done: **On the bare-metal host (FIX45)**, below). The fullscreen window shows the same defect: its
pixels (1333x748 here) are neither the desktop mode nor the mode asked for, and the base rendered
and read back at them. With the frame at the mode's size `metal` writes what `gl1-metal` writes
and what the client's line says, in every mode. HighDPI windows are not made on macOS (section 25)
until TASK118 (below).

**On the bare-metal host (FIX45).** The case above, run on the bare-metal Apple Silicon Mac
(Apple M6) at engine `c01bd7d2` (the TASK93 merge on `main`): `make client game ref_metal
ref_gl1` in a worktree, `release/` copied out, then `make ref_gl1 WITH_METAL_SHIM=yes` and copied
again, so `ref_gl1` (`GL_RENDERER: Apple M6`) and the shim (`GL_RENDERER: Metal shim on Apple M6
(gl1-metal)`) ran beside the same `quake2` and `ref_metal.dylib`. The display: one 5120x2880
Retina panel, desktop mode 2560x1440 points, 5120x2880 pixels, 60 Hz (a 24-line C probe kept
outside the repository, `CGDisplayCopyDisplayMode` on the main display, printed `points 2560x1440
pixels 5120x2880 refresh 60.00`). A console script beside the binary (deleted after): `developer
1`, `map base1`, 100 `wait`s (an alias), `screenshot tga`, 30 `wait`s, `quit`; one run per mode,
the mode on the command line: `./quake2 -datadir <game data> -portable +set vid_renderer R +set
vid_fullscreen F +set r_mode M [+set r_customwidth W +set r_customheight H] +set s_initsound 0
+exec <script>` under `timeout 120`, `config.cfg` removed before each run, every `metal` run with
`MTL_DEBUG_LAYER=1`. The harness capture is the line `PINS_DRYRUN=1 scripts/pins.sh record metal
world` printed (`r_mode -1`, 640x480, `demo1.dm2`, `pins_capture 30`) with `vid_renderer` and
`vid_fullscreen 1` set by hand and a private `pins_dir`, as TASK98 ran it, under `timeout 300`. Each TGA's size from its header. The client's lines are
the same for the three renderers in every mode:

| Mode | `Setting mode` | `User requested ..., setting closest mode` | `Real display mode` | `Drawable size` | `metal` | `gl1-metal` | `ref_gl1` |
|---|---|---|---|---|---|---|---|
| `r_mode 4`, `vid_fullscreen 1` | `4: 640x480 (vid_fullscreen 1)` | `640x480@-1.000000`, `960x540@60.000000` | `960x540@60.00` | `640x480` | 640x480 | 640x480 | 640x480 |
| harness `world`, `r_mode -1` 640x480, `vid_fullscreen 1` | `-1: 640x480 (vid_fullscreen 1)` | `640x480@-1.000000`, `960x540@60.000000` | `960x540@60.00` | `640x480` | 23 frames, each 640x480 | 23, each 640x480 | 23, each 640x480 |
| `r_mode -1`, 1920x1080, `vid_fullscreen 1` | `-1: 1920x1080 (vid_fullscreen 1)` | `1920x1080@-1.000000`, `1920x1080@60.000000` | `1920x1080@60.00` | `1920x1080` | 1920x1080 | 1920x1080 | 1920x1080 |
| `r_mode -1`, 2560x1440, `vid_fullscreen 1` | `-1: 2560x1440 (vid_fullscreen 1)` | `2560x1440@-1.000000`, `2560x1440@60.000000` | `2560x1440@60.00` | `2560x1440` | 2560x1440 | 2560x1440 | 2560x1440 |
| `r_mode -1`, 5120x2880, `vid_fullscreen 1` | `-1: 5120x2880 (vid_fullscreen 1)` | `5120x2880@-1.000000`, `5120x2880@60.000000` | `5120x2880@60.00` | `5120x2880` | 5120x2880 | 5120x2880 | 5120x2880 |
| `r_mode 4`, windowed | `4: 640x480 (vid_fullscreen 0)` | none | `2560x1440@60.00` | `640x480` | 640x480 | 640x480 | 640x480 |
| `r_mode 4`, `vid_fullscreen 2` | `4: 640x480 (vid_fullscreen 2)` | none | `2560x1440@60.00` | `640x480` | 640x480 | 640x480 | 640x480 |

So the display really switched for every `vid_fullscreen 1` run, and 640x480 fell back to
960x540 as in TASK98 (`User requested 640x480@-1.000000, setting closest mode 960x540@60.000000`,
then `Real display mode: 960x540@60.00` and `Drawable size: 640x480`). No log has a `SDL
SetVideoMode failed`, `Couldn't switch` or `Reverting to windowed` line. The harness manifests
list every frame at 640x480 (`world-000000.tga metal 640x480`). One more `metal` harness capture,
the same line with `vid_fullscreen 0`, has 23 frames byte-identical to the fullscreen ones
(`cmp`; `framediff` score 0, bad 0): the fallback mode changes neither the size nor a byte of the
frame. With `scripts/pins/framediff.c` at 0.005, 0.001, the fullscreen `metal` capture against
the fullscreen `gl1-metal` one passes 23 of 23 (worst score 0.000002, bad at most 0.000016), and
`gl1-metal` against `ref_gl1` 23 of 23 (0.000010, 0.000042). Each of the eight `metal` logs holds
`Metal API Validation Enabled` and no other validation message and no failed command buffer, and
its teardown line reads `ref_metal: teardown: 0 live objects (created/destroyed: device 1/1, queue
1/1, view 1/1, texture 203/203, buffer 8/8, state 45/45), 0 unreleased, 0 unmatched` (`texture
232/232` in the two harness runs). 22 runs, 21:56 to 22:01 local, each status 0 with no kill;
before and after each the screen was unlocked (`ioreg -n Root -d1 | grep -c
CGSSessionScreenIsLocked` printed 0), no other `quake2` was running before each (`pgrep -x
quake2`); one-minute load 2.52 to 4.63.
After every fullscreen run, the last `vid_fullscreen 1` one (`gl1-metal` at 5120x2880) included,
the probe printed the desktop mode again (`points 2560x1440 pixels 5120x2880 refresh 60.00`), and
after the last run `system_profiler SPDisplaysDataType` reported `Resolution: 5120 x 2880 Retina`
as before the first; what the panel showed during the runs was not looked at, the runs are judged
from their files. FIX45's acceptance:

- With `vid_fullscreen 1` and 640x480 asked for, `screenshot` and the harness capture from
  `ref_metal` are 640x480 (TASK98's finding 1 had them at the fallback mode's size): **holds**.
- The `Drawable size` line matches the size actually rendered: **holds**. The line says 640x480,
  the frame read back from `ref_metal`'s colour target is 640x480, and it is the windowed frame
  byte for byte; in the other modes the line and the `metal` picture agree too.
- Windowed behaviour is unchanged: **holds**. `r_mode 4` windowed and `vid_fullscreen 2` write
  640x480 on all three renderers, as in TASK93's table; the client's `Real display mode` is the
  desktop's 2560x1440 here.

At `67e56ab` (after `f1e23000`, whose black clear runs only when the drawable is larger than the
frame) the fullscreen-window case was run once more under the validation layer: `MTL_DEBUG_LAYER=1
./quake2 -datadir <game data> -portable +set vid_renderer metal +set vid_fullscreen 2 +set r_mode
4 +set s_initsound 0 +exec <script>`, the script `developer 1`, `map base1`, 30 `wait`s,
`screenshot`, 10 `wait`s, `quit` (status 0, no kill needed). The client printed `Setting mode 4:
640x480 (vid_fullscreen 2)`, `Real display mode: 1333x888@60.00`, `Drawable size: 640x480`; the
screenshot's TGA header reads 640x480; the log holds `Metal API Validation Enabled` and no other
validation message and no failed command buffer, and ends `ref_metal: teardown: 0 live objects
(... texture 203/203, buffer 8/8, state 45/45), 0 unreleased, 0 unmatched`.

**vsync.** In the same run, `set r_vsync 0`, `1`, `0`, `1` between frames printed
`ref_metal: displaySyncEnabled off`, `on`, `off`, `on`: the property read back from the layer,
which is also what `IsVSyncActive` returns.

**Selection.** `+set vid_renderer gl3` with no `ref_gl3.dylib` beside the binary: `Library
ref_gl3.dylib cannot be found!`, `Retrying with metal...`, `Successfully loaded ref_metal.dylib`.
The preprocessed `vid.c` (the Makefile's compile line with `-E -P`) has `"gl3", "metal", "gl1",
"soft"`; the `r_order` block preprocessed without `__APPLE__` (and with `_WIN32`) has `"gl3",
"gl1", "soft"`. The menu's `fulllist` preprocessed the same way has `[Metal     ]` on macOS and
not without `__APPLE__`.

**The module mapping.** A scratch program that `dlopen`s a module, `dlclose`s it and asks
`dlopen(RTLD_NOLOAD)` again: a plain C dylib is unmapped, `ref_metal.dylib` and the shim's
`ref_gl1.dylib` stay mapped (dyld keeps an image with Objective-C metadata). `RM_KeepMapped`
adds the documented `RTLD_NODELETE` reference.

**Mutants.** Each of the five new functions of `rm_pure.c` (`rm_ledger_kind_live`,
`rm_ledger_clean`, `rm_frame_size`, `rm_frame_remake`, `rm_present_region`), every operand of
each compound guard on its own, each comparison moved by one, each result field: 51 mutants, all
killed by `tests/metal/rm_host_test.c` under ASAN and UBSAN. Three that made a parameter unused
were refused by `-Werror`; each was run again with the parameter cast to `void` and killed. The
one that let `RM_OBJ_KINDS` through `rm_ledger_kind_live` was killed only by UBSAN at first; the
case now reads a 1 through such an index, so it fails without a sanitizer too.

#### HighDPI windows on the bare-metal host (TASK118)

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

### Acceleration structures in `ref_metal` (TASK53, RT1)

RT1 builds the acceleration structures of `docs/plans/metal-raytracing-design.md`,
section 1, which nothing reads yet; the counts and times are in that section. Host: the
bare-metal Mac, `Apple M6` (`supportsRaytracing: yes`), macOS 27.2.1 (26B131). The pins,
the validation runs and the ASAN run are of engine commit `0269ca35` (pin ids
`0269ca350d6b-*`), the card's final source on its first base, `1701a052`. The card was then
rebased onto `82f2671f` (TASK122, #301), whose only source change is `metal_particle_cap`, off
by default, so the card's own `src` and Makefile are unchanged by the rebase; the counts and times are
of `eaf80979`, which differs from it only in where `RI_Init` registers the two cvars (a run of
`base2` at `0269ca35` gave the same counts). Screen unlocked before every run (`ioreg -n Root -d1 | grep -c
CGSSessionScreenIsLocked` 0), no other `quake2`; one-minute load 2.0 to 4.0 for the single runs
and 3.5 to 5.3 for the records (another agent was building). Every log and frame is in
`<artifacts>/TASK53/` and `<pin store>`.

```sh
export PINS_STORE=<pin store> PINS_BASEDIR=<game data> MTL_DEBUG_LAYER=1
make client game ref_metal
scripts/pins.sh record metal <scenario>            # PINS_SEED=1 for models, particles, dlights as well
# with the structures built: scenarios/<s>-rtb.cfg (<s>.cfg plus "set r_rt_stats 1"), deleted after;
# the cinematic by the line appended to cinematic.cfg for one record (only that scenario plays a .cin)
scripts/pins.sh record metal <s>-rtb
scripts/pins.sh compare metal <scenario> --ref gl1 --ref-id <TASK83 record> --cand-id <record> [values]
cmp <default frame> <the -rtb record's frame of the same number>
```

**Pins.** Against the `gl1` records of TASK83 (`de1da7fdba66-*`, `f6bb192775b8-*`, the two
accelerated references), at the values of "Accept `gl1-metal` against `gl1` at":

| Scenario | Value | Each of the two `gl1` references |
|---|---|---|
| `world` | the defaults | 23 of 23, worst 0.000010, bad 0.000033 |
| `world-fullbright` | the defaults | 23 of 23, worst 0.000031, bad 0.000072 |
| `hud2d` | the defaults | 23 of 23, 0, 0 |
| `skywater`, `demo1.dm2` | the defaults | 23 of 23, worst 0.000010, bad 0.000033 |
| `cinematic` (`idlog.cin`, every 7th) | the defaults | 12 of 12, 0, 0 |
| `models` | the defaults | 23 of 23, worst 0.000084 and 0.000236, bad 0.000612 |
| `particles` | 0.02, 0.25 | 23 of 23, worst 0.010409 and 0.010403, bad 0.153356 and 0.181045 (the references' scenario file differs from today's by its header comment only) |

The store holds no `gl1` record of `dlights`; seeded `dlights`, `models` and `particles`
(`PINS_SEED=1`) were compared with the seeded `gl1-metal` records of `bdda98128905` at 0.005 and
0.001: 23 of 23 each, `dlights` 0 and 0, the other two worst 0.000039, bad 0.000589. With the
structures built, every frame of `world`, `world-fullbright`, `hud2d`, `skywater`, `cinematic`
and of seeded `models`, `particles` and `dlights` is byte-identical to the frame recorded without
them (`cmp`: 23 of 23, 12 of 12 for `cinematic`), so those records pass as the ones above do.
Unseeded `models` and `particles` were not recorded with the structures (two unseeded records
differ by themselves). Every record's `engine.log` holds `Metal API Validation Enabled` and no
other validation line, no failed command buffer and `teardown: 0 live objects`; without the
structures it has no `rt:` line and `rt 0/0`, with them the map's line and `rt 66/66` (no
alias models in the scenario, `cl_entities 0`) or `rt 309/309` (`models`, `particles`). The
cinematic's record with `r_rt_stats 1` built nothing (`rt 0/0`): a cinematic draws no 3D view.

**Counts and times.** `r_rt_stats 2`, one line per frame, 640 x 480, `r_vsync 0`, `timedemo 1`,
nothing captured (`pins_capture 100000`): `+demomap demo1.dm2` (plays `base2`), `+demomap
demo2.dm2` (`waste2`; the data has no `demo3.dm2`), and `base1` from a scratch script beside the
binary (`map base1`, 400 one-frame waits, `quit`), each without the validation layer; the warm
figures skip each run's first 100 frame lines. The GPU times are counter samples at the encoders'
boundaries (`MTLCommonCounterSetTimestamp`, converted through `sampleTimestamps`); a run of
`base2` with `MTL_GPU_TIME=1` gave 0.142 ms per frame command buffer (median) without the
structures and 0.841 ms with them, against 0.821 ms of lerps, refits and rebuild by the samples,
which places the samples' scale. `r_rt_gunshadow 1` on `base2` adds the view weapon (72
instances on frame 91 instead of 71, `0 weapon` left out).

**Validation.** One run per map with `MTL_DEBUG_LAYER=1` and `r_rt_stats 2`: status 0, only the
validation banner, no failed command buffer, `teardown: 0 live objects` (`rt 309/309` on
`base2`, `463/463` on `waste2`, `74/74` on `base1`).

**ASAN.** `make ASAN=1 client game ref_metal`; `debug/quake2` with `MTL_DEBUG_LAYER=1`,
`r_rt_stats 1` and a scratch script (`map base1`, 200 waits, `map base2`, 200 waits,
`vid_restart`, 300 waits, `quit`): status 0, no sanitizer line, both maps' structures built and
the summary line printed at each map change, two renderer sessions, each teardown `0 live
objects` (`rt 176/176`, then `rt 120/120`).

**No device.** This host cannot show the path without ray tracing (the VM's); the decision is
`rm_rt_wanted` (`rm_rt.c`), host-tested (`r_rt_stats 1` or `2` with no ray tracing on the
device: 0), called by `RM_RT_Wanted` (`rm_rtscene.c`) before anything else in
`RM_RT_MapLoaded` and `RM_RT_Frame`, whose only other effect is one line saying the device has
no ray tracing; `rmb_rt_open` checks `supportsRaytracing` again.

### The G-buffer and the lighting resolve in `ref_metal` (TASK54, RT2)

RT2 adds the G-buffer of `docs/plans/metal-raytracing-design.md`, section 3 (its
subsection "The G-buffer as RT2 built it" has the paths, the contract and the off-state
decision). Host: the bare-metal Mac, `Apple M6`, macOS 27.2.1. Base: `origin/main` at
`aa5ab641` (FIX61). The card was first measured on the earlier base `d9e76e40` (TASK53)
and then rebased onto `aa5ab641`, which adds TASK119 (2D options, all off by default) and
FIX61 (the frame sized by the client's rule, `GLimp_DrawableInPixels`).

After the rebase and the first rework, these were run again on engine commit `f0127557`
(pin ids `f012755714a9-*`), on base `aa5ab641`. The card was then rebased onto `5f476f88`
(TASK11, #308), which replaces the image decoder (ImageIO for stb_image). Every stock TGA
and PNG decodes byte for byte as before, and TASK11's own `metal` and `gl1-metal` pins match
its base byte for byte. So that rebase changes nothing these runs measured. The
card's own source is unchanged by it (`git range-diff` shows every commit equal):

- the pins of `world`, `hud2d`, `models` (seeded) and `particles` (seeded), on both paths;
- the render-size check, the motion-vector check and the particle-size check.

The commits after `f0127557` change only the documentation. The other pin rows, the
jitter check and the ASAN run are of engine commit `fde68b47` (pin ids `fde68b47738c-*`)
on `d9e76e40`, and the timing is of `4bf077a4` on `d9e76e40`. Neither commit is an
ancestor of the head. After them came the rebase and the rework:

- particles sized in render pixels on a view below the frame's size;
- the world normals buffer made by the first G-buffer view instead of at map load;
- `r_rt_gbuffer_show` clamped before it becomes an integer;
- the entity split as a pure function.

On the default path the four scenarios run again are byte-identical to `aa5ab641`'s
`ref_metal`. On the G-buffer path their scores are those `fde68b47` had. Screen unlocked
before every run, no other `quake2`: the wrapper checked both and printed the one-minute
load, 1.95 to 2.98 on the runs on `f0127557`. Every log, frame list, script and result is
in `<artifacts>/TASK54/` (the runs on `f0127557` in `<artifacts>/TASK54/rework1/`) and
`<pin store>`.

```sh
export PINS_STORE=<pin store> PINS_BASEDIR=<game data> MTL_DEBUG_LAYER=1
make client game ref_metal
scripts/pins.sh record metal <scenario>          # PINS_SEED=1 too for models, particles; dlights seeded only
# the G-buffer path: scenarios/<s>-gb.cfg (<s>.cfg plus "set r_rt_gbuffer 1"), deleted after;
# the cinematic by the line appended to cinematic.cfg for one record (only that scenario plays a .cin)
scripts/pins.sh record metal <s>-gb
framediff <gl1 reference frame> <frame> --threshold T --bad-threshold B   # every frame, both references
cmp <base frame> <frame>
# the base on f0127557: a copy of the tree at aa5ab641 built outside the checkout, recorded by
# its own scripts/pins.sh into a scratch store (<artifacts>/TASK54/rework1/basestore-aa5ab641)
```

**Pins on `f0127557`.** Against the `gl1` records of TASK83 (`de1da7fdba66-*`,
`f6bb192775b8-*`) at TASK53's values, and seeded `models` and `particles` against the seeded
`gl1-metal` records of `bdda98128905` at 0.005 and 0.001. "Identical" counts frames that are
byte-identical to `aa5ab641`'s `ref_metal` record of the same settings:

| Scenario | Values | Default path (forward) | G-buffer path, every effect off |
|---|---|---|---|
| `world` | defaults | 23 of 23 each, worst 0.000010, bad 0.000033; 23 identical | 23 of 23 each, worst 0.000493, bad 0.000033; 0 identical |
| `hud2d` | defaults | 23 of 23, 0, 0; 23 identical | 23 of 23, 0, 0; 23 identical |
| `models`, seeded | 0.005, 0.001 | 23 of 23, worst 0.000039, bad 0.000589; 23 identical | 23 of 23, worst 0.000462, bad 0.000596; 0 identical |
| `particles`, seeded | 0.005, 0.001 | 23 of 23, worst 0.000039, bad 0.000589; 23 identical | 23 of 23, worst 0.000471, bad 0.000596; 0 identical |

**Pins on `fde68b47`** (before the rebase). The same references. "Identical" counts frames
that are byte-identical to the base build's `ref_metal` record of the same settings
(TASK53's `0269ca35`, whose frames are the base's: TASK53 drew nothing differently):

| Scenario | Values | Default path (forward) | G-buffer path, every effect off |
|---|---|---|---|
| `world-fullbright` | defaults | 23 of 23, worst 0.000031, bad 0.000072; 23 identical | the same; 23 identical |
| `skywater` | defaults | 23 of 23, worst 0.000010, bad 0.000033; 23 identical | 23 of 23, worst 0.000493, bad 0.000033; 0 identical |
| `cinematic` (`idlog.cin`, every 7th) | defaults | 12 of 12, 0, 0; 12 identical | 12 of 12, 0, 0; 12 identical (no 3D view) |
| `models` | defaults | 23 of 23, worst 0.000084 and 0.000232, bad 0.000612; 22 identical (unseeded) | 23 of 23, worst 0.000549 and 0.000694, bad 0.000618; 0 identical |
| `particles` | 0.02, 0.25 | 23 of 23, worst 0.010542 and 0.010208, bad 0.175205; 7 identical (unseeded) | 23 of 23, worst 0.010410 and 0.010318, bad 0.150456; 0 identical |
| `dlights`, seeded | 0.005, 0.001 | 23 of 23, 0, 0; 23 identical | 23 of 23, worst 0.000501, bad 0; 0 identical |

Every score is under 0.005 but unseeded `particles`, which two unseeded records reach by
themselves (TASK53). Byte identity on the G-buffer path holds where the light is white
(`world-fullbright`, `hud2d`, the cinematic) and not where it is a lightmap or a vertex
colour: the raster stores the texel and the light in `rgba8` and the resolve multiplies the
rounded values, where the stock fragment multiplies them unrounded, so a channel moves by
one level now and then (the largest score added is 0.0005, on `dlights`; a bad fraction
moves by 0.000007 at most). The forward path with depth and motion (`r_rt_motion 1`) is
byte-identical to the default path on all 23 frames of `world` (on `f0127557`), and so to
`aa5ab641`. With `gl_shadows 1` and `gl1_stencilshadow 1` (seeded `models`, on `fde68b47`), the
G-buffer path is within 0.000460 of the default path on every frame (the shadows are the
forward pass's).

**Validation and ASAN.** Every record above ran with `MTL_DEBUG_LAYER=1`. Each `engine.log`
holds:

- `Metal API Validation Enabled`;
- the attachments line on the G-buffer and motion paths;
- `teardown: 0 live objects`;
- no other validation line and no failed command buffer.

So does a G-buffer record of `models` over `demo2.dm2` (`waste2`, every 7th, 89 frames, on
`fde68b47`). On `f0127557` the default path's records end with 8 buffers created and
destroyed, as `aa5ab641`'s do. The G-buffer path's end with 9: the world normals buffer,
which its first view makes. The motion path's end with 8. The ASAN run is of `fde68b47`:

- `make ASAN=1 client game ref_metal`;
- `debug/quake2` with `MTL_DEBUG_LAYER=1`, `r_rt_gbuffer 1` and a scratch script beside
  the binary (`map base1`, 200 waits, `map base2`, 200 waits, `vid_restart`, 300 waits,
  `quit`);
- status 0, no sanitizer line, both renderer sessions `0 live objects` (1280x960
  attachments: `vid_highdpiaware` is 1 by default).

**Motion vectors** (on `f0127557`). `r_rt_motion_test 0.5` takes the previous view to be
this one turned 0.5 degrees about the camera's vertical axis, every entity its own previous
one. `r_rt_gbuffer_show 4` draws the vectors over the view (red and green 0.5 plus the
motion over 16 pixels). The check ran over the 23 frames of `world`:

- `<artifacts>/TASK54/mvcheck.c` gives each pixel's expected motion from the projection
  the log printed (1.0 and 1.333333);
- the bottom 41 rows (the status bar) and the top 33 (the notify lines `developer 1` prints)
  are left out, which leaves 5,975,455 pixels;
- the error is 0.024 px on average and at most 0.062 px, none over 0.1 px, on both the
  G-buffer and the motion path;
- the expected motion is 3.76 px on average and 5.9 px at most;
- the 8-bit view alone rounds by up to 0.031 px a channel.

**Jitter** (on `fde68b47`). `r_rt_jitter_x 0.5`, `r_rt_jitter_y 0.25` against 0, on the G-buffer path: the
colour moves (no frame identical, scores 0.00077 to 0.0073), the depth moves (`r_rt_gbuffer_show
6`: no frame identical, 0.000028 to 0.00011), and the vectors do not: with the turn above,
the jittered frames' vectors still match the unjittered expectation (0.024 px on average,
0.13 px at most, one pixel over 0.1 px, on an edge); a jitter in them would show as a 0.56 px
error everywhere. At 0 every frame is the frames above.

**Render size** (on `f0127557`). `r_rt_gbuffer_scale 0.5` at 640 x 480:

- `G-buffer attachments 320x240`, 2,293,760 bytes allocated;
- `motion attachments 320x240`, 1,146,880 bytes allocated;
- the view stretched into the frame, validation clean, `0 live objects`;
- against the full-size frames of the same path, the scores are 0.000662 to 0.010564
  (G-buffer) and 0.000628 to 0.010548 (motion): the stretch of a half-size image;
- the status bar unchanged.

**Particle size at the render size** (correctness I1 of the first review). Seeded `particles`
on the G-buffer path at scale 1 and at 0.5, each also with `cl_particles 0`: the same frames
without the particles, since the seed keeps the rest the same. Measured: the light the
particles add, the sum over pixels and channels of |with - without| / 255
(`<artifacts>/TASK54/rework1/penergy.c`, the rows above the status bar), over all 23
frames:

- before the fix (`33443533`): 18,214 at scale 1 and 40,100 at scale 0.5, 2.20 times; 2.14
  times as many pixels moved by more than 8 levels;
- after it (`f0127557`): 18,214 and 18,225, 1.00 times; 1.15 times the pixels, from the
  stretch's spread.

Scale 0.5 against scale 1 scores at most 0.012577 before the fix and 0.010065 after. The
frames without particles score the same 0.010065, so the particles add nothing to what the
stretch changes. At scale 1 the particle frames are byte-identical before and after the fix
(23 of 23), and so are the frames without particles at 0.5.

**Cost** (on `4bf077a4`; design section 3 says why the changes after it leave the timed
draws as they were). `MTL_GPU_TIME=1`, the `particles` scenario with `r_vsync 0` and one capture
(`PINS_EVERY=100000`, `PINS_TRACE=0`, a scratch pin store), windowed 640 x 480 and 1920 x 1080,
and `PINS_HIDPI=1` at 2560 x 1440 for a 5120x2880 drawable; base, forward, motion and
G-buffer alternated in each of three rounds; the base is `d9e76e40`'s `ref_metal` with the
same `MTL_GPU_TIME` aid beside the same `quake2`. The table and the decision are in design
section 3; in short, at 5120 x 2880 the frame's drawing takes 1.09 ms (base), 1.10 ms
(forward), 1.73 ms (forward with depth and motion, 136 MB added) and 6.80 ms (G-buffer, 332 MB
added), so the forward path stays the default with every effect off. A first attempt timed the
whole frame command buffer: at 1920 x 1080 the G-buffer's then read 16 ms, the wait for the
previous frame's present, since the raster can start before it; the timing above has none.

**Mutants.** 34 mutants of `rm_gbuf.c` and `common/mtl_gbuf_math.h`
(`<artifacts>/TASK54/mutants.sh`): 33 fail `tests/metal/rm_gbuf_test.c`; the survivor
(`rm_gb_render_size` scaling a scale of 1 to 3) is equivalent, since a side is never above
the frame's. The host test failed 60 of its 108 checks against a stub first; `scripts/pins.sh
selftest framediff_mask` failed on the framediff before `--mask` and passes with it. The
rework added the following:

- 17 mutants of the new pure functions (`rm_gb_entity_split`, `rm_gb_pixel_scale`,
  `rm_gb_point_scale` and `rm_gb_show`, in `<artifacts>/TASK54/rework1/mutants-rework1.txt`).
  16 fail the host test. The survivor (`k < 1` as `k <= 1` in `rm_gb_point_scale`) is
  equivalent, since a scale of 1 multiplies exactly.
- A framediff that divides the masked sum by every pixel instead of the marked ones. It
  passed `selftest framediff_mask` before the rework and fails it now.
- The host test of `rm_gb_show`, run with UBSAN against the old expression. UBSAN reported
  the conversion of 1e10, and 5 checks failed.

### Ray-traced shadows from dynamic lights in `ref_metal` (TASK55, RT3)

RT3 adds the first ray-traced effect, `r_rt_shadows` (`docs/plans/metal-raytracing-design.md`,
section 2). Host: the bare-metal Mac, `Apple M6` (apple9, `supportsRaytracing: yes`), macOS
27.2.1 (26B131). Base: `origin/main` at `33845c6c` (TASK123 and TASK100; neither touches
`ref_metal`). Every run below is of engine commit `3d659a2b` (pin ids `3d659a2bb17b-*`), an
ancestor of the card's head; the commits after it change only the host test and the
documentation. Two results are the round before's, of `4deded44`, and say so: the ledger with
`r_rt_stats 1` and the shadows off (Proof 1) and the fps line (Proof 4); the commits since
change only what runs with the shadows on. After the measurements the lane merged `origin/main`
at `0fb0603c` (merged, not rebased, so `3d659a2b` stays an ancestor). That brought in TASK111
(the MetalFX scalers, which change `RM_GB_Choose`, `RM_GB_Begin`, `RM_GB_End`, `rmb_gbuf.m` and
`rm_main.c`), TASK123, TASK129, TASK100 and TASK130 (the shared image helpers). The merged head
was not measured again. With the scalers off (`r_metalfx 0`, the default, as in every run
here), `RM_GB_Choose` takes the same path as before and TASK111's other hooks do nothing;
TASK111's own bare-metal run found all 14 scenarios byte-identical to its base with the
scalers off (#265). The merge's resolution is in the merge commit. Before every run the wrapper checked that the screen
was unlocked (`ioreg -n Root -d1 | grep -c CGSSessionScreenIsLocked` 0) and that no other
`quake2` ran, and logged the one-minute load: 2.4 to 5.6 (other agents share the host). Every
engine batch held the shared timing lock. Every log, frame, mask, script and result is in
`<artifacts>/TASK55/` (this round in `rework2/`, the round before in `rework1/`) and its scratch
stores; nothing derived from game data is in the repository.

**What it does.** With `r_rt_shadows 1`, on a device with ray tracing, and with gl1's dynamic
lights on the lightmaps (`gl1_dynamic` on, neither `r_fullbright` nor `gl1_flashblend`), a frame
whose instance structure was built takes the G-buffer path. Before the raster builds the
lightmaps, `RM_RTS_View` settles everything the trace needs that can fail (the view's inverse
and the kernel's outputs at the render size, `rmb_rts_prepare`); only then does gl1's
dynamic-light term leave the lightmaps (`r_dlight_term`), and only until the resolve. A frame
that cannot trace keeps the stock lightmaps. The remaining failures are a command buffer or an
encoder Metal does not give, which lose the frame anyway. A compute pass between the raster and
the resolve (`rm_rts_trace`, `rm_rtshadow.metal`) does the following for each lightmapped world
pixel (material 1):

- reconstructs the position from the depth and takes the normal from the G-buffer;
- finds the pixel's surface by a ray from the eye to that point, through a triangle-to-surface
  table (`rm_rts_surface_map`) built when the shadows are first on for the map;
- moves the pixel into that triangle's model space (the hit's world-to-object transform, the
  identity for the world), because gl1 marks and lights a brush entity's surfaces with the
  light's untransformed world origin against the model-space planes and texture axes
  (`R_MarkBrushLights`); the light stays where it is (`mtl_rts_surf`);
- skips a surface without light samples, which gl1 lights white and adds no dynamic light to
  (`R_BuildLightMap`; the table marks its triangles, `MTL_RTS_SURF_UNLIT`);
- evaluates gl1's `R_AddDynamicLights` for every light on that surface's own texture axes and
  shifts: at the four luxels around the pixel, interpolated as the bilinear lightmap sample
  (`mtl_rts_add_lm`, `mtl_rts_surf_light`);
- traces one shadow ray toward each of the four lights that add the most (`intensity - dist`),
  which passes the models that carry its light (*a light's own model*, below).

The eye ray is cast where a light reaches the pixel's plane in the world, and on every world
pixel of a frame with a moved or turned brush entity (its pixels are tested in its model's
space, where the world's test does not hold).

It writes the four visibilities at 8 bits each with the four light indices (an `RG32Uint`) and the
term, `visibility x add x colour` summed over the lights (an `RGBA16Float`). The resolve adds the
term to the static lightmap's sample as `R_BuildLightMap`'s store would (`mtl_rts_light`: scaled
down by the brightest channel past 1). A light past the four adds its light unshadowed.

- *Ray origin.* The surface point moved along its normal by `0.5 + k d^2` units, `d` the distance
  from the eye and `k = 4 x 2^-24 / gl_znear` (`mtl_rts_bias`). The depth buffer's step at `d` is
  `2^-24 d^2 / znear`, so the start is above the true plane within four depth steps. Half a unit is
  under the thinnest brush (one unit). The ray ends half a unit short of the light.
- *Translucency.* A per-triangle transmission built at map load (`rm_rts_transmit`): water and
  translucent triangles pass 171/255 (`SURF_TRANS33`) or 87/255 (`SURF_TRANS66`), as gl1 draws
  them. Water drawn opaque, the sky, the world and models stop the ray. On a map with such
  triangles a ray looks past each one, nearest first (at most 8). Neither demo map has any
  (TASK53's counts), so the frames below do not exercise it; the host test does.
- *Alias models* are lit as before, by `R_LightPoint`, whose vertex light holds the dynamic
  lights once; the resolve adds no term to material 2. They cast shadows (they are in the
  instance structure, the view weapon only with `r_rt_gunshadow`); they receive none.
- *A light's own model.* An alias model does not shadow a light it carries: one whose origin,
  taken into the model's space, lies in gl1's box of the model's frame and old frame
  (`R_CullAliasModel`) grown by one unit on every side (`rm_rts_carried`, `RM_RTS_CARRY_TOL`).
  That is a rocket's or a bolt's light inside its own model, an explosion's at the origin of its
  model, a monster's muzzle flash at the origin of the flash and smoke models it spawns and at
  the gun of the monster that fires it. `RM_RT_Frame` finds them per frame for the lights as
  `R_TakeDlights` takes them, and `rmb_rt_frame` keeps each instance's lights beside its first
  triangle and gives a carrier its own instance mask (`mtl_rts_inst_mask`). A light no model
  carries keeps the any-hit query over every instance. The ray of a carried light first tests
  every instance but the carriers, as any light; then the carriers alone, the nearest first: it
  passes a triangle of a model that carries this light (`mtl_rts_passes`) and stops at one that
  does not (`rm_rts_visibility`; past 16 of its own model's triangles it counts as through).
  Nothing is non-opaque. Real shadows stay: the world and brush entities are never carriers,
  and another model shadows the light unless the light is inside that model's own box. The unit
  covers the rounding of the transform and a light on the box's face (a gun's tip).
- *Where it cannot match per texel.* The term is added after the store, not before:
  - the store truncates the sum and scales a saturated luxel down before filtering; the resolve
    does so per pixel after filtering (within two levels on the host test's luxels);
  - `vid_gamma` other than 1 and `gl1_minlight` are applied by gl1 to the sum and here to the
    static part only;
  - `metal_lightmap_filter 1` filters the static part with its cubic, while the term is
    interpolated bilinearly;
  - a pixel whose eye ray does not meet its own triangle (an edge) takes the world's space, the
    base axes and the continuous falloff (on a moved brush entity, its world position).

**Proof 1, effect off.** Every scenario recorded on `3d659a2b`, with `MTL_DEBUG_LAYER=1` and
`PINS_SEED=1`, against the round before's records of the base `33845c6c` (an export of the tree
built in scratch, recorded by its own `scripts/pins.sh` into a scratch store, the same settings):

- 14 scenarios, 23 frames each (`cinematic` 12), and `dlights` at `PINS_DEMO=demo2.dm2
  PINS_EVERY=7` (89 frames): byte-identical to the base on every frame. The scenarios are
  `world`, `world-fullbright`, `hud2d`, `hud2d-sharp`, `hud2d-scale8`, `world-scale8`,
  `world-lmcubic`, `skywater`, `dlights`, `dlights-lmcubic`, `models`, `particles`,
  `particles-cap` and `cinematic` (`idlog.cin`, every 7th).
- Every log holds the validation banner and no other validation line, no failed command buffer,
  `teardown: 0 live objects` and `rt 0/0`: nothing ray-traced is built or allocated.
- With `r_rt_stats 1` and `r_rt_shadows 0` (the same trace replayed by both builds; measured on
  `4deded44`) the head allocates what the base does: the same ledger (`rt 66/66`), the same
  structure lines, no shadow line, the frames identical. `rmb_rt_frame` takes no ring entry and
  keeps no scene without the shadows, and the map's shadow tables and transmission buffer are
  built when the shadows are first on. Without the shadows every instance keeps TASK53's mask
  (`0xff`) and no carrier is looked for.

Since the frames are the base's byte for byte, their scores against TASK83's `gl1` references
are the base's, those TASK54 recorded at TASK28's values. They were not compared again.

**Proof 2, effect on.** The double-count check compares, pixel for pixel, a view with the effect
on and the same view with it off, on the pixels an `r_rt_shadows_show 1` mask marks (no light in
range). Its two views must have been given the same lights. A control settled whether separate
records do: three RT-off records of `dlights` at `demo2.dm2` every 7th, and two at `demo1.dm2`,
are byte-identical, and a developer print (since removed) showed the same light radii and times
on every one of the 617 frames of each `demo2.dm2` run. The demo's frame time does not reach
the lights (in a timedemo `cl.time` steps by the demo's 100 ms). So the round before's failures
were not a frame-time effect. To give every view the same `refdef_t` all the same, each view
below is a replay of one live record's trace (the harness's TASK46 replay, bit-identical to live
by construction). The trace's cvar snapshot is the only thing that differs between the replays:
the value of `r_rt_shadows`, `r_rt_shadows_show` or `r_rt_gbuffer`, rewritten in a copy of the
trace with the same length (the replayer restores it over the command line). Each record's
"on" replay is byte-identical to the live record.

The cause of the four failing `demo2.dm2` frames (33, 36, 76, 77) was the plane, not the time.
Their lights are explosions, which sit an eighth of a unit above the floor they hit. The pass
measured each light against the G-buffer's normal, 10 bits an axis and off by about 1/1000, so
over a hundred units the light fell behind the floor on one side of a line through its foot.
The floor there went dark where gl1 lit it. The pass now takes the surface's own plane for a
found surface (`rm_rts_plane`, `mtl_rts_surf`), and the world's test before the search allows for
the normal's and the depth's error (`mtl_rts_near`). The same replays then passed.

Records (`PINS_SEED=1`): `rt-shadows` and `rt-shadows-entities` (`cl_entities 1`) at `demo1.dm2`
(23 frames) and `demo2.dm2` every 7th (89 frames). Against the G-buffer path with the effect off,
the RT-off pixel on the path the effect takes:

| Record | Frames | Masked pixels a frame | Worst max, worst masked score |
|---|---|---|---|
| `rt-shadows`, `demo1.dm2` | 23 | 48,559 to 307,100 | 0, 0.000000 |
| `rt-shadows`, `demo2.dm2` every 7th | 89 | 111,665 to 306,963 | 0, 0.000000 |
| `rt-shadows-entities`, `demo1.dm2` | 23 | 49,294 to 307,100 | 0, 0.000000 |
| `rt-shadows-entities`, `demo2.dm2` every 7th | 89 | 115,138 to 306,963 | 0, 0.000000 |

The invariant holds on every frame of the four. Against the default path the masked pixels
differ by what the G-buffer path itself differs from it with the effect off, frame for frame
the same max and score: `rt-shadows` within 1/255 at `demo1.dm2`, up to 3 on `demo2.dm2`
frame 9; with entities up to 44 (`demo1.dm2`) and 67 (`demo2.dm2`). That is TASK54's path, not
this pass.

- *Moved brush entities.* Both entity records hold doors and lifts that move (`demo1.dm2`: `*50`
  in 25 places, `*42` and `*43` in 14; `demo2.dm2`: `*32` in 33, `*36` in 9), with lights in 270
  and 238 of their frames. The host test holds the review's probe (a floor at model z 0, its
  entity at z -200, a light at world (8, 8, 50) of radius 200: gl1 adds 138 at luxel 0, and so
  does the pass) and 200 turned and moved faces at every luxel.
- *Alias models* (the round before's replays, `4deded44`). With the material view
  (`r_rt_gbuffer_show 3`): of
  1,180,417 alias-model pixels none is in range and none changes with the effect; every pixel
  the effect changes (1,895,766) is a world pixel with a light in range.
- *A frame with nothing in range* (13 of 23 at `demo1.dm2`, 59 of 89 at `demo2.dm2`): its mask is
  white wherever the 2D is not drawn. `r_rt_shadows_show` draws its view with no light in range
  when nothing is traced.

Per frame (the classes view, `r_rt_shadows_show 2`, of the same replays), pixels with a light
in range, of 307,200:

- `rt-shadows`, `demo1.dm2`: 0 to 258,122 (mean 49,046); traced 38,616 on average, 0.84 %
  shadowed;
- `rt-shadows`, `demo2.dm2`: 0 to 194,858 (mean 34,991); traced 28,392, 0.92 % shadowed;
- `rt-shadows-entities`, `demo1.dm2`: 0 to 257,387 (mean 47,999); traced 37,701, 1.71 %
  shadowed (14,793 of 867,130 traced pixels), 48.4 % before the rule;
- `rt-shadows-entities`, `demo2.dm2`: 0 to 191,385 (mean 32,929); traced 26,637, 4.76 % shadowed
  (112,935 of 2,370,702), 36.6 % before.

Both records without entities are as the round before, frame for frame. The round before's own
four traces, replayed on `3d659a2b`, give the same classes as this round's records: the same
`refdef_t` before and after the rule.

*A light's own model, the numbers.* Before the rule a monster's muzzle flash put its light at the
very origin of the flash and smoke models it spawns, and a rocket or a bolt carries its light
inside its own model, so every ray to such a light met the model around it. Per frame with a
light in range, in range / shadowed pixels, with the round before's shadowed in brackets:

- `demo1.dm2` (10 of 23 frames): 5: 12,384 / 374 (6,110); 7: 59,708 / 3,665 (40,180); 8:
  257,387 / 0 (242,931); 10: 95,665 / 0 (70,898); 14: 91,601 / 4,919 (4,919); 16: 122,324 /
  1,894 (1,894); 18: 42,828 / 0 (0); 19: 125,334 / 0 (0); 20: 195,355 / 383 (383); 21: 101,394 /
  3,558 (52,217);
- `demo2.dm2` (29 of 89 frames): 4: 179,337 / 583 (583); 5: 174,901 / 2,421 (2,421); 6: 139,373 /
  32,038 (32,038); 9: 99,101 / 0 (0); 10: 95,798 / 157 (157); 11: 103,579 / 203 (203); 12:
  101,197 / 5 (5); 13: 105,326 / 21 (21); 14: 62,825 / 55 (16,935); 15: 72,986 / 3,249 (3,249);
  21: 125,851 / 7,365 (7,365); 23: 191,385 / 1,006 (166,114); 27: 24,966 / 284 (6,153); 28:
  87,827 / 0 (0); 32: 39,748 / 2,232 (3,694); 33: 170,228 / 24,648 (93,419); 34: 11,597 / 755
  (3,166); 36: 29,269 / 4,065 (24,008); 42: 93,374 / 0 (0); 44: 109,545 / 89 (78,832); 45:
  104,158 / 793 (12,926); 47: 96,967 / 2,975 (2,975); 49: 91,112 / 30 (30); 58: 83,770 / 404
  (404); 76: 122,492 / 20,774 (83,010); 77: 7,887 / 582 (1,660); 78: 81,834 / 1 (40,296); 82:
  174,737 / 5,597 (153,984); 83: 149,518 / 2,603 (134,284).

`demo1.dm2` frame 8 goes from 242,931 shadowed pixels of 242,931 traced to none, `demo2.dm2` frame
23 from 166,114 of 166,114 to 1,006. What is left is explained by two diagnostic builds (never
committed: `RM_RT_Frame` patched in a scratch build, the same replays of the classes):

- every alias model passing every light: 0.85 % (`demo1.dm2`) and 0.81 % (`demo2.dm2`) shadowed,
  the world's and the brush entities' share, as without entities (0.84 % and 0.92 %);
- the carriers' boxes grown by 15 more units: 1.17 % and 3.14 %.

So of `demo2.dm2`'s 112,935 shadowed pixels the world and the brush entities cast 19,139, models
the light is within 16 units of but outside 38,514, and models farther from it 55,282
(`demo1.dm2`: 7,406, 4,622 and 2,765). Those are real shadows: a model between the light and the
surface, which gl1 never draws and the effect is for.

*Real shadows stay.* `demo2.dm2` frame 6 keeps its 32,038 shadowed pixels, the same as before the
rule; with every alias model passing every light only 50 are left (the world's), so models cast
the other 31,988 (22,800 of them are within 16 units of the light). Frame 21 keeps 7,365, all of
them the world's (as with the models passing), and frame 23's 1,006 are the world's too.

The per-frame lists are in `<artifacts>/TASK55/rework2/invariant/`, the diagnostics in
`rework2/e84fed75/diag/`.

**Proof 3, look.** For the owner, in `<artifacts>/TASK55/sidebyside/` (off, on and the classes
view of the same frame, replays of one trace each, `3d659a2b`; its README gives the counts). The
frames with the most shadowed pixels show the effect best:

- `rt-shadows`, `demo1.dm2` frames 21 and 7 (about 3,500 shadowed pixels);
- `rt-shadows`, `demo2.dm2` frames 21 (7,363), 36 (4,344), 76 (3,902) and 15 (2,598);
- `rt-shadows-entities`, `demo2.dm2` frames 6 (32,038) and 33 (24,648), shadows of models, and
  76 (20,774), `demo1.dm2` frame 14 (4,919);
- `rt-shadows-entities`, `demo1.dm2` frame 8 and `demo2.dm2` frame 23, the round before's open
  finding: lit now (0 and 1,006 shadowed).

**Proof 4, performance** (no validation layer; a scratch store; the timing lock held; the load
2.4 to 4.4, other agents on the host):

- `timedemo.txt` at 1920 x 1080 (`4deded44`), `dlights` and `rt-shadows` alternated, three
  rounds: 60.0, 59.9 and 60.0 fps off, 59.9, 60.0 and 59.9 on (689 frames in 11.5 s), the
  display's rate (TASK54).
- The shadow pass's GPU time per frame: timestamps at the compute encoder's boundaries, the
  `r_rt_stats 2` line, over `demo1.dm2` whole. Warm: each run's frames past its first 100 that
  traced, 308 a run (309 with entities). Three runs each of `rt-shadows` and
  `rt-shadows-entities`, alternated:

| Size | Entities | Median (run medians) | p10 | p90 | Max |
|---|---|---|---|---|---|
| 1920 x 1080 | off | 2.550 ms (2.546 to 2.557) | 1.609 to 1.624 | 3.476 to 3.488 | 4.662 |
| 1920 x 1080 | on | 3.577 ms (3.483 to 3.633) | 2.007 to 2.026 | 4.791 to 4.962 | 8.102 |
| 5120 x 2880 | off | 8.525 ms (8.486 to 8.597) | 5.427 to 5.511 | 11.602 to 11.908 | 22.912 |
| 5120 x 2880 | on | 9.053 ms (8.504 to 9.473) | 6.103 to 6.325 | 12.933 to 13.595 | 34.100 |

5120 x 2880 is `PINS_HIDPI=1` at 2560 x 1440. Rays a pixel (the eye rays and the shadow rays):
0.86 without entities, 1.09 with them; 31.5 % and 30.7 % of pixels have a light in range, 23.4 %
and 22.7 % trace. Without entities the round before measured 2.546 and 8.681 ms.

- *The rule's own cost*, against the build without it: the two 1080p traces of the records
  above replayed by three builds beside the same `quake2`, three rounds alternated. `37b7a7da`
  (the round before's head, no rule) 2.509 ms without entities and 3.275 ms with them; this
  round's first build (`e84fed75`: carriers non-opaque, the carried light's ray a ray query)
  3.048 and 3.854; `3d659a2b` (instance masks) 2.528 and 3.475. The query cost half a
  millisecond even without entities, a heavier kernel for every pixel, and was replaced; the
  masks cost 0.02 ms without entities and 0.20 ms (6 %) with them. Both builds give the same
  frames.

**Proof 5, validation and ASAN.**

- Validation: every record and replay of Proofs 1 and 2 ran with `MTL_DEBUG_LAYER=1` (15 records
  with the effect off, 4 with it on and 24 replays of them): the banner and no other validation
  line, no failed command buffer, `teardown: 0 live objects` (`rt 0/0` with the effect off; on,
  `rt 75/75` and `rt 54/54` without entities, `rt 472/472` and `rt 318/318` with them, as the
  round before).
- ASAN: `make ASAN=1 client game ref_metal`, then `debug/quake2` with `MTL_DEBUG_LAYER=1` and a
  scratch script beside the binary, deleted after with the `config.cfg` the run saves. The
  script sets `r_rt_shadows 1`, `r_rt_stats 1` and `cheats 1`, then plays `map base1`, the
  blaster fired for 200 frames (its bolts carry their light), `vid_restart`, firing again, `map
  base2` and firing again.
- Result: status 0, no sanitizer line, no validation line past the banner, the pass's targets
  made in each renderer session (2560 x 1920), an instance structure in 446, 441 and 445 frames
  (the `r_rt_stats` summaries of the three maps' sessions), both teardowns `0 live objects`
  (`rt 93/93`, `rt 186/186`).

**Host test and mutants.** `tests/metal/rm_rtshadow_test.c` (2,767 checks), run by
`tests/check-ref-metal.sh`. It holds the falloff to `rm_lm_dlight`:

- at every luxel of 600 axial faces (front and back);
- at every luxel and a point inside every cell of 400 faces with scaled, rotated, sheared and
  shifted axes;
- at every luxel of 200 faces of turned and moved brush entities, in their model's space with the
  light's world origin, and the review's probe;
- at every luxel of a floor with an explosion's light an eighth above it, the floor's normal as
  rgb10a2 returns it (55 of its 100 lit luxels have the light behind that normal);
- and none on a surface without light samples.

It also holds the in-range margin, the choice of four, the packing, the bias, the resolve's light
against `rm_lm_store`, the unprojection, the transmission, the surface map with its unlit bit,
the plane and the world's test. For a light's own model it holds gl1's box of two frames, and
`rm_rts_carried`: lights at the model's origin, inside, just inside and just outside the grown
box on each side under a turn, a second monster between a muzzle flash and a wall (it carries
none of it), the flash at the gun of the monster that fires it, a scaled and sheared transform,
400 models turned every way against the box test in their own space, 32 lights and no more, an
inverted box, a basis too small to invert and lights that are not finite; and the light bit and
the instance and ray masks.

Failing first:

- the card's first round: against a stub `rm_rtshadow.c`, 53 of 2,160 checks; with the shared
  header stubbed too, 2,045;
- rework 1, each fix reverted in a copy: the pixel kept in the world's space fails the probe;
  an unlit surface lit fails 2 checks, the table without the unlit bit 1;
- the four `demo2.dm2` frames failed the replayed check before the exact plane and pass after;
- rework 2, a light's own model: against stubs of `rm_rts_alias_box`, `rm_rts_carried`,
  `mtl_rts_light_bit` and `mtl_rts_passes`, 18 of 2,755 checks; `demo1.dm2` frame 8 and
  `demo2.dm2` frame 23 shadowed everywhere before the rule and lit after (Proof 2).

Mutants:

- the card's first round's 36: 34 killed, 2 equivalent;
- rework 1's 11, of `common/mtl_rts_math.h` and `rm_rtshadow.c`: 9 killed; the two survivors
  are equivalent (`sf.unlit = unlit` for `unlit ? 1 : 0`, the field only tested for zero; `>` for
  `>=` in the surface map's bound, an index at the bit needs 2^31 surfaces);
- rework 2's 16, of the box, `rm_rts_carried`, the light bit, `mtl_rts_passes` and the masks
  (`<artifacts>/TASK55/rework2/tdd/`): 15 killed. The survivor is equivalent: without the test
  for a transform that is not finite, a basis that is not finite gives a determinant that is not,
  and a translation that is not finite fails every box test, so the result is 0 either way. Three
  of the 16 survived the first run (an inverted box, a near-singular basis, no cap at 32 lights)
  and are killed by the checks added for them.

### MetalFX scalers for the 3D view in `ref_metal` (TASK111)

TASK111 makes the MetalFX scalers selectable for the world view
([decision 0011](../decisions/0011-metalfx-framework.md); the cvars are in
`doc/040_cvarlist.md`). The scaler runs at the seam of the G-buffer contract
(`docs/plans/metal-raytracing-design.md`, section 3): a mode asks for the motion path
(`RM_FX_WantsMotion`), the view is drawn at the input size at the target's origin
(`RM_FX_Begin`, from `RM_GB_Begin`), and `rmb_gbuf_view_end` hands the render-size colour,
depth and motion to `rmb_fx_upscale` in place of the bilinear stretch. The output target is
the world view's rectangle, copied into the frame at that rectangle; the 2D draws after it.
Every decision is a pure function in `rm_metalfx.c`, held by `tests/metal/rm_fx_test.c`.

Host: this VM, `Apple Paravirtual device` (`apple5` only, no ray tracing), macOS 27.2.1, SDK
27.2. The first runs were of engine commit `27fd81fd` on `origin/main` at `33845c6c`; after
the review round the branch has `origin/main` at `9039f602` (TASK129) merged, and the pins
below were recorded again at `a8e0dd77` against a base built at `9039f602`. Every log,
script and result is in `<artifacts>/TASK111/`; the pin store is a scratch one.

**What `supportsDevice:` reports here** (three runs of a probe, and the renderer's own line
at every start, `ref_metal: MetalFX supportsDevice: ...`):

| Descriptor | This VM | Mode |
|---|---|---|
| `MTLFXSpatialScalerDescriptor` | YES (a 320 x 240 to 640 x 480 `BGRA8Unorm` scaler is made) | `r_metalfx 1` |
| `MTLFXTemporalScalerDescriptor` | NO (it reports an input content scale of 1 to 3 all the same) | `r_metalfx 2` |
| `MTLFXTemporalDenoisedScalerDescriptor` | NO (1 to 3 as well) | `r_metalfx 3` |
| `MTLFXFrameInterpolatorDescriptor` | NO | `r_metalfx_interp 1` |

So `r_metalfx_modes` reads 3 here. The M6 reported YES for all four (design section 8).

```sh
export PINS_STORE=<scratch store> PINS_BASEDIR=<game data> MTL_DEBUG_LAYER=1
make client game ref_metal
# the base: a copy of the tree at the merge base built outside the checkout, run as PINS_BIN
PINS_BIN=<copy>/release/quake2 scripts/pins.sh record metal <s>    # PINS_SEED=1 for the seeded ones
scripts/pins.sh record metal <s>
scripts/pins.sh compare metal <s> --ref metal --ref-id <base id> --cand-id <id> --threshold 0 --bad-threshold 0
cmp <base frame> <frame>                                            # every frame
```

**Defaults: every scenario against the base** (`r_metalfx 0`, `r_metalfx_interp 0`; at
`a8e0dd77` against `9039f602`, and the same at `27fd81fd` against `33845c6c`):

| Scenario | Result |
|---|---|
| `world`, `world-fullbright`, `world-lmcubic`, `world-scale8`, `hud2d`, `hud2d-sharp`, `hud2d-scale8`, `skywater` | 23 of 23, worst 0, bad 0; 23 byte-identical each |
| `models`, `particles`, `particles-cap`, `dlights`, `dlights-lmcubic` (`PINS_SEED=1`) | 23 of 23, worst 0, bad 0; 23 byte-identical each |
| `cinematic` (`idlog.cin`, every 7th) | 12 of 12, worst 0, bad 0; 12 byte-identical |

Each `engine.log` holds `Metal API Validation Enabled` (the layer's own line, on the stream
the check greps, so a validation message would land there too), no other validation line, no
failed command buffer, and `teardown: 0 live objects (... rt 0/0, metalfx 0/0)`: no MetalFX
object is made with the defaults. The supportsDevice line asks the descriptors' class
methods only.

**The spatial scaler on this VM** (`r_metalfx 1`, scratch scenarios `<s>.cfg` plus `set
r_metalfx 1`, `set r_metalfx_scale <k>`, `set metal_stats 1`, deleted after; `models` and
`particles` seeded): `world`, `models` and `particles` at `r_metalfx_scale` 0.5, 0.75, 0.34
and 0 (the default, which is 1 at a 480-line output) each draw all 23 frames, validation
clean, `teardown: 0 live objects` with `metalfx 2/2` (the scaler and its output target), and
the `metal_stats` line, for example `metalfx spatial: input 320x240, output 640x480 at 0,0,
reset first, scaler not timed` on the first view and `reset none, scaler 0.170 ms` after.
The input sizes were 320 x 240, 480 x 360, 218 x 163 and 640 x 480 at `27fd81fd`. The review
round rounds an input side up, so the output-to-input ratio never passes the clamp: at
`a8e0dd77` the 0.34 run of `world` takes 218 x 164 (ratio 2.94 at most), with the same
validation, teardown and ledger. The scaler's GPU time,
from the 11 timed lines of each run (not a quiet window, a VM's paravirtual GPU, one run
each: a range, not a measurement for the design): 0.000 to 0.555 ms, medians 0.096 to
0.329 ms.

**Fallbacks** (the committed scenarios at `a8e0dd77`, no `metal_stats`, compared pixel by
pixel, since the TGA header names the scenario, so the files differ even where the pixels
do not; `world-metalfx1` at `a8e0dd77` is also 23 of 23 pixel-identical to its record at
`27fd81fd`):

- `world-metalfx2`: `ref_metal: r_metalfx 2 (temporal) is not supported on this device, it
  reads 1 (spatial)`; 23 of 23 frames pixel-identical to `world-metalfx1`'s.
- `world-metalfx3`: `r_metalfx 3 (temporal denoised) is not supported on this device, it
  reads 1 (spatial)`; 23 of 23 pixel-identical to `world-metalfx1`'s.
- `world-metalfx-interp`: `r_metalfx_interp 1 is not supported on this device, it reads 0`;
  23 of 23 pixel-identical to `world`'s, `metalfx 0/0`.

Here the best mode below 2 and 3 is 1, not 0, so their frames are the spatial scaler's;
the interpolator's fallback is the case whose frame equals the off one. The line is
printed when the renderer starts, so a map load clears it from the notify lines and it
is not in the frames. A first version printed it at the first view, and every frame that
showed it differed.

**Reset events** (a live session, `metal_stats 2`, which prints the line on every scaled
view, `r_metalfx 1`, `r_metalfx_scale 0.5`, marker lines between the events;
`<artifacts>/TASK111/resets.log`):

```
alias w "wait;wait;wait;wait;wait;wait;wait;wait;wait;wait"
alias w50 "w;w;w;w;w"
map base1                        // reset first
teleport 128 -219 36             // 100 units: no reset
teleport 1128 -219 36            // 1000 units: reset cut
teleport 128 -319 36             // reset cut
viewsize 80                      // reset size rect: input 256x192, output 512x384 at 64,48
viewsize 100                     // reset size rect
r_metalfx_scale 0.75             // reset size: input 480x360
fov 30                           // no reset
menu_playerconfig                // the model view over the world: no reset
forcemenuoff
r_metalfx 0, then r_metalfx 1    // reset first on the first scaled view
r_metalfx 2                      // reads 1 with its line, no reset
r_metalfx_interp 1               // reads 0 with its line
vid_restart                      // teardown 0 live objects, metalfx 10/10; then reset first
map base2                        // reset first
quit                             // teardown 0 live objects, metalfx 2/2; status 0
```

(`w50` between the steps.) Every expectation held, on the frame of the event; the 1,303
other lines say `reset none`. The same run at `a8e0dd77`, where the step is `rm_fx_step` and
`rm_fx_frame_end`, gave the same reset lines (`resets2.log`). A change of mode between two scalers (`reset mode`) cannot
happen on this VM, which has one; the host test holds it.

**A known edge** (from the bare-metal host): with a scaled view the smooth-point particle
cap of 256 pixels applies in render pixels (`rm_gb_point_scale`, TASK54), so a particle
above 256 frame pixels is drawn smaller than its stock size. Reaching it needs
`gl1_particle_max_size` above 256. The cap is not changed; `doc/040_cvarlist.md` says so
under `r_metalfx`.

**Mutants.** 112 mutants of `rm_metalfx.c` (`<artifacts>/TASK111/mutants.py`, one text
replacement each, built with ASAN and UBSAN, run with a 20 s bound), over every function and
every operand of its compound guards, the review round's included (the rounding of a side,
`rm_fx_same_size`, `rm_fx_step`, `rm_fx_frame_end`, the refusal memo, `rm_fx_note_format`):
109 fail `tests/metal/rm_fx_test.c` (by a failed check, a sanitizer, or the bound:
`rm_fx_halton` with base 1 loops). Two survivors were missing cases and have them now: `v <
0` as `v < -1` in `rm_fx_mode_of` (`-1` truncates to `-1`), and the floor of a side lowered
to 0 (a product inside the rounding slack). The refusal memo without its mode (review I1)
fails 2 checks. Three are equivalent:

- `m < 0` as `m < -1` in `rm_fx_supported`: the loop never runs for `m` at or below 0;
- `m > RM_FX_OFF` as `m >= RM_FX_OFF` there: bit 0 gives off, as the end does;
- the scale's finite test dropped in `rm_fx_sizes`: `!(scale > 0)` takes a NaN, and an
  infinite scale gives the output's size, as 1 does.

The host test failed 141 of its 241 checks against a stub first.

#### On the M6 and an M1-family Mac (the card's T2 and T1 items)

The card stays open for these. Screen unlocked, nothing else running, a quiet window for
the timings. Expected first line at start, from the M6's section 8 probe:
`ref_metal: MetalFX supportsDevice: spatial yes, temporal yes, temporal denoised yes, frame
interpolator yes (r_metalfx_modes 31)`; on the M1, record what it prints.

```sh
export PINS_STORE=<scratch store> PINS_BASEDIR=<game data> MTL_DEBUG_LAYER=1
make client game ref_metal
# defaults against the base, as above: every scenario score 0, bad 0, metalfx 0/0
# each supported mode at three scales: scratch scenarios, deleted after
for s in world models particles; do for m in 1 2 3; do for k in 50 75 34; do
  { cat scripts/pins/scenarios/$s.cfg; printf 'set r_metalfx %s\nset r_metalfx_scale 0.%s\nset metal_stats 1\n' $m $k; } \
    > scripts/pins/scenarios/$s-fx$m-$k.cfg
  PINS_SEED=1 scripts/pins.sh record metal $s-fx$m-$k
  rm scripts/pins/scenarios/$s-fx$m-$k.cfg
done; done; done
```

Expected in each `engine.log`: `Metal API Validation Enabled` and no other validation line;
for `r_metalfx 2` lines `metal_stats: metalfx temporal: input <w>x<h>, output 640x480 at
0,0, reset first, ...` then `reset none, scaler <t> ms`; for `r_metalfx 3`, once,
`r_metalfx 3 (temporal denoised) runs without a ray-traced effect as 2 (temporal)` and the
temporal lines; at the end `teardown: 0 live objects` with `metalfx 2/2`. The temporal
scaler is new to this card on real hardware: the VM could not run it.

- *The jitter's sign.* `jitterOffsetX` and `jitterOffsetY` take the jitter as applied to the
  projection (`rm_gb_jitter`: +x right, +y down, in input pixels), by MetalFX's description
  ("the pixel offset this scaler samples to return to the frame's reference frame"). This
  is the one assumption the VM could not check. A still view (the live script's `map base1`
  and 300 frames without input, `r_metalfx 2`, scale 0.5) must settle to a still, sharp
  image. Edges that shimmer by about a pixel mean the sign is wrong: negate both in
  `rmb_fx_upscale`.
- *The reset events.* The live script above, with `r_metalfx 2`; add `r_metalfx 1` then
  `r_metalfx 2` for `reset mode`. Expected as on the VM.
- *GPU time per mode and scale.* With `metal_stats 1` the scaler's pass runs in a command
  buffer of its own and the line gives its GPU time. Per host, mode and scale: three runs of
  `particles` at `PINS_HIDPI=1` and the native size, frames 101 on. Record the median and
  range of the lines here, as a range.
- *Frame generation* (`r_metalfx_interp 1`) is not built: it reads 0 with `r_metalfx_interp
  1 has no frame generation in this build, it reads 0`. Its acceptance items wait for the
  card that builds it.

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
(`--threshold 0.034 --bad-threshold 0.68`; on the accelerated references
re-measured as 0.031 / 0.66, see the TASK83 section), and for `world`, `hud2d`,
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

- [TASK21](../runbook/TASK21.md): Closing TASK21 on the build host
- [TASK39](../runbook/TASK39.md): Observed on the first run
- [TASK40](../runbook/TASK40.md): Observed on sky, water and cinematics
- [TASK41](../runbook/TASK41.md): `gl1` references and the `soft` against `gl1` thresholds (card P3)
- [TASK49](../runbook/TASK49.md): The `world` scenario against the software renderer
- [TASK50](../runbook/TASK50.md): Sky, water and translucent surfaces against the software renderer
- [TASK51](../runbook/TASK51.md): The `models` scenario against the software renderer
- [TASK52](../runbook/TASK52.md): Particles against the software renderer
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
