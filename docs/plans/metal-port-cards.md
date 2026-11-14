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
| TASK21 (#28) | The title says 61 entry points. Measured: 54 called directly at 435 sites, plus 5 loaded by pointer at 6 sites. gl1 has no `glBegin`/`glEnd`; it draws through vertex arrays (19 `glDrawArrays` sites and one `glDrawElements` in `R_ApplyGLBuffer`). Closing pass after S1 to S6 (2026-11-13), partly met on the build host. Criterion 1 is met by the coverage report, the host test and the frame pins; no GL-call recorder, trace or pin was built or run, so its GL-call-pin half is not met as worded. Met: the coverage report (all 59 exported, 50 implemented, 5 partial, 3 no-op, 1 stub that gl1 never loads, checked by `tests/check-metal-shim.sh`), no file under `gl1/` outside `metal/` changed by the shim (not even the context seat, which `shim_sdl.h` remaps at compile time), `timedemo` and every scenario with the Metal API validation layer clean, the startup capability report, the Phase A amendment; `hud2d` and `cinematic` pass against `gl1`. Pending: the 3D scenarios against an accelerated `gl1` reference (TASK83, #187); `vid_restart` has no crash and the shim frees all of its own Metal objects, but each restart keeps the closed window alive (both renderers), and with it the `CAMetalLayer` and its drawables, outside the shim (FIX41, #193). Deviations: the shim is in `gl1/metal/`, not `glmetal/`, and was written in-tree, not seeded. Details: `docs/plans/metal-port-harness.md`, "Closing TASK21 on the build host" | `docs/analysis/gl1-state-inventory.md` section 1; `docs/plans/metal-shim-design.md` section 14 |
| TASK28 (#38) | The title says 444 call sites. Measured: 441 | same |
| INFORM8 (#35) | Its context quotes the same two numbers. The decision does not depend on them | same |
| TASK19 (#26) | `README.md` is rewritten: it describes this repository, names the fork point, lists the differences, and mentions upstream only as the origin and in "Upstream and credits". `FORK.md`, the `CHANGELOG` section and the remaining upstream links under `doc/` are what is left of the card | `README.md` |
| TASK27 (#37) | The core is in the tree: capture hook, capture before present, unattended quit, `scripts/pins.sh`, the comparator, four scenarios, the gate check. Groups P and R below are what remains | `docs/plans/metal-port-harness.md` |

## Group P: pins on real game data (parent TASK27, #37)

Dependencies: the cards depend only on one another in sequence (N1
first); they do not wait for TASK21 (closed) or TASK27. TASK28 (#38)
closes when N10 (TASK93) lands (`docs/plans/ref-metal-native.md`,
"Decisions taken at filing").
Design: `docs/plans/ref-metal-native.md`; per-concept reference: the shim
(`docs/plans/metal-shim-design.md`) and `docs/analysis/gl1-state-inventory.md`.
The cards form one chain, each stacked on its predecessor. Risk tier and
size per card are in the design's section 12. `metal` is the renderer
name the harness records `ref_metal` under; every comparison below is
against `gl1-metal` recorded on the same host with `MTL_DEBUG_LAYER=1`,
"tight" means `--threshold 0.005 --bad-threshold 0.001`, and a card keeps
the pins of the cards before it passing. gl1's own files, gl3, soft,
`files/` and `ref.h` are not edited by any card in this group.

| Key | Title | Depends | Tier | Model | Design reference | Acceptance | Filed as |
|---|---|---|---|---|---|---|---|
| N1 | `ref_metal` skeleton: build target, window seat, clear, present, frame capture, capability report, gate check | none | T0 | opus | sections 2, 3, 10 | `make ref_metal` links on macOS with Metal and no OpenGL (`otool -L`); on another OS it fails with one line. `scripts/pins.sh record metal hud2d` completes (23 frames, manifest `metal`, the engine quits by itself) with no validation message, and every pixel of every frame is the `r_clear` colour 255, 0, 128. The log shows the device, `supportsFamily` (apple5 to apple9) and the ray-tracing flags; a device below apple5 is refused. Four `vid_restart` cycles between `metal` and `soft` under `ASAN=1`: status 0, no sanitizer line, no `ref_metal` object live after teardown. `tests/check-ref-metal.sh` (host test, build, vocabulary check with its mutant) is green | TASK84 (#195) |
| N2 | Images, textures, the 2D pipeline and the cinematic; the shim's GL-free arithmetic moved to `metal/common/` | N1 | T0 | opus | sections 4, 5, 8 | `compare metal cinematic --ref gl1-metal` (`PINS_DEMO=idlog.cin PINS_EVERY=7`) passes on every frame at the tight values. `gl1-metal` records of `hud2d`, `cinematic` and `world` after the move compare at score 0 and bad 0 against records before it, and the shim's host test passes with the same check count. `metal_stats` reports 0 pipelines created after init | TASK85 (#196) |
| N3 | Pics, characters, the scrap and the HUD | N2 | T0 | sonnet | section 5 | `compare metal hud2d --ref gl1-metal` passes on all 23 frames at the tight values (this also proves the row order on frames that are not uniform) | TASK86 (#197) |
| N4 | World geometry: BSP registration, retained world buffers, traversal, fullbright surfaces | N3 | T0 | opus | sections 3, 9 | A new scenario `world-fullbright` (`world` plus `r_fullbright 1`) passes `pins.sh selftest`, and two `gl1-metal` records of it compare at score 0. `compare metal world-fullbright --ref gl1-metal` passes at the tight values on every frame on which `metal_stats 1` reports no skipped sky, warped or translucent surface; the card lists the others. The retained vertex, index and surface-record sizes are logged per map | TASK87 (#198) |
| N5 | Lightmaps: one array texture, lightstyles, overbright, staged updates | N4 | T0 | opus | sections 5, 6 | `compare metal world --ref gl1-metal` passes at the tight values on every frame `metal_stats 1` reports no skipped surface for. A scratch copy of `world` with `gl1_overbrightbits 2`, recorded on both renderers, passes at the same values (not committed) | TASK88 (#199) |
| N6 | Sky, water warp, translucent and flowing surfaces | N5 | T0 | sonnet | sections 5, 9 | `compare metal world --ref gl1-metal` and `compare metal skywater --ref gl1-metal` (from `demo1.dm2`) pass on every frame at the tight values, with no skipped surface left. `skywater` with `PINS_DEMO=water.dm2` passes at `--threshold 0.02 --bad-threshold 0.03` against two `gl1-metal` records | TASK89 (#200) |
| N7 | Brush entities, alias models, the view weapon, model lighting | N6 | T0 | opus | sections 4, 9 | `compare metal models --ref gl1-metal` passes at the defaults against two `gl1-metal` records on every frame `metal_stats 1` reports no skipped sprite, beam or null model for; the card lists the others | TASK90 (#201) |
| N8 | Sprites, beams, null models, stencil shadows, dynamic lights, flash blobs | N7 | T0 | opus | sections 6, 9 | `models` passes on every frame at the defaults against two `gl1-metal` records, also in a scratch copy with `r_shadows 1` and `gl1_stencilshadow 1`. A new scenario `dlights` (`world` plus `cl_lights 1`) passes `pins.sh selftest`; its `gl1-metal` run-to-run values are measured and recorded in the file, and `compare metal dlights --ref gl1-metal` passes at them (the tight values if it is exact), also in a scratch copy with `gl1_flashblend 1` | TASK91 (#202) |
| N9 | Particles, the screen blend, the full parity sweep and frame time | N8 | T0 | sonnet | sections 4, 11 | `compare metal particles --ref gl1-metal` passes at `--threshold 0.02 --bad-threshold 0.25` against two records, also in scratch copies with `gl1_pointparameters 0` and with `gl1_particle_square 1`. Every scenario passes at its value of the design's section 11 in one sweep at one commit, recorded as a table in the runbook. The `timedemo` fps of `metal` is at least that of `gl1-metal` on every scenario at the same load, both recorded | TASK92 (#203) |
| N10 | Lifecycle, selection and the docs: `vid_restart`, resize, fullscreen, vsync, gamma, the fallback order and the video menu on macOS | N9 | T0 | opus | sections 10, 13, 15 | Eight `vid_restart` cycles through `metal`, `gl1` (shim) and `soft` under `ASAN=1` and under `leaks --atExit`: status 0, 0 leaks, no `ref_metal` object live after any teardown, and no growth beyond what FIX41 (#193) records for the windows. After a mode change, a fullscreen toggle and with HighDPI the screenshot has the drawable's size; `r_vsync` and `vid_gamma` behave as in `gl1`. `metal` is in the fallback order ahead of `gl1` on macOS only, and in the video menu only there. Every pin of N9's sweep still passes. `doc/`, `docs/README.md`, this manifest and the plans describe the tree as built | TASK93 (#204) |

## TASK20, TASK22, TASK23

No sub-cards here.

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
none), then N1 to N10 (one chain, stacked the same way) and TASK28, TASK20, TASK22, TASK23, then RT1 to RT10 with RTA.
Their parents (TASK27, TASK21, TASK28, TASK24, TASK25) follow their children.

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
6. N1 to N10, then TASK28.
7. RT1 to RT8 with RTA, then TASK24; RT9 and RT10, then TASK25.

What the build VM can finish alone: P1, P2, R1 to R5 on `soft`, S1 and
RT8's VM half. P3 and everything after S1 need `ref_gl1` references, which
the VM now records through Apple's software OpenGL renderer (TASK80).
Group N is judged against `gl1-metal` on the same host and can be finished
on the VM; its `--ref gl1` half waits for TASK83 (#187), as the shim's does. Group RT needs T1
and T2 hardware.
