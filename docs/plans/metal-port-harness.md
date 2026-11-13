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
| Frame writer, no stb | `VID_WritePinFrame` in `src/client/vid/vid.c` | compiles; never run |
| Capture before present | `SCR_PinsCapture` in `src/client/cl_screen.c` | compiles; never run |
| Unattended quit at end of demo | `CL_PinsFrame` in `src/client/cl_main.c` | compiles; never run |
| Driver: record, compare, list, selftest | `scripts/pins.sh` (bash 3.2) | selftest green; `record` never run against an engine |
| Comparator | `scripts/pins/framediff.c` | selftest green |
| Scenarios, one per feature | `scripts/pins/scenarios/*.cfg` | names checked against `src/`; never run |
| Gate check, no game data needed | `tests/check-pins.sh` | green in `scripts/test.sh` |

"Compiles" means each file builds to an object with the Makefile's flags on
Linux. The client has not been linked with these hooks anywhere.

Not in the tree from TASK27: the renderer-API recorder and replayer (design
in `docs/plans/refapi-record-replay.md`), the sky-and-water and cinematics
scenarios, and every reference frame.

## Cvars

- `pins_dir <path>`: when set, a screenshot is written as an uncompressed
  24-bit TGA `<pins_scenario>-<frame>.tga` into that directory, with a line
  in `manifest.txt` (file, renderer, size). `scrnshot/` is not touched.
- `pins_scenario <id>`: the file-name prefix.
- `pins_capture <N>`: while a demo plays, every Nth rendered frame is
  captured after it is complete and before it is presented, by running the
  renderer's own `screenshot` command at that point. When playback ends the
  engine quits. `0`, the default, disables both.

With none of them set the engine behaves as before.

## How a run works

`scripts/pins.sh record <renderer> <scenario>` starts the engine with
`-datadir $PINS_BASEDIR -portable`, a fixed window size, sound off, the
three cvars above, every `set` line of the scenario as a `+set` argument,
and `+demomap $PINS_DEMO` (default `demo1.dm2`). Frames land in
`<store>/<scenario>/<renderer>/<engine commit>-<settings hash>/`. A watchdog
kills a run after `PINS_TIMEOUT` seconds. `-portable` keeps the engine from
rewriting the owner's saved configuration; nothing is written into the game
data.

`scripts/pins.sh compare <renderer> <scenario> --ref <renderer>` compares
frame by frame and prints one line per frame and a summary. It exits
non-zero when any frame is over the threshold or missing.

## Not yet run

Nothing below has been observed. The first card (P1 in the manifest)
answers each item here and replaces this section with what it found.

1. The client links and starts with the three hooks.
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
(`$PINS_STORE`, or `pins.store` in `.claude/dev-loop.json`).

1. References, once per engine commit and scenario (INFORM9):
   `scripts/pins.sh record gl1 <scenario>` for each scenario on a machine
   where `ref_gl1` renders, and the same with `soft`.
2. Build the candidate.
3. `scripts/pins.sh record <candidate> <scenario>`, then
   `scripts/pins.sh compare <candidate> <scenario> --ref gl1`.
4. Fix the one feature the failing scenario names, rebuild, repeat 3. The
   threshold is never widened to make a pin pass. A scenario marked
   `nondeterministic:` has its own recorded threshold.
5. A card's pins pass when every frame of every scenario it names is `ok`.

`scripts/test.sh` stays green with and without game data: the gate runs
only `tests/check-pins.sh`, on synthetic frames. Pins on real data are run
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

- INFORM8 and INFORM9 are still open. Groups P, R and S in the manifest
  wait on them.
- Whether a freely distributable demo may be downloaded for a CI-side pin
  job (INFORM9, option C). Until then CI runs synthetic frames only.
