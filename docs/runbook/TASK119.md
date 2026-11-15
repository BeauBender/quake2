# Crisp 2D in `ref_metal` (TASK119)

Three switches, all off by default (the plan, [P4](../plans/high-resolution.md)):
`r_scale8bittextures` as gl1 takes it, `metal_2d_sharp` (sharp-bilinear
sampling of the filtered 2D pictures) and the client's `con_backaspect` (the
console background at 4:3, cropped). Host: the build VM (a paravirtualised
Metal device), base `1701a052`; `MTL_DEBUG_LAYER=1` on every `metal` record.

**Host tests** (`tests/metal/rm_host_test.c`, `tests/unit/test_cl_conback.c`).
`mtl_sharp_coord` runs the shader's own formula: at every integer scale 1
to 8 the sample at each pixel centre of a bilinear sampler equals the nearest
texel's value; a texel centre keeps its coordinate; an edge is the half-way
blend; at one texel per pixel or fewer it is the plain bilinear coordinate;
a zero, negative or NaN derivative leaves it alone; at the non-integer scales
1.5, 2.5, 3.7, 5.25 and 7.9 only pixels within half a pixel of an edge are
blended. `rm_scale8_factor`, `rm_scale8_dims` (gl1's factor and its overflow
check, at the limit and one past it) and `rm_2d_sharp` are pinned, and
`Con_BackRect` (the console background's rectangle, in `cl_conback.c`
because `cl_console.c` cannot be linked into the unit binary) at 4:3, wide,
narrow, degenerate and int-limit sizes. The shader function
`rm_fragment_2d` is MSL and has no host test; the pins and stills below are
its check.

**Mutants** (each operand of every new pure function and compound guard):
host functions 46, 44 killed, 2 survive: `d >= 1` for `d > 1` in
`mtl_sharp_coord` (equivalent: both give 1) and `h < 0` for `h <= 0` in
`rm_scale8_dims` (a division by zero that arm64 answers with 0, so it is
equivalent there and undefined behaviour elsewhere; the check's UBSAN build
is the guard). `Con_BackRect` 34, 33 killed, 1 equivalent (`>=` for `>` at
exactly 4:3, where both branches give the same rectangle). Two first-run
survivors of `Con_BackRect` (a guard that could not fire, a fallback of a
side that was already right) led to the simpler code, and the exact-limit
case was added for a third; `rm_2d_sharp` first had redundant index bounds
whose mutants survived, and is now the two sampler values it accepts.

Rework mutants (round 1): `mtl_sharp_coord` (the offset of the blend, the
shape of the ramp, the `d` clamp) and the over-size case, 6 mutants, 6 killed;
the monotonic check now runs and a slow decrease of 2e-5 per texel is killed
(together with the other checks, which also fire). The scrap smoothing:
removing the call is killed (`hud2d-scale8` against `gl1-metal` then differs
by 55 bytes), making its guard always true is killed (`hud2d-scale8` then
equals `hud2d`). The fallback of `R_LoadPic` to an unscaled upload is now the
pure `rm_scale8_plan` (drop, unscaled or scaled), pinned in `test_scale8`:
(3000, 3000) unscaled, (320, 240) scaled to 640x480, the overflow dropped,
the cvar off unscaled, the limit exactly and one under. Round 2 mutants over
its guards, 9 in all, all killed except one equivalent (the explicit zeroing of
the sides on the drop path, which `rm_scale8_dims` already does; the line was
removed): the factor guard (never, or also at 2), the dims guard, the
staging guard (off, inverted, the unscaled sides not restored), the `nolerp`
and `enabled` operands. Not killed by any committed test: `vid_loaded_renderer`
(a name set and cleared in `vid.c`) and the call site in `R_LoadPic`
(which needs a picture of that size; the helper it calls is tested).

