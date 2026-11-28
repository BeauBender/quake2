# Option A on TASK111's scaler, the `effect` reset and the `r_rt_stats` reset line (TASK109)

TASK109 is RT11 of `docs/plans/metal-raytracing-design.md` (section 6), option A of
section 2. TASK111 already built the scaler, its jitter, its motion vectors and most
of its reset events, so this card reconciles the plan with that code and adds what
is left for the ray-traced path. Its slices:

- **Slice 1, the docs.** By the owner's direction of 2026-11-14 on #263 there is no
  `r_rt_scale`. `r_metalfx_scale` is the internal scale for every path. Option A is
  `r_metalfx 2` with `r_metalfx_scale 0`, and options E and C are `r_metalfx 0`.
  - The design's section 2 and row RT11, `docs/plans/metal-port-cards.md` row RT11
    and `docs/plans/metal-renderer.md` now say so.
  - The design also records these facts:
    - the cut threshold, `RM_FX_CUT_DIST`, which is 256 units on an axis;
    - stereo is ignored: two eyes are two views of one frame;
    - `timerefresh`'s views step the jitter;
    - at `r_metalfx_scale 1` the scaler still runs at 1:1, as temporal
      anti-aliasing. TASK105's text said there was no scaler there; the code's
      behaviour is kept and the text corrected.
  - `presentation-modes.md` 4.4 belongs to TASK190 and is left alone here.
- **Slice 2, the code.**
  - **The `effect` reason.** The scaler has a new reset reason,
    `RM_FX_RESET_EFFECT` (`effect`). It is set when the first scaled world view with
    a ray-traced effect on (`rm_rtfx_active`) follows a scaled world view with none.
  - **What it resets.** An effect kept on resets nothing, an effect turned off resets
    nothing, and on, off, on resets again. The player-setup menu's model view
    (`RDF_NOWORLDMODEL`) takes the forward path and never steps the scaler. A `fov`
    change is no input of the step.
  - **The reset line.** Under `r_rt_stats 1`, a scaled world view that resets either
    history prints one line:
    `ref_metal: r_rt_stats: reset: scaler <reasons>, temporal <reasons>`.
    - It is formatted by `rm_fx_reset_line_format` and decided by `rm_fx_reset_says`,
      both pure.
    - RT7's reasons come from `RM_RTT_TakeReasons`.
    - With the scaler off, no line prints.
- **Not here.**
  - Mode 3's `rm_fx_run(..., 0, ...)` belongs to TASK113.
  - The RT-primary overlay row and its denoiser belong to TASK190.
  - The measurement was slice 4's; it moved to TASK243 (#835).

## Host checks (no lock)

| Check | Result |
|---|---|
| `tests/check-ref-metal.sh` (the MetalFX host test and its three mutants) | ok: `MetalFX 392 checks, 0 failed, 3 reset mutants refused` |
| `make test` | `yq2test: 591 tests, 0 failed` |
| `tests/check-cvar-docs.sh`, `tests/check-doc-links.sh`, `tests/check-runbook-index.sh`, `tests/check-pin-chars.sh` | ok, each |

The table above is from the branch before the hold. The hold runs the same checks again on
its final commit: all ran again at `c063b41b2c24`, on main `f10aa5bc3abe` merged in, and passed: `make test` (`yq2test: 591 tests, 0 failed`), `tests/check-ref-metal.sh` (`MetalFX 392 checks, 0 failed, 3 reset mutants refused`), and the four doc checks above, each ok.

The tests were red first, at `c73e08df`: the build of `tests/metal/rm_fx_test.c` stopped on
`rm_fx_step`'s new effect argument. They were green at `b5d1ac15`.

These are the mutants of `rm_metalfx.c` that `tests/metal/rm_fx_test.c` must refuse.
Each one compiles:

- the reason dropped;
- the comparison inverted, so an effect turned off resets and one turned on does not;
- the two fields of the line swapped.

## The hold (M6, under the timing lock)

The hold builds main (the before) and this branch in scratch. Its checks:

- The stock pins, the `world-metalfx*` records and `hdr-metalfx2` are byte-identical
  to main's.
- The `rt-*` records are identical. They run at `r_metalfx 0`, where nothing here
  changes.
