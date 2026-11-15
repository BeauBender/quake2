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
 * The frame of ref_metal's back end (docs/plans/ref-metal-native.md,
 * section 3): the offscreen colour target (bgra8Unorm) and its
 * depth32Float_stencil8 attachment, the 3D and 2D passes, the readback
 * blit and the present blit. Everything is drawn offscreen, so readback
 * does not depend on the drawable; at the end of the frame the target is
 * blitted to the drawable when one comes within its timeout, and the
 * frame is complete (and counted) without one. Readback follows the
 * shim's mgl_layer_read: end the pass, blit into a shared buffer, commit
 * and wait (bounded), go on with a new command buffer whose next pass
 * loads the target.
 *
 * =======================================================================
 */

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <stdio.h>
#include <string.h>

#include "header/rmb.h"
#include "common/mtl_ring.h"

#if !__has_feature(objc_arc)
#error "rmb_encode.m is written for ARC: compile it with -fobjc-arc"
#endif

/* rmb_device.m */
extern id<MTLDevice> rmb_device;
extern id<MTLCommandQueue> rmb_queue;
extern CAMetalLayer *rmb_layer;
extern dispatch_semaphore_t rmb_frame_sem;
extern rm_ledger_t rmb_ledger;
extern rmb_stats_t rmb_counts;
dispatch_time_t rmb_deadline(long ms);
long rmb_frame_wait_ms(void);
void rmb_watch(id<MTLCommandBuffer> cmd);
int rmb_commit_wait(id<MTLCommandBuffer> cmd);

/* rmb_resource.m */
void rmb_upload_flush(void);
id<MTLTexture> rmb_texture_for_draw(int id);
id<MTLRenderPipelineState> rmb_pipe(int i);
id<MTLSamplerState> rmb_sampler(int i);
id<MTLRenderPipelineState> rmb_pipe3d(int i);
id<MTLDepthStencilState> rmb_depth_state(int i);
id<MTLBuffer> rmb_static_buffer(int bid);

/* rmb_gbuf.m (TASK54): the passes of a MOTION or GBUFFER view and their draws' pipelines and bindings */
int rmb_gbuf_pass_kind(void);
int rmb_gbuf_reopen(void);
id<MTLRenderPipelineState> rmb_gbuf_pipe3d(int pipe);
void rmb_gbuf_bind_motion(id<MTLRenderCommandEncoder> enc, const float *mvp, const rm_viewport_t *vp);
int rmb_gbuf_bind_world(id<MTLRenderCommandEncoder> enc, int dynamic, unsigned nv);
int rmb_gbuf_bind_alias(id<MTLRenderCommandEncoder> enc, int pipe, const rm_avert_t *v, unsigned nv);
void rmb_gbuf_frame_reset(void);
void rmb_gbuf_release(void);

/* the frame ring of 2D vertices and indices (RMB_RING_BYTES): one shared buffer per frame in flight */
static id<MTLBuffer> ring_buf[RMB_FRAMES_IN_FLIGHT];
static mtl_ring_t ring;
static unsigned long frames_begun;

/*
 * Changes with every frame command buffer (a split makes a new one), and
 * when the frame ends: a texture whose last draw carries the current value
 * was drawn by the open command buffer (rmb_resource.m).
 */
unsigned long rmb_frame_serial = 1;

/* kept across frames, rebuilt when the size changes */
static id<MTLTexture> color;     /* the frame, private */
static id<MTLTexture> depth;     /* depth32Float_stencil8, private */
static id<MTLBuffer> readbuf;    /* shared, for rmb_read */
static int color_defined;        /* a pass has written color since it was made */

/* the open frame */
static int frame_open, frame_skipped, frame_passes, frame_depth;
static int frame_stencil;   /* the open pass has a stencil attachment (every stock pass with depth has) */
static int frame_keep;      /* its depth is stored whether or not a split reopens it (rmb_gbuf.m's passes) */
static id<MTLBuffer> frame_ring;
static id<MTLCommandBuffer> frame_cmd;
static id<MTLRenderCommandEncoder> frame_enc;

static void
rmb_set_texture(__strong id<MTLTexture> *slot, id<MTLTexture> t)
{
	if (*slot)
	{
		rm_ledger_destroy(&rmb_ledger, RM_OBJ_TEXTURE);
	}

	*slot = t;

	if (t)
	{
		rm_ledger_create(&rmb_ledger, RM_OBJ_TEXTURE);
	}
}

static id<MTLTexture>
rmb_new_target(MTLPixelFormat format, int w, int h, MTLTextureUsage usage)
{
	MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
			width:(NSUInteger)w height:(NSUInteger)h mipmapped:NO];

	d.storageMode = MTLStorageModePrivate;
	d.usage = usage;
	return [rmb_device newTextureWithDescriptor:d];
}

/*
 * Ends the open pass. A 3D pass's depth and stencil are opened with an
 * unknown store action and stored only when keep is set (the split that
 * reopens the 3D pass, rm_split_reopen); otherwise they are dropped, as
 * every pass did before N5.
 */
static void
rmb_end_encoder_keep(int keep)
{
	if (frame_enc)
	{
		if (frame_depth)
		{
			MTLStoreAction a = keep ? MTLStoreActionStore : MTLStoreActionDontCare;

			[frame_enc setDepthStoreAction:frame_keep ? MTLStoreActionStore : a];

			if (frame_stencil)
			{
				[frame_enc setStencilStoreAction:a];
			}
		}

		[frame_enc endEncoding];
		frame_enc = nil;
	}
}

