# Option E and the hybrids: each ray-traced effect at its own scale, upsampled (TASK126)

TASK126 is RT13 of `docs/plans/metal-raytracing-design.md` (section 6), option E of section 2
("Option E, from first principles"). By the owner's direction of 2026-11-14 on TASK109 (#263) there
is no `r_rt_scale`: `r_metalfx_scale` is the internal scale on every path. The configurations of
section 2, "Configurations a test names", read:

| Configuration | `r_metalfx` | `r_metalfx_scale` | Effect scales |
|---|---|---|---|
| A | 2 | 0 (the family default) | 1 |
| E | 0 | – | 1/2 |
| H | 2 | 0.5 | 1/2 |
| C | 0 | – | 1 |

E at every effect scale 1 is C, one frame. A at `r_metalfx_scale 1` is not C: the scaler still runs
at 1:1, as temporal anti-aliasing.

## The plan

- **What main has.** Every effect runs at the render size, `RM_GB_TargetSize`: the output at
  `r_metalfx 0`, the scaler's input under `r_metalfx 2`. So an effect scale applied to that size is
  the hybrid with no code of its own. Every reader of an effect (the resolves, the water's blend)
  reads its texture at the drawn pixel, never through a sampler, so an effect traced smaller needs an
  upsample before its reader. AO already traces at `r_rt_ao_scale` (default 0.5, TASK157) and
  upsamples with its own surface test; its RT7 runs after the upsample, at the render size.
- **The cvars.** `r_rt_shadows_scale`, `r_rt_sun_scale` and `r_rt_water_scale`, archived, default
  `1`, the values of `r_rt_ao_scale` (1, 1/2, 1/3). A bad value of the new scales (zero, negative, not finite) is 1, the
  effect at the drawn size; a bad `r_rt_ao_scale` is not: `rm_rtao_div` sends it to its default, 0.5,
  divisor 2. Each lands with the slice that
  wires its effect, with its entry in `doc/040_cvarlist.md`, so no cvar is documented before it does
  anything. AO keeps `r_rt_ao_scale` and its upsample.
- **Not scaled.** RT-primary and its bounce stay at the render size: their output is the whole lit
  term, not a visibility or reflection factor, and RT-primary already lowers its cost through
  `r_metalfx_scale`.
- **The upsample** (`common/mtl_rtup_math.h`). The four traced pixels about a drawn pixel, in
  TASK157's block layout, each weighted by its bilinear weight times RT7's depth and normal guides
  (`mtl_rtd_depth_w`, `mtl_rtd_normal_w`), and 0 for another material id or no sample. Where no
  traced pixel matches (thin geometry, a silhouette), the one nearest in depth is taken, and it is
  counted: on each effect's `r_rt_stats 2` line, in the `r_rt_stats 1` map summary, and on a
  `r_rt_stats 1` line when an effect's traced size changes. At scale 1 the back ends dispatch
  today's kernels unchanged.
- **The reset.** Each effect's history records the divisor it was made at, and a change resets that
  effect alone, with a new reason, `scale`. The player-setup model view and a `fov` change reset
  nothing.
- **The slices.**
  1. the upsample rule and its host tests;
  2. the `scale` reset, the reprojection at the effect size and the line formatters, all pure;
  3. the sun at its scale;
  4. the shadows, with the light set in the weights;
  5. water;
  6. AO's fallback counted, the line on a device without ray tracing, and the E and H scenarios;
  7. the measurement in `docs/audits/rt-performance.md`.
- **Owner decisions** (each reversible, with the default the loop takes):
  - AO's 0.5 stays;
  - AO keeps its order and its weights;
  - the name is `r_rt_sun_scale`, beside `r_rt_sunscale`;
  - the scales are 1, 1/2 and 1/3 only;
  - the menu items are a follow-up card;
  - RT-primary stays at the render size.

## Slice 1: the upsample's rule and its host test

- **The rule.** `src/client/refresh/metal/common/mtl_rtup_math.h`, plain C99 and Metal alike:
  - `mtl_rtup_div`, an effect scale as a divisor of 1, 2 or 3, rounded as `rm_rtao_div` rounds
    `r_rt_ao_scale`. A bad value is 1.
  - `mtl_rtup_size` and `mtl_rtup_src`, the traced size and each traced pixel's source pixel, in
    TASK157's layout.
  - `mtl_rtup_bilinear`, the four taps' bilinear weights from `mtl_rtao_up_pair`.
  - `mtl_rtup_tap_w`, a tap's weight. It is the bilinear weight times `mtl_rtd_depth_w` and
    `mtl_rtd_normal_w`, and 0 for another material id or a tap with no sample.
  - `mtl_rtup_weights`, the weights normalised. Where none matches, the tap with a sample nearest in
    depth carries the whole weight (the first of equals), with `fallback` set. Where none has a
    sample, the result is `empty`, and so it is for a pixel whose own depth is not finite (infinity or
    NaN), whatever its taps hold: the neutral value is the safe result. At a divisor of 1 it is tap 0, whole, and no fallback.
  - `mtl_rtup_value`, one channel under the weights, or the neutral value where they are empty.

  No kernel includes it yet, so the Makefile's `ref_metal.metallib` list does not name it. The slice
  that adds the first kernel adds it there.
- **The test.** `tests/metal/rm_rtup_test.c` covers:
  - the divisor;
  - a scale of 1 as the identity, with no fallback for a pixel with no normal or no sample;
  - a tap across a depth step, across a material-id change, across a crease or with no sample, which
    weighs nothing;
  - the fallback to the tap nearest in depth, with the count, the passed-over tap with no sample,
    equals and a silhouette;
  - the empty case, and a pixel depth that is not finite;
  - each tap's bilinear weight on its own axis;
  - an 8 x 8 image at divisor 2. Each side of a depth step keeps its value, and a thin model line that
    no traced pixel lies on takes the far wall's value: the traced pixels nearest it in depth, 8
    fallbacks.
- **The check.** `tests/check-ref-metal.sh` builds and runs the test. It runs the test against
  eight mutants of the header, each of which compiles and must fail it with a `FAIL` line (a crash or a
  sanitizer trip is an error, not a refusal):
  - the depth guide dropped;
  - the material test dropped;
  - the normal guide dropped;
  - the sample test dropped;
  - the fallback taking the first tap;
  - the fallback not counted;
  - the identity dropped at a scale of 1;
  - the two cross weights of the bilinear taps swapped.

  Where `xcrun metal` is there, the check also compiles a kernel that calls every function of the
  header.

### What the later slices must do

- Slices 3 to 5: a kernel skips a pixel with no normal (or no sample) before it calls the rule.
  `mtl_rtd_normal_w` is 0 for a zero normal, so the rule would count such a pixel as a fallback below
  divisor 1, and the fallback counter would be inflated.

### Host checks (no lock)

| Check | Result |
|---|---|
| the upsample segment of `tests/check-ref-metal.sh`, at the red commit (`87561daa`, the test without the header) | `common/mtl_rtup_math.h is missing`; `refused 0 of its 7 header mutants` |
| the same segment with the header | `upsample 72 checks, 0 failed, 8 rule mutants refused, compiled` |
| `tests/check-cvar-docs.sh`, `tests/check-doc-links.sh`, `tests/check-runbook-index.sh` | ok, each |

### The hold (M6, under the timing lock)

The orchestrator runs the hold script. The values below are filled in from its summary.

| Check | Result |
|---|---|
| base and branch | (from the hold) |
| `make ref_metal`, `make`, `make test` | (from the hold) |
| `tests/check-ref-metal.sh` | (from the hold) |
| the 30 scenarios outside `rtprimary-*`, byte-identical to main | (from the hold) |
| every `rtprimary-*` record, byte-identical to main | (from the hold) |

No kernel is wired in this slice, so there is no run at an effect scale of 1/2.

## Slice 2: the plumbing (pure, host-tested, no kernel)

- **The reset reason.** `RM_RTT_RESET_SCALE` (16, named `scale`), which `RM_RTT_RESET_ALL` (now 31)
  takes, in `header/rm_rttemporal.h`. Each history (`rm_rtt_hist_t`) records `div`, the divisor of its
  last take. `rm_rtt_hist_take_at(h, t, div)` adds `scale` when that differs (`0` there is no take yet,
  and adds nothing); `rm_rtt_hist_take` is the take at divisor 1, so the shadows' and the AO's takes
  are unchanged. `rm_rtt_hist_reset` keeps the divisor, as `rm_rtt_state_reset` keeps `seq`: an
  effect's histories are made again at its new traced size when its scale changes, which resets them
  (`first`), and the kept divisor still says why (`first scale`). TASK109's line names `scale` among
  the temporal reasons (`rm_metalfx.c`); the state's own step never sets it, so that line is as before.
- **Reprojection at the effect size.** `mtl_rtup_reproject` in `common/mtl_rtup_math.h`: the
  render-size motion vector, read at the traced pixel's source pixel, over the divisor, through
  `mtl_rtt_reproject` at the traced size. In TASK157's layout traced pixel `x`'s centre is at
  `x d + o + 1/2` render pixels, so the traced pixel nearest the point is the one holding
  `x + 1/2 + mx / d`. At divisor 1 it is `mtl_rtt_reproject` itself. `common/mtl_rtt_math.h` is not
  edited (FIX212 rewrites its guards on another branch).
- **The formatters,** in `rm_rttemporal.c`: `rm_rtt_scale_line_format` writes
  `ref_metal: r_rt_stats: reset: <effect> scale`, and `rm_rtt_traced_format` writes the stats line's
  part `, traced at WxH, F upsample fallbacks`.
- **The tests.**
  - `tests/metal/rm_rttemporal_test.c` (`test_scale`): the bit is distinct and in `ALL`; the first
    take resets for `first` alone; the same divisor keeps; a change resets that history alone; the
    plain take is divisor 1; a history made again keeps its divisor (`first scale`); the view's own
    reasons are kept beside it; a divisor of 0 is 1; both lines. `test_hist`'s histories are now
    zeroed as the back ends' static ones are, since the take reads the divisor.
  - `tests/metal/rm_rtup_test.c` (`test_reproject`): divisor 1 against `mtl_rtt_reproject`, divisors
    2 and 3, a move inside the traced pixel, off the traced size, a vector that is not finite.
  - `tests/metal/rm_fx_test.c`: the reset line with `scale`, and the unknown bit moved to `0x20`.
- **The mutants** in `tests/check-ref-metal.sh`, each compiled and refused: the scale reason dropped
  (`scalereason`), the divisor not compared (`scalecmp`), the divisor forgotten when a history is made
  again (`scalekeep`), and in the upsample segment the motion not divided (`motion`, the ninth header
  mutant). The upsample's Metal probe calls `mtl_rtup_reproject`.

### Host checks (no lock)

| Check | Result |
|---|---|
| the tests at the red commit (`6a1f206e`) | do not compile: `RM_RTT_RESET_SCALE`, `rm_rtt_hist_take_at` and `mtl_rtup_reproject` undeclared |
| `rm_rttemporal_test`, `rm_rtup_test`, `rm_fx_test` with the change | 148, 79 and 394 checks, 0 failed |
| the temporal segment of `tests/check-ref-metal.sh` | 14 rule mutants (11 before), 7 kernel pins with 10 kernel mutants and 4 back-end mutants refused |
| the upsample segment | `upsample 79 checks, 0 failed, 9 rule mutants refused, compiled` |

## Slice 3: the sun at `r_rt_sun_scale`

- **The cvar.** `r_rt_sun_scale`, archived, default `1` (`rm_rtsunscene.c`), taken through
  `mtl_rtup_div` as the view's divisor (`rm_rtsun_view_t`'s last word, `pad` before, now `div`; the
  stock kernels never read it). Its `doc/040_cvarlist.md` entry names `r_rt_sunscale`, the strength,
  and that entry points back. `rtg_cvars` lists it as a setting.
