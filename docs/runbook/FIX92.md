# A hidden door, plat or lift leaves no hole in the water reflection at `r_rt_water_hits 0` (FIX92)

FIX92 follows [FIX81](FIX81.md) (the pass-over loop for hidden alias models and its bound of
`MTL_RTW_LAYERS + 1` casts) and [TASK158](TASK158.md) (`r_rt_water_hits 1`), and is stacked on
[TASK162](TASK162.md) (the water marks in the G-buffer raster). It uses TASK58's masked comparison
(*The mask* in [its file](TASK58.md)).

## The finding

An inline brush model (a door, a plat, a lift) is a map face. At `r_rt_water_hits 0` a reflection ray
whose first hit is such a face, where the view does not show it, got no reflection
(`mtl_rtw_choose`), and `mtl_rtw_ss_next` stopped the ray there, since it passes alias models only.
So a door opening or a lift moving behind a wall left its outline as a stock-colour hole in the
reflection of a wall the view shows. The host test pinned it ("a hidden door is a map face") and
`doc/040_cvarlist.md` stated it.

## The design

**How a face is told apart as the world's or an inline model's.** Not by a mask bit: a Metal
intersection result gives the instance's index, the primitive and the distance, not the instance's
mask, so a mask bit only chooses what a query tests and cannot say what a query met. The per-instance
words that `rmb_rt_frame` already writes do tell: the first word of each instance
(`inst[2 * instance_id]`) is its first triangle of the world's index buffer, `~0` for an alias model
and `map_first[model]` for the world (model 0) and for each inline model. `rmb_rt_bind_geometry`
now gives the world's word in the fourth word of the geometry constants (`rm_rtw_geom.world`, which
was the padding `0`; `~0` when the world has no structure), and `mtl_rtw_brush(b, world)` says a
face belongs to an inline brush model when its instance's word is neither `~0` nor the world's. The
submodels' triangle ranges do not overlap, so the world's word is its own; a malformed map whose
inline model starts at the world's first triangle has that model taken as the world, whose faces
stop the ray as before, so the fault can only keep FIX81's picture, never pass more. No instance
mask changes, so no other query (the shadows, the AO, the eye ray that finds translucent water)
meets anything different.

**How `mtl_rtw_ss_next` passes over a hidden inline model.** The step takes the brush flag beside
the alias flag: at `0` a face of an alias model or of an inline brush model whose choice is "no
reflection" is passed, and the ray is intersected again just past it with the same mask, as FIX81
passes an alias face; at the `MTL_RTW_LAYERS`-th such face it is traced on with `MTL_RTW_MASK_MAP`
(FIX81's last cast), and that cast's choice stands. So the ray is cast at most `MTL_RTW_LAYERS + 1`
times, FIX81's bound, whichever models it passes. A world face the view does not show still stops
it with no reflection, a door the view shows gives its reflection, and a ray that meets no hidden
brush model face takes exactly FIX81's steps. A closed brush has a front and a back face (the rays
cull none), so a hidden door costs two more casts, as a hidden alias model does.

*The bound.* The last cast's mask still holds the brush models, so past four hidden model faces a
brush model is met as on FIX81: one the view shows gives its reflection, a hidden one none. A mask
of the world alone for the last cast would pass a hidden one there too, but also a door the view
shows behind four hidden alias faces, which FIX81 shows: with the map's mask no ray that FIX81
reflects loses its reflection.

**Why `r_rt_water_hits 1` is unchanged.** `rm_rtw_trace_ws` calls `mtl_rtw_ws_next`, which is not
changed: at `1` a hidden brush model is shaded in world space like any other map face (TASK158), so
it is not passed. The new word is bound to both kernels with the other geometry constants, and only
`rm_rtw_trace` reads it; the G-buffer marks, the compaction and the indirect trace are untouched.

## The host test

`test_ss_models` in `tests/metal/rm_rtwater_test.c`. The scripted faces now carry an inline model
(`rtw_face_t.model`, 0 the world, 1 to 3 a door, a plat and a lift), and the driver of
`rm_rtw_trace`'s loop reads the alias and brush flags from the hit instance's first word
(`rtw_face_word`: `~0`, the world's first triangle or the model's) through `mtl_rtw_brush`, as the
kernel does. The cases, with the world's first triangle 0 and 500:

- the step alone: a hidden brush model's face is passed, `MTL_RTW_NEXT_MAP` at the
  `MTL_RTW_LAYERS`-th; a shown one, a world face, the sky, nothing and the map's last cast stop;
