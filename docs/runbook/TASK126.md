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

The orchestrator ran the hold script. The values are its summary's; it ended `hold done, 0 failed`.

| Check | Result |
|---|---|
| base and branch | branch `ca41f718f58b`, base main `9a81cceeef7d`, an ancestor of the branch; one engine file changed, `common/mtl_rtup_math.h` |
| `make ref_metal`, `make`, `make test` | exit 0, each; `yq2test: 591 tests, 0 failed` |
| `tests/check-ref-metal.sh` | exit 0; `upsample 72 checks, 0 failed, 8 rule mutants refused, compiled` |
| the 30 scenarios outside `rtprimary-*`, byte-identical to main | 30 of 30 (the `rt-*`: 8 of 8) |
| every `rtprimary-*` record, byte-identical to main | 20 of 20 |

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
    visibility 1 in the light, and taps 1 and 2 swapped in the upsample's x and y tap lines (once
    each).
  - It pins the back end's take at the divisor (mutant: the plain take) and the scene's cvar,
    divisor and traced-size history call (mutants: the default 0.5, the divisor left at 1, the render
    width).
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

The orchestrator ran the hold script. The values are its summary's, and the lit rays are a probe's
that counted the `r_rt_stats 2` sun lines of both scenarios.

| Check | Result |
|---|---|
| base and branch | branch `94001f0c92d9`, base main `bd82ccb8f876`, not an ancestor of the branch |
| `make ref_metal`, `make`, `make test` | exit 0, each; `yq2test: 591 tests, 0 failed` |
| `tests/check-ref-metal.sh`, with the new segments' mutant counts | exit 0; `temporal 148 checks, 0 failed, 14 rule mutants, 7 kernel pins with 10 kernel mutants and 4 back-end mutants refused`; `upsample 79 checks, 0 failed, 9 rule mutants refused, compiled`; `sun at its scale 10 kernel pins with 11 kernel mutants compiled and refused, 3 back-end and scene mutants refused`; `34 kernel calls, 340 kernel-call mutants refused` |
| the 30 stock scenarios outside `rtprimary-*` (the `rt-*` records at their defaults among them), byte-identical to main | 30 of 30 (the `rt-*`: 8 of 8) |
| the 20 RT-primary records, byte-identical to main | 20 of 20 |
| `rt-sun-half`, three runs identical, and different from `rt-sun` | three runs identical, `1bbb1b9b59f6`; `rt-sun` is `b7e0212dfda8`, and 1 of 23 frames differs |
| the `r_rt_stats 2` sun line at `r_rt_sun_scale 0.5`, with its fallback count | 688 sun lines, 687 with the traced part; the first, `frame 18: 307200 pixels, 76800 world, 17120 bright, 12869 facing away, 46811 rays, 0 lit, 0.224 ms, traced at 320x240, 933 upsample fallbacks`; over the lines, 354398 fallbacks of 211046400 drawn pixels (0.168 %) |
| the rays that reached the sky, over the 688 lines (the probe) | `rt-sun`: 197639 lit of 107904687 rays, 37 lines with a lit ray; `rt-sun-half`: 49497 lit of 26957759 rays, 36 lines |
| the reset line on a change from 1 to 0.5 | one `reset: sun scale` line on each change (1 to 0.5, 0.5 to 0.33, 0.33 to 1), three in all; none at the three steady views, the `fov` change or the player-setup model view; the run exited 0 under Metal's validation layer |

- **The summary's last line.** It is `hold done, 1 failed`. The one failure is the hold script's own
  marker check, which asked for at least 12 events where the run makes 11. Each of the 11 is `ok`
  or unjudged above, and the summary says `live: 0 events wrong`.
- **Its `validation or pipeline errors: 1`.** That is the validation layer's banner line,
  `Metal API Validation Enabled`, which the script counted. No assertion failed and no pipeline was
  refused.
- **The sun's segment.** The hold ran the tip before the tap-order and traced-size pins. The
  host-check table above has the later tip's 12 pins, 13 kernel mutants and 4 back-end and scene
  mutants.

## Slice 4: the dlight shadows at `r_rt_shadows_scale`

Slice 4 follows slice 3's arrangement: a separate low-scale unit, a stock path that is the parent's
at scale 1, kernel pins with mutants, and a skip before the rule.

