# `r_rt_water_hits 1` with `r_metalfx 2` at 5120 x 2880: why its records differ (FIX96)

FIX96 follows [TASK162](TASK162.md) (*Setting 1*, `rtw-h1fx`), which found this case the only one
whose records differ run to run, and builds on [TASK158](TASK158.md) (the world-space path) and
[FIX81](FIX81.md) (the pass-over loop). The scalers are [TASK111](TASK111.md)'s; the harness is
[doc/100_renderpins.md](../../doc/100_renderpins.md). Base: FIX92's branch, which holds TASK162.

## Design note: the runs and what each decides

Every run: the screen locked or not (frames only, no timing), `MTL_DEBUG_LAYER=1`, `PINS_SEED=1`,
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
