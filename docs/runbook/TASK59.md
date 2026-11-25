Temporal accumulation in `ref_metal` (TASK59, RT7), slices 1, 1b and 2

RT7 accumulates the ray-traced effects over frames
([design](../plans/metal-raytracing-design.md), section 2, "Temporal accumulation", the
"Reset" row of the scaler's inputs, and row RT7 of section 6). Slice 1 adds the history, the
rejection rule, the blend and the resets, under `r_rt_temporal`, off by default, and
accumulates the AO; slice 1b accumulates the shadows and the sun with one reset state for
the three; slice 2 denoises the AO and the sun and sets the design's default of 0.9. This
file holds the slices' results and what they leave to the next.

**Status.** `r_rt_temporal` is 0.9 by default since slice 2. With it at 0 nothing is
made, written or read: each pass's encoder has its dispatch as before, the shadows' with the
stock `rm_rts_trace`, and the stock pins, whose `rt-*` scenarios set it to 0, are
byte-identical to the base (the holds, below). With it above 0 the shadows, the sun and the
AO are accumulated, and the AO and the sun denoised.

## What exists in the code, traced

- **Motion vectors.** The G-buffer path's `rg16Float` attachment (`rmb_gbuf_motion()`,
  `header/rm_gbuf.h`): pixels of the render size, previous minus current, +x right, +y
  down, from the unjittered current and previous projections (TASK54). The accumulation
  reads them as they are.
- **Material id.** The G-buffer's `R8Uint` material attachment (`MTL_GB_MAT_*`,
  `common/mtl_gbuf_math.h`): 1 world, 2 alias model, 3 warped water, 4 sky, 0 where no draw
  wrote. Every ray pass already binds it; the AO's accumulation reads it there.
- **Dynamic light set.** No per-pixel light set exists for the AO or the sun: neither reads
  a dynamic light. The shadows' kernel writes one, the packed indices of the four nearest
  lights in reach (`mtl_rts_pack_idx`, the second word of its `out` texture). The rule
  takes the set as a word on both sides; the AO passes 0 for both. The shadows' slice is to
  pass that word with its flag bits masked off. No G-buffer attachment changes.

## What slice 1 adds

- `common/mtl_rtt_math.h`, shared by the kernel and the host test:
  - `mtl_rtt_reproject`: the pixel's centre moved by its motion vector, off the screen when
    outside the render size or not finite;
  - `mtl_rtt_reject`: the reasons a history is dropped: the view's reset, off the screen,
    the distance from the eye beyond the tolerance, a different material id, a different
    dynamic light set, an empty history;
  - the depth tolerance (`mtl_rtt_depth_ok`): each frame's distance from its own eye may
    differ by the eye's move between the frames plus 2 units plus 2 % of the distance;
  - `mtl_rtt_blend` and `mtl_rtt_count`: the exponential blend, the history weighted by
    `r_rt_temporal` but never more than n / (n + 1) for a history of n samples, so a
    young history is the plain mean of its samples; the count is capped at 255 for the
    denoiser of slice 2.
- `rm_rttemporal.c` (`header/rm_rttemporal.h`): `rm_rtt_alpha` reads `r_rt_temporal` (0 for
  a value at or below 0 or not finite, at most 0.98), and the reset state: `rm_rtt_step`
  gives a view's reasons (first view, render size, rectangle, a move of more than 256 units
  on an axis), `rm_rtt_frame_end` makes the first view after a frame that accumulated
  nothing reset, `rm_rtt_state_reset` is the map load and the renderer's start.
- The AO's accumulation: `rm_rtao_temporal` in `rm_rtao.metal`, a second dispatch in the
  AO pass's encoder after the trace (`rmb_rtao.m`). It reads this frame's AO, the history at
  the reprojected texel, writes the blended AO with its distance, material id and count to
  the other history, and writes the light the resolve reads again from the blended AO. The
  two histories are `RGBA32Float` at the render size, made at the first view that
  accumulates and released when `r_rt_temporal` is 0; the engine logs
  `ref_metal: rt temporal WxH: N bytes allocated` when it makes them.
