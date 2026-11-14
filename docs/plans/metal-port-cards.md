# Card manifest: Metal port and ray tracing

Status: **filed on 2026-11-12.** Each table row below is one card filed
under the epic `metal-renderer`. The keys in the first column (P1, R1, S1,
RT1 and so on) are placeholders, not card ids; card bodies refer to them as
"row P1" and so on. The id minted for each row is in its "Filed as" cell; a
row with an id there is never filed again.

This manifest is the authoritative card list. The four design documents
keep their own sub-card lists as the detailed scope behind each row; where
a list there differs from this file, this file wins.

## Filing rules

1. One issue per row, in the order listed, so a `depends` label can name an
   id that already exists.
2. Title `<id> — <title>`. Labels: `state:backlog`, `epic:metal-renderer`,
   `id:<id>`, `model:<model>`, and one `depends:<id>` per dependency, with
   keys translated to the minted ids.
3. Every card in a group also carries its parent's current `depends`
   labels, listed under the group heading. The "Depends" column holds only
   the dependencies inside this manifest.
4. Body in the shape the existing cards use: a `Plan anchor:` line naming
   `docs/plans/metal-renderer.md` and the design reference from the row,
   the hardware tier, then `## Goal`, `## Context / rationale`,
   `## Scope (directional, not line-pinned)` and
   `## Acceptance criteria (outcome-level)`. Goal and acceptance come from
   the row. Context and scope come from the design section the row cites.
   Every acceptance list ends with "`scripts/test.sh` green".
5. Lock each new card right after creating it
   (`docs/plans/trust-boundary.md`, "Repository access").
6. When a group is filed, add one `depends:<child id>` label per child to
   the parent card. The parent is then pulled last, and what remains of it
   is its own acceptance list checked end to end.
7. Record the ids in the "Filed as" cells here and in the "Cards generated"
   list of `docs/plans/metal-renderer.md`.

Hardware tiers are the plan's: T0 is any Mac or the build VM, T1 a
bare-metal Mac where `ref_gl1` renders, T2 an M3 or later.

## Corrections to existing cards (edit them, file nothing)

