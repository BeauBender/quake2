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
| TASK27 (#37) | The core is in the tree: capture hook, capture before present, unattended quit, `scripts/pins.sh`, the comparator, four scenarios, the gate check. Groups P and R below are merged. Closing pass at `4175e8cc` on the build host (2026-11-14): criterion 1 met with a limit: replay is exact on all three renderers (21 replays, thresholds 0); the `soft` references of `models`, `particles` and `skywater` from `demo1.dm2` in the owner's store are at an older settings hash, and recording new ones is the owner's step (commands given). Criterion 2 met with a limit: `gl1` references of every scenario, all Apple Software Renderer, and a `soft` against `gl1` row per scenario in the runbook (a threshold for `cinematic` and `skywater` from `water.dm2`, "not meaningful" with reasons for the rest); accelerated references are TASK83 (#187), and the `water.dm2` threshold holds for the demo it was measured on (re-measuring it on the accelerated host is noted on TASK83, #187). Criterion 3 met: no file derived from game data is tracked, no trace is committed. Criterion 4 met as far as the checks go: no check reads the game data or a pin store, all 16 green with those variables unset and set to empty directories. Open from the scope, now held by TASK94 (#209): the store path from configuration, a content hash, a fixed `cl_maxfps`. Details: `docs/plans/metal-port-harness.md`, "Closing TASK27 on the build host" | `docs/plans/metal-port-harness.md` |

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
| R4 | Determinism: cvar snapshot and restore, a fixed PRNG seed behind a cvar, the `refdef_t.time` check | R3 | T0 | opus | section 4; sub-card d | Live against replay scores 0.000000 on every frame for each renderer on the same machine, `particles` included (as built: measured on `soft` and `gl1-metal`, every scenario; `gl1` has no context on the T0 host; with `PINS_SEED` two live records also score 0.000000) | TASK45 (#87) |
| R5 | `scripts/pins.sh replay` and cross-renderer compare from one trace | R4 | T0 | opus | sub-card e | `scripts/pins.sh replay <renderer> world` from another renderer's trace, followed by `compare`, runs with the game module absent (as built: measured on the T0 host, `soft` from a `gl1-metal` trace and `gl1-metal` from a `soft` trace, 0 differing frames against the live record of the renderer replayed into, the game module absent from every directory the engine searches) | TASK46 (#88) |

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

## Group N: native `ref_metal` (parent TASK28, #38)

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
| N1 | `ref_metal` skeleton: build target, window seat, clear, present, frame capture, capability report, gate check | none | T0 | opus | sections 2, 3, 10 | `make ref_metal` links on macOS with Metal and no OpenGL (`otool -L`); on another OS it fails with one line. `scripts/pins.sh record metal hud2d` completes (23 frames, manifest `metal`, the engine quits by itself) with no validation message, and every pixel of every frame is the `r_clear` colour 255, 0, 128. The log shows the device, `supportsFamily` (apple5 to apple9) and the ray-tracing flags; a device below apple5 is refused. Four `vid_restart` cycles between `metal` and `soft` under `ASAN=1`: status 0, no sanitizer line, no `ref_metal` object live after teardown. `tests/check-ref-metal.sh` (host test, build, vocabulary check with its mutant) is green; the vocabulary check rejects a `GL_` identifier outside string literals and comments (the `gl_texturemode` value strings such as `"GL_LINEAR_MIPMAP_NEAREST"` stay legal) | TASK84 (#195) |
| N2 | Images, textures, the 2D pipeline and the cinematic; the shim's GL-free arithmetic moved to `metal/common/` | N1 | T0 | opus | sections 2 ("Inputs and bounds"), 4, 5, 8 | `compare metal cinematic --ref gl1-metal` (`PINS_DEMO=idlog.cin PINS_EVERY=7`) passes on every frame at the tight values. `gl1-metal` records of `hud2d`, `cinematic` and `world` after the move compare at score 0 and bad 0 against records before it, and the shim's host test passes with the same check count. `metal_stats` reports 0 pipelines created after init. An image whose width times height exceeds the staging ring is not uploaded (skipped or `ri.Sys_Error`, no write past the ring), with a host-test case of an oversized count (as built: `cinematic` scores 0 and bad 0 on all 12 frames, byte-identical to `gl1-metal`, in three release records and one `ASAN=1` record; the `gl1-metal` records after the move are byte-identical to those before it on all three scenarios, the shim's host test 501 checks before and after; every captured frame reports 0 pipelines created after init; an oversized image is skipped and counted; design section 17) | TASK85 (#196) |
| N3 | Pics, characters, the scrap and the HUD | N2 | T0 | sonnet | section 5 | `compare metal hud2d --ref gl1-metal` passes on all 23 frames at the tight values (this also proves the row order on frames that are not uniform) (as built: `hud2d` scores 0 and bad 0 on all 23 frames in three release records and one `ASAN=1` record, `cinematic` on all 12; no validation message; every captured frame reports 0 pipelines created after init; the scrap allocator, the quads and the batch capacity are host-tested with 28 mutants killed; the card is M, it touched `rm_image.c`; design section 18) | TASK86 (#197) |
| N4 | World geometry: BSP registration, retained world buffers, traversal, fullbright surfaces, the screen blend | N3 | T0 | opus | sections 2 ("Inputs and bounds"), 3, 9, 11 | A new scenario `world-fullbright` (`world` plus `r_fullbright 1`) passes `pins.sh selftest`, and two `gl1-metal` records of it compare at score 0. `compare metal world-fullbright --ref gl1-metal` passes at the tight values on every frame on which `metal_stats 1` reports no skipped sky, warped, translucent or flowing surface (flowing surfaces count as skipped until N6 draws them scrolled); the card lists the others. The screen blend (gl1's `R_PolyBlend`, one `color` quad as the 3D pass's last draw) is drawn, so the frames with a blend are judged here (the scenarios leave `gl_polyblend` at 1). The built-in texture `r_notexture` (made by gl1's `R_InitParticleTexture`, `gl1_misc.c`; N3 left it out) is made here and kept across registrations: it is the fallback of a missing world texture (and, from N7, of a missing skin). The retained vertex, index and surface-record sizes are logged per map. A face, vertex or index count from the BSP that would overflow a retained buffer is refused (skipped or `ri.Sys_Error`, no write past the end), with a host-test case of an oversized count (as built: two `gl1-metal` records of `world-fullbright` compare at 0; frames 19 to 22 are excluded for skipped sky and translucent surfaces, and frames 0 to 20 pass at the tight values in three release records and one `ASAN=1` record, 15 of the 19 judged ones at 0, the others within 6 pixels; the blend is judged on 5 frames; `hud2d` and `cinematic` at 0; 0 pipelines created after init; the world vertex is 32 bytes with w stored, which the shim's clip coordinates needed; 45 mutants killed, and the six a review left alive after its cases; about 3,700 lines against the estimate of 1,150; design section 19) | TASK87 (#198) |
| N5 | Lightmaps: one array texture, lightstyles, overbright, staged updates | N4 | T0 | opus | sections 2 ("Inputs and bounds"), 5, 6 | `compare metal world --ref gl1-metal` passes at the tight values on every frame `metal_stats 1` reports no skipped surface for (flowing surfaces count as skipped, like sky, warped and translucent ones, until N6 draws them scrolled). A scratch copy of `world` with `gl1_overbrightbits 2`, recorded on both renderers, passes at the same values (not committed). A map that needs more than `MAX_LIGHTMAPS` slices, or a surface rectangle outside its slice, is refused (skipped or `ri.Sys_Error`, no write past the array), with a host-test case of an oversized count (as built: frames 19 to 22 of `world` are excluded for skipped sky and translucent surfaces, none for dynamic lights; frames 0 to 20 pass at the tight values in three release records and one `ASAN=1` record (the 19 judged ones and two excluded ones), each within 6 differing pixels, which with `GL_NEAREST` or `GL_LINEAR` are 0 on frames 0 to 20, so the array texture and the lightmap filter add no difference; the overbright 2 copy passes the same frames; `world-fullbright` unchanged, `hud2d` and `cinematic` at 0; 0 pipelines created after init; a map needing a 127th slice, extents past gl1's limits and light samples past the lump are refused; 98 mutants, 96 killed, 2 equivalent; 400,000 fuzzed maps clean; about 2,430 lines against the estimate of 850; design section 20) | TASK88 (#199) |
| N6 | Sky, water warp, translucent and flowing surfaces | N5 | T0 | sonnet | sections 5, 9 | `compare metal world --ref gl1-metal` and `compare metal skywater --ref gl1-metal` (from `demo1.dm2`) pass on every frame at the tight values, with no skipped surface left. `skywater` with `PINS_DEMO=water.dm2` passes at `--threshold 0.02 --bad-threshold 0.03` against two `gl1-metal` records (as built: all frames of `world` and `skywater` from `demo1.dm2` score 0 and bad 0 with no frame excluded, `skywater` from `water.dm2` 4 and 19 frames at 0, `world-fullbright` at bad 0 to 0.000013, `hud2d` and `cinematic` at 0, `demo2.dm2` 89 of 89, a scratch flowing experiment 23 of 23, `ASAN=1` clean, `metal_stats 1` 0 skipped on every line, 17 pipelines and 0 created after init, no validation message and 0 live objects; the turbulence table is gl1's halved one; a warped surface of more than 60 vertices, a coordinate outside +-9999, more than 20,000 pieces on a surface or 1,048,576 vertices on a map is refused, a sky polygon of more than 62 vertices is skipped and counted; 199 mutants of the pure code, 193 killed, 6 equivalent; 400,000 fuzzed maps clean and the 39 maps of `pak0.pak` accepted; about 3,000 lines against the estimate of 800; design section 21) | TASK89 (#200) |
| N7 | Brush entities, alias models, the view weapon, model lighting | N6 | T0 | opus | sections 2 ("Inputs and bounds"), 4, 9 | `compare metal models --ref gl1-metal` passes at the defaults against two `gl1-metal` records on every frame `metal_stats 1` reports no skipped sprite, beam or null model for; the card lists the others. An alias vertex count that does not fit the model's frame-ring region is refused (the draw skipped or `ri.Sys_Error`, no write past the region), with a host-test case of an oversized count. N7 owns the brush entities' lightmap chains (`R_GetBrushesLighting`) and calls `R_LightPoint` with an entity (the sampler's bound, `rm_lp_coords`, is in place since N5) | TASK90 (#201) |
| N8 | Sprites, beams, null models, stencil shadows, dynamic lights, flash blobs | N7 | T0 | opus | sections 6, 9 | `models` passes on every frame at the defaults against two `gl1-metal` records, also in a scratch copy with `r_shadows 1` and `gl1_stencilshadow 1`. A new scenario `dlights` (`world` plus `cl_lights 1`) passes `pins.sh selftest`; its `gl1-metal` run-to-run values are measured and recorded in the file, and `compare metal dlights --ref gl1-metal` passes at them (the tight values if it is exact), also in a scratch copy with `gl1_flashblend 1`. N8 owns `R_AddDynamicLights` (N5's step between the styles and the store) and lifts the `metal_stats` count of surfaces "drawn without their dynamic light" and its exclusion rule; the `dlights` scenario also captures frames with an animated lightstyle (`demo2.dm2` at every 7th frame, or an equivalent), so the committed pins guard the per-frame lightmap update | TASK91 (#202) |
| N9 | Particles, the full parity sweep and frame time (the screen blend moved to N4) | N8 | T0 | sonnet | sections 4, 11 | `compare metal particles --ref gl1-metal` passes at `--threshold 0.02 --bad-threshold 0.25` against two records, also in scratch copies with `gl1_pointparameters 0` and with `gl1_particle_square 1`. The particle texture (gl1's `r_particletexture`, made by `R_InitParticleTexture` beside `r_notexture`; N3 left it out) is made here and kept across registrations. Every scenario passes at its value of the design's section 11 in one sweep at one commit, recorded as a table in the runbook. The `timedemo` fps of `metal` is at least that of `gl1-metal` on every scenario at the same load, both recorded | TASK92 (#203) |
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