- One live run at `r_metalfx 2`, `r_metalfx_scale 0.5` and `r_rt_ao 1`, with
  `r_rt_stats 1`, prints the reset line for each event below and none for the last
  two.

| Event | Line |
|---|---|
| map load | 1 line: `scaler first, temporal first` |
| `vid_restart` | 1 line: `scaler first, temporal first` |
| a camera cut (the game's `teleport`, 300 units away) | 1 line: `scaler cut, temporal cut` |
| a teleport (the same command, 700 units on; base1 has no teleporter a script can reach) | 1 line: `scaler cut, temporal cut` |
| `r_metalfx_scale` 0.5 to 0.75 | 1 line: `scaler size, temporal size rect` |
| `viewsize 60`, then `viewsize 100` | 1 line each: `scaler size rect, temporal size rect` |
| the window's size (`r_customwidth` and a `vid_restart`: the tooling has no live resize) | 1 line: `scaler first, temporal first` (a `vid_restart` rebuilds the scaler, so it reads as `first`) |
| the effect off (no line), then on | off: no line; on: 1 line, `scaler effect, temporal first` |
| the player-setup menu's model view (no line) | no line |
| `fov` 90 to 30 (no line) | no line |

The menu is closed with the console's toggle, because no command closes a menu by itself.
The hold does not judge those frames.

What the host test cannot show, and the live run does:

- The model-view and `fov` cases of `rm_fx_test.c` pass because the code path never steps the
  scaler for them: `RM_GB_Begin` returns before `RM_FX_Begin` for the model view, and `fov` is
  not an argument of `rm_fx_step`. The test alone could not catch a regression there.
- `RM_FX_End`'s wiring (the take before the `fx_view` early return) has no host test.
- The effect reason follows each effect's own active flag (`rm_rtfx_active()`), not the
  `r_rt_*` switch alone. A switch that is on but whose effect cannot run (no ray tracing, or
  the sun on a map with little sky) does not trigger it, and nor does a second effect added
  while one is on.
- `vid_restart` and the resize both reset as `first`, since the tooling has no live resize.

The base and the branch: main `f10aa5bc3abe`, branch `c063b41b2c24`; the hold: 16 events, 0 wrong. Stock and MetalFX records: 29 of 29 scenarios byte-identical to the base (stock 17 of 17, `metalfx` 5 of 5).
`rt-*` records: 7 of 7 byte-identical to the base.

## Slice 3, the ray-traced effects at the scaler's internal size

### The scenarios

Two scenarios run the effects through `MTLFXTemporalScaler` (`r_metalfx 2`) at the default
`r_metalfx_scale 0`:

- `rt-all-metalfx2` is `dlights` with every ray-traced effect on, each at its `rt-*`
  scenario's values: `r_rt_shadows`, `r_rt_ao`, `r_rt_sun` and `r_rt_water`. It is
  recorded with `PINS_SEED`, as `dlights`. `demo1.dm2` shows no warped water, so
  `r_rt_water` is exercised by recording it a second time on `skywater`'s camera path
  (`PINS_DEMO=water.dm2`), as `rt-water` is.
- `rtprimary-metalfx2` is `rtprimary-world` under the scaler.

Both set `r_rt_temporal 0`, explicit so a later change of RT7's default cannot move them
(the `rt-*` pins rely on the default of 0). At the harness's 480 lines the default scale is the view's own size: the
family's default rows are 720 or 1080, and the scale is kept at 1 or below. So the committed
records run the scaler as temporal anti-aliasing. The hold records `r_metalfx_scale 0.5` from
scratch copies, which are never committed.

`rt-all-metalfx2` leaves out the ten lines of the `rt-*` scenarios that restate a default
(`r_rt_ao_range 128`, `r_rt_ao_min 0.4`, `r_rt_sunscale 0.35`, `r_rt_sunfloor 0.5`,
`r_rt_water_normal 0.08`, `r_rt_water_reflect 0.5` and the four `*_show 0`, each the
`Cvar_Get` default), so it draws what it did. With them, its seeded record started the
engine with 141 arguments, above `MAX_NUM_ARGVS` (128, `src/common/argproc.c`), and the first
hold's every `rt-all-metalfx2` record stopped at once with `argc > MAX_NUM_ARGVS`. It now
takes 111 of 128, the most of any scenario (`rt-water` 99, `rtprimary-metalfx2` 96), and
`tests/check-scenario-parsers.sh` (`t_argcount`) fails when a committed scenario's seeded
record would take more.

### The read of each effect

Every effect was read for a size that is the window's or the drawable's where it should be
the render size, and for a per-pixel radius or step that does not scale. Nothing is wrong,
and no code changes.

- **Where the size comes from.** Under the scaler, `RM_GB_Begin` takes the input size from
  `RM_FX_Begin` (`rm_fx_sizes`). The render target is `iw` x `ih`, and `view->vp` is the view
  at the target's origin with that size (`rm_fx_input_viewport`). The jittered projection
  replaces `view->proj` before any effect's view is made (`rm_rtfx_view` runs after
  `RM_GB_Begin` in `R_RenderViewGB`). Every effect's view takes `RM_GB_TargetSize` (the
  render size on the G-buffer path) and `view->vp`:
  - RT3, `RM_RTS_View`;
  - RT4, `RM_RTSun_View`;
  - RT5, `RM_RTAO_View`;
  - RT6, `RM_RTW_View`;
  - RT-primary, `RM_RTDir_View`.

  Each back end sizes its outputs from the depth attachment (`depth.width`), the render
  size. No effect reads `rmb_target_size`, `vid` or `r_newrefdef`'s size.
