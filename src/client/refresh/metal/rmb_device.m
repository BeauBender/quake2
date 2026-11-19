/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * =======================================================================
 *
 * The device side of ref_metal's back end (docs/plans/ref-metal-native.md,
 * section 3): the device and queue, the capabilities, the CAMetalLayer,
 * the three frame slots, the command-buffer watch, the counters and the
 * object ledger. Copied in shape from the shim's mgl_layer.m
 * (mgl_layer_create, mgl_report_capabilities, mgl_watch,
 * mgl_commit_wait, mgl_layer_destroy). ARC (-fobjc-arc): the objects
 * live in the strong globals below, shared with rmb_encode.m, and are
 * released by assigning nil.
 *
 * Every wait is bounded: a frame slot RMB_FRAME_WAIT_MS (the frame is
 * skipped after it), the GPU RMB_GPU_WAIT_MS (readback and teardown), a
 * drawable one second (allowsNextDrawableTimeout).
 *
 * =======================================================================
 */

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <CoreGraphics/CGColorSpace.h>
#include <TargetConditionals.h>

#include <os/lock.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "header/rmb.h"
#include "header/rmb_drain.h"

#if !__has_feature(objc_arc)
#error "rmb_device.m is written for ARC: compile it with -fobjc-arc"
#endif

#define RMB_FRAME_WAIT_MS 2000
#define RMB_GPU_WAIT_MS 5000
#define RMB_FAILED_LOGGED 8

/* shared with rmb_encode.m */
id<MTLDevice> rmb_device;
id<MTLCommandQueue> rmb_queue;
CAMetalLayer *rmb_layer;
dispatch_semaphore_t rmb_frame_sem;
rm_ledger_t rmb_ledger;
rmb_stats_t rmb_counts;

/* metal_colorspace (TASK121): the mode asked for, and the object while it is on the layer */
static int rmb_cs_mode;
static CGColorSpaceRef rmb_cs;
static CGColorSpaceRef rmb_cs_own;   /* the layer's own colour space, kept while ours is on it (may be NULL) */
/*
 * r_hdr (TASK196, HD2): the float16 path asked for, whether the layer is
 * set for it (RGBA16Float, extended linear sRGB, EDR), and that colour
 * space while it is on the layer
 */
static int rmb_hdr_want, rmb_hdr_layer, rmb_hdr_dead;
static CGColorSpaceRef rmb_hdr_cs;

/* rmb_encode.m */
void rmb_encode_abandon(void);
void rmb_encode_release(void);

static atomic_ulong rmb_failed;
static int rmb_sync = 1;
static rmb_frames_t rmb_frames;   /* the frame slots a drain gave up on, until rmb_attach (header/rmb_drain.h, FIX84) */

dispatch_time_t
rmb_deadline(long ms)
{
	return dispatch_time(DISPATCH_TIME_NOW, (int64_t)ms * NSEC_PER_MSEC);
}

long
rmb_frame_wait_ms(void)
{
	return RMB_FRAME_WAIT_MS;
}

/*
 * FIX51: with display sync off a frame waits for no drawable
 * (docs/runbook/FIX51.md). The window server hands a drawable back at its
 * own pace, whatever the frame costs, and that pace was the frame rate
 * twice over: a frame waited for the drawable in nextDrawable, and a
 * drawable the window server had not let go of yet held the command
 * buffer that wrote it on the GPU, and with it the frame slot.
 *
 * So with display sync off the next drawable is acquired ahead, on a
 * serial queue, and is written apart from the frame, from the very target
 * the frame was drawn into. There are RMB_PRESENTS present slots, one for
 * each of the layer's drawables and one for the frame being drawn: a
 * semaphore of one (rmb_present_sem) and a colour target
 * (rmb_present_target). A frame takes a free slot when it begins and is
 * drawn into the slot's target (rmb_present_take). When it ends with a
 * drawable ready, the present writes the drawable from that target in one
 * pass (rmb_frame_end), on rmb_present_queue: the wait for the drawable
 * holds that queue and no frame slot, and nothing copies the frame first.
 *
 * A frame can be drawn in several command buffers (a readback, the
 * in-frame split, rmb_frame_cut, MTL_GPU_TIME), and Metal states only that
 * an event a command buffer signals follows that buffer's own commands.
 * So the present waits for no event: every command buffer committed while
 * the frame holds its slot is in the frame's dispatch group
 * (rmb_present_group, rmb_watch), left when its completed handler runs,
 * and the present is committed, on the serial rmb_present_order and so in
 * frame order, once the group is empty (rmb_present_commit).
 *
 * The slot comes back when its present has completed, so its target is
 * not drawn into again while it is read; a frame that is not presented
 * gives its slot back when it ends (a later frame that draws into that
 * target is on the same queue, after it). A frame that begins with every
 * slot held is drawn into the frame's own target (rmb_encode.m) and is not
 * presented, nor is one that ends with no drawable ready: both are
 * complete and counted as without a drawable. With display sync on the
 * frame waits in nextDrawable and writes the drawable itself, as before,
 * and every frame is presented.
 *
 * A present handed to rmb_present_order is in flight until its block
 * there and both of its completed handlers have run (rmb_present_live).
 * Turning display sync on and teardown wait for that, bounded, before the
 * present state is released (rmb_present_drain, FIX69), so no handler or
 * block runs after its state is gone. A drain that gives up at the bound
 * leaves those presents to their group and marks it, so a later drain of
 * it neither waits for them nor counts them again (FIX77); the next present
 * starts a new group, and a drain of that one waits again (FIX84). The
 * decisions are header/rmb_drain.h's, on the groups' identities.
 */