static void
rmb_end_encoder(void)
{
	rmb_end_encoder_keep(0);
}

static void
rmb_frame_reset(void)
{
	frame_open = frame_skipped = frame_passes = frame_depth = 0;
	frame_stencil = frame_keep = 0;
	rmb_gbuf_frame_reset();
	frame_cmd = nil;
	frame_enc = nil;
	frame_ring = nil;
	rmb_frame_serial++;
}

/* Commits the frame command buffer so far, after the uploads before it. */
static void
rmb_commit_partial(void)
{
	rmb_end_encoder();
	rmb_upload_flush();
	rmb_watch(frame_cmd);
	[frame_cmd commit];
}

static int rmb_pass(const float *clear_rgba, int with_depth);

/* the stencil the next 3D pass clears to (rmb_pass_3d): gl1's glClearStencil */
static unsigned pass_stencil;

int
rmb_frame_begin(int w, int h, int dw, int dh, const float *clear_rgba)
{
	static int once_skip;

	if (frame_open)
	{
		return !frame_skipped;
	}

	if (!rmb_device || !rmb_layer || !rmb_frame_sem || (w <= 0) || (h <= 0))
	{
		return 0;
	}

	frame_open = 1;

	if (dispatch_semaphore_wait(rmb_frame_sem, rmb_deadline(rmb_frame_wait_ms())) != 0)
	{
		frame_skipped = 1;
		rmb_counts.skipped++;

		if (!once_skip)
		{
			once_skip = 1;
			fprintf(stderr, "ref_metal: the GPU still holds every frame slot after %ld ms, skipping a frame\n",
					rmb_frame_wait_ms());
		}

		return 0;
	}

	@autoreleasepool
	{
		MTLTextureUsage rt = MTLTextureUsageRenderTarget;
		rm_frame_held_t held;
		unsigned make;

		/* without the window's pixels the drawable is the frame's size */
		if ((dw <= 0) || (dh <= 0))
		{
			dw = w;
			dh = h;
		}

		held.color = color != nil;
		held.depth = depth != nil;
		held.cw = color ? (int)color.width : 0;
		held.ch = color ? (int)color.height : 0;
		held.lw = (int)rmb_layer.drawableSize.width;
		held.lh = (int)rmb_layer.drawableSize.height;
		make = rm_frame_remake(&held, w, h, dw, dh);

		/* a target that failed to allocate is tried again on the next frame */
		if (make & (RM_REMAKE_COLOR | RM_REMAKE_DEPTH))
		{
			color_defined = 0;
			rmb_set_texture(&color, rmb_new_target(MTLPixelFormatBGRA8Unorm, w, h, rt | MTLTextureUsageShaderRead));
			rmb_set_texture(&depth, rmb_new_target(MTLPixelFormatDepth32Float_Stencil8, w, h, rt));
		}

		if (make & RM_REMAKE_DRAWABLE)
		{
			rmb_layer.drawableSize = CGSizeMake(dw, dh);
		}

		frame_cmd = [rmb_queue commandBuffer];
		rmb_frame_serial++;

		/* the frame slot guarantees the frame that last used this ring buffer has completed */
		frame_ring = ring_buf[frames_begun % RMB_FRAMES_IN_FLIGHT];

		if (!frame_ring)
		{
			frame_ring = [rmb_device newBufferWithLength:RMB_RING_BYTES options:MTLResourceStorageModeShared];

			if (frame_ring)
			{
				ring_buf[frames_begun % RMB_FRAMES_IN_FLIGHT] = frame_ring;
				rm_ledger_create(&rmb_ledger, RM_OBJ_BUFFER);
			}
		}

		frames_begun++;
		ring.size = RMB_RING_BYTES;
		ring.used = 0;

		if (!color || !depth || !frame_cmd || !frame_ring)
		{
			frame_cmd = nil;
			frame_skipped = 1;
			rmb_counts.skipped++;
			dispatch_semaphore_signal(rmb_frame_sem);
			return 0;
		}
	}

	/* gl1's R_Clear: the frame's first pass, colour cleared with r_clear */
	return rmb_pass(clear_rgba, 0);
}

/*
 * A pass on the colour target, colour as rm_color_load says; depth and
 * stencil only for the 3D pass: with_depth 1 clears them (the view's
 * pass), 2 loads them (the 3D pass a split reopens).
 */