- **The unprojection.** Each kernel turns a pixel into clip space through the view's
  viewport (`v.vp`, for example `rm_rtao.metal` and `rm_rtdir.metal`), so the points it finds
  follow the render size.
- **RT5, the AO.** The range (`r_rt_ao_range`, 128 units) and the floor are in world units.
  The traced size is `r_rt_ao_scale` of the render size (`mtl_rtao_lo_size`). So at
  `r_metalfx_scale 0.5` and the default `r_rt_ao_scale 0.5`, the AO is traced at a quarter of
  the output's width and height. That is the cvar as documented, a fraction of the view as it
  is drawn, and it is not a size bug. The upsample's four traced pixels (`mtl_rtao_up_pair`)
  are relative to the divisor.
- **RT6, water.**
  - The marks' 3 x 3 dilation is one pixel of the target it marks.
  - The screen-space lookups map a hit through `v.vp` into the render size.
  - The world-space hits' mip level (`lod_k`) is `2 / (h x proj[5])`, with `h` the render
    viewport's height, so it is the footprint of a render pixel.
  - The test for whether a lit pixel shows the hit (`mtl_rtw_seen_tol`) is in world units:
    1 % of the distance, at least 4 units. At a lower render size a grazing surface's pixel
    spans more depth, so a few more grazing hits fall back to the world-space shading or to
    the stock water. That is a graceful fallback, not a size bug, and it is noted here only.
- **RT3 and RT4, the shadows and the sun.** These have no screen-space radius. The depth
  bias (`bias_k`, `4 / 2^24 / znear`) is depth precision, not pixels.
- **RT7, the sizes.**
  - `RM_RTT_ViewFor` keys the history on `RM_GB_TargetSize` and the viewport, so a change
    of the render size or of the rectangle resets it.
  - The motion vectors are in render pixels: `mtl_gb_motion` scales by `half_vp`, from the
    render viewport.
  - `mtl_rtt_reproject` reads them against the same `w` x `h`.
  - Each history is allocated at the depth's size.
- **RT-primary.** `rm_rtdir_view` and `rmb_rtdir_prepare` take the same render size and
  viewport. `t_vis` and `t_light` are made at the depth's size.
- **The particles.** These are not an effect, but their size is in frame pixels, and the
  G-buffer view already rescales it (`R_DrawPoints`, TASK54).

### The hold (M6, under the timing lock)

The orchestrator runs the hold script. The values in this section are filled in from its
summary.