#define RMB_PRESENTS (RMB_FRAMES_IN_FLIGHT + 1)   /* maximumDrawableCount (rmb_attach), and the open frame */
#define RMB_GROUP_ID(g) ((__bridge const void *)(g))   /* a group's identity for header/rmb_drain.h: compared, never kept */

static dispatch_queue_t rmb_ahead_queue;
static os_unfair_lock rmb_ahead_lock = OS_UNFAIR_LOCK_INIT;
static id<CAMetalDrawable> rmb_ahead;   /* rmb_ahead_lock: the drawable acquired ahead */
static int rmb_ahead_busy;              /* rmb_ahead_lock: an acquisition is on the queue */

static id<MTLCommandQueue> rmb_present_queue;
static dispatch_queue_t rmb_present_order;   /* serial: commits each present once its frame has completed */
static dispatch_group_t rmb_present_group;   /* the command buffers committed while the open frame holds a slot */
static dispatch_group_t rmb_present_live;    /* the presents handed to rmb_present_order, until they are done (FIX69) */
static dispatch_group_t rmb_present_spent;   /* the group a drain gave up on: not waited for again (FIX84) */
static dispatch_semaphore_t rmb_present_sem[RMB_PRESENTS];   /* 1: no frame and no present holds this slot */
static id<MTLTexture> rmb_present_target[RMB_PRESENTS];      /* the target the slot's frame is drawn into */
static int rmb_present_slot = -1;              /* the slot the open frame holds, or -1 */
static id<MTLCommandBuffer> rmb_present_cmd;   /* the present of the frame that ends, committed after it */

void rmb_watch(id<MTLCommandBuffer> cmd);
static void rmb_watch_until(id<MTLCommandBuffer> cmd, dispatch_group_t done);

/* With display sync off the frames are presented apart (with the present queue and its order queue made). */
static int
rmb_apart(void)
{
	return !rmb_sync && rmb_layer && rmb_present_sem[0];
}

/* Puts an acquisition of the next drawable on the queue, unless one is there or done. */
static void
rmb_ahead_request(void)
{
	CAMetalLayer *layer = rmb_layer;
	int go;

	if (!layer || !rmb_ahead_queue)
	{
		return;
	}

	os_unfair_lock_lock(&rmb_ahead_lock);
	go = !rmb_ahead_busy && !rmb_ahead;

	if (go)
	{
		rmb_ahead_busy = 1;
	}

	os_unfair_lock_unlock(&rmb_ahead_lock);

	if (go)
	{
		dispatch_async(rmb_ahead_queue, ^{
			@autoreleasepool
			{
				id<CAMetalDrawable> d = [layer nextDrawable];   /* nil after one second */

				os_unfair_lock_lock(&rmb_ahead_lock);
				rmb_ahead = d;
				rmb_ahead_busy = 0;
				os_unfair_lock_unlock(&rmb_ahead_lock);
			}
		});
	}
}

/* Takes the drawable acquired ahead, if any. */
static id<CAMetalDrawable>
rmb_ahead_take(void)
{
	id<CAMetalDrawable> d;

	os_unfair_lock_lock(&rmb_ahead_lock);
	d = rmb_ahead;
	rmb_ahead = nil;
	os_unfair_lock_unlock(&rmb_ahead_lock);
	return d;
}

/* Drops the target of slot i (a command buffer that reads it keeps it alive). */
static void
rmb_present_target_drop(int i)
{
	if (rmb_present_target[i])
	{
		rmb_present_target[i] = nil;
		rm_ledger_destroy(&rmb_ledger, RM_OBJ_TEXTURE);
	}
}

/*
 * Waits, at most until, for every present handed to rmb_present_order to
 * be done: its block there and both of its completed handlers have run
 * (FIX69); 1 when they are. A wait that ends at the bound is counted and
 * logged, 0, and marks the group (rmb_present_spent): the presents it gave
 * up on are left to it, which their blocks hold, and a later drain of it
 * (teardown drains twice, from RI_ShutdownContext and rmb_close) neither
 * waits for them again nor counts them again (FIX77): -1, at once. Only
 * that group is marked: the next present starts a new one
 * (rmb_present_commit), and a drain of the new one waits again (FIX84).
 */
static int
rmb_present_drain(dispatch_time_t until)
{
	const void *live = RMB_GROUP_ID(rmb_present_live), *spent = RMB_GROUP_ID(rmb_present_spent);
	int done = 1, result;

	if (rmb_present_drain_waits(live, spent))
	{
		done = (dispatch_group_wait(rmb_present_live, until) == 0);
	}

	result = rmb_present_drain_result(live, spent, done);

	if (rmb_present_drain_marks(result, 0))
	{
		rmb_counts.present_drains_late++;
		fprintf(stderr, "ref_metal: a present is still on its way after %d ms\n", RMB_GPU_WAIT_MS);
		rmb_present_spent = rmb_present_live;
	}

	return result;
}