- at both settings the same alias models are passed, and at `0` a brush model's face as an alias
  model's (`mtl_rtw_ss_next(.., 1, ..)` equals `mtl_rtw_ws_next(.., alias 1, ..)` for every input);
- `mtl_rtw_brush` for an alias model, the world, an inline model below and above the world's first
  triangle, and no world structure (`~0`: every map instance a brush model);
- "a hidden door is a map face" is now "a hidden world face stops the ray", and a hidden door, plat
  or lift, one face or two, before a wall the view shows gives the wall (`LIT`, in 2 or 3 casts),
  before the sky the sky, before a hidden wall or nothing none;
- a door the view shows stops the ray with its reflection, in one cast, before a shown or a hidden
  wall; a hidden door before a shown plat gives the plat; a hidden door and a hidden model before a
  wall give the wall in `MTL_RTW_LAYERS + 1` casts;
- the bound: a hidden door and a hidden plat (four faces) before a shown wall give the wall in five
  casts; a hidden lift past them is met by the last cast (none); a shown lift past them, or past four
  hidden alias faces, gives the lift; at `1` the first hidden door is shaded where it is (one cast);
- every mix of up to four hidden faces (alias, door, plat, lift: 341 mixes) before each of the five
  ends gives what the end alone gives, in `k + 1` casts.

At `1` (`test_ws_models`) a hidden door, plat or lift, its two faces before a shown wall, is shaded
where it is, in one cast.