static int
rmb_pass(const float *clear_rgba, int with_depth)
{
	if (!frame_open || frame_skipped || !frame_cmd)
	{
		return 0;
	}

	/* a split inside a pass of a MOTION or GBUFFER view reopens that pass (rmb_gbuf.m) */
	if ((with_depth == 2) && rmb_gbuf_pass_kind())
	{
		return rmb_gbuf_reopen();
	}

	@autoreleasepool
	{
		MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];

		rmb_end_encoder();

		rp.colorAttachments[0].texture = color;
		rp.colorAttachments[0].storeAction = MTLStoreActionStore;

		switch (rm_color_load(clear_rgba != NULL, color_defined, frame_passes == 0))
		{
			case RM_LOAD_CLEAR:
				rp.colorAttachments[0].loadAction = MTLLoadActionClear;
				rp.colorAttachments[0].clearColor = MTLClearColorMake(clear_rgba[0], clear_rgba[1],
						clear_rgba[2], clear_rgba[3]);
				break;
			case RM_LOAD_BLACK:
				/* the frame's first pass, or a new target's undefined contents: black, as the shim's */
				rp.colorAttachments[0].loadAction = MTLLoadActionClear;
				rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
				break;
			default:
				rp.colorAttachments[0].loadAction = MTLLoadActionLoad;
				break;
		}

		if (with_depth)
		{
			MTLLoadAction load = (with_depth == 2) ? MTLLoadActionLoad : MTLLoadActionClear;

			/* the store action is chosen when the pass ends (rmb_end_encoder_keep) */
			rp.depthAttachment.texture = depth;
			rp.depthAttachment.loadAction = load;
			rp.depthAttachment.clearDepth = 1.0;
			rp.depthAttachment.storeAction = MTLStoreActionUnknown;
			rp.stencilAttachment.texture = depth;
			rp.stencilAttachment.loadAction = load;
			rp.stencilAttachment.clearStencil = pass_stencil;
			rp.stencilAttachment.storeAction = MTLStoreActionUnknown;
		}

		frame_enc = [frame_cmd renderCommandEncoderWithDescriptor:rp];

		if (!frame_enc)
		{
			return 0;
		}

		frame_passes++;
		frame_depth = with_depth != 0;
		frame_stencil = frame_depth;
		frame_keep = 0;
		color_defined = 1;
	}

	return 1;
}

int
rmb_pass_3d(int stencil)
{
	pass_stencil = (stencil > 0) ? (unsigned)(stencil & 0xff) : 0;
	return rmb_pass(NULL, 1);
}

int
rmb_pass_2d(void)
{
	return rmb_pass(NULL, 0);
}

void
rmb_target_size(int *w, int *h)
{
	*w = color ? (int)color.width : 0;
	*h = color ? (int)color.height : 0;
}

int
rmb_read(unsigned char *bgra, size_t len)
{
	int ok, own;

	/* a target no pass has written holds undefined contents: never read it */
	if (!color || !color_defined || (frame_open && (frame_skipped || !frame_cmd)))
	{
		return 0;
	}

	@autoreleasepool
	{
		NSUInteger w = color.width, h = color.height, row = w * 4, need = row * h;
		id<MTLCommandBuffer> cmd;
		id<MTLBlitCommandEncoder> blit;

		if (len < need)
		{
			return 0;
		}

		/* inside a frame: what was drawn so far (the first pass opens in rmb_frame_begin) */
		if (frame_open && (frame_passes == 0) && !rmb_pass(NULL, 0))
		{
			return 0;
		}

		rmb_end_encoder();
		rmb_upload_flush();     /* the frame reads what was written before it */
		own = !frame_open;
		cmd = own ? [rmb_queue commandBuffer] : frame_cmd;

		if (!cmd)
		{
			return 0;
		}

		if (!readbuf || readbuf.length < need)
		{
			if (readbuf)
			{
				rm_ledger_destroy(&rmb_ledger, RM_OBJ_BUFFER);
			}

			readbuf = [rmb_device newBufferWithLength:need options:MTLResourceStorageModeShared];

			if (!readbuf)
			{
				return 0;
			}

			rm_ledger_create(&rmb_ledger, RM_OBJ_BUFFER);
		}

		blit = [cmd blitCommandEncoder];
		[blit copyFromTexture:color
				sourceSlice:0
				sourceLevel:0
				sourceOrigin:MTLOriginMake(0, 0, 0)
				sourceSize:MTLSizeMake(w, h, 1)
				toBuffer:readbuf
				destinationOffset:0
				destinationBytesPerRow:row
				destinationBytesPerImage:need];
		[blit endEncoding];

		ok = rmb_commit_wait(cmd);

		if (!own)
		{
			/* the rest of the frame goes on a new command buffer; its next pass loads */
			frame_cmd = [rmb_queue commandBuffer];
			rmb_frame_serial++;
		}

		if (!ok)
		{
			/* the GPU may still write it; the command buffer keeps it alive */
			readbuf = nil;
			rm_ledger_destroy(&rmb_ledger, RM_OBJ_BUFFER);
			return 0;
		}

		memcpy(bgra, readbuf.contents, need);
	}

	rmb_counts.readbacks++;
	return 1;
}