/*
 * Waits for the acquisition on the queue (bounded, as nextDrawable is),
 * for every present to be done and every present slot but the open
 * frame's to be free (bounded, RMB_GPU_WAIT_MS for them all), and drops
 * the drawable acquired ahead and the targets. A drain of a group given up
 * on waits for nothing (rmb_present_drain).
 */
static void
rmb_ahead_drop(void)
{
	dispatch_time_t until = rmb_deadline(RMB_GPU_WAIT_MS);
	int i, done;

	if (rmb_ahead_queue)
	{
		dispatch_sync(rmb_ahead_queue, ^{});
	}

	(void)rmb_ahead_take();
	done = rmb_present_drain(until);

	for (i = 0; i < RMB_PRESENTS; i++)
	{
		if (!rmb_present_sem[i] || (i == rmb_present_slot))
		{
			continue;
		}

		/*
		 * With every present of the group done a slot is free, unless a
		 * present of a group an earlier drain gave up on holds it: waited
		 * for with the same deadline (FIX84). A slot still held then marks
		 * the group as the drain's own bound does, so the second drain of
		 * teardown waits for none (FIX77).
		 */
		if (dispatch_semaphore_wait(rmb_present_sem[i], rmb_present_slot_waits(done) ? until : DISPATCH_TIME_NOW) == 0)
		{
			dispatch_semaphore_signal(rmb_present_sem[i]);
		}
		else if (rmb_present_drain_marks(done, 1))
		{
			fprintf(stderr, "ref_metal: a present is still on the GPU after %d ms\n", RMB_GPU_WAIT_MS);
			rmb_present_spent = rmb_present_live;
		}
	}

	for (i = 0; i < RMB_PRESENTS; i++)
	{
		rmb_present_target_drop(i);
	}
}

/*
 * The colour target of the frame that begins, w by h: with display sync
 * off the target of the first free present slot, which the frame then
 * holds, made (again) for the size; nil with display sync on, with every
 * slot held, or when the target cannot be made, and the frame is drawn
 * into its own target and not presented (rmb_encode.m).
 */
id<MTLTexture>
rmb_present_take(int w, int h)
{
	MTLTextureDescriptor *d;
	int i, slot = -1;

	/* a slot still held by a frame that never ended is given back first */
	if ((rmb_present_slot >= 0) && !rmb_present_cmd)
	{
		dispatch_semaphore_signal(rmb_present_sem[rmb_present_slot]);
		rmb_present_slot = -1;
		rmb_present_group = nil;
	}

	if (!rmb_apart() || (rmb_present_slot >= 0))
	{
		return nil;
	}

	for (i = 0; (i < RMB_PRESENTS) && (slot < 0); i++)
	{
		if (dispatch_semaphore_wait(rmb_present_sem[i], DISPATCH_TIME_NOW) == 0)
		{
			slot = i;
		}
	}

	if (slot < 0)
	{
		rmb_counts.present_held++;   /* every slot held (TASK161) */
		return nil;
	}

	/* a new size or colour format (r_hdr, TASK196): no target of the old one is drawn into again */
	if (rmb_present_target[slot] && !rm_hdr_slot_keep((int)rmb_present_target[slot].pixelFormat,
			(int)rmb_present_target[slot].width, (int)rmb_present_target[slot].height, rm_hdr_format(rmb_hdr_on()), w, h))
	{
		for (i = 0; i < RMB_PRESENTS; i++)
		{
			rmb_present_target_drop(i);
		}
	}

	if (!rmb_present_target[slot])
	{
		@autoreleasepool
		{
			/* as rmb_encode.m's own target */
			d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:(MTLPixelFormat)rm_hdr_format(rmb_hdr_on())
					width:(NSUInteger)w height:(NSUInteger)h mipmapped:NO];
			d.storageMode = MTLStorageModePrivate;
			d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
			rmb_present_target[slot] = [rmb_device newTextureWithDescriptor:d];
		}

		if (!rmb_present_target[slot])
		{
			dispatch_semaphore_signal(rmb_present_sem[slot]);
			return nil;
		}

		rm_ledger_create(&rmb_ledger, RM_OBJ_TEXTURE);
	}

	/* every command buffer committed from here to the frame's end joins it (rmb_watch) */
	rmb_present_group = dispatch_group_create();

	if (!rmb_present_group)
	{
		dispatch_semaphore_signal(rmb_present_sem[slot]);
		return nil;
	}

	rmb_present_slot = slot;
	return rmb_present_target[slot];
}

/*
 * The drawable of the frame that ends: with display sync on the next one,
 * waited for; with it off the one acquired ahead, or nil when none is
 * there yet, when it is not of the layer's drawable size (the size changed
 * after it was acquired), or when the frame holds no present slot (the
 * drawable is then left for the next frame). With display sync off the
 * next one is then acquired ahead.
 */
