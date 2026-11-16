# The shim's present holds per context, and one deadline per wait stage at teardown (FIX77)

FIX69 ([its runbook](FIX69.md)) bounded the shim's present wait and drained
`ref_metal`'s presents at teardown, and its review left two weaknesses on a
GPU hung past the bound ([What is left](FIX69.md#what-is-left)): the shim's
hold counter was a static array indexed by slot, so a handler that
completed after a teardown gave up on its slot counted against the next
context's slot; and teardown's waits added up, about 50 s for the shim with
every slot hung, where one 5 s deadline was meant. The shared runbook's
method applies: [how a run works](../plans/metal-port-harness.md#how-a-run-works)
and the [pin arguments](../plans/metal-port-harness.md#pin-arguments).

**Verdict.** A late handler from a torn-down context now counts down its
own context's hold and nothing of the next context's (a fault probe shows
main's handlers taking from the new context's counts, the fix's from their
own). With every present slot held past the bounds, the shim's teardown
took 50.0 s on main and 10.0 s with the fix; `ref_metal`'s waited 5 s twice
and counted the drain twice on main, once and once with the fix. The host
test holds the drop's decisions (one deadline per stage, no second wait on a
slot given up on). Pins are byte for byte main's, and the shim's present
rate at 5120x2880 is within main's run-to-run spread.

## The changes

**The shim's holds are made per context** (`src/client/refresh/gl1/metal/mgl_layer.m`,
`mgl_hold.h`). `MGLPresentHold`, an object made for each slot of each
context by `mgl_layer_create`, carries the slot's count (`mgl_hold_t`) and
its semaphore. The present's completed handler and the one of the command
buffer that ends its wait capture it, as `ref_metal`'s present handlers
capture their `rmb_present_live`, so a handler that runs after its context
was torn down counts down that context's hold and signals that context's
semaphore. The counts' three steps are `mgl_hold_commit`, `mgl_hold_extend`
(the compare-and-swap from 1 to 2 that FIX69 made) and `mgl_hold_release`.

**The drop's waits share one deadline per stage** (`mgl_drop_presents` in
`mgl_hold.h`, called by `mgl_ahead_drop`). Every slot but the open frame's
is waited for with one deadline, `MGL_GPU_WAIT_MS`; the slots still held
then have their presents' waits ended (FIX69's `mgl_present_end_wait`) and
are waited for with one more deadline for them all. A slot still held after
that is lost: it is left held, one line says how many, and a later drop
does not wait for it again; it only takes it back, at once, if it is free
by then, and the slot's next present clears the mark. So a drop on a hung
GPU takes two deadlines at most, whatever the number of slots (main: one,
then one more for each slot, 25 s with four), and teardown's second drop
(`mgl_release_all`) takes none. The comment at the first drop in
`mgl_layer_destroy` said `mgl_release_all` "finds none left"; it now says
that it waits for none, every slot being free or one the first drop gave up
on. Turning display sync on runs one drop: 10 s at most where it was 25 s.

The drop's waits are the caller's (`mgl_drop_take`, `mgl_drop_give`,
`mgl_drop_end_wait`, `mgl_drop_deadline`: the slot semaphores and
`dispatch_time_t`, with `DISPATCH_TIME_NOW` as 0), so the host test runs the
same function on a fake clock. The present path from FIX51, FIX68 and FIX69
is unchanged: the same events, the same signal past the bound, the same
holds.

**`ref_metal`'s second drain does not wait again** (`src/client/refresh/metal/rmb_device.m`).
When `rmb_present_drain` gives up at the bound, it counts and logs that
once, leaves the presents it gave up on to their group (their blocks hold
it) and starts a new `rmb_present_live` for the presents after them, and
says so to `rmb_ahead_drop`, which then waits for the slots with the same,
spent deadline, as before. A drain that finds every present done tells it
that a slot still held is held by a present an earlier drain gave up on,
and `rmb_ahead_drop` takes such a slot only if it is free at once, without
a line. Teardown runs `rmb_detach` twice (`RI_ShutdownContext`, then
`rmb_close`): the second no longer waits 5 s for the same present, and
`<k> waits for them past the bound` counts it once.

## The builds

| Name | Commit | What it is |
|---|---|---|
| main | `4ced30f5` | `origin/main` when this card began (FIX69 merged) |
| fix | `ec11574c` | this card's code; the commits after it change only this document |
| base | `3d53f722` | main at FIX69's start, the commit of the stored rate baseline |

