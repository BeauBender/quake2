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
 * The Metal side of the shim, and its only Objective-C: the CAMetalLayer
 * from SDL_Metal_CreateView, the device, the command queue, drawable
 * acquisition, the render passes, readback and present (design
 * section 3). Compiled with ARC (-fobjc-arc): the Metal objects live in
 * the strong file-scope variables below and are released by assigning
 * nil; every function that creates Metal objects drains its own
 * autorelease pool, so nothing is held past the call that made it.
 *
 * Every wait is bounded:
 *  - the frame semaphore (at most 3 frames on the GPU) is waited on for
 *    MGL_FRAME_WAIT_MS; when it times out the frame is skipped (no
 *    command buffer, clears and reads do nothing, the swap ends it);
 *  - with display sync off the frame then waits for a second free slot
 *    of the semaphore, bounded by MGL_FRAME_WAIT_MS too, and gives it back
 *    at once, so at most two frames are on the GPU (FIX68). When that wait
 *    times out the frame is not skipped: it goes on, three on the GPU, and
 *    the frame's begin has waited up to twice MGL_FRAME_WAIT_MS. The
 *    teardown line counts those frames and the most frames that were on
 *    the GPU when a frame began with display sync off;
 *  - a present apart waits on the GPU for its frame's signal of the
 *    present slot's event; when it has not completed MGL_GPU_WAIT_MS
 *    after its commit, a command buffer of the frame queue signals that
 *    event (one line on stderr), so a frame whose command buffer ended in
 *    error before its signal cannot hold the present, and its slot, for
 *    the rest of the context (FIX69, mgl_present_bound);
 *  - nextDrawable returns nil after one second (allowsNextDrawableTimeout,
 *    for example while the window cannot be shown); the frame is then
 *    drawn into an offscreen texture of the same size, which glReadPixels
 *    reads, and nothing is presented. With display sync off the frame
 *    waits for no drawable at all: it is drawn offscreen and presented
 *    apart, when a drawable acquired ahead is ready (FIX51,
 *    mgl_present_apart_begin);
 *  - glFinish and glReadPixels wait for their command buffer for
 *    MGL_GPU_WAIT_MS; a read that times out fails (sticky error, nothing
 *    written).
 *
 * =======================================================================
 */

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <dlfcn.h>
#include <os/lock.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "mgl_internal.h"

#if !__has_feature(objc_arc)
#error "mgl_layer.m is written for ARC: compile it with -fobjc-arc"
#endif

/* The context: one per process, like gl1's. */
static SDL_Window *mgl_window;
static SDL_MetalView mgl_view;
static CAMetalLayer *mgl_metal_layer;
static id<MTLDevice> mgl_device;
static id<MTLCommandQueue> mgl_queue;
static id<MTLLibrary> mgl_library;
static dispatch_semaphore_t mgl_frame_sem;
static int mgl_ready;
static int mgl_sync = 1;
static char mgl_device_name[128];

/* The open frame. */
static int frame_open;
static int frame_skipped;
static int frame_passes;
static int frame_unseen;                   /* a command buffer of the frame was not seen complete (mgl_flush, FIX51) */
static id<CAMetalDrawable> frame_drawable;
static id<MTLTexture> frame_target;        /* the drawable's texture, a present slot's target (FIX51), or mgl_offscreen */
static id<MTLCommandBuffer> frame_cmd;
static id<MTLRenderCommandEncoder> frame_enc;
static int frame_w, frame_h;

/* Kept across frames, rebuilt when the size changes. */
static id<MTLTexture> mgl_depth;           /* depth32Float_stencil8, private */
static id<MTLTexture> mgl_offscreen;       /* the target when there is no drawable */
/*
 * The last presented frame, for reads between frames (the screenshot
 * command): its target (the drawable's texture, or mgl_offscreen), and
 * its drawable, held so the layer does not hand that texture out again
 * while it may be read. Replaced at each present, dropped on a resize
 * and at teardown. No copy is made.
 */
static id<MTLTexture> mgl_last;
static id<CAMetalDrawable> mgl_last_drawable;
static id<MTLBuffer> mgl_readbuf;          /* shared, for glReadPixels */

/* Counters, printed when the context is destroyed. */
static unsigned long count_frames, count_no_drawable, count_skipped, count_ring_full;
static atomic_ulong count_failed;          /* command buffers ended in error (mgl_watch) */
static unsigned long count_present_late;   /* presents whose wait for their frame was ended at the bound (FIX69) */
static unsigned long count_cap_late;       /* frames whose second wait with display sync off timed out (FIX68) */
static int count_most_apart;               /* the most frames on the GPU, the new one counted, as a frame began with display sync off */
static atomic_int mgl_on_gpu;              /* frames committed by mgl_layer_present and not yet completed */

#define MGL_FRAMES_IN_FLIGHT 3

/*
 * The draw path (TASK48, TASK82). The caches are keyed by the packed
 * keys of mgl_draw.c and mgl_tex.c, so each holds at most one object per
 * key gl1 reaches (13 and 12 bits for pipelines and samplers; the depth
 * and stencil key has 34, of which gl1 sets a handful).
 */
static id<MTLFunction> mgl_vfunc, mgl_ffunc;
static NSMutableDictionary<NSNumber *, id<MTLRenderPipelineState>> *mgl_pipelines;
static NSMutableDictionary<NSNumber *, id<MTLSamplerState>> *mgl_samplers;
static NSMutableDictionary<NSNumber *, id<MTLDepthStencilState>> *mgl_depth_states;
static id<MTLTexture> mgl_unbound;         /* bound to texture(0) when a draw samples nothing */

/*
 * The vertex ring: one buffer per frame in flight. The frame semaphore
 * lets a frame begin only when at most two others are on the GPU, so
 * the buffer of the frame three back is no longer read.
 */
static id<MTLBuffer> ring_buf[MGL_FRAMES_IN_FLIGHT];
static mgl_ring_t ring;
static unsigned ring_slot;

/* Texture uploads made outside a frame, committed before the next frame's command buffer. */
static id<MTLCommandBuffer> upload_cmd;
static id<MTLBlitCommandEncoder> upload_blit;

static dispatch_time_t
mgl_deadline(long ms)
{
	return dispatch_time(DISPATCH_TIME_NOW, (int64_t)ms * NSEC_PER_MSEC);
}

static void
mgl_err(char *err, size_t errlen, const char *what, const char *detail)
{
	snprintf(err, errlen, "mgl: %s%s%s", what, detail ? ": " : "", detail ? detail : "");
}

/*
 * The console of the renderer this shim is linked into (R_Printf in
 * src/client/refresh/files/common.c, declared in ref_shared.h, which
 * this file does not include); 0 is PRINT_ALL.
 */
extern void R_Printf(int level, const char *msg, ...);

/*
 * The startup capability report: the device, the Apple GPU families 5
 * to 9 and ray tracing, once per context, to the console. Report only:
 * nothing in the shim depends on it.
 */
static void
mgl_report_capabilities(id<MTLDevice> dev)
{
	static const struct
	{
		MTLGPUFamily family;
		const char *name;
	} families[] = {
		{ MTLGPUFamilyApple5, "apple5" }, { MTLGPUFamilyApple6, "apple6" },
		{ MTLGPUFamilyApple7, "apple7" }, { MTLGPUFamilyApple8, "apple8" },
		{ MTLGPUFamilyApple9, "apple9" },
	};
	char list[64];   /* all five names and their spaces are 34 characters */
	size_t i, n = 0;
	int rt_render = 0;

	list[0] = '\0';

	for (i = 0; i < sizeof(families) / sizeof(families[0]); i++)
	{
		if ([dev supportsFamily:families[i].family])
		{
			n += (size_t)snprintf(list + n, sizeof(list) - n, "%s%s", n ? " " : "", families[i].name);
		}
	}

	if (@available(macOS 14.0, *))
	{
		rt_render = dev.supportsRaytracingFromRender ? 1 : 0;
	}

	R_Printf(0, "Metal device: %s\n", dev.name.UTF8String);
	R_Printf(0, "Metal supportsFamily (apple5 to apple9): %s\n", n ? list : "none");
	R_Printf(0, "Metal supportsRaytracing: %s, supportsRaytracingFromRender: %s\n",
			dev.supportsRaytracing ? "yes" : "no", rt_render ? "yes" : "no");
}