/*
 * FIX133: the region the present writes (rm_present_region_in's), or 0
 * when it writes nothing: a drawable and a frame of different formats
 * (same_format 0: r_hdr switched while it was held), or on the float16 path
 * (hdr) without its present pipeline (have_pso 0) or after a pipeline of
 * it was not built (rmb_hdr_fail: a draw of the frame was skipped).
 */
int
rmb_present_region(int region, int same_format, int hdr, int have_pso)
{
	if (!same_format || (hdr && (!have_pso || rmb_hdr_dead)))
	{
		return 0;
	}

	return region;
}

/*
 * FIX133: drawable presented through cmd when the present wrote it
 * (rmb_present_region nonzero), 1; 0 with it dropped unpresented (it goes
 * back to the layer), as a frame of the other format always was.
 */
int
rmb_present_written(id<MTLCommandBuffer> cmd, id<CAMetalDrawable> drawable, int region)
{
	if (!cmd || !drawable || !region)
	{
		return 0;
	}

	[cmd presentDrawable:drawable];
	return 1;
}

id<CAMetalDrawable>
rmb_next_drawable(void)
{
	id<CAMetalDrawable> d;
	CGSize want;

	if (!rmb_layer)
	{
		return nil;
	}

	if (rmb_present_slot < 0)
	{
		if (!rmb_apart())
		{
			return [rmb_layer nextDrawable];
		}

		rmb_ahead_request();
		return nil;
	}

	d = rmb_ahead_take();
	want = rmb_layer.drawableSize;

	/* nor of the layer's pixel format (r_hdr switched after it was acquired, TASK196) */
	if (d && ((d.texture.width != (NSUInteger)want.width) || (d.texture.height != (NSUInteger)want.height) ||
			(d.texture.pixelFormat != rmb_layer.pixelFormat)))
	{
		d = nil;
	}

	/* only while display sync is off: with it on nothing takes a drawable acquired ahead (the shim's mgl_present_apart_begin) */
	if (rmb_apart())
	{
		rmb_ahead_request();
	}

	return d;
}

/*
 * The command buffer that writes the drawable rmb_next_drawable returned:
 * the frame's own (display sync on, the frame holds no present slot, or no
 * command buffer of the present queue could be made), or one of the
 * present queue, which rmb_present_commit commits only once every command
 * buffer of the frame has completed.
 */
id<MTLCommandBuffer>
rmb_present_begin(id<MTLCommandBuffer> frame)
{
	id<MTLCommandBuffer> cmd;

	if (rmb_present_slot < 0)
	{
		return frame;
	}

	cmd = [rmb_present_queue commandBuffer];

	if (!cmd)
	{
		return frame;   /* the slot comes back when the frame ends */
	}

	rmb_present_cmd = cmd;
	return cmd;
}

static int rmb_present_late_reported;   /* rmb_present_order only */

/*
 * After the frame's last command buffer is committed, or when a frame ends
 * with nothing to present: gives the slot back now when there is no
 * present, or hands the present to rmb_present_order, which commits it
 * once every command buffer of the frame's group has completed (bounded,
 * RMB_GPU_WAIT_MS), so after every write of the frame to its target; the
 * present gives the slot back when it has completed. A frame still on the
 * GPU at the bound is not presented and its slot comes back.
 */
void
rmb_present_commit(void)
{
	dispatch_semaphore_t sem;
	dispatch_group_t group = rmb_present_group, live;
	id<MTLCommandBuffer> cmd = rmb_present_cmd;
	int slot = rmb_present_slot;

	rmb_present_slot = -1;
	rmb_present_group = nil;   /* before rmb_watch below: the present is not in its own frame's group */
	rmb_present_cmd = nil;

	if (slot < 0)
	{
		return;
	}

	sem = rmb_present_sem[slot];

	if (!cmd)
	{
		dispatch_semaphore_signal(sem);
		return;
	}

	/* the group a drain gave up on keeps only the presents it gave up on: this one starts a new one (FIX84) */
	if (rmb_present_new_group(RMB_GROUP_ID(rmb_present_live), RMB_GROUP_ID(rmb_present_spent)))
	{
		live = dispatch_group_create();

		if (!live)
		{
			dispatch_semaphore_signal(sem);   /* not presented: no drain would wait for it */
			return;
		}

		rmb_present_live = live;
	}

	/* in flight until the block below and both completed handlers have run (rmb_present_drain, FIX69) */
	live = rmb_present_live;
	dispatch_group_enter(live);   /* rmb_watch_until's handler */
	dispatch_group_enter(live);   /* the handler below */
	dispatch_group_enter(live);   /* the block on rmb_present_order */
	rmb_counts.presents++;

	rmb_watch_until(cmd, live);
	[cmd addCompletedHandler:^(id<MTLCommandBuffer> b) {
		(void)b;
		dispatch_semaphore_signal(sem);
		dispatch_group_leave(live);
	}];

	dispatch_async(rmb_present_order, ^{
		if (!group || (dispatch_group_wait(group, rmb_deadline(RMB_GPU_WAIT_MS)) == 0))
		{
			[cmd commit];
		}
		else
		{
			if (!rmb_present_late_reported)
			{
				rmb_present_late_reported = 1;
				fprintf(stderr, "ref_metal: a frame is still on the GPU after %d ms, not presented\n",
						RMB_GPU_WAIT_MS);
			}

			dispatch_semaphore_signal(sem);
			/* never committed: neither completed handler will run */
			dispatch_group_leave(live);
			dispatch_group_leave(live);
		}

		dispatch_group_leave(live);
	});
}