Every engine run is of a build exported with `git archive` (Makefile, `src`,
`third_party/sdl3.make`), with this card's probes (never committed), built
with `make -j8 ref_gl1 WITH_METAL_SHIM=yes` and `make -j8 ref_metal`, beside
the engine and game of this card's own build (the commits change no engine
code). The rate probe is FIX69's: at most every 100 ms one line with the
time (`CLOCK_UPTIME_RAW`) and the shim's counts of frames ended and of
frames without a drawable. The fault probes are below. On 2026-11-16, local
time, on the bare-metal host of FIX51 to FIX69 (60 Hz 5K display); before
every run the screen was unlocked and no other `quake2` ran, and every
build, run, check and pin call ran under the artifacts folder's `heavy.sh`,
which holds the host's timing lock.

## The host test

`test_present_holds` (`tests/metal/mgl_host_test.c`, run by
`tests/check-metal-shim.sh`) drives `mgl_drop_presents` on a fake clock
(a bound of 5000): every slot hung, where the drop must end at two bounds
and give up on each slot once; the second drop of teardown, which must not
wait, end a wait or count a slot again; a slot given up on whose present has
completed since, taken back at once; a new present on such a slot, waited
for again while the others are not; one shared second deadline (slots 0 and
2 hung, slot 1 completing 4000 after its wait is ended: the drop ends at
10000, where main's ended at 19000); slots that come back in time and the
open frame's, never waited for. It also runs the counts: a late handler
from a torn-down context counts down its own context's hold, and the new
context's present keeps its slot and can still have its wait ended.

`025da881` holds the test with `mgl_hold.h` as main's drop, extracted as it
was: `mgl_host_test: 550 checks, 18 failed`, every failure one of the
deadline or second-drop checks. With the fix (`a6e8f12a`): `550 checks, 0
failed`.

## The engine checks

Runs at 1280x720 windowed, `r_vsync 0`, `demo1.dm2` at its own pace, under
`MTL_DEBUG_LAYER=1`, a console script of `wait`s then `quit`; the fix
(06:23, load 2.41 to 2.22). Starting in `ref_metal`: `vid_restart`,
`vid_renderer gl1` and `vid_restart`, back to `metal`, `quit`; then the same
starting in the shim. Both exited 0, `Metal API Validation Enabled` and no
validation message. Every `ref_metal` session ended with `0 waits for them
past the bound` (1536, 922, 981 and 970 presents apart), `0 failed command
buffers` and `0 live objects (...), 0 unreleased, 0 unmatched`; every shim
session with `0 present waits ended; with display sync off at most 2 frames
on the GPU, 0 past the cap`, both of its drops taking 0.0 to 0.1 ms.

## The fault probes

**A skipped signal no longer hangs past the bound.** The first probe left
out one presented frame's signal of its slot's event, as FIX69's did, and
held back the command buffer that ends the present's wait. The GPU itself
then failed the present (`Caused GPU Timeout Error
(kIOGPUCommandBufferCallbackErrorTimeout)`) about 5 s after its commit, and
the slots came back within the first stage on main and the fix alike. So a
slot held past both bounds was made the other way: the probe's completed
handler of one present, 2.5 s after the first context was made, waits
before it counts the hold down, and the presents' handlers after it wait
with it, so the slots stay held whatever the GPU does.

**The worst case** (`FIX77_SLOW_MS` long: the handler waits past the run),
the shim, `quit` about 1.7 s after the handler began (06:26 to 06:29, load
1.82 to 1.73). Every drop began with all four slots held.

| Build | First drop | Second drop (`mgl_release_all`) | Teardown |
|---|---|---|---|
| main | 25010.8 ms, four `still on the GPU` lines | 25009.5 ms, four waits ended again, four lines | 50024.5 ms |
| fix | 10003.9 ms, `4 presents are still on the GPU after 10000 ms, not waited for again` | 0.0 ms | 10008.1 ms |

**A late handler in the next context.** The same handler waits instead
until 2 s after the next context was made, with a `vid_restart` about 1.6 s
after it began; the probe prints, as each late handler counts down, what it
takes from (06:30 to 06:31, load 2.48 to 1.93). On main the old context's
four present handlers took from the new context's counts: `takes from
context 2's count of slot 0, 0 -> -1`, and the same for slots 1 to 3. They
found the new context's slots free, so the next present of each set the
count back to 1; had a slot been held by a present then, the count would
have gone from 1 to 0, the slot given back on the old context's semaphore,
and the new present's own completion, finding 0, would never have given it
back. With the fix each late handler took from its own count, `1 -> 0`,
with the new context's counts beside it (`1 0 0 0`, then `0 0 0 0`, its own
presents completing), and the new context's teardown found no slot held
(0.1 ms). The first context's teardown took 50.0 s on main and 10.0 s with
the fix here too.