/* ref_gl1.metallib lies next to the dylib this code is linked into. */
static int
mgl_library_path(char *path, size_t len)
{
	Dl_info info;
	const char *slash;
	size_t dir;

	if (!dladdr((const void *)&mgl_library_path, &info) || !info.dli_fname)
	{
		return 0;
	}

	slash = strrchr(info.dli_fname, '/');
	dir = slash ? (size_t)(slash - info.dli_fname) : 0;

	if (dir + sizeof("/ref_gl1.metallib") > len)
	{
		return 0;
	}

	if (dir)
	{
		memcpy(path, info.dli_fname, dir);
		memcpy(path + dir, "/ref_gl1.metallib", sizeof("/ref_gl1.metallib"));
	}
	else
	{
		memcpy(path, "ref_gl1.metallib", sizeof("ref_gl1.metallib"));
	}

	return 1;
}

/*
 * FIX53: a layer that is not opaque (r_vsync 0) is blended with what is
 * behind the window, so every drawable pixel needs alpha 1. The blended
 * draws, straight into the drawable, leave alpha below 1. This pass
 * writes alpha 1, and only alpha, over the drawable after it. Its
 * pipeline is built with the context (mgl_alpha_build) from the shim's
 * shader library, once: it is nil when that failed, the frames are then
 * presented without the pass, and one line says so.
 *
 * FIX51: a frame presented apart (below) is written to the drawable in
 * one pass that is the copy and this pass together (mgl_present_pass,
 * mgl_fragment_present), with a pipeline built here too; when that one
 * was not built the frame is copied and this pass written over it.
 */
static id<MTLRenderPipelineState> mgl_alpha_pso;
static id<MTLRenderPipelineState> mgl_present_pso;
static int mgl_alpha_reported;

static void
mgl_alpha_build(void)
{
	NSError *e = nil;
	MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];

	d.vertexFunction = [mgl_library newFunctionWithName:@"mgl_vertex_alpha"];
	d.fragmentFunction = [mgl_library newFunctionWithName:@"mgl_fragment_alpha"];
	mgl_alpha_pso = nil;
	mgl_present_pso = nil;

	if (d.vertexFunction && d.fragmentFunction)
	{
		d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
		d.colorAttachments[0].writeMask = MTLColorWriteMaskAlpha;
		mgl_alpha_pso = [mgl_device newRenderPipelineStateWithDescriptor:d error:&e];
	}

	d.fragmentFunction = [mgl_library newFunctionWithName:@"mgl_fragment_present"];

	if (d.vertexFunction && d.fragmentFunction)
	{
		d.colorAttachments[0].writeMask = MTLColorWriteMaskAll;
		mgl_present_pso = [mgl_device newRenderPipelineStateWithDescriptor:d error:&e];
	}
}