/*
 * Counts a command buffer that ends in error (a GPU fault, a timeout, a
 * fault the validation layer found); the first RMB_FAILED_LOGGED are
 * logged to stderr (the handler runs on a Metal thread, not the
 * engine's). Called before every commit.
 */
void
rmb_watch(id<MTLCommandBuffer> cmd)
{
	rmb_watch_until(cmd, nil);
}

/* As rmb_watch; the handler then leaves done, which the caller entered for it, as its last step (FIX69). */
static void
rmb_watch_until(id<MTLCommandBuffer> cmd, dispatch_group_t done)
{
	/* MTL_GPU_TIME set in the environment: one line per command buffer with its GPU time (a measuring aid, TASK120) */
	static int timing = -1;

	if (timing < 0)
	{
		timing = getenv("MTL_GPU_TIME") != NULL;
	}

	/* FIX51: committed while the open frame holds a present slot, the frame's present waits for it */
	if (rmb_present_group)
	{
		dispatch_group_t group = rmb_present_group;

		dispatch_group_enter(group);
		[cmd addCompletedHandler:^(id<MTLCommandBuffer> b) {
			(void)b;
			dispatch_group_leave(group);
		}];
	}

	[cmd addCompletedHandler:^(id<MTLCommandBuffer> b) {
		unsigned long n;

		if (timing && b.status == MTLCommandBufferStatusCompleted)
		{
			/* the frame's last command buffer says so (TASK54): its GPU time apart from the uploads' */
			fprintf(stderr, "ref_metal: gpu_ms %.4f%s\n", (b.GPUEndTime - b.GPUStartTime) * 1000.0,
					[b.label isEqualToString:@"ref_metal frame"] ? " frame" : "");
		}

		if (b.status == MTLCommandBufferStatusError)
		{
			n = atomic_fetch_add(&rmb_failed, 1);

			if (n < RMB_FAILED_LOGGED)
			{
				fprintf(stderr, "ref_metal: a command buffer failed: %s\n",
						b.error ? b.error.localizedDescription.UTF8String : "no error given");
			}
		}

		if (done)
		{
			dispatch_group_leave(done);
		}
	}];
}

/* Commits cmd and waits for it, at most RMB_GPU_WAIT_MS. */
int
rmb_commit_wait(id<MTLCommandBuffer> cmd)
{
	dispatch_semaphore_t done = dispatch_semaphore_create(0);
	long r;

	rmb_watch(cmd);
	[cmd addCompletedHandler:^(id<MTLCommandBuffer> b) {
		(void)b;
		dispatch_semaphore_signal(done);
	}];
	[cmd commit];

	r = dispatch_semaphore_wait(done, rmb_deadline(RMB_GPU_WAIT_MS));
	return r == 0 && cmd.status == MTLCommandBufferStatusCompleted;
}

void
rmb_ledger_note(rm_obj_kind_t kind, int created)
{
	if (created)
	{
		rm_ledger_create(&rmb_ledger, kind);
	}
	else
	{
		rm_ledger_destroy(&rmb_ledger, kind);
	}
}

int
rmb_open(rmb_caps_t *caps, char *err, size_t errlen)
{
	int n, i;

	memset(caps, 0, sizeof(*caps));

	/* refused before the counts are reset: they belong to the open session */
	if (rmb_device)
	{
		snprintf(err, errlen, "the Metal device is open already");
		return 0;
	}

	memset(&rmb_ledger, 0, sizeof(rmb_ledger));
	memset(&rmb_counts, 0, sizeof(rmb_counts));
	atomic_store(&rmb_failed, 0);

	@autoreleasepool
	{
		rmb_device = MTLCreateSystemDefaultDevice();

		if (!rmb_device)
		{
			snprintf(err, errlen, "no Metal device");
			return 0;
		}

		rm_ledger_create(&rmb_ledger, RM_OBJ_DEVICE);
		rmb_queue = [rmb_device newCommandQueue];

		if (!rmb_queue)
		{
			snprintf(err, errlen, "no Metal command queue");
			rmb_device = nil;
			rm_ledger_destroy(&rmb_ledger, RM_OBJ_DEVICE);
			return 0;
		}

		rm_ledger_create(&rmb_ledger, RM_OBJ_QUEUE);

		/* the present queue and the queue that commits to it (FIX51); without them every frame writes its own drawable */
		rmb_present_queue = [rmb_device newCommandQueue];
		rmb_present_order = dispatch_queue_create("ref_metal present", DISPATCH_QUEUE_SERIAL);
		rmb_present_live = dispatch_group_create();
		rmb_present_spent = nil;
		rmb_present_group = nil;
		rmb_present_slot = -1;

		if (rmb_present_queue)
		{
			rm_ledger_create(&rmb_ledger, RM_OBJ_QUEUE);
		}

		for (i = 0; i < RMB_PRESENTS; i++)
		{
			rmb_present_sem[i] = nil;

			if (rmb_present_queue && rmb_present_order && rmb_present_live)
			{
				rmb_present_sem[i] = dispatch_semaphore_create(0);
				dispatch_semaphore_signal(rmb_present_sem[i]);
			}
		}

		snprintf(caps->device, sizeof(caps->device), "%s", rmb_device.name.UTF8String);

		for (n = RMB_FAMILY_FIRST; n <= RMB_FAMILY_LAST; n++)
		{
			/* MTLGPUFamilyApple1 to MTLGPUFamilyApple9 are 1001 to 1009 */
			if ([rmb_device supportsFamily:(MTLGPUFamily)(MTLGPUFamilyApple1 + n - 1)])
			{
				caps->families |= RMB_FAMILY_BIT(n);
			}
		}

		caps->raytracing = rmb_device.supportsRaytracing ? 1 : 0;

		if (@available(macOS 14.0, *))
		{
			caps->raytracing_render = rmb_device.supportsRaytracingFromRender ? 1 : 0;
		}

		/* created at 0 and raised to 3: libdispatch traps on a semaphore
		   released below its initial value (a frame on a hung GPU at teardown) */
		rmb_frame_sem = dispatch_semaphore_create(0);
		rmb_frames_reset(&rmb_frames);

		for (i = 0; i < RMB_FRAMES_IN_FLIGHT; i++)
		{
			dispatch_semaphore_signal(rmb_frame_sem);
		}
	}

	return 1;
}

