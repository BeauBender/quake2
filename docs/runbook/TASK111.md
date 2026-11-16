# MetalFX scalers for the 3D view in `ref_metal` (TASK111)

TASK111 makes the MetalFX scalers selectable for the world view
([decision 0011](../decisions/0011-metalfx-framework.md); the cvars are in
`doc/040_cvarlist.md`). The scaler runs at the seam of the G-buffer contract
(`docs/plans/metal-raytracing-design.md`, section 3): a mode asks for the motion path
(`RM_FX_WantsMotion`), the view is drawn at the input size at the target's origin
(`RM_FX_Begin`, from `RM_GB_Begin`), and `rmb_gbuf_view_end` hands the render-size colour,
depth and motion to `rmb_fx_upscale` in place of the bilinear stretch. The output target is
the world view's rectangle, copied into the frame at that rectangle; the 2D draws after it.
Every decision is a pure function in `rm_metalfx.c`, held by `tests/metal/rm_fx_test.c`.

Host: this VM, `Apple Paravirtual device` (`apple5` only, no ray tracing), macOS 27.2.1, SDK
27.2. The first runs were of engine commit `27fd81fd` on `origin/main` at `33845c6c`; after
the review round the branch has `origin/main` at `9039f602` (TASK129) merged, and the pins
below were recorded again at `a8e0dd77` against a base built at `9039f602`. Every log,
script and result is in `<artifacts>/TASK111/`; the pin store is a scratch one.

**What `supportsDevice:` reports here** (three runs of a probe, and the renderer's own line
at every start, `ref_metal: MetalFX supportsDevice: ...`):

| Descriptor | This VM | Mode |
|---|---|---|
| `MTLFXSpatialScalerDescriptor` | YES (a 320 x 240 to 640 x 480 `BGRA8Unorm` scaler is made) | `r_metalfx 1` |
| `MTLFXTemporalScalerDescriptor` | NO (it reports an input content scale of 1 to 3 all the same) | `r_metalfx 2` |
| `MTLFXTemporalDenoisedScalerDescriptor` | NO (1 to 3 as well) | `r_metalfx 3` |
| `MTLFXFrameInterpolatorDescriptor` | NO | `r_metalfx_interp 1` |

So `r_metalfx_modes` reads 3 here. The M6 reported YES for all four (design section 8).

```sh
export PINS_STORE=<scratch store> PINS_BASEDIR=<game data> MTL_DEBUG_LAYER=1
make client game ref_metal
# the base: a copy of the tree at the merge base built outside the checkout, run as PINS_BIN
PINS_BIN=<copy>/release/quake2 scripts/pins.sh record metal <s>    # PINS_SEED=1 for the seeded ones
scripts/pins.sh record metal <s>
scripts/pins.sh compare metal <s> --ref metal --ref-id <base id> --cand-id <id> --threshold 0 --bad-threshold 0
cmp <base frame> <frame>                                            # every frame
```

**Defaults: every scenario against the base** (`r_metalfx 0`, `r_metalfx_interp 0`; at
`a8e0dd77` against `9039f602`, and the same at `27fd81fd` against `33845c6c`):

| Scenario | Result |
|---|---|
| `world`, `world-fullbright`, `world-lmcubic`, `world-scale8`, `hud2d`, `hud2d-sharp`, `hud2d-scale8`, `skywater` | 23 of 23, worst 0, bad 0; 23 byte-identical each |
| `models`, `particles`, `particles-cap`, `dlights`, `dlights-lmcubic` (`PINS_SEED=1`) | 23 of 23, worst 0, bad 0; 23 byte-identical each |
| `cinematic` (`idlog.cin`, every 7th) | 12 of 12, worst 0, bad 0; 12 byte-identical |

Each `engine.log` holds `Metal API Validation Enabled` (the layer's own line, on the stream
the check greps, so a validation message would land there too), no other validation line, no
failed command buffer, and `teardown: 0 live objects (... rt 0/0, metalfx 0/0)`: no MetalFX
object is made with the defaults. The supportsDevice line asks the descriptors' class
methods only.

