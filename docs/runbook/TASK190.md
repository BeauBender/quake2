# RT-primary's temporal accumulation of its light (TASK190, PM8 slice 1)

Card: TASK190 (#537). Plan: `docs/plans/presentation-modes.md`, row PM8 of section 7 and its sections 4.3
(the denoiser), 4.4 (the MetalFX budget), 4.5 (the cost) and 6 (how a mode stays reversible). Stacked on
TASK189 slice 2 (the bounce), with main merged in for TASK59's slices 1 and 1b (RT7, the temporal
accumulation), which this slice builds on. Slice 1 accumulates RT-primary's light over the views. The 5x5
filter and the water blur under RT-primary (slice 2, after TASK59's slices 2 and 2b), the scaler through
the overlay (slice 3) and the cost table (slice 4) are not in it.

## What existed before this slice

Traced in the code at `origin/task189s2` with main merged:

- **RT-primary's light.** `rm_rtdir_trace` (`rm_rtdir.metal`) sums every term of a traced world pixel (the
  map's lights, the dynamic lights, the emissive faces' own light, the sky light, the sun and the bounce)
  and writes it once to `light_out`, which is `t_light` (`rmb_rtdir.m`): RGBA16Unorm, clamped to [0, 1],
  with no albedo, so it is already the demodulated irradiance. Every pixel that is not a traced world pixel
  (not the G-buffer's world material, at the far plane, or with no normal written) takes the light the
  resolve would have read, bit for bit. `rmb_rtdir_fx_trace` puts `t_light` in `io->light`, and the resolve
  multiplies it by the albedo.
- **Nothing accumulated it.** The sky light and the bounce are one stochastic ray a pixel; the RT-primary
  scenarios fix their noise with `r_rt_direct_seed 1`, and unseeded the frames move frame to frame.
- **RT7** (TASK59, `rm_rttemporal.c`, `common/mtl_rtt_math.h`): one reset state for the views
  (`RM_RTT_ViewFor`, `rm_rtaoscene.c`, stepped once a world view at the first effect that asks), the
  rejection rule (the view's reset, off the screen, the depth, the material, the dynamic-light set, an empty
  history), the count and the exponential blend, and a per-effect record of the last view encoded
  (`rm_rtt_hist_take`). `r_rt_temporal` defaults to `0`. Under RT-primary the overlay reads `r_rt_shadows`,
  `r_rt_sun` and `r_rt_ao` as `0`, so none of RT7's three accumulations runs there.

## What it adds

- **The kernel.** `rm_rtp_temporal`, in a translation unit of its own, `rm_rtptemporal.metal`, linked into
  `ref_metal.metallib` after `rm_rtdir.metal`, which is not edited (TASK59 1b's lesson: a stock kernel whose
  helpers gained a second caller in its own unit was inlined differently and its frames moved). It reads
  the trace's light, the G-buffer's depth, normal, material and motion, the history and its key, and no
  lightmap. Its rule is `common/mtl_rtp_math.h` (new), which includes `mtl_rtt_math.h` unchanged.
- **The reject rules.** RT7's, by `mtl_rtt_reject`: the view's reset, the point off the screen in the
  history's frame (the motion vector, without the jitter), the distance from the eye beyond the eye's move
  plus 2 units plus 2 % of the distance, the material id, an empty history. **There is no light-set
  reject**: RT-primary's light sums every light, so the set of dynamic lights that reach a pixel is not one
  of its guides (`mtl_rtp_reject` passes `0, 0` for the two sets). A muzzle flash's light that comes and
  goes is blended like any other change of the light, at the history's weight; the plan's default is no
  neighbourhood clamp unless the frames show trails. Beside RT7's reasons, a history whose red, green or
  blue is not a finite value in [0, 1] is rejected (`MTL_RTP_REJ_BAD`), and the trace's light is clamped to
  [0, 1] (a NaN to 0) before the blend, so no NaN reaches the output or the history.
- **The history.** Two of each, read and written in turn: the light with its count in alpha, RGBA16Float
  (every count up to `MTL_RTT_COUNT_MAX`, 255, is exact in a half), and the key, RG32Uint (the distance
  from the eye's bits and the material id). Red, green and blue are each blended with their own history by
  `mtl_rtt_blend` (the history weighted `r_rt_temporal`, at most `count / (count + 1)`), the trace's alpha
  kept. A pixel that is not a traced world pixel is copied bit for bit to the output, and its history holds
  no sample (count 0), so a world pixel that reprojects onto it later starts again (empty, and the
  material). The output, `t_acc`, is RGBA16Unorm, `t_light`'s format, and becomes `io->light`.
- **The dispatch.** From `rmb_rtdir_fx_trace`, after the trace, in an encoder of its own
  (`rmb_rtdir_accumulate`), with its own timestamp pair (a counter sample buffer of its own, made with the
  pipeline), reported on a line of its own under `r_rt_stats 2`
  (`ref_metal: rt direct temporal: frame N: X ms`). The view goes into the history through
  `rm_rtt_hist_take`, only where the dispatch is encoded, after every early return.
- **The reset state.** `RM_RTDir_View` (`rm_rtdirscene.c`) steps RT7's shared state through
  `RM_RTT_ViewFor` after handing the view over, so a map load, a `vid_restart`, a cut or a teleport, a
  change of the render size or of the view's rectangle, and a frame that accumulated nothing reset it as
  they reset RT7's. A change of `r_rt_direct_show` also resets it (the light then holds another term).
- **`r_rt_temporal`.** The user's value: the presentation's overlay has no row for it, and the scene reads
  the cvar itself. At `0` (the default) `RM_RTT_ViewFor` returns none, `rmb_rtdir_temporal` releases the
  histories and hands no view over, `rmb_rtdir_accumulate` returns nil before encoding anything, and the
  resolve reads `t_light` as before: the frames are the trace's, bit for bit.
- **Allocation.** The pipeline, the timestamps, the histories and `t_acc` are made only while the
  direct-light pass is open (RT-primary) and `r_rt_temporal` is above 0, at the first view that
  accumulates; made again at a new render size (the old ones released first); released at `0`, when the
  pass closes (leaving RT-primary, a `vid_restart`). The line is
  `ref_metal: rt temporal direct WxH: N bytes allocated`. A pipeline that cannot be made is latched until
  the pass closes, a size whose textures cannot be made until another size.
- **The development view.** `r_rt_direct_show`'s terms are drawn from what the resolve reads: accumulated
  where the view accumulated, the trace's otherwise. RT3 to RT5's show views stay the trace's own.
- **The pins.** Every `scripts/pins/scenarios/rtprimary-*.cfg` sets `r_rt_temporal 0`, so each RT-primary
  record stays byte-identical to TASK189's when TASK59's slice 2 makes `0.9` the default. The line changes
  each scenario's id (`pins.sh`'s scenario hash is of the file); the frames are compared by their hashes.

## Tests

- **Host:** `tests/metal/rm_rtptemporal_test.c` (new): the world rule, the blend of each channel with its
  own history, a young history's mean, `r_rt_temporal` 0, the reset, off, depth and material rejects (and no
  light-set reject), the empty history, the count to its saturation at 255, NaN in the trace's light and NaN,
  infinite, negative or above-1 values in the history, and the pass-through of non-world pixels bit for bit:
  59 checks.
