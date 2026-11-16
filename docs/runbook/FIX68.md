# The shim's presented rate at 5120x2880 with display sync off (FIX68)

FIX51 left one cell below the old loop: the `ref_gl1` Metal shim
(`WITH_METAL_SHIM=yes`) at 5120x2880 fullscreen with `r_vsync 0`
presented 3 to 8 percent fewer frames a second than before, its GPU full
([FIX51, round 3](FIX51.md#round-3-measurements) and
[round 4](FIX51.md#round-4-measurements)). This card finds where the time
goes, fixes the part that is in the shim, and measures the windowed slow
phase that round 4 left open. The shared runbook's method applies:
[how a run works](../plans/metal-port-harness.md#how-a-run-works) and the
[pin arguments](../plans/metal-port-harness.md#pin-arguments).

**Verdict.** The shim's frames that no drawable takes were drawn on the
GPU three at a time, and the frame that is presented, and then its
present, waited behind them. With at most two frames on the GPU while
display sync is off (`bf58931b`), the shim at 5120x2880 presents 443.8 to
449.6 frames a second against the old loop's 451.9 to 456.7 in the same
batch: 1 to 3 percent under, where main was 13 to 15 percent under (382.5
to 388.6). The rest is the present apart from the frame, FIX51's design,
which this card does not change ([below](#what-is-left)). At 1920x1080
windowed the new loop presented above the old loop in every run of two
batches; the window server was not in a slow phase in either, so that
question is still not answered by a measurement ([below](#the-windowed-slow-phase)).

## The builds

| Name | Commit | What it is |
|---|---|---|
| old | `718e59d4` | the old present loop (`origin/main` at FIX51 round 3's merge) |
| round 3 | `d7cef009` | FIX51 round 3's present apart; the stored baseline at 5120x2880 |
| main | `ddbfb6ff` | `origin/main` when this card began; its shim and `ref_metal` present path are `df2acd46`'s (FIX51 round 4), unchanged since |
| fix | `bf58931b` | main with this card's change in `mgl_layer.m` |

Every rate is from a probe build: exported with `git archive`, the probe
applied, built with `make -j4` (the renderers, `ref_gl1` with
`WITH_METAL_SHIM=yes`). The probe is FIX51's (`probe.py`, `probe2.py`:
the time of each frame and whether it was presented, the present's GPU
time) and this card's `probe4.py`; none of it is committed. It prints, per
event, with `CLOCK_UPTIME_RAW` times:

- each `nextDrawable` and its wait: in the new loop on the queue that
  acquires ahead (`mgl_ahead_request`, `rmb_ahead_request`), in the old
  loop in the frame;
- in the new loop, how long a drawable acquired ahead waits for a frame
  to take it, and how long each present slot is held, from its take to its
  give or to its present's completion;
- the GPU time of the frame's last command buffer and the time from its
  commit to its GPU start and end; the same for the present's command
  buffer, and the time from its drawable's acquisition to its completion;
- each frame's time on the CPU, from its begin (before the wait for a
  frame slot) to the end of its present, and of it the wait for a frame
  slot; each shim flush (`mgl_flush`).

The old build is `718e59d4`'s renderers with the probe and the engine and
game of FIX51 round 3's build of that commit (the probe edits renderer
sources only); main is `ddbfb6ff` built whole; the fix is `bf58931b`'s
renderers with main's engine and game (the commit changes `mgl_layer.m`
only). The drift-check runs use FIX51 round 3's builds of `718e59d4` and
`d7cef009`, the builds the stored baselines were measured with.

## How a run was counted

FIX51 round 3's own-pace runs: the launch line of the earlier rounds
(`vid_highdpiaware 0`, `logfile 2`, `metal_stats 1`, `s_initsound 0`,
`-portable`), `r_vsync 0`, `timedemo 0`, `vid_maxfps 999`, `demo1.dm2` at
its recorded pace, 8 s from launch, the engine then ended by its own pid;
counted from 1 s after the window's first frame to the run's last frame,
all of it demo. Before and after alternate run by run, the order flipped
each round. On 2026-11-15 from 20:57 to 21:44, on the bare-metal host of
FIX51 (60 Hz 5K display). Before every run the screen was unlocked and no
other `quake2` ran; every build, batch and pin call ran under the
artifacts folder's `heavy.sh`, which holds the host's timing lock. The
one-minute load (`sysctl -n vm.loadavg`) at the batches' starts and ends
was 1.84 to 3.55; each table gives its batch's.

## Where the time goes

5120x2880 `vid_fullscreen 1`, three runs each, old and main alternated,
21:01 to 21:03, load 2.68 to 3.55. Times are medians in ms unless said;
"share" is the sum over the counted interval divided by its length.

| | Old, shim | Main, shim | Old, `ref_metal` | Main, `ref_metal` |
|---|---|---|---|---|
| Presented a second | 441.0 to 450.5 | 382.8 to 388.6 | 458.4 to 461.6 | 539.2 to 548.5 |
| Rendered a second | the same | 699.0 to 703.2 | the same | 995.1 to 995.2 |
| `nextDrawable`, mean wait; share | 1.06 to 1.23; 48 to 54 % | 1.64 to 1.69; 64 to 65 % | 0.92 to 1.06; 42 to 49 % | 1.07 to 1.10; 59 % |
| A drawable acquired ahead, waiting for a frame's end | | 0.77 to 0.81 | | 0.96 |
| Frame, GPU start to end | 3.34 to 3.56 | 3.57 to 3.91 | 2.63 to 3.02 | 0.62 to 0.66 |
| Frame, commit to GPU start | 0.11 to 0.13 | 0.05 to 0.06 | 1.19 to 1.26 | 0.06 |
| Frame, commit to completion | 3.46 to 3.69 | 3.70 to 3.99 | 4.26 to 4.35 | 0.68 to 0.72 |
| Frames' GPU time, share | 150 to 161 % | 251 to 262 % | 130 to 131 % | 62 to 65 % |
| Present, GPU start to end | in the frame | 1.31 | in the frame | 0.60 |
| Present, commit to completion | | 4.63 to 4.82 | | 1.53 to 1.56 |
| Drawable acquired to its present completed | about 4.6 | 5.63 to 5.74 | about 4.3 | 2.24 to 2.28 |
| Present slot held, presented frames | | 5.21 to 5.25 | | 1.68 to 1.72 |
| CPU per frame | 2.27 to 2.29 | 0.99 to 1.00 | 2.07 to 2.17 | 0.09 |
| Waiting for a frame slot, share | 6 to 10 % | 52 to 62 % | 17 to 19 % | 0 % |
| Frames that found every present slot held, a run | | 73 to 75 | | 0 (1 to 5 in the first second) |

The old loop's "drawable acquired to its present completed" is the CPU
time of its frame after `nextDrawable` returned (its CPU time less the
wait, 1.0 to 1.2) and its command buffer's commit to completion, which
writes the drawable and presents it; the old `ref_metal` acquires its
drawable at the frame's end, just before that commit. The shim flushed (`glFinish`,
`glReadPixels`) no frame in any run. The GPU-time shares add up to more
than 100 % because the command buffers of successive frames run at the
same time on the GPU.

With three drawables, each comes back once per three presents: 7.77 ms at
main's 386 a second, 6.71 ms at the old loop's 447. Of that the time from
a drawable's acquisition to its present's end is 5.68 ms in main and about
4.6 in the old loop; the remainder, from the present's end until
`nextDrawable` hands the drawable back (the window server's part), is
about 2.1 ms in both. So the shortfall is all on the shim's side of the
round trip, 1.1 ms longer:

- 44 to 46 percent of main's frames (2198 to 2271 of a run's 4962 to
  4971, the teardown's counts) end with no drawable ready. They are drawn all the same, three frames on the
  GPU at a time (the CPU waits for a frame slot 52 to 62 percent of the
  time), and one frame of the shim at this size is heavy: 1.04 ms of GPU
  time alone on the GPU (the experiment with one frame in flight, below),
  3.6 to 3.9 ms from start to end beside two others.
- The frame that takes the drawable, and its present after it, wait
  behind those: from the present's commit to its completion 4.63 to 4.82
  ms, where its own pass takes 1.31.
- `ref_metal`'s frames take 0.62 to 0.66 ms of GPU time at this size, it
  is not GPU bound (it renders at the cap), and from a drawable's
  acquisition to its present's end it takes 2.24 to 2.28 ms: it presents
  17 to 19 percent above the old loop.

## The experiments

Builds of main changed in one place each, never committed: the frame
semaphore raised to two or one rather than three (frames in flight, with
display sync on and off), and the present's command buffer made from the
frame queue (`mgl_queue`) rather than the present queue. The shim at
5120x2880 fullscreen, two batches (21:10 to 21:12, load 2.14 to 2.68;
21:13 to 21:15, load 1.84 to 2.33), two runs per build in each, the old
loop in both:

| Build | Presented a second | Rendered a second | Drawable acquired to present completed | Waiting for a frame's end |
|---|---|---|---|---|
| Old | 454.6 to 455.6; 446.5 to 454.9 | the same | | |
| Main, three in flight | 382.5 to 387.3 | 700.3 to 702.7 | 5.65 to 5.75 | 0.79 |
| Two in flight | 433.2 to 439.4; 416.2 to 445.2 | 630.0 to 667.8 | 4.87 to 5.10 | 0.86 to 0.94 |
| One in flight | 422.8 to 425.8 | 438.5 to 454.4 | 3.94 to 4.02 | 2.07 to 2.11 |
| Three in flight, present on the frame queue | 368.5 to 389.5 | 686.4 to 689.1 | 5.34 to 5.59 | 1.05 to 1.48 |
| Two in flight, present on the frame queue | 433.1 to 464.4 | 616.5 to 625.9 | 4.21 to 4.57 | 1.17 to 1.28 |

Fewer frames on the GPU shorten the presented frame's wait. With one, its
GPU part is shortest (the frame 1.04 ms, the present 0.55), but the frame
on the CPU when a drawable comes is waiting for the previous one on the
GPU, so the drawable waits 2.1 ms for it, and rendering is no faster than
presenting. Two is the best of the three. Moving the present onto the
frame queue gains nothing by itself (three in flight), and with two its
two runs spread over the range of two alone; it would also change FIX51's
design, in which the present's wait for a drawable the window server still
holds is the present queue's, not the frame queue's. So the fix is two in
flight, with display sync off only.

## The fix

`mgl_layer_frame_begin` in `src/client/refresh/gl1/metal/mgl_layer.m`:
with display sync off (`mgl_present_apart()`), once the frame holds its
slot of the frame semaphore it waits for a second slot too, bounded by
`MGL_FRAME_WAIT_MS` as the first, and gives it back at once. So a frame
begins only when at most one other frame is on the GPU. With display sync
on nothing changes. The frame semaphore, the vertex ring, the present
slots, the layer's three drawables and the present path are as they were;
no cvar, no change to a module API. When the second wait ends at its bound
the frame goes on as before.

**Measured.** 5120x2880 fullscreen, the old loop and the fix alternated,
three rounds, then one drift-check run of main, 21:22 to 21:23, load 1.86
to 3.36:

| | Old | Fix | Main (drift check) |
|---|---|---|---|
| Presented a second | 451.9 to 456.7 | 443.8 to 449.6 | 388.3 |
| Rendered a second | the same | 630.1 to 636.0 | 698.8 |
| Drawable acquired to its present completed | about 4.6 | 4.79 to 4.85 | 5.65 |
| Frame, commit to completion | 3.36 to 3.86 | 2.31 to 2.33 | 3.76 |
| Present, commit to completion | | 3.56 to 3.65 | 4.65 |
| Present, GPU start to end | | 1.32 to 1.33 | 1.31 |
| A drawable acquired ahead, waiting for a frame's end | | 0.93 to 0.94 | 0.81 |
| Present slot held, presented frames | | 4.33 to 4.35 | 5.24 |

Main's drift-check run is inside main's range of the earlier batch (382.5
to 388.6). At 1920x1080 windowed, where the GPU is not full (the frames'
GPU time 30 to 49 percent of the interval), main and the fix alternated, two
rounds, 21:29 to 21:30, load 2.93 to 3.12: the fix 506.1 to 528.2 a
second, main 519.6 to 568.5, both rendering at the cap (995.2); the
ranges overlap.

### What is left

From a drawable's acquisition to its present's end the fix takes 4.8 ms
and the old loop about 4.6, and the window server's part is the same, so
the fix presents 1 to 3 percent fewer. The difference is the present
apart:

- the drawable acquired ahead waits for the frame being drawn to end,
  0.93 ms, where the old loop's frame began with its drawable;
- the present is a full-size pass of its own after the frame, 1.32 ms of
  GPU time beside the next frame, where the old loop's frame wrote the
  drawable itself.

Both are FIX51's design (a frame waits for no drawable, and is presented
from its present slot's target), which the card keeps unless it is the
cause. Among the builds above none that keeps that design presents above
the old loop at this size: one frame in flight shortens the GPU part, but
the drawable then waits 2.1 ms for the frame on the CPU.

## The windowed slow phase

1920x1080 windowed, the old loop and main alternated, both renderers.
Two batches: three rounds at 21:26 to 21:28 (load 3.23 to 3.47) and two at
21:42 to 21:43 (load 2.65 to 2.86).

| Renderer | Old | Main |
|---|---|---|
| shim | 409.5 to 413.4; 431.9 to 433.2 | 502.6 to 547.7; 518.6 to 520.1 |
| `ref_metal` | 415.4 to 451.6; 412.8 to 418.2 | 504.0 to 519.0; 511.8 to 517.7 |

Every run of main is above every run of the old loop in its cell. Neither
batch was in the window server's slow phase: the old loop read inside or
above its stored range in both (round 4's slow phase had it at 289.7 to
343.0, and main at 233.7 to 297.2). So whether the new loop falls below
the old one in a slow phase is still not measured; it did not happen in
the 39 minutes of this card's runs. In every 1920x1080 run of main no
frame found every present slot held, and the queue that acquires ahead
waited in `nextDrawable` 57 to 62 percent of the interval (a mean of 1.00
to 1.24 ms a drawable): the window server's pace limits both loops here,
as FIX51 found.

## Baselines and drift checks

The stored baselines (in the artifacts folder, not in the repository):

- `d7cef009` at 5120x2880, the shim: the drift-check runs read 378.3,
  376.4 and 384.6, below the stored 384.6 to 404.2; those three runs
  replace it.
- `718e59d4` at 5120x2880, both renderers: missing, measured once at 20:59
  to 21:00 (load 2.54 to 2.98): the shim 445.7 to 453.4, `ref_metal` 443.9
  to 464.9. That is above FIX51 round 3's old loop at this size (408.7 to
  418.0, 362.7 to 398.8): the host's window server is faster now than then,
  so this card compares only within a batch.
- `718e59d4` at 1920x1080, both renderers: the drift-check runs read 438.9
  (the shim) and 432.8 (`ref_metal`), above the stored 339.4 to 409.2 and
  378.0 to 424.6; measured again at 21:31 to 21:32 (load 2.02 to 2.50),
  412.8 to 451.4 and 414.9 to 422.6, and replaced.

## Pins

`world`, `hud2d`, `particles`, `models`, `dlights` and `skywater`
(`PINS_SEED=1`, the default 640x480 and the harness's display sync), and
scratch copies of `world`, `hud2d` and `particles` that add `set r_vsync
0` (deleted after), recorded for `metal` and `gl1-metal` with builds of
`ddbfb6ff` (`--run 2`) and `bf58931b` without the probe, each engine
binary beside its own renderer modules (`PINS_BIN`), 21:33 to 21:42, load
2.25 to 3.27. The eighteen pairs compared at `--threshold 0
--bad-threshold 0`: 23 frames each, 0 failed, worst score 0, worst bad 0;
all 414 frame files are byte for byte equal (`cmp`). The two builds'
`ref_metal.dylib` are byte for byte equal too.