- The resets: `RM_RTT_Reset` is called from `RM_FX_Reset` (a map load through
  `BeginRegistration`, and the renderer's start, which a `vid_restart` repeats) and
  `RM_RTT_EndFrame` from `RM_FX_EndFrame`, so RT7's history resets on the scaler's events.
  `r_rt_scale` does not exist yet (RT11); its change will reach the history as a change of
  the render size, which resets it today.
- `r_rt_temporal` in `doc/040_cvarlist.md` and classified in `rm_rtgate.c` as a setting.
  It has no tuning-screen row yet: a row of `rt_tune` is also a preset cvar
  (`VidRules_PresetCvar`, counted by `scripts/rt/preset-to-table.sh`) and shifts the rows the
  unit tests name by index, so it lands with the default in slice 2, with the presets and the
  settings optimizer.

## Tests

`tests/metal/rm_rttemporal_test.c`, run by `tests/check-ref-metal.sh` (86 checks): the
reprojection at the edges and on values that are not finite; the rejection rule, the
accepted case and one case per reason alone; the tolerance; the blend, the count, a
constant kept, two alternating samples averaged, a rejection starting again; the cvar; and
every reset event the renderer sees. The check then builds the test against mutants of the
two files, each of which it must fail: the material, light set, depth and off-screen
reasons dropped one at a time, the view's reset ignored, a frame without an accumulated view
not resetting, a map load not resetting (7 refused). The kernel's call to the rule is pinned
as text, `mtl_rtt_reject(t.reset, r.ok, dist, h.y, t.move, m, hm, 0u, 0u, h.w)`, with two
mutants refused (the reset passed as `0u`, the material compared with itself), and the
kernel's calls of `mtl_rtt_reproject`, `mtl_rtt_reject`, `mtl_rtt_blend` and `mtl_rtt_count`
join the kernel-call pins with their ten mutants each. `check-ref-metal: ok (...; temporal
86 checks, 0 failed, 7 rule mutants and 2 kernel mutants refused; ...; 10 kernel calls, 100
kernel-call mutants refused; ...)`.

## The hold

One hold on the final commit, as the scratchpad's `task59s1-hold.sh`: `make -j8 ref_metal`,
`make`, `make test`, `tests/check-ref-metal.sh` and the doc and menu checks; the stock
`metal` pins (every committed scenario but RT-primary's) recorded by the base and by the
branch and compared frame by frame; and two scratch scenarios, never committed, from
`rt-ao.cfg` with the shadows, the sun and the AO on, one AO ray a pixel and the noise
moving (`r_rt_ao_seed 0`), at `r_rt_temporal 0.9` (run twice) and at 0. Their frames are
for the owner and are kept outside the repository.

Results of the first hold (commit `f70d57b7`, base `8870daa5`; the menu row it still had is
gone since, which changes no renderer frame):

- `pins: 29 of 29 stock scenarios byte-identical to the base`, so accumulation off changes
  no frame.
- The accumulated scenario, 23 frames of `demo1.dm2` at 640 x 480: the engine log has
  `ref_metal: rt temporal 640x480: 9895936 bytes allocated` once, the run at
  `r_rt_temporal 0` has no such line, and all 23 frames differ between the two.
- Determinism: two runs of the accumulated scenario give one hash (`e55e54e3528f`), every
  frame byte-identical. The pins harness drives `timedemo 1`, so the frames, the noise's
  frame count (`r_rt_ao_seed 0`) and the histories are the same on every run.

The hold for the final commit repeats every check.

No pin is made of an accumulated scenario in this slice.

## Slice 1b: the shadows, the sun and one reset state

### What it found, traced

- **The shadows' term needs the visibilities before it is made.** `rm_rts_trace` adds each
  light's add times its visibility to an RGBA16Float term, and the resolve reads only the
  term. A pass after the trace cannot accumulate the visibilities and make the term again:
  the adds come from the surface's lightmap axes and plane, found in the trace itself (the
  G-buffer's surface attachment or an eye ray), and a pixel that is fully shadowed this
  frame keeps no trace of them in the term. So the accumulation is inside the trace:
  `rm_rts_temporal` is the trace with the four visibilities blended with their history
  before the term is made, run in `rm_rts_trace`'s place while `r_rt_temporal` is above 0.
  `rm_rts_temporal` calls a copy of the trace (`rm_rts_shade`, the stock kernel's body up to
  its visibility loop) and of its store (`rm_rts_finish`: the term, the packed words, the
  counts); `rm_rts_trace` keeps main's text and is not a caller of them.
- **Why sharing the trace changed the stock frames (round 1).** The first hold of slice 1b
  had `rm_rts_trace` call the two helpers, and `rt-shadows` and `rt-shadows-entities`
  differed from main at `r_rt_temporal 0`. The source arithmetic was the same, and so was
  the AIR's (`xcrun metal -S -emit-llvm`): the same `fast` float operations in the same order,
  except that main packs the four visibilities twice (for the term and for the packed word)
  where the helpers pack them once. What changed is what the compiler inlined. Main's unit
  inlines the whole trace into `rm_rts_trace` but `mtl_rts_surf`; with the helpers it called
  `rm_rts_shade` and `rm_rts_finish` as functions, and with `rm_rts_trace` restored to main's
  text but the copy in the same unit, `rm_rts_visibility` and `mtl_rts_surf_light` had a
  second caller and stopped being inlined into it. Under Metal's default fast math, the GPU
  compiler is then free to contract and reorder a different program, which is the likely way
  a few pixels round differently. That last step happens below the AIR and is not traced. So
  `rm_rts_temporal` and its copy are compiled apart: they sit behind `RM_RTS_TEMPORAL_TU`, and
  `rm_rtshadow_temporal.metal` defines it and includes `rm_rtshadow.metal`. The stock unit's
  AIR for `rm_rts_trace`, `mtl_rts_surf` and `rm_fragment_resolve_rts` is then identical to
  main's.
- **The sun's light can be made again after the trace.** Its one ray gives a class (lit or
  shadowed, else not traced: bright, facing away) and the light is the base plus the sun's
  term where lit (`mtl_rtsun_light`); the base, the normal and the sun's direction are all
  inputs of the pass. So `rm_rtsun_temporal` follows the trace in its encoder, as the AO's
  does, and writes the light again from the accumulated visibility. A pixel that was not
  traced writes a sample count of 0, so its history is not read where it is traced next.
- **No shared depth and material record.** The three histories each keep their own distance
  and material id. They are written by three passes in three encoders, each effect can run
  alone, and one effect's history can be a view behind the others' (its pass skipped a view,
  below): a record shared by the three would have to be written by whichever pass runs first
  in the view and read by the others with one ping-pong index, and one effect's skip would
  then judge the others' histories by a record of another view. Kept apart, a skip resets
  only that effect. The cost is 8 bytes a pixel in each of an effect's two histories: the distance and the material
  id of the AO's and the sun's RGBA32Float, the same of the shadows' record.

### What it adds

- The light set (`mtl_rtt_lights`, `common/mtl_rtt_math.h`): the shadow kernel's packed
  indices (`mtl_rts_pack_idx`) with the in-range and shadowed flags masked off, the four
  slots' 6-bit indices. A change of the lights reaching the pixel or of their order rejects
  the history; a pixel shadowed in one frame and lit in the next keeps it. The AO and the sun
  pass 0 on both sides. The set compares slot indices into the frame's dynamic-light list, not
  the lights themselves, so a light that takes a dead light's slot in the same frame, in the
  same order, can inherit its history (the slice 1b review's N2).
- The shadows' history: a record, RGBA32Uint (the light set, the distance's bits, the
  material id, the count's bits), and the four visibilities, RGBA16Float, two of each at the
  render size. The sun's: RGBA32Float (the visibility, the distance, the material id, the
  count), as the AO's.
- One reset state for the three (`RM_RTT_ViewFor`, `rm_rtaoscene.c`): `R_RenderViewGB` calls
  `RM_RTT_BeginView` before the effects' views, and the first effect that asks steps the state
  (`rm_rtt_step`) for the view; every effect after it gets the same view, with the same
  reasons. A map load, a `vid_restart`, a cut, a change of the render size or the rectangle,
  and a frame after one that accumulated nothing reset the three histories in the same view.
- The early return (slice 1's note, and the slice 1 reviews' I1 and N1): each history keeps
  the number of the last view its dispatch encoded (`rm_rtt_hist_t`), and each back end takes
  the view (`rm_rtt_hist_take`) only where it encodes its temporal dispatch, after every
  return of its trace: no view or pass open, a size mismatch, no command buffer or encoder,
  the scene not bound, the motion vectors missing or of another size. A history that did not
  take the view before this one resets (FIRST) in this one, so a skipped dispatch never leaves
  a history two views old, and a reset reason of the skipped view is not lost. The same rule
  covers an effect turned off for views the others accumulated, and histories made again
  (`rm_rtt_hist_reset`, which replaces slice 1's `hist_fresh`). The views' number
  (`rm_rtt_state_t`'s `seq`, the view's former `pad`) is kept across a state reset, so it
  never repeats.
- `r_rt_temporal 0` and back: at 0 the state is forgotten and every back end releases its
  histories at its next view; at 0.9 again they are made anew, so the first view resets.
  That next view is one that reaches the back end's temporal call: `RM_RTS_View` returns
  before it while the pass is inactive or the view has no dynamic light, so the shadows' and
  the sun's histories can stay allocated over such views (the slice 1b review's N1). None is
  read meanwhile.
- A failure is latched (the slice 1 review's N1): a temporal pipeline that cannot be made is
  not tried again until its pass closes, and histories that cannot be made at a render size
  are not tried again at that size. Each logs one line.
- `r_rt_ao_show`, `r_rt_shadows_show` and `r_rt_sun_show` show the frame's own trace, before
  the accumulation (the slice 1 review's N5), as `doc/040_cvarlist.md` says.
- The shadow stats describe the raw trace: `MTL_RTS_SHADOWED`, the in-range and shadowed
  counts and the `r_rt_stats` shadow figures come from `rm_rts_shade`'s visibilities, while
  the term and the packed visibilities use the blended ones (the slice 1b review's N4).

### Memory

Per pixel of the render size: the AO 32 bytes (two RGBA32Float), the sun 32, the shadows 48
(two RGBA32Uint and two RGBA16Float), 112 for the three. At 640 x 480 that is 34,406,400
bytes before the allocator's rounding; at 1920 x 1080, 232,243,200; at 5120 x 2880 (a 5K
output with the render size equal to it, the largest it can be: `rm_gb_render_size` caps it
at the frame's size) 1,651,507,200, of which the AO's and the sun's 471,859,200 each and
the shadows' 707,788,800 (the slice 1 review's N2). Nothing caps the allocation itself: a
failed one is latched for that size and the frame is the trace's alone. The engine logs
each effect's `ref_metal: rt temporal ...: N bytes allocated` line.

### Tests

`tests/metal/rm_rttemporal_test.c` adds the light set (the mask holds every index bit and no
flag; flags masked keep the set; another light, one more, one gone, the slots' order, none
left each reject; the same set accepts), the reset shared by the three histories (the first
view and a cut reset all three in the same view), the early return (a history that skipped
a view resets alone in the next, the others kept; a reason of a skipped reset view is not
lost), a view taken twice, the views' number kept across a map load, and histories made
again. `tests/check-ref-metal.sh`:

- The rule mutants must compile: the material, light-set and depth mutants of slice 1 left a
  parameter unused and failed under `-Werror` (the slice 1 review's I1); they are written as
  `if ((mat != hmat) && 0)` and so on, and a mutant that does not compile is reported as bad,
  never counted as refused. Four more: the light set unmasked, a history that skipped a view
  still read, a history made again still read, the views' number restarting at a map load.
- The kernels' calls pinned as text, each with its mutants refused: the shadows'
  `mtl_rtt_reject(t.reset, r.ok, dist, hd, t.move, m, hm, set, hset, hc)` (the reset passed
  as `0u`, the light sets as `0u, 0u`), its light set from `mtl_rtt_lights` (unmasked), the
  sun's rule call (the reset, the material compared with itself), and the reprojection
  `mtl_rtt_reproject(px.x, px.y, mv.x, mv.y, w, hh)` in all three kernels with the motion's
  sign flipped (the slice 1 review's N4).
- Each back end's `tv.reset = rm_rtt_hist_take(&thist, &tview);`, with the mutant that
  passes the view's reasons on alone, and (round 2, the slice 1b review's N3) where it sits:
  after every early return of the back end's trace function, its one return after the call
  being the function's last statement, and before a dispatch. A mutant moving it above the
  `rmb_rt_bind_scene` check in `rmb_rtshadow.m` is refused.
- Kernel-call pairs for both new kernels (the reprojection, the rule, the blend, the count,
  the light set) and for the shadows' temporal kernel's trace and store.
- The stock shadow trace (round 1): `rm_rts_trace`'s text, pinned by its `cksum`; the copy
  (`rm_rts_shade` and `rm_rts_finish`) holding the stock kernel's code lines and no others but
  its own carrying of `rm_rts_px`; and the temporal kernel and its copy only behind
  `RM_RTS_TEMPORAL_TU`, built apart by `rm_rtshadow_temporal.metal` into the metallib. Four
  mutants are refused: the stock kernel edited (both the checksum and the copy pin refuse
  it), the copy edited in `rm_rts_shade` or in `rm_rts_finish`, and the guard dropped. Against
  the first hold's file the pins fail; against the fix they pass.
- The copy pin in order (round 2, the slice 1b review's I1): round 1 compared sets of lines
  with the braces dropped, so a repeated line of the copy deleted or moved passed it. Now the
  braces stay, each side-only line must occur exactly once on its side, and with those lines
  dropped the `diff` of the two sequences must be exactly the pinned one: `float3 t` declared
  in `rm_rts_finish` at its use instead of at the kernel's top, and the copy closing the
  trace's two blocks before its loop into `s.vis`, where the stock kernel makes the term
  inside them. Two more mutants are refused: `slab-continue-dropped` (the `continue;` under
  `if (!slab)` in `rm_rts_shade` deleted) and `adds-zero-moved` (`adds[i] = 0.0f;` moved
  below that block). Where a side-only line sits is still not compared.

### The hold

One hold on the final commit, as the scratchpad's `task59s1b-hold.sh`: the builds, `make
test`, `tests/check-ref-metal.sh` and the doc and menu checks; the stock `metal` pins (every
committed scenario but RT-primary's) by the base and by the branch, compared frame by frame;
and six scratch scenarios, never committed: `rt-shadows.cfg`, `rt-sun.cfg` and slice 1's view
with all three effects on (one AO ray a pixel, the noise moving), each at `r_rt_temporal 0.9`
(run twice) and at 0. Their frames 12 and 22 are for the owner and are kept outside the
repository.

Results of the hold (code commit `7ef2b286`, base main at `3d02c2ac`). The first hold of
this slice (`98a095ba`) failed the stock shadows pins; the cause paragraph above says why,
and `7ef2b286` is the fix.

- `pins: 29 of 29 stock scenarios byte-identical to the base`, 0 differ: accumulation off
  changes no stock frame.
- The shadows at `r_rt_temporal 0` hash to `2b8d11a25392`, main's `rt-shadows` digest, and
  the sun at 0 to `b7e0212dfda8`, main's `rt-sun` digest (the stock digests; the scratch
  scenarios are seeded alike, so only the frames are compared by hash). All three at 0 hash
  to `d900f43703bd`.
- Determinism, 23 frames of `demo1.dm2` at 640 x 480, two runs at 0.9 each, 0 frames
  different between the two runs:

  | Effect | One hash at 0.9 | At 0.9 against 0 |
  | --- | --- | --- |
  | shadows | `721a6ef80b2d` | 3 frames differ |
  | sun | `91cf74b316b8` | 1 frame differs |
  | all three | `31eebeb590cf` | 23 frames differ |

- The frames kept for the owner are frames 12 and 22 of each scenario's accumulated run and
  its run at 0 (the sun's: frame 12 of each). The summary names how many frames differ, not
  which.
- Allocation, one `ref_metal: rt temporal ...: N bytes allocated` line per effect at
  640 x 480, none at 0: the shadows 14,843,904 bytes; the sun 9,895,936; the AO 9,895,936
  (all three on: the three lines, one each).
- Checks: `check-ref-metal: ok (13162 checks, 0 failed; ...)` with `temporal 123 checks, 0
  failed, 11 rule mutants, 7 kernel pins with 10 kernel mutants and 3 back-end mutants
  refused, 3 stock-trace pins with 4 mutants refused` and `21 kernel calls, 210
  kernel-call mutants refused`; `yq2test: 591 tests, 0 failed`; `check-cvar-docs: ok`,
  `check-doc-links: ok`, `check-runbook-index: ok`, `check-video-menu: ok`,
  `check-pin-chars: ok`.

## Slice 2: the denoiser and the default

### What it adds

- The weight rule (`common/mtl_rtd_math.h`, shared by the kernels and the host test): the
  strength from the pixel's sample count, 1 at 1 sample, 0 from 16, linear between
  (`(16 - n) / 15`), and 0 for a count below 1 or not finite (`mtl_rtd_strength`); the 5 x 5
  binomial taps, 1 4 6 4 1 in each axis over 256 (`mtl_rtd_tap`); the depth guide, 1 at the
  pixel's distance from the eye and falling linearly to 0 at 2 units plus 2 % of it, the
  temporal rule's tolerance without the eye's move (`mtl_rtd_depth_w`); the normal guide, 1
  for equal normals and 0 at a cosine of 0.9 or less (`mtl_rtd_normal_w`); the weight of a
  neighbour, 0 for another material id or a neighbour with no sample (`mtl_rtd_weight`); the
  running sums, to which a weight not above 0, or a weight or value not finite, adds nothing
  (`mtl_rtd_add`); and the result, the pixel's value moved toward the weighted mean by its
  strength, the value itself when nothing was added or the mean is not finite, the mean when
  the value is not finite (`mtl_rtd_result`).
- The kernels, `rm_rtdenoise.metal`, a translation unit of its own (slice 1b's warning: a
  stock kernel's unit that gains a caller of its helpers can compile differently). Both
  call `rm_rtd_filter`, which reads the 5 x 5 neighbours of the history the temporal kernel
  has just written (the value, the distance, the material id and the count it already
  keeps), the G-buffer's normals, and leaves out a neighbour without a normal.
  `rm_rtao_denoise` writes the light again from the denoised AO on the world's pixels, as
  `rm_rtao_temporal` does from the accumulated one; `rm_rtsun_denoise` on the pixels the sun
  traced this frame (a count of 1 or more), as `rm_rtsun_temporal` does. A pixel whose count
  leaves a strength of 0 is not written again, so a settled history's light is the temporal
  kernel's. The denoiser writes no history: the history keeps the accumulation alone, so
  the blur does not feed back into the next frame's blend.
- The back ends (`rmb_rtao.m`, `rmb_rtsun.m`): a second dispatch after the temporal one in
  the same encoder, behind a texture barrier, with the history just written at index 8 and
  the denoiser's view (`mtl_rtd_view_t`: the sun's direction and colour, the AO's floor) at
  buffer 10. It runs only where the temporal dispatch does, so only while `r_rt_temporal` is
  above 0, after the blend and before the resolve. Its pipeline is made with the
  accumulation's; one that cannot be made is latched until the pass closes and logged once
  (`ref_metal: rt temporal: no AO denoise pipeline`, `no sun denoise pipeline`), and the
  frames are then accumulated alone. The shadows are not denoised: the design names the AO
  and the sun only.
- The default: `r_rt_temporal` is `0.9` (`rm_rtaoscene.c`), the design's value, and
  `doc/040_cvarlist.md` says it moved from 0 and why. The design runs the effect pins with
  the accumulation off, so every committed `rt-*` scenario sets `r_rt_temporal 0`, and the
  scenarios' README says so in each row that said no accumulation existed yet.
- RT-primary (`r_presentation 3`): its overlay does not set `r_rt_temporal`, which reads as
  the user's value, 0.9 by default. The overlay reads `r_rt_shadows`, `r_rt_sun` and
  `r_rt_ao` as 0 (`rm_rtgate.c`'s mode line), so no pass that accumulates runs and the
  default changes no RT-primary frame (the hold compares them with main's).

### Tests

`tests/metal/rm_rtdenoise_test.c`, run by `tests/check-ref-metal.sh`: the strength at 1, 2,
8.5, 15, 16, 17 and 255 samples and at 0, a half, a negative, NaN and both infinities; the
taps' sum, symmetry and values; the depth guide inside, at and past the tolerance and on
NaN; the normal guide at 1, at a cosine of 0.95 (a half), across a right angle, opposite,
and on NaN; the weight with each edge alone (material, no sample, NaN count, depth, normal,
a tap outside the 5 x 5); the sums and the result (nothing added, every bad weight and
value refused, the mean at 1 sample, half way at 8.5, kept at 16 and past it and on a NaN
or zero count); and a 5 x 5 blur over a 9 x 7 image as the kernel runs it: a constant kept,
a checkerboard smoothed toward its mean at 1 sample and kept at 16, 300 and NaN samples, a
depth edge, a normal edge and a material edge that stop it, a neighbour with no sample or a
NaN value left out, a NaN pixel given the mean.

The check builds it against six mutants of the header, each of which must compile and fail
it: the depth guide always 1 (`depth`), the normal guide always 1 (`normal`), the material
not compared (`mat`), the count's cut at 16 dropped (`countcut`), the strength always 1
(`countflat`), the sums' guard against a weight or value that is not finite dropped
(`finite`). The kernels' calls are pinned as text, each with a mutant the pin must refuse,
and, with the Metal compiler, the real file and every mutant must compile (a mutant that
does not is reported as bad, never counted): `mtl_rtd_weight(dx, dy, hc.y, hn.y, nc, nn, mc,
mn, hn.w)` in `rm_rtd_filter` (the depth guide dropped: the neighbour's distance replaced
by the pixel's own), `return mtl_rtd_result(acc, hc.x, hc.w);` (the count scale dropped:
the count replaced by 1), and no texture the denoiser can write but `light_out` (a mutant
that writes the denoised AO into a history at texture 9). The five calls (`rm_rtd_filter`'s
of `mtl_rtd_weight`, `mtl_rtd_add` and `mtl_rtd_result`, and each kernel's of
`rm_rtd_filter`) join the kernel-call pins with their ten mutants each.

Run by hand on the branch, with the check's compile lines and no lock:
`denoiser 119 checks, 0 failed, 6 rule mutants, 3 kernel pins with 3 kernel mutants refused
and compiled` and `26 kernel calls, 260 kernel-call mutants refused`; `check-cvar-docs: ok`,
`check-doc-links: ok`, `check-runbook-index: ok`, `check-pin-chars: ok`,
`check-video-menu: ok` (3383 checks); `xcrun metal -c` of `rm_rtdenoise.metal` and a
syntax-only compile of both back ends with `-Wall -Wextra -Werror`.

### The hold

One hold on the final commit, as the scratchpad's `task59s2-hold.sh`: the builds, `make
test`, `tests/check-ref-metal.sh` and the doc, menu and pin-character checks; main and
slice 1b's code built in scratch; the stock `metal` pins (every committed scenario but
RT-primary's, the `rt-*` ones at `r_rt_temporal 0`) by main and by the branch, compared
frame by frame; the RT-primary scenarios likewise, each side at its default; and nine
scratch scenarios, never committed: `rt-shadows.cfg`, `rt-sun.cfg` and slice 1's view with
all three effects on, each at `r_rt_temporal 0.9` (run twice), at the default (no
`r_rt_temporal` line), and at 0, and the 0.9 one run by slice 1b's code. Frames 12 and 22 of
the 0.9 run, of slice 1b's 0.9 run and of the run at 0 are for the owner and are kept outside
the repository.

Results of the hold (code commit `8091364e`, base main at `4efcd73b`, slice 1b at `3845abe2`):

- `pins: 29 of 29 stock scenarios byte-identical to the base`,
  0 differ.
- `pins: 5 of 5 RT-primary scenarios byte-identical to the base`.
- The shadows at 0 hash to `2b8d11a25392`, the sun at 0 to `b7e0212dfda8` (main's `rt-shadows`
  and `rt-sun` digests: `2b8d11a25392`, `b7e0212dfda8`).
- The default is 0.9: 0, 0 and 0 frames differ between the
  default runs and the 0.9 runs of the shadows, the sun and all three.
- Determinism, 23 frames of `demo1.dm2` at 640 x 480, two runs at 0.9 each:

  | Effect | One hash at 0.9 | Frames different between the runs | At 0.9 against 0 | Against slice 1b's code at 0.9 |
  | --- | --- | --- | --- | --- |
  | shadows | `721a6ef80b2d` | 0 | 3 | 0 |
  | sun | `84642945e98e` | 0 | 1 | 1 |
  | all three | `f262c5f10fa1` | 0 | 23 | 23 |

  The shadows are not denoised, so their column against slice 1b's code is expected to be
  0.
- Checks: `check-ref-metal: ok (13162 checks, 0 failed; ray tracing 169 checks, 0 failed; G-buffer 177 checks, 0 failed; shadows 4384 checks, 0 failed; AO 283 checks, 0 failed; temporal 123 checks, 0 failed, 11 rule mutants, 7 kernel pins with 10 kernel mutants and 4 back-end mutants refused, 3 stock-trace pins with 6 mutants refused; denoiser 119 checks, 0 failed, 6 rule mutants, 3 kernel pins with 3 kernel mutants refused and compiled; water 727 checks, 0 failed; sun 711 checks, 0 failed; direct light 680 checks, 0 failed; RT data 170 checks, 0 failed; MetalFX 335 checks, 0 failed; ray-tracing gate 952 checks, 0 failed, Retro textures 197 checks, 0 failed, effect table 464 checks, 0 failed, 10 table mutants refused, no effect outside it in rm_main.c or rmb_gbuf.m, 4 call mutants refused, tone-map order and probe cancel held, 10 order mutants refused, 35 r_rt_* cvars classified, 4 table mutants refused; 72 vocabulary mutants refused; 3 flush points, 12 flush mutants refused; blend factors once, 4 mutants refused; 5 gate-seat rules, 11 seat mutants refused; 26 kernel calls, 260 kernel-call mutants refused; colour space 736 checks, 0 failed; 12 colour-space mutants refused; metal_rate_start read once, 2 mutants refused; no hand prototype in a .m file, 18 mutants refused, 7 shapes passed; ref_metal built)`; `yq2test: 591 tests, 0 failed`; `check-cvar-docs: ok`, `check-doc-links: ok`,
  `check-runbook-index: ok`, `check-video-menu: ok`, `check-pin-chars: ok`.

## Left for the next slices

- The reflections' 3x3 depth-guided blur, with no history (the design, section 2), and its
  kernel pins with the mutant that blurs them with a history: left by slice 2, which stopped at
  the denoiser and the default to stay near its size. The design names no cvar for it;
  `r_rt_temporal` above 0 is the card's reading.
- The tuning-screen row of `r_rt_temporal`, greyed with its reason when no accumulated effect
  is on, with the presets table (`scripts/rt/preset-to-table.sh`), the counts that pin the
  screen (`tests/check-video-menu.sh`, `tests/menu/videomenu_test.c`) and, if
  `src/client/menu/optsearch.c` lists every tuning cvar, the settings optimizer: left by
  slice 2 likewise.
- The look of the accumulated and denoised effects, judged by the owner from the hold's
  frames and the tuning screen (INFORM11).
- RT11's `r_rt_scale` and the `r_rt_stats 1` reset line of the design's RT11 row.
- The GPU time of the accumulation, recorded as information (decision 0014: no target).
- A smaller history (the slice 1 review's N2): the AO's and the sun's value and count fit
  half floats, the distance a float and the material a byte.