**Red first**, with the step taking the flag but ignoring it (FIX81's rule, commit `5965a563`): 71 of
722 checks failed, every one a hidden-brush case (`<artifacts>/FIX92/tdd/red-stub.out`). With the rule
(`c4835dae`): 722 checks, 0 failed.

**The mutants.** The test file builds two by hand (`-DRTW_MUTANT=1` or `2`, the flags of
`tests/check-ref-metal.sh` otherwise): 1 drops the brush flag (`mtl_rtw_brush` finds no inline
model): 72 of 722 checks fail; 2 drops it from the step: 71 fail (`<artifacts>/FIX92/tdd/mutant1.out`,
`mutant2.out`).

## Runs on the bare-metal host

Builds: the fix `c4835dae` (`make client ref_metal game ref_soft` in the worktree) and the base,
TASK162's head `8d17698f`, whose `ref_metal` was built from an export of that commit and loaded by the
fix's client (byte-identical: the card changes only `ref_metal`). **The screen was locked for every
run**; records and byte comparisons are valid locked (the brief's 2026-11-16 check). No other `quake2`
ran; every batch held `tools/heavy.sh`; the one-minute load was 2.5 to 4.3. `PINS_SEED=1`,
`MTL_DEBUG_LAYER=1` on every run: each of the 33 records' logs holds the validation banner and no
other line naming validation. A scratch copy of the harness adds TASK162's cases (`fx-off`, `rtw-h1`,
`rtw-h1e`) and the classes views (`rtw-h0ecls`: `rt-water-entities.cfg` with `r_rt_water_show 2`;
`rtw-h1ecls` the same at `1`). Both demos were made once, by `scripts/pins.sh make-demo`, and the same
files lay beside both builds. Everything is in `<artifacts>/FIX92/` (`logs/runs.txt`, `compare.sh`,
`mask.sh`, `tga.py`).

### The traces that must not change

`water.dm2` (98 messages), the fix's record against the base's, frame by frame:

| Scenario | Output | Frames equal |
|---|---|---|
| `rt-water` | 640 x 480, every 6th | 16 of 16 |
| `rt-water` | 1920 x 1080, every 30th | 4 of 4 |
| `rt-water-entities` | 640 x 480 | 16 of 16 |
| `fx-off` (`r_rt_water 0`, `r_rt_gbuffer 1`, entities) | 640 x 480 | 16 of 16 |
| `skywater` | 640 x 480 | 16 of 16 |
| `rtw-h1` (setting 1) | 640 x 480 | 16 of 16 |
| `rtw-h1e` (setting 1, entities) | 640 x 480 | 16 of 16 |

No frame of these changed: `water.dm2` has no brush model the reflection rays meet hidden, and the
kernel's first hit is TASK58's code as it was (FIX81's *Code shape*), so the compiler's arithmetic
did not move either.

### The door case

No stock demo shows a hidden brush model in a reflection, so one was made: `fix92.dm2` (106
messages, `demos/fix92.cfg` in the scratch harness), the pool of `city1` with its two plats standing
in the water (`func_plat` models 84 and 89), from six places over and under the water, facing the
level start's yaw. The mask is the base's classes view with its red pixels (no reflection) marking
the only pixels that may change. Each base record was made twice (`base`, `base2`):

| Trace | Frames | Frames that changed | Changed inside the base's no-reflection pixels | Outside (must be 0) | Classes view |
|---|---|---|---|---|---|
| `fix92.dm2`, 640 x 480, every 3rd | 33 | 8 | **152** (1 to 57 a frame) | **0** | red to green only, 152 |
| `fix92.dm2`, 1920 x 1080, every 10th | 10 | 4 | **200** (6 to 99 a frame) | **0** | red to green only, 200 |

The last capture of `fix92.dm2` (frame 32 at 640 x 480, at the demo's end) is not deterministic in
either build: of two base records, or of two fix records, one can differ from the other there (25
pixels of the classes view, about 4,000 of the picture) at both settings, and the fix's frame 32
equals one of the base's two. It is left out of the counts above; every other frame of every pair of
base records is equal.

### Setting 1

At `r_rt_water_hits 1` the fix is byte-identical to the base: `rtw-h1` and `rtw-h1e` on `water.dm2`
above, and on `fix92.dm2` `rtw-h1e` (the picture) and `rtw-h1ecls` (the classes view) 32 of 33
frames, the 33rd the demo's last capture, equal to one of the base's two records.

### Cost

Measured on the owner's protocol, one `tools/heavy.sh` hold, the screen unlocked (`ioreg`
`CGSSessionScreenIsLocked` 0 before the queue, at the batch's start, after the pre-warm, after every run
and at the batch's end; no other `quake2` ran). The case is the frame's own GPU time, `MTL_GPU_TIME=1`'s
`gpu_ms ... frame` line (each run's first 20 lines dropped, 115 frames a run), at `r_rt_water_hits 0`,
not held (`r_vsync 0`), 1920 x 1080, FIX81's `water.dm2` played whole, `PINS_SEED=1`,
`PINS_EVERY=100000`, `r_rt_stats` off, no validation layer. The builds are this branch's `193a9687` and
main's `512c0745` (which holds TASK162, this branch's base), each a full build of its own export. After
a 240 s full-load pre-warm (210 runs of the same case) they ran alternated, three runs a build. Records
are in `<artifacts>/FIX92/timing/` (`logs/summary.txt`, `logs/*.engine.log`, `logs/medians.txt`,
`snaps/`, `batch.sh`).

Run medians in ms (rounds 1, 2, 3):

| Build | Run medians | Median of the three | Range |
|---|---|---|---|
| This branch (`193a9687`) | 2.409, 2.563, 2.410 | 2.410 | 2.409 to 2.563 |
| Main (`512c0745`) | 2.387, 2.403, 2.546 | 2.403 | 2.387 to 2.546 |
| Branch against main | | +0.007 ms (+0.3 %) | inside both ranges |

**Frequencies** (the no-root IOReport probe, `freq dump` before and after each run, `freq delta`):

| Window | eacc MHz (top state %) | pacc0 MHz (top state %) | GPU MHz (top state %), active % |
|---|---|---|---|
| Pre-warm (210 runs, 240 s) | 1533 (10.6) | 4520 (74.9) | 892 (1.2), 40 |
| Runs, branch (3) | 1471 to 1506 (4.6 to 11.8) | 4509 to 4545 (63 to 68) | 892 to 907 (0), 39 to 40 |
| Runs, main (3) | 1504 to 1592 (6.8 to 9.7) | 4510 to 4788 (66 to 100) | 834 to 935 (0), 38 to 42 |

The GPU never reached its top state in a run and its clock differs by up to 12 % between runs
(834 to 935 MHz), which is as large as the spread of the run medians. Main's second run had the
performance cluster at its top state for the whole run (4788 MHz) and `mediaanalysisd` at 119 % CPU
in its after snapshot, so that run is the least quiet; its 2.403 ms is the middle of main's three, so
dropping it would not change the verdict. Load average 3.75 to 4.83 over the batch (the pre-warm's
own), GPU device utilization 0 to 4 % before each run (limit 10 %), memory free 84 %, no swap, no
thermal or performance warning, low-power mode off. The top process in a snapshot was `mediaanalysisd`
(68 % before the first run, 8 % before main's second); no `backupd` or `mds`.

**What it shows.** At `r_rt_water_hits 0` in a view that meets no hidden brush model the fix costs
nothing the frame shows: the branch's median is 0.007 ms from main's, with the run medians of the two
builds interleaved inside a 0.17 ms spread.

## Checks

On the branch, each through `tools/heavy.sh`: see the card's report. The full `scripts/test.sh`
was not run on this lane. `tests/check-runbook-index.sh` fails on this file until its index line is
added to `docs/runbook/INDEX.md` at merge (the base predates that index).
