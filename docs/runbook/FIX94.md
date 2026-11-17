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

## Runs without timing: the command buffers of a frame, before and after

A temporary probe, never committed, built on `8caca620` and on
`95a3f955` (FIX51's six changed files laid over the same tree, so the two
builds differ by FIX51 alone): with `FIX94_PROBE` set, the completed
handler of every watched command buffer (`rmb_watch`, and on `95a3f955`
`rmb_watch_until`) prints its queue, `GPUStartTime`, `GPUEndTime` and
label, and the frame's last command buffer on the frame queue is labelled.
One run of each, 12 s, at the case's settings (1920x1080 windowed, display
sync off, `vid_maxfps 300`, `r_metalfx 2`, `r_metalfx_scale 0.34`,
`metal_stats 2`, the world scenario, demo1 at its own pace), both in one
heavy.sh hold on 2026-11-17 at 04:33 UTC, the screen **locked**, load
1.76 to 2.37, frequency: not recorded. The window read is the middle of the
run (past the first quarter of the scaler's buffers, to the last tenth,
7.3 s). The logs stay in the card's artifacts folder.

| | `8caca620` (before) | `95a3f955` (FIX51) |
|---|---|---|
| frames (engine total), without a drawable | 1,436, 0 | 3,604, 2,160 |
| scaler buffers a second (= frames drawn) | 120.1 | 299.8 |
| present-queue buffers a second | none (the present is in the frame's last buffer) | 120.0 |
| scaler buffers a frame | 1 (872 scaler, 871 last-of-frame) | 1 (2,188, 2,187) |
| GPU busy share, the union of this process's buffers' spans | 15.6 % | 55.9 % |
| scaler buffers whose span holds another buffer of this process | 0 of 872 | 0 of 2,188 |
| scaler span, probe median (p10 to p90) | 0.769 ms (0.699 to 0.807) | 0.712 ms (0.695 to 0.751) |
| `metal_stats 2` scaler lines past 60, median | 0.767 ms (1,245 lines) | 0.710 ms (3,222 lines) |
| the frame's buffer before the scaler (G-buffer, 3D view), median | 0.353 ms | 0.085 ms |
| the frame's last buffer, median | 0.573 ms (with the present) | 1.009 ms (no present) |

The GPU times in this table were read with the screen locked and are not
comparable with the stored baselines or the card's warm numbers; the
structure is (which buffers, how many, which queue, whether two spans
overlap), and so is the fact that the two builds read alike here.

What the listing shows:

- **Not real extra work (hypothesis 3, ruled out).** One scaler command
  buffer a drawn frame in both builds, and the code's one encoder in it.
- **Not this process's work inside the span (hypothesis 2, in-process part
  ruled out).** No command buffer of ref_metal, the present queue's
  included, is on the GPU inside any scaler span, before or after. The
  present of frame N runs after the scaler of frame N+1 has ended or
  before it starts. What another process (the window server's compositing
  of the window) runs inside the span is not seen by the probe.
- **FIX51 does not leave the GPU idler (hypothesis 1 as stated, not
  supported here).** FIX51 lifts the frames drawn from the window server's
  120 a second to the case's cap of 300 and this process's busy share from
  16 to 56 percent: the GPU idles less between frames after FIX51, not
  more.
- **Locked, the doubling is absent.** Both builds read near the stored
  0.705 ms (0.767 and 0.710 ms). The 1.458 ms of the card was read warm
  with the screen unlocked; whatever doubles the span needs the unlocked
  desktop: the window composited on screen, or the GPU's performance
  state there. Only timing runs can tell those apart.

## Timing

Timing pending: screen locked. At every check during this card
(`ioreg -n Root -d1 | grep -c CGSSessionScreenIsLocked` printed 1) no
timing run was made, and none of the numbers above is a timing result.

The runs that decide, with the screen unlocked and the owner's protocol
(one heavy.sh hold, a 4-minute full-load pre-warm in it, the two builds
alternated three runs each, the host snapshot before every run,
"frequency: not recorded" until TASK166 lands):

1. the same probed listing, before and after, warm: does a scaler span now
   hold this process's present (a change in scheduling that the locked
   desktop hides), and do the G-buffer and the last buffer slow by the
   scaler's share (the clock) or not (the scaler alone);
2. `95a3f955` at `vid_maxfps 120`, alternated with it at 300: if the
   scaler reads 0.70 ms at 120 and 1.46 ms at 300, the frame pace FIX51
   allowed is the cause, not the present ordering;
3. the GPU's busy share from `ioreg` (Device Utilization %) sampled during
   each run, beside this process's own share from the listing: a busy
   share well above this process's is another process's GPU work.

## Verdict so far

- Real extra work: ruled out (the code and the listing).
- The measurement: unchanged code; no buffer of this process inside the
  span, locked. Open only for another process's work inside the span on
  the unlocked desktop.
- Clock-down: FIX51 leaves the GPU busier, not idler; open only as a
  performance state of the unlocked desktop.

No code, bench case or baseline changes on this evidence: the locked runs
show both builds alike, and a fix or a corrected metric needs the timing
runs above. FIX51's present ordering is left as it is.