**The spatial scaler on this VM** (`r_metalfx 1`, scratch scenarios `<s>.cfg` plus `set
r_metalfx 1`, `set r_metalfx_scale <k>`, `set metal_stats 1`, deleted after; `models` and
`particles` seeded): `world`, `models` and `particles` at `r_metalfx_scale` 0.5, 0.75, 0.34
and 0 (the default, which is 1 at a 480-line output) each draw all 23 frames, validation
clean, `teardown: 0 live objects` with `metalfx 2/2` (the scaler and its output target), and
the `metal_stats` line, for example `metalfx spatial: input 320x240, output 640x480 at 0,0,
reset first, scaler not timed` on the first view and `reset none, scaler 0.170 ms` after.
The input sizes were 320 x 240, 480 x 360, 218 x 163 and 640 x 480 at `27fd81fd`. The review
round rounds an input side up, so the output-to-input ratio never passes the clamp: at
`a8e0dd77` the 0.34 run of `world` takes 218 x 164 (ratio 2.94 at most), with the same
validation, teardown and ledger. The scaler's GPU time,
from the 11 timed lines of each run (not a quiet window, a VM's paravirtual GPU, one run
each: a range, not a measurement for the design): 0.000 to 0.555 ms, medians 0.096 to
0.329 ms.

**Fallbacks** (the committed scenarios at `a8e0dd77`, no `metal_stats`, compared pixel by
pixel, since the TGA header names the scenario, so the files differ even where the pixels
do not; `world-metalfx1` at `a8e0dd77` is also 23 of 23 pixel-identical to its record at
`27fd81fd`):

- `world-metalfx2`: `ref_metal: r_metalfx 2 (temporal) is not supported on this device, it
  reads 1 (spatial)`; 23 of 23 frames pixel-identical to `world-metalfx1`'s.
- `world-metalfx3`: `r_metalfx 3 (temporal denoised) is not supported on this device, it
  reads 1 (spatial)`; 23 of 23 pixel-identical to `world-metalfx1`'s.
- `world-metalfx-interp`: `r_metalfx_interp 1 is not supported on this device, it reads 0`;
  23 of 23 pixel-identical to `world`'s, `metalfx 0/0`.

Here the best mode below 2 and 3 is 1, not 0, so their frames are the spatial scaler's;
the interpolator's fallback is the case whose frame equals the off one. The line is
printed when the renderer starts, so a map load clears it from the notify lines and it
is not in the frames. A first version printed it at the first view, and every frame that
showed it differed.

**Reset events** (a live session, `metal_stats 2`, which prints the line on every scaled
view, `r_metalfx 1`, `r_metalfx_scale 0.5`, marker lines between the events;
`<artifacts>/TASK111/resets.log`):

```
alias w "wait;wait;wait;wait;wait;wait;wait;wait;wait;wait"
alias w50 "w;w;w;w;w"
map base1                        // reset first
teleport 128 -219 36             // 100 units: no reset
teleport 1128 -219 36            // 1000 units: reset cut
teleport 128 -319 36             // reset cut
viewsize 80                      // reset size rect: input 256x192, output 512x384 at 64,48
viewsize 100                     // reset size rect
r_metalfx_scale 0.75             // reset size: input 480x360
fov 30                           // no reset
menu_playerconfig                // the model view over the world: no reset
forcemenuoff
r_metalfx 0, then r_metalfx 1    // reset first on the first scaled view
r_metalfx 2                      // reads 1 with its line, no reset
r_metalfx_interp 1               // reads 0 with its line
vid_restart                      // teardown 0 live objects, metalfx 10/10; then reset first
map base2                        // reset first
quit                             // teardown 0 live objects, metalfx 2/2; status 0
```

(`w50` between the steps.) Every expectation held, on the frame of the event; the 1,303
other lines say `reset none`. The same run at `a8e0dd77`, where the step is `rm_fx_step` and
`rm_fx_frame_end`, gave the same reset lines (`resets2.log`). A change of mode between two scalers (`reset mode`) cannot
happen on this VM, which has one; the host test holds it.

**A known edge** (from the bare-metal host): with a scaled view the smooth-point particle
cap of 256 pixels applies in render pixels (`rm_gb_point_scale`, TASK54), so a particle
above 256 frame pixels is drawn smaller than its stock size. Reaching it needs
`gl1_particle_max_size` above 256. The cap is not changed; `doc/040_cvarlist.md` says so
under `r_metalfx`.

