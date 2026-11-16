# In-flight presents at teardown, and the shim's present wait (FIX69)

FIX51 ([its runbook](FIX51.md)) presents a frame apart from it with
display sync off, and its review left two gaps for this card: `ref_metal`'s
teardown did not wait for the presents' blocks and completed handlers, and
the shim's present waited on the GPU for its slot's event with no time
limit of its own ([round 4](FIX51.md#round-4)). FIX68's review
([its runbook](FIX68.md)) added the header comment of the shim and a way
to see whether its cap of two frames on the GPU held. The shared runbook's
method applies: [how a run works](../plans/metal-port-harness.md#how-a-run-works)
and the [pin arguments](../plans/metal-port-harness.md#pin-arguments).

**Verdict.** `ref_metal`'s teardown now waits until every present has
completed and all of its handlers have run before it releases the present
state; the shim's present wait ends `MGL_GPU_WAIT_MS` after its commit at
the latest, and a fault run shows its slot coming back and the presents
going on; the depth target's size is in `rm_frame_remake`'s verdict, which
the host test holds. The presented rates of the final build are within
main's run-to-run spread in every measured cell (below), and the pins are
byte for byte main's. A first
version that made the shim's events shared events cost the shim at
5120x2880 about a tenth of its presents and was replaced. FIX68's cap
costs nothing measurable at 1920x1080 windowed.

## The changes

**`ref_metal` drains its presents** (`src/client/refresh/metal/rmb_device.m`).
A present handed to the serial queue that commits it (`rmb_present_order`)
is in flight until three things have run: its block on that queue and both
of its completed handlers (the watch's, which counts a failed command
buffer, and the one that gives the present slot back). A dispatch group,
`rmb_present_live`, is entered three times when the present is handed over
and left once at the end of each; a present the block does not commit (its
frame still on the GPU at `RMB_GPU_WAIT_MS`) has the two handler parts left
by the block, since an uncommitted command buffer never completes.
`rmb_ahead_drop`, which teardown (`rmb_detach`, from `rmb_close` and
`rmb_attach`) and turning display sync on call, waits for the group with
the slots' bound (`RMB_GPU_WAIT_MS`, one deadline for all) before it drops
the present targets, and `rmb_close` releases the present queue, the order
queue, the group and the semaphores only after that. It then prints
`ref_metal: teardown: <n> presents apart, <k> waits for them past the
bound` on stderr (`rm_presents_format`, host-tested); `k` is 0 in a clean
teardown. Whatever the order in which Metal calls a command buffer's
handlers, the wait ends only after every one of them, so nothing here
relies on that order. The module stays mapped after the engine's `dlclose`
(`RM_KeepMapped`, `RTLD_NODELETE`), so a handler that outlives the bound
runs mapped code; with this change it no longer runs after its state was
released, save past the bound, which the line counts.

**The shim's present wait has a time limit**
(`src/client/refresh/gl1/metal/mgl_layer.m`). When a present apart is
committed, its slot's commit time is kept and the slot is held by one
command buffer, the present (`mgl_present_holds`, 1). As each frame begins
(`mgl_present_bound`), a present that has not completed `MGL_GPU_WAIT_MS`
after its commit has its wait ended: a command buffer of the frame queue
signals the slot's event with the value the present waits for
(`mgl_present_end_wait`), with one line on stderr. The slot is then held by
both command buffers (2, by a compare-and-swap from 1, so a present that
completed in between is left alone), and the last of them to complete gives
it back. So no later frame of the slot can signal the event before that
signal has run, and a signal of a value the frame gave already changes
nothing. The case is the one the FIX51 review named in its round 6: a
frame whose command buffer ends in error before its signal. Before, its
present waited on the GPU forever, its slot was held for the rest of the
context, and the presents behind it on the present queue waited with it.
Teardown and turning display sync on (`mgl_ahead_drop`) end such a wait at
the slot's bound and wait for the slot once more. FIX51's design is
unchanged: the present still waits on the GPU for its own slot's event,
which only the frame holding the slot signals in every frame that does not
fail.

The first version of this change made the slots' events `MTLSharedEvent`s,
so that the CPU could set the value itself (`1f45a963`). That cost the
shim at 5120x2880 about a tenth of its presents
([below](#a-shared-event-costs-the-shim-at-5120x2880)); the events are
`MTLEvent`s again, as FIX51 made them.

**The depth target follows `rm_frame_remake`**
(`rm_pure.c`, `rmb_encode.m`, `header/rmb.h`). `rm_frame_held_t` holds the
depth target's size, and `rm_frame_remake` names `RM_REMAKE_DEPTH` when it
is not the frame's size, also when the colour target is (with display sync
off, a present slot's target, made for the frame's size by
`rmb_present_take`). `rmb_frame_begin` makes again only what the verdict
names, where FIX51 round 4 checked the depth target's size apart from it.
The host test `test_frame_remake` fails on a revert (3 checks, below).

**The shim's header and teardown line** (`mgl_layer.m`). The list of
bounded waits now describes FIX68's second wait on the frame semaphore: it
takes a second free slot and gives it back at once, so at most two frames
are on the GPU; when it times out the frame is not skipped, it goes on with
three on the GPU, and its begin has waited up to twice
`MGL_FRAME_WAIT_MS`. The teardown line, printed after the presents are
drained, adds the present waits ended at the bound, the most frames on
the GPU as a frame began with display sync off (counted from the frames
committed and not completed, the new one included: 2 while the cap holds,
3 without it) and the frames whose second wait timed out:

```
mgl: 2009 frames, 1017 without a drawable, 0 skipped, 0 draws past the vertex ring, 0 present waits ended; with display sync off at most 2 frames on the GPU, 0 past the cap
```

## The builds

| Name | Commit | What it is |
|---|---|---|
| main | `3d53f722` | `origin/main` when this card began (FIX68 merged) |
| shared | `1f45a963` | this card's first version: the shim's events `MTLSharedEvent`s, signalled by the CPU past the bound |
| exp | `3d53f722` changed in one line, never committed | main with `newSharedEvent` for the shim's events, nothing else |
| fix | `5536e6c7` | this card's code as it stands: the shim's events `MTLEvent`s again, a late wait ended by a signal on the frame queue |
| nocap | `3d53f722` changed in one line, never committed | main without FIX68's second wait on the frame semaphore |

The engine checks below ran the fix with a check probe, and main and the
fix with a handler probe, on top of the rate probe; none of them is
committed.

`5536e6c7` is the last commit that changes code; the commits after it
change only this document and one number in a comment of `mgl_layer.m`
(8 became 9, the measured shortfall below). `ref_metal` is the same in shared and fix.
Every rate is from a build exported with `git archive`, with a probe of
this card's (never committed) and built with `make -j8 ref_metal` and
`make -j8 ref_gl1 WITH_METAL_SHIM=yes`, beside the engine and game of this
card's own build (the commits change no engine code). The probe prints, at
most every 100 ms, the time (`CLOCK_UPTIME_RAW`) and the renderer's own
counts of frames ended and of frames without a drawable (`rmb_counts`,
`count_frames`), so a frame presented is a frame ended with a drawable. It
is not FIX68's probe (which printed every frame), so its numbers are
compared only within this card's batches.

## How a run was counted

FIX51 round 3's own-pace runs, as FIX68 ran them: the launch line
(`vid_highdpiaware 0`, `logfile 2`, `metal_stats 1`, `s_initsound 0`,
`-portable`), `r_vsync 0`, `timedemo 0`, `vid_maxfps 999`, `demo1.dm2` at
its own pace, 8 s from launch, then the engine ended by its own pid;
counted from 1 s after the first probe line (the first frame) to the last.
Main and the build under test alternate run by run, the order flipped each
round. On 2026-11-16, local time, on the bare-metal host of FIX51 and
FIX68 (60 Hz 5K display). Before every run the screen was unlocked and no
other `quake2` ran; every build, batch, check and pin call ran under the
artifacts folder's `heavy.sh`, which holds the host's timing lock. Each
table gives the one-minute load (`sysctl -n vm.loadavg`) of its batch.

No stored baseline of main with any probe existed (FIX68's `bf58931b` is
main's shim, but measured with FIX68's probe), so main was measured in the
same batches, alternated with the build under test, and its runs are now
the stored baseline of `3d53f722` (four cases, in the artifacts folder):
every run of main after the first batch is a drift check of that baseline.

## A shared event costs the shim at 5120x2880

Batch one, 01:01 to 01:06, load 2.61 to 4.10: main and the shared build,
five rounds at 1920x1080 windowed, three at 5120x2880 `vid_fullscreen 1`.
Presented a second, range and mean:

| Cell | Renderer | Main | Shared |
|---|---|---|---|
| 1920x1080 windowed | shim | 502.7 to 531.4, mean 518.1 | 471.1 to 509.6, mean 492.9 |
| 1920x1080 windowed | `ref_metal` | 506.5 to 546.0, mean 519.9 | 479.5 to 513.8, mean 503.0 |
| 5120x2880 fullscreen | shim | 416.7 to 450.0, mean 433.8 | 368.2 to 400.5, mean 384.3 |
| 5120x2880 fullscreen | `ref_metal` | 524.6 to 548.4, mean 539.8 | 496.6 to 535.7, mean 512.4 |

The shim at 5120x2880 rendered 716.5 to 742.7 frames a second where main
rendered 627.9 to 673.6: more frames drawn, fewer presented, the pattern of
FIX68's main before its cap. Main changed in the one line (exp), three
rounds alternated with main at 5120x2880, 01:08 to 01:09, load 2.20 to
2.42: exp presented 389.0 to 403.0 and rendered 711.5 to 724.5, main
426.1 to 438.5 and 633.7 to 648.0. So the shared event alone is the cost:
a present waits on the GPU for its frame's signal, and with a shared event
that wait ends later. (A batch of the same pair at 01:07 ran a build that
`make` had not rebuilt, the edit and the copy in the same second; it is
discarded, its main runs read 418.7 to 445.4.) The fix keeps FIX51's
`MTLEvent`s.

## The present rates of the fix

Batch two, 01:28 to 01:33, load 1.78 to 4.45, main and the fix, five
rounds at 1920x1080 windowed and three at 5120x2880 fullscreen. Presented
a second, range and mean; every run of both rendered at the cap (995.0 to
995.1) but the shim at 5120x2880 (main 629.1 to 634.5, the fix 630.8 to
640.5):

| Cell | Renderer | Main | Fix |
|---|---|---|---|
| 1920x1080 windowed | shim | 528.8 to 551.0, mean 535.4 | 522.0 to 636.8, mean 548.6 |
| 1920x1080 windowed | `ref_metal` | 512.9 to 518.3, mean 516.5 | 511.5 to 533.5, mean 521.8 |
| 5120x2880 fullscreen | shim | 438.5 to 446.1, mean 442.7 | 437.2 to 445.5, mean 440.9 |
| 5120x2880 fullscreen | `ref_metal` | 533.8 to 572.3, mean 552.4 | 547.9 to 549.8, mean 549.0 |

The shim's 636.8 at 1920x1080 is one run (the window server's pace moves by
that much over minutes, FIX51 round 4); without it the fix reads 522.0 to
535.7, mean 526.5. `ref_metal`'s code is the same in batch one and batch
two; the fix sat under main in batch one and at or above it in batch two.

**FIX68's cap at 1920x1080 windowed.** FIX68's review asked whether its
cap of two frames on the GPU costs anything where the GPU is not full
(over two rounds there, the capped build 4.9 percent under by mean). Main
against main with the second wait taken out (one line, never committed),
the shim, five rounds alternated, 02:03 to 02:05, load 1.93 to 2.91: main
511.1 to 561.1 presented a second, mean 528.3; without the cap 517.3 to
536.6, mean 525.5. The cap costs nothing measurable there.

A third batch for `ref_metal` alone, three rounds of each cell, 01:46 to
01:48, load 1.81 to 3.75, settles batch one's gap: at 1920x1080 the fix
518.8 to 525.5 (mean 522.2), main 515.0 to 519.7 (516.7); at 5120x2880 the
fix 538.5 to 547.5 (544.3), main 539.8 to 550.9 (545.8). Over all three
batches, 13 rounds at 1920x1080 and 9 at 5120x2880, `ref_metal`'s fix
reads a mean of 514.7 and 535.2 presented a second, main 517.9 and 546.0
(0.6 and 2.0 percent under); without batch one, 522.0 against 516.6 and
546.7 against 549.1 (1.0 percent above, 0.4 percent under). The fix's two lowest runs in each cell are batch one's, and so are
main's lowest. What the fix adds on `ref_metal`'s per-frame path is three
enters and three leaves of a dispatch group and one counter per present;
the waits are at teardown and at turning display sync on only.

## The engine checks

A build of the fix with a check probe (never committed) on top of the rate
probe: at the start of each drop of the present state (teardown, display
sync turned on) one line with the present slots then held by a present, and
two faults, below. Runs at 1280x720 windowed, `r_vsync 0`, the demo at its
own pace, under `MTL_DEBUG_LAYER=1`, a console script of `wait`s (about
16 ms each) then `quit`.

**A restart and a renderer switch, both renderers** (01:34 to 01:35, load
1.72 to 2.39). Starting in `ref_metal`: about 3 s, `vid_restart`, 2 s,
`vid_renderer gl1` and `vid_restart`, 2 s, back to `metal`, 2 s, `quit`;
then the same starting in the shim. Both exited 0, `Metal API Validation
Enabled` and no validation message. Every `ref_metal` session ended with
`ref_metal: teardown: <n> presents apart, 0 waits for them past the bound`
(1533, 995, 960 and 982 presents), `metal_stats` 0 failed command buffers,
and `teardown: 0 live objects (...), 0 unreleased, 0 unmatched`; every shim
session with `0 present waits ended; with display sync off at most 2
frames on the GPU, 0 past the cap`. At every drop the probe found no slot
held: the drain of the frames (`rmb_drain`, the frame semaphore) comes
first, and by its end the presents of those frames had completed too. So
these runs show a clean teardown with presents in flight when it began, but
not the drain waiting; the next check makes it wait.

**A slow present handler (`ref_metal`, main and the fix).** A handler
probe (never committed) on main's build and on the fix's: with
`FIX69_SLOW_HANDLER=30`, the present's completed handler that gives the
slot back sleeps 30 ms after it has given it and then prints the time and
its slot's semaphore; `rmb_close` prints the time and its session's four
slot semaphores as it releases the present state, so each handler is
matched to its session. The same restart and switch run as above, starting
in `ref_metal`, three `ref_metal` sessions each (just before 02:03, under
`heavy.sh`, as every run here):

| Build | Handlers of the session that ended after its release | Of |
|---|---|---|
| main (`3d53f722`) | 1, 29.7 ms after; 1, 30.3 ms after; 0 | 98, 66, 65 |
| fix (`5536e6c7`) | 0; 0; 0 | 98, 65, 66 |

On main, `rmb_ahead_drop` waited for the slot semaphores only, which the
handler signals before the rest of it has run, so in two of the three
teardowns a handler was still running when the present queue, the order
queue and the semaphores were released. With the fix every teardown waited
until the last part of every present was done, and said so: `98 presents
apart, 0 waits for them past the bound`, and so on.

**A frame that never signals (the shim).** The check probe's
`FIX69_SKIP_SIGNAL=300` leaves out the 300th presented frame's signal of
its slot's event, as a frame whose command buffer ends before its signal
(01:35, load 2.39 to 2.69). The log: the skip 0.70 s after the first
frame, while the map loaded; from 1.81 s no frame presented (the presents
behind the one that waits, on the present queue, wait with it) until
`mgl: the present of slot 0 has not completed after 5000 ms; its wait for
its frame is ended`, at 5.73 s, 5.03 s after the skip; from 0.5 s later to
the end of the run, 536.0 frames presented a second. The teardown line:
`1 present waits ended; with display sync off at most 2 frames on the GPU,
0 past the cap`; the run exited 0, with no validation message and no
failed command buffer. Before this change nothing ended that wait (FIX51
round 4: "The wait on the GPU still has no time limit of its own").

## The host test

`test_frame_remake` (`tests/metal/rm_host_test.c`) asks for
`RM_REMAKE_DEPTH` alone when only the depth target is not of the frame's
size, on each side, and for the depth target with the drawable when the
frame's size changes with a colour target already of it. On `17892fb7`
(the test, and the struct's new fields, before the fix) it failed with
`12705 checks, 3 failed`, the three new depth checks; with the fix,
`12709 checks, 0 failed` (with `test_presents_format`, the teardown line's
format).

## Pins

`world`, `hud2d`, `particles`, `models`, `dlights` and `skywater`
(`PINS_SEED=1`, the default 640x480 and the harness's display sync), and
FIX68's scratch copies `fix68vs0-world`, `fix68vs0-hud2d` and
`fix68vs0-particles` that add `set r_vsync 0` (made again with the same
settings hash, deleted after), recorded for `metal` and `gl1-metal` with
this card's build of `5536e6c7` (`PINS_BIN`), 01:35 to 01:38, load 2.69 to
3.48, and compared with main's stored records, FIX68's `bf58931b` (main's
`ref_gl1` and the `ref_metal` present path of `3d53f722`), at `--threshold
0 --bad-threshold 0`: 18 pairs of 23 frames each, 0 failed, worst score 0,
worst bad 0; all 414 frame files are byte for byte equal (`cmp`).

## The checks

On the fix's code (`5536e6c7`), each under `heavy.sh`: `make test`
(`yq2test: 163 tests, 0 failed`), `tests/check-metal-shim.sh` (515 checks,
0 failed, shim built), `tests/check-ref-metal.sh` (12709 checks, 0 failed,
`ref_metal` built), `tests/check-pins.sh` (the selftest, with the scratch
copies deleted), `tests/check-doc-links.sh` and
`tests/check-no-local-identity.sh`, all ok; the builds `make client`,
`make ref_metal`, `make ref_gl1 WITH_METAL_SHIM=yes` and
`make STATIC_MODULES=1 STATIC_RENDERER=metal client`, all without an
error or a warning from the changed files.

## What is left

- The shim's other completion handlers (its frames', its uploads') are
  still code in `ref_gl1`, which has no `RTLD_NODELETE` like `ref_metal`'s
  `RM_KeepMapped`: a handler that arrives after teardown's bounded waits
  gave up, past `MGL_GPU_WAIT_MS` on a hung GPU, could run after the engine
  unloaded the module. `mgl_layer_destroy` says so; this card does not
  change it.
- `mgl_present_holds` is a static array: a present handler that runs after
  a teardown gave up on it (a GPU hung past the bounds) and after a new
  context was made would count against the new context's slot.
- The present slot whose wait was ended still read a target its frame did
  not finish, so that one frame may show what the target held; the frame's
  command buffer had failed.
