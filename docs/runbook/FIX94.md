# The MetalFX temporal scaler's GPU time at 1920x1080 after FIX51 (FIX94)

FIX94 (#428): on the M6, warm, the bench case
`metalfx-scaler-1920x1080-temporal-0.34` reads 0.705 ms up to `8caca620`
(TASK111) and 1.458 ms from `95a3f955` (FIX51, #365) on; the temporal
cells at scales 0.5 and 0.75 at 1920x1080 rose by 72 and 55 percent, and
every 5120x2880 cell and the 1920x1080 spatial cells did not rise. The
cases are [TASK154's](TASK154.md), the metric is
[TASK111's](TASK111.md), and the present ordering is
[FIX51's](FIX51.md). The shared runbook's method applies:
[how a run works](../plans/metal-port-harness.md#how-a-run-works).

## Design

Three hypotheses, tested in this order; none is assumed.

1. **Clock-down, not work.** FIX51's present waits for every command
   buffer of its frame and runs on a queue of its own. If at 1920x1080 the
   GPU now idles between frames and clocks down, the same scaler work
   reads slower. Decided by: the frame rate, the GPU's busy share
   (`ioreg -r -c IOAccelerator -d 1 -w 0`, Device Utilization %) and the
   scaler's time against the time of a pass FIX51 did not touch, in the
   same frames, before and after. The answer if the frame rate or the
   busy share fell and every pass slowed by a similar share.
2. **The measurement.** `scaler_ms` is the scaler's command buffer's
   `GPUEndTime - GPUStartTime`. If after FIX51 that span holds other work
   (a wait, another encoder, another queue's command buffer on the GPU at
   the same time), the metric no longer reads the scaler alone. Decided
   by: where the timestamps are taken before and after (code), and which
   command buffers run on the GPU inside the scaler's span (a listing of
   every command buffer's queue, label and GPU start and end over a few
   frames, before and after).
3. **Real extra work.** The scaler's command buffer holds more work, or
   the scaler runs twice a frame. Decided by: the code, and the count of
   scaler command buffers per frame in the same listing.

The runs:

- no timing (the screen may be locked): the listing above, one run each of
  `8caca620` and `95a3f955` with a temporary probe that is never
  committed, at the case's settings; the logs go to the card's artifacts
  folder and are never committed;
- timing (the screen unlocked only): one heavy.sh hold per batch, a
  4-minute full-load pre-warm inside it, the two builds alternated three
  runs each, the host snapshot (`sysctl -n vm.loadavg`,
  `memory_pressure -Q`, the GPU's utilization, `pmset -g therm`) before
  every run; the frame rate, the busy share, `scaler_ms` and one other
  pass's GPU time per build.