| Check | Result |
|---|---|
| base and branch | slice 1 (`task109s1`, which this slice is stacked on) `78d36b87c275`, branch `4f6653edcbdc` (tree clean, base merged) |
| stock, `world-metalfx*`, `hdr-metalfx2`, `rt-*` and `rtprimary-*`, byte-identical to the base | 34 of 34 scenarios byte-identical to the base (stock 17 of 17, metalfx 5 of 5, rt-* 7 of 7, rtprimary-* 5 of 5); base and branch each 34 scenarios, 0 failed |
| `rt-all-metalfx2`, default scale, three runs | 23 frames, hash `320411ffa021`, 0 frames differing between runs |
| `rtprimary-metalfx2`, default scale, three runs | 23 frames, hash `6fdc926a51b2`, 0 frames differing between runs |
| `rt-all-metalfx2`, `r_metalfx_scale 0.5`, three runs | 23 frames, hash `a915d837d5ba`, 0 frames differing between runs |
| `rtprimary-metalfx2`, `r_metalfx_scale 0.5`, three runs | 23 frames, hash `106645babbe0`, 0 frames differing between runs |
| `rt-all-metalfx2` on `water.dm2`, default scale, three runs | 4 frames, hash `9b12cc02843f`, 0 frames differing between runs |
| `rt-all-metalfx2` on `water.dm2`, `r_metalfx_scale 0.5`, three runs, and its change against `r_metalfx 0` (water drawn under the scaler) | 4 frames, hash `a733326e7c53`, 0 frames differing between runs. Capture 3 against `r_metalfx 0`: at 0.5, mean 0.8454 and 0.000785 of pixels over 8 levels; at the default scale, mean 0.5418 and 0.000146 over 8. The change box at both is the whole frame, x 0..639 y 0..479. `rt-water` is unchanged at `929e43a01e45`, also at `r_metalfx 0` (`water.dm2` recorded by `make-demo`, `6cb7ef4c39c2` twice) |
| the input and output sizes (`metal_stats 1`), default and 0.5 | `rt-all` and `rtprimary`, the same: default input 640x480, output 640x480 at 0,0 (the default scale clamps to 1 at the pin's 480 lines); at 0.5 input 320x240, output 640x480 at 0,0. `supportsDevice`: spatial, temporal, temporal denoised and frame interpolator all yes (`r_metalfx_modes 31`); the `rtprimary` probes' teardown: 0 presents apart, 0 waits past the bound |
| `viewsize 60`: the box of the changing pixels at `r_metalfx 2` and at `r_metalfx 0`, against 384x288 at 128,96 | 23 captures each. The changing pixels fill x 128..511 y 96..479 (384x384 at 128,96) at `r_metalfx 0`, at 0.5 and at the default scale. The `r_metalfx 2` frames against `r_metalfx 0`: 0.5, 2307212 differing pixels, all inside 384x288 at 128,96, 0 outside (x 128..511 y 96..383); default, 2135759 differing pixels, 0 outside (same box) |
| a still `base1`, 300 frames and more, effects on, `r_metalfx 2` at 0.5: the steps at every 32nd frame and every 5th, and the last against `r_metalfx 0` | A made demo `t109still.dm2` (404 messages), recorded twice with the same hash `a11067671f9f`. Every 32nd frame (one jitter cycle), 13 captures, the last five step-to-step mean differences (pixels over 8 levels): `r_metalfx 0`: 0.1382 (0.003932), 0.0000 (0), 0.0041 (0), 0.0000 (0), 0.1382 (0.003932). At 0.5, temporal 0: 0.1386 (0.003727), 0.0227 (0), 0.0234 (0), 0.0231 (0), 0.1368 (0.003711). At 0.5, temporal 0.9: 0.1385 (0.003724), 0.0238 (0), 0.0239 (0), 0.0228 (0), 0.1369 (0.003711). So the step settles to about 0.02 levels with 0 pixels over 8 levels, and the 0.138 steps also appear at `r_metalfx 0`: they belong to the scene, not the scaler. The last capture against `r_metalfx 0`: temporal 0, mean 1.5140 (0.012568 over 8); temporal 0.9, mean 1.5261 (0.013314). Every 5th frame, 80 captures, last five steps: `r_metalfx 0` 0.0777, 0.0014, 0.0792, 0.0777, 0.0000; temporal 0 0.1841, 0.0865, 0.1824, 0.1603, 0.0893; temporal 0.9 0.1806, 0.0843, 0.1791, 0.1583, 0.0867; the last against `r_metalfx 0`: temporal 0 mean 1.4977 (0.012350), temporal 0.9 mean 1.5098 (0.012995) |
| the frames for the owner (captures 12 and 21, at 0.5 and at `r_metalfx 0`) | Sent to the owner directly, not committed: Enhanced (every effect) capture 12 and Ray traced capture 21, each at `r_metalfx 0` and at 0.5 |

The host checks (no lock) on the branch before the hold:

| Check | Result |
|---|---|
| `tests/check-ref-metal.sh` | ok: `13162 checks, 0 failed`; `MetalFX 392 checks, 0 failed, 3 reset mutants refused` |
| `make test` | `yq2test: 591 tests, 0 failed` |
| `tests/check-cvar-docs.sh`, `tests/check-doc-links.sh`, `tests/check-runbook-index.sh`, `tests/check-pin-chars.sh` | ok, each |
| `tests/check-scenario-parsers.sh` | ok: `5 tests, 11 mutants caught`; argc at most 111 of 128 (`rt-all-metalfx2`) |
| `scripts/pins.sh check-content` | `content tags ok` |

No engine code changed. The one red test is `t_argcount` in
`tests/check-scenario-parsers.sh`: red on the first `rt-all-metalfx2` (141 of 128), green
on the trimmed one, and its mutant (`argc-restated-defaults`, the ten lines put back) is
caught.

## Slice 4: the two open code lines

Slice 4 closes the two acceptance lines of #263 that needed code or a test. The third open
line, the cost table in `docs/audits/rt-performance.md`, moved to TASK243 (#835), which
measures the RT cost tables of TASK109, TASK126, TASK190 and TASK191 once in the owner's
quiet window. This runbook no longer tracks it.

### What it adds

- **`rm_fx_view_mode`** (`rm_metalfx.c`, pure). `RM_FX_Read` used to set the world views'
  mode from `rm_fx_run(got, 0, fx_has)`, with `got` being
  `rm_fx_supported(rm_fx_mode_of(asked_v), fx_has)`. `rm_fx_view_mode(asked_v, has)` is
  that chain as one function, and `RM_FX_Read` calls it. `RM_FX_Begin` makes no scaler
  for `RM_FX_OFF`: it frees one left from before and draws the view at its output's size.
  Behaviour is unchanged.
- **`tests/metal/rm_fxgate_test.c`.** It links `rm_metalfx.c`, `rm_rtgate.c` and
  `rm_rtfx.c`, so the presentation's rules and the scaler's decision are tested together.
  `view_size` in the test restates `RM_FX_Begin`'s size rule: the output's size for
  `RM_FX_OFF`, else `rm_fx_sizes` at `rm_fx_scale`.
  - `test_no_scaler`: stock, with the user's `r_metalfx` at 0, -0, 0.25, 0.99, -1, -inf
    and NaN. These are tried on every one of the 32 combinations of the `RM_FX_HAS` bits,
    with and without the temporal scaler, a development view and a view below its output.
    The result is `RM_FX_OFF` and the output's size at `r_metalfx_scale` 0, 0.5 and 0.75.
    The test also checks that each mode the device has is still made when asked for, and
    that Retro reads off.
  - `test_no_temporal`: RT-primary on a device that traces rays (apple7, compute path)
    but has the spatial scaler only, or no scaler. `r_metalfx` reads 0 for every user
    value from 0 to 3, below the output or not. The view is drawn at its output's size,
    and the start-up line is
    `ref_metal: presentation: ray traced: r_metalfx reads 0 (off): no MetalFX temporal scaler on this device`.
  - `test_unsupported`: the user's `r_metalfx` 1, 2 or 3 on a device with no MetalFX
    scaler reads 0, and the view is drawn at its output's size. `RM_FX_Read`'s line is
    `ref_metal: r_metalfx 2 (temporal) is not supported on this device, it reads 0 (off)`.
  - `test_no_rt`: the VM (no Apple family, the spatial scaler only). Enhanced and
    RT-primary read stock with
    `ref_metal: presentation: stock; r_presentation 3 (ray traced) reads 0: no ray tracing on this device`
    (and its Enhanced twin). The gate prints `ray tracing: none (...); the ray-tracing
    effects read 0` and
    `ref_metal: ray tracing: none on this device: the effect scales do nothing`, and each
    effect switch prints `ref_metal: r_rt_ao reads 0: no ray tracing on this device (no
    Apple GPU family reported)`. At `r_metalfx 0` no scaler is made and the view is drawn
    at its output's size.
- **`tests/check-ref-metal.sh`** (segment 10.1) builds and runs the test with the
  sanitizers, then runs four scratch mutants. Each mutant must compile and fail, each is
  named on its own line, and all four must be refused:
  - `forceon`: `rm_fx_view_mode` asks for at least the temporal scaler, so the scaler is
    forced on;
  - `fallbackdrop`: `rm_fx_supported` no longer falls back, so a mode the device lacks
    runs;
  - `rtpclampdrop`: RT-primary's clamp to off without the temporal scaler is dropped
    (`!temporal || show` becomes `!temporal && show`);
  - `nortdrop`: the presentation no longer falls to stock without ray tracing.

### The two lines, as met

- **No scaler is created with every effect off** (restated for `r_metalfx 0`). Met by
  `test_no_scaler` and the `forceon` mutant. The gl1 half is the stock pins, unchanged in
  the holds of #742 and #775.
- **The scale reads 1 with a report line where the scaler is unsupported or the device
  has no ray tracing** (restated for `r_metalfx_scale` and `r_metalfx 0`). Met by
  `test_no_temporal`, `test_unsupported` and `test_no_rt`, and by the `fallbackdrop`,
  `rtpclampdrop` and `nortdrop` mutants.
  - A caveat, by design and left as it is: the scaler is a property of the 3D view, with
    or without ray tracing (the owner's direction of 2026-11-14). So on the VM, which has
    the spatial scaler, a user's `r_metalfx 1` or `2` still runs the spatial scaler at
    `r_metalfx_scale`, with the line `r_metalfx 2 (temporal) is not supported on this
    device, it reads 1 (spatial)`. The scale reads 1 there only at `r_metalfx 0`, which
    is how the rescope restates the line.

### Commits

| Commit | What |
|---|---|
| `63013904` | the test, red: `rm_fx_view_mode` does not exist yet, so the test does not link (`ld: symbol(s) not found`) |
| `db6e972f` | `rm_fx_view_mode` extracted and called by `RM_FX_Read`; green: `rm_fxgate_test: 2080 checks, 0 failed` |
| `604ea554` | `tests/check-ref-metal.sh` segment 10.1, with the four mutants |

### Host checks (no lock)

| Check | Result |
|---|---|
| `tests/metal/rm_fxgate_test.c` with ASAN and UBSAN | `rm_fxgate_test: 2080 checks, 0 failed` |
| segment 10.1 alone (extracted from `tests/check-ref-metal.sh`) | `scaler decision 2080 checks, 0 failed, 4 mutants refused` |
| `tests/metal/rm_fx_test.c` over the changed `rm_metalfx.c` | `rm_fx_test: 394 checks, 0 failed` |
| `bash -n tests/check-ref-metal.sh` | ok |

### The hold

The orchestrator runs this under the bench lock, from the lane's worktree. It uses no
scratch directory; everything goes to standard output.

```bash
#!/bin/bash
# TASK109 slice 4: the full checks, run under the bench lock
cd "$(git rev-parse --show-toplevel)" || exit 1
git log -1 --format='%h %s'
git status --porcelain
bash -n tests/check-ref-metal.sh || exit 1
tests/check-ref-metal.sh 2>&1 | tail -n 5
tests/check-doc-links.sh && tests/check-runbook-index.sh && tests/check-pin-chars.sh
scripts/test.sh 2>&1 | tail -n 5
```

What to read in its output:

- the check's summary names `scaler decision 2080 checks, 0 failed, 4 mutants refused`,
  and `MetalFX 394 checks, 0 failed, 3 reset mutants refused`;
- `ok` from the check and from each doc check;
- `scripts/test.sh` green.

The results go here when the hold has run.
