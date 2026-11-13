# Runbook: the Metal port and ray-tracing loop

Plan anchor: `docs/plans/metal-renderer.md`, as revised by INFORM8 (#35: pin
at two seams, shim first, then native `ref_metal`) and INFORM9 (#36: pins
from the owner's game data, in a local store, never committed). Parent
cards: TASK27 (#37, harness), TASK21 (#28, OpenGL-subset shim), TASK28
(#38, native `ref_metal`), TASK24 (#31) and TASK25 (#32, ray tracing). The
cards still to file are in `docs/plans/metal-port-cards.md`.

This document is the operating procedure: what the harness is, how a
renderer is judged with it, and what has not been verified yet.

## What is in the tree (the TASK27 core)

| Piece | Where | Verified |
|---|---|---|
| Command-line latch of the three pin arguments | `CL_PinsLatch` in `src/client/cl_pins.c`, called from `Qcommon_Init` | `tests/check-pins-latch.sh` green; never run inside the engine |
| Frame writer, no stb | `VID_WritePinFrame` in `src/client/vid/vid.c` | compiles and links; never run |
| No renderer fallback while capturing | `VID_CheckChanges` in `src/client/vid/vid.c` | compiles and links; never run |
| Capture before present | `SCR_PinsCapture` in `src/client/cl_screen.c` | compiles and links; never run |
| gl1 screenshot flushes the batched 2D quads before reading | `R_ScreenShot` in `src/client/refresh/gl1/gl1_misc.c` | compiles and links; never run |
| Unattended quit at end of demo | `CL_PinsFrame` in `src/client/cl_main.c` | compiles and links; never run |
| Driver: record, compare, list, selftest | `scripts/pins.sh` (bash 3.2) | selftest green; `record` never run against an engine |
| Comparator | `scripts/pins/framediff.c` | selftest green |
| Scenarios, one per feature | `scripts/pins/scenarios/*.cfg` | names checked against `src/`; never run |
| Gate checks, no game data needed | `tests/check-pins.sh`, `tests/check-pins-latch.sh` | green in `scripts/test.sh` |

"Compiles and links" means the client and all three renderers build with
these hooks on macOS arm64 against SDL3 3.4.20 (`make` with an untracked
`config.mk`). Starting the client and capturing a frame have not been
observed anywhere.

Not in the tree from TASK27: the renderer-API recorder and replayer (design
in `docs/plans/refapi-record-replay.md`, including `pins.sh replay`), the
sky-and-water and cinematics scenarios, and every reference frame.

## Pin arguments

`pins_dir`, `pins_scenario` and `pins_capture` look like cvars but are not:
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
  renderer's own `screenshot` command at that point. When playback ends the
  engine quits. If the requested renderer fails to load, the engine stops
  with an error instead of falling back to another one. `0`, the default,
  disables all three.

A refused value turns the harness off and suppresses every screenshot
write. With none of them given the engine behaves as before.

## How a run works

`scripts/pins.sh record <renderer> <scenario> [--run N]` starts the engine
with `-datadir $PINS_BASEDIR -portable`, a fixed window size, sound off,
the three pin arguments above, every `set` line of the scenario as a
`+set` argument, and `+demomap $PINS_DEMO` (default `demo1.dm2`). Frames
land in `<store>/<scenario>/<renderer>/<engine commit>-<settings hash>/`,
with `-r<N>` appended for `--run N` (N from 2), so a second run of the same
engine and scenario is kept next to the first. A watchdog kills a run after
`PINS_TIMEOUT` seconds. `-portable` keeps the engine from rewriting the
owner's saved configuration; nothing is written into the game data. The
engine re-joins its command line unquoted, so `record` refuses a store
path, `PINS_BASEDIR`, `PINS_DEMO` or scenario value with anything but
`A-Z a-z 0-9 _ . / -`.

A record is complete when the engine exited 0 before the timeout, wrote at
least one frame, and every line of `manifest.txt` names the requested
renderer; `record` then writes the file `complete` last. Otherwise it
writes `incomplete` with the reason and exits non-zero.

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

## Not yet run

Nothing below has been observed. The first card (P1 in the manifest)
answers each item here and replaces this section with what it found.

1. The client starts with the hooks (it links on macOS arm64; it has not
   been started).
2. A demo started with `+demomap` on the command line reaches playback, and
   `cl.attractloop` is set while it plays (the capture condition).
3. When the demo ends, the client reaches `ca_disconnected` and the queued
   `quit` runs. The server issues `killserver` when `nextserver` is empty
   (`SV_Nextserver`); the rest of that path is read from source only.
4. Running `screenshot` inside `SCR_UpdateScreen` before `R_EndFrame` yields
   the complete current frame on `soft` and on `gl1`.
5. Which demos the owner's game data contains. `PINS_DEMO` selects one.
6. Two runs of the same renderer and scenario give identical frames. The
   engine PRNG is seeded from OS entropy, so `particles` is expected to
   differ between runs; by how much is unknown.
7. What `hud2d` shows behind the 2D elements with `r_drawworld 0` on each
   renderer.
8. That `timedemo.txt` receives the frames-per-second line.

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
| `docs/plans/metal-port-cards.md` | every card still to file, with dependencies and acceptance |
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
