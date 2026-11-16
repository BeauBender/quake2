# A hidden model never leaves a hole in the water reflection at `r_rt_water_hits 0` (FIX81)

FIX81 follows [TASK58](TASK58.md) (the default pass, `rm_rtw_trace`) and
[TASK158](TASK158.md) (`r_rt_water_hits 1`, whose *Review round 1* passes over hidden models). It
uses TASK58's masked comparison (*The mask* in its file) and the shared runbook's method for the GPU
time ([the shadows' proofs](../plans/metal-port-harness.md#ray-traced-shadows-from-dynamic-lights-in-ref_metal-task55-rt3)).

## The finding

TASK58's reflection ray stopped at the first face it met. An alias model the picture does not show
(behind a wall or another model, in the PVS, or the far side of a model the picture does show) could
be that face, while what lies behind it on the ray, a wall the picture shows, would have given a
reflection. The pixel then got none, so the water showed the hidden model's outline as a
stock-colour hole. TASK158's security pass found it.

## The fix

- *The rule.* `rm_rtw_trace` now takes `r_rt_water_hits 1`'s step after TASK58's choice
  (`mtl_rtw_ss_next` in `common/mtl_rtw_math.h`, which is `mtl_rtw_ws_next`): a hit on an alias
  model whose choice is "no reflection" (the picture does not show it at its point) is passed, and
  the ray is intersected again just past it with the same mask; at the `MTL_RTW_LAYERS`-th such face
  it is traced on with `MTL_RTW_MASK_MAP` alone, so it ends where it would end without the models, in
  at most five intersections. Every other first hit stops the ray as before: a ray that does not meet
  a hidden model is cast once and gives TASK58's choice, so those pixels are TASK58's bit for bit. A
  map face the picture does not show (a hidden door too) still gives no reflection at `0`.
- *Which mechanism, and why.* TASK158's loop (pass with the same mask, then the map's mask alone),
  not one re-trace with the map's mask from the first hit. The single re-trace also passes a model
  the picture shows behind the hidden one, and the wall behind that model is hidden on the screen by
  it, so the overlap of the two would stay a hole (part of the hidden model's outline). The loop gives
  the shown model there, as without the hidden one; it costs a second cast only on the rays that meet
  a hidden model, and the two settings now pass over exactly the same models. The bit
  `MTL_RTW_MASK_MAP` is already on the map's instances at every setting (`rmb_rt_frame`), so no
  host-side setup changed.
- *Files:* `rm_rtwater.metal` (the kernel's loop and comments), `common/mtl_rtw_math.h`
  (`mtl_rtw_ss_next`), `tests/metal/rm_rtwater_test.c`, `doc/040_cvarlist.md` (`r_rt_water` and
  `r_rt_water_hits` now say that a model the view does not show is passed over at both settings, and
  a hidden map surface gives no reflection at `0`), this file. Nothing outside the card's list but
  this file.

## The host test

`test_ss_models` in `tests/metal/rm_rtwater_test.c`: a driver of `rm_rtw_trace`'s loop over scripted
faces (TASK158's intersector that honours the masks), with TASK58's choice and the new step. The
step alone; the step equal to `mtl_rtw_ws_next` for every input (both settings pass the same
models); one cast and TASK58's choice with nothing in front; a hidden door stops the ray; the card's
case, a hidden model (two faces) in front of a wall the picture shows, gives the wall (`LIT`), in
front of the sky the sky, in front of a hidden wall none; a hidden model in front of a shown one gives
the shown one; any number (0 to 9) of hidden model faces before any end give what the end gives
alone, in at most five casts. **Red first** with main's rule (the step stubbed to "every face stops
the ray"): 10 of 438 checks failed, the hidden-model cases ending with no reflection
(`<artifacts>/FIX81/tdd/red-main-rule.out`); with the fix 438 checks, 0 failed.

## Runs on the bare-metal host

Commits measured: main `419eeda7` (TASK158's merge) and the fix's first version `979a52c9`, whose
kernel folded the first hit into the loop. The final kernel, `03dd4163`, keeps TASK58's first-hit code
verbatim and runs the re-trace in a block of its own (*Code shape*, below); it is built and
host-tested, and its engine runs are in *The final kernel* below (made with the screen locked). The
first version's runs checked that the screen was unlocked and that no other `quake2` ran, and held the timing lock
(`tools/heavy.sh`, under four minutes a hold); the one-minute load was 2.0 to 4.5. `PINS_SEED=1`.
Both builds were copies of the worktree's `release/` (the client binary is byte-identical, so each
build had a pin store of its own, or a `--run` of its own in one store). A scratch copy of the
harness holds the cases: `fx-on` (`rt-water-entities.cfg` plus `r_rt_stats 2`), `fx-off`
(`r_rt_water 0`, `r_rt_gbuffer 1`) and the cost cases (`rt-water.cfg` plus `r_rt_stats 2`, `r_metalfx
2`, `r_vsync 0`). Every comparison is on one trace: main's record, the fix's replay of its trace
(TASK46), and the classes view (`r_rt_water_show 2`) of both from a copy of the trace with that cvar
rewritten at the same length. Main's own replay of its record equals the record byte for byte (17 of
17 and 4 of 4 frames). Everything is in `<artifacts>/FIX81/`.

### Reproduction: the hole on main

The stock demos show no hidden-model hit: `water.dm2` has no alias model in view or in its PVS,
and over all of `demo1.dm2` and `demo2.dm2` (640 x 480, every frame's stats line) main and the fix
differ by at most one pixel, within two live records' warp difference. So a demo was made
(`scripts/pins.sh make-demo` from the scratch harness, `fix81.cfg`: `water.cfg` with six ammo
boxes dropped into the pool of base1 before the record, 120 messages). Under the water the
reflection rays from the surface meet the boxes on the pool floor, and the parts of them the view
does not show (their far sides, and boxes behind boxes) were the first hit of rays whose wall behind
the picture shows.

| Trace | Frames | No reflection on main (red in the classes view) | Of those, filled by the fix (red to green) | Frames with a hole |
|---|---|---|---|---|
| `fix81.dm2`, 640 x 480, every 7th | 17 | 114,890 | **1,145** (60 to 292 a frame) | 9 of 9 under the water, 0 of 8 over it |
| `fix81.dm2`, 1920 x 1080, every 30th | 4 | 165,505 | **728** (100 and 628) | 2 of 2 under the water |
| The same, every traced frame (stats line) | 113 / 111 | | 7,614 / 26,940 pixels from none to lit | 63 / 89 |

Every changed class is red (no reflection) to green (the picture's colour): no other class changed
on any frame of these traces.

### The masked comparison against main

The mask is main's classes view with its red pixels black and every other pixel white
(`fx81 notred`, a scratch tool), so it marks every pixel that must not change: everything but the
water main left without a reflection. Counted with an exact per-pixel comparison (any channel),
main's record against the fix's replay:

| Trace | Frames | Changed inside main's no-reflection pixels | Changed elsewhere (must be 0) |
|---|---|---|---|
| `fix81.dm2`, 640 x 480 | 17 | 1,145 (exactly the red-to-green pixels) | 41, one level, on 15 frames |
| `fix81.dm2`, 1920 x 1080 | 4 | 728 | 62, one level |
| `rt-water` (no model), 640 x 480 | 20 | 0 | 56 on 20 frames; one pixel lit to none |
| `rt-water`, 1920 x 1080 | 5 | | 97 (no classes view recorded) |
| `rt-water-entities`, 640 x 480 | 20 | | 56 |
| Effect off: `fx-off` (`r_rt_water 0`, entities), 640 x 480 | 20 | | **0: byte-identical** |
| Effect off: `skywater`, 640 x 480 | 20 | | **0: byte-identical** |

<a id="code-shape"></a>**Code shape.** On `rt-water`, which has no alias model, the loop can never
pass anything, yet the first version changed 1 to 7 pixels a frame by one level (and one pixel's
choice). Main's own replay equals main's record, so this is the compiler's arithmetic: folding the
first hit into a loop changed how the Metal compiler (fast math) arranged the projection's
floating point. The final kernel (`03dd4163`) keeps the first hit's choice as TASK58's source,
line for line, and the re-trace past a hidden model is a block after it (its per-hit choice is
`rm_rtw_ss_at`, the same steps), entered only when the step says to pass. Its `rt-water`
frames are byte-identical to main's (*The final kernel*, below).

### Cost

GPU time of the pass in ms from the `r_rt_stats 2` line (the shared runbook's method), `water.dm2`
whole, no validation layer, each run's lines past its first 20 (117 a run), "held" the display's
sync and "not held" `r_vsync 0`; 5120 x 2880 is `PINS_HIDPI=1` at 2560 x 1440 with `r_metalfx 2`.
The fix: three runs a case (`979a52c9`), alternated with main in rounds. Main: the drift check of
TASK58's stored baseline (`<artifacts>/baselines/d89bf519f27c/`) held, and three runs a case not
held, stored as `<artifacts>/baselines/419eeda7b66a/rt-water-cost.txt`.

| Output | Sync | Main `419eeda7`, median (p10 / p90) | The fix, median (run medians) | p10 / p90 |
|---|---|---|---|---|
| 1920 x 1080 | held | 4.67 (3.53 / 4.76), one run | 4.75 (4.76, 4.75, 4.76) | 4.60 / 4.84 |
| 1920 x 1080 | not held | 0.99 (0.97 / 1.05) | 1.00 (1.01, 1.00, 1.00) | 0.99 / 1.07 |
| 5120 x 2880, `r_metalfx 2` | held | 3.58 (3.39 / 3.78), one run | 3.70 (3.62, 3.60, 3.77) | 3.44 / 3.82 |
| 5120 x 2880, `r_metalfx 2` | not held | 0.99 (0.98 / 1.38) | 1.00 (1.00, 1.00, 1.23) | 0.99 / 1.36 |

Not held, the fix costs 0.01 ms more (1 %). Held, 0.08 and 0.12 ms more against one run of main;
the held drift checks fell outside TASK58's stored range (4.67 against 3.40 ms at 1080p, 3.58
against 3.48 with `r_metalfx 2`), so the stored held baseline was not reused and main's held case
has one run only: the held difference is within main's own spread (its p10 to p90, 3.5 to 4.8 ms).
`water.dm2` has no alias model, so this is the loop's fixed cost (a ray that meets no hidden model
is cast once); the re-traces add work only where hidden models are hit. The final kernel's cost is
in *The final kernel*, below.

### Frames for the owner

Two before/after pairs at 1920 x 1080, the made demo's captures 2 and 3 (under the water, 100 and
628 pixels filled), main's record and the first version's replay, converted with `sips`:
`<artifacts>/FIX81/frames/<n>-before.png` and `<n>-after.png`, never committed.

## The final kernel

Commit `03dd4163`'s source (the branch head `f8baa052` adds only this file), built from `f8baa052`
(`make ref_metal`, release flags, Metal shaders compiled) and copied into the fix's build. Main is
`419eeda7` as before. **These runs were made with the screen locked** (`CGSSessionScreenIsLocked`
set), under `tools/heavy.sh`, no other `quake2` running. Replays, the byte and masked comparisons and
the frame captures do not depend on the screen: on this host a locked replay of main's `rt-water`
record equalled the unlocked record in 20 of 20 frames. The one-minute load was 2.3 to 3.7 before and
after every run. `PINS_SEED=1`; every replay is of main's stored record, `--run 2`.

### The comparison against main

Main's record against the final kernel's replay, any channel, the same traces as above:

| Trace | Frames | Differing frames | Changed pixels |
|---|---|---|---|
| `rt-water` (no model), 640 x 480 | 20 | **0: byte-identical** | 0 (inside or outside the mask) |
| `rt-water`, 1920 x 1080 | 5 | **0: byte-identical** | 0 |
| `rt-water-entities`, 640 x 480 | 20 | **0: byte-identical** | 0 |
| `fx-off`, 640 x 480 | 20 | **0: byte-identical** | 0 |
| `skywater`, 640 x 480 | 20 | **0: byte-identical** | 0 |
| `fx81.dm2`, 640 x 480 | 17 | 9 (under the water) | 1,145 inside main's no-reflection pixels, **0 outside** |
| `fx81.dm2`, 1920 x 1080 | 4 | 2 | 728 inside, **0 outside** |

That answers the open question: with TASK58's first-hit code kept verbatim, a trace that meets no
hidden model is byte-identical to main (the first version changed 56 to 97 pixels by one level on
the same traces). On the made demo the changes are exactly the first version's, 1,145 and 728 pixels,
all inside the mask (main's red pixels, no reflection), none outside it: the same classes view
transitions, red to green only. Main's own replay equals main's record on the made demo (17 of 17
and 4 of 4 frames), so the mask holds against the record too.

### Cost, screen locked

The held cases (display sync on) were **not run**: they need an unlocked screen. A drift check came
first: one run of main's build on the not-held 1920 x 1080 case, 117 lines: median 0.99 ms, p10 0.98,
p90 1.04, inside the stored baseline's not-held range (0.99, p10 0.97, p90 1.05), so the locked GPU
time was taken as comparable for the not-held cases. Three runs a case, the fix alternated with main
in rounds (the same method, `water.dm2` whole, each run's lines past its first 20):

| Output | Sync | Main `419eeda7`, median (p10 / p90) | The final kernel, median (run medians) | p10 / p90 |
|---|---|---|---|---|
| 1920 x 1080 | not held | 0.99 (0.97 / 1.15), runs 0.98, 0.98, 1.01 | 1.00 (1.00, 1.00, 1.00) | 0.99 / 1.05 |
| 5120 x 2880, `r_metalfx 2` | not held | 1.29 (0.97 / 1.38), runs 1.26, 1.29, 1.28 | 1.01 (1.01, 1.29, 1.15) | 1.00 / 1.39 |

At 1920 x 1080 the fix costs 0.01 ms more (1 %), as the first version did. The 5120 x 2880 case
is not comparable: the drift check covered only the 1080p case, and this case alternates between
about 1.0 and about 1.3 ms in both builds (main's stored baseline had 0.99 with p90 1.38; this
time main's runs sat at the upper level and the fix's at both), so the medians say nothing about
the fix there; its distributions overlap (p90 1.38 and 1.39).

### Frames for the owner

The two before/after pairs of the made demo (captures 2 and 3, 1920 x 1080) were regenerated from
the final kernel's replay (`<artifacts>/FIX81/frames/<n>-before.png` and `<n>-after.png`, replaced;
never committed, never viewed here).

## Checks

On `f8baa052`, each through `tools/heavy.sh`: `tests/check-doc-links.sh` and `tests/check-no-local-identity.sh`, in the card's report.

## Review round 1: the stated bound

The review of `abc0aad4` found that the doc and the comments promised "never a hole" and "however many models" where the code passes at most `MTL_RTW_LAYERS` (four) hidden model faces. After that the ray is traced against the map alone, so a model the picture shows that lies behind four hidden faces is passed too: at `r_rt_water_hits 0` the reflection there is none (a hole), at `1` it is the wall behind the model in world space. One hidden player with its weapon is already four faces, because the rays cull no triangle.

**The bound stays.** It keeps the cost at five casts per water pixel at most (`MTL_RTW_LAYERS + 1`); lifting it would make the cost depend on how many models stand in line. So no compiled code changes: the kernel comments, the header comments, the `r_rt_water` and `r_rt_water_hits` rows of `doc/040_cvarlist.md` and this runbook now state the bound, and also that the pass-over works per face (a face of a model the view does not show at that pixel is passed, so a shown model's unseen far side is passed too).

### The test

`test_ss_models` in `tests/metal/rm_rtwater_test.c` pins it. Faces in order along the ray, values read from the code before they were asserted:

| case | faces | result |
| --- | --- | --- |
| A | shown model, hidden wall | `MTL_RTW_LIT`, 1 cast |
| D | three hidden model faces, shown model, hidden wall (hits 0) | `MTL_RTW_LIT`, 4 casts |
| B | four hidden model faces, shown model, hidden wall (hits 0) | `MTL_RTW_MISS`, 5 casts |
| C | the faces of B at hits 1 (`rtw_trace_ws`) | `MTL_RTW_LIT \| MTL_RTW_WS` |

B and C are the bound: the model the picture shows is passed, the ray ends at the hidden wall. The suite prints `438 checks` before and `445 checks, 0 failed` after.

### The code is unchanged

Preprocessed with comments stripped, the kernel at `abc0aad4` and at this head give an empty diff, and so does the header:

```sh
xcrun -sdk macosx metal -E -P src/client/refresh/metal/rm_rtwater.metal > kernel.i   # 2039 lines at both heads
diff kernel.i.abc0aad4 kernel.i.head    # empty
# header: a file that includes mtl_rtao_math.h, then mtl_rtw_math.h (the header needs it first)
cc -std=gnu99 -E -P inc.c > header.i                                                  # 898 lines at both heads
diff header.i.abc0aad4 header.i.head    # empty
```

So the replays, the masked comparison and the costs of the sections above stand for the new head unchanged: no byte of the compiled kernel or the host code differs.

### Checks

`tests/check-ref-metal.sh`, `tests/check-doc-links.sh`, `tests/check-runbook-index.sh` and `tests/check-no-local-identity.sh`, each through `tools/heavy.sh`: see the card's report.
