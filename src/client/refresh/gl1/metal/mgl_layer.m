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
 *  - nextDrawable returns nil after one second (allowsNextDrawableTimeout,
 *    for example while the window cannot be shown); the frame is then
 *    drawn into an offscreen texture of the same size, which glReadPixels
 *    reads, and nothing is presented;
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
static id<CAMetalDrawable> frame_drawable;
static id<MTLTexture> frame_target;        /* the drawable's texture, or mgl_offscreen */
static id<MTLCommandBuffer> frame_cmd;
static id<MTLRenderCommandEncoder> frame_enc;
static int frame_w, frame_h;

/* Kept across frames, rebuilt when the size changes. */
static id<MTLTexture> mgl_depth;           /* depth32Float_stencil8, private */
static id<MTLTexture> mgl_offscreen;       /* the target when there is no drawable */
static id<MTLBuffer> mgl_readbuf;          /* shared, for glReadPixels */

/* Counters, printed when the context is destroyed. */
static unsigned long count_frames, count_no_drawable, count_skipped;

#define MGL_FRAMES_IN_FLIGHT 3

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

static void
mgl_release_all(void)
{
	frame_enc = nil;
	frame_cmd = nil;
	frame_drawable = nil;
	frame_target = nil;
	mgl_depth = nil;
	mgl_offscreen = nil;
	mgl_readbuf = nil;
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
		int w = 0, h = 0, i;

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

		if (!mgl_queue || !mgl_library || ![[mgl_library functionNames] containsObject:@"mgl_vertex_stub"])
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
		mgl_metal_layer.drawableSize = CGSizeMake(w > 0 ? w : 1, h > 0 ? h : 1);

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

		snprintf(mgl_device_name, sizeof(mgl_device_name), "%s", mgl_device.name.UTF8String);
	}

	count_frames = count_no_drawable = count_skipped = 0;
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

	if (mgl_metal_layer)
	{
		mgl_metal_layer.displaySyncEnabled = mgl_sync ? YES : NO;
	}
}

int
mgl_layer_get_sync(void)
{
	return mgl_sync;
}

static id<MTLTexture>
mgl_texture(MTLPixelFormat format, int w, int h, MTLTextureUsage usage)
{
	MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
			width:(NSUInteger)w height:(NSUInteger)h mipmapped:NO];

	d.storageMode = MTLStorageModePrivate;
	d.usage = usage;
	return [mgl_device newTextureWithDescriptor:d];
}

/* Commits cmd and waits for it, at most MGL_GPU_WAIT_MS. */
static int
mgl_commit_wait(id<MTLCommandBuffer> cmd)
{
	dispatch_semaphore_t done = dispatch_semaphore_create(0);
	long r;

	[cmd addCompletedHandler:^(id<MTLCommandBuffer> b) {
		(void)b;
		dispatch_semaphore_signal(done);
	}];
	[cmd commit];

	r = dispatch_semaphore_wait(done, mgl_deadline(MGL_GPU_WAIT_MS));
	return r == 0 && cmd.status == MTLCommandBufferStatusCompleted;
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

	frame_open = 1;
	frame_skipped = 0;
	frame_passes = 0;

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
		}

		frame_drawable = [mgl_metal_layer nextDrawable];

		if (frame_drawable)
		{
			frame_target = frame_drawable.texture;
		}
		else
		{
			count_no_drawable++;

			if (!once_drawable)
			{
				once_drawable = 1;
				fprintf(stderr, "mgl: no drawable (window not shown?), drawing offscreen and not presenting\n");
			}

			if (!mgl_offscreen || (int)mgl_offscreen.width != w || (int)mgl_offscreen.height != h)
			{
				mgl_offscreen = mgl_texture(MTLPixelFormatBGRA8Unorm, w, h,
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
				mgl_depth = mgl_texture(MTLPixelFormatDepth32Float_Stencil8, frame_w, frame_h, rt);
			}
		}

		if (!frame_target || !frame_cmd || !mgl_depth)
		{
			frame_drawable = nil;
			frame_target = nil;
			frame_cmd = nil;
			frame_skipped = 1;
			count_skipped++;
			dispatch_semaphore_signal(mgl_frame_sem);
			return 0;
		}
	}

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

	return frame_passes ? 1 : mgl_layer_pass(0, black, 1.0, 0);
}

/* Commits the frame so far and opens a new command buffer for the rest of it. */
static int
mgl_flush(void)
{
	int ok;

	mgl_end_encoder();
	ok = mgl_commit_wait(frame_cmd);
	frame_cmd = [mgl_queue commandBuffer];
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
		/* an open frame holds one slot of the frame semaphore itself */
		held = (frame_open && !frame_skipped) ? 1 : 0;

		if (held && frame_cmd)
		{
			ok = mgl_flush();
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
mgl_layer_read(int x, int y, int w, int h, unsigned char *bgra)
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
		[blit copyFromTexture:frame_target
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
			if (frame_drawable)
			{
				[frame_cmd presentDrawable:frame_drawable];
			}

			[frame_cmd addCompletedHandler:^(id<MTLCommandBuffer> b) {
				(void)b;
				dispatch_semaphore_signal(sem);
			}];
			[frame_cmd commit];
		}
		else
		{
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
		if (frame_open && !frame_skipped && frame_cmd)
		{
			dispatch_semaphore_t sem = mgl_frame_sem;

			mgl_end_encoder();
			[frame_cmd addCompletedHandler:^(id<MTLCommandBuffer> b) {
				(void)b;
				dispatch_semaphore_signal(sem);
			}];
			[frame_cmd commit];
		}

		/* wait for the frames on the GPU, bounded; the semaphore outlives a hung one */
		for (i = 0; i < MGL_FRAMES_IN_FLIGHT; i++)
		{
			if (dispatch_semaphore_wait(mgl_frame_sem, mgl_deadline(MGL_GPU_WAIT_MS)) != 0)
			{
				break;
			}
		}

		fprintf(stderr, "mgl: %lu frames, %lu without a drawable, %lu skipped\n",
				count_frames, count_no_drawable, count_skipped);

		mgl_release_all();
	}

	mgl_ready = 0;
}