/* Puts the layer's own colour space back, then releases ours and the kept one; the object is counted. */
static void
rmb_cs_release(void)
{
	/* under r_hdr the layer keeps the extended space, and the one it was found with stays kept (TASK196) */
	if (rmb_layer && !rmb_hdr_layer)
	{
		rmb_layer.colorspace = rmb_cs_own;   /* as found, not untagged */
	}

	if (rmb_cs_own && !rmb_hdr_layer)
	{
		CGColorSpaceRelease(rmb_cs_own);
		rmb_cs_own = NULL;
	}

	CGColorSpaceRelease(rmb_cs);
	rmb_cs = NULL;
	rm_ledger_destroy(&rmb_ledger, RM_OBJ_STATE);
}

/* Puts the colour space asked for on the layer, or takes it off; the object is counted. */
static void
rmb_cs_apply(void)
{
	if (rmb_layer && rmb_cs_mode && !rmb_cs)
	{
		rmb_cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
		if (rmb_cs)
		{
			rm_ledger_create(&rmb_ledger, RM_OBJ_STATE);

			/* under r_hdr the one found is kept already, and the layer keeps the extended space (TASK196) */
			if (!rmb_hdr_layer)
			{
				rmb_cs_own = rmb_layer.colorspace;
				if (rmb_cs_own)
				{
					CGColorSpaceRetain(rmb_cs_own);
				}

				rmb_layer.colorspace = rmb_cs;
			}
		}
	}
	else if (rmb_cs && (!rmb_layer || !rmb_cs_mode))
	{
		rmb_cs_release();
	}
}

void
rmb_colorspace_set(int mode)
{
	rmb_cs_mode = mode ? 1 : 0;
	rmb_cs_apply();
}

/*
 * r_hdr (TASK196, HD2; docs/plans/hdr-output.md, section 2.2): the layer
 * set for the float16 path, RGBA16Float in extended linear sRGB with
 * wantsExtendedDynamicRangeContent, or back to the stock BGRA8Unorm with
 * the colour space metal_colorspace gives it: ours, or the one the layer
 * was found with, which is kept here when metal_colorspace 0 has not kept
 * it. Between frames; the frame's targets follow at the next frame.
 */
static void
rmb_hdr_apply(void)
{
	if (!rmb_layer || (rmb_hdr_want == rmb_hdr_layer))
	{
		return;
	}

	if (rmb_hdr_want)
	{
		CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearSRGB);

		if (!cs)
		{
			return;
		}

		rm_ledger_create(&rmb_ledger, RM_OBJ_STATE);
		rmb_hdr_cs = cs;

		/* metal_colorspace 0: the layer's colour space as found, put back when r_hdr goes to 0 */
		if (!rmb_cs)
		{
			rmb_cs_own = rmb_layer.colorspace;
			if (rmb_cs_own)
			{
				CGColorSpaceRetain(rmb_cs_own);
			}
		}

		rmb_layer.pixelFormat = MTLPixelFormatRGBA16Float;
		rmb_layer.colorspace = rmb_hdr_cs;
#if TARGET_OS_OSX
		rmb_layer.wantsExtendedDynamicRangeContent = YES;
#elif !TARGET_OS_TV /* not on tvOS (the plan, section 3) */
		if (@available(iOS 16.0, *))
		{
			rmb_layer.wantsExtendedDynamicRangeContent = YES;
		}
#endif
		rmb_hdr_layer = 1;
		return;
	}

	rmb_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
#if TARGET_OS_OSX
	rmb_layer.wantsExtendedDynamicRangeContent = NO;
#elif !TARGET_OS_TV /* not on tvOS (the plan, section 3) */
	if (@available(iOS 16.0, *))
	{
		rmb_layer.wantsExtendedDynamicRangeContent = NO;
	}
