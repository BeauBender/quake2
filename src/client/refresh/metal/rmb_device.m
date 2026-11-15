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
 * FIX51: with display sync off a frame does not wait for a drawable. The
 * window server hands drawables back at its own pace, whatever the frame
 * costs, and waiting for one in nextDrawable made that pace the frame
 * rate (docs/plans/metal-port-harness.md, "The fixed per-frame cost of the
 * Metal renderers (FIX51)"). So the next drawable is acquired ahead, on a
 * serial queue, and the frame that ends takes it when it is there. A frame
 * that finds none is complete without one and is not presented (counted as
 * without a drawable); every frame is drawn offscreen either way. With
 * display sync on the frame waits in nextDrawable as before, and every
 * frame is presented.
 */
static dispatch_queue_t rmb_ahead_queue;
static os_unfair_lock rmb_ahead_lock = OS_UNFAIR_LOCK_INIT;
static id<CAMetalDrawable> rmb_ahead;   /* rmb_ahead_lock: the drawable acquired ahead */
static int rmb_ahead_busy;              /* rmb_ahead_lock: an acquisition is on the queue */

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

/* Waits for the acquisition on the queue (bounded, as nextDrawable is) and drops what it got. */
static void
rmb_ahead_drop(void)
{
	if (rmb_ahead_queue)
	{
		dispatch_sync(rmb_ahead_queue, ^{});
	}

	(void)rmb_ahead_take();
}

/*
 * The drawable of the frame that ends: with display sync on the next one,
 * waited for; with it off the one acquired ahead, or nil when none is
 * there yet or it is not of the layer's drawable size (the size changed
 * after it was acquired). The next one is then acquired ahead.
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

	if (rmb_sync)
	{
		return [rmb_layer nextDrawable];
	}

	d = rmb_ahead_take();
	want = rmb_layer.drawableSize;

	if (d && ((d.texture.width != (NSUInteger)want.width) || (d.texture.height != (NSUInteger)want.height)))
	{
		d = nil;
	}

	rmb_ahead_request();
	return d;
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
	rmb_sync = vsync ? 1 : 0;

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
		rmb_layer.displaySyncEnabled = rmb_sync ? YES : NO;
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
	rmb_sync = on ? 1 : 0;

	/* with display sync on every frame waits for its own drawable again */
	if (rmb_sync)
	{
		rmb_ahead_drop();
	}

	if (rmb_layer)
	{
		rmb_layer.displaySyncEnabled = rmb_sync ? YES : NO;
		/* an opaque layer of about 1720x700 points or more is shown once per refresh (FIX53) */
		rmb_layer.opaque = rmb_sync ? YES : NO;
	}
}

/* What the layer has: its displaySyncEnabled, or what the next layer gets without one. */
int
rmb_vsync(void)
{
	if (rmb_layer)
	{
		return rmb_layer.displaySyncEnabled ? 1 : 0;
	}

	return rmb_sync;
}
