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
 * skipped after it), the GPU RMB_GPU_WAIT_MS (readback and teardown).
 *
 * =======================================================================
 */

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <stdatomic.h>
#include <stdio.h>
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
 * Counts a command buffer that ends in error (a GPU fault, a timeout, a
 * fault the validation layer found); the first RMB_FAILED_LOGGED are
 * logged to stderr (the handler runs on a Metal thread, not the
 * engine's). Called before every commit.
 */
void
rmb_watch(id<MTLCommandBuffer> cmd)
{
	[cmd addCompletedHandler:^(id<MTLCommandBuffer> b) {
		unsigned long n;

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

	@autoreleasepool
	{
		rmb_layer = (__bridge CAMetalLayer *)metal_layer;
		rmb_layer.device = rmb_device;
		rmb_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
		rmb_layer.maximumDrawableCount = RMB_FRAMES_IN_FLIGHT;
		rmb_layer.framebufferOnly = NO;              /* the present blit writes it */
		rmb_layer.allowsNextDrawableTimeout = YES;   /* nextDrawable gives up after one second */
		rmb_layer.displaySyncEnabled = rmb_sync ? YES : NO;
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

	if (rmb_layer)
	{
		rmb_layer.displaySyncEnabled = rmb_sync ? YES : NO;
	}
}

int
rmb_vsync(void)
{
	return rmb_sync;
}