- **The kernels,** in `rm_rtsun.metal` behind `RM_RTSUN_LO_TU`, compiled apart by
  `rm_rtsun_lo.metal` (TASK59 slice 1b's arrangement for the shadows' temporal kernel), so the stock
  unit holds no new caller of the helpers `rm_rtsun_trace` calls:
  - `rm_rtsun_trace_lo`, at the traced size: `rm_rtsun_trace`'s class and ray for each traced pixel,
    from its source pixel's G-buffer sample and lightmap. It writes the class (`R8Uint`) and the
    visibility (`R32Float`), and the counts of the traced pixels.
  - `rm_rtsun_temporal_lo` and `rm_rtsun_denoise_lo`, at the traced size, while `r_rt_temporal` is
    above 0: `rm_rtsun_temporal`'s accumulation with `mtl_rtup_reproject`, and RT7's 5 x 5 with the
    normals read at the source pixels. They write the visibility again, never the light.
  - `rm_rtsun_up`, at the render size. Each lightmapped world pixel with a depth and a normal (a pixel
    without one is skipped before the rule, so the fallbacks are not inflated) takes its own floor
    gate and its own `max(0, N.L)`. Below the floor and facing the sun, its visibility comes from the
    four traced pixels about it under `mtl_rtup_weights`. A tap's sample counts only where the tap was
    traced (lit or shadowed). Then the class and the light are written as `rm_rtsun_trace` writes
    them, and the fallbacks are counted.