- **The cvar.** `r_rt_shadows_scale`, archived, default `1` (`rm_rtshadowscene.c`), taken through
  `mtl_rtup_div`. `rtg_cvars` lists it as a setting, and `doc/040_cvarlist.md` has its entry.
- **Where the divisor travels.** `rm_rts_view_t` has no word to spare and keeps its 128 bytes. The
  scene hands the divisor to `rmb_rts_prepare(w, h, div)`, and the back end binds it to the new
  kernels as a 16-byte block of its own (`rmb_rts_lo_t`, buffer 13). Both sizes have a
  `_Static_assert`. The stock kernels bind nothing new.
- **The rule's extension** (host-tested):
  - `mtl_rtup_light` in `common/mtl_rtup_math.h`: one light's visibility under the pixel's weights.
    A tap weighs for a light only when its packed set holds that light, and the holders' weights are
    taken over their own sum. A light that no weighted tap traced takes visibility 1, the stock
    dlight, with `fallback` set.
  - `mtl_rts_holds` in `common/mtl_rts_math.h`: whether packed indices hold a light. The empty
    slots' index names none.
- **The kernels,** in `rm_rtshadow.metal` behind `RM_RTS_LO_TU`, inside its `RM_RTS_TEMPORAL_TU`
  part, compiled apart by `rm_rtshadow_lo.metal`. That unit holds the helpers and `rm_rts_shade`,
  without `rm_rts_trace`, the resolve and `rm_rts_temporal`. So neither stock unit holds a new
  caller of its helpers.
  - `rm_rts_trace_lo`, at the traced size: `rm_rts_shade` for each traced pixel, from its source
    pixel's G-buffer sample. It writes the packed words alone (RT3's `t_out` layout, `RG32Uint`)
    and the counts.
  - `rm_rts_temporal_lo`, in its place while `r_rt_temporal` is above 0: RT3's fused accumulation
    with `mtl_rtup_reproject`, the motion read at the source pixel. It rejects on distance, material
    and the light set, as the stock one does. The histories are at the traced size.
  - `rm_rts_lit`: `rm_rts_shade` without its shadow rays and their start, copied line for line. The
    check pins the two alike.
  - `rm_rts_up`, at the render size. It builds `t_out` and `t_term` as the stock trace writes them,
    so `rm_fragment_resolve_rts` is unchanged.
- **The drawn pixel's own light set.** It is the stock trace's at that pixel: `rm_rts_lit` finds
  the pixel's surface as `rm_rts_trace` does (the G-buffer's surface attachment, or the eye ray),
  every light's add there, and the four lights that add the most. Only each of those lights'
  visibility comes from the taps. A light past the four is added unshadowed, as in the stock trace.
- **The fallback counter counts pixels.** A drawn pixel is counted once when the weights fell back
  to the tap nearest in depth, or when a light of its set took visibility 1 because no weighted tap
  traced it. The line's `F upsample fallbacks` is that count of drawn pixels.
- **The skip before the rule.** A pixel with no normal (nothing lit there), or with no light in
  its set, reads no tap and counts nothing.
- **A tap's sample.** A tap holds a sample where `rm_rts_shade` traced its source pixel: a world
  pixel with a depth and a normal.
- **RT7.** The stock shadows have no denoise, and slice 4 adds none. RT7 at the traced size is the
  fused accumulation alone.
- **At scale 1** the back end dispatches `rm_rts_trace`, or `rm_rts_temporal`, and the resolve as
  before, in `else` branches. Compiled with `xcrun metal`, the parent's `rm_rtshadow.metal` and
  `rm_rtshadow_temporal.metal` and this branch's give AIR whose disassembly differs only in the
  module's path. No field is renamed.
- **The back end** (`rmb_rtshadow.m`):
  - `rmb_rts_prepare(w, h, div)` makes the traced size's texture and the three pipelines above
    divisor 1, a pipeline failure latched until the pass is closed. At 1 it releases the texture.
  - `rmb_rts_trace` takes the view into the history with `rm_rtt_hist_take_at(&thist, &tview,
    prep_div)` below the render size. The stock path's take stays the plain one, so a change either
    way resets the histories for the scale.
  - The upsample is dispatched after the trace, behind a texture barrier, in the same encoder.
  - The counts take a fifth word, the fallbacks. Each stats slot keeps its divisor and traced size.
  - The histories are made at the traced size: the scene passes `mtl_rtup_size` of the render size
    to `rmb_rts_temporal`.
