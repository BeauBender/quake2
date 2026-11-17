# `r_rt_water_hits 1` with `r_metalfx 2` at 5120 x 2880: why its records differ (FIX96)

FIX96 follows [TASK162](TASK162.md) (*Setting 1*, `rtw-h1fx`), which found this case the only one
whose records differ run to run, and builds on [TASK158](TASK158.md) (the world-space path) and
[FIX81](FIX81.md) (the pass-over loop). The scalers are [TASK111](TASK111.md)'s; the harness is
[doc/100_renderpins.md](../../doc/100_renderpins.md) (its *Determinism* section). Base: FIX92's
branch at `1551a377`, which holds TASK162.

## Verdict

**Neither a race in the hits-1 path nor the temporal scaler's own state: the engine's input differs.**
On the frame after each capture, `refdef_t.time` differs between two live records by 1 to 5 ms:
the differing frames follow the capture cadence and records at 1920 x 1080 and below repeat, so the
capture at this size (the readback and a 44 MB TGA) is the stall that takes that frame past 100 ms,
after which the timedemo clock follows the host (the caveat *Determinism* already gives for
`skywater`; the stall itself was not timed, the screen being locked). That
frame is never captured, so without a temporal scaler the records repeat; the temporal scaler's
history carries it into every later frame, the captured ones included. It does not depend on
`r_rt_water_hits` (setting 0 varies the same way), nor on the water at all (`r_rt_water 0` and
`world-metalfx2` vary too). With the time rounded to the server frame in a scratch build, five
records repeat byte for byte.

## Design note: the runs and what each decides

Every run: the screen locked (frames only, no timing), `MTL_DEBUG_LAYER=1`, `PINS_SEED=1`,
FIX81's `water.dm2`, `PINS_EVERY=30`, 2560 x 1440 with `PINS_HIDPI=1` (5120 x 2880, as TASK162),
one `tools/heavy.sh` hold a batch, no other `quake2`. A scratch copy of the harness adds the cases as
scenarios: `rt-water.cfg` plus `r_rt_water_hits` and `r_metalfx`. Each case is recorded five times
(`--run 1` to `5`), and every pair of the five is compared frame by frame (the differing pixels'
share and the largest level difference). Every log must hold the validation banner and no other
validation line.

| Case | `r_rt_water_hits` | `r_metalfx` | Decides |
|---|---|---|---|
| `h1fx2` | 1 | 2 (temporal) | the defect reproduces on this base |
| `h1fx0` | 1 | 0 | hits 1 varies without any scaler: a race or an uninitialised read |
| `h1fx1` | 1 | 1 (spatial) | the spatial scaler alone: a scaler with no history |
| `h0fx2` | 0 | 2 | the control, byte-identical in TASK162 |

- If `h1fx0` or `h1fx1` vary, the hits-1 path itself does: the marks, the compaction's list and
  count, the indirect dispatch and the trace's outputs are read for a race or an uninitialised read
  (the shader validation layer, `MTL_SHADER_VALIDATION=1`, if this host has it; a run that clears
  the reflection target and the list each frame).
- If only `h1fx2` varies, the input that differs is found by hashing each of the temporal scaler's
  inputs (colour, depth, motion, the jitter, the reset flag, the exposure, the reactive mask) per
  frame in two runs and comparing them.
- `h0fx2` repeating, as in TASK162, keeps the hits-1 path the variable.

## Runs on the bare-metal host

The base's `client`, `ref_metal` and `game`, built once and copied beside FIX81's `water.dm2`. The
screen was locked for every run (`ioreg` 1 at each), records and comparisons only; the one-minute
load was 1.9 to 2.7. Every engine log holds `Metal API Validation Enabled` and no other validation
line; every teardown has 0 live objects. Everything is in `<artifacts>/FIX96/` (`logs/runs.txt`,
`logs/step2-pairs.txt`, the probe as `probe.diff`). Frames are compared by a small C tool (per pair of
TGA frames: the differing pixels, their share, the largest channel difference), and with the
harness's own `compare`.

### Step 2: five records a case, every pair

| Case | Output | Pairs | Frames equal, each pair | Largest share | Largest level |
|---|---|---|---|---|---|
| `h1fx2`: hits 1, `r_metalfx 2` | 5120 x 2880 | 10 | 1 of 5 (frame 0) | 3.27 % | 10 |
| `h1fx0`: hits 1, `r_metalfx 0` | 5120 x 2880 | 10 | 5 of 5 | 0 | 0 |
| `h1fx1`: hits 1, `r_metalfx 1` | 5120 x 2880 | 10 | 5 of 5 | 0 | 0 |
| `h0fx2`: hits 0, `r_metalfx 2` (control) | 5120 x 2880 | 10 | 1 of 5 (frame 0) | 3.30 % | 11 |
| `w0fx2`: `r_rt_water 0`, `r_metalfx 2` | 5120 x 2880 | 3 | 1 of 5 (frame 0) | 3.94 % | 9 |
| `world-metalfx2` (no ray tracing, scale 0.5) | 5120 x 2880 | 3 | 1 of 5 (frame 0) | 2.75 % | 5 |
| `h0fx2` | 1920 x 1080 | 3 | 5 of 5 | 0 | 0 |
| `h0fx2` | 640 x 480 | 3 | 5 of 5 | 0 | 0 |