**`ref_metal`, a slow present handler** (`FIX77_SLOW_MS=15000`, from 2.5 s;
`quit` about 1.7 s later; 06:24 to 06:25, load 2.82 to 2.43). Main: the
first `rmb_detach` waited 5 s for the presents, `a present is still on its
way after 5000 ms`, four slots still on the GPU; the second waited 5 s
again with the same lines; `teardown: 1112 presents apart, 2 waits for them
past the bound`. The fix: the first `rmb_detach` the same, 5 s; the second
took no measurable time and printed nothing; `1198 presents apart, 1 waits
for them past the bound`.

The shim's module kept its statics across every `vid_restart` and renderer
switch here (the probe's count of contexts went 1, 2, 3 in one process), so
the late handlers above ran in code that was still mapped.

## Present rates

The shim at 5120x2880 `vid_fullscreen 1`, `r_vsync 0`, own pace (FIX68's
cell), as [FIX69 counted it](FIX69.md#how-a-run-was-counted): 06:32 to
06:33, load 1.82 to 3.34. A drift-check run of base first: 439.8 presented
a second (636.8 rendered), inside the stored baseline of `3d53f722`
(416.7 to 450.0), so that baseline holds. Then main and the fix, three
rounds, alternated, the order flipped each round; presented a second:

| Build | Runs | Mean |
|---|---|---|
| main | 452.0, 441.5, 441.5 | 445.0 |
| fix | 443.5, 438.4, 457.1 | 446.3 |

Rendered: main 628.0 to 636.6, the fix 628.2 to 634.3. Main's three runs
are stored as the baseline of `4ced30f5` in the artifacts folder. The fix
adds nothing per frame but a pointer where an index was.

## Pins

`world`, `hud2d`, `particles`, `models`, `dlights` and `skywater`
(`PINS_SEED=1`, the default 640x480 and the harness's display sync), and
FIX68's scratch copies `fix68vs0-world`, `fix68vs0-hud2d` and
`fix68vs0-particles` that add `set r_vsync 0` (made again with the same
settings hash, deleted after), recorded for `metal` and `gl1-metal` with
this card's build of `ec11574c` (`PINS_BIN`), 06:34 to 06:37, and compared
with main's stored records, FIX69's `5536e6c7` (the code of `4ced30f5`), at
`--threshold 0 --bad-threshold 0`: 18 pairs of 23 frames each, 0 failed,
worst score 0, worst bad 0; all 414 frame files are byte for byte equal
(`cmp`).

## The checks

On `cadeacc7` (the fix's code and this document), each under `heavy.sh`,
06:40 to 06:43: `make test` (`yq2test: 173 tests, 0 failed`),
`tests/check-metal-shim.sh` (550 checks, 0 failed, shim built),
`tests/check-ref-metal.sh` (12824 checks, 0 failed, `ref_metal` built),
`tests/check-pins.sh` (the selftest), `tests/check-doc-links.sh` and
`tests/check-no-local-identity.sh`, all ok; the builds `make client`,
`make ref_metal`, `make ref_gl1 WITH_METAL_SHIM=yes` and
`make STATIC_MODULES=1 STATIC_RENDERER=metal client`, all without an error
or a warning from the changed files.

## What is left

- `ref_metal`'s second `rmb_detach` still waits again for the frames
  (`rmb_drain`, the frame semaphore): on a GPU hung past the bound that is
  5 s in each of the two. This card is about the presents' waits.
  [FIX84](#fix84-the-marks-end-with-what-they-mark) does this.
- A command buffer that fails after a teardown gave up on it is counted in
  a static count (`count_failed` in the shim, `rmb_failed` in `ref_metal`),
  which the next context or session has reset: it counts there.

## FIX84: the marks end with what they mark

FIX77's review left four notes on the teardown waits; FIX84 settles them.
Its builds: base `2da65254` (TASK121's head, which FIX84 is stacked on:
main of FIX77 plus the colour-space restore in `rmb_detach`) and the fix
`0299c238`.

**The changes.** The shim's `lost` mark on a slot now also ends when a
frame takes the slot (`mgl_hold_take`, called by `mgl_present_take`), not
only when a present is committed: a slot given up on, freed, taken by a
frame that ends without a present (display sync on commits none) and then
held again is waited for by the next drop with the shared deadline, where
the mark made it look once and leave it. In `ref_metal` a drain that gives
up marks the group it gave up on (`rmb_present_spent`) and no longer
starts a new one at once: a later drain of that group (teardown's second
`rmb_detach`) waits for nothing and counts nothing, and the next present
starts a new group, whose drain waits the shared deadline for the group
and for the slots; a slot still held when such a group is done is held by
a present given up on before, is waited for with the same deadline, and
marks the group when it does not come back. A group that cannot be made
leaves the present uncommitted. `ref_metal`'s frames share one deadline
(each had a fresh one), and the slots a drain gave up on
(`rmb_frames_lost`) are not waited for again until `rmb_attach` puts on a
new layer (a frame begins only with one, `rmb_frame_begin`): the second
`rmb_detach` of teardown and `rmb_attach`'s own, after
`RI_ShutdownContext`'s, take them back only if they are free at once, as
the shim's `mgl_release_all` waits for no frame. The shim's teardown wait
for its frames shares one deadline too.

**The host test** (`test_present_holds`) runs the first through the seam:
slot 0 given up on at 10000, freed, `mgl_hold_take`, given back, held
again until 13000; the next drop must take it at 13000. `4696fc2a` holds
the test with `mgl_hold_take` leaving the mark: `555 checks, 3 failed`;
the fix: `555 checks, 0 failed`. Mutants, each in a scratch copy, all
killed: the take a no-op (3 failed), the take setting the mark (4), the
take touching the count (1), the drop ignoring the mark (3), every slot
looked at once (10), a slot taken back keeping its mark (1), a commit
keeping it (2). The test's comment now says that it holds the seam only:
`tests/check-metal-shim.sh` builds no `mgl_layer.m` into it, so the
handlers' capture of their context's hold, the take's call and the waits
on the semaphores are held by the probes, not by it. The `ref_metal` items
have no host test: `ref_metal` may not include a `gl1/` header
(`tests/check-ref-metal.sh`), so `mgl_hold.h` cannot carry its decisions,
and the card's files hold no `ref_metal` test; the probe below holds them.

**The fault probe, on a VM.** This host is a virtual machine: its Metal
device is `Apple Paravirtual device`, not the bare-metal GPU of the runs
above. The probe is built as FIX77's were, from `git archive` exports of
base and fix with the probe (never committed), with `make -j3 ref_metal`
and `make -j3 ref_gl1 WITH_METAL_SHIM=yes`, beside the engine and game of
the fix's own build. Once the console script has written a file
(`condump`), every completed handler that the renderer watches
(`rmb_watch_until`, `mgl_watch`) waits 30 s, past the run, before the
rest of its command buffer's handlers run: frames and presents are both
held. The script then waits two frames and quits. 1280x720 windowed,
`r_vsync 0`, `demo1.dm2`, `MTL_DEBUG_LAYER=1` (`Metal API Validation
Enabled`), every run under the host's timing lock, 15:29 to 15:31 local
time, one run each:

| Renderer | Build | First teardown stage | Second | Teardown |
|---|---|---|---|---|
| `ref_metal` | base | `rmb_detach`: frames 5000.6 ms, presents 5003.3 ms | `rmb_detach`: frames 5004.1 ms, presents 0.0 ms | 15008 ms |
| `ref_metal` | fix | `rmb_detach`: frames 5005.3 ms, presents 5002.7 ms | `rmb_detach`: 0.0 ms, 0.0 ms | 10008 ms |
| shim | base | frames 5002.0 ms, first drop 10004.5 ms | `mgl_release_all` 0.3 ms | 15006.9 ms |
| shim | fix | frames 5000.5 ms, first drop 10005.0 ms | `mgl_release_all` 0.4 ms | 15006.0 ms |

So each stage waits one deadline (the shim's drop two, as FIX77 made it)
and the second `rmb_detach` none: base printed `the GPU still holds a
frame after 5000 ms` twice, the fix once. Both `ref_metal` runs ended with
`1 waits for them past the bound` and `1 unreleased` (the command queue,
which its held command buffers keep alive); the shim's with `3
presents are still on the GPU after 10000 ms, not waited for again`. The
shim's numbers do not move: with every frame held its first wait timed out
at once on base too; the shared deadline bounds the case where the frames
complete one by one. The first note (a slot freed and taken again) and the
second (a group done while an older present holds a slot) are not
reproduced by the probe: the handlers held here complete in order, so a
newer group cannot be done while an older present is held; the host test
holds the first, the code the second.

**Pins.** This host holds no `metal` reference in the owner's pin store
for any of `world`, `hud2d`, `particles`, `models`, `dlights` and
`skywater`, and none of `gl1-metal` for `particles`, `models` and
`dlights`; none was recorded. `gl1-metal` `world` and `hud2d` were
recorded with the fix's build into a scratch store, with the references'
settings hash, and every frame compared with `cmp` against the newest
complete reference: 23 of 23 byte for byte equal in each. The `skywater`
reference has another settings hash (5 frames): not compared.

**The checks.** `tests/check-metal-shim.sh` (`555 checks, 0 failed; shim
built`) and `tests/check-ref-metal.sh` (`12831 checks, 0 failed`, `ref_metal
built`); `make client game`, `make ref_metal` and `make ref_gl1
WITH_METAL_SHIM=yes` with no warning from the changed files.
