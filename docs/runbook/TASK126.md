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
  `1`, the values of `r_rt_ao_scale` (1, 1/2, 1/3; a bad value is 1). Each lands with the slice that
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

### Host checks (no lock)

| Check | Result |
|---|---|
| the upsample segment of `tests/check-ref-metal.sh`, at the red commit (`87561daa`, the test without the header) | `common/mtl_rtup_math.h is missing`; `refused 0 of its 7 header mutants` |
| the same segment with the header | `upsample 67 checks, 0 failed, 8 rule mutants refused, compiled` |
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