**Mutants.** 112 mutants of `rm_metalfx.c` (`<artifacts>/TASK111/mutants.py`, one text
replacement each, built with ASAN and UBSAN, run with a 20 s bound), over every function and
every operand of its compound guards, the review round's included (the rounding of a side,
`rm_fx_same_size`, `rm_fx_step`, `rm_fx_frame_end`, the refusal memo, `rm_fx_note_format`):
109 fail `tests/metal/rm_fx_test.c` (by a failed check, a sanitizer, or the bound:
`rm_fx_halton` with base 1 loops). Two survivors were missing cases and have them now: `v <
0` as `v < -1` in `rm_fx_mode_of` (`-1` truncates to `-1`), and the floor of a side lowered
to 0 (a product inside the rounding slack). The refusal memo without its mode (review I1)
fails 2 checks. Three are equivalent:

- `m < 0` as `m < -1` in `rm_fx_supported`: the loop never runs for `m` at or below 0;
- `m > RM_FX_OFF` as `m >= RM_FX_OFF` there: bit 0 gives off, as the end does;
- the scale's finite test dropped in `rm_fx_sizes`: `!(scale > 0)` takes a NaN, and an
  infinite scale gives the output's size, as 1 does.

The host test failed 141 of its 241 checks against a stub first.

## On the M6 and an M1-family Mac (the card's T2 and T1 items)

The card stays open for these. Screen unlocked, nothing else running, a quiet window for
the timings. Expected first line at start, from the M6's section 8 probe:
`ref_metal: MetalFX supportsDevice: spatial yes, temporal yes, temporal denoised yes, frame
interpolator yes (r_metalfx_modes 31)`; on the M1, record what it prints.

```sh
export PINS_STORE=<scratch store> PINS_BASEDIR=<game data> MTL_DEBUG_LAYER=1
make client game ref_metal
# defaults against the base, as above: every scenario score 0, bad 0, metalfx 0/0
# each supported mode at three scales: scratch scenarios, deleted after
for s in world models particles; do for m in 1 2 3; do for k in 50 75 34; do
  { cat scripts/pins/scenarios/$s.cfg; printf 'set r_metalfx %s\nset r_metalfx_scale 0.%s\nset metal_stats 1\n' $m $k; } \
    > scripts/pins/scenarios/$s-fx$m-$k.cfg
  PINS_SEED=1 scripts/pins.sh record metal $s-fx$m-$k
  rm scripts/pins/scenarios/$s-fx$m-$k.cfg
done; done; done
```

Expected in each `engine.log`: `Metal API Validation Enabled` and no other validation line;
for `r_metalfx 2` lines `metal_stats: metalfx temporal: input <w>x<h>, output 640x480 at
0,0, reset first, ...` then `reset none, scaler <t> ms`; for `r_metalfx 3`, once,
`r_metalfx 3 (temporal denoised) runs without a ray-traced effect as 2 (temporal)` and the
temporal lines; at the end `teardown: 0 live objects` with `metalfx 2/2`. The temporal
scaler is new to this card on real hardware: the VM could not run it.

- *The jitter's sign.* `jitterOffsetX` and `jitterOffsetY` take the jitter as applied to the
  projection (`rm_gb_jitter`: +x right, +y down, in input pixels), by MetalFX's description
  ("the pixel offset this scaler samples to return to the frame's reference frame"). This
  is the one assumption the VM could not check. A still view (the live script's `map base1`
  and 300 frames without input, `r_metalfx 2`, scale 0.5) must settle to a still, sharp
  image. Edges that shimmer by about a pixel mean the sign is wrong: negate both in
  `rmb_fx_upscale`.
- *The reset events.* The live script above, with `r_metalfx 2`; add `r_metalfx 1` then
  `r_metalfx 2` for `reset mode`. Expected as on the VM.
- *GPU time per mode and scale.* With `metal_stats 1` the scaler's pass runs in a command
  buffer of its own and the line gives its GPU time. Per host, mode and scale: three runs of
  `particles` at `PINS_HIDPI=1` and the native size, frames 101 on. Record the median and
  range of the lines here, as a range.
- *Frame generation* (`r_metalfx_interp 1`) is not built: it reads 0 with `r_metalfx_interp
  1 has no frame generation in this build, it reads 0`. Its acceptance items wait for the
  card that builds it.