- **The counts below divisor 1.** `in range`, `traced` and `shadowed` are of the traced pixels.
  `rays` is the traced pixels' rays plus the eye rays the upsample took for a pixel whose surface
  the G-buffer does not name.
- **Lines.**
  - Under `r_rt_stats 2` the shadows' line ends `, traced at WxH, F upsample fallbacks` below
    divisor 1, and is unchanged at 1. `rm_rtshadow.c` writes that part itself, in
    `rm_rtt_traced_format`'s words, because many host tests link it without `rm_rttemporal.c`.
  - Under `r_rt_stats 1`, a view whose take set `scale` prints
    `ref_metal: r_rt_stats: reset: shadows scale` once, at any `r_metalfx`.
  - The model view traces no shadows (`RDF_NOWORLDMODEL`), and `fov` is not the divisor, so neither
    prints it.
  - The take runs only on a view that accumulates (`r_rt_temporal` above 0) with a dynamic light in
    it. So the line follows the first such view after the change.
- **The Makefile** compiles `rm_rtshadow_lo.air` into `ref_metal.metallib` and lists
  `rm_rtshadow_lo.metal` as a prerequisite.
- **The check** (`tests/check-ref-metal.sh`, segment 7.87):
  - It checks the unit's arrangement, and that `rm_rts_temporal` sits alone before the `#else` and
    the three kernels and `rm_rts_lit` after it.
  - It pins `rm_rts_lit` to `rm_rts_shade`'s code, with two mutants: an add halved, and a pixel
    without light to add no longer skipped.
  - It pins nineteen kernel lines with twenty-one mutants. Each must be refused and must compile
    with `xcrun metal`:
    - the sample at the traced pixel itself (twice), and the trace run at the traced pixel (twice);
    - the motion not divided, its sign flipped, the reset not passed, the light set not passed, and
      the distance taken at the traced pixel;
    - the upsample calling `rm_rts_shade`, which traces;
    - a pixel with no normal or no light not skipped;
    - taps 1 and 2 swapped in the x and the y tap lines (once each);
    - every tap taken as traced, and the weights at divisor 1;
    - the light-set test dropped, so every tap weighs for every light;
    - taps 2 and 3's visibilities swapped, and the visibility 1 in the slot;
    - neither fallback counted (once each), and the term made without the visibility.
  - It pins the back end with three mutants (the plain take, the accumulation tested at the render
    size, the barrier dropped) and the scene with three (the default 0.5, the divisor left at 1,
    the render width).
  - The upsample segment (7.85) gains four header mutants of `mtl_rtup_light`: the set test
    dropped, the fallback visibility 0, the fallback not flagged, and the weights not taken over
    their sum.
  - Segment 20 gains nine kernel-call pairs of the new kernels.
- **The scenario** `rt-shadows-half`: `rt-shadows` plus `r_rt_shadows_scale 0.5`, in the scenarios
  README (both tables) and `doc/100_renderpins.md`. It declares no band of its own, so its
  companion is nominal. `dlights` gets no `-half` variant: it runs with `r_rt_shadows 0`, where the
  scale does nothing, and the hold holds it byte-identical to the parent.

### What slice 4 leaves

- The `r_rt_stats 1` parts of the plan's slice 4 are cut, and move to slice 6 for the sun and the
  shadows, beside AO's count: the map-summary share of the fallback and the line
  `ref_metal: r_rt_stats: upsample: <effect> WxH of WxH, F fallbacks`. The per-frame `r_rt_stats 2`
  shadows line carries `traced at WxH, F upsample fallbacks` below scale 1 in this slice. The rule,
  the cvar, the trace, RT7 at the traced size and the upsample are all here.
- The counts buffer: `RMB_RTS_COUNTS` went from 4 to 5 words per slot, so the buffer's stride (the
  offsets 0, 20 and 40) changes at scale 1 too. The stock kernels still write words 0 to 3, the
  buffer is cleared each frame and the offsets stay 4-byte aligned, and the stock AIR is identical to
  the parent's. No frame changes.
- A scale change made while `r_rt_shadows` is off prints `reset: shadows scale` on the first
  accumulating view after the shadows are turned back on: the histories keep their divisor across a
  pass close, and only the next accumulation compares it.
- The low path has never run on a GPU on this branch. The host checks compile it and pin its text.
  The hold below is its first run.

### Host checks (no lock)