- **The neutral value.** Where no traced pixel about a pixel has a sample, the sun's visibility is
  `0`: no term, the frame of the effect off. The plan's "visibility 1" is the dlight shadows' neutral
  (the stock dlight); for the sun, which only adds light, 1 would add the sun where no ray looked.
- **At scale 1** `rmb_rtsun_trace` dispatches `rm_rtsun_trace` and the stock temporal and denoise
  kernels exactly as before, in an `else` branch. Compiled with `xcrun metal`, main's
  `rm_rtsun.metal` and this branch's give AIR whose disassembly differs only in the module's path and
  in the view's last field's name (`pad`, `div`): the stock code is the same.
- **The back end** (`rmb_rtsun.m`):
  - `rmb_rtsun_prepare(w, h, div)` makes the traced size's two textures and the four pipelines above
    divisor 1, a pipeline failure latched until the pass is closed, and releases the textures at 1.
  - `rmb_rtsun_encode_lo` encodes the temporal pass, the denoise and the upsample behind barriers. It
    takes the view into the history with `rm_rtt_hist_take_at(&thist, &tview, view.div)`. The stock
    path's take is the plain one (divisor 1), so a change either way resets the history for its scale.
  - The histories are made at the traced size: the scene passes `mtl_rtup_size` of the render size to
    `rmb_rtsun_temporal`.
  - The counts take a sixth word, the fallbacks; each stats slot keeps its divisor and traced size.