## The scaler's GPU time per mode and scale on the M6 (the card's T2 item)

Measured at engine commit `c9ecf7d7` (`origin/main`; only this file changes after it), on the
bare-metal Apple M6 (12-core GPU), macOS 27.2.1, release build, Metal validation layer off
(`MTL_DEBUG_LAYER` unset: the layer's cost is not part of a timing). Logs and the analysis
script are in `<artifacts>/TASK111/gputime/`.

**Method.** One run is one engine start of `+demomap demo1.dm2` at the pace the engine sets
(`timedemo 0`, vsync), about 14 s, ended by SIGTERM (the teardown still runs), with the
`world` scenario's settings (`viewsize 100`, no entities, particles, lights, blend, gun or
crosshair), `r_metalfx <m>`, `r_metalfx_scale <k>` and `metal_stats 2`. `metal_stats 2` prints
the line on every scaled view, where 1 prints one in 60 (`RM_FX_STATS_EVERY`); both turn the
timing on, and the figure is the same `scaler <t> ms` field. The first 60 lines of a run are
skipped, which leaves 578 to 722 timed views per run (the `not timed` line of a first view
is never counted). Each run took the timing lock, with no other `quake2` running and the
screen unlocked; the runs were alternated (round 1, 2, 3 over every cell, never three in a row
of one cell). Three runs per cell, and five for the three spatial 5K cells, whose runs split in
two groups (below). 1080 is windowed at 1920 x 1080 with `vid_highdpiaware 0`; 5K is
fullscreen at 5120 x 2880 with `vid_highdpiaware 1`, the display's native size.

**Load.** `vm.loadavg` (1 minute) read 2.1 to 7.0 before or after a run, mostly 2.2 to 4.4:
other lanes' builds and engine runs shared the host, so this is not an idle window. The
timing lock only kept the engine runs apart. Read the table as a range on a busy host.

The columns: the median of the three (five) run medians with the range of the run medians in
brackets; the median of the run 90th percentiles with its range; the smallest and largest
line of all the runs; the runs; the 1-minute load average across them. Times are in ms.

| Output | `r_metalfx` | Scale | Input | Median (run range) | p90 (run range) | Min to max | Runs | Load |
|---|---|---|---|---|---|---|---|---|
| 1920x1080 | 1 spatial | 0.75 | 1440x810 | 0.92 (0.91 to 0.92) | 1.02 (1.02 to 1.03) | 0.79 to 1.88 | 3 | 2.2 to 3.4 |
| 1920x1080 | 1 spatial | 0.5 | 960x540 | 0.83 (0.80 to 0.84) | 0.89 (0.88 to 0.90) | 0.72 to 1.80 | 3 | 2.5 to 3.4 |
| 1920x1080 | 1 spatial | 0.34 | 653x368 | 0.81 (0.81 to 0.81) | 0.86 (0.86 to 0.86) | 0.71 to 1.40 | 3 | 2.2 to 5.3 |
| 1920x1080 | 2 temporal | 0.75 | 1440x810 | 1.43 (1.42 to 1.43) | 1.50 (1.49 to 1.53) | 1.01 to 2.51 | 3 | 2.4 to 5.1 |
| 1920x1080 | 2 temporal | 0.5 | 960x540 | 0.98 (0.98 to 0.98) | 1.05 (1.04 to 1.05) | 0.82 to 1.83 | 3 | 2.4 to 5.3 |
| 1920x1080 | 2 temporal | 0.34 | 653x368 | 0.80 (0.79 to 0.80) | 0.84 (0.84 to 0.85) | 0.69 to 1.51 | 3 | 2.2 to 4.4 |
| 1920x1080 | 3 (runs as 2) | 0.75 | 1440x810 | 1.41 (1.24 to 1.43) | 1.50 (1.39 to 1.52) | 1.00 to 2.15 | 3 | 2.2 to 3.8 |
| 1920x1080 | 3 (runs as 2) | 0.5 | 960x540 | 0.97 (0.88 to 0.98) | 1.04 (0.91 to 1.04) | 0.82 to 1.77 | 3 | 2.2 to 4.0 |
| 1920x1080 | 3 (runs as 2) | 0.34 | 653x368 | 0.78 (0.75 to 0.79) | 0.83 (0.83 to 0.84) | 0.68 to 1.46 | 3 | 2.2 to 3.8 |
| 5120x2880 | 1 spatial | 0.75 | 3840x2160 | 5.60 (1.65 to 6.55) | 7.21 (1.70 to 7.27) | 1.64 to 7.59 | 5 | 2.2 to 4.3 |
| 5120x2880 | 1 spatial | 0.5 | 2560x1440 | 5.08 (4.91 to 6.20) | 6.70 (5.33 to 6.92) | 1.52 to 7.29 | 5 | 2.1 to 3.4 |
| 5120x2880 | 1 spatial | 0.34 | 1741x980 | 4.98 (4.93 to 6.06) | 6.72 (6.71 to 6.76) | 3.51 to 7.18 | 5 | 2.1 to 4.3 |
| 5120x2880 | 2 temporal | 0.75 | 3840x2160 | 5.46 (5.30 to 5.47) | 5.82 (5.70 to 5.83) | 4.29 to 6.70 | 3 | 2.3 to 7.0 |
| 5120x2880 | 2 temporal | 0.5 | 2560x1440 | 5.02 (4.80 to 5.04) | 5.15 (5.13 to 5.18) | 4.17 to 5.68 | 3 | 2.3 to 7.0 |
| 5120x2880 | 2 temporal | 0.34 | 1741x980 | 4.99 (4.94 to 4.99) | 5.09 (5.04 to 5.10) | 2.26 to 5.34 | 3 | 2.2 to 6.3 |
| 5120x2880 | 3 (runs as 2) | 0.75 | 3840x2160 | 5.43 (5.30 to 5.43) | 5.78 (5.53 to 5.81) | 4.52 to 6.57 | 3 | 2.2 to 4.8 |
| 5120x2880 | 3 (runs as 2) | 0.5 | 2560x1440 | 4.99 (4.96 to 5.02) | 5.14 (5.14 to 5.15) | 4.16 to 5.90 | 3 | 2.2 to 5.6 |
| 5120x2880 | 3 (runs as 2) | 0.34 | 1741x980 | 4.81 (3.60 to 4.96) | 5.04 (4.87 to 5.11) | 2.19 to 5.29 | 3 | 2.2 to 4.3 |