| Check | Result |
|---|---|
| `rm_rtup_test` and `rm_rtshadow_test` at the red commit (`f30bd472`) | do not compile: `mtl_rtup_light_t`, `mtl_rtup_light` and `mtl_rts_holds` undeclared, `rm_rts_stats_t` has no `div`, `tw`, `th`, `fallbacks` |
| `rm_rtup_test`, `rm_rtshadow_test` with the change | 94 and 4395 checks, 0 failed |
| `xcrun metal -c` of `rm_rtshadow.metal`, `rm_rtshadow_temporal.metal` and `rm_rtshadow_lo.metal` | all three compile, no warning |
| the two stock units' AIR against the parent's (`e96c43b8`) | each disassembly differs only in the module path |
| `cc -fsyntax-only -Wall -Wextra` of `rmb_rtshadow.m` and `rm_rtshadowscene.c` | no diagnostic |
| segment 7.87 | `shadows at their scale 20 kernel pins with 23 kernel mutants compiled and refused, 6 back-end and scene mutants refused` |
| segment 7.85 | `upsample 94 checks, 0 failed, 15 rule mutants refused, compiled` (13 upsample header mutants and 2 of `mtl_rts_holds`) |
| segment 5.5 | `temporal 148 checks, 0 failed, 14 rule mutants, 7 kernel pins with 10 kernel mutants and 4 back-end mutants refused, 3 stock-trace pins with 6 mutants refused` |
| segments 4, 7.86, 10, 10.2, 11, 12, 15, 20 | pass; 41 `r_rt_*` cvars classified; 43 kernel calls, 430 kernel-call mutants refused |
| `tests/check-cvar-docs.sh`, `tests/check-doc-links.sh`, `tests/check-runbook-index.sh`, `tests/check-scenario-parsers.sh`, `scripts/pins.sh check-content` | ok, each |

`make`, `make test` and the whole of `tests/check-ref-metal.sh` were not run here. The hold runs
them.

### Review round 1

- **Correctness I1, the bench adapter dropped the shadows line below scale 1.** Fixed. The parser in
  `scripts/bench/adapters/quake2.sh` keeps the 18-field line and the 24-field line that ends in
  `traced at WxH, F upsample fallbacks`, the ms field being field 17 in both. The metrics fixture in
  `tests/check-bench-quake2.sh` has a divisor-2 line (the text `rm_rtshadow_test` expects), and
  `rts_ms` counts 4 (median 1.5) and `rts_traced_ms` 3, and 3 and 3 after frame 3. It failed at
  `2256773b`. The check has a mutant list: a mutant that goes back to `n == 18` is refused.
- **Spec I1, the `r_rt_stats 1` fallback lines.** Accepted as deferred to slice 6, as above.
- **Note, `mtl_rtup_light` and a NaN weight.** A NaN-weight case is added to `rm_rtup_test`. The
  `ws <= 0.0f` mutant of `!(ws > 0.0f)` is equivalent, so it is not in the list: a weight is added
  only when it is above 0, so the sum is never a NaN and the two forms never differ.
- **Note, `mtl_rts_holds`.** Two header mutants, the `MTL_RTS_NO_LIGHT` guard dropped and the slot loop
  reading one slot, are refused by `rm_rtshadow_test` (`test_holds`). They run in segment 7.85, whose
  mutant count is now 15.
- **Notes N4 and N5.** The counts buffer's stride and the `reset: shadows scale` print are stated
  above.
- **Note N3, `shlo_lit`'s cut window.** Fixed. The cut ends at the shadow loop's own closing brace
  instead of at `nl = nl;`, so a line added between them is part of what `rm_rts_lit` must hold. Its
  two mutants are still refused (segment 7.87 is unchanged: 20, 23 and 6).
- **Notes N1 and N2.** Accepted: a host cannot run the kernels; the hold does.

### The hold of slice 4 (M6, under the timing lock)

The orchestrator runs the hold script. The values below are copied from its summary. The base
is the parent slice, `task126s23`.