void
rmb_frame_end(void)
{
	if (!frame_open)
	{
		return;
	}

	if (frame_skipped || !frame_cmd)
	{
		if (!frame_skipped)
		{
			dispatch_semaphore_signal(rmb_frame_sem);
		}

		rmb_frame_reset();
		return;
	}

	@autoreleasepool
	{
		dispatch_semaphore_t sem = rmb_frame_sem;
		id<CAMetalDrawable> drawable;

		if (frame_passes == 0)
		{
			rmb_pass(NULL, 0);
		}

		rmb_end_encoder();
		drawable = [rmb_layer nextDrawable];

		if (drawable)
		{
			id<MTLTexture> dst = drawable.texture;
			rm_present_t r;

			/* at gl1's place: the frame's bottom-left corner on the drawable's (FIX45) */
			if (rm_present_region((int)color.width, (int)color.height, (int)dst.width, (int)dst.height, &r))
			{
				id<MTLBlitCommandEncoder> blit;

				/* a smaller frame: the rest of the drawable black, as the shim's whole-window clear */
				if (((NSUInteger)r.w < dst.width) || ((NSUInteger)r.h < dst.height))
				{
					MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];

					rp.colorAttachments[0].texture = dst;
					rp.colorAttachments[0].loadAction = MTLLoadActionClear;
					rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
					rp.colorAttachments[0].storeAction = MTLStoreActionStore;
					[[frame_cmd renderCommandEncoderWithDescriptor:rp] endEncoding];
				}

				blit = [frame_cmd blitCommandEncoder];

				[blit copyFromTexture:color sourceSlice:0 sourceLevel:0
						sourceOrigin:MTLOriginMake(0, (NSUInteger)r.src_y, 0)
						sourceSize:MTLSizeMake((NSUInteger)r.w, (NSUInteger)r.h, 1)
						toTexture:dst destinationSlice:0 destinationLevel:0
						destinationOrigin:MTLOriginMake(0, (NSUInteger)r.dst_y, 0)];
				[blit endEncoding];
			}

			[frame_cmd presentDrawable:drawable];
		}
		else
		{
			rmb_counts.no_drawable++;
		}

		rmb_upload_flush();     /* committed ahead of the frame that reads it */
		rmb_watch(frame_cmd);   /* first: it counts before the slot is given back */
		[frame_cmd addCompletedHandler:^(id<MTLCommandBuffer> b) {
			(void)b;
			dispatch_semaphore_signal(sem);
		}];
		[frame_cmd commit];
	}

	rmb_counts.frames++;
	rmb_frame_reset();
}

/* Commits an open frame without presenting it; its slot comes back when it completes. */
void
rmb_encode_abandon(void)
{
	if (frame_open && !frame_skipped && frame_cmd)
	{
		@autoreleasepool
		{
			dispatch_semaphore_t sem = rmb_frame_sem;

			rmb_end_encoder();
			rmb_upload_flush();
			rmb_watch(frame_cmd);
			[frame_cmd addCompletedHandler:^(id<MTLCommandBuffer> b) {
				(void)b;
				dispatch_semaphore_signal(sem);
			}];
			[frame_cmd commit];
		}
	}

	rmb_frame_reset();
}

/* Releases the targets; one that is still alive afterwards is counted as unreleased. */
void
rmb_encode_release(void)
{
	__weak id<MTLTexture> wc, wd;
	__weak id<MTLBuffer> wb;
	int i;

	@autoreleasepool
	{
		wc = color;
		wd = depth;
		wb = readbuf;

		for (i = 0; i < RMB_FRAMES_IN_FLIGHT; i++)
		{
			if (ring_buf[i])
			{
				ring_buf[i] = nil;
				rm_ledger_destroy(&rmb_ledger, RM_OBJ_BUFFER);
			}
		}

		color_defined = 0;
		rmb_set_texture(&color, nil);
		rmb_set_texture(&depth, nil);
		rmb_gbuf_release();

		if (readbuf)
		{
			readbuf = nil;
			rm_ledger_destroy(&rmb_ledger, RM_OBJ_BUFFER);
		}
	}

	rmb_ledger.unreleased += (wc ? 1 : 0) + (wd ? 1 : 0) + (wb ? 1 : 0);
}

/*
 * The in-frame reuse split (section 3): the draws so far are committed
 * (after the uploads before them) and the frame goes on with a new
 * command buffer, whose next pass loads the target.
 */
void
rmb_encode_split(void)
{
	rm_reopen_t reopen;

	if (!frame_open || frame_skipped || !frame_cmd)
	{
		return;
	}

	reopen = rm_split_reopen(frame_enc != nil, frame_depth);

	@autoreleasepool
	{
		/* a 3D pass keeps its depth and stencil for the pass that goes on after the write */
		rmb_end_encoder_keep(reopen == RM_REOPEN_3D_KEEP);
		rmb_commit_partial();
		frame_cmd = [rmb_queue commandBuffer];
		rmb_frame_serial++;
	}

	rmb_counts.splits++;

	if (reopen == RM_REOPEN_3D_KEEP)
	{
		rmb_pass(NULL, 2);
	}
}

void
rmb_clear(const float *rgba)
{
	rmb_pass(rgba, 0);
}