Every run: no failed command buffer in any stats line, and no validation text (the layer was
off). `r_metalfx 3` printed `r_metalfx 3 (temporal denoised) runs without a ray-traced effect
as 2 (temporal)` once and reported `metalfx temporal`: no ray-traced effect exists yet, so
the mode 3 rows are the temporal scaler again, and their differences from mode 2 are the
noise of the run (compare `0.97` and `0.98`).

**What the figures say.**

- At 1920 x 1080 the pass takes 0.7 to 1.5 ms, repeatable to a few percent between runs. It
  falls with the scale, where the input is smaller: the spatial scaler from 0.92 ms at 0.75 to
  0.81 ms at 0.34, the temporal one from 1.43 to 0.80 ms. The temporal scaler costs more than
  the spatial one at the larger inputs (1.43 against 0.92 ms at 0.75) and about the same at 0.34.
- At 5120 x 2880 the pass takes about 5 ms, and the scale matters little (5.46, 5.02 and 4.99
  ms for the temporal scaler): the cost is set by the output size, not the input. At 60 Hz a
  frame is 16.7 ms, so the scaler is about a third of it at the native 5K size. This is the
  number to weigh against the saving in the world draw at the smaller input.
- The spatial 5K runs are not repeatable: they fall in two groups, run medians of 1.65 ms (one
  run of 0.75) and 4.9 to 6.6 ms (the others), with the p90 at 6.7 to 7.3 ms. The temporal
  runs are tight (p90 within 0.1 ms between runs). The 1.65 ms run was in a lighter-loaded
  stretch (load 2.4), but runs of the same load gave 5 ms, so the cause is not established:
  GPU clock changes and the other lanes' GPU work are both candidates. The upper group is the
  figure to plan with, the 1.65 ms group is what the pass can reach when the GPU is not
  shared or throttled.
- The temporal 0.34 row at 5K has a smaller lower end (2.26 ms), and one mode 3 run has a
  median of 3.60 ms: the same effect, seen less often.

This item is done for the M6. The M1-family Mac still needs the same table: run the same
script on its host (the timing there is likely to differ more than the modes do).