| Check | Result |
|---|---|
| base and branch | The hold's commit is `2256773b`, 0 files changed, based on `task126s23` (`e96c43b8`), an ancestor of it. The base builds: `make` and `make ref_metal` exit 0. |
| `make ref_metal`, `make`, `make test` | `make ref_metal` exit 0, `make` exit 0, `make test` exit 0 (591 tests, 0 failed). Build warnings: 0 for `ref_metal`, 2 for `make`. |
| `tests/check-ref-metal.sh`, with segment 7.87's mutant counts | ok, 13162 checks, 0 failed. Shadows 4395 checks, 0 failed. Temporal 148 checks, 14 rule mutants, 7 kernel pins with 10 kernel mutants and 4 back-end mutants refused. Upsample 92 checks, 13 rule mutants refused. Sun at its scale: 12 kernel pins, 13 kernel mutants and 4 back-end and scene mutants refused. Shadows at their scale (segment 7.87): 20 kernel pins, 23 kernel mutants and 6 back-end and scene mutants refused. 43 kernel calls, 430 kernel-call mutants refused. The summary's line for the whole run is cut off after the sun's check count, so the sun's own totals are not recorded here. |
| every stock scenario outside `rtprimary-*` (`dlights`, `rt-shadows` and `rt-sun-half` among them), byte-identical to the parent | 31 scenarios on the base and 31 on the branch, 0 failed. 31 of 31 outside `rtprimary-*` and `rt-shadows-half` byte-identical to the parent (`rt-*`: 9 of 9). `dlights` 5552278ed108, `rt-shadows-entities` 04e115786187, `rt-shadows` 2b8d11a25392 and `rt-sun-half` 1bbb1b9b59f6, all identical. |
| the 20 RT-primary records, byte-identical to the parent | 20 of 20 records byte-identical to the parent's, each 23 frames unless noted: `rtprimary-world` c2d7b7d94e6c, `-direct` 8975350df42b, `-vis` 469ce42fec8c, `-noshadow` b3d10fafdc08, `-sky` 9acbe75f592b, `-sun` 6f6c37b9a084, `-bounce` e2a45f031fcc, `-metalfx2` b03c73ee59d1, `-denoise` 371419d919dd, `-overlayfx` 77f3485fe88f, `-styles` a254ac92e157 (89 frames), `-pool` e73ffa9aea7d (4 frames), `-denoise-pool` b5cc3f8dd8bf (4 frames), `-dlights` 6d01f9bb9bca, `-dlights-d2` 8f8b43a1f6ee (89 frames), `-models-off` c5ec8f241450, `-models` 93c5ef890067, `-models-vis` a5cd3e3316b9, `-weapon` 7a061e1228e1, `-weapon-gunshadow` 7a061e1228e1. |
| `rt-shadows-half`, three runs identical, and different from `rt-shadows` | Three runs identical, 23 frames, 428dde119d31. Against `rt-shadows` (2b8d11a25392) they differ, 4 of 23 frames. The engine reports the half-size buffers: 640x480 traced at 320x240, 671744 bytes, and 640x480, 4947968 bytes. |
| the `r_rt_stats 2` shadows line at `r_rt_shadows_scale 0.5`, with its fallback count | At scale 0.5: 329 shadows lines, 329 with the traced part, 129 with a shadowed pixel, 299 with a ray. Totals: 101068800 pixels, 8056260 in range, 6071183 traced, 66387 shadowed, 6410378 rays. Fallbacks: 14181 of 101068800 drawn pixels (0.014 %). The line with the most shadowed pixels, frame 227: 307200 pixels, 18651 in range, 13506 traced, 5665 shadowed, 13506 rays, 0.477 ms, traced at 320x240, 597 upsample fallbacks. For comparison, `rt-shadows` at full scale: 329 lines, 0 with the traced part, 140 with a shadowed pixel, 299 with a ray, 32296034 in range, 24357171 traced, 265302 shadowed; its frame 227 line is 74261 in range, 53962 traced, 22521 shadowed, 0.324 ms. |
| the shadows lines with a shadowed pixel, at `r_rt_shadows_scale 0.5` | 129 of 329 lines at scale 0.5 (140 of 329 at full scale). |
| the reset line on a change from 1 to 0.5 | The live run exit 0, 0 validation or pipeline errors, 0 events wrong. The reset line appears on the change to `half` (1 line, one wanted), on `third` (1) and on `full` (1), and on no other event. Allocations seen: 1280x720 traced at 427x240 (933888 bytes), traced at 640x360 (1982464 bytes), and 1280x720 (15204352 bytes). The hold ended with 0 untracked files left and 0 failed. |

The round 1 fixes after the hold (`769dc80b`, `ceae532a`, `f7709471`) change only the bench adapter, the tests, `tests/check-ref-metal.sh` and `docs/runbook/TASK126.md`, not the engine, which is why the hold was not run again.