/* one indexed draw of n vertices with count indices (rm_fan_indices or rm_quad_indices made them) */
static int
rmb_draw_2d_idx(int pipe, int tex, int smp, int sharp, const rmb_vert2d_t *v, int n,
		const unsigned short *idx, int count, const float *mvp)
{
	size_t vbytes = (size_t)(n > 0 ? n : 0) * sizeof(rmb_vert2d_t);
	long vat, iat;

	if (!frame_open || frame_skipped || !frame_cmd || !count)
	{
		rmb_counts.draw_skips++;
		return 0;
	}

	@autoreleasepool
	{
		id<MTLRenderPipelineState> ps = rmb_pipe(pipe);
		id<MTLTexture> t = tex ? rmb_texture_for_draw(tex) : nil;
		id<MTLSamplerState> ss = rmb_sampler(smp);

		int textured = rm_pipe_2d_textured(pipe);

		/* a textured pipeline without a texture is a skip, never a draw on whatever was bound */
		if (!ps || (textured && !t) || (t && !ss))
		{
			rmb_counts.draw_skips++;
			return 0;
		}

		/* the 2D pipelines have no depth: a draw after the 3D pass opens the 2D pass */
		if ((!frame_enc || frame_depth) && !rmb_pass(NULL, 0))
		{
			rmb_counts.draw_skips++;
			return 0;
		}

		vat = mtl_ring_alloc(&ring, vbytes);
		iat = vat < 0 ? -1 : mtl_ring_alloc(&ring, (size_t)count * sizeof(idx[0]));

		if (iat < 0)
		{
			rmb_counts.draw_skips++;
			return 0;
		}

		memcpy((unsigned char *)frame_ring.contents + vat, v, vbytes);
		memcpy((unsigned char *)frame_ring.contents + iat, idx, (size_t)count * sizeof(idx[0]));

		[frame_enc setRenderPipelineState:ps];
		[frame_enc setViewport:(MTLViewport){ 0, 0, (double)color.width, (double)color.height, 0, 1 }];
		[frame_enc setVertexBuffer:frame_ring offset:(NSUInteger)vat atIndex:0];
		[frame_enc setVertexBytes:mvp length:16 * sizeof(float) atIndex:1];

		{
			/* rm_params2d: x is the sharp-bilinear switch (metal_2d_sharp), the rest padding */
			const float params[4] = { sharp ? 1.0f : 0.0f, 0, 0, 0 };

			[frame_enc setFragmentBytes:params length:sizeof(params) atIndex:0];
		}

		if (t)
		{
			[frame_enc setFragmentTexture:t atIndex:0];
			[frame_enc setFragmentSamplerState:ss atIndex:0];
		}

		[frame_enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
				indexCount:(NSUInteger)count
				indexType:MTLIndexTypeUInt16
				indexBuffer:frame_ring
				indexBufferOffset:(NSUInteger)iat];
	}

	rmb_counts.draws++;
	return 1;
}


int
rmb_draw_2d(int pipe, int tex, int smp, int sharp, const rmb_vert2d_t *v, int n, const float *mvp)
{
	unsigned short idx[3 * 62];
	int count = rm_fan_indices(n, idx, (int)(sizeof(idx) / sizeof(idx[0])));

	return rmb_draw_2d_idx(pipe, tex, smp, sharp, v, n, idx, count, mvp);
}

int
rmb_draw_2d_quads(int pipe, int tex, int smp, int sharp, const rmb_vert2d_t *v, int nquads, const float *mvp)
{
	unsigned short idx[6 * RM_BATCH_QUADS];
	int count = ((nquads >= 1) && (nquads <= RM_BATCH_QUADS))
			? rm_quad_indices(nquads, idx, (int)(sizeof(idx) / sizeof(idx[0]))) : 0;

	return rmb_draw_2d_idx(pipe, tex, smp, sharp, v, count ? nquads * 4 : 0, idx, count, mvp);
}

/* ---- the 3D pass (N4) ----------------------------------------------------------- */

/* rm_shaders.metal's rm_uniforms3d: mvp and the colour */
typedef struct
{
	float mvp[16];
	float color[4];
} rmb_uniforms3d_t;

/*
 * The state of a 3D draw: the shim's mgl_layer_draw sets the depth state,
 * GL's counter-clockwise front faces and the cull mode, and the viewport
 * of mgl_compute_raster on every draw.
 */
static int
rmb_draw_3d_state(int pipe, int ds, const float *mvp, const float *rgba, const rm_viewport_t *vp, int cull)
{
	id<MTLRenderPipelineState> ps = rmb_gbuf_pipe3d(pipe);   /* the stock one outside rmb_gbuf.m's passes */
	id<MTLDepthStencilState> dss = rmb_depth_state(ds);
	rmb_uniforms3d_t u;

	/* only inside the 3D pass, whose depth the draws test */
	if (!frame_open || frame_skipped || !frame_cmd || !frame_enc || !frame_depth || !ps || !dss)
	{
		return 0;
	}

	memset(&u, 0, sizeof(u));
	memcpy(u.mvp, mvp, sizeof(u.mvp));
	memcpy(u.color, rgba, sizeof(u.color));

	[frame_enc setRenderPipelineState:ps];
	[frame_enc setDepthStencilState:dss];
	[frame_enc setFrontFacingWinding:MTLWindingCounterClockwise];
	[frame_enc setCullMode:(cull == RMB_CULL_BACK) ? MTLCullModeBack : (cull ? MTLCullModeFront : MTLCullModeNone)];
	[frame_enc setViewport:(MTLViewport){ vp->x, vp->y, vp->w, vp->h, vp->znear, vp->zfar }];
	[frame_enc setVertexBytes:&u length:sizeof(u) atIndex:1];
	rmb_gbuf_bind_motion(frame_enc, mvp, vp);   /* nothing outside rmb_gbuf.m's passes */
	return 1;
}

int
rmb_draw_world(int vbuf, const unsigned *idx, unsigned long count, int tex, int smp,
		const float *mvp, const rm_viewport_t *vp, int cull)
{
	static const float white[4] = { 1, 1, 1, 1 };
	long iat;

	if (!count || (count > RM_WORLD_MAX_INDICES))
	{
		rmb_counts.draw_skips++;
		return 0;
	}

	@autoreleasepool
	{
		id<MTLBuffer> vb = rmb_static_buffer(vbuf);
		id<MTLTexture> t = rmb_texture_for_draw(tex);
		id<MTLSamplerState> ss = rmb_sampler(smp);

		if (!vb || !t || !ss || !rmb_draw_3d_state(RM_PIPE_WORLD_TEX, RMB_DS_LEQUAL_WRITE, mvp, white, vp, cull) ||
			!rmb_gbuf_bind_world(frame_enc, 0, 0))
		{
			rmb_counts.draw_skips++;
			return 0;
		}

		iat = mtl_ring_alloc(&ring, (size_t)count * sizeof(idx[0]));

		if (iat < 0)
		{
			rmb_counts.draw_skips++;
			return 0;
		}

		memcpy((unsigned char *)frame_ring.contents + iat, idx, (size_t)count * sizeof(idx[0]));
		[frame_enc setVertexBuffer:vb offset:0 atIndex:0];
		[frame_enc setFragmentTexture:t atIndex:0];
		[frame_enc setFragmentSamplerState:ss atIndex:0];
		[frame_enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
				indexCount:(NSUInteger)count
				indexType:MTLIndexTypeUInt32
				indexBuffer:frame_ring
				indexBufferOffset:(NSUInteger)iat];
	}

	rmb_counts.draws++;
	return 1;
}