**Default pins unchanged.** The reference is a record of `metal` built at
`1701a052`; the candidate is this branch with every switch at its default,
`--threshold 0 --bad-threshold 0`, 640x480, `PINS_SEED=1` for `dlights`,
`models` and `particles`, `idlog.cin` every 7th frame for `cinematic`:

| Scenario | Frames | Failed | Worst score | Worst bad |
|---|---|---|---|---|
| `hud2d` | 23 | 0 | 0 | 0 |
| `world` | 23 | 0 | 0 | 0 |
| `skywater` | 23 | 0 | 0 | 0 |
| `dlights` (seed 1) | 23 | 0 | 0 | 0 |
| `models` (seed 1) | 23 | 0 | 0 | 0 |
| `particles` (seed 1) | 23 | 0 | 0 | 0 |
| `cinematic` | 12 | 0 | 0 | 0 |

**`r_scale8bittextures 1` against gl1.** `world-scale8` and `hud2d-scale8`
from `metal` against `gl1-metal` (`make client game ref_gl1 ref_metal
WITH_METAL_SHIM=yes`) with the same setting, `--threshold 0.005
--bad-threshold 0.001`: `world-scale8` at 640x480 (factor 2) 23 frames, 0
failed, worst score 0 (a frame has 5 pixels that differ, by at most 4 of
255); at 1280x720 (factor 3) 23 frames, 0 failed, worst 1e-06, worst bad
9e-06. The setting acts: a `world-scale8` frame differs from a `world`
frame in 125,000 pixels of 307,200. `hud2d-scale8` is smoothed, not
scaled: the HUD pictures are in the scrap, and gl1 smooths the scrap copy
(`Scrap_Update` reaches `R_Upload32`), as `rm_scrap.c` now does (the
first version of this branch did not, and the comparison at 0.005 could not
see a step of 2 in a channel). Rework: `hud2d-scale8` from `metal` against
`gl1-metal` at `--threshold 0 --bad-threshold 0`, 23 frames, 0 failed,
worst 0, worst bad 0; and `metal` `hud2d-scale8` against `metal` `hud2d`
differs in all 23 frames, 55 bytes in all, the same 55 as `gl1-metal`'s
pair. The console background and the menus are the other pictures it
changes, and no pin plays them (a scenario is a list of `set` lines, and
the demo's console is shut), so those are stills.

**`metal_2d_sharp 1`.** At 2560x1440 (this VM opens it; the window is the
drawable) the 2D scale is 4, an integer: all 23 frames of `hud2d-sharp`
equal the same demo with `r_2D_unfiltered 1` (every 2D picture nearest)
pixel for pixel, and differ from `hud2d` (39,627 pixels in frame 10). The
record against itself (`--run 2`): 23 frames, score 0, bad 0. The console
background (320x240 over 2560x1440, 8 by 6, also an integer scale) equals
the nearest still but for the 576 pixels of the clock. A switch that does not
apply is not hidden: the cvar is the `ref_metal` one, the other renderers do
not register it, and there is no menu item for it.

**`con_backaspect 1`.** At 1024x768 (4:3) a still with the full console equals
the default but for the clock (46 pixels in a 15 by 6 box at the bottom
right); at 1920x1080 it differs (the picture is 1920x1440, anchored at the
console's lower edge, so the top 360 lines are cropped). The pin harness
cannot play the console, so `hud2d` with the console down is a still, taken
at the disconnected full console (`+wait` and `screenshot tga`, the config
reset between runs: the switches archive).

**Validation reaches the log**: a scratch build without the fragment bytes
the 2D draw now binds aborted a start under `MTL_DEBUG_LAYER=1`
(`-[MTLDebugRenderCommandEncoder validateCommonDrawErrors:]` ... `failed
assertion`); the edit was reverted. The records here (`engine.log`) hold
only `Metal API Validation Enabled`.

**Not measured here.** The bare-metal M1-family Mac at 5K and the cost of
the sharp sample (one `fwidth` and a few operations a fragment, in 2D only).
