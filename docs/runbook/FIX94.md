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

## The code: where `scaler_ms` is taken, before and after FIX51

The same code takes the scaler's time at `8caca620` and at `95a3f955`:
FIX51 changed nothing in `rmb_metalfx.m` or in `rmb_frame_cut`. With
`metal_stats` on, `rmb_fx_upscale` (`rmb_metalfx.m`) calls
`rmb_frame_cut` (`rmb_encode.m`), which commits the frame's command
buffer so far (`rmb_commit_partial`: the G-buffer and the 3D view at the
input size, after the uploads) and goes on in a new one. The scaler is then
encoded alone into a command buffer of `rmb_queue` of its own, labelled
`ref_metal metalfx`, committed at once, and its completed handler stores
`(GPUEndTime - GPUStartTime) * 1000` in `fx_ms`. The copy of the scaler's
output (`fx_out`, its own target) into the frame goes into the frame's
next command buffer. `RM_FX_End` (`rm_fxscene.c`) prints the last
stored value on every view at `metal_stats 2`; the bench adapter reads
that line (`scripts/bench/adapters/quake2.sh`). One scaler encode a view,
before and after: the scaler's command buffer holds the same one encoder.

So `scaler_ms` is, before and after, the wall span on the GPU's clock from
the start to the end of one command buffer, not the scaler's own GPU
cycles: whatever else the GPU runs while that buffer is on it, from
another queue or another process, lies inside the span.

What FIX51 changed on the frame's command-buffer path, with display sync
off (the case's `vsync=0`):

- **The present left the frame's queue.** Before, the copy of the frame to
  the drawable, FIX53's alpha pass and `presentDrawable` were in the
  frame's last command buffer on `rmb_queue`, after the scaler's in queue
  order; the next frame's command buffers, the next scaler's among them,
  queued behind it. After, they are one pass (`rmb_present_pass`) in a
  command buffer of a second queue, `rmb_present_queue`, which the serial
  dispatch queue `rmb_present_order` commits once every command buffer of
  the frame has completed (`rmb_present_commit`). By then the engine is
  encoding and committing the next frame, so the present of frame N and
  the next frame's command buffers, its scaler's among them, can be on the
  GPU at the same time.
- **The frame no longer waits for a drawable.** The drawable is acquired
  ahead on a serial queue (`rmb_next_drawable`); a frame with no drawable
  ready, or with every present slot held, is drawn and not presented. The
  frame loop then runs at `vid_maxfps` (300 in the case) rather than at the
  window server's pace, and only some of its frames are presented.
- **The frame is drawn into a present slot's target**, one of
  `RMB_PRESENTS` (4) targets in turn, not into one target of its own. The
  scaler's inputs (the render-size colour, depth and motion) and its
  output (`fx_out`) are not those targets: the scaler's command buffer
  reads and writes the same textures as before.
- **Every command buffer of a frame joins its present's dispatch group**
  (`rmb_watch`): one more completed handler on the CPU, no GPU work.

Hypothesis 3 (more work in the scaler's command buffer, or the scaler
twice) is ruled out by the code: the scaler's command buffer holds one
encoder, from the same call, with the same textures, once a view. The two
changes that can move the span are the present on a second queue (work
of another queue inside the span: hypothesis 2) and the frame pace (the
GPU's clock: hypothesis 1); the runs below tell them apart.
