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
#include <TargetConditionals.h>

#include <os/lock.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "header/rmb.h"

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

/* rmb_encode.m */
void rmb_encode_abandon(void);
void rmb_encode_release(void);

static atomic_ulong rmb_failed;
static int rmb_sync = 1;

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
 */
#define RMB_PRESENTS (RMB_FRAMES_IN_FLIGHT + 1)   /* maximumDrawableCount (rmb_attach), and the open frame */

static dispatch_queue_t rmb_ahead_queue;
static os_unfair_lock rmb_ahead_lock = OS_UNFAIR_LOCK_INIT;
static id<CAMetalDrawable> rmb_ahead;   /* rmb_ahead_lock: the drawable acquired ahead */
static int rmb_ahead_busy;              /* rmb_ahead_lock: an acquisition is on the queue */

static id<MTLCommandQueue> rmb_present_queue;
static dispatch_queue_t rmb_present_order;   /* serial: commits each present once its frame has completed */
static dispatch_group_t rmb_present_group;   /* the command buffers committed while the open frame holds a slot */
static dispatch_semaphore_t rmb_present_sem[RMB_PRESENTS];   /* 1: no frame and no present holds this slot */
static id<MTLTexture> rmb_present_target[RMB_PRESENTS];      /* the target the slot's frame is drawn into */
static int rmb_present_slot = -1;              /* the slot the open frame holds, or -1 */
static id<MTLCommandBuffer> rmb_present_cmd;   /* the present of the frame that ends, committed after it */

void rmb_watch(id<MTLCommandBuffer> cmd);

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
 * Waits for the acquisition on the queue (bounded, as nextDrawable is) and
 * for every present slot but the open frame's (bounded, RMB_GPU_WAIT_MS
 * for them all), and drops the drawable acquired ahead and the targets.
 */
static void
rmb_ahead_drop(void)
{
	dispatch_time_t until = rmb_deadline(RMB_GPU_WAIT_MS);
	int i;

	if (rmb_ahead_queue)
	{
		dispatch_sync(rmb_ahead_queue, ^{});
	}

	(void)rmb_ahead_take();

	for (i = 0; i < RMB_PRESENTS; i++)
	{
		if (!rmb_present_sem[i] || (i == rmb_present_slot))
		{
			continue;
		}

		if (dispatch_semaphore_wait(rmb_present_sem[i], until) == 0)
		{
			dispatch_semaphore_signal(rmb_present_sem[i]);
		}
		else
		{
			fprintf(stderr, "ref_metal: a present is still on the GPU after %d ms\n", RMB_GPU_WAIT_MS);
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
		return nil;
	}

	/* a new size: no target of the old one is drawn into again */
	if (rmb_present_target[slot] && ((rmb_present_target[slot].width != (NSUInteger)w) ||
			(rmb_present_target[slot].height != (NSUInteger)h)))
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
			d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
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

	if (d && ((d.texture.width != (NSUInteger)want.width) || (d.texture.height != (NSUInteger)want.height)))
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
	dispatch_group_t group = rmb_present_group;
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

	rmb_watch(cmd);
	[cmd addCompletedHandler:^(id<MTLCommandBuffer> b) {
		(void)b;
		dispatch_semaphore_signal(sem);
	}];

	dispatch_async(rmb_present_order, ^{
		if (!group || (dispatch_group_wait(group, rmb_deadline(RMB_GPU_WAIT_MS)) == 0))
		{
			[cmd commit];
			return;
		}

		if (!rmb_present_late_reported)
		{
			rmb_present_late_reported = 1;
			fprintf(stderr, "ref_metal: a frame is still on the GPU after %d ms, not presented\n",
					RMB_GPU_WAIT_MS);
		}

		dispatch_semaphore_signal(sem);
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

		if (b.status != MTLCommandBufferStatusError)
		{
			return;
		}

		n = atomic_fetch_add(&rmb_failed, 1);

		if (n < RMB_FAILED_LOGGED)
		{
			fprintf(stderr, "ref_metal: a command buffer failed: %s\n",
					b.error ? b.error.localizedDescription.UTF8String : "no error given");
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
		rmb_present_group = nil;
		rmb_present_slot = -1;

		if (rmb_present_queue)
		{
			rm_ledger_create(&rmb_ledger, RM_OBJ_QUEUE);
		}

		for (i = 0; i < RMB_PRESENTS; i++)
		{
			rmb_present_sem[i] = nil;

			if (rmb_present_queue && rmb_present_order)
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

		for (i = 0; i < RMB_FRAMES_IN_FLIGHT; i++)
		{
			dispatch_semaphore_signal(rmb_frame_sem);
		}
	}

	return 1;
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
	}

	return 1;
}

/* Takes every frame slot back (bounded), then returns them: no frame is on the GPU after it. */
static void
rmb_drain(void)
{
	int i, got = 0;

	if (!rmb_frame_sem)
	{
		return;
	}

	for (i = 0; i < RMB_FRAMES_IN_FLIGHT; i++)
	{
		if (dispatch_semaphore_wait(rmb_frame_sem, rmb_deadline(RMB_GPU_WAIT_MS)) != 0)
		{
			fprintf(stderr, "ref_metal: the GPU still holds a frame after %d ms\n", RMB_GPU_WAIT_MS);
			break;
		}

		got++;
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
		rmb_layer = nil;        /* the SDL Metal view owns it */
	}

	rmb_encode_release();       /* the targets, once no command buffer holds them */
}

void
rmb_close(rmb_stats_t *stats, rm_ledger_t *ledger)
{
	__weak id<MTLCommandQueue> queue;
	int i;

	rmb_detach();
	rmb_resources_close();      /* textures, staging, pipelines, samplers (rmb_resource.m) */
	rmb_ahead_queue = nil;      /* empty: rmb_detach drained it */

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