- **Lines.**
  - Under `r_rt_stats 2` the sun's line ends `, traced at WxH, F upsample fallbacks` below divisor 1,
    and is unchanged at 1.
  - Under `r_rt_stats 1`, a view whose take set `scale` prints `ref_metal: r_rt_stats: reset: sun
    scale` once, at any `r_metalfx`.
  - The model view takes no sun (`RDF_NOWORLDMODEL`), and `fov` is not the divisor, so neither prints
    it.
- **The Makefile** compiles `rm_rtsun_lo.air` into `ref_metal.metallib` and lists `rm_rtsun_lo.metal`
  and `common/mtl_rtup_math.h` as its prerequisites.
- **The check** (`tests/check-ref-metal.sh`, segment 7.86):
  - It checks the unit's arrangement, and that the stock kernels sit alone before the `#else` and the
    four after it.
  - It pins twelve kernel calls with thirteen mutants, each of which must be refused and must compile with
    `xcrun metal`. The mutants: the sample at the traced pixel itself (twice), the motion not divided,
    its sign flipped, the reset not passed, the weights at divisor 1, a pixel with no normal not
    skipped, every tap taken as traced, the neutral value 1, the fallback never counted, and the
    visibility 1 in the light.
  - It pins the back end's take at the divisor (mutant: the plain take) and the scene's cvar and
    divisor (mutants: the default 0.5, the divisor left at 1).
  - Segment 20 gains five kernel-call pairs of the new kernels.
- **The scenario** `rt-sun-half`: `rt-sun` plus `r_rt_sun_scale 0.5`, in the scenarios README (both
  tables) and `doc/100_renderpins.md`.

### Host checks (no lock)

| Check | Result |
|---|---|
| `rm_rtsun_test` at the red commit (`3b4cb486`) | does not compile: `rm_rtsun_stats_t` has no `div`, `tw`, `th`, `fallbacks`, the view no `div` |
| `rm_rtsun_test` with the change | 716 checks, 0 failed |
| `xcrun metal -c` of `rm_rtsun.metal` and `rm_rtsun_lo.metal` | both compile, no warning |
| the stock unit's AIR against main's | the disassembly differs only in the module path and the field name `pad` / `div` |
| segment 7.86 | `sun at its scale 12 kernel pins with 13 kernel mutants compiled and refused, 4 back-end and scene mutants refused` |
| segments 5.5, 5.6, 7, 10, 11, 12, 13, 15, 20 | pass; 40 `r_rt_*` cvars classified; 34 kernel calls, 340 kernel-call mutants refused |
| `tests/check-cvar-docs.sh`, `tests/check-doc-links.sh`, `tests/check-runbook-index.sh`, `scripts/pins.sh check-content` | ok, each |

### The hold of slices 2 and 3 (M6, under the timing lock)

The orchestrator runs the hold script. The values below are filled in from its summary.

| Check | Result |
|---|---|
| base and branch | (from the hold) |
| `make ref_metal`, `make`, `make test` | (from the hold) |
| `tests/check-ref-metal.sh`, with the new segments' mutant counts | (from the hold) |
| the 30 stock scenarios outside `rtprimary-*` (the `rt-*` records at their defaults among them), byte-identical to main | (from the hold) |
| the 20 RT-primary records, byte-identical to main | (from the hold) |
| `rt-sun-half`, three runs identical, and different from `rt-sun` | (from the hold) |
| the `r_rt_stats 2` sun line at `r_rt_sun_scale 0.5`, with its fallback count | (from the hold) |
| the reset line on a change from 1 to 0.5 | (from the hold) |