/* rm_shaders.metal's rm_uniforms_lm: gl1's lightmap unit and the slice */
typedef struct
{
	int env;
	float scale;
	unsigned slice;
	unsigned filter;
} rmb_uniforms_lm_t;

int
rmb_draw_world_lm(int vbuf, const unsigned *idx, unsigned long count, const rmb_lm_draw_t *d,
		const float *mvp, const rm_viewport_t *vp, int cull)
{
	static const float white[4] = { 1, 1, 1, 1 };
	long iat;

	if (!d || !count || (count > RM_WORLD_MAX_INDICES))
	{
		rmb_counts.draw_skips++;
		return 0;
	}

	@autoreleasepool
	{
		id<MTLBuffer> vb = rmb_static_buffer(vbuf);
		id<MTLTexture> t = rmb_texture_for_draw(d->tex);
		id<MTLTexture> lm = rmb_texture_for_draw(d->lmtex);
		id<MTLSamplerState> ss = rmb_sampler(d->smp);
		id<MTLSamplerState> ls = rmb_sampler(RMB_SAMPLER(RMB_SMP_LIGHTMAP, 0));
		rmb_uniforms_lm_t u;

		/* the slice must be one of the array's: a draw outside it is a skip */
		if (!vb || !t || !lm || !ss || !ls || (lm.textureType != MTLTextureType2DArray) || (d->slice < 0) ||
			((NSUInteger)d->slice >= lm.arrayLength) ||
			!rmb_draw_3d_state(RM_PIPE_WORLD_LM, RMB_DS_LEQUAL_WRITE, mvp, white, vp, cull) ||
			!rmb_gbuf_bind_world(frame_enc, 0, 0))
		{
			rmb_counts.draw_skips++;
			return 0;
		}

		iat = mtl_ring_alloc(&ring, (size_t)count * sizeof(idx[0]));

		if (iat < 0)
		{
			rmb_counts.draw_skips++;
			return 0;
		}

		memset(&u, 0, sizeof(u));
		u.env = d->env;
		u.scale = d->scale;
		u.slice = (unsigned)d->slice;
		u.filter = (unsigned)(d->filter == 1);

		memcpy((unsigned char *)frame_ring.contents + iat, idx, (size_t)count * sizeof(idx[0]));
		[frame_enc setVertexBuffer:vb offset:0 atIndex:0];
		[frame_enc setFragmentTexture:t atIndex:0];
		[frame_enc setFragmentSamplerState:ss atIndex:0];
		[frame_enc setFragmentTexture:lm atIndex:1];
		[frame_enc setFragmentSamplerState:ls atIndex:1];
		[frame_enc setFragmentBytes:&u length:sizeof(u) atIndex:1];
		[frame_enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
				indexCount:(NSUInteger)count
				indexType:MTLIndexTypeUInt32
				indexBuffer:frame_ring
				indexBufferOffset:(NSUInteger)iat];
	}

	rmb_counts.draws++;
	return 1;
}