#endif
	rmb_layer.colorspace = rmb_cs ? rmb_cs : rmb_cs_own;

	if (!rmb_cs && rmb_cs_own)
	{
		CGColorSpaceRelease(rmb_cs_own);
	}

	if (!rmb_cs)
	{
		rmb_cs_own = NULL;
	}

	CGColorSpaceRelease(rmb_hdr_cs);
	rmb_hdr_cs = NULL;
	rm_ledger_destroy(&rmb_ledger, RM_OBJ_STATE);
	rmb_hdr_layer = 0;
}

void
rmb_hdr_set(int active)
{
	rmb_hdr_want = (active && !rmb_hdr_dead) ? 1 : 0;
	rmb_hdr_apply();
}

/* a pipeline of the float16 path could not be built: off until the next rmb_attach (FIX133), applied by the next rmb_hdr_set (between frames) */
void
rmb_hdr_fail(void)
{
	rmb_hdr_dead = 1;
	rmb_hdr_want = 0;
}

int
rmb_hdr_failed(void)
{
	return rmb_hdr_dead;
}

int
rmb_hdr_on(void)
{
	return rmb_hdr_layer;
}

/*
 * The display's headroom now and its potential (the plan's section 2.4):
 * below 0, unavailable, until HD1 reads them (NSScreen on macOS, UIScreen
 * on iOS and iPadOS). The peak in force is then 1.0 (mtl_hdr_peak), and
 * r_hdr 1 is shown at SDR white.
 */
void
rmb_hdr_headroom(float *now, float *potential)
{
	*now = -1.0f;
	*potential = -1.0f;
}

/* metal_hdr_info's line: the layer's state as it is now, and the headroom (rmb_hdr_headroom) */
int
rmb_hdr_info(char *buf, size_t len)
{
	float now, potential;
	char name[96] = "";
	int edr = 0, fmt = 0;

	if (rmb_layer)
	{
		CGColorSpaceRef cs = rmb_layer.colorspace;
		CFStringRef n = cs ? CGColorSpaceCopyName(cs) : NULL;

		if (n)
		{
			if (!CFStringGetCString(n, name, sizeof(name), kCFStringEncodingUTF8))
			{
				snprintf(name, sizeof(name), "unknown");
			}

			CFRelease(n);
		}
		else if (cs)
		{
			snprintf(name, sizeof(name), "unnamed");
		}

		fmt = (int)rmb_layer.pixelFormat;
#if TARGET_OS_OSX
		edr = rmb_layer.wantsExtendedDynamicRangeContent ? 1 : 0;
#elif !TARGET_OS_TV /* not on tvOS (the plan, section 3) */
		if (@available(iOS 16.0, *))
		{
			edr = rmb_layer.wantsExtendedDynamicRangeContent ? 1 : 0;
		}
#endif
	}

	rmb_hdr_headroom(&now, &potential);
	return rm_hdr_info_format(fmt, name, edr, now, potential, buf, len);
}

int
rmb_attach(void *metal_layer, int vsync, char *err, size_t errlen)
{
	if (!rmb_device || !metal_layer)
	{
		snprintf(err, errlen, "%s", rmb_device ? "no CAMetalLayer" : "no Metal device");
		return 0;
	}

	rmb_detach();
	rmb_hdr_dead = 0;   /* a failed float16 path is off until the next renderer start, which tries it again (FIX133) */
	rmb_frames_reset(&rmb_frames);   /* frames begin on the new layer: a drain waits for every slot again (FIX84) */
#if TARGET_OS_OSX
	rmb_sync = vsync ? 1 : 0;
#else
	rmb_sync = rm_display_sync(1, vsync);   /* in step with the display always (TASK147) */
#endif

	if (!rmb_ahead_queue)
	{
		rmb_ahead_queue = dispatch_queue_create("ref_metal drawable", DISPATCH_QUEUE_SERIAL);
	}

	@autoreleasepool
	{
		rmb_layer = (__bridge CAMetalLayer *)metal_layer;
		rmb_layer.device = rmb_device;
		rmb_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
		rmb_layer.maximumDrawableCount = RMB_FRAMES_IN_FLIGHT;
		rmb_layer.framebufferOnly = NO;              /* the present blit writes it */
		rmb_layer.allowsNextDrawableTimeout = YES;   /* nextDrawable gives up after one second */
#if TARGET_OS_OSX /* displaySyncEnabled is macOS only (C2 settles vsync elsewhere) */
		rmb_layer.displaySyncEnabled = rmb_sync ? YES : NO;
#endif
		/* an opaque layer of about 1720x700 points or more is shown once per refresh (FIX53) */
		rmb_layer.opaque = rmb_sync ? YES : NO;
		rmb_cs_apply();
		rmb_hdr_apply();   /* r_hdr (TASK196) */
	}

	return 1;
}

/*
 * Takes every frame slot back, then returns them: no frame is on the GPU
 * after it. The slots share one deadline, RMB_GPU_WAIT_MS; those still
 * held then are given up on (rmb_frames, header/rmb_drain.h), and a later
 * drain before the next layer is attached (teardown's second rmb_detach,
 * from rmb_close, and rmb_attach's own) takes them back only if they are free
 * at once, as the shim's mgl_release_all waits for no frame (FIX84). A
 * frame begins only with a layer (rmb_frame_begin), so until rmb_attach
 * the slots given up on are those the hung frames hold.
 */
