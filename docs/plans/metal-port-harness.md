# Runbook: the autonomous Metal port and ray-tracing loop

Plan anchor: `docs/plans/metal-renderer.md`, as revised by INFORM8 (#35: pin
at two seams, shim first, then native `ref_metal`) and INFORM9 (#36: pins
from the owner's game data, in a local store, never committed). Cards:
TASK27 (#37, harness), TASK21 (#28, GL-subset shim), TASK28 (#38, native
`ref_metal`), TASK24 (#31, hybrid ray tracing), TASK25 (#32, render-stage
ray queries). This document is the operating procedure an agent follows,
unattended, from a clean checkout to a Metal renderer with ray tracing.

## What this commit ships (the TASK27 core)

| Piece | Where | Status |
|---|---|---|
| Capture hook, no stb | `src/client/vid/vid.c` `VID_WritePinFrame` | built here on Linux |
| Capture cadence + unattended quit | `src/client/cl_main.c` `CL_PinsFrame` | built here on Linux |
| Driver: record / compare / list / selftest | `scripts/pins.sh` (bash 3.2) | selftest green |
| Tolerant diff stand-in, same CLI as the plugin's | `scripts/pins/framediff.c` | selftest green |
| Scenarios, one per feature | `scripts/pins/scenarios/*.cfg` | 4 committed |
| Gate check, no game data needed | `tests/check-pins.sh` | runs from `scripts/test.sh` |

Not yet shipped from TASK27: the renderer-API recorder and replayer
(`refdef_t` deep copy, `Draw*` stream, registration by name). The frame
pins do not need it; the shim's GL-call pins (TASK21) come from the plugin.

## Cvars the engine now understands

- `pins_dir <path>`: when set, every screenshot is written as an uncompressed
  24-bit TGA `<pins_scenario>-<frame>.tga` into that directory plus a
  `manifest.txt` line (file, renderer, size). `scrnshot/` is not touched.
- `pins_scenario <id>`: the file-name prefix.
- `pins_capture <N>`: during demo playback, request a screenshot every N
  client frames; when playback ends, `quit`. `0` (default) disables all of it.

Default behaviour with none of them set is unchanged.

## The loop, per renderer under test

Prerequisites on the Mac: the owner's game data at `$PINS_BASEDIR/baseq2`,
a built client (`make client` plus the renderer modules), and a pin store
outside the repo (`$PINS_STORE`, or `pins.store` in `.claude/dev-loop.json`).

1. References, once per engine commit and scenario (INFORM9):
   `scripts/pins.sh record gl1 world`, then `models`, `hud2d`, `particles`;
   the same with `soft`. The store path embeds the engine commit and the
   scenario hash, so a stale reference is a different directory.
2. Build the candidate (`ref_metal`, or `gl1` on the shim for TASK21).
3. `scripts/pins.sh record metal world` and `scripts/pins.sh compare metal
   world --ref gl1 --threshold 0.02`. The report lists every frame; `OVER`
   names the first frame to look at, the scenario names the feature.
4. Fix the one feature, rebuild, repeat 3. Never widen the threshold to pass;
   a scenario marked `nondeterministic:` may be accepted looser, and the
   threshold used is recorded next to the pin in the closing comment.
5. A scenario passes when every frame is `ok`. The card passes when all four
   scenarios pass against `gl1` and `soft` agrees with `gl1` within the
   threshold documented per scenario (that number is the harness's own
   calibration and is measured first).

`scripts/test.sh` stays green with and without game data: the gate runs only
`tests/check-pins.sh` (synthetic frames). Real pins are run by the agent
inside the card, never by CI.


## Analysis and design documents (written 2026-11-06, read before starting a card)

| Document | Feeds |
|---|---|
| `docs/analysis/gl1-state-inventory.md` | TASK21, TASK28: the 61 GL entry points, draw-time state space, pipeline-state keys, implementation order |
| `docs/plans/metal-shim-design.md` | TASK21: shim module layout, PSO cache, ring buffers, readback, frame sync, first cards |
| `docs/plans/metal-raytracing-design.md` | TASK24, TASK25: acceleration structures, the three effects, G-buffer needs, hardware gating, sub-cards |
| `docs/plans/refapi-record-replay.md` | TASK27 remainder: proxy refexport_t, deep copy rules, on-disk format, replay driver, header sketch |

## Order of work for an unattended agent

1. TASK27 remainder: recorder and replayer behind `GetRefAPI`; acceptance,
   a renderer replaying its own records reproduces its own frames
   bit-identically (`framediff` score 0).
2. TASK21: the in-tree OpenGL-subset shim on Metal; GL-call pins from the
   plugin, frame pins from this harness as the second oracle.
3. TASK28: specialize into native `ref_metal`, one concept at a time, frame
   pins as acceptance after every concept.
4. TASK24: hybrid ray tracing (shadows, AO, water reflections) in compute.
   Frame pins cannot judge "better lighting"; the harness is used
   differently here: (a) with every RT effect off, `ref_metal` must still
   pass all pins against `gl1`, so RT never regresses the rasterizer; (b)
   each RT effect gets its own scenario with the effect on, pinned against
   `ref_metal` itself at the commit the owner accepted the look, so later
   work cannot drift it silently; (c) `timedemo.txt` in each record holds the
   fps line, the performance number the card reports.
5. TASK25: render-stage ray queries on apple9; same three uses of the harness,
   on the M6, numbers into `docs/audits/rt-performance.md`.

## Hardware tiers (from the plan)

T0, any Mac or the VM: everything in this commit, `soft` references, the
rasterizer. T1, a bare-metal Mac where `ref_gl1` runs: the `gl1` references,
the shim, the rasterizer parity work, hybrid RT in compute. T2, M3 or
later: render-stage ray queries and the performance numbers.

## Open decisions for the owner (not blocking the first three cards)

- Whether the shareware demo may be downloaded for a CI-side pin job
  (INFORM9 option C). Until then CI runs synthetic frames only.
- The pin plugin's diff tool replaces `framediff` by setting `PINS_DIFF`;
  the contract is the same and nothing else changes.