int
rmb_draw_world_dyn(const rm_wvert_t *v, unsigned nv, const unsigned *idx, unsigned long ni,
		const rmb_dyn_t *d, const float *mvp, const rm_viewport_t *vp, int cull)
{
	static const float white[4] = { 1, 1, 1, 1 };
	static const int pipes[RM_DYNS] = { RM_PIPE_WORLD_TEX, RM_PIPE_WORLD_TEXC, RM_PIPE_WORLD_TEXC_BLEND,
		RM_PIPE_WORLD_LM };
	unsigned long i;
	long vat, iat;

	if (!v || !idx || !d || !nv || !ni || (nv > RM_DYN_MAX_VERTS) || (ni > RM_DYN_MAX_INDICES) ||
		(d->kind < 0) || (d->kind >= RM_DYNS))
	{
		rmb_counts.draw_skips++;
		return 0;
	}

	/* an index outside the vertices would read the rest of the ring */
	for (i = 0; i < ni; i++)
	{
		if (idx[i] >= nv)
		{
			rmb_counts.draw_skips++;
			return 0;
		}
	}

	@autoreleasepool
	{
		const int lm = (d->kind == RM_DYN_LM);
		id<MTLTexture> t = rmb_texture_for_draw(d->tex);
		id<MTLSamplerState> ss = rmb_sampler(d->smp);
		id<MTLTexture> lmt = lm ? rmb_texture_for_draw(d->lm.lmtex) : nil;
		id<MTLSamplerState> ls = lm ? rmb_sampler(RMB_SAMPLER(RMB_SMP_LIGHTMAP, 0)) : nil;
		rmb_uniforms_lm_t u;

		if (!t || !ss || (lm && (!lmt || !ls || (lmt.textureType != MTLTextureType2DArray) || (d->lm.slice < 0) ||
			((NSUInteger)d->lm.slice >= lmt.arrayLength))) ||
			!rmb_draw_3d_state(pipes[d->kind], RMB_DS_LEQUAL_WRITE, mvp, (d->kind == RM_DYN_TEXC) ||
				(d->kind == RM_DYN_TEXC_BLEND) ? d->color : white, vp, cull) ||
			!rmb_gbuf_bind_world(frame_enc, 1, nv))
		{
			rmb_counts.draw_skips++;
			return 0;
		}

		vat = mtl_ring_alloc(&ring, (size_t)nv * sizeof(*v));
		iat = vat < 0 ? -1 : mtl_ring_alloc(&ring, (size_t)ni * sizeof(idx[0]));

		if (iat < 0)
		{
			rmb_counts.draw_skips++;
			return 0;
		}

		memcpy((unsigned char *)frame_ring.contents + vat, v, (size_t)nv * sizeof(*v));
		memcpy((unsigned char *)frame_ring.contents + iat, idx, (size_t)ni * sizeof(idx[0]));
		[frame_enc setVertexBuffer:frame_ring offset:(NSUInteger)vat atIndex:0];
		[frame_enc setFragmentTexture:t atIndex:0];
		[frame_enc setFragmentSamplerState:ss atIndex:0];

		if (lm)
		{
			memset(&u, 0, sizeof(u));
			u.env = d->lm.env;
			u.scale = d->lm.scale;
			u.slice = (unsigned)d->lm.slice;
			[frame_enc setFragmentTexture:lmt atIndex:1];
			[frame_enc setFragmentSamplerState:ls atIndex:1];
			[frame_enc setFragmentBytes:&u length:sizeof(u) atIndex:1];
		}

		[frame_enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
				indexCount:(NSUInteger)ni
				indexType:MTLIndexTypeUInt32
				indexBuffer:frame_ring
				indexBufferOffset:(NSUInteger)iat];
	}

	rmb_counts.draws++;
	return 1;
}

int
rmb_draw_color(const float *xyz, int n, const float *rgba, const float *mvp,
		const rm_viewport_t *vp, int cull)
{
	unsigned short idx[3 * 62];
	float pos[64][4];
	int count = rm_fan_indices(n, idx, (int)(sizeof(idx) / sizeof(idx[0]))), i;
	size_t vbytes = (size_t)(count ? n : 0) * sizeof(pos[0]);
	long vat, iat;

	if (!count || (n > 64))
	{
		rmb_counts.draw_skips++;
		return 0;
	}

	/* the shim's vertices: (x, y, z, 1), read by the vertex function as one float4 */
	for (i = 0; i < n; i++)
	{
		pos[i][0] = xyz[i * 3];
		pos[i][1] = xyz[i * 3 + 1];
		pos[i][2] = xyz[i * 3 + 2];
		pos[i][3] = 1.0f;
	}

	@autoreleasepool
	{
		if (!rmb_draw_3d_state(RM_PIPE_COLOR_BLEND, RMB_DS_NONE, mvp, rgba, vp, cull))
		{
			rmb_counts.draw_skips++;
			return 0;
		}

		vat = mtl_ring_alloc(&ring, vbytes);
		iat = vat < 0 ? -1 : mtl_ring_alloc(&ring, (size_t)count * sizeof(idx[0]));

		if (iat < 0)
		{
			rmb_counts.draw_skips++;
			return 0;
		}

		memcpy((unsigned char *)frame_ring.contents + vat, pos, vbytes);
		memcpy((unsigned char *)frame_ring.contents + iat, idx, (size_t)count * sizeof(idx[0]));
		[frame_enc setVertexBuffer:frame_ring offset:(NSUInteger)vat atIndex:0];
		[frame_enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
				indexCount:(NSUInteger)count
				indexType:MTLIndexTypeUInt16
				indexBuffer:frame_ring
				indexBufferOffset:(NSUInteger)iat];
	}

	rmb_counts.draws++;
	return 1;
}

/*
 * One alias entity (N7): its vertices and indices go into one region of
 * the frame ring (rm_alias_region: the fixed layout RT1 refits from),
 * drawn as gl1's R_ApplyGLBuffer draws buf_alias: GL_MODULATE of the
 * vertex colour and the skin (untextured for a shell), blended with
 * RF_TRANSLUCENT, depth LEQUAL with or without writes, the cull and the
 * depth range of the entity. N8 draws its sprites (alpha-tested when
 * opaque), beams, null models, shadows (with the stencil test of
 * gl1_stencilshadow) and flash blobs (GL_ONE, GL_ONE) the same way.
 */