/* Alpha 1 over the whole of tex, colour untouched. */
static void
mgl_force_alpha(id<MTLCommandBuffer> cmd, id<MTLTexture> tex)
{
	id<MTLRenderPipelineState> pso = mgl_alpha_pso;
	MTLRenderPassDescriptor *rp;
	id<MTLRenderCommandEncoder> enc;

	if (!pso)
	{
		if (!mgl_alpha_reported)
		{
			mgl_alpha_reported = 1;
			fprintf(stderr, "mgl: no alpha pass (its pipeline was not built): "
					"the window shows the frame's own alpha\n");
		}

		return;
	}

	rp = [MTLRenderPassDescriptor renderPassDescriptor];
	rp.colorAttachments[0].texture = tex;
	rp.colorAttachments[0].loadAction = MTLLoadActionLoad;
	rp.colorAttachments[0].storeAction = MTLStoreActionStore;
	enc = [cmd renderCommandEncoderWithDescriptor:rp];
	[enc setRenderPipelineState:pso];
	[enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
	[enc endEncoding];
}

/*
 * FIX51: src, of the drawable's size, onto dst: with alpha 1 in one pass
 * when the layer is not opaque and the pipeline was built, otherwise a
 * copy, and FIX53's alpha pass over it when the layer is not opaque.
 */
static void
mgl_present_pass(id<MTLCommandBuffer> cmd, id<MTLTexture> src, id<MTLTexture> dst)
{
	MTLRenderPassDescriptor *rp;
	id<MTLRenderCommandEncoder> enc;
	id<MTLBlitCommandEncoder> blit;

	if (!mgl_metal_layer.opaque && mgl_present_pso)
	{
		rp = [MTLRenderPassDescriptor renderPassDescriptor];
		rp.colorAttachments[0].texture = dst;
		rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;   /* every pixel is written */
		rp.colorAttachments[0].storeAction = MTLStoreActionStore;
		enc = [cmd renderCommandEncoderWithDescriptor:rp];
		[enc setRenderPipelineState:mgl_present_pso];
		[enc setFragmentTexture:src atIndex:0];
		[enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
		[enc endEncoding];
		return;
	}

	blit = [cmd blitCommandEncoder];
	[blit copyFromTexture:src toTexture:dst];
	[blit endEncoding];

	if (!mgl_metal_layer.opaque)
	{
		mgl_force_alpha(cmd, dst);
	}
}

/*
 * FIX51: with display sync off a frame waits for no drawable
 * (docs/runbook/FIX51.md). The window server hands a drawable back at its
 * own pace, whatever the frame costs, and that pace was the frame rate
 * twice over: a frame waited for the drawable in nextDrawable, and a
 * drawable the window server had not let go of yet held the command
 * buffer that drew into it on the GPU, and with it the frame slot.
 *
 * So with display sync off the next drawable is acquired ahead, on a
 * serial queue, and the present is apart from the frame, from the very
 * target the frame was drawn into. There are MGL_PRESENTS present slots,
 * one for each of the layer's drawables and one for the frame being
 * drawn: a semaphore of one (mgl_present_sem) and a colour target
 * (mgl_present_target). A frame takes a free slot when it begins and is
 * drawn into the slot's target (mgl_present_take). When it ends with a
 * drawable ready, the present goes on mgl_present_queue, which waits for
 * the frame's command buffer (the slot's own event, mgl_present_event,
 * which no other frame signals while this one holds the slot), and writes the drawable
 * from that target in one pass (mgl_present_pass): the wait for the
 * drawable holds that queue and no frame slot, and nothing copies the
 * frame first. Metal states only that an event a command buffer signals
 * follows that buffer's own commands, so the frame's earlier command
 * buffers (mgl_flush: glFinish, glReadPixels) are each waited for until
 * they have completed, and a frame with one that did not complete in the
 * bound is not presented.
 *
 * The slot comes back when its present has completed, so its
 * target is not drawn into again while it is read; a frame that is not
 * presented gives its slot back when it ends (a later frame that draws
 * into that target is on the same queue, after it). A frame that begins
 * with every slot held is drawn into mgl_offscreen and is not presented,
 * nor is one that ends with no drawable ready (counted as without a
 * drawable). With display sync on the frame waits in nextDrawable and
 * draws into the drawable, as before, and every frame is presented.
 *
 * The present's wait on the GPU has a time limit (FIX69). When a present
 * has not completed MGL_GPU_WAIT_MS after its commit (mgl_present_bound,
 * as each frame begins), a command buffer of the frame queue signals its
 * slot's event with the value the present waits for (mgl_present_end_wait):
 * a frame whose command buffer ended in error before its signal would
 * otherwise hold the present, and its slot, for the rest of the context.
 * The slot is then held by both command buffers (mgl_present_holds) and
 * comes back when the last of them completes, so no later frame of the
 * slot signals the event before that signal has run. The events stay
 * MTLEvents: a shared event, which the CPU could signal, made the shim
 * present 9 to 11 percent fewer frames at 5120x2880 (docs/runbook/FIX69.md).
 */
#define MGL_PRESENTS (MGL_FRAMES_IN_FLIGHT + 1)   /* maximumDrawableCount (mgl_layer_create), and the open frame */

static dispatch_queue_t mgl_ahead_queue;
static os_unfair_lock mgl_ahead_lock = OS_UNFAIR_LOCK_INIT;
static id<CAMetalDrawable> mgl_ahead;   /* mgl_ahead_lock: the drawable acquired ahead */
static int mgl_ahead_busy;              /* mgl_ahead_lock: an acquisition is on the queue */

static id<MTLCommandQueue> mgl_present_queue;
static id<MTLEvent> mgl_present_event[MGL_PRESENTS];         /* signalled by the frame that holds the slot, or past the bound */
static uint64_t mgl_present_value[MGL_PRESENTS];             /* the last value the slot's event was given */
static uint64_t mgl_present_since[MGL_PRESENTS];             /* when the slot's present was committed (ns, CLOCK_UPTIME_RAW), or 0 */
static atomic_int mgl_present_holds[MGL_PRESENTS];           /* command buffers holding the slot after its frame: 1, or 2 past the bound */
static dispatch_semaphore_t mgl_present_sem[MGL_PRESENTS];   /* 1: no frame and no present holds this slot */
static id<MTLTexture> mgl_present_target[MGL_PRESENTS];      /* the target the slot's frame is drawn into */
static int frame_slot = -1;                                  /* the slot the open frame holds, or -1 */

static void mgl_watch(id<MTLCommandBuffer> cmd);
static id<MTLTexture> mgl_new_texture(MTLPixelFormat format, int w, int h, MTLTextureUsage usage);

/* With display sync off a frame is drawn offscreen and presented apart (FIX51). */
static int
mgl_present_apart(void)
{
	return !mgl_sync && mgl_present_sem[0];
}

/*
 * The target of the first free present slot, which the open frame then
 * holds (frame_slot), made (again) for w by h; nil with every slot held or
 * when it cannot be made.
 */
static id<MTLTexture>
mgl_present_take(int w, int h)
{
	int i, slot = -1;

	for (i = 0; (i < MGL_PRESENTS) && (slot < 0); i++)
	{
		if (mgl_present_sem[i] && (dispatch_semaphore_wait(mgl_present_sem[i], DISPATCH_TIME_NOW) == 0))
		{
			slot = i;
		}
	}

	if (slot < 0)
	{
		return nil;
	}

	/* a new size: no target of the old one is drawn into again */
	if (mgl_present_target[slot] && (((int)mgl_present_target[slot].width != w) ||
			((int)mgl_present_target[slot].height != h)))
	{
		for (i = 0; i < MGL_PRESENTS; i++)
		{
			mgl_present_target[i] = nil;
		}
	}

	if (!mgl_present_target[slot])
	{
		mgl_present_target[slot] = mgl_new_texture(MTLPixelFormatBGRA8Unorm, w, h,
				MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
	}

	if (!mgl_present_target[slot])
	{
		dispatch_semaphore_signal(mgl_present_sem[slot]);
		return nil;
	}

	frame_slot = slot;
	mgl_present_since[slot] = 0;   /* the slot's last present has completed */
	return mgl_present_target[slot];
}

/* A command buffer that held slot i after its frame has completed: the last of them gives the slot back (FIX69). */
static void
mgl_present_unhold(int i, dispatch_semaphore_t sem)
{
	if (atomic_fetch_sub(&mgl_present_holds[i], 1) == 1)
	{
		dispatch_semaphore_signal(sem);
	}
}

/*
 * Ends the GPU wait of slot i's present, when it has not completed: a
 * command buffer of the frame queue signals the slot's event with the
 * value the present waits for, and holds the slot with it (FIX69). A
 * signal of a value the frame gave already changes nothing.
 */
static void
mgl_present_end_wait(int i)
{
	dispatch_semaphore_t sem = mgl_present_sem[i];
	id<MTLEvent> ev = mgl_present_event[i];
	int one = 1;

	/* holds 0: the present has completed and the slot is back (or taken again) */
	if (!mgl_queue || !sem || !ev || !atomic_compare_exchange_strong(&mgl_present_holds[i], &one, 2))
	{
		return;
	}

	@autoreleasepool
	{
		id<MTLCommandBuffer> cmd = [mgl_queue commandBuffer];

		if (!cmd)
		{
			mgl_present_unhold(i, sem);
			return;
		}

		[cmd encodeSignalEvent:ev value:mgl_present_value[i]];
		mgl_watch(cmd);
		[cmd addCompletedHandler:^(id<MTLCommandBuffer> b) {
			(void)b;
			mgl_present_unhold(i, sem);
		}];
		[cmd commit];
	}

	count_present_late++;
	fprintf(stderr, "mgl: the present of slot %d has not completed after %d ms; its wait for its frame is ended\n",
			i, MGL_GPU_WAIT_MS);
}

/* The present wait's time limit, checked as each frame begins (FIX69). */
static void
mgl_present_bound(void)
{
	uint64_t now = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
	int i;

	for (i = 0; i < MGL_PRESENTS; i++)
	{
		if (mgl_present_since[i] && (now - mgl_present_since[i] >= (uint64_t)MGL_GPU_WAIT_MS * NSEC_PER_MSEC))
		{
			mgl_present_since[i] = 0;
			mgl_present_end_wait(i);
		}
	}
}

/* Gives the open frame's present slot back, if it holds one and nothing presents it. */
static void
mgl_present_give(void)
{
	if (frame_slot >= 0)
	{
		dispatch_semaphore_signal(mgl_present_sem[frame_slot]);
		frame_slot = -1;
	}
}

/* Puts an acquisition of the next drawable on the queue, unless one is there or done. */
static void
mgl_ahead_request(void)
{
	CAMetalLayer *layer = mgl_metal_layer;
	int go;

	if (!layer || !mgl_ahead_queue)
	{
		return;
	}

	os_unfair_lock_lock(&mgl_ahead_lock);
	go = !mgl_ahead_busy && !mgl_ahead;

	if (go)
	{
		mgl_ahead_busy = 1;
	}

	os_unfair_lock_unlock(&mgl_ahead_lock);

	if (go)
	{
		dispatch_async(mgl_ahead_queue, ^{
			@autoreleasepool
			{
				id<CAMetalDrawable> d = [layer nextDrawable];   /* nil after one second */

				os_unfair_lock_lock(&mgl_ahead_lock);
				mgl_ahead = d;
				mgl_ahead_busy = 0;
				os_unfair_lock_unlock(&mgl_ahead_lock);
			}
		});
	}
}

/* Takes the drawable acquired ahead, if any. */
static id<CAMetalDrawable>
mgl_ahead_take(void)
{
	id<CAMetalDrawable> d;

	os_unfair_lock_lock(&mgl_ahead_lock);
	d = mgl_ahead;
	mgl_ahead = nil;
	os_unfair_lock_unlock(&mgl_ahead_lock);
	return d;
}

/*
 * Waits for the acquisition on the queue (bounded, as nextDrawable is) and
 * for every present slot but the open frame's (bounded, MGL_GPU_WAIT_MS
 * for them all), and drops the drawable acquired ahead and the targets.
 * A slot whose present still waits for its frame's signal at the bound has
 * that wait ended and is waited for once more, MGL_GPU_WAIT_MS (FIX69).
 */
static void
mgl_ahead_drop(void)
{
	dispatch_time_t until = mgl_deadline(MGL_GPU_WAIT_MS);
	int i;

	if (mgl_ahead_queue)
	{
		dispatch_sync(mgl_ahead_queue, ^{});
	}

	(void)mgl_ahead_take();

	for (i = 0; i < MGL_PRESENTS; i++)
	{
		if (!mgl_present_sem[i] || (i == frame_slot))
		{
			continue;
		}

		if (dispatch_semaphore_wait(mgl_present_sem[i], until) != 0)
		{
			mgl_present_end_wait(i);

			if (dispatch_semaphore_wait(mgl_present_sem[i], mgl_deadline(MGL_GPU_WAIT_MS)) != 0)
			{
				fprintf(stderr, "mgl: a present is still on the GPU after %d ms\n", 2 * MGL_GPU_WAIT_MS);
				continue;
			}
		}

		dispatch_semaphore_signal(mgl_present_sem[i]);
		mgl_present_since[i] = 0;
	}

	for (i = 0; i < MGL_PRESENTS; i++)
	{
		mgl_present_target[i] = nil;
	}
}

/*
 * The present of the frame drawn into src, the target of the open frame's
 * present slot, on the drawable acquired ahead, apart from the frame (see
 * above): the slot's event goes on the frame's command buffer, which is committed
 * after this, and is signalled after that buffer's own commands; every
 * earlier command buffer of the frame has completed (mgl_flush waited for
 * it, and mgl_layer_present does not call this when a wait ended first),
 * so it is signalled after every write of the frame to src. The present
 * goes on the present queue, waits for it, and is returned, to be
 * committed after the frame by mgl_present_commit. nil when the frame is
 * not presented: no drawable is ready, or it is of another size than the
 * frame (the drawable size changed after it was acquired).
 */
static id<MTLCommandBuffer>
mgl_present_apart_begin(id<MTLCommandBuffer> frame, id<MTLTexture> src)
{
	id<CAMetalDrawable> d = mgl_ahead_take();
	id<MTLCommandBuffer> cmd = nil;

	/*
	 * Only while display sync is off: a frame that took its slot before
	 * display sync was turned on ends here, and a drawable acquired ahead
	 * then would be held until display sync is off again, one of the
	 * layer's drawables fewer for every frame in between.
	 */
	if (mgl_present_apart())
	{
		mgl_ahead_request();
	}

	if (d && ((d.texture.width != src.width) || (d.texture.height != src.height)))
	{
		d = nil;
	}

	if (d)
	{
		cmd = [mgl_present_queue commandBuffer];
	}

	if (!cmd)
	{
		return nil;
	}

	/*
	 * The slot's own event: only a frame that holds the slot signals it,
	 * and the slot is held until this present has completed, so no later
	 * frame's signal can end this wait (FIX51).
	 */
	mgl_present_value[frame_slot]++;
	[frame encodeSignalEvent:mgl_present_event[frame_slot] value:mgl_present_value[frame_slot]];
	[cmd encodeWaitForEvent:mgl_present_event[frame_slot] value:mgl_present_value[frame_slot]];
	mgl_present_pass(cmd, src, d.texture);
	[cmd presentDrawable:d];
	return cmd;
}

/*
 * After the frame's command buffer is committed: commits cmd, the frame's
 * present, which gives the frame's present slot back when it has
 * completed, or with no present gives the slot back now.
 */
static void
mgl_present_commit(id<MTLCommandBuffer> cmd)
{
	dispatch_semaphore_t sem;
	int slot = frame_slot;

	if (!cmd)
	{
		mgl_present_give();
		return;
	}

	sem = (slot >= 0) ? mgl_present_sem[slot] : nil;
	frame_slot = -1;
	mgl_watch(cmd);

	if (sem)
	{
		/* the present holds the slot until it completes; the bound starts (mgl_present_bound, FIX69) */
		atomic_store(&mgl_present_holds[slot], 1);
		mgl_present_since[slot] = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
		[cmd addCompletedHandler:^(id<MTLCommandBuffer> b) {
			(void)b;
			mgl_present_unhold(slot, sem);
		}];
	}

	[cmd commit];
}

static void
mgl_release_all(void)
{
	int i;

	mgl_ahead_drop();       /* nothing of the layer is left on the queues */
	mgl_ahead_queue = nil;
	mgl_present_queue = nil;

	for (i = 0; i < MGL_PRESENTS; i++)
	{
		mgl_present_event[i] = nil;
		mgl_present_sem[i] = nil;
		mgl_present_since[i] = 0;
	}

	frame_slot = -1;
	frame_enc = nil;
	frame_cmd = nil;
	frame_drawable = nil;
	frame_target = nil;
	mgl_depth = nil;
	mgl_offscreen = nil;
	mgl_last = nil;
	mgl_last_drawable = nil;
	mgl_readbuf = nil;
	upload_blit = nil;
	upload_cmd = nil;
	mgl_pipelines = nil;
	mgl_alpha_pso = nil;
	mgl_present_pso = nil;
	mgl_alpha_reported = 0;
	mgl_samplers = nil;
	mgl_depth_states = nil;
	mgl_unbound = nil;

	for (i = 0; i < MGL_FRAMES_IN_FLIGHT; i++)
	{
		ring_buf[i] = nil;
	}

	mgl_vfunc = nil;
	mgl_ffunc = nil;
	mgl_library = nil;
	mgl_queue = nil;
	mgl_device = nil;
	mgl_metal_layer = nil;

	if (mgl_view)
	{
		SDL_Metal_DestroyView(mgl_view);
		mgl_view = NULL;
	}

	mgl_frame_sem = nil;
	mgl_window = NULL;
	frame_open = frame_skipped = frame_passes = 0;
}

int
mgl_layer_create(void *sdl_window, const mgl_config_t *config, char *err, size_t errlen)
{
	(void)config;   /* depth and stencil are always depth32Float_stencil8; no MSAA yet */

	if (mgl_ready)
	{
		mgl_err(err, errlen, "a context exists already", NULL);
		return 0;
	}

	@autoreleasepool
	{
		char path[1024];
		NSError *error = nil;
		int w = 0, h = 0, i, apart;

		mgl_window = (SDL_Window *)sdl_window;
		mgl_view = SDL_Metal_CreateView(mgl_window);

		if (!mgl_view)
		{
			mgl_err(err, errlen, "SDL_Metal_CreateView failed", SDL_GetError());
			mgl_release_all();
			return 0;
		}

		mgl_metal_layer = (__bridge CAMetalLayer *)SDL_Metal_GetLayer(mgl_view);
		mgl_device = MTLCreateSystemDefaultDevice();

		if (!mgl_metal_layer || !mgl_device)
		{
			mgl_err(err, errlen, mgl_device ? "no CAMetalLayer" : "no Metal device", NULL);
			mgl_release_all();
			return 0;
		}

		mgl_queue = [mgl_device newCommandQueue];

		if (!mgl_library_path(path, sizeof(path)))
		{
			mgl_err(err, errlen, "cannot find the directory of ref_gl1", NULL);
			mgl_release_all();
			return 0;
		}

		mgl_library = [mgl_device newLibraryWithURL:[NSURL fileURLWithPath:@(path)] error:&error];

		mgl_vfunc = [mgl_library newFunctionWithName:@"mgl_vertex_main"];
		mgl_ffunc = [mgl_library newFunctionWithName:@"mgl_fragment_main"];

		if (!mgl_queue || !mgl_library || !mgl_vfunc || !mgl_ffunc)
		{
			mgl_err(err, errlen, "cannot load the shader library ref_gl1.metallib next to ref_gl1",
					error ? error.localizedDescription.UTF8String : path);
			mgl_release_all();
			return 0;
		}

		SDL_GetWindowSizeInPixels(mgl_window, &w, &h);

		mgl_metal_layer.device = mgl_device;
		mgl_metal_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
		mgl_metal_layer.maximumDrawableCount = MGL_FRAMES_IN_FLIGHT;
		mgl_metal_layer.framebufferOnly = NO;          /* glReadPixels blits from it */
		mgl_metal_layer.allowsNextDrawableTimeout = YES;
		mgl_metal_layer.displaySyncEnabled = mgl_sync ? YES : NO;
		/* an opaque layer of about 1720x700 points or more is shown once per refresh (FIX53) */
		mgl_metal_layer.opaque = mgl_sync ? YES : NO;
		mgl_metal_layer.drawableSize = CGSizeMake(w > 0 ? w : 1, h > 0 ? h : 1);
		mgl_ahead_queue = dispatch_queue_create("mgl drawable", DISPATCH_QUEUE_SERIAL);

		/* the present queue and an event for each slot (FIX51); without them every frame draws into its drawable */
		mgl_present_queue = [mgl_device newCommandQueue];
		apart = mgl_present_queue != nil;

		for (i = 0; i < MGL_PRESENTS; i++)
		{
			mgl_present_event[i] = [mgl_device newEvent];
			mgl_present_value[i] = 0;
			mgl_present_since[i] = 0;
			atomic_store(&mgl_present_holds[i], 0);
			apart = apart && (mgl_present_event[i] != nil);
		}

		for (i = 0; (i < MGL_PRESENTS) && apart; i++)
		{
			mgl_present_sem[i] = dispatch_semaphore_create(0);
			dispatch_semaphore_signal(mgl_present_sem[i]);
		}

		/*
		 * Created at 0 and raised to 3: libdispatch traps when a
		 * semaphore is released below its initial value, which a frame
		 * still on a hung GPU at shutdown would otherwise cause.
		 */
		mgl_frame_sem = dispatch_semaphore_create(0);

		for (i = 0; i < MGL_FRAMES_IN_FLIGHT; i++)
		{
			dispatch_semaphore_signal(mgl_frame_sem);
		}

		mgl_pipelines = [NSMutableDictionary dictionary];
		mgl_alpha_build();
		mgl_samplers = [NSMutableDictionary dictionary];
		mgl_depth_states = [NSMutableDictionary dictionary];

		snprintf(mgl_device_name, sizeof(mgl_device_name), "%s", mgl_device.name.UTF8String);
		mgl_report_capabilities(mgl_device);
	}

	count_frames = count_no_drawable = count_skipped = count_ring_full = 0;
	count_present_late = count_cap_late = 0;
	count_most_apart = 0;
	atomic_store(&count_failed, 0);
	mgl_ready = 1;
	return 1;
}

int
mgl_layer_ready(void)
{
	return mgl_ready;
}

const char *
mgl_layer_device_name(void)
{
	return mgl_device_name;
}

void
mgl_layer_set_sync(int on)
{
	mgl_sync = on ? 1 : 0;

	/* with display sync on every frame waits for its own drawable again */
	if (mgl_sync)
	{
		mgl_ahead_drop();
	}

	if (mgl_metal_layer)
	{
		mgl_metal_layer.displaySyncEnabled = mgl_sync ? YES : NO;
		/* an opaque layer of about 1720x700 points or more is shown once per refresh (FIX53) */
		mgl_metal_layer.opaque = mgl_sync ? YES : NO;
	}
}

int
mgl_layer_get_sync(void)
{
	return mgl_sync;
}

static id<MTLTexture>
mgl_new_texture(MTLPixelFormat format, int w, int h, MTLTextureUsage usage)
{
	MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
			width:(NSUInteger)w height:(NSUInteger)h mipmapped:NO];

	d.storageMode = MTLStorageModePrivate;
	d.usage = usage;
	return [mgl_device newTextureWithDescriptor:d];
}

/*
 * Reports a command buffer that ends in error (a GPU fault, a timeout, or
 * a fault the shader validation layer found), which would otherwise draw
 * nothing and say nothing: the first MGL_FAILED_LOGGED are logged, all are
 * counted and the count is printed when the context is destroyed. Called
 * before every commit.
 */
#define MGL_FAILED_LOGGED 8

static void
mgl_watch(id<MTLCommandBuffer> cmd)
{
	[cmd addCompletedHandler:^(id<MTLCommandBuffer> b) {
		unsigned long n;

		if (b.status != MTLCommandBufferStatusError)
		{
			return;
		}

		n = atomic_fetch_add(&count_failed, 1);

		if (n < MGL_FAILED_LOGGED)
		{
			fprintf(stderr, "mgl: a command buffer failed: %s\n",
					b.error ? b.error.localizedDescription.UTF8String : "no error given");
		}
	}];
}

/* Commits cmd and waits for it, at most MGL_GPU_WAIT_MS. */
static int
mgl_commit_wait(id<MTLCommandBuffer> cmd)
{
	dispatch_semaphore_t done = dispatch_semaphore_create(0);
	long r;

	mgl_watch(cmd);
	[cmd addCompletedHandler:^(id<MTLCommandBuffer> b) {
		(void)b;
		dispatch_semaphore_signal(done);
	}];
	[cmd commit];

	r = dispatch_semaphore_wait(done, mgl_deadline(MGL_GPU_WAIT_MS));
	return r == 0 && cmd.status == MTLCommandBufferStatusCompleted;
}

/* Commits the uploads made outside a frame; the queue runs them before any later command buffer. */
static void
mgl_commit_uploads(void)
{
	if (upload_blit)
	{
		[upload_blit endEncoding];
		upload_blit = nil;
	}

	if (upload_cmd)
	{
		mgl_watch(upload_cmd);
		[upload_cmd commit];
		upload_cmd = nil;
	}
}

static void
mgl_end_encoder(void)
{
	if (frame_enc)
	{
		[frame_enc endEncoding];
		frame_enc = nil;
	}
}

static void
mgl_apply_raster(void)
{
	mgl_raster_t r;
	MTLViewport vp;
	MTLScissorRect sc;

	if (!frame_enc)
	{
		return;
	}

	mgl_compute_raster(&mgl_state, frame_w, frame_h, &r);

	vp.originX = r.x;
	vp.originY = r.y;
	vp.width = r.w > 0 ? r.w : 1;
	vp.height = r.h > 0 ? r.h : 1;
	vp.znear = r.znear;
	vp.zfar = r.zfar;
	[frame_enc setViewport:vp];

	/* An empty GL scissor has no Metal form; the draw path skips draws while it holds. */
	sc.x = r.scissor_empty ? 0 : (NSUInteger)r.sx;
	sc.y = r.scissor_empty ? 0 : (NSUInteger)r.sy;
	sc.width = r.scissor_empty ? 1 : (NSUInteger)r.sw;
	sc.height = r.scissor_empty ? 1 : (NSUInteger)r.sh;
	[frame_enc setScissorRect:sc];
}

void
mgl_layer_update_raster(void)
{
	if (mgl_ready && frame_enc)
	{
		@autoreleasepool
		{
			mgl_apply_raster();
		}
	}
}

int
mgl_layer_frame_begin(int *tw, int *th)
{
	static int once_skip, once_drawable;

	if (!mgl_ready)
	{
		return 0;
	}

	if (frame_open)
	{
		*tw = frame_w;
		*th = frame_h;
		return !frame_skipped;
	}

	/* the uploads made since the last frame go to the GPU before it */
	mgl_commit_uploads();
	mgl_present_bound();   /* no present waits for its frame past the bound (FIX69) */

	frame_open = 1;
	frame_skipped = 0;
	frame_passes = 0;
	frame_unseen = 0;

	if (dispatch_semaphore_wait(mgl_frame_sem, mgl_deadline(MGL_FRAME_WAIT_MS)) != 0)
	{
		frame_skipped = 1;
		count_skipped++;

		if (!once_skip)
		{
			once_skip = 1;
			fprintf(stderr, "mgl: the GPU still holds %d frames after %d ms, skipping a frame\n",
					MGL_FRAMES_IN_FLIGHT, MGL_FRAME_WAIT_MS);
		}

		return 0;
	}

	/*
	 * FIX68: with display sync off at most two frames are on the GPU, not
	 * three. Frames no drawable takes are drawn there too, and a frame that
	 * is presented, and then its present, waited on the GPU behind them:
	 * with the GPU full (the shim at 5120x2880), from a drawable's
	 * acquisition to its present's end 5.7 ms where the old loop took 4.6,
	 * and the drawables came back that much later
	 * (docs/runbook/FIX68.md). So the frame begins only once a second slot
	 * of the semaphore is free too, given back at once. When that wait
	 * times out the frame goes on, three on the GPU; the teardown line
	 * counts it, and the most frames on the GPU as one began (FIX69).
	 */
	if (mgl_present_apart())
	{
		int n;

		if (dispatch_semaphore_wait(mgl_frame_sem, mgl_deadline(MGL_FRAME_WAIT_MS)) == 0)
		{
			dispatch_semaphore_signal(mgl_frame_sem);
		}
		else
		{
			count_cap_late++;
		}

		n = atomic_load(&mgl_on_gpu) + 1;

		if (n > count_most_apart)
		{
			count_most_apart = n;
		}
	}

	@autoreleasepool
	{
		int w = 0, h = 0;
		MTLTextureUsage rt = MTLTextureUsageRenderTarget;

		SDL_GetWindowSizeInPixels(mgl_window, &w, &h);
		w = w > 0 ? w : 1;
		h = h > 0 ? h : 1;

		if (mgl_metal_layer.drawableSize.width != w || mgl_metal_layer.drawableSize.height != h)
		{
			mgl_metal_layer.drawableSize = CGSizeMake(w, h);
			/* the kept frame is of the old size: nothing to read, nothing to hold */
			mgl_last = nil;
			mgl_last_drawable = nil;
		}

		/* with display sync off the frame is drawn offscreen, into a present slot's target, and presented apart (FIX51) */
		frame_drawable = mgl_present_apart() ? nil : [mgl_metal_layer nextDrawable];
		frame_target = mgl_present_apart() ? mgl_present_take(w, h) : nil;

		if (frame_drawable)
		{
			frame_target = frame_drawable.texture;
		}
		else if (!frame_target)
		{
			/* with display sync off every slot is held: drawn here, and not presented */
			/* with display sync off: counted at the present, when no drawable takes the frame */
			if (!mgl_present_apart())
			{
				count_no_drawable++;
			}

			if (!once_drawable && !mgl_present_apart())
			{
				once_drawable = 1;
				fprintf(stderr, "mgl: no drawable (window not shown?), drawing offscreen and not presenting\n");
			}

			if (!mgl_offscreen || (int)mgl_offscreen.width != w || (int)mgl_offscreen.height != h)
			{
				mgl_offscreen = mgl_new_texture(MTLPixelFormatBGRA8Unorm, w, h,
						rt | MTLTextureUsageShaderRead);
			}

			frame_target = mgl_offscreen;
		}

		frame_cmd = [mgl_queue commandBuffer];

		if (frame_target)
		{
			frame_w = (int)frame_target.width;
			frame_h = (int)frame_target.height;

			if (!mgl_depth || (int)mgl_depth.width != frame_w || (int)mgl_depth.height != frame_h)
			{
				mgl_depth = mgl_new_texture(MTLPixelFormatDepth32Float_Stencil8, frame_w, frame_h, rt);
			}
		}

		if (!frame_target || !frame_cmd || !mgl_depth)
		{
			frame_drawable = nil;
			frame_target = nil;
			frame_cmd = nil;
			frame_skipped = 1;
			count_skipped++;
			mgl_present_give();   /* nothing to present: the present slot comes back */
			dispatch_semaphore_signal(mgl_frame_sem);
			return 0;
		}
	}

	/* this frame holds a slot of the semaphore: its ring buffer is free */
	ring_slot = (ring_slot + 1) % MGL_FRAMES_IN_FLIGHT;
	ring.size = MGL_RING_BYTES;
	ring.used = 0;

	*tw = frame_w;
	*th = frame_h;
	return 1;
}

int
mgl_layer_pass(GLbitfield clear_mask, const GLclampf color[4], double depth, unsigned stencil)
{
	if (!frame_open || frame_skipped || !frame_cmd)
	{
		return 0;
	}

	@autoreleasepool
	{
		MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
		int first = frame_passes == 0;

		mgl_end_encoder();

		/*
		 * A cleared attachment loads its clear value. On the frame's
		 * first pass the rest is cleared too (black, depth 1, stencil
		 * 0) instead of left undefined, so a frame is reproducible.
		 */
		rp.colorAttachments[0].texture = frame_target;
		rp.colorAttachments[0].storeAction = MTLStoreActionStore;

		if (clear_mask & GL_COLOR_BUFFER_BIT)
		{
			rp.colorAttachments[0].loadAction = MTLLoadActionClear;
			rp.colorAttachments[0].clearColor = MTLClearColorMake(color[0], color[1], color[2], color[3]);
		}
		else if (first)
		{
			rp.colorAttachments[0].loadAction = MTLLoadActionClear;
			rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
		}
		else
		{
			rp.colorAttachments[0].loadAction = MTLLoadActionLoad;
		}

		rp.depthAttachment.texture = mgl_depth;
		rp.depthAttachment.storeAction = MTLStoreActionStore;
		rp.depthAttachment.clearDepth = (clear_mask & GL_DEPTH_BUFFER_BIT) ? depth : 1.0;
		rp.depthAttachment.loadAction = ((clear_mask & GL_DEPTH_BUFFER_BIT) || first) ?
			MTLLoadActionClear : MTLLoadActionLoad;

		rp.stencilAttachment.texture = mgl_depth;
		rp.stencilAttachment.storeAction = MTLStoreActionStore;
		rp.stencilAttachment.clearStencil = (clear_mask & GL_STENCIL_BUFFER_BIT) ? stencil : 0;
		rp.stencilAttachment.loadAction = ((clear_mask & GL_STENCIL_BUFFER_BIT) || first) ?
			MTLLoadActionClear : MTLLoadActionLoad;

		frame_enc = [frame_cmd renderCommandEncoderWithDescriptor:rp];

		if (!frame_enc)
		{
			return 0;
		}

		frame_passes++;
		mgl_apply_raster();
	}

	return 1;
}

int
mgl_layer_ensure_pass(void)
{
	static const GLclampf black[4] = {0, 0, 0, 1};
	int w, h;

	if (!mgl_layer_frame_begin(&w, &h))
	{
		return 0;
	}

	/*
	 * After a mid-frame flush (glReadPixels, glFinish) or an upload the
	 * encoder is nil while frame_passes > 0, so this answers 1 with no
	 * open encoder: the target is defined, which is all readback and
	 * present need. mgl_layer_draw opens a Load pass itself.
	 */
	return frame_passes ? 1 : mgl_layer_pass(0, black, 1.0, 0);
}

int
mgl_layer_read_prepare(void)
{
	int w, h;

	if (!mgl_layer_frame_begin(&w, &h))
	{
		return -1;
	}

	if (mgl_read_uses_last(frame_passes, mgl_last ? (int)mgl_last.width : 0,
			mgl_last ? (int)mgl_last.height : 0, w, h))
	{
		return 1;
	}

	return mgl_layer_ensure_pass() ? 0 : -1;
}

/*
 * Commits the frame so far, waits for it, and opens a new command buffer
 * for the rest of it. When the wait ends before the command buffer has
 * completed, the frame is not presented apart (FIX51, mgl_present_apart_begin).
 */
static int
mgl_flush(void)
{
	int ok;

	mgl_end_encoder();
	ok = mgl_commit_wait(frame_cmd);
	frame_cmd = [mgl_queue commandBuffer];

	if (!ok)
	{
		frame_unseen = 1;
	}

	return ok;
}

int
mgl_layer_finish(void)
{
	int ok = 1, got = 0, held, i;

	if (!mgl_ready)
	{
		return 0;
	}

	@autoreleasepool
	{
		/* the uploads made outside a frame first */
		if (upload_cmd)
		{
			id<MTLCommandBuffer> cmd = upload_cmd;

			if (upload_blit)
			{
				[upload_blit endEncoding];
				upload_blit = nil;
			}

			upload_cmd = nil;
			ok = mgl_commit_wait(cmd);
		}

		/* an open frame holds one slot of the frame semaphore itself */
		held = (frame_open && !frame_skipped) ? 1 : 0;

		if (held && frame_cmd)
		{
			ok = mgl_flush() && ok;
		}

		/* and the frames presented before it: every other slot comes back when they complete */
		for (i = 0; i < MGL_FRAMES_IN_FLIGHT - held; i++)
		{
			if (dispatch_semaphore_wait(mgl_frame_sem,
					mgl_deadline(MGL_GPU_WAIT_MS)) != 0)
			{
				ok = 0;
				break;
			}

			got++;
		}

		for (i = 0; i < got; i++)
		{
			dispatch_semaphore_signal(mgl_frame_sem);
		}
	}

	return ok;
}

int
mgl_layer_read(int x, int y, int w, int h, unsigned char *bgra, int last)
{
	if (!frame_open || frame_skipped || !frame_cmd || x < 0 || y < 0 || w <= 0 || h <= 0 ||
		x + w > frame_w || y + h > frame_h)
	{
		return 0;
	}

	@autoreleasepool
	{
		NSUInteger row = (NSUInteger)w * 4, need = row * (NSUInteger)h;
		id<MTLBlitCommandEncoder> blit;
		int ok;

		mgl_end_encoder();

		if (!mgl_readbuf || mgl_readbuf.length < need)
		{
			mgl_readbuf = [mgl_device newBufferWithLength:need options:MTLResourceStorageModeShared];

			if (!mgl_readbuf)
			{
				return 0;
			}
		}

		blit = [frame_cmd blitCommandEncoder];
		[blit copyFromTexture:(last ? mgl_last : frame_target)
				sourceSlice:0
				sourceLevel:0
				sourceOrigin:MTLOriginMake((NSUInteger)x, (NSUInteger)y, 0)
				sourceSize:MTLSizeMake((NSUInteger)w, (NSUInteger)h, 1)
				toBuffer:mgl_readbuf
				destinationOffset:0
				destinationBytesPerRow:row
				destinationBytesPerImage:need];
		[blit endEncoding];

		ok = mgl_flush();

		if (!ok)
		{
			/* the GPU may still write it; the command buffer keeps it alive */
			mgl_readbuf = nil;
			return 0;
		}

		memcpy(bgra, mgl_readbuf.contents, need);
	}

	return 1;
}

int
mgl_layer_present(void)
{
	int w, h;

	if (!mgl_ready)
	{
		return 0;
	}

	if (!frame_open)
	{
		/* nothing drew this frame: present a cleared one */
		mgl_layer_frame_begin(&w, &h);
	}

	if (frame_skipped)
	{
		frame_open = frame_skipped = frame_passes = 0;
		return 1;
	}

	@autoreleasepool
	{
		dispatch_semaphore_t sem = mgl_frame_sem;

		mgl_layer_ensure_pass();
		mgl_end_encoder();

		if (frame_cmd)
		{
			id<MTLCommandBuffer> pcmd = nil;

			if ((frame_slot >= 0) && !frame_unseen)
			{
				pcmd = mgl_present_apart_begin(frame_cmd, frame_target);
			}
			else if (mgl_present_apart())
			{
				/* every slot was held, or the frame's earlier command buffer was not seen complete:
				   a drawable ready is left for the next frame */
				mgl_ahead_request();
			}

			/* with display sync off, a frame no drawable takes */
			if (!frame_drawable && !pcmd && ((frame_slot >= 0) || mgl_present_apart()))
			{
				count_no_drawable++;
			}

			if (frame_drawable)
			{
				if (!mgl_metal_layer.opaque)
				{
					mgl_force_alpha(frame_cmd, frame_drawable.texture);
				}

				[frame_cmd presentDrawable:frame_drawable];
			}

			mgl_watch(frame_cmd);   /* first: it counts before the slot is given back */
			atomic_fetch_add(&mgl_on_gpu, 1);
			[frame_cmd addCompletedHandler:^(id<MTLCommandBuffer> b) {
				(void)b;
				atomic_fetch_sub(&mgl_on_gpu, 1);   /* before the slot: a frame that takes it sees this one gone */
				dispatch_semaphore_signal(sem);
			}];
			[frame_cmd commit];
			mgl_present_commit(pcmd);   /* after the frame it reads; without one the slot comes back */

			/* what is now on screen; a read between frames reads it in place */
			mgl_last = frame_target;
			mgl_last_drawable = frame_drawable;
		}
		else
		{
			mgl_present_give();
			dispatch_semaphore_signal(sem);
		}

		frame_cmd = nil;
		frame_drawable = nil;
		frame_target = nil;
	}

	frame_open = frame_passes = 0;
	count_frames++;
	return 1;
}

void
mgl_layer_destroy(void)
{
	int i;

	if (!mgl_ready)
	{
		return;
	}

	@autoreleasepool
	{
		/* uploads nothing will draw with: ended, never committed */
		if (upload_blit)
		{
			[upload_blit endEncoding];
			upload_blit = nil;
		}

		if (frame_open && !frame_skipped && frame_cmd)
		{
			dispatch_semaphore_t sem = mgl_frame_sem;

			mgl_end_encoder();
			mgl_watch(frame_cmd);   /* first: it counts before the wait below returns */
			[frame_cmd addCompletedHandler:^(id<MTLCommandBuffer> b) {
				(void)b;
				dispatch_semaphore_signal(sem);
			}];
			[frame_cmd commit];
		}

		mgl_present_give();   /* the open frame is not presented (FIX51) */

		/*
		 * wait for the frames on the GPU, bounded; the semaphore outlives a hung one.
		 * The completion handlers are code in this dylib: one that arrives after
		 * this wait gives up, and after the engine unloads the module, would run
		 * unmapped code. It needs a GPU hung past MGL_GPU_WAIT_MS and is not
		 * handled yet (design section 7, notes for the next card).
		 */
		for (i = 0; i < MGL_FRAMES_IN_FLIGHT; i++)
		{
			if (dispatch_semaphore_wait(mgl_frame_sem, mgl_deadline(MGL_GPU_WAIT_MS)) != 0)
			{
				break;
			}
		}

		/* then the presents, bounded, before the line below counts them (FIX69; mgl_release_all finds none left) */
		mgl_ahead_drop();

		fprintf(stderr, "mgl: %lu frames, %lu without a drawable, %lu skipped, %lu draws past the vertex ring, "
				"%lu present waits ended; with display sync off at most %d frames on the GPU, %lu past the cap\n",
				count_frames, count_no_drawable, count_skipped, count_ring_full, count_present_late,
				count_most_apart, count_cap_late);

		if (atomic_load(&count_failed))
		{
			fprintf(stderr, "mgl: %lu command buffers failed\n", atomic_load(&count_failed));
		}

		mgl_release_all();
	}

	mgl_ready = 0;
}

/* ---- textures ---------------------------------------------------------------- */

void *
mgl_layer_tex_create(int w, int h, int levels)
{
	if (!mgl_ready || w <= 0 || h <= 0 || levels <= 0)
	{
		return NULL;
	}

	@autoreleasepool
	{
		MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
				width:(NSUInteger)w height:(NSUInteger)h mipmapped:NO];
		id<MTLTexture> t;

		d.mipmapLevelCount = (NSUInteger)levels;
		d.storageMode = MTLStorageModePrivate;
		d.usage = MTLTextureUsageShaderRead;
		t = [mgl_device newTextureWithDescriptor:d];

		/* the texture table of mgl_tex.c owns it until mgl_layer_tex_release */
		return t ? (void *)CFBridgingRetain(t) : NULL;
	}
}

void
mgl_layer_tex_release(void *tex)
{
	if (tex)
	{
		/* a command buffer that still uses it keeps its own reference */
		CFBridgingRelease(tex);
	}
}

int
mgl_layer_tex_upload(void *tex, int level, int x, int y, int w, int h,
		const unsigned char *rgba, int gen_mips)
{
	if (!mgl_ready || !tex || w <= 0 || h <= 0)
	{
		return 0;
	}

	@autoreleasepool
	{
		id<MTLTexture> t = (__bridge id<MTLTexture>)tex;
		NSUInteger row = (NSUInteger)w * 4, size = row * (NSUInteger)h;
		id<MTLBuffer> staging = [mgl_device newBufferWithBytes:rgba length:size
				options:MTLResourceStorageModeShared];
		id<MTLBlitCommandEncoder> blit;
		int in_frame = frame_open && !frame_skipped && frame_cmd;

		if (!staging)
		{
			return 0;
		}

		if (in_frame)
		{
			/*
			 * Between the draws: the copy is encoded after the ones before
			 * it and before the ones after it, which open a Load pass.
			 */
			mgl_end_encoder();
			blit = [frame_cmd blitCommandEncoder];
		}
		else
		{
			if (!upload_cmd)
			{
				upload_cmd = [mgl_queue commandBuffer];
			}

			if (upload_cmd && !upload_blit)
			{
				upload_blit = [upload_cmd blitCommandEncoder];
			}

			blit = upload_blit;
		}

		if (!blit)
		{
			return 0;
		}

		[blit copyFromBuffer:staging
				sourceOffset:0
				sourceBytesPerRow:row
				sourceBytesPerImage:size
				sourceSize:MTLSizeMake((NSUInteger)w, (NSUInteger)h, 1)
				toTexture:t
				destinationSlice:0
				destinationLevel:(NSUInteger)level
				destinationOrigin:MTLOriginMake((NSUInteger)x, (NSUInteger)y, 0)];

		if (gen_mips)
		{
			[blit generateMipmapsForTexture:t];
		}

		if (in_frame)
		{
			[blit endEncoding];
		}
	}

	return 1;
}

/* ---- pipeline, depth and sampler state ----------------------------------------- */

/* Indexed by mgl_blend_index (mgl_draw.c). */
static const MTLBlendFactor mgl_blend_factors[] = {
	MTLBlendFactorZero, MTLBlendFactorOne, MTLBlendFactorSourceColor,
	MTLBlendFactorOneMinusSourceColor, MTLBlendFactorSourceAlpha,
	MTLBlendFactorOneMinusSourceAlpha, MTLBlendFactorDestinationAlpha,
	MTLBlendFactorOneMinusDestinationAlpha, MTLBlendFactorDestinationColor,
	MTLBlendFactorOneMinusDestinationColor, MTLBlendFactorSourceAlphaSaturated
};

#define MGL_BLEND_FACTORS (sizeof(mgl_blend_factors) / sizeof(mgl_blend_factors[0]))

static id<MTLRenderPipelineState>
mgl_pipeline(uint32_t key)
{
	static int once;
	id<MTLRenderPipelineState> p = mgl_pipelines[@(key)];
	MTLRenderPipelineDescriptor *d;
	uint32_t src = (key >> 1) & 15, dst = (key >> 5) & 15;
	NSError *error = nil;

	if (p)
	{
		return p;
	}

	if ((key & 1) && (src >= MGL_BLEND_FACTORS || dst >= MGL_BLEND_FACTORS))
	{
		return nil;
	}

	d = [MTLRenderPipelineDescriptor new];
	d.vertexFunction = mgl_vfunc;
	d.fragmentFunction = mgl_ffunc;
	d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
	d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
	d.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;

	if (key & 1)
	{
		d.colorAttachments[0].blendingEnabled = YES;
		d.colorAttachments[0].sourceRGBBlendFactor = mgl_blend_factors[src];
		d.colorAttachments[0].sourceAlphaBlendFactor = mgl_blend_factors[src];
		d.colorAttachments[0].destinationRGBBlendFactor = mgl_blend_factors[dst];
		d.colorAttachments[0].destinationAlphaBlendFactor = mgl_blend_factors[dst];
	}

	d.colorAttachments[0].writeMask = ((key & (1u << 9)) ? MTLColorWriteMaskRed : 0) |
		((key & (1u << 10)) ? MTLColorWriteMaskGreen : 0) |
		((key & (1u << 11)) ? MTLColorWriteMaskBlue : 0) |
		((key & (1u << 12)) ? MTLColorWriteMaskAlpha : 0);

	p = [mgl_device newRenderPipelineStateWithDescriptor:d error:&error];

	if (!p)
	{
		if (!once)
		{
			once = 1;
			fprintf(stderr, "mgl: cannot make a pipeline (key 0x%x): %s\n", key,
					error ? error.localizedDescription.UTF8String : "unknown");
		}

		return nil;
	}

	mgl_pipelines[@(key)] = p;
	return p;
}

/* The key is mgl_sampler_key's (mgl_tex.c). GL_CLAMP is drawn as clamp to edge: Metal has no border blend. */
static id<MTLSamplerState>
mgl_sampler(uint32_t key)
{
	static const MTLSamplerAddressMode wrap[] = {
		MTLSamplerAddressModeRepeat, MTLSamplerAddressModeClampToEdge,
		MTLSamplerAddressModeClampToEdge, MTLSamplerAddressModeClampToEdge
	};
	id<MTLSamplerState> s = mgl_samplers[@(key)];
	MTLSamplerDescriptor *d;
	uint32_t min = key & 7;

	if (s)
	{
		return s;
	}

	d = [MTLSamplerDescriptor new];
	/* GL_NEAREST, GL_LINEAR, then GL_{NEAREST,LINEAR}_MIPMAP_{NEAREST,LINEAR} */
	d.minFilter = (min == 0 || min == 2 || min == 4) ? MTLSamplerMinMagFilterNearest :
		MTLSamplerMinMagFilterLinear;
	d.mipFilter = min < 2 ? MTLSamplerMipFilterNotMipmapped :
		(min < 4 ? MTLSamplerMipFilterNearest : MTLSamplerMipFilterLinear);
	d.magFilter = (key & 8) ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
	d.sAddressMode = wrap[(key >> 4) & 3];
	d.tAddressMode = wrap[(key >> 6) & 3];
	d.maxAnisotropy = ((key >> 8) & 15) + 1;
	s = [mgl_device newSamplerStateWithDescriptor:d];

	if (s)
	{
		mgl_samplers[@(key)] = s;
	}

	return s;
}

/* The key is mgl_depth_stencil_key's (mgl_draw.c); both stencil faces get the same state, as in GL 1.4. */
static id<MTLDepthStencilState>
mgl_depth_state(uint64_t key)
{
	/* indexed by the GL function minus GL_NEVER, and by mgl_stencil_op_index */
	static const MTLCompareFunction funcs[] = {
		MTLCompareFunctionNever, MTLCompareFunctionLess, MTLCompareFunctionEqual,
		MTLCompareFunctionLessEqual, MTLCompareFunctionGreater, MTLCompareFunctionNotEqual,
		MTLCompareFunctionGreaterEqual, MTLCompareFunctionAlways
	};
	static const MTLStencilOperation ops[] = {
		MTLStencilOperationKeep, MTLStencilOperationZero, MTLStencilOperationReplace,
		MTLStencilOperationIncrementClamp, MTLStencilOperationDecrementClamp, MTLStencilOperationInvert
	};
	id<MTLDepthStencilState> s = mgl_depth_states[@(key)];
	MTLDepthStencilDescriptor *d;

	if (s)
	{
		return s;
	}

	if (((key >> 9) & 7) >= 6 || ((key >> 12) & 7) >= 6 || ((key >> 15) & 7) >= 6)
	{
		return nil;
	}

	d = [MTLDepthStencilDescriptor new];
	d.depthCompareFunction = (key & 1) ? funcs[(key >> 1) & 7] : MTLCompareFunctionAlways;
	d.depthWriteEnabled = (key & (1u << 4)) ? YES : NO;

	if (key & (1u << 5))
	{
		MTLStencilDescriptor *st = [MTLStencilDescriptor new];

		st.stencilCompareFunction = funcs[(key >> 6) & 7];
		st.stencilFailureOperation = ops[(key >> 9) & 7];
		st.depthFailureOperation = ops[(key >> 12) & 7];
		st.depthStencilPassOperation = ops[(key >> 15) & 7];
		st.readMask = (uint32_t)((key >> 18) & 0xff);
		st.writeMask = (uint32_t)((key >> 26) & 0xff);
		d.frontFaceStencil = st;
		d.backFaceStencil = st;
	}

	s = [mgl_device newDepthStencilStateWithDescriptor:d];

	if (s)
	{
		mgl_depth_states[@(key)] = s;
	}

	return s;
}

/*
 * Copies n bytes into the frame's ring buffer: the buffer and the
 * offset, or nil when the ring is full (the caller then makes a buffer
 * for the draw alone).
 */
static id<MTLBuffer>
mgl_ring_copy(const void *bytes, size_t n, NSUInteger *offset)
{
	long at;

	if (!ring_buf[ring_slot])
	{
		ring_buf[ring_slot] = [mgl_device newBufferWithLength:MGL_RING_BYTES
				options:MTLResourceStorageModeShared];
	}

	if (!ring_buf[ring_slot] || (at = mgl_ring_alloc(&ring, n)) < 0)
	{
		return nil;
	}

	memcpy((unsigned char *)ring_buf[ring_slot].contents + at, bytes, n);
	*offset = (NSUInteger)at;
	return ring_buf[ring_slot];
}

/* ---- draws ------------------------------------------------------------------------ */

int
mgl_layer_draw(const mgl_draw_t *d)
{
	static const GLclampf black[4] = {0, 0, 0, 1};
	int w, h;

	if (!mgl_layer_frame_begin(&w, &h))
	{
		/* a skipped frame draws nothing, as its clears do nothing */
		return 1;
	}

	@autoreleasepool
	{
		static int once_full;
		static const MTLCullMode culls[] = {MTLCullModeNone, MTLCullModeFront, MTLCullModeBack};
		id<MTLRenderPipelineState> pso;
		id<MTLSamplerState> smp, smp1;
		id<MTLDepthStencilState> ds;
		id<MTLBuffer> vbuf, ibuf;
		NSUInteger voff = 0, ioff = 0;
		size_t vbytes = d->nvertices * sizeof(mgl_vertex_t), ibytes = d->nindices * sizeof(uint16_t);
		mgl_raster_t r;

		/* the first pass of the frame, or a Load pass after a flush or an upload */
		if (!frame_enc && !mgl_layer_pass(0, black, 1.0, 0))
		{
			return 0;
		}

		mgl_compute_raster(&mgl_state, frame_w, frame_h, &r);

		if (r.scissor_empty)
		{
			return 1;
		}

		if (!mgl_unbound)
		{
			mgl_unbound = mgl_new_texture(MTLPixelFormatRGBA8Unorm, 1, 1, MTLTextureUsageShaderRead);
		}

		pso = mgl_pipeline(d->pipeline_key);
		smp = mgl_sampler(d->texture0 ? d->sampler0 : 0);
		smp1 = mgl_sampler(d->texture1 ? d->sampler1 : 0);
		ds = mgl_depth_state(d->depth_stencil_key);

		/* the frame's ring; a draw it cannot hold gets buffers of its own, which the command buffer keeps alive */
		vbuf = mgl_ring_copy(d->vertices, vbytes, &voff);
		ibuf = vbuf ? mgl_ring_copy(d->indices, ibytes, &ioff) : nil;

		if (!vbuf || !ibuf)
		{
			count_ring_full++;

			if (!once_full)
			{
				once_full = 1;
				fprintf(stderr, "mgl: the vertex ring (%u bytes) is full, drawing from a buffer per draw\n",
						(unsigned)MGL_RING_BYTES);
			}

			voff = ioff = 0;
			vbuf = [mgl_device newBufferWithBytes:d->vertices length:vbytes options:MTLResourceStorageModeShared];
			ibuf = [mgl_device newBufferWithBytes:d->indices length:ibytes options:MTLResourceStorageModeShared];
		}

		if (!pso || !smp || !smp1 || !ds || !vbuf || !ibuf || !mgl_unbound)
		{
			return 0;
		}

		[frame_enc setRenderPipelineState:pso];
		[frame_enc setDepthStencilState:ds];
		[frame_enc setStencilReferenceValue:d->stencil_ref];
		[frame_enc setFrontFacingWinding:MTLWindingCounterClockwise];
		[frame_enc setCullMode:culls[d->cull >= 0 && d->cull <= MGL_CULL_BACK ? d->cull : 0]];
		[frame_enc setTriangleFillMode:(d->fill_lines ? MTLTriangleFillModeLines : MTLTriangleFillModeFill)];
		[frame_enc setDepthBias:d->depth_bias slopeScale:d->depth_slope clamp:0.0f];
		[frame_enc setVertexBuffer:vbuf offset:voff atIndex:0];
		[frame_enc setVertexBytes:&d->u length:sizeof(d->u) atIndex:1];
		[frame_enc setFragmentBytes:&d->u length:sizeof(d->u) atIndex:1];
		[frame_enc setFragmentTexture:(d->texture0 ? (__bridge id<MTLTexture>)d->texture0 : mgl_unbound)
				atIndex:0];
		[frame_enc setFragmentSamplerState:smp atIndex:0];
		[frame_enc setFragmentTexture:(d->texture1 ? (__bridge id<MTLTexture>)d->texture1 : mgl_unbound)
				atIndex:1];
		[frame_enc setFragmentSamplerState:smp1 atIndex:1];
		[frame_enc drawIndexedPrimitives:(d->lines ? MTLPrimitiveTypeLine : MTLPrimitiveTypeTriangle)
				indexCount:d->nindices
				indexType:MTLIndexTypeUInt16
				indexBuffer:ibuf
				indexBufferOffset:ioff];
	}

	return 1;
}