| Card | Correction | Source |
|---|---|---|
| TASK21 (#28) | The title says 61 entry points. Measured: 54 called directly at 435 sites, plus 5 loaded by pointer at 6 sites. gl1 has no `glBegin`/`glEnd`; it draws through vertex arrays (19 `glDrawArrays` sites and one `glDrawElements` in `R_ApplyGLBuffer`). Closing pass after S1 to S6 (2026-11-13): met on the build host: the coverage report (all 59 exported, 51 implemented, 5 partial, 2 no-op, 1 stub that gl1 never loads, checked by `tests/check-metal-shim.sh`), no file under `gl1/` outside `metal/` changed by the shim (not even the context seat, which `shim_sdl.h` remaps at compile time), `timedemo` and every scenario with the Metal API validation layer clean, the startup capability report, the Phase A amendment; `hud2d` and `cinematic` pass against `gl1`. Pending: the 3D scenarios against an accelerated `gl1` reference (TASK83, #187); `vid_restart` has no crash and the shim frees all of its own Metal objects, but each restart keeps the closed window alive (both renderers), and with it the `CAMetalLayer` and its drawables, outside the shim. Details: `docs/plans/metal-port-harness.md`, "Closing TASK21 on the build host" | `docs/analysis/gl1-state-inventory.md` section 1; `docs/plans/metal-shim-design.md` section 14 |
| TASK28 (#38) | The title says 444 call sites. Measured: 441 | same |
| INFORM8 (#35) | Its context quotes the same two numbers. The decision does not depend on them | same |
| TASK19 (#26) | `README.md` is rewritten: it describes this repository, names the fork point, lists the differences, and mentions upstream only as the origin and in "Upstream and credits". `FORK.md`, the `CHANGELOG` section and the remaining upstream links under `doc/` are what is left of the card | `README.md` |
| TASK27 (#37) | The core is in the tree: capture hook, capture before present, unattended quit, `scripts/pins.sh`, the comparator, four scenarios, the gate check. Groups P and R below are what remains | `docs/plans/metal-port-harness.md` |

## Group P: pins on real game data (parent TASK27, #37)

Parent dependencies carried by every card: INFORM8, INFORM9, TASK29
(INFORM8 and INFORM9 are closed, accepted by the owner on 2026-11-12).

| Key | Title | Depends | Tier | Model | Design reference | Acceptance | Filed as |
|---|---|---|---|---|---|---|---|
| P1 | First capture on real data: link the client with the capture hooks and record `ref_soft` references for the four scenarios | none | T0 | opus | `docs/plans/metal-port-harness.md`, "Observed on the first run (TASK39)" | `make client ref_soft` links on macOS. `scripts/pins.sh record soft <scenario>` exits 0 for all four scenarios and the engine quits by itself. Two consecutive records of `world`, `models` and `hud2d` (`record soft <scenario>`, then `record soft <scenario> --run 2`) compare at score 0.000000 and bad 0.000000 on every frame (`compare soft <scenario> --run 2 --ref soft`). The run-to-run score of `particles` is measured and recorded as its threshold. Every item in "Not yet run" is answered in the runbook. Nothing derived from game data is committed. The first run found `world` and `hud2d` exact and `models` within the default thresholds but not exact (see the runbook); exactness for `models` waits for the record/replay group's fixed seed | TASK39 (#81) |
| P2 | Remaining scenarios and the user page: sky and water, cinematics, and a `doc/` page for the harness | P1 | T0 | sonnet | TASK27 (#37) scope list; `scripts/pins/scenarios/README.md` | Two new scenario files pass `scripts/pins.sh selftest`. Each records twice on `soft` (the second with `--run 2`) and the two runs compare at score 0 (`compare soft <scenario> --run 2 --ref soft`). A page in `doc/` lists the scenarios, the environment variables and how to record and compare The first run found no water in the stock demos. | TASK40 (#82) |
| P3 | `ref_gl1` references and the thresholds at which `soft` and `gl1` agree | P1, P2 | T0: `ref_gl1` renders on the build VM through Apple's software OpenGL renderer (TASK80); `GL_RENDERER` is `Apple Software Renderer`, `GL_VERSION` `2.1 APPLE-24.0.2` | opus | INFORM9 (#36) | `ref_gl1` references for every scenario are in the pin store of a machine where gl1 renders. `scripts/pins.sh compare soft <scenario> --ref gl1` has been run for each, and the score at which the two agree is recorded per scenario in the runbook. The `gl1` frames of this host come from that software renderer, so they are references for the shim's arithmetic, not for a graphics card; the card says so. It also confirms that a normal `ref_gl1` screenshot is unchanged (carried item (k) of TASK39) Done on the build host through the software renderer (TASK41): four `gl1` records and three `soft` records per scenario, every pair compared, and the table, the host load and the `GL_RENDERER` string are in the runbook (`docs/plans/metal-port-harness.md`, section on `gl1` references). `soft` and `gl1` agree at a usable threshold only on `cinematic` and on `skywater` with `water.dm2`; elsewhere they differ too much, so the numbers a Metal renderer is judged against are `gl1` against `gl1`: the floor is recorded per scenario, and the table "Accept `gl1-metal` against `gl1` at" in the runbook gives the starting value each shim card cites. Item (k), that a normal `ref_gl1` screenshot is unchanged, was not compared with a pre-change build and stays open. | TASK41 (#83) |

## Group R: renderer-API record and replay (parent TASK27, #37)

Parent dependencies carried by every card: INFORM8, INFORM9, TASK29
(INFORM8 and INFORM9 are closed, accepted by the owner on 2026-11-12).
Design: `docs/plans/refapi-record-replay.md`. On a host where only `soft`
runs, "each renderer" below means `soft`.

| Key | Title | Depends | Tier | Model | Design reference | Acceptance | Filed as |
|---|---|---|---|---|---|---|---|
| R1 | Record serializers and pass-through hooks in the `vid.c` wrappers, recording off | P1 | T0 | opus | sections 1, 5; sub-card a | Frames of the `world` scenario are byte-identical to the build without the hooks. `tests/check-refrecord.sh` round-trips every record type through a buffer | TASK42 (#84) |
| R2 | Recording: handle map, `refdef_t` deep copy, string table, frame index | R1 | T0 | opus | sections 2, 3; sub-card b | A record run with `pins_record` set leaves a trace whose frame count equals the captured frame count and whose string table names every model, skin and pic the engine registered (as built: the header's `frame_count` counts every rendered frame; the captured frames are its `REC_CAPTURE` records, one per captured frame) | TASK43 (#85) |
| R3 | Replay driver: registration from the string table, frame feed, capture at the recorded cadence, quit (amended: capture at the `REC_CAPTURE` records, not at a cadence; the replayer bounds entity skin, frame and particle colour before it hands a frame to a renderer) | R2 | T0 | opus | section 4; sub-card c | A replay produces the same number of frames as the live record, with no server and no demo file | TASK44 (#86) |
| R4 | Determinism: cvar snapshot and restore, a fixed PRNG seed behind a cvar, the `refdef_t.time` check | R3 | T0 | opus | section 4; sub-card d | Live against replay scores 0.000000 on every frame for each renderer on the same machine, `particles` included | TASK45 (#87) |
| R5 | `scripts/pins.sh replay` and cross-renderer compare from one trace | R4 | T0 | opus | sub-card e | `scripts/pins.sh replay <renderer> world` from another renderer's trace, followed by `compare`, runs with the game module absent | TASK46 (#88) |

## Group S: the OpenGL-subset shim on Metal (parent TASK21, #28)

Parent dependencies carried by every card: INFORM8, TASK13, TASK29 (TASK27
was dropped from S2 to S6 on 2026-11-13, see record 0006)
(INFORM8 is closed, accepted by the owner on 2026-11-12).
Design: `docs/plans/metal-shim-design.md` and
`docs/analysis/gl1-state-inventory.md`. The six cards consolidate the ten
stages of the inventory's section 6 and the three cards of the shim
design's section 6. `gl1-metal` is the name the harness records under when
`ref_gl1` is built with the shim. gl1's C files are not edited by any card
in this group.

| Key | Title | Depends | Tier | Model | Design reference | Acceptance | Filed as |
|---|---|---|---|---|---|---|---|
| S1 | Shim skeleton and readback: build selection, context, present, clear, the state struct, `glGetString`, `glGetError`, `glReadPixels`, the pin hook on the shim | none | T0 | opus | shim design sections 1 to 3; inventory stages a, j | `ref_gl1` built with the shim links without the OpenGL framework (`otool -L` shows Metal and no OpenGL). The engine boots to the console and quits under `pins_capture`. A frame captured through `pins_dir` is byte-identical to the shim's own `glReadPixels` of that frame | TASK47 (#89) |
| S2 | 2D path: RGBA textures, samplers, replace and modulate, blend, alpha test, the 2D batch, cinematic uploads | S1, P3 | T0 | opus | inventory stages b, c | `scripts/pins.sh compare gl1-metal hud2d --ref gl1` passes on every frame at the `hud2d` value in the harness table "Accept `gl1-metal` against `gl1` at" (docs/plans/metal-port-harness.md, section "`gl1` against `gl1`: the floor for the Metal shim") | TASK48 (#90), TASK82 (#181) |
| S3 | World: matrices, depth, cull, the classic lightmap pass, two-unit combine with overbright, dynamic lightmap sub-uploads, polygon offset | S2 | T0 | opus | inventory stages d, e | `compare gl1-metal world --ref gl1` passes on every frame with `gl1_multitexture` 0 and with 1 TASK49: implementation complete, acceptance pending an accelerated reference. Against the `gl1` references of Apple's software renderer `world` passes on 7 of 23 frames with `gl1_multitexture` 1 and on none with 0, for faults of the reference (runbook, "The `world` scenario against the software renderer (TASK49)"); no threshold was changed. The row is judged again by TASK83 (#187). | TASK49 (#91), TASK83 (#187) |
| S4 | Translucent surfaces, water and sky | S3 | T0 | opus | inventory stage f | The `world` frames that contain water or sky pass, and so does the sky-and-water scenario from P2 The water half needs a water source before it can be accepted: a demo that passes water, recorded by the owner or found in other data the owner has. Until then only sky is pinned. TASK70: the water source is `water.dm2` from `scripts/pins.sh make-demo water`, which fails a demo outside the message range its script states; `skywater` recorded with `PINS_DEMO=water.dm2` at `PINS_EVERY` 30 shows the water from above on the first half of its frames and from below on the second half (4 frames, 2 and 2, in every record so far), and is compared at `--bad-threshold 0.03` (runbook, thresholds for nondeterministic scenarios). TASK50: implementation complete, acceptance pending an accelerated reference. Against the `gl1` references of Apple's software renderer `world` and `skywater` from `demo1.dm2` pass on 7 of 23 frames, and `skywater` from `water.dm2` on none of 5 (measured with a demo `make-demo water` refused, indicative only), for faults of the reference (runbook, "Sky, water and translucent surfaces against the software renderer (TASK50)"); no threshold was changed. The row is judged again by TASK83 (#187). | TASK50 (#92), TASK83 (#187) |
| S5 | Alias models, the view weapon, dynamic lights, flashes and stencil shadows | S4 | T0 | opus | inventory stages g, h | `compare gl1-metal models --ref gl1` passes, also with `gl1_stencilshadow 1` TASK51: implementation complete, acceptance pending an accelerated reference. The shim needed no new code for models, the view weapon, flash blobs or stencil shadows (no "not implemented" line in any record); against the `gl1` references of Apple's software renderer `models` passes on 6 of 23 frames, with and without stencil shadows, for faults of the reference; one region first suspected as a shim defect (frame 19) was decided as a fault of the reference in review (runbook, "The `models` scenario against the software renderer (TASK51)"); no threshold was changed. The row is judged again by TASK83 (#187). | TASK51 (#93), TASK83 (#187) |
| S6 | Particles, beams, sprites and the screen blend | S5 | T0 | opus | inventory stage i | `compare gl1-metal particles --ref gl1` passes at the `particles` value in the same table TASK52: implementation complete, acceptance pending an accelerated reference. Against the `gl1` references of Apple's software renderer `particles` passes on 18 of 23 frames at `--threshold 0.02 --bad-threshold 0.25` in each of 9 record pairs, and so do its copies with `gl1_pointparameters 0` and `gl1_particle_square 1`, for faults of the reference (runbook, "Particles against the software renderer (TASK52)"); no threshold was changed. The row is judged again by TASK83 (#187). | TASK52 (#94), TASK83 (#187) |

## TASK28, TASK20, TASK22, TASK23

No sub-cards here. TASK28 (native `ref_metal`) is decomposed when it is
pulled, one GL concept at a time, from the concept map in the inventory's
section 5 and the pipeline-state keys in its section 2. The frame pins are
its acceptance after every concept.

## Group RT: ray tracing (parents TASK24, #31, and TASK25, #32)

Parent dependencies carried by RT1 to RT8 and RTA: INFORM7 (closed, accepted
by the owner on 2026-11-12), TASK28. RT9 and
RT10 belong to TASK25 and carry TASK24. Design:
`docs/plans/metal-raytracing-design.md`.

| Key | Title | Depends | Tier | Model | Design reference | Acceptance | Filed as |
|---|---|---|---|---|---|---|---|
| RT1 | Acceleration structures: world and model triangle extraction, per-map and per-model structures, per-frame instances, counts logged; no visual change | none | T1 | opus | section 1; row RT1 | All gl1 pins unchanged. Triangle and byte counts for the demo maps are printed and recorded in the design document | TASK53 (#95) |
| RT2 | G-buffer in `ref_metal` and a lighting resolve that reproduces the stock result | RT1 | T1 | opus | section 3; row RT2 | All gl1 pins unchanged at the accepted threshold, 0.005 at most | TASK54 (#96) |
| RT3 | Ray-traced shadows from dynamic lights, replacing the term `R_AddDynamicLights` adds when on | RT2 | T1 | opus | section 2; row RT3 | Effect off: pins unchanged. Effect on: an `rt-shadows` scenario records, the dynamic-light term is not counted twice, fps from the timedemo line is stated | TASK55 (#97) |
| RT4 | Sun and sky shadow with a per-map direction table | RT3 | T1 | opus | section 2; row RT4 | As RT3 with `rt-sun`. Maps under the sky-area threshold render byte-identical to effect off | TASK56 (#98) |
| RT5 | Ambient occlusion | RT3 | T1 | opus | section 2; row RT5 | As RT3 with `rt-ao`. No pixel falls below the configured floor times the lightmap | TASK57 (#99) |
| RT6 | Water reflections, one bounce | RT3 | T1 | opus | section 2; row RT6 | As RT3 with `rt-water`. Pixels that are not water are byte-identical to effect off | TASK58 (#100) |
| RTA | INFORM: the look of the four ray-traced effects, accepted by the owner from the frames RT3 to RT6 captured (owner-gated, `gate:manual:BeauBender`) | RT3, RT4, RT5, RT6 | none | opus | section 5 | The owner closes the card. The commit accepted becomes the reference each effect scenario is pinned against | INFORM11 (#101) |
| RT7 | Temporal accumulation and the denoiser | RTA | T1 | opus | section 2; row RT7 | Pins unchanged with temporal accumulation off. Every effect on: the fps target in the design document is met on the T1 host and the number is recorded | TASK59 (#102) |
| RT8 | Hardware gating and fallbacks, with ray tracing forced off on the VM | RT2 | T0 and T1 | opus | section 4; row RT8 | On the VM every `r_rt_*` cvar reads 0 with a report line at start-up, and all pins are unchanged there | TASK60 (#103) |
| RT9 | Render-stage ray queries on M3 or later, compute path kept for older families | RT7 | T2 | opus | section 4; row RT9 | Both paths produce the same pins within 0.005. fps on the T2 host at native resolution is recorded in `docs/audits/rt-performance.md` | TASK61 (#104) |
| RT10 | Refit against rebuild tuning and resolution-scale defaults per GPU family | RT9 | T2 | opus | row RT10 | A per-pass timing table for each host is in `docs/audits/rt-performance.md`. No pin changes | TASK62 (#105) |

## Pull order

Record 0006 (`docs/decisions/0006-port-first.md`) sets the queue. The port
queue is the rows of this file, in the order the sections list them: P1 to
P3, R1 to R5, S1 to S6 (the shim cards S2 to S6 form one chain; each
stacks on its predecessor's open pull request and waits when there is
none), then TASK28, TASK20, TASK22, TASK23, then RT1 to RT10 with RTA.
Their parents (TASK27, TASK21, TASK24, TASK25) follow their children.

Everything else is parked: the card carries the label
`gate:manual:port-first` and is not ready. It is released only when it
blocks a port card, or when no port card can start. A review finding
outside a port card's scope goes to a parked card.

## Order of work and what gates it

1. Land the branch that carries this file. Done: `metal-port-harness` was
   merged to main.
2. TASK13 and TASK9, then TASK29. These existing cards gate the harness
   through TASK27's labels. TASK13 is merged (#106); TASK9 and TASK29 remain.
3. Owner gates: INFORM8 and INFORM9 for groups P, R and S; INFORM7 for
   group RT. All three were closed as accepted by the owner on 2026-11-12;
   nothing waits on them. The cards still carry the `depends` labels the
   group headings list.
4. P1, P2, P3 and R1 to R5, then TASK27 itself.
5. S1 to S6, then TASK21.
6. TASK28.
7. RT1 to RT8 with RTA, then TASK24; RT9 and RT10, then TASK25.

What the build VM can finish alone: P1, P2, R1 to R5 on `soft`, S1 and
RT8's VM half. P3 and everything after S1 need `ref_gl1` references, which
the VM now records through Apple's software OpenGL renderer (TASK80). Group RT needs T1
and T2 hardware.