int
rmb_draw_alias(const rm_avert_t *v, unsigned nv, const unsigned short *idx, unsigned long ni,
		const rmb_alias_draw_t *d, const float *mvp, const rm_viewport_t *vp)
{
	static const float white[4] = { 1, 1, 1, 1 };
	unsigned long i;
	rm_region_t r;
	rm_viewport_t dvp;
	int pipe, ds;

	if (!v || !idx || !d || !vp || (nv > RM_ALIAS_MAX_VERTS) || (ni > RM_ALIAS_MAX_INDICES))
	{
		rmb_counts.draw_skips++;
		return 0;
	}

	/* an index outside the vertices would read the rest of the ring */
	for (i = 0; i < ni; i++)
	{
		if (idx[i] >= nv)
		{
			rmb_counts.draw_skips++;
			return 0;
		}
	}

	if (d->point)
	{
		/* N9: the quads of gl1's points (rm_point_quads), untextured: the caller names no texture */
		pipe = RM_PIPE_PARTICLE;
	}
	else if (d->tex)
	{
		pipe = d->blend ? RM_PIPE_ALIAS_BLEND : (d->atest ? RM_PIPE_ALIAS_ATEST : RM_PIPE_ALIAS);
	}
	else
	{
		pipe = d->add ? RM_PIPE_ALIAS_SHELL_ADD : (d->blend ? RM_PIPE_ALIAS_SHELL_BLEND : RM_PIPE_ALIAS_SHELL);
	}

	ds = d->stencil ? RMB_DS_STENCIL_SHADOW : (d->depth_write ? RMB_DS_LEQUAL_WRITE : RMB_DS_LEQUAL);
	dvp = *vp;
	dvp.znear = d->znear;
	dvp.zfar = d->zfar;

	@autoreleasepool
	{
		id<MTLTexture> t = d->tex ? rmb_texture_for_draw(d->tex) : nil;
		id<MTLSamplerState> ss = d->tex ? rmb_sampler(d->smp) : nil;

		if ((d->tex && (!t || !ss)) ||
			!rmb_draw_3d_state(pipe, ds, mvp, white, &dvp, d->cull) ||
			!rmb_gbuf_bind_alias(frame_enc, pipe, v, nv))
		{
			rmb_counts.draw_skips++;
			return 0;
		}

		if (d->stencil)
		{
			[frame_enc setStencilReferenceValue:RM_SHADOW_STENCIL_REF];   /* glStencilFunc(GL_EQUAL, 1, 2) */
		}

		if (!rm_alias_region(&ring, (long)nv, (long)ni, &r))
		{
			rmb_counts.draw_skips++;
			return 0;
		}

		memcpy((unsigned char *)frame_ring.contents + r.voff, v, r.vbytes);
		memcpy((unsigned char *)frame_ring.contents + r.ioff, idx, r.ibytes);
		[frame_enc setVertexBuffer:frame_ring offset:(NSUInteger)r.voff atIndex:0];

		if (t)
		{
			[frame_enc setFragmentTexture:t atIndex:0];
			[frame_enc setFragmentSamplerState:ss atIndex:0];
		}

		[frame_enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
				indexCount:(NSUInteger)ni
				indexType:MTLIndexTypeUInt16
				indexBuffer:frame_ring
				indexBufferOffset:(NSUInteger)r.ioff];
	}

	rmb_counts.draws++;
	return 1;
}

/*
 * For rmb_rt.m (TASK53): the open frame's command buffer with no encoder
 * open, for the compute and acceleration-structure work of design section
 * 3, step 1, which goes before the 3D pass; *frame is the frame's number
 * (its ring and semaphore slot is (frame - 1) % RMB_FRAMES_IN_FLIGHT,
 * since frames_begun counts up after the slot is picked). The pass opened next loads
 * the colour target as after any other pass, so the frame draws the same.
 * nil outside a frame or in a skipped one.
 */
/*
 * For rmb_gbuf.m (TASK54): ends the open pass and opens rp's on the frame's
 * command buffer, as rmb_pass opens the stock ones: with_depth when rp has
 * a depth attachment (its store chosen when the pass ends; keep: always
 * stored), stencil when it has a stencil one. nil outside a frame.
 */
id<MTLRenderCommandEncoder>
rmb_frame_open_pass(MTLRenderPassDescriptor *rp, int with_depth, int stencil, int keep)
{
	if (!frame_open || frame_skipped || !frame_cmd)
	{
		return nil;
	}

	rmb_end_encoder();
	frame_enc = [frame_cmd renderCommandEncoderWithDescriptor:rp];

	if (!frame_enc)
	{
		return nil;
	}

	frame_passes++;
	frame_depth = with_depth != 0;
	frame_stencil = frame_depth && stencil;
	frame_keep = frame_depth && keep;
	return frame_enc;
}

/* ends the open pass (rmb_gbuf.m) */
void
rmb_frame_close_pass(void)
{
	rmb_end_encoder();
}

/* the frame's colour target (rmb_gbuf.m), nil before the first frame */
id<MTLTexture>
rmb_frame_color(void)
{
	return color;
}

id<MTLCommandBuffer>
rmb_frame_compute(unsigned long *frame)
{
	if (!frame_open || frame_skipped || !frame_cmd)
	{
		return nil;
	}

	rmb_end_encoder();
	*frame = frames_begun;
	return frame_cmd;
}

/*
 * For rmb_rt.m: n bytes of the frame ring for the GPU to read in this
 * frame (the frame slot keeps them until the frame completes): their CPU
 * address, with the ring's buffer and the offset; NULL when the ring is
 * full or there is no frame.
 */
void *
rmb_frame_ring_take(size_t n, __strong id<MTLBuffer> *buf, size_t *ofs)
{
	long at;

	if (!frame_open || frame_skipped || !frame_ring)
	{
		return NULL;
	}

	at = mtl_ring_alloc(&ring, n);

	if (at < 0)
	{
		return NULL;
	}

	*buf = frame_ring;
	*ofs = (size_t)at;
	return (unsigned char *)frame_ring.contents + at;
}
