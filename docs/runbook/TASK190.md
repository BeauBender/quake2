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
  (`rm_rtt_hist_take`). `r_rt_temporal` defaulted to `0` (then). Under RT-primary the overlay reads `r_rt_shadows`,
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
  the cvar itself. At `0` (not the default, which is `0.9` since TASK59's slice 2) `RM_RTT_ViewFor` returns none, `rmb_rtdir_temporal` releases the
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
  record stays byte-identical to TASK189's: TASK59's slice 2 has made `0.9` the default, so RT-primary's
  accumulation is on by default under the Ray traced presentation. The line changes
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

- Builds, `make test` and the checks: the hold ran at `4c12bcfe94d2`, on main `f10aa5bc3abe` and
  `task189s2` `0147ff9a412a`, both merged; every build and every check exited 0. `make test`: 591 tests,
  0 failed. `check-ref-metal`: ok, with RT-primary's temporal pass at 59 checks, 0 failed, 5 rule mutants,
  and 4 kernel pins with 7 kernel mutants refused and compiled. The cvar-docs, runbook-index, doc-links,
  video-menu, pin-chars and scenario-parsers checks: ok.
- The stock pins at `r_presentation 0`, against main: 29 of 29 stock scenarios byte-identical to the base
  (29 scenarios on each side, 0 failed).
- Every RT-primary record (at `r_rt_temporal 0` by its scenario) against `task189s2`: 12 of 12 records
  byte-identical to `task189s2`'s.
- Scratch scenarios without `r_rt_direct_seed`, three runs each:
  - `rtprimary-world` at `0.9`: 23 frames, `2f6e7791f25d` in all three runs, 0 of 23 frames differ between
    run 1 and 2 and between run 1 and 3; at `0`: 23 frames, `96a043d6d138` in all three runs, 0 of 23 frames
    differ between the runs.
  - `rtprimary-bounce` at `0.9`: 23 frames, `6629d6d589f3` in all three runs, 0 of 23 frames differ between
    the runs; at `0`: 23 frames, `a94e7ce56a82` in all three runs, 0 of 23 frames differ between the runs.
  - At `0.9` against `0`: 22 of 23 frames differ, for the world and for the bounce.
- The history's allocation line: `ref_metal: rt temporal direct 640x480: 12369920 bytes allocated`, once at
  `0.9` and no allocation line at `0`; the accumulation's GPU time: a mean of 0.039 ms a frame for the world
  and 0.042 ms for the bounce, over 687 `rt direct temporal` lines each, and no time at `0`. Measured with
  the screen locked during a hold: not a timing measurement, and no conclusion about performance is drawn
  from it (decision 0014). The cost table is slice 4's.
- Frames for the owner (base2, `demo1.dm2` captures 21 and 22 outdoors and 12 indoors, at `0` and `0.9`):
  the frames of captures 21, 22 and 12, for the world and the bounce scenarios, at `0` and at `0.9` (12
  frames), went to the owner directly and were not committed.

The hold was run again on the carried head `d78d736743e6`, on main `8ef64ddfb44d`, after TASK189 slice 2 and
TASK59 slice 2 merged. The stock pins: 29 of 29 byte-identical to main. RT-primary: 12 of 12 records
byte-identical to main. The scratch runs at `0.9` are deterministic: 0 of 23 frames differ between runs. The
hashes are the same as in the first hold (`2f6e7791f25d` for the world and `6629d6d589f3` for the bounce at
`0.9`, `96a043d6d138` and `a94e7ce56a82` at `0`).

## Notes from review

- There is no light-set reject, by design (above): RT-primary's light sums every light.
- The RGBA16Float history can settle up to about 0.0024 short of the mean near 1.0 (half-float rounding),
  which is under one 8-bit level.
- The `normal` and `material` sizes are not checked separately before the dispatch: they are G-buffer
  attachments of one size, and the trace has the same gap.
- A non-finite history count is not rejected. It is unreachable today because fresh histories always reset
  first; slice 2 hardens it.

# Slice 2: the 5x5 filter on RT-primary's light, and the water blur under RT-primary

Branch `task190s2`, on main after slice 1 and TASK59's slices 2 and 2b merged. The card's defaults 3 (the
summed light is denoised, not each term) and 10 (the water blur runs under RT-primary).

## What existed before slice 2

- `rm_rtp_temporal` wrote `t_acc` (the accumulated light) and the history and key for the next view, and
  nothing filtered them: a history that had just started again (a reset, a disocclusion) showed its one
  sample's noise.
- RT7's denoiser (`rm_rtdenoise.metal`, `common/mtl_rtd_math.h`) filtered only the AO and the sun, both off
  under RT-primary.
- The water scene gated the reflections' blur by `rm_rtt_wblur(RM_RTT_On(), RM_PresentationMode() ==
  RM_PRES_RTPRIMARY)`, so under RT-primary the water was the trace's alone, and the menu's "accumulate" row was
  "set by the presentation" there.

## What slice 2 adds

- **The rule.** `common/mtl_rtpd_math.h` (new) includes `mtl_rtp_math.h` and `mtl_rtd_math.h` unchanged and
  copies none of their weights: `mtl_rtpd_weight` is `mtl_rtd_weight` (the binomial tap, the depth and normal
  guides, the material, a neighbour with a sample) and 0 for a neighbour whose normal was not written;
  `mtl_rtpd_add` sums each of red, green and blue with `mtl_rtd_add`; `mtl_rtpd_result` moves each channel
  toward its own mean by `mtl_rtd_strength` of the pixel's count (full at 1 sample, none from 16), clamps it
  to [0, 1] and keeps the trace's alpha; `mtl_rtpd_on` is the pixels it writes, world pixels whose count
  leaves a strength.
- **The kernel.** `rm_rtp_denoise`, in `rm_rtpdenoise.metal`, a unit of its own linked after
  `rm_rtptemporal.metal`, which is not edited. It reads the history and key `rm_rtp_temporal` has just
  written (the pixel's and each neighbour's distance from the eye and material from the key, the light and
  count from the history), the G-buffer's depth, normal and material and the trace's alpha, and writes only
  `t_acc`, on the pixels it filters. The history keeps the accumulation alone, as RT7's.
- **The dispatch.** In `rmb_rtdir_accumulate`, in the accumulation's encoder after its dispatch, behind a
  texture barrier, with the same bindings, before the histories swap. The `r_rt_stats 2` line
  `rt direct temporal` times both. The pipeline is made with the accumulation's and released when the pass
  closes; one that cannot be made is latched (`ref_metal: rt temporal: no direct-light denoise pipeline`) and
  the light is then accumulated alone. It allocates nothing.
- **The size guard.** The filter reads the G-buffer's normal and material at its neighbours, not only at the
  pixel, so `rmb_rtdir_accumulate` also requires `normal` and `material` to be the light's size before it
  dispatches (added in review; slice 1's note above). They are G-buffer attachments made at one size, so this
  changes no frame today.
- **The count.** `mtl_rtp_reject` also rejects a history whose count is past `MTL_RTT_COUNT_MAX`, infinite
  included (`MTL_RTP_REJ_BAD`), so no count can make the blend a NaN (slice 1's note).
- **The water.** `rm_rtt_wblur` is removed and the scene calls `rmb_rtw_set_blur(RM_RTT_On())`: the blur
  runs with `r_rt_temporal` above 0 under every presentation. Stock, Enhanced and Retro read it as before
  (Retro reads `r_rt_water` as 0). The menu's "accumulate" row is live under RT-primary.
- **No new cvar.** `doc/040_cvarlist.md`'s `r_rt_temporal` and `r_rt_stats` entries are updated.

## Slice 2's tests

- **Host:** `tests/metal/rm_rtpdenoise_test.c` (new), 35 checks: the pixels it writes, the weight, each
  channel with its own neighbours, the strength at 1 and 8.5 samples, a settled history left as it is, values
  that are not finite, the clamp, and a 9 x 7 image filtered as the kernel does, across a material edge and a
  depth edge. Red before the header (it did not exist), then 35 checks, 0 failed.
  `tests/metal/rm_rtptemporal_test.c` gains 4 checks for the count (red: 3 failed), then 63, 0 failed.
- **Header mutants** (`tests/check-ref-metal.sh`), each of which must compile and fail it: green summed from
  red, the count ignored, a neighbour without a normal weighed, the count kept as the alpha, the clamp dropped.
- **Kernel text pins**, five with six mutants, each compiled with `xcrun metal` where it is there: the key's
  distance and material and the neighbour's count and normal reaching the weight (mutants: the material
  compared with itself, the count not passed), the history read for the values (mutant: the trace's light
  read in its place), the neighbours bounded on all four sides (mutant: the right and bottom bounds
  dropped), the result taken from the pixel's history and not the raw trace (mutant:
  `mtl_rtpd_rgba(light_in.read(px))` in place of `mtl_rtpd_rgba(hc)`), and only `t_acc` written (mutant: the
  history written). The bounds and result pins were added in review; the slice 2 hold below ran before
  them, with three pins and four mutants. The back end's order (the
  accumulation, the barrier, the filter, the swap) and the Makefile's two lines are pinned.
- **The water's pin** is inverted: the scene must call `rmb_rtw_set_blur(RM_RTT_On());` once and name no
  presentation there (mutants: the RT-primary gate put back, the call commented out). `rm_rttemporal_test`
  loses its four `rm_rtt_wblur` checks; the temporal rule's mutants drop `wblur`.
- **Menu:** `tests/menu/videomenu_test.c` expects the row live under RT-primary with the user's water and sun
  off (red with the old exclusion).
- **Pins:** `scripts/pins/scenarios/rtprimary-denoise.cfg` (new) is `rtprimary-world` at `r_rt_temporal 0.9`,
  recorded at `demo1.dm2` and with `PINS_DEMO=water.dm2`. Every other scenario is unchanged: the stock ones
  draw nothing this slice changes, and every other `rtprimary-*` sets `r_rt_temporal 0`, so neither the
  accumulation, the filter nor the water's blur runs there.

## Slice 2 hold

Run by the orchestrator; the numbers below are its `task190s2-summary.log`'s.

- Builds, `make test` and the checks: the hold ran at `e778d9b6a956`, on main `157f1a346d25` (main not
  merged into the branch; main's tree built separately as the base); every build and every check exited 0.
  `make test`: 591 tests, 0 failed. `check-ref-metal`: ok, 13162 checks, 0 failed, with the RT-primary filter
  at 35 checks, 0 failed, 5 rule mutants, and 3 kernel pins with 4 kernel mutants refused and compiled, and
  the blur under every presentation pinned with 2 mutants refused. The cvar-docs, runbook-index, doc-links,
  video-menu, pin-chars and scenario-parsers checks: ok.
- The stock pins at `r_presentation 0`, against main: 30 of 30 stock scenarios byte-identical to the base
  (30 scenarios on each side, 0 failed).
- Every other RT-primary record (at `r_rt_temporal 0` by its scenario) against main: 12 of 12 records
  byte-identical to main's.

  | Record | Frames | Hash (branch and main) |
  |---|---|---|
  | `rtprimary-world` | 23 | `c2d7b7d94e6c` |
  | `rtprimary-direct` | 23 | `8975350df42b` |
  | `rtprimary-vis` | 23 | `469ce42fec8c` |
  | `rtprimary-noshadow` | 23 | `b3d10fafdc08` |
  | `rtprimary-sky` | 23 | `9acbe75f592b` |
  | `rtprimary-sun` | 23 | `6f6c37b9a084` |
  | `rtprimary-bounce` | 23 | `e2a45f031fcc` |
  | `rtprimary-metalfx2` | 23 | `b03c73ee59d1` |
  | `rtprimary-world` on `demo2.dm2` | 89 | `a254ac92e157` |
  | `rtprimary-world` on `water.dm2` | 4 | `e73ffa9aea7d` |
  | `rtprimary-dlights` | 23 | `6d01f9bb9bca` |
  | `rtprimary-dlights` on `demo2.dm2` | 89 | `8f8b43a1f6ee` |

- `rtprimary-denoise` at `demo1.dm2`: three branch runs of 23 frames, `371419d919dd` in all three, 0 of 23
  frames differ between run 1 and 2 and between run 1 and 3. Main's code (the accumulation alone) records
  `d9428ae6ae4f`: 23 of 23 frames differ from the branch's.
- `rtprimary-denoise` on `water.dm2` (the same demo file on both sides, `6a7eaf9ec2bb`): two branch runs of 4
  frames, `b5cc3f8dd8bf` in both, 0 of 4 frames differ between them. Main's code (no blur under RT-primary)
  records `d728871280c4`: 4 of 4 frames differ from the branch's.
- The engine lines: at `demo1.dm2`, `ref_metal: rt temporal direct 640x480: 12369920 bytes allocated`, and 0
  `no direct-light denoise pipeline` lines. On `water.dm2`, `ref_metal: rt temporal direct 640x480: 12369920
  bytes allocated`, `ref_metal: rt water 640x480: 3883008 bytes allocated` and `ref_metal: rt water blur
  640x480: 1327104 bytes allocated`.
- Frames for the owner (`demo1.dm2` captures 21, 22 and 12, filtered and accumulated alone; `water.dm2`
  captures 1 and 3, the blur on and off) were set aside for the owner and not committed.

# Slice 3: the MetalFX temporal scaler through RT-primary's overlay

Branch `task190s3`, on main after slice 2 merged (#791). The card's defaults 2 (a development view is drawn
unscaled under the overlay's scaler) and 5 (the scaler resets only when an effect goes from none to on, which is
TASK109's code and is not touched here). No change to the scaler's own code (`rm_fxscene.c`, `rm_metalfx.c`,
`rmb_metalfx.m`), which TASK109 owns.

## What existed before slice 3

- `pres_rtprimary` (`rm_rtgate.c`) decided `r_rt_direct`, `r_rt_shadows`, `r_rt_sun`, `r_rt_ao` and `r_rt_water`;
  `r_metalfx` was the player's under RT-primary. `rtprimary-metalfx2` sets `r_metalfx 2` itself.
- The scaler reads `r_metalfx` through `RM_Effect` (`rm_fxscene.c`'s `RM_FX_Read`), so an overlay row reaches it,
  and a value the overlay supplies is read and never written back (`rm_fx_writeback`, `rm_effect_overlaid`). A
  mode the device lacks falls to the best one below (`rm_fx_supported`): the temporal scaler to the spatial one.
- At `r_metalfx_scale 1`, or at the default in a window no taller than the GPU's default rows (the harness's 480
  lines), a scaler on runs at 1:1 as temporal anti-aliasing, with its jitter (TASK109's runbook).
- The menu greyed the scaler items only under Retro (`VidRules_MetalFXOverridden`, `VidRules_TuneOverridden`).

## What slice 3 adds

- **The row.** `pres_rtprimary` gains `{ "r_metalfx", 2.0f }`, the temporal scaler. `rm_pres_fx` (new,
  `rm_rtgate.c`) is the value `RM_Effect` returns for `r_metalfx`: under RT-primary it is `0` (off) on a device
  without the temporal scaler (never the spatial one below it, the plan's section 2.3) and while a
  development view is asked for (default 2: the view is drawn unscaled); else the row's `2` where
  the view's internal size is below its output, and the player's value at the output's own size. The last rule
  is the plan's section 2.2 ("the temporal scaler where the output is above the internal size") and section 6
  ("at `r_metalfx 0` and the internal size equal to the output, so no scaler history enters a pin"): at the
  harness's 480 lines and the default scale the mode runs no scaler of its own, so every existing `rtprimary-*`
  record keeps its frames, `rtprimary-metalfx2`'s 1:1 temporal anti-aliasing included. Every other mode reads
  `rm_pres_value`'s value as before.
- **The development view.** `rm_main.c` reads it through the effect table, as it reads every ray-traced effect
  (TASK179): each row gains `show_asked` (`RM_*_ShowAsked`: the effect wanted and its `_show` cvar naming a
  view), walked by `rm_rtfx_show_asked`. Unlike `rm_rtfx_show_only` it holds before the frame's `frame` and
  `view` entries, so the scaler, sized in `RM_GB_Begin` before them, can read it. Under RT-primary only the direct
  light and the water are wanted, so `r_rt_direct_show` and `r_rt_water_show` are the views it sees.
- **The size.** `RM_PresBelow` (`rm_main.c`) sets, at the start of each `RI_RenderFrame`, whether the view's
  internal size is below its output, exactly as `RM_FX_Begin` sizes it: `rm_fx_scale` of `r_metalfx_scale` (as
  the A/B side gives it), the GPU's default rows and the temporal scaler's largest ratio, over the view's
  rectangle (`rm_view_rect`, as `R_SetupGL`). It calls the scaler's pure functions and changes none.
- **The capability.** `rm_main.c` reads `rmb_fx_support()` with the presentation at start-up and at
  `vid_restart`, and prints a second line after the presentation's under RT-primary (`rm_pres_fx_format`):
  `ref_metal: presentation: ray traced: r_metalfx reads 2 (temporal) where the internal size is below the output,
  r_metalfx_scale the user's`, or, without the temporal scaler, `... r_metalfx reads 0 (off): no MetalFX temporal
  scaler on this device`.
- **The menu.** `VidRules_MetalFXDecided` (`vidrules.c`, new) is the menu's copy of the rule until TASK237 makes
  one table: Retro `0` ("set by the presentation", as before), Ray traced where it runs `2` ("set by the
  presentation: temporal") or `0` without the temporal scaler ("off: no temporal scaler on this GPU"), and the
  player's at `r_metalfx_scale` 1 or above. The menu does not see the window's height, so it counts the default
  scale as below the output. The video menu's "MetalFX scaler" item and the Tuning screen's "scaler" row are
  greyed with that reason and show the value in use; the "MetalFX scale" item follows the value in use (live at
  `2`, "needs a scaler" at `0`). Apply writes no `r_metalfx` while the item is greyed.
- **No new cvar.** `doc/040_cvarlist.md`'s `r_presentation` and `r_metalfx` entries say what Ray traced reads.

## Slice 3's tests

- **Host:** `tests/metal/rm_rtgate_test.c` gains the row and `rm_pres_fx` for six player values (NaN among them),
  each of the device, development-view and size inputs, and every mode, the row's `rm_effect_overlaid`, and the
  two lines. Red before the code (`rm_pres_fx` did not exist), then 1162 checks, 0 failed.
- **Menu and renderer:** `tests/metal/rm_pres_menu_test.c` (new) links `rm_rtgate.c`, `rm_metalfx.c` and
  `vidrules.c` into one binary and holds `VidRules_MetalFXDecided` to `rm_pres_decide` then `rm_pres_fx` over 11
  presentation values, ray tracing on and off, all 32 mode masks, 10 scales (0, NaN and the infinities among them)
  and 4 player values, at an output of 2160 lines: 56329 checks, 0 failed. `tests/check-ref-metal.sh` runs it and
  requires its five mutants refused: the renderer's row dropped, the renderer's and then the menu's capability rule
  dropped, the renderer's and then the menu's size rule dropped (768, 1216, 1218, 192 and 193 checks failed).
- **The menu:** `tests/menu/videomenu_test.c` expects, under Ray traced, the Tuning screen's scaler greyed showing
  "temporal", the main menu's scaler greyed with each reason (modes 7 and 3), the scale item live with the temporal
  scaler and greyed without it, Apply writing no `r_metalfx`, and the item the player's at scale 1 and without ray
  tracing. The scaler's row was live under Ray traced before (red).
- **The effect table:** `tests/metal/rm_rtfx_test.c` walks `show_asked` as it walks `show_only` (every row in
  the order up to the first that says yes): 481 checks, 0 failed. `tests/metal/rm_rttab_test.c`, over the real
  text of `RM_Effect`, checks its `r_metalfx` under RT-primary (`2` below the output, the player's `0` at its own
  size, `0` for an asked direct or water view and without the temporal scaler) and under stock (the player's `2`,
  a view asked or not): 18 checks, 0 failed. With the walk replaced by `0` in `RM_Effect` it fails 2 of 18.
  `check-ref-metal`'s effect-call section refuses its 4 call mutants and names nothing in the real files.
- **Pins:** `scripts/pins/scenarios/rtprimary-overlayfx.cfg` (new) is `rtprimary-world` at `r_metalfx_scale 0.5`
  with the player's `r_metalfx 0`: only the overlay's scaler runs there. Every other scenario is unchanged.

## Slice 3 hold

Run by the orchestrator at `f89121ee19fc`, on main `d580216f8033` (merged), with 0 files changed.

- **Builds:** `make ref_metal`, `make` and `make test` exit 0; `make test`: `yq2test: 591 tests, 0 failed`.
- **Checks:** `check-cvar-docs`, `check-video-menu`, `check-runbook-index`, `check-doc-links`, `check-pin-chars`
  and `check-scenario-parsers` (`ok (5 tests, 11 mutants caught)`) pass. `check-ref-metal` failed:
  `rm_main.c` named `rm_rtdir_show`, an effect outside the effect table, and `tests/metal/rm_rttab_test.c` did
  not build over `RM_Effect`'s new text. The three `FAIL` lines of `rm_rtdenoise_test` in its log
  (`171 checks, 3 failed`) are its header mutant's run (the `rtdm` tree), which must fail. Fixed after the
  hold (the development view through the table, above); the next hold reruns the check.
- **Stock pins:** `water.dm2` made (100 messages, the move on rendered frame 36), `f34c779dbab4` on both sides.
  Base and branch 30 scenarios each, 0 failed: 30 of 30 stock scenarios byte-identical to the base.
- **RT-primary's records against main's (want same), one branch run each, all identical:**

  | Record | Frames | Hash |
  |---|---|---|
  | `rtprimary-world` | 23 | `c2d7b7d94e6c` |
  | `rtprimary-direct` | 23 | `8975350df42b` |
  | `rtprimary-vis` | 23 | `469ce42fec8c` |
  | `rtprimary-noshadow` | 23 | `b3d10fafdc08` |
  | `rtprimary-sky` | 23 | `9acbe75f592b` |
  | `rtprimary-sun` | 23 | `6f6c37b9a084` |
  | `rtprimary-bounce` | 23 | `e2a45f031fcc` |
  | `rtprimary-metalfx2` | 23 | `b03c73ee59d1` |
  | `rtprimary-denoise` | 23 | `371419d919dd` |
  | `rtprimary-styles` | 89 | `a254ac92e157` |
  | `rtprimary-pool` | 4 | `e73ffa9aea7d` |
  | `rtprimary-denoise-pool` | 4 | `b5cc3f8dd8bf` |
  | `rtprimary-dlights` | 23 | `6d01f9bb9bca` |
  | `rtprimary-dlights-d2` | 89 | `8f8b43a1f6ee` |

  RT-primary: 14 of 14 existing records byte-identical to main's.
- **`rtprimary-overlayfx`:** three branch runs of 23 frames, `77f3485fe88f` in all three, 0 of 23 frames differ
  between run 1 and 2 and between run 1 and 3. Main's code (the player's `r_metalfx 0`, no scaler) records
  `c2d7b7d94e6c`: 23 of 23 frames differ from the branch's. The branch's engine line adds `ref_metal:
  presentation: ray traced: r_metalfx reads 2 (temporal) where the internal size is below the output,
  r_metalfx_scale the user's` after the presentation's; main's has no such line.
- **The development view under the overlay's scaler** (`overlayfx-show`): 23 frames, `8975350df42b` on the
  branch and on main, identical: unscaled, as main.
- Frames for the owner (`rtprimary-overlayfx` captures 21, 22 and 12, branch and base) were set aside for the
  owner and not committed. 0 untracked files left.

## Left for the next slices

- Slice 4: the cost table of section 4.5, measured in the owner's quiet window.
