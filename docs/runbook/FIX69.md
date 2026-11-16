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
(`src/client/refresh/gl1/metal/mgl_layer.m`). The present slots' events
are now `MTLSharedEvent`s. When a present apart is committed its slot's
commit time is kept; as each frame begins (`mgl_present_bound`) a present
still pending `MGL_GPU_WAIT_MS` after its commit whose event is below the
value it waits for has that value set by the CPU (`signaledValue`), with
one line on stderr. The present then runs and its completion gives the slot
back, as for any present. Only a frame whose command buffer ended before
its signal (in error) leaves the value unreached that long, so this is the
case the FIX51 review named in its round 6: before, that present waited on
the GPU forever and its slot was held for the rest of the context, and the
presents behind it on the present queue waited with it. Teardown and
turning display sync on (`mgl_ahead_drop`) end such a wait at the slot's
bound and wait for the slot once more. FIX51's design is unchanged: the
present still waits on the GPU for its own slot's event, which only the
frame holding the slot signals; the CPU signals only past the bound.

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
drained, adds the present waits the CPU ended, the most frames on the GPU
as a frame began with display sync off (counted from the frames committed
and not completed, the new one included: 2 while the cap holds, 3 without
it) and the frames whose second wait timed out.