Every differing frame differs over the whole frame (box 0,0 to 5119,2879). The harness's `compare`
of records 2 to 5 against record 1 at both thresholds 0: 4 of 5 frames over, worst score 4.5e-05
(`h0fx2` 4.6e-05, `world-metalfx2` 3.8e-05), bad fraction 0 on every frame. So the control varies on
this base as the case does. TASK162's one equal pair of `h0fx2` records (main against its branch)
was, by step 3, a pair whose frames after a capture had the same time, not a property of setting 0.

### Step 3: the input that differs

Only the temporal scaler varies, so a scratch build (`probe.diff`, never committed) printed, per
view: `refdef_t.time`, the view's origin and angles (`rm_gbufscene.c`, before `RM_FX_Begin`); the
scaler's jitter, reset flag and sizes; and an FNV-1a hash of the colour, depth and motion inputs and
of the scaler's output, each blitted to a buffer in the frame's command buffer around the encode
(`rmb_metalfx.m`). Exposure is fixed (`autoExposureEnabled = NO`, no exposure texture) and no
reactive mask is passed, so there is nothing more to hash. The frame numbers are the scaler's calls,
140 a record; the frames still differed with the probe on (3.16 to 3.18 %).

| Pair (probe on) | `time` differs | origin, angles, jitter, reset, sizes | colour | depth | motion | output |
|---|---|---|---|---|---|---|
| `h1fx2`, `PINS_EVERY=30` | 63 66 96 126 | equal on all 140 | 63 66 96 126 | 63 66 96 126 | those and the frame after | every frame from 63 |
| `h1fx2`, `PINS_EVERY=20` | 6 26 46 66 83 86 103 106 123 126 | equal on all 140 | the same frames | from 66 | those and the frame after | every frame from 6 |
| `h1fx1` (spatial), `PINS_EVERY=30` | 32 62 92 122 | equal on all 140 | the same frames | not an input | not an input | (records equal) |

- The frames whose time differs are frames after a capture (calls 6, 36, 66, 96 and 126 at
  `PINS_EVERY=30`, not each of them in every pair; every 20th from 6 at `20`), and in some runs the frame two before a capture (63, 83, 103, 123), only
  once the camera is at its second, lower origin (from frame 63). The time differs by 1 to 5 ms
  (for example 13.2000008 against 13.1950006).
- Every input differs only on those frames: the colour on each, the depth and the motion only at the
  lower origin, the motion also on the frame after (it compares with the frame before). Nothing
  differs on any frame whose time is equal: no race, no uninitialised read.
- The spatial scaler's input differs on the same kind of frame, and its records repeat: the
  difference is in the engine's frame, and only a scaler with history moves it into a captured one.
- **Sufficiency:** a second scratch build rounds `r_newrefdef.time` to 0.1 s (the server frame) in
  `R_RenderViewSetup` (not a fix: it steps the warp at 10 Hz). Five `h1fx2` records: all 10 pairs
  5 of 5 equal; three `h0fx2` records: all 3 pairs 5 of 5 equal.

The shader validation layer and the per-frame clears were not needed: the inputs hash equal on every
frame whose time is equal, at both settings.

## The fix, and the marking

The time comes from the client's timedemo clock (`src/client/cl_main.c`, `CL_Frame`, and the clamp in
`CL_AddEntities`, `cl_entities.c`), outside this card's files, and the stall is the harness's own
capture (`cl_pins.c`). `ref_metal` cannot make it repeat without changing the look. So, within the
card: no source change, and the case is marked in [doc/100_renderpins.md](../../doc/100_renderpins.md)
(*Determinism*): a live record made with a temporal scaler at a size whose capture takes the next
frame past 100 ms is not compared byte for byte against another live record; it is judged at the
harness's thresholds, with the run-to-run spread measured here. `replay` takes the time from the
trace, but it takes no `PINS_HIDPI=1`, so it was not tried at this size. No scenario file changed: the case has no
committed scenario (TASK162's `rtw-h1fx` was a scratch one), and a comment line in
`world-metalfx2.cfg` would change its pin's id.

**Follow-up (outside the card's files):** keep the capture's stall out of the clock during a pin
record (`cl_main.c` or `cl_pins.c`), so a live record at any size repeats byte for byte with a
temporal scaler; `skywater`'s `// nondeterministic:` line has the same cause.

## Pin evidence

No source file changed. `ref_metal.dylib` and `ref_metal.metallib` built from this branch are
byte-identical to the base's (`a4f4b6ed`, `7838477c`), so `rt-water`, `rt-water-entities`, `fx-off`
and `skywater` at both settings, and the 5K `r_metalfx 2` hits-0 case, draw the base's frames by
construction. The 5K hits-0 case is itself not byte-repeatable live (step 2); with the time rounded
it is.

## Checks

Each through `tools/heavy.sh`: `tests/check-ref-metal.sh` (ok: 12891 checks, 0 failed; water 722,
MetalFX 319; `ref_metal` built), `tests/check-doc-links.sh` (ok), `tests/check-no-local-identity.sh`
(ok). `tests/check-pins.sh` was not run: no scenario changed. The full `scripts/test.sh` was not run on this lane.
`tests/check-runbook-index.sh` fails on this file until its index line is added at merge.
