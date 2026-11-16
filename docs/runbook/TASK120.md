# Bicubic lightmaps in `ref_metal` (TASK120)

`metal_lightmap_filter 1` filters each lightmap sample of the lightmapped
world with a cubic B-spline built from four bilinear taps, every tap clamped
half a luxel inside the face's rectangle of the slice (the plan,
[P5](../plans/high-resolution.md)). The default `0` is the bilinear tap as before.
Host: the build VM (a paravirtualised Metal device), at this card's base
`c01bd7d2` and the branch.

**Host test.** `tests/metal/rm_host_test.c` runs the shader's own formulas
(`mtl_shader_math.h`): the weights are positive and sum to 1; the two taps
of an axis weigh 1 together; away from an edge the four taps equal the
direct 4 by 4 sum; a constant lightmap stays constant; no result leaves the
range of its 4 by 4 neighbourhood of luxels (clamped to the rectangle) over
20,000 random samples including a margin past the edges; no luxel outside
the rectangle carries weight at an edge, a corner or past it, for six
rectangles including ones one luxel wide in one axis; a rectangle narrower
than a luxel is total (both taps at its low bound plus a half); and
`mtl_lm_rect_valid` is what tells the shader that a vertex has no
rectangle. Mutants of every new pure function (each operand of the
compound guards and each clamp bound): 88 run, 76 killed outright, 9 the
compiler refused for an unused operand (re-run in a form that compiles: all
9 killed) and 3 equivalent (`v <= lo` and `v >= hi` in `mtl_clamp_range`,
and `>=` for `>` in the guard of the upper bound, each differing only where
both branches give the same value). Two mutants of the first run survived
(the upper bound without its guard against a rectangle narrower than a
luxel, in two spellings); the narrow-rectangle cases were added for them and
kill both. The shader
function `rm_lm_sample_cubic` itself has no host test (it is MSL); the
pins below and the validation layer are its check.

**Pins at `metal_lightmap_filter 0`** (`MTL_DEBUG_LAYER=1`, 640x480, every
23 frames of the record; the reference is a record of `metal` built at
`c01bd7d2`, named with `--ref-id`; `dlights`, `models` and `particles` seeded
with `PINS_SEED=1` and the trace on, one seed for the reference and the
candidate; `--threshold 0.005 --bad-threshold 0.001`):

| Scenario | Frames | Failed | Worst score | Worst bad |
|---|---|---|---|---|
| `world` | 23 | 0 | 0 | 0 |
| `skywater` | 23 | 0 | 0 | 0 |
| `dlights` (seed 1) | 23 | 0 | 0 | 0 |
| `models` (seed 1) | 23 | 0 | 0 | 0 |
| `particles` (seed 1) | 23 | 0 | 0 | 0 |

The records have no validation message. The vertex grew by a member and
the vertex output by a flat one, and no pixel moved.

**At value 1** (rework round: the scenarios `world-lmcubic` and
`dlights-lmcubic`, `world` and `dlights` plus `set metal_lightmap_filter 1`,
are in the tree; `MTL_DEBUG_LAYER=1`, 640x480, `dlights-lmcubic` with
`PINS_SEED=1` and the trace on). Run against run (`--threshold 0
--bad-threshold 0`): `world-lmcubic` 23 frames, 0 failed, worst 0, bad 0;
`dlights-lmcubic` (seed 1) the same. Against the filter-0 record of
`world` (the frames copied under the scenario's frame names, since a
compare pairs frames by name; `--threshold 0.005 --bad-threshold 0.001`):
all 23 frames differ (score 0.000345 to 0.002014, so the worst is above 0),
bad at most 0.002816, 3 of 23 over the bad value. `dlights-lmcubic` against
the seed-1 `dlights` record: score 0.000345 to 0.002014, bad at most
0.001940, 2 of 23 over. A filter-1 record that is not the filter-0 one is
what the scenarios are for; the owner does not gate the look.
**Validation reaches the log**: a scratch build with the index buffer
offset of the lightmapped draw made 2 bytes odd aborted `world-lmcubic`
under `MTL_DEBUG_LAYER=1` (status 134, `engine.log`:
`-[MTLDebugRenderCommandEncoder validateDrawIndexedPrimitives:...]: failed
assertion`); the edit was reverted. The real records (7 `engine.log`)
hold only `Metal API Validation Enabled`.

**GPU time (VM numbers).** A measuring aid, `MTL_GPU_TIME=1` in the
environment, prints one line `ref_metal: gpu_ms <ms>` per command buffer
that completes (`GPUEndTime - GPUStartTime`, from `rmb_watch`). A frame is
one command buffer, so it is the 3D pass and the 2D pass of a frame; the
`world` scenario draws no entities and almost no 2D, so the 3D pass is
most of it. Five runs of each value, alternated, the `world` scenario's
engine command line (`PINS_DRYRUN=1 scripts/pins.sh record metal world`
prints it) with `r_vsync 0`, `pins_capture 100000` (so nothing is read
back), 715 command buffers a run; each run's median and 90th percentile:

| Size | Value | Median (ms), range of 5 runs | 90th percentile (ms), range |
|---|---|---|---|
| 1280x720 | 0 | 1.042 to 1.063 | 1.270 to 1.298 |
| 1280x720 | 1 | 1.037 to 1.074 | 1.263 to 1.370 |
| 1920x1080 | 0 | 1.102 to 1.186 | 1.353 to 1.653 |
| 1920x1080 | 1 | 1.062 to 1.099 | 1.295 to 1.357 |

The two values are not told apart on this host: the ranges overlap, and the
frame time does not follow the pixel count either (the paravirtualised
device is not shading-bound at these sizes), so no cost is read from these
numbers.

**On the bare-metal M1-family Mac, 2560x1600 (open; the card stays open for
it).** Build `client game ref_metal`, then for value in `0` and `1`, five
runs each, alternated, from the repository root (the engine ends itself
after the demo; `PINS_BASEDIR` is the game data):

```sh
mkdir -p "${PINS_STORE:?}/gpu"
MTL_GPU_TIME=1 release/quake2 -datadir "${PINS_BASEDIR:?}" -portable \
  +set timedemo 1 +set scr_conspeed 99999 +set cl_showfps 0 +set viewsize 100 \
  +set r_drawworld 1 +set crosshair 0 +set cl_gun 0 +set cl_entities 0 \
  +set cl_particles 0 +set cl_lights 0 +set cl_blend 0 \
  +set vid_renderer metal +set vid_fullscreen 0 +set r_mode -1 \
  +set r_customwidth 2560 +set r_customheight 1600 +set r_vsync 0 \
  +set s_initsound 0 +set metal_lightmap_filter VALUE \
  +set pins_dir "${PINS_STORE:?}/gpu" +set pins_scenario world +set pins_capture 100000 \
  +demomap demo1.dm2 2> "${PINS_STORE:?}/gpu/VALUE.log"
grep gpu_ms "${PINS_STORE:?}/gpu/VALUE.log" | awk '{print $3}' | sort -n \
  | awk '{a[NR]=$1} END {print NR, a[int((NR+1)/2)], a[int(NR*0.9)]}'
```

The printed line is the command buffers, the median and the 90th
percentile in milliseconds; record the range of the five medians per value,
and the host's model, display mode and macOS build. A window of 2560x1600
needs a display that holds it (windowed); if it does not fit, say which size
ran. The VM's two values being equal, the expected finding is a cost below
the noise; a median that rises by more than a tenth of a millisecond at 1
is a finding to report on the card.
