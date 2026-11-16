# The fixed per-frame cost of the Metal renderers (FIX51)

This card's section of the shared runbook, moved out of
[`docs/plans/metal-port-harness.md`](../plans/metal-port-harness.md) in
its third round. The shared runbook's method still applies:
[how a run works](../plans/metal-port-harness.md#how-a-run-works), the
[pin arguments](../plans/metal-port-harness.md#pin-arguments), the frame
rates of [TASK97](../plans/metal-port-harness.md#frame-rates-on-a-physical-gpu-task97)
and the opaque layer of
[FIX53](../plans/metal-port-harness.md#the-opaque-layer-of-ref_metal-and-the-shim-fix53).

TASK97 (#224) found `ref_metal` and the Metal-backed `gl1` about eight times
slower than `ref_gl1` and no faster than `ref_soft`, at 3 to 4 ms a frame
whatever the size; TASK108 (#259) then read about 120 fps at 640x480 and
1280x720 windowed for the three `CAMetalLayer` renderers. FIX51 (#238) times
the four renderers against GPU completion, names the per-frame cost and
removes it.

## The three rounds

| Round | Engine commit | The present with display sync off | What it left |
|---|---|---|---|
| 1 | `1c2d4b12` | one present on its way at a time: the frame copied to a stage, the stage to the drawable, FIX53's alpha pass | presented fewer frames a second than before at every size |
| 2 | `3427719d` | as many presents on their way as the layer has drawables, a stage each, the same three passes | rendered 4 to 20 times before; presented still below before: at 5120x2880 the present's GPU time (2.05 to 2.38 ms), windowed a measurement that ended inside the window server's slow phase |
| 3 | `d7cef009` | the frame drawn into its present slot's own target; one pass writes the drawable with alpha 1 | rendered 1.8 to 22 times before; presented above before at 1920x1080 and, in `ref_metal`, at 5120x2880; the shim's 5120x2880 presents 3 to 8 percent under before; at 640x480 the window server's pace, the ranges overlapping (below) |

## The fix (round 3)

With display sync off a frame waits for no drawable. In `ref_metal`:
`rmb_present_take`, `rmb_next_drawable`, `rmb_present_begin` and
`rmb_present_commit` in `src/client/refresh/metal/rmb_device.m`, called
from `rmb_frame_begin` and `rmb_frame_end` in `rmb_encode.m`, whose
`rmb_present_pass` writes the drawable. In the shim: `mgl_present_take`,
`mgl_present_apart_begin`, `mgl_present_pass` and `mgl_present_commit` in
`src/client/refresh/gl1/metal/mgl_layer.m`, called from
`mgl_layer_frame_begin` and `mgl_layer_present`.

- The next drawable is acquired ahead, on a serial dispatch queue, one at
  a time, and taken by the frame only when it is there.
- There are four present slots: one for each of the layer's three
  drawables (`maximumDrawableCount`) and one for the frame being drawn.
  Each is a semaphore of one and a colour target of the frame's size. A
  frame takes the first free slot when it begins and is drawn into that
  slot's target, so nothing copies the frame before its present.
- When the frame ends with a drawable ready, its present goes on a second
  command queue, and runs only after every write of the frame to the
  slot's target (below, [the order of a frame and its
  present](#the-order-of-a-frame-and-its-present)). One render pass writes
  the drawable from the slot's target with alpha 1 (`rm_fragment_present`,
  `mgl_fragment_present`): the copy and FIX53's alpha pass are one pass
  over the drawable. The slot comes back when that command buffer
  completes, so a target is not drawn into while its present reads it.
  The wait for a drawable the window server still holds is that queue's,
  not a frame slot's.
- A frame that ends with no drawable ready, or with one of an old size,
  gives its slot back at once (a later frame that draws into that target
  is on the same queue, after it) and is counted in the teardown's
  "without a drawable". A frame that begins with every slot held is drawn
  into the renderer's own target (`ref_metal`'s, the shim's
  `mgl_offscreen`) and not presented. That was expected not to happen with
  four slots and three drawables, but a slot comes back only when its
  present's command buffer has completed (on the CPU, in its completed
  handler), and the measurements found it: in the shim at 5120x2880, 19
  to 32 frames a run, and in `ref_metal` in a run's first second only
  (below).

With `r_vsync 1` nothing changes: the layer is opaque, the frame is drawn
into the renderer's own target (`ref_metal`) or the drawable (the shim),
waits in `nextDrawable`, writes the drawable on its own command buffer
with a copy, and every frame is presented. Turning display sync on, and
teardown, wait for every slot but the open frame's with one shared bound
(`RMB_GPU_WAIT_MS`, `MGL_GPU_WAIT_MS`) and drop the targets. No cvar, no
change to a module API, the window or `glimp_sdl3.c`.

**The pipelines.** No pipeline is built on the present path: both are
built at init with the others, FIX53's rule. The shim builds the
present's pipeline when the context is made, with FIX53's
(`mgl_alpha_build`). `ref_metal` builds it in `rmb_resources_open`
(`rmb_new_pipe_present` in `rmb_resource.m`), next to FIX53's alpha pass,
and releases it in `rmb_resources_close`; like the alpha pass a failed
build is not fatal and is not tried again. When it was not built the
frame is copied to the drawable and FIX53's alpha pass is written over it,
as in round 2, and FIX53's line says so only if that pass's pipeline
failed too.

### The order of a frame and its present

A frame is not always one command buffer. In `ref_metal` a readback
(`rmb_read`), the in-frame split (`rmb_encode_split`), `rmb_frame_cut`
(TASK111) and `MTL_GPU_TIME` each commit the frame so far and go on in a
new one; in the shim `mgl_flush` (`glFinish`, `glReadPixels`) does. The
present reads the slot's target on another queue, so it must follow every
one of them. The first version of round 3 had the frame's last command
buffer signal an `MTLEvent` that the present waited for, which covers the
earlier command buffers only if a signal also follows work committed
earlier to the same queue. Apple's page "encodeSignalEvent(_:value:)"
(MTLCommandBuffer) says:

> When the GPU device reaches the signal command that this method encodes,
> Metal updates the event after the GPU finishes the buffer's prior
> commands.

"The buffer's prior commands": it says nothing about other command buffers
of the queue. So neither renderer relies on that now; each relies on what
Apple's page "addCompletedHandler(_:)" (MTLCommandBuffer) states:

> Registers a completion handler the GPU device calls immediately after
> the GPU finishes running the commands in the command buffer.

- **`ref_metal` waits for every command buffer of the frame.** From the
  moment a frame takes its slot (`rmb_present_take`) every command buffer
  committed is entered in the frame's dispatch group (`rmb_watch`, which
  runs before every commit) and leaves it in its completed handler. The
  present waits for no event: at the frame's end `rmb_present_commit`
  hands it to a serial dispatch queue (`rmb_present_order`), which commits
  it once the group is empty, so the presents go to their queue in the
  order of the frames. The wait is bounded (`RMB_GPU_WAIT_MS`): a frame
  still on the GPU then is not presented and its slot comes back, with one
  line on stderr. The `MTLEvent` is gone, and with it one `state` from the
  ledger.
- **The shim's earlier command buffers have completed.** Every command
  buffer of a frame but its last is committed by `mgl_flush`, which waits
  for it (`mgl_commit_wait`). Only the last one signals the event, after
  its own prior commands (the quote above); the ones before it have
  completed before it was made. When a wait ended first (the bound,
  `MGL_GPU_WAIT_MS`), the frame is not presented and is counted without a
  drawable (`frame_unseen`). A frame in one command buffer, the usual
  case, is unchanged.

A frame that is not presented gives its slot back when it ends, and a
later frame may draw into that target while the earlier one's command
buffers run. Both are on the frame queue and write the same texture,
made from the device with no hazard tracking mode set. Apple's page
"MTLHazardTrackingMode.default" says "Metal tracks hazards for MTLBuffer
and MTLTexture instances when you create them with default", and
"MTLHazardTrackingMode.tracked" that when a command writes a tracked
resource Metal will "Prevent subsequent commands from running until write
operations finish".

## What changed from round 2, and why

Round 2 wrote each presented frame three times over at full size: the
frame's command buffer copied its colour target to the present slot's
stage, the present copied the stage to the drawable, and FIX53's alpha
pass loaded and stored the whole drawable again. The old loop does two of
those (its copy and the alpha pass). At 5120x2880 round 2's present took
2.05 to 2.38 ms of GPU time and held the presents below before; with the
alpha pass switched off `ref_metal` reached the before range. Round 3
removes the stage copy by drawing the frame into the slot's own target,
and folds the copy and the alpha pass into one render pass: one full-size
pass per present, where the old loop has two and round 2 had three.

Because a frame now takes its slot when it begins, not when it ends, the
frame being drawn holds a slot too, so there are four slots rather than
three; memory is round 2's (four full-size colour targets with display
sync off: round 2's own target and three stages). `ref_metal` no longer
keeps its own target with display sync off unless display sync was on
before.

Round 2's `ref_metal` presented nothing with display sync off when the
present queue or its event could not be made (no slot, so no drawable);
round 3 then waits for the drawable as with display sync on (in
`ref_metal` when the present queue or the serial queue that commits to it
cannot be made). The shim did that already.

After review, round 3 moved `ref_metal`'s present pipeline to init
(`d4f0af6e`) and stopped relying on the order of an event and the earlier
command buffers of a frame (`d7cef009`, the section above).

## Round 3 measurements

**The measured builds.** After is `d7cef009`, the last commit of this round
that changes code; the commits after it change only this document. Before
is `origin/main` at this round's merge, `718e59d4`. One A/B adds
`d4f0af6e`, this round's code before `d7cef009`, in which the present
waited on an `MTLEvent` (below, [the CPU step](#the-cpu-step-of-the-present)).
All three are ancestors of this branch's head. Each was exported and built
twice, with the probe and without (`make -j4 client game ref_metal`, then
`make -j4 ref_gl1 WITH_METAL_SHIM=yes`; `d4f0af6e` with the probe and
`ref_metal` only). The pins and the validation run use the builds without
the probe; every rate is from the probe builds.

**When, and the load.** The bare-metal host of the earlier rounds (below),
on 2026-11-15 from 16:07 to 17:15. Before every run the screen was
unlocked, no other `quake2` ran and this lane held the timing lock; each
batch took the lock when it was free and released it at its end, none held
for more than two minutes. The one-minute load at the batches' starts and
ends was 1.98 to 8.79 (`sysctl -n vm.loadavg`), and at the runs' starts 1.9
to 10.5; the rows below give each batch's.

**How a run was counted.** The launch line of the earlier rounds
(`vid_highdpiaware 0`, `logfile 2`, `metal_stats 1`, `r_vsync 0` unless
said otherwise), windowed or `vid_fullscreen 1`, 8 s from launch, then the
engine ended by its own pid. Before and after alternated run by run, the
order flipped each round, three rounds a cell. The probe (never committed,
in both builds) prints a time per frame and whether the frame got a
drawable, and the GPU time of each present's command buffer. For the
batches from 16:19 on it also prints each frame that began, with display
sync off, finding every present slot held, and in `ref_metal` the time
from `rmb_present_commit` handing the present to `rmb_present_order` to its
commit there. Two kinds of run:

- **Timedemo runs**, the TASK97 line (`timedemo 1`, `demo1.dm2`). The
  timedemo's `fps` line is the rendered rate. The cfg meant to play the
  demo in a loop does not loop it: `SV_Map` sets `nextserver` from
  `nextdemo`, so the demo plays once and the console follows ("Server was
  killed."). (With `nextdemo` instead, the map reloaded over and over and
  no frame was drawn after the first demo.) Presented a second is counted
  from 1 s after the window's first frame to the end of the run, so in
  both builds over the console after the demo, which the new loop ends
  about 0.1 s after it starts.
- **Own-pace runs** (`timedemo 0`, `vid_maxfps 999`). The demo at its
  recorded pace lasts about 69 s, so the counted interval, from 1 s after
  the window's first frame to the end (about 7 s), is all demo, the same
  content in both builds, and well past the window server's slow phase of
  a new window (round 2: 150 to 550 ms after the first frame). These are
  the steady interval. The new loop renders 995 frames a second there,
  the cap.

**Rendered**, the timedemo's `fps` line, three runs each. Before presents
every frame: 0 frames without a drawable in all its runs.

| Size | Renderer | Before | After | Load (batch) |
|---|---|---|---|---|
| 640x480 windowed | `ref_metal` | 312.6 to 384.3 | 6263.6 to 6959.6 | 3.91 to 5.46 |
| 640x480 windowed | `gl1-metal` | 306.2 to 382.8 | 5259.5 to 5512.0 | 3.91 to 5.46 |
| 1920x1080 windowed | `ref_metal` | 339.9 to 379.0 | 6439.3 to 6754.9 | 2.29 to 3.53 |
| 1920x1080 windowed | `gl1-metal` | 363.2 to 381.7 | 4532.9 to 4956.8 | 2.29 to 3.53 |
| 5120x2880 `vid_fullscreen 1` | `ref_metal` | 368.6 to 390.8 | 1822.8 to 2008.7 | 2.61 to 4.73 |
| 5120x2880 `vid_fullscreen 1` | `gl1-metal` | 362.1 to 368.8 | 660.0 to 672.9 | 2.61 to 4.73 |

**Presented a second over the steady interval**, own-pace runs, three
each. Several ranges in a cell are separate batches. "Every slot held" is
the count of after frames, per run, that found every present slot held in
the counted interval; 640x480's first batch (16:11) did not count it, its
later two did.

| Size | Renderer | Before | After | Every slot held | Load (batch) |
|---|---|---|---|---|---|
| 640x480 windowed | `ref_metal` | 304.5 to 343.6; 350.3 to 381.1; 308.3 to 342.0 | 244.1 to 379.3; 303.6 to 431.9; 294.3 to 329.1 | 0 | 4.92 to 4.58; 3.42 to 3.55; 3.48 to 4.76 |
| 640x480 windowed | `gl1-metal` | 276.6 to 377.7; 309.8 to 356.5 | 238.0 to 470.0; 326.0 to 389.7 | 0 | 4.92 to 4.58; 3.48 to 4.76 |
| 1920x1080 windowed | `ref_metal` | 362.5 to 368.9 | 400.1 to 437.7 | 0 | 2.82 to 3.74 |
| 1920x1080 windowed | `gl1-metal` | 373.3 to 390.7 | 414.7 to 433.7 | 0 | 2.82 to 3.74 |
| 5120x2880 `vid_fullscreen 1` | `ref_metal` | 362.7 to 384.8; 392.9 to 398.8 | 375.8 to 401.5; 409.4 to 416.7 | 0 | 3.71 to 5.13; 1.98 to 8.79 |
| 5120x2880 `vid_fullscreen 1` | `gl1-metal` | 408.7 to 418.0 | 386.5 to 394.9 | 19 to 32 | 3.71 to 5.13 |

(The second 640x480 `ref_metal` batch and the second 5120x2880 one are the
A/B below, whose order was before, `d4f0af6e`, after in every round.)

**Presented a second over the console after the timedemo**, the timedemo
runs above:

| Size | Renderer | Before | After |
|---|---|---|---|
| 640x480 windowed | `ref_metal` | 379.0 to 389.2 | 248.5 to 287.5 |
| 640x480 windowed | `gl1-metal` | 363.7 to 422.3 | 226.9 to 292.6 |
| 1920x1080 windowed | `ref_metal` | 374.4 to 375.7 | 409.3 to 464.5 |
| 1920x1080 windowed | `gl1-metal` | 382.7 to 402.8 | 427.4 to 467.0 |
| 5120x2880 `vid_fullscreen 1` | `ref_metal` | 374.4 to 395.3 | 377.3 to 386.3 |
| 5120x2880 `vid_fullscreen 1` | `gl1-metal` | 371.9 to 384.3 | 406.7 to 431.4 |

**Acceptance.**

- **Rendered** is 1.8 to 22 times before at every size: 4532.9 to 6959.6
  fps windowed against 306.2 to 384.3, at 5120x2880 1822.8 to 2008.7
  (`ref_metal`) and 660.0 to 672.9 (the shim) against 362.1 to 390.8.
- **1920x1080 windowed**: presented is above before in both renderers,
  over the demo (400.1 to 437.7 against 362.5 to 390.7) and over the
  console (409.3 to 467.0 against 374.4 to 402.8). No frame found every
  slot held.
- **5120x2880 fullscreen, `ref_metal`**: at or above before in both
  batches (375.8 to 401.5 against 362.7 to 384.8, and 409.4 to 416.7
  against 392.9 to 398.8); over the console 377.3 to 386.3 against 374.4
  to 395.3, inside before's range. Round 2 was below before here (336.5
  to 347.1 against 374.9 to 407.0).
- **5120x2880 fullscreen, the shim**: below before, 386.5 to 394.9 against
  408.7 to 418.0 (3 to 8 percent), though over the console it is above
  (406.7 to 431.4 against 371.9 to 384.3). In the demo the shim renders
  786.1 to 803.1 frames a second, short of the cap, so its GPU is full;
  19 to 32 frames a run found every slot held, and a present took 1.251 to
  1.300 ms of GPU time at the median, against 0.820 to 0.830 in
  `ref_metal`. Capping its rendering near before's rate does not bring it
  up: with `vid_maxfps 420` (three runs, 17:14 to 17:15, load 5.44 to
  5.94) it rendered 419.5 to 419.6 and presented 375.2 to 388.9 a second,
  no frame found every slot held, a present took 0.547 to 0.567 ms, and
  260 to 413 of its 2812 to 2832 frames found no drawable ready. So the
  shortfall is not the GPU time of the frames that are not presented: the
  window server hands drawables back to the shim's new loop more slowly
  than to the old loop, which draws each frame into its drawable. Why was
  not found.
- **640x480 windowed**: no frame found every slot held, and before and
  after overlap in all three own-pace batches, so the window server's
  pace of handing drawables back limits both loops there, as round 2
  found: before 276.6 to 381.1, after 238.0 to 470.0, with the widest
  spread of any size. Over the console after the timedemo the new loop
  presented fewer in both renderers (226.9 to 292.6 against 363.7 to
  422.3), in the batch with the highest load (3.91 to 5.46 at its ends);
  that batch was not repeated.
- **`r_vsync 1`** is as before (1920x1080 windowed, timedemo, three runs
  each, 6 s runs, 17:04 to 17:05, load 2.22 at both ends): `ref_metal`
  59.9 to 60.1 fps before and 60.0 to 60.1 after, the shim 60.2 before and
  60.0 to 60.2 after, presented 59.9 to 60.2 a second, and 0 frames
  without a drawable in all twelve runs (362 to 380 frames each).

### The CPU step of the present

`d7cef009` commits each present from a serial dispatch queue once its
frame's dispatch group is empty, where `d4f0af6e` committed it at once
with a wait on an `MTLEvent` encoded in it. `ref_metal`, own pace, the two
and before in the same batches, three rounds each:

| Size | Before | `d4f0af6e` (event) | `d7cef009` (CPU step) | Hand-off p50 / p90 (ms) | Load (batch) |
|---|---|---|---|---|---|
| 640x480 windowed | 350.3 to 381.1 | 351.4 to 431.7 | 303.6 to 431.9 | 0.237 to 0.241 / 0.277 to 0.290 | 3.42 to 3.55 |
| 5120x2880 `vid_fullscreen 1` | 392.9 to 398.8 | 404.3 to 421.0 | 409.4 to 416.7 | 1.057 to 1.181 / 1.472 to 1.487 | 1.98 to 8.79 |

The hand-off is the time from `rmb_present_commit` to the commit on
`rmb_present_order`: what is left of the frame's GPU work, its completed
handlers and the queue's wake. Elsewhere it read 0.157 to 0.166 ms at the
median at 1920x1080 over the console, 0.326 to 0.329 over the demo, and
0.743 to 0.759 at 5120x2880 over the console. The presented rates do not
show it: the two versions' ranges are within each other at both sizes
(the 640x480 batch's lowest run of each, 351.4 and 303.6, is its first
round), and at 5120x2880 both are above before. No `ref_metal` frame found
every present slot held in a counted interval with either version (`d4f0af6e`
up to 2 and `d7cef009` up to 9 in a run's first second, none after), so
the later commit does not keep the slots longer than the window server
does.

### The present's GPU time

The median and p90 of the present command buffer's GPU time (ms), three
runs each, after:

| Size, run | `ref_metal` | `gl1-metal` |
|---|---|---|
| 640x480 windowed, own pace | 0.025 / 0.025 | 0.025 / 0.025 |
| 1920x1080 windowed, own pace | 0.102 / 0.148 to 0.776 | 0.063 to 0.065 / 0.102 |
| 1920x1080 windowed, console | 0.097 to 0.105 / 0.128 to 0.143 | 0.120 to 0.123 / 0.207 to 0.208 |
| 5120x2880 `vid_fullscreen 1`, own pace | 0.796 to 0.830 / 1.336 to 1.429 | 1.251 to 1.300 / 1.867 to 1.913 |
| 5120x2880 `vid_fullscreen 1`, console | 1.688 to 1.747 / 2.325 to 2.368 | 1.028 to 1.113 / 1.583 to 1.666 |

Round 2's present at 5120x2880, the copy from the stage and FIX53's alpha
pass, took 2.019 to 2.079 ms (`ref_metal`) and 2.291 to 2.402 (the shim)
over the timedemo, with the frames rendered as fast as the GPU allows. In
that condition, over the console, the one pass of round 3 takes 1.69 to
1.75 and 1.03 to 1.11; the frames (2698.0 to 2738.0 a second in
`ref_metal`) run beside the present on the GPU, so its time there includes
theirs. Over the demo at its own pace it takes 0.80 to 0.83 and 1.25 to
1.30. Every own-pace run at 640x480 read the
same 0.025 ms at both quantiles (the timedemo runs at that size 0.011 to
0.023); that was not looked into. FIX53's alpha pass has no time of its
own now: it is the alpha 1 that the present pass writes.

### Validation and pins

**Validation.** One `ref_metal` run of the build without the probe at
1920x1080 windowed, `r_vsync 0` (the present queue and its order queue
running), under `MTL_DEBUG_LAYER=1`, 17:06, load 2.20: `Metal API
Validation Enabled` and no validation message; 689 frames at 1127.7 fps
(the layer slows it); `metal_stats: 68126 frames, 65005 without a
drawable, 0 skipped, 0 failed command buffers`, `26 pipelines, 0 created
after init`; teardown `0 live objects (created/destroyed: device 1/1,
queue 2/2, view 1/1, texture 234/234, buffer 7/7, state 47/47, rt 0/0,
metalfx 0/0), 0 unreleased, 0 unmatched`.

**Pins.** `world`, `hud2d`, `particles`, `models`, `dlights` and
`skywater` (`PINS_SEED=1`, the default 640x480 and the harness's display
sync), and scratch copies of `world`, `hud2d` and `particles` that add `set
r_vsync 0` (deleted after), recorded for `metal` and `gl1-metal` with the
builds of `718e59d4` and of `d7cef009` without the probe, each engine
binary beside its own renderer modules (`PINS_BIN`), 17:06 to 17:15, load
2.18 to 4.69. The eighteen pairs compared at `--threshold 0
--bad-threshold 0`: 23 frames each, 0 failed, worst score 0, worst bad 0;
all 414 frame files are byte for byte equal (`cmp`).

**Not measured in round 3.** Why the window server hands drawables back to
the shim's new loop more slowly at 5120x2880; whether the console
shortfall at 640x480 holds at a lower load; what keeps the window
server's pace at 640x480 so variable (one run of each loop reads 238.0 to
470.0); `ref_gl1`'s presented frames at swap interval 0 (as in round 2:
OpenGL gives the engine no count of what the window server took); the
other window sizes and fullscreen modes, and `vid_highdpiaware 1`. In
every run with display sync off, both loops, the drawables the window
server reported shown (`addPresentedHandler`, a time above 0) were about
half the frames presented; with `r_vsync 1`, all of them (60 a second).

## Rounds 1 and 2, as measured then

What follows is the section as round 2 left it. "This version" in it is
round 2, `3427719d`.

**The measured builds.** The card was measured twice. The first
measurements (Scopes 1 and 2, the ceiling, the 1600x900 size, and the first
rows quoted below as such) were made with this card's commits on FIX53's
head before its review, `fdcd4179`, which was reworked before FIX53 merged:
before was `fdcd4179` with a timing probe added (below), after was this
branch's commit "FIX51: with display sync off the Metal renderers wait for
no drawable", then `21ec0065`. Neither id is in this branch's history now.
The commits then moved onto FIX53's merge, `dd258ab1` (#322), whose alpha
pass builds its pipeline at init and latches a failed build with one line,
and the frame loop was measured again there: before is main at `dd258ab1`,
built in a scratch worktree; after is `1c2d4b12`, that same commit on
`dd258ab1`. The tables and figures from here to the 1600x900 paragraph
are from that second measurement unless they say "first measurements".
The present path was then reworked and measured a third time, on
`3427719d`: the fix, the before and after table and everything after it
describe that version.

**What was measured, and how.** The bare-metal host of TASK97 (Apple M6,
macOS 27.2.1 build 26B131, the Studio Display at 2560x1440 points, 60 Hz).
`timedemo` of `demo1.dm2` (689 frames), windowed, the TASK97 line plus
`+set vid_highdpiaware 0` (TASK118, #303, made the default of 1 high-DPI on
macOS, so without it a 640x480 window draws 1280x960 pixels and a
1920x1080 one 3840x2160; every log here prints `Drawable size:` equal to
the requested size) and `+set logfile 2` (the engine's console log, flushed
per line, which the run script polls for the `fps` line; the engine is
then ended by its own pid). `ref_metal` runs add `+set metal_stats 1`, before
and after alike, for its teardown count of frames without a drawable (the
shim prints its count anyway). Before every run the screen was unlocked
(`CGSSessionScreenIsLocked` absent), no other `quake2` ran and this lane
held the timing lock; every batch took the lock and released it within 20
minutes. Renderers and builds alternated run by run, three rounds per cell,
and the `frames, ... fps` line is the number. Builds: `make client game
ref_soft ref_gl1 ref_metal`, then `make ref_gl1 WITH_METAL_SHIM=yes` into a
second copy of `release/`, for each build. The logs, the probe's per-frame
lines and the per-run tables are kept outside the repository.

The batches of the second measurement, each under this lane's timing lock
(free when taken, released at the batch's end), with the one-minute load at
the runs' starts: the rates 13:18:00 to 13:20:23 (2.71 to 4.31); `ref_soft`,
`r_vsync 1`, the alpha pass and the validation run 13:21:37 to 13:25:20
(1.69 to 5.14); the pins 13:26:09 to 13:31:30 (`uptime` 3.66 3.19 3.17 at
the start, 3.40 3.25 3.19 at the end); the ceiling's cross-check 13:35:39 to
13:36:59 (2.48 to 3.35).

The probe (never committed) printed, per frame, the CPU time waiting for a
frame slot, the encode time, the time blocked in `nextDrawable`, the
frame-to-frame interval, whether the frame got a drawable, and the frame
command buffer's GPU time (`GPUEndTime - GPUStartTime`). Its figures below
are p10/p50/p90 over the frames of a run after the first 100. Two switches
in the probe build made one-binary comparisons: the old blocking frame loop,
and the FIX53 alpha pass off. For the second measurement it was added to a
scratch copy of `1c2d4b12` with two more lines: the GPU time of each
present's command buffer, and whether the frame drew a 3D view, so that a
rate can count the demo's 689 frames alone. (The first measurements counted
every frame after the first 100; after the fix the demo lasts about 0.1 s,
and most of those frames are the console after it, in the half second
before the engine is ended.)

**`ref_gl1` against GPU completion (Scope 1, first measurements).**
`gl_finish 1` calls `glFinish` at the start of every frame's view
(`R_RenderView`), so a frame starts only once the GPU has finished
everything before it, the previous frame's swap included: that rate is
GPU-complete, and lower than the throughput, since the CPU and the GPU then
never overlap. The Metal paths are GPU-complete within three frames of 689
(at most three frames in flight, the frame slots). One-minute load at run
start 2.50 to 3.89 (`uptime` at the batch start `11:33 load averages: 3.89
3.97 3.41`, at its end `11:35 load averages: 3.15 3.51 3.29`); the before
Metal columns are from the same alternation:

| Size | `ref_soft` | `ref_gl1` | `ref_gl1`, `gl_finish 1` | `gl1-metal` before | `ref_metal` before |
|---|---|---|---|---|---|
| 640x480 | 120.0 (all three) | 2367.7 to 2460.7 | 1789.6 to 1808.4 | 120.2 (all three) | 120.0 to 120.1 |
| 1920x1080 | 187.2 to 198.5 | 1709.7 to 1963.0 | 1332.7 to 1343.1 | 237.6 to 290.0 | 310.8 to 376.5 |

An earlier batch of the same cells, on the probe build (42 runs, load 2.80
to 4.28), agrees: `ref_gl1` 2400.7 to 2523.8 and 1882.5 to 1951.8, with
`gl_finish 1` 1735.5 to 1803.7 and 1307.4 to 1364.4; the Metal paths 120.0 to
120.2 and 272.9 to 306.1. So `ref_gl1`'s GPU-complete rate is about 25
percent under its plain rate, and still five to seven times the Metal paths'.
At 5120x2880 fullscreen `ref_gl1` read 622.4 to 648.2 fps, with `gl_finish
1` 583.9 to 602.8 (load 3.11 to 4.05). `ref_gl1` and `ref_soft` were not run
again in the second measurement, except `ref_soft` at 640x480 (below).

**Where the Metal frame went (Scope 2, first measurements).** The probe,
before the fix (load 2.80 to 4.28):

| Size | Renderer | `nextDrawable` (ms) | encode (ms) | GPU (ms) | interval, mean (ms) |
|---|---|---|---|---|---|
| 640x480 | `ref_metal` | 7.37 / 7.58 / 8.82 | 0.10 / 0.23 to 0.38 / 0.82 | 0.12 / 0.15 / 0.19 | 8.33 |
| 640x480 | `gl1-metal` | 6.32 / 7.58 / 8.80 | (in the frame) | 0.11 / 0.17 to 0.18 / 0.23 | 8.33 |
| 1920x1080 | `ref_metal` | 0.00 / 0.41 to 1.50 / 6.71 | 0.05 / 0.15 to 0.42 / 0.86 | 0.38 / 0.43 to 0.63 / 1.93 | 3.22 to 3.49 |
| 1920x1080 | `gl1-metal` | 0.00 / 0.25 to 1.90 / 6.92 | (in the frame) | 0.35 / 0.68 to 1.23 / 2.55 | 3.12 to 3.57 |

(p10 / p50 / p90, ranges over three runs; the shim draws into the drawable,
so its encode is in the time after `nextDrawable`, 0.19 to 0.91 ms at the
median.) The frame costs well under a millisecond on the CPU and the GPU;
the rest is the wait for a drawable, 8.33 ms a frame on average at 640x480,
exactly 120 a second.

The first fix tried (acquiring the drawable ahead, on a serial queue, and
presenting only when one was ready) showed the second half of the cost. The
frame no longer waited in `nextDrawable` (p90 0.008 ms), but at 640x480 the
timedemo rose only to 359.4 to 360.9 fps in both renderers, and the frame
three after a presented one waited 6.5 to 6.6 ms for its frame slot (after
an unpresented one 0.0 to 0.5 ms): the command buffer that writes a drawable
the window server has not let go of yet waits on the GPU until it does
(its own GPU time stays 0.11 to 0.16 ms), and with it the frame slot and the
frames queued behind it. So the fixed cost is the window server's pace of
handing back drawables, paid twice: in `nextDrawable`, and in the command
buffer that writes the drawable.

**The ~120 fps windowed ceiling: no commit causes it, the window server
sets it.** FIX51's card (#238) now also carries this ceiling. In the first
measurements `ref_soft` at 640x480 read 119.9 to 120.1 fps from FIX53's
pre-review head, from TASK97's merge `462f8d3e` and from `41745a59`, the
very source TASK97 measured at 253.6 to 318.1 (three runs each, alternated,
load 2.80 to 4.28; at 1920x1080 the first two read 180.1 to 208.5,
`41745a59` 60.0, the opaque-layer cap FIX52 removed after it). With the code
and the launch line the same and the result not, the change is in the host
between TASK97's runs and TASK108's, not in the tree, so there was nothing
to bisect. The launch line is not it either: at 640x480, before the fix, the
plain line, the plain line with an empty `config.cfg` beside the binary
(found before the data directory's) and FIX52's pin-run line (`pins_capture
1000000`, no `config.cfg` read at all) read 119.9 to 120.1 fps in `ref_soft`
and `ref_metal` alike (three runs each, load 2.31 to 2.79). What sets it is
the window server: after the fix the frame loop ran at 5341.1 to 8402.4 fps
at 640x480 while the drawables presented stayed at 119.4 to 121.2 a second
(probe, six runs), so the window server took a drawable of this window 120
times a second. A bare `CAMetalLayer` program in a window of its own (no
quake2 code, clears and presents only, `displaySyncEnabled = NO`) presented
241.0 to 376.8 a second at 640x480 with the layer not opaque (eight runs,
contents scale 1 or 2) and 60.0 with it opaque (two runs; load 2.56 to
3.93), where FIX52's probe found 320.8 to 367.6 either way: the window
server's handling of small windows had changed since FIX52 too. What in the
engine's window gave 120 rather than the bare window's rate was not
isolated; FIX53's alpha-readback runs, which counted more frames than 120 a
second at 640x480, were not repeated.

About two hours later, in the second measurement, the ceiling was not
there: `dd258ab1` at 640x480 read 304.3 to 352.2 fps in `ref_metal`, 264.7 to
348.3 in the shim and 314.9 to 364.7 in `ref_soft` (three runs each, load
2.71 to 4.02 and 3.08 to 3.18), on the same host with the same launch line.
The very binaries the first measurements read 120 from were then run again
(13:35 to 13:37, three rounds alternated with `dd258ab1`, load 2.48 to
3.35): the probe build of `fdcd4179`, 120.0 to 120.1 in `ref_metal` at 11:33
to 11:35, read 331.1 to 350.1, and its `ref_soft` 326.2 to 328.7;
`41745a59`'s `ref_soft`, 119.9 to 120.0 at 10:57 to 11:01, read 60.0 to 60.1,
its opaque layer now capped at the refresh in a 640x480 window too, as
FIX52 found for larger ones; `dd258ab1` read 234.6 to 376.5 (`ref_soft`) and
327.5 to 330.3 (`ref_metal`). The same binaries, the same launch line, and a
different ceiling: it comes and goes with the window server's state, not
with a commit. What in the host turns it on was not found.

**The 1600x900 spread of TASK108 did not reproduce (first measurements).**
Windowed 1600x900, before the fix (three runs, load 2.31 to 2.73):
`ref_soft` 319.1 to 330.3, `ref_gl1` 2050.6 to 2146.4, `gl1-metal` 330.0 to
402.0, `ref_metal` 364.2 to 407.0, against TASK108's 197.7 to 321.2, 2087.9
to 2194.3, 67.1 to 96.1 and 62.3 to 75.2. Its cause was not found; after the
fix `gl1-metal` read 5219.7 to 5341.1 and `ref_metal` 7252.6 to 7655.6.

**The fix (Scope 3).** With display sync off a frame waits for no drawable
(`rmb_next_drawable`, `rmb_present_begin` and `rmb_present_commit` in
`src/client/refresh/metal/rmb_device.m`, called from `rmb_frame_end` in
`rmb_encode.m`; `mgl_present_apart`, `mgl_present_apart_begin` and
`mgl_present_commit` in `src/client/refresh/gl1/metal/mgl_layer.m`, called
from `mgl_layer_frame_begin` and `mgl_layer_present`):

- the next drawable is acquired ahead, on a serial dispatch queue, one at a
  time, and taken by the frame only when it is there;
- the frame is drawn offscreen (`ref_metal` always is; the shim then draws
  into its offscreen texture, its path for a frame without a drawable),
  copied to a stage texture on the frame's command buffer, which signals an
  `MTLEvent`; the copy from the stage to the drawable, FIX53's alpha pass
  and the present go on a second command queue that waits for that event,
  so the wait for the drawable holds that queue and no frame slot;
- as many presents are on their way as the layer has drawables
  (`maximumDrawableCount`, 3, at most `RMB_PRESENTS` and `MGL_PRESENTS`,
  3), each in a present slot of its own: a semaphore of one, given back
  when its present's command buffer completes, and the stage that present
  reads, so a stage is not written again while it is read. A frame takes
  the first free slot (`rmb_present_take`, `mgl_present_take`), so a stage
  is made only when the slots before it are on their way;
- a frame that ends with no drawable ready, every slot on its way, or a
  drawable of an old size is complete and not presented, and is counted
  in the existing "without a drawable" counter of the teardown line.

FIX53's alpha pass runs only on a presented frame and only while the layer
is not opaque (display sync off), with the pipeline FIX53 builds at init
(`rmb_alpha_pipe()`, `mgl_alpha_pso`); when that build failed the frames are
presented without it and FIX53's one line says so. No run here printed that
line. With `r_vsync 1` nothing changes: the frame waits in `nextDrawable` and
writes the drawable itself, and every frame is presented. Turning display
sync on drains the queue and drops the stages. Teardown drains both, bounded
(one second for `nextDrawable`, `RMB_GPU_WAIT_MS` and `MGL_GPU_WAIT_MS` for
all the presents together); the second queue, the event and the stages are
in `ref_metal`'s ledger (`queue 2/2`, `state 47/47` at teardown). No cvar, no
change to a module API, the window or `glimp_sdl3.c`. No pure helper and no
host test: the rule is which command buffer writes the drawable, and nothing
in it can run without a window server; the measurements below are its test.

**What changed in this version, and why.** The version measured on
`1c2d4b12` let one present be on its way at a time (a semaphore of one and
one stage), and presented fewer frames a second than before the fix at every
size: 116.7 to 206.4 windowed and 142.0 to 262.0 at 5120x2880, against 264.7
to 396.0 before. Its likely cause was that a present's command buffer
completes only when the window server lets the drawable go, so with one on
its way the presents followed one another at the window server's pace, where
the old loop had up to three frames in flight, each holding its drawable.
This version, FIX51's second rework, lets as many presents be on their way
as the layer has drawables (above), so the present path never waits for a
present while a drawable is free. The functions changed: `rmb_ahead_drop`,
`rmb_next_drawable`, `rmb_present_begin`, `rmb_present_commit`, `rmb_open`
and `rmb_close`, with `rmb_present_slots` and `rmb_present_take` added
(`rmb_device.m`); `mgl_present_apart`, `mgl_ahead_drop`,
`mgl_present_apart_begin`, `mgl_present_commit`, `mgl_release_all`,
`mgl_layer_create` and `mgl_layer_present`, with `mgl_present_take` added
(`mgl_layer.m`). `rmb_encode.m` only lost blank lines the probe had left.
At 5120x2880 the presents rose (`ref_metal` from 259.8 to 262.0 a second to
336.5 to 347.1, the shim from 142.0 to 143.2 to 235.7 to 240.8); windowed,
over the demo, they did not, and the measurements below say why: the slots
were never the limit there.

**The measured builds of this version.** Before is `dd258ab1`, the very
binaries of the second measurement; after is `3427719d` ("FIX51: as many
presents on their way as the layer has drawables"), an ancestor of this
branch's head, whose later commits change only documents. The probe (never
committed) was added to a scratch copy of `3427719d`. Per frame it prints
the line of the second measurement plus whether the frame drew a 3D view
and, for a frame not presented, why (every slot on its way, no drawable
ahead, or one of an old size); per present, its command buffer's GPU time
and its wait from commit to GPU start; per drawable, its `presentedTime`
when the window server reports it shown (`addPresentedHandler`, a time
above 0); and the time each `nextDrawable` took on the queue ahead. Its
switches: the old blocking loop (`FIX51_BLOCK`: the frame waits in
`nextDrawable` and writes the drawable, as `dd258ab1` does), the alpha pass
off (`FIX51_NOALPHA`), and a cap on the frame rate (`FIX51_CAP`). The launch
line, the host, the screen and lock checks and the alternation are those of
the second measurement, three rounds a cell. A batch run while the machine's
user may have been at it (focus, occlusion or input change the rates) was
thrown away, with every batch begun in the 15 minutes before that was
known, and all of them were run again; the figures below are from the
re-runs, 14:18 to 14:35, under this lane's timing lock, each batch taken
and released within 20 minutes. One-minute load at the batches' starts and
ends: the rates 2.49 to 3.83 windowed and 3.77 to 8.55 at 5120x2880 (the
load rose during that batch); the windows shown first 8.19 to 4.47; `r_vsync
1` 5.07 to 2.12; the alpha pass, the cap and the validation run 2.12 to
3.39; the pins 2.31 to 3.57.

**Before and after** (fps, three runs each. Rendered is the timedemo's own
rate. Presented is the frames that got a drawable per second: before, every
frame, and the teardown counts 0 frames without a drawable in all 18 before
runs, 796 to 896 frames each; after, the probe's count over the demo's 689
frames. The last column is the old loop in the probe build, which presents
every frame):

| Size | Renderer | Before, rendered and presented | After, rendered | After, presented a second | Old loop, probe build |
|---|---|---|---|---|---|
| 640x480 windowed | `ref_metal` | 332.2 to 362.4 | 5789.9 to 6754.9 | 125.1 to 154.6 | 339.1 to 368.8 |
| 640x480 windowed | `gl1-metal` | 321.2 to 364.6 | 4784.7 to 5029.2 | 116.4 to 120.4 | 279.4 to 368.1 |
| 1920x1080 windowed | `ref_metal` | 237.9 to 343.8 | 5425.2 to 6500.0 | 112.7 to 138.6 | 331.7 to 349.4 |
| 1920x1080 windowed | `gl1-metal` | 311.1 to 348.0 | 3870.8 to 4474.0 | 151.4 to 193.0 | 287.9 to 325.6 |
| 5120x2880 `vid_fullscreen 1` | `ref_metal` | 374.9 to 407.0 | 887.9 to 921.1 | 336.5 to 347.1 | 409.6 to 416.0 |
| 5120x2880 `vid_fullscreen 1` | `gl1-metal` | 384.9 to 387.5 | 464.3 to 473.9 | 235.7 to 240.8 | 384.4 to 393.6 |

The probe build's own rendered rates agree with the after column (6155.1 to
6656.9 and 5012.3 to 5184.0 at 640x480, 5772.7 to 6471.7 and 3944.0 to
4432.2 at 1920x1080, 895.8 to 946.5 and 461.8 to 477.7 at 5120x2880). The
drawables the window server reported shown, a second over the demo: after
46.3 to 69.4 (`ref_metal`) and 45.3 to 97.5 (the shim) windowed, 197.9 to
200.3 and 220.6 to 232.9 at 5120x2880; the old loop 167.9 to 186.2 and
142.0 to 185.6 windowed, 207.8 to 213.9 and 208.5 to 229.5 at 5120x2880.

**Acceptance.** Rendered: above the old rate at every size, 3870.8 to
6754.9 fps windowed against 237.9 to 364.6 before, and at 5120x2880 887.9
to 921.1 (`ref_metal`) against 374.9 to 407.0, but only 464.3 to 473.9 (the
shim) against 384.9 to 387.5. Presented: at least the before range at no
size. Windowed the
presents are 112.7 to 193.0 a second against 237.9 to 364.6; at 5120x2880
`ref_metal` is 336.5 to 347.1 against 374.9 to 407.0, and the shim 235.7 to
240.8 against 384.9 to 387.5. Why, from the probe:

- **The present slots are not the limit windowed.** No frame of any
  windowed run found every slot on its way; the frames not presented (659
  to 677 of the demo's 689 in each run) found no drawable ahead. So what is
  presented is the pace at which the window server hands drawables back.
- **Windowed, that pace has a slow phase, and the demo now fits inside
  it.** From a window's first frame the window server hands back two
  drawables per display refresh: the presents come in pairs, 16.7 ms apart,
  120 a second. It leaves that phase (the first 50 ms with more than 250
  presents a second) 150 to 450 ms after the first frame with this loop,
  and 200 to 550 ms with the old loop in the same binary; one run of this
  loop did not leave it before the run ended. The demo now runs from 52 to
  61 ms after the first frame to 155 to 233 ms, so inside the slow phase
  every time; the old loop's demo runs from 128 to 161 ms to 1993 to 2590
  ms, mostly after it. In the same window, the first 100 ms from the demo's
  first frame, the old loop presents 130 to 170 a second (`ref_metal`) and
  130 to 300 (the shim), this loop 120 to 140 and 120 to 140.
- **In a window shown for a second first the demo is still short of
  before, and the pace varies.** With the demo started after 60 `wait`s
  (about one second of the console) the demo presented 125.2 to 240.9
  (`ref_metal`) and 208.3 to 294.8 (the shim) a second, the old loop 311.7
  to 355.7 and 275.1 to 335.3: the map load before the demo slows the
  window server again. Over the console from 0.5 to 0.95 s after the first
  frame, the same content in both loops, this loop presented 169 to 409
  (`ref_metal`) and 191 to 373 (the shim) a second, the old loop 164 to 467
  and 231 to 336: the ranges overlap, with the lowest run of each loop
  (169 and 164) near the slow phase's rate. What makes the window server
  leave the slow phase, and whether presents that come in pairs keep it
  there longer, was not found.
- **At 5120x2880 the present path's GPU time is the limit.** Fullscreen has
  no slow phase (the old loop presents 290 to 480 a second in the demo's
  first 100 ms), and here 22 to 51 frames a run found every slot on its way.
  A present's command buffer takes 2.05 to 2.10 ms on the GPU (`ref_metal`)
  and 2.32 to 2.38 ms (the shim), and starts 3.0 to 5.3 ms after its commit,
  behind the frames on the other queue. The alpha pass is about 1.2 and 1.45
  ms of it (below): with the pass off `ref_metal` presents 391.0 to 398.8 a
  second, in the before range, and the shim 343.2 to 347.9. With the frame
  rate capped near the old one (`FIX51_CAP=400`: rendered 341.8 to 343.2
  and 347.4 to 349.3) `ref_metal` presents 320.3 to 325.5 a second and the
  shim 293.0 to 302.6, so the frames that are not presented are not all of
  the shim's shortfall. The old loop does less per presented frame: it
  copies the frame to the drawable once, where this loop copies it to the
  stage and the stage to the drawable.

How many of `ref_gl1`'s frames reach the screen at swap interval 0 was not
measured: it presents through OpenGL, which gives the engine no count of
what the window server took, and no Metal System Trace was recorded.

**`r_vsync 1` is as before** (1920x1080 windowed, three runs each, before and
after alternated): `ref_metal` 59.9 to 60.0 fps before and 59.9 to 60.0
after, the shim 60.0 to 60.1 before and 59.9 to 60.1 after, and every frame
got a drawable (727 to 738 frames, 0 without a drawable, in all twelve
runs). In the first measurements 640x480 read 120.0 to 120.2 before and
after with `r_vsync 1`, as FIX53 found; it was not run again.

**The alpha pass of FIX53.** First measurements, with the old loop in one
binary, the pass on and off, alternated (three runs each), the median GPU
time of a frame's command buffer, which held the pass then: at 1920x1080
windowed about 0.13 ms a frame, at 5120x2880 fullscreen about 1.1 to 1.6 ms
a frame. With the fix the pass runs only on the frames presented, on the
present queue. Its cost per presented frame, on `3427719d`: the median GPU
time of a present's command buffer (the copy from the stage, then the pass),
the probe build with the pass on and off, alternated, three runs each:

| Size | Renderer | With the pass (ms) | Without (ms) | The pass (ms) |
|---|---|---|---|---|
| 1920x1080 windowed | `ref_metal` | 0.174 to 0.193 | 0.070 to 0.087 | about 0.10 |
| 1920x1080 windowed | `gl1-metal` | 0.233 to 0.264 | 0.064 to 0.127 | about 0.14 |
| 5120x2880 `vid_fullscreen 1` | `ref_metal` | 2.019 to 2.079 | 0.800 to 0.840 | about 1.2 |
| 5120x2880 `vid_fullscreen 1` | `gl1-metal` | 2.291 to 2.402 | 0.877 to 0.879 | about 1.45 |

At 5120x2880 the pass is more than half of a present's GPU time and shows
in the rates: with it off the presents rise from 333.0 to 357.4 a second to
391.0 to 398.8 (`ref_metal`) and from 241.3 to 246.8 to 343.2 to 347.9 (the
shim), and the timedemo from 840.5 to 932.9 fps to 1561.1 to 1613.3 and from
471.0 to 485.0 to 573.9 to 579.0. At 1920x1080 the demo falls in the slow
phase above and the rates do not separate.

**Validation.** One `ref_metal` run on `3427719d` at 1920x1080 windowed with
`r_vsync 0` (the present queue running) under `MTL_DEBUG_LAYER=1`, load
3.34: `Metal API Validation Enabled` and no validation message; 689 frames
at 1064.9 fps (the layer slows it); `metal_stats: 5548 frames, 5160 without
a drawable, 0 skipped, 0 failed command buffers`; teardown `0 live objects
(created/destroyed: device 1/1, queue 2/2, view 1/1, texture 234/234, buffer
7/7, state 47/47, rt 0/0, metalfx 0/0), 0 unreleased, 0 unmatched`. The
second measurement's run on `1c2d4b12` counted `texture 233/233`; the one
more here is, from the code, a second stage, made for a frame presented
while the first stage's present was on its way.

**Pins.** `world`, `hud2d`, `particles`, `models`, `dlights` and
`skywater` (`PINS_SEED=1`, the default 640x480 and the harness's display
sync), and scratch copies of `world`, `hud2d` and `particles` that add `set
r_vsync 0` (so the present queue runs; deleted after), were recorded for
`metal` and `gl1-metal` with the builds of `dd258ab1` and of `3427719d`,
each engine binary beside its own renderer modules (`PINS_BIN`). The
eighteen pairs compared at `--threshold 0 --bad-threshold 0`: 23 frames
each, 0 failed, worst score 0, worst bad 0; all 414 frame files are byte for
byte equal (`cmp`). A pin reads the offscreen target (the shim's, with
display sync off, is now always its offscreen texture), so the frames
presented apart are not what it compares; the validation run above covers
the present queue.

**Not measured.** What in the host turns the 640x480 ceiling of the first
measurements on and off; what makes the window server leave the paired
pace of a new window, and whether presents spread out in time rather than
in pairs would make it leave sooner (one probe of that was in a batch that
was thrown away, above, and was not run again); whether a present path
without the stage copy, or with the copy and the alpha pass in one pass,
would reach the before rate at 5120x2880; `ref_gl1`'s presented frames;
the 1280x720 and 1600x1200 windowed sizes and any fullscreen mode but
5120x2880 after the fix; the high-DPI default (`vid_highdpiaware 1`) after
the fix; what the lower present rate changes on screen (latency, tearing).
On the probe build before the fix, at the default of 1, 640x480 (1280x960
pixels) read 120.0 to 120.1 for `ref_soft` and both Metal paths, and at
1920x1080 (3840x2160) `ref_gl1` read 692.5 and 711.8 (`gl_finish` 0 and 1),
`gl1-metal` 296.7 and `ref_metal` 360.5, with a GPU time of 2.17 ms a frame
at the median for `ref_metal` (one run each, first measurements).