static void
rmb_drain(void)
{
	dispatch_time_t until = rmb_deadline(RMB_GPU_WAIT_MS);
	int i, got = 0, wait = rmb_frames_wait(&rmb_frames);

	if (!rmb_frame_sem)
	{
		return;
	}

	for (i = 0; i < RMB_FRAMES_IN_FLIGHT; i++)
	{
		if (dispatch_semaphore_wait(rmb_frame_sem, (i < wait) ? until : DISPATCH_TIME_NOW) != 0)
		{
			break;
		}

		got++;
	}

	if (rmb_frames_drained(&rmb_frames, got))
	{
		fprintf(stderr, "ref_metal: the GPU still holds a frame after %d ms\n", RMB_GPU_WAIT_MS);
	}

	for (i = 0; i < got; i++)
	{
		dispatch_semaphore_signal(rmb_frame_sem);
	}
}

void
rmb_detach(void)
{
	@autoreleasepool
	{
		rmb_encode_abandon();   /* commits an open frame, presenting nothing */
		rmb_drain();
		rmb_ahead_drop();       /* nothing of this layer is left on the queue */
		if (rmb_hdr_layer)
		{
			const int want = rmb_hdr_want;

			/* the stock format and the colour space as found (TASK196); asked for again at the next attach */
			rmb_hdr_want = 0;
			rmb_hdr_apply();
			rmb_hdr_want = want;
		}
		if (rmb_cs)
		{
			rmb_cs_release();   /* the view's layer is left as found */
		}
		rmb_layer = nil;        /* the SDL Metal view owns it */
	}

	rmb_encode_release();       /* the targets, once no command buffer holds them */
}

void
rmb_close(rmb_stats_t *stats, rm_ledger_t *ledger)
{
	__weak id<MTLCommandQueue> queue;
	char line[128];
	int i;

	rmb_detach();               /* every present done, or the bound logged (rmb_present_drain) */
	rmb_resources_close();      /* textures, staging, pipelines, samplers (rmb_resource.m) */
	rmb_ahead_queue = nil;      /* empty: rmb_detach drained it */

	if (rmb_device && rm_presents_format(&rmb_counts, line, sizeof(line)))
	{
		fprintf(stderr, "%s\n", line);
	}

	if (rmb_device)
	{
		@autoreleasepool
		{
			queue = rmb_queue;
			rmb_queue = nil;
			rm_ledger_destroy(&rmb_ledger, RM_OBJ_QUEUE);

			if (rmb_present_queue)
			{
				rmb_present_queue = nil;
				rm_ledger_destroy(&rmb_ledger, RM_OBJ_QUEUE);
			}

			/* a present still on it (the GPU past the bound at teardown) keeps it alive */
			rmb_present_order = nil;
			rmb_present_group = nil;
			rmb_present_live = nil;     /* likewise: each present's blocks hold it until they have run */
			rmb_present_spent = nil;

			for (i = 0; i < RMB_PRESENTS; i++)
			{
				rmb_present_sem[i] = nil;
			}

			/* the system default device is shared with the rest of the process:
			   only ref_metal's reference is dropped, it is not weak-checked */
			rmb_device = nil;
			rm_ledger_destroy(&rmb_ledger, RM_OBJ_DEVICE);
			rmb_frame_sem = nil;
		}

		if (queue)
		{
			rmb_ledger.unreleased++;
		}
	}

	rmb_stats(stats, ledger);
}

void
rmb_stats(rmb_stats_t *stats, rm_ledger_t *ledger)
{
	*stats = rmb_counts;
	stats->failed = atomic_load(&rmb_failed);
	*ledger = rmb_ledger;
}

void
rmb_rate_counts(unsigned long *frames, unsigned long *no_drawable)
{
	*frames = rmb_counts.frames;
	*no_drawable = rmb_counts.no_drawable;
}

void
rmb_set_vsync(int on)
{
#if TARGET_OS_OSX
	rmb_sync = on ? 1 : 0;
#else
	rmb_sync = rm_display_sync(1, on);   /* r_vsync has no effect here (TASK147) */
#endif

	/* with display sync on every frame waits for its own drawable again */
	if (rmb_sync)
	{
		rmb_ahead_drop();
	}

	if (rmb_layer)
	{
#if TARGET_OS_OSX /* displaySyncEnabled is macOS only (C2 settles vsync elsewhere) */
		rmb_layer.displaySyncEnabled = rmb_sync ? YES : NO;
#endif
		/* an opaque layer of about 1720x700 points or more is shown once per refresh (FIX53) */
		rmb_layer.opaque = rmb_sync ? YES : NO;
	}
}

/* What the layer has: its displaySyncEnabled, or what the next layer gets without one (and off macOS, which has no displaySyncEnabled). */
int
rmb_vsync(void)
{
#if TARGET_OS_OSX
	if (rmb_layer)
	{
		return rmb_layer.displaySyncEnabled ? 1 : 0;
	}
#endif

	return rmb_sync;
}