- **Header mutants** (`tests/check-ref-metal.sh`), each of which must compile and fail it: the reset
  ignored, the count frozen, green blended with red's history, the bad history kept, a non-world pixel not
  passed through. Red then green: on the real header 59 checks, 0 failed; the mutants fail 4, 13, 2, 5 and 2
  checks.
- **Kernel text pins** with seven mutants, each of which, like the kernel, must compile with `xcrun metal`
  where it is there: the reprojection with the motion vector (mutants: its sign flipped, no motion read),
  the view's reset and the material reaching the rule (mutants: the reset not passed, a material compared
  with itself), no lightmap read (mutant: `lm` read into the output), and only the light, the history and
  its key written, once each (mutants: a fourth output written, the history not written). The check
  requires the counts: 5 header mutants, 4 pins, 7 kernel mutants.
- **The back end:** RT7's `rtt_place` holds `rmb_rtdir_accumulate`'s take after every early return and
  before its dispatch; `rmb_rtdir_fx_trace` reads the accumulated light after the trace; the scene steps
  `RM_RTT_ViewFor`; the Makefile compiles and links the new unit.

## The hold

Written as the scratchpad's `task190s1-hold.sh` and run by the orchestrator; the numbers below are its
`task190s1-summary.log`'s.

- Builds, `make test` and the checks: @HOLD_CHECKS@.
- The stock pins at `r_presentation 0`, against main: @STOCK_PINS@.
- Every RT-primary record (at `r_rt_temporal 0` by its scenario) against `task189s2`: @RTP_PINS@.
- Scratch scenarios without `r_rt_direct_seed`, three runs each:
  - `rtprimary-world` at `0.9`: @WORLD_T9@; at `0`: @WORLD_T0@.
  - `rtprimary-bounce` at `0.9`: @BOUNCE_T9@; at `0`: @BOUNCE_T0@.
  - At `0.9` against `0`: @T9_VS_T0@.
- The history's allocation line: @ALLOC_LINE@; the accumulation's GPU time: @TEMPORAL_MS@.
- Frames for the owner (base2, `demo1.dm2` captures 21 and 22 outdoors and 12 indoors, at `0` and `0.9`):
  @OWNER_FRAMES@.

## Left for the next slices

- Slice 2: the 5x5 filter on the irradiance (`rm_rtp_denoise`, after TASK59's slice 2), scaled by the count,
  and the water blur under RT-primary (after TASK59's slice 2b).
- Slice 3: the scaler through the overlay (`r_metalfx 2` in `pres_rtprimary`).
- Slice 4: the cost table of section 4.5, measured in the owner's quiet window.
