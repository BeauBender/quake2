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
 * ref_metal's G-buffer (RT2, TASK54; docs/plans/metal-raytracing-design.md,
 * section 3): the attachments at the render size, the pipelines of the
 * raster and of the forward pass with a motion vector, the passes of a
 * MOTION or GBUFFER view (header/rm_gbuf.h), the lighting resolve and the
 * copy of a render-size view into the frame. The stock 3D pass and its
 * pipelines (rmb_encode.m, rmb_resource.m) are not changed: a draw asks
 * rmb_gbuf_pipe3d for its pipeline, which is the stock one outside these
 * passes, and rmb_gbuf_bind_* for the G-buffer's extra bindings.
 *
 * =======================================================================
 */

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <stdio.h>
#include <string.h>

#include "header/rmb.h"
#include "header/rm_gbuf.h"

#if !__has_feature(objc_arc)
#error "rmb_gbuf.m is written for ARC: compile it with -fobjc-arc"
#endif

/* rmb_device.m */
extern id<MTLDevice> rmb_device;
extern rm_ledger_t rmb_ledger;
extern rmb_stats_t rmb_counts;

/* rmb_resource.m */
id<MTLLibrary> rmb_library(void);
id<MTLRenderPipelineState> rmb_pipe3d(int i);
id<MTLBuffer> rmb_static_buffer(int bid);

/* rmb_encode.m */
id<MTLRenderCommandEncoder> rmb_frame_open_pass(MTLRenderPassDescriptor *rp, int depth, int stencil, int keep);
void rmb_frame_close_pass(void);
id<MTLTexture> rmb_frame_color(void);
void *rmb_frame_ring_take(size_t n, __strong id<MTLBuffer> *buf, size_t *ofs);

/* the attachments, kept across frames and made again for another size or path */
static id<MTLTexture> t_depth, t_stencil, t_normal, t_material, t_motion, t_light, t_albedo, t_rcolor;
static int t_w, t_h, t_path, t_scaled;

/* the pipelines, made at the first view that needs them; failed once, never again in the session */
static id<MTLRenderPipelineState> gb_pipes[RM_PIPE_3D_COUNT], mv_pipes[RM_PIPE_3D_COUNT], resolve_pipe, blit_pipe;
static int pipes_state;   /* 0 not made, 1 made, -1 failed */

/* the view */
static int pass_kind, view_path, view_scaled, view_cleared;
static unsigned view_stencil;
static float view_clear[4];
static rm_gb_uniforms_t mv;
static int mv_set;
static int world_nbuf;
static const rm_gb_avx_t *alias_x;
static unsigned alias_nx;
static const float (*dyn_n)[4];
static unsigned dyn_nn;

static void
rmb_gbuf_tex(__strong id<MTLTexture> *slot, id<MTLTexture> t)
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

static void
rmb_gbuf_state(__strong id<MTLRenderPipelineState> *slot, id<MTLRenderPipelineState> p)
{
	if (*slot)
	{
		rm_ledger_destroy(&rmb_ledger, RM_OBJ_STATE);
	}

	*slot = p;

	if (p)
	{
		rm_ledger_create(&rmb_ledger, RM_OBJ_STATE);
		rmb_counts.pipelines++;
	}
}

static id<MTLTexture>
rmb_gbuf_new(MTLPixelFormat f, int w, int h, MTLTextureUsage usage)
{
	MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:f
			width:(NSUInteger)w height:(NSUInteger)h mipmapped:NO];

	d.storageMode = MTLStorageModePrivate;
	d.usage = usage;
	return [rmb_device newTextureWithDescriptor:d];
}

static void
rmb_gbuf_free_textures(void)
{
	rmb_gbuf_tex(&t_depth, nil);
	rmb_gbuf_tex(&t_stencil, nil);
	rmb_gbuf_tex(&t_normal, nil);
	rmb_gbuf_tex(&t_material, nil);
	rmb_gbuf_tex(&t_motion, nil);
	rmb_gbuf_tex(&t_light, nil);
	rmb_gbuf_tex(&t_albedo, nil);
	rmb_gbuf_tex(&t_rcolor, nil);
	t_w = t_h = t_path = t_scaled = 0;
}

static size_t
rmb_gbuf_alloc(id<MTLTexture> t)
{
	return t ? (size_t)t.allocatedSize : 0;
}

/* the attachments of path at w x h (and the render-size colour target when scaled); 0 when one is not made */
static int
rmb_gbuf_textures(int path, int w, int h, int scaled)
{
	const MTLTextureUsage rt = MTLTextureUsageRenderTarget, rd = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
	rm_gb_bytes_t b;
	size_t alloc;

	if ((t_w == w) && (t_h == h) && (t_path == path) && (t_scaled == scaled) && t_depth)
	{
		return 1;
	}

	rmb_gbuf_free_textures();

	rmb_gbuf_tex(&t_depth, rmb_gbuf_new(MTLPixelFormatDepth32Float, w, h, rd));
	rmb_gbuf_tex(&t_stencil, rmb_gbuf_new(MTLPixelFormatStencil8, w, h, rt));
	rmb_gbuf_tex(&t_motion, rmb_gbuf_new(MTLPixelFormatRG16Float, w, h, rd));

	if (path == RM_GB_PATH_GBUFFER)
	{
		rmb_gbuf_tex(&t_normal, rmb_gbuf_new(MTLPixelFormatRGB10A2Unorm, w, h, rd));
		rmb_gbuf_tex(&t_material, rmb_gbuf_new(MTLPixelFormatR8Uint, w, h, rd));
		rmb_gbuf_tex(&t_light, rmb_gbuf_new(MTLPixelFormatRGBA8Unorm, w, h, rd));
		rmb_gbuf_tex(&t_albedo, rmb_gbuf_new(MTLPixelFormatRGBA8Unorm, w, h, rd));
	}

	if (scaled)
	{
		rmb_gbuf_tex(&t_rcolor, rmb_gbuf_new(MTLPixelFormatBGRA8Unorm, w, h, rd));
	}

	if (!t_depth || !t_stencil || !t_motion || (scaled && !t_rcolor) ||
		((path == RM_GB_PATH_GBUFFER) && (!t_normal || !t_material || !t_light || !t_albedo)))
	{
		rmb_gbuf_free_textures();
		return 0;
	}

	t_w = w;
	t_h = h;
	t_path = path;
	t_scaled = scaled;

	/* the measure of the off state (TASK54): what the formats take and what Metal allocated */
	rm_gb_bytes(path, w, h, scaled, &b);
	alloc = rmb_gbuf_alloc(t_depth) + rmb_gbuf_alloc(t_stencil) + rmb_gbuf_alloc(t_motion) +
		rmb_gbuf_alloc(t_normal) + rmb_gbuf_alloc(t_material) + rmb_gbuf_alloc(t_light) +
		rmb_gbuf_alloc(t_albedo) + rmb_gbuf_alloc(t_rcolor);
	fprintf(stderr, "ref_metal: %s attachments %dx%d: %zu bytes by the formats, %zu allocated\n",
			(path == RM_GB_PATH_GBUFFER) ? "G-buffer" : "motion", w, h, b.total, alloc);
	return 1;
}

/* ---- the pipelines ----------------------------------------------------------------- */

static id<MTLFunction>
rmb_gbuf_fn(NSString *name, int kind, int normals, int atest, NSError **e)
{
	MTLFunctionConstantValues *fc = [MTLFunctionConstantValues new];
	bool n = normals != 0, a = atest != 0;

	[fc setConstantValue:&kind type:MTLDataTypeInt atIndex:2];
	[fc setConstantValue:&n type:MTLDataTypeBool atIndex:3];
	[fc setConstantValue:&a type:MTLDataTypeBool atIndex:4];
	return [rmb_library() newFunctionWithName:name constantValues:fc error:e];
}

/* the stock functions of a 3D pipeline, as rmb_resource.m's rmb_new_pipe_3d names them */
static void
rmb_gbuf_stock_names(int which, NSString **vs, NSString **fs)
{
	switch (which)
	{
		case RM_PIPE_WORLD_TEX:
			*vs = @"rm_vertex_world";
			*fs = @"rm_fragment_world";
			break;
		case RM_PIPE_WORLD_LM:
			*vs = @"rm_vertex_world";
			*fs = @"rm_fragment_world_lm";
			break;
		case RM_PIPE_WORLD_TEXC:
		case RM_PIPE_WORLD_TEXC_BLEND:
			*vs = @"rm_vertex_world";
			*fs = @"rm_fragment_world_color";
			break;
		case RM_PIPE_ALIAS:
		case RM_PIPE_ALIAS_BLEND:
			*vs = @"rm_vertex_alias";
			*fs = @"rm_fragment_world_color";
			break;
		case RM_PIPE_ALIAS_ATEST:
			*vs = @"rm_vertex_alias";
			*fs = @"rm_fragment_world_color_at";
			break;
		case RM_PIPE_ALIAS_SHELL:
		case RM_PIPE_ALIAS_SHELL_BLEND:
		case RM_PIPE_ALIAS_SHELL_ADD:
			*vs = @"rm_vertex_alias";
			*fs = @"rm_fragment_color";
			break;
		case RM_PIPE_PARTICLE:
			*vs = @"rm_vertex_particle";
			*fs = @"rm_fragment_particle";
			break;
		default:
			*vs = @"rm_vertex_color";
			*fs = @"rm_fragment_color";
			break;
	}
}

/* the stock blend of a pipeline on colour attachment 0 (rmb_new_pipe_3d's); 1 when it blends */
static int
rmb_gbuf_blend(int which, MTLRenderPipelineColorAttachmentDescriptor *c)
{
	if ((which == RM_PIPE_COLOR_BLEND) || (which == RM_PIPE_WORLD_TEXC_BLEND) || (which == RM_PIPE_ALIAS_BLEND) ||
		(which == RM_PIPE_ALIAS_SHELL_BLEND) || (which == RM_PIPE_PARTICLE))
	{
		c.blendingEnabled = YES;
		c.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
		c.sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
		c.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
		c.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
		return 1;
	}

	if (which == RM_PIPE_ALIAS_SHELL_ADD)
	{
		c.blendingEnabled = YES;
		c.sourceRGBBlendFactor = MTLBlendFactorOne;
		c.sourceAlphaBlendFactor = MTLBlendFactorOne;
		c.destinationRGBBlendFactor = MTLBlendFactorOne;
		c.destinationAlphaBlendFactor = MTLBlendFactorOne;
		return 1;
	}

	return 0;
}

/* the G-buffer raster's variant of an opaque pipeline: the world's three, an alias model's; nil for the rest */
static id<MTLRenderPipelineState>
rmb_gbuf_new_raster(int which, NSError **e)
{
	MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
	int kind, world = 1;

	switch (which)
	{
		case RM_PIPE_WORLD_TEX:
			kind = 0;
			break;
		case RM_PIPE_WORLD_LM:
			kind = 1;
			break;
		case RM_PIPE_WORLD_TEXC:
			kind = 2;
			break;
		case RM_PIPE_ALIAS:
			kind = 2;
			world = 0;
			break;
		default:
			return nil;
	}

	d.vertexFunction = world ? rmb_gbuf_fn(@"rm_vertex_world_gb", kind, 1, 0, e) :
		rmb_gbuf_fn(@"rm_vertex_alias_gb", kind, 0, 0, e);
	d.fragmentFunction = rmb_gbuf_fn(@"rm_fragment_gbuf", kind, world, 0, e);

	if (!d.vertexFunction || !d.fragmentFunction)
	{
		return nil;
	}

	d.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;   /* albedo */
	d.colorAttachments[1].pixelFormat = MTLPixelFormatRGB10A2Unorm; /* normal */
	d.colorAttachments[2].pixelFormat = MTLPixelFormatR8Uint;       /* material id */
	d.colorAttachments[3].pixelFormat = MTLPixelFormatRG16Float;    /* motion vector */
	d.colorAttachments[4].pixelFormat = MTLPixelFormatRGBA8Unorm;   /* light */
	d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
	return [rmb_device newRenderPipelineStateWithDescriptor:d error:e];
}

/* a forward pipeline with the motion attachment: the opaque ones write it, the blended ones leave it */
static id<MTLRenderPipelineState>
rmb_gbuf_new_motion(int which, NSError **e)
{
	MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
	int blend;

	blend = rmb_gbuf_blend(which, d.colorAttachments[0]);

	if (blend)
	{
		NSString *vs, *fs;

		rmb_gbuf_stock_names(which, &vs, &fs);
		d.vertexFunction = [rmb_library() newFunctionWithName:vs];
		d.fragmentFunction = [rmb_library() newFunctionWithName:fs];
		d.colorAttachments[1].writeMask = MTLColorWriteMaskNone;
	}
	else
	{
		int kind, world = 0, atest = 0;

		switch (which)
		{
			case RM_PIPE_WORLD_TEX:
				kind = 0;
				world = 1;
				break;
			case RM_PIPE_WORLD_LM:
				kind = 1;
				world = 1;
				break;
			case RM_PIPE_WORLD_TEXC:
				kind = 2;
				world = 1;
				break;
			case RM_PIPE_ALIAS:
				kind = 2;
				break;
			case RM_PIPE_ALIAS_ATEST:
				kind = 2;
				atest = 1;
				break;
			default:   /* RM_PIPE_ALIAS_SHELL */
				kind = 3;
				break;
		}

		d.vertexFunction = world ? rmb_gbuf_fn(@"rm_vertex_world_gb", kind, 0, 0, e) :
			rmb_gbuf_fn(@"rm_vertex_alias_gb", kind, 0, 0, e);
		d.fragmentFunction = rmb_gbuf_fn(@"rm_fragment_mv", kind, 0, atest, e);
	}

	if (!d.vertexFunction || !d.fragmentFunction)
	{
		return nil;
	}

	d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
	d.colorAttachments[1].pixelFormat = MTLPixelFormatRG16Float;
	d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
	d.stencilAttachmentPixelFormat = MTLPixelFormatStencil8;
	return [rmb_device newRenderPipelineStateWithDescriptor:d error:e];
}

static id<MTLRenderPipelineState>
rmb_gbuf_new_simple(NSString *vs, NSString *fs, NSError **e)
{
	MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];

	d.vertexFunction = [rmb_library() newFunctionWithName:vs];
	d.fragmentFunction = [rmb_library() newFunctionWithName:fs];

	if (!d.vertexFunction || !d.fragmentFunction)
	{
		return nil;
	}

	d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
	return [rmb_device newRenderPipelineStateWithDescriptor:d error:e];
}

static void
rmb_gbuf_free_pipes(void)
{
	int i;

	for (i = 0; i < RM_PIPE_3D_COUNT; i++)
	{
		rmb_gbuf_state(&gb_pipes[i], nil);
		rmb_gbuf_state(&mv_pipes[i], nil);
	}

	rmb_gbuf_state(&resolve_pipe, nil);
	rmb_gbuf_state(&blit_pipe, nil);
}

static int
rmb_gbuf_pipes(void)
{
	NSError *e = nil;
	int i;

	if (pipes_state)
	{
		return pipes_state > 0;
	}

	if (!rmb_device || !rmb_library())
	{
		return 0;
	}

	@autoreleasepool
	{
		pipes_state = -1;

		for (i = 0; i < RM_PIPE_3D_COUNT; i++)
		{
			rmb_gbuf_state(&gb_pipes[i], rmb_gbuf_new_raster(i, &e));
			rmb_gbuf_state(&mv_pipes[i], rmb_gbuf_new_motion(i, &e));

			if (!mv_pipes[i] || (!gb_pipes[i] && ((i == RM_PIPE_WORLD_TEX) || (i == RM_PIPE_WORLD_LM) ||
				(i == RM_PIPE_WORLD_TEXC) || (i == RM_PIPE_ALIAS))))
			{
				break;
			}
		}

		if (i == RM_PIPE_3D_COUNT)
		{
			rmb_gbuf_state(&resolve_pipe, rmb_gbuf_new_simple(@"rm_vertex_fullscreen", @"rm_fragment_resolve", &e));
			rmb_gbuf_state(&blit_pipe, rmb_gbuf_new_simple(@"rm_vertex_blit", @"rm_fragment_blit", &e));
		}

		if (!resolve_pipe || !blit_pipe)
		{
			fprintf(stderr, "ref_metal: the G-buffer's pipelines cannot be made: %s\n",
					e ? e.localizedDescription.UTF8String : "a function is missing");
			rmb_gbuf_free_pipes();
			return 0;
		}

		pipes_state = 1;
	}

	return 1;
}

/* ---- the passes -------------------------------------------------------------------- */

static void
rmb_gbuf_att(MTLRenderPassDescriptor *rp, int i, id<MTLTexture> t, int clear)
{
	rp.colorAttachments[i].texture = t;
	rp.colorAttachments[i].loadAction = clear ? MTLLoadActionClear : MTLLoadActionLoad;
	rp.colorAttachments[i].clearColor = MTLClearColorMake(0, 0, 0, 0);
	rp.colorAttachments[i].storeAction = MTLStoreActionStore;
}

static id<MTLTexture>
rmb_gbuf_target(void)
{
	return view_scaled ? t_rcolor : rmb_frame_color();
}

/* the view's colour target as attachment 0: loaded, or the render-size one cleared the first time */
static void
rmb_gbuf_att_target(MTLRenderPassDescriptor *rp)
{
	rmb_gbuf_att(rp, 0, rmb_gbuf_target(), 0);

	if (view_scaled && !view_cleared)
	{
		rp.colorAttachments[0].loadAction = MTLLoadActionClear;
		rp.colorAttachments[0].clearColor = MTLClearColorMake(view_clear[0], view_clear[1], view_clear[2],
				view_clear[3]);
		view_cleared = 1;
	}
}

/* depth (and stencil) cleared or loaded; their store is chosen when the pass ends, the depth always kept */
static void
rmb_gbuf_att_depth(MTLRenderPassDescriptor *rp, int clear, int stencil, int stencil_clear)
{
	rp.depthAttachment.texture = t_depth;
	rp.depthAttachment.loadAction = clear ? MTLLoadActionClear : MTLLoadActionLoad;
	rp.depthAttachment.clearDepth = 1.0;
	rp.depthAttachment.storeAction = MTLStoreActionUnknown;

	if (stencil)
	{
		rp.stencilAttachment.texture = t_stencil;
		rp.stencilAttachment.loadAction = stencil_clear ? MTLLoadActionClear : MTLLoadActionLoad;
		rp.stencilAttachment.clearStencil = view_stencil;
		rp.stencilAttachment.storeAction = MTLStoreActionUnknown;
	}
}

/* the raster: the five colour attachments and the depth, cleared (load 0: a split reopens it loaded) */
static int
rmb_gbuf_open_raster(int clear)
{
	MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];

	rmb_gbuf_att(rp, 0, t_albedo, clear);
	rmb_gbuf_att(rp, 1, t_normal, clear);
	rmb_gbuf_att(rp, 2, t_material, clear);
	rmb_gbuf_att(rp, 3, t_motion, clear);
	rmb_gbuf_att(rp, 4, t_light, clear);
	rmb_gbuf_att_depth(rp, clear, 0, 0);

	if (!rmb_frame_open_pass(rp, 1, 0, 1))
	{
		return 0;
	}

	pass_kind = RMB_GB_PASS_RASTER;
	return 1;
}

/* a forward pass with the motion attachment: MOTION's first (clear), GBUFFER's after the resolve, a split's reopening */
static int
rmb_gbuf_open_motion(int clear_motion, int clear_depth, int clear_stencil)
{
	MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];

	rmb_gbuf_att_target(rp);
	rmb_gbuf_att(rp, 1, t_motion, clear_motion);
	rmb_gbuf_att_depth(rp, clear_depth, 1, clear_stencil);

	if (!rmb_frame_open_pass(rp, 1, 1, 1))
	{
		return 0;
	}

	pass_kind = RMB_GB_PASS_MOTION;
	return 1;
}

int
rmb_gbuf_view_begin(int path, int rw, int rh, int stencil, const float *clear_rgba)
{
	id<MTLTexture> color = rmb_frame_color();
	int ok;

	pass_kind = RMB_GB_PASS_NONE;
	mv_set = 0;
	alias_x = NULL;
	dyn_n = NULL;

	if (!color || ((path != RM_GB_PATH_MOTION) && (path != RM_GB_PATH_GBUFFER)) || (rw < 1) || (rh < 1) ||
		!rmb_gbuf_pipes())
	{
		return 0;
	}

	view_path = path;
	view_scaled = ((NSUInteger)rw != color.width) || ((NSUInteger)rh != color.height);
	view_cleared = 0;
	view_stencil = (stencil > 0) ? (unsigned)(stencil & 0xff) : 0;

	if (clear_rgba)
	{
		memcpy(view_clear, clear_rgba, sizeof(view_clear));
	}
	else
	{
		view_clear[0] = view_clear[1] = view_clear[2] = 0;
		view_clear[3] = 1;
	}

	@autoreleasepool
	{
		if (!rmb_gbuf_textures(path, rw, rh, view_scaled))
		{
			return 0;
		}

		ok = (path == RM_GB_PATH_GBUFFER) ? rmb_gbuf_open_raster(1) : rmb_gbuf_open_motion(1, 1, 1);
	}

	return ok;
}

/* a split (rmb_encode_split) reopens the pass that was open, every attachment loaded */
int
rmb_gbuf_reopen(void)
{
	int ok = 0;

	@autoreleasepool
	{
		if (pass_kind == RMB_GB_PASS_RASTER)
		{
			ok = rmb_gbuf_open_raster(0);
		}
		else if (pass_kind == RMB_GB_PASS_MOTION)
		{
			ok = rmb_gbuf_open_motion(0, 0, 0);
		}
	}

	return ok;
}

int
rmb_gbuf_resolve(const rm_gb_resolve_t *r)
{
	if (pass_kind != RMB_GB_PASS_RASTER)
	{
		return 0;
	}

	@autoreleasepool
	{
		MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
		id<MTLRenderCommandEncoder> enc;

		rmb_gbuf_att_target(rp);
		enc = rmb_frame_open_pass(rp, 0, 0, 0);

		if (!enc)
		{
			pass_kind = RMB_GB_PASS_NONE;
			return 0;
		}

		pass_kind = RMB_GB_PASS_OTHER;
		[enc setRenderPipelineState:resolve_pipe];
		[enc setFragmentBytes:r length:sizeof(*r) atIndex:0];
		[enc setFragmentTexture:t_albedo atIndex:0];
		[enc setFragmentTexture:t_light atIndex:1];
		[enc setFragmentTexture:t_material atIndex:2];
		[enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
		rmb_counts.draws++;

		/* the forward pass over the resolved image: its depth and motion kept, the stencil cleared as the stock pass's */
		return rmb_gbuf_open_motion(0, 0, 1);
	}
}

void
rmb_gbuf_view_end(const rm_viewport_t *src, const rm_viewport_t *dst)
{
	id<MTLTexture> color = rmb_frame_color();

	if (pass_kind == RMB_GB_PASS_NONE)
	{
		return;
	}

	pass_kind = RMB_GB_PASS_NONE;
	mv_set = 0;
	rmb_frame_close_pass();

	if (!view_scaled || !t_rcolor || !color || !src || !dst)
	{
		return;
	}

	@autoreleasepool
	{
		MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
		id<MTLRenderCommandEncoder> enc;
		const double fw = (double)color.width, fh = (double)color.height;
		const double rw = (double)t_rcolor.width, rh = (double)t_rcolor.height;
		float u[8];

		rmb_gbuf_att(rp, 0, color, 0);
		enc = rmb_frame_open_pass(rp, 0, 0, 0);

		if (!enc)
		{
			return;
		}

		/* dst in clip coordinates, top edge first; src in texture coordinates, top edge first */
		u[0] = (float)(dst->x / fw * 2.0 - 1.0);
		u[1] = (float)(1.0 - dst->y / fh * 2.0);
		u[2] = (float)((dst->x + dst->w) / fw * 2.0 - 1.0);
		u[3] = (float)(1.0 - (dst->y + dst->h) / fh * 2.0);
		u[4] = (float)(src->x / rw);
		u[5] = (float)(src->y / rh);
		u[6] = (float)((src->x + src->w) / rw);
		u[7] = (float)((src->y + src->h) / rh);

		[enc setRenderPipelineState:blit_pipe];
		[enc setViewport:(MTLViewport){ 0, 0, fw, fh, 0, 1 }];
		[enc setVertexBytes:u length:sizeof(u) atIndex:0];
		[enc setFragmentTexture:t_rcolor atIndex:0];
		[enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
		rmb_counts.draws++;
	}
}

/* ---- what the draws of rmb_encode.m ask -------------------------------------------- */

int
rmb_gbuf_pass_kind(void)
{
	return pass_kind;
}

/* the pipeline of a 3D draw in the open pass: the stock one outside the G-buffer's passes, nil where none fits */
id<MTLRenderPipelineState>
rmb_gbuf_pipe3d(int pipe)
{
	if ((pipe < 0) || (pipe >= RM_PIPE_3D_COUNT))
	{
		return nil;
	}

	switch (pass_kind)
	{
		case RMB_GB_PASS_RASTER:
			return gb_pipes[pipe];
		case RMB_GB_PASS_MOTION:
			return mv_pipes[pipe];
		case RMB_GB_PASS_OTHER:
			return nil;
		default:
			return rmb_pipe3d(pipe);
	}
}

void
rmb_gbuf_set_motion(const rm_gb_uniforms_t *u)
{
	if (u)
	{
		mv = *u;
		mv_set = 1;
	}
	else
	{
		mv_set = 0;
	}
}

void
rmb_gbuf_set_material(unsigned material)
{
	mv.material = material;
}

void
rmb_gbuf_set_alias_extra(const rm_gb_avx_t *x, unsigned n)
{
	alias_x = x;
	alias_nx = n;
}

void
rmb_gbuf_set_dyn_normals(const float (*n)[4], unsigned nv)
{
	dyn_n = n;
	dyn_nn = nv;
}

void
rmb_gbuf_set_world_normals(int nbuf)
{
	world_nbuf = nbuf;
}

/* the motion uniforms at vertex and fragment buffer 3: the set ones, or the draw's matrix twice (no motion) */
void
rmb_gbuf_bind_motion(id<MTLRenderCommandEncoder> enc, const float *mvp, const rm_viewport_t *vp)
{
	rm_gb_uniforms_t u;

	if ((pass_kind != RMB_GB_PASS_RASTER) && (pass_kind != RMB_GB_PASS_MOTION))
	{
		return;
	}

	if (mv_set)
	{
		u = mv;
	}
	else
	{
		memset(&u, 0, sizeof(u));
		memcpy(u.cur, mvp, sizeof(u.cur));
		memcpy(u.prev, mvp, sizeof(u.prev));
		u.nrm[0] = u.nrm[5] = u.nrm[10] = u.nrm[15] = 1;
	}

	u.half_vp[0] = (float)(vp->w * 0.5);
	u.half_vp[1] = (float)(vp->h * 0.5);
	[enc setVertexBytes:&u length:sizeof(u) atIndex:3];
	[enc setFragmentBytes:&u length:sizeof(u) atIndex:3];
}

/* n bytes of the frame ring at vertex buffer 2, filled by the caller; 0 when the ring is full */
static void *
rmb_gbuf_ring2(id<MTLRenderCommandEncoder> enc, size_t n)
{
	__strong id<MTLBuffer> buf = nil;
	size_t ofs = 0;
	void *p = rmb_frame_ring_take(n, &buf, &ofs);

	if (p)
	{
		[enc setVertexBuffer:buf offset:ofs atIndex:2];
	}

	return p;
}

/* a world draw in the raster: its normals at buffer 2 (the static ones, or a dynamic draw's nv); 0 when it cannot be made */
int
rmb_gbuf_bind_world(id<MTLRenderCommandEncoder> enc, int dynamic, unsigned nv)
{
	if (pass_kind != RMB_GB_PASS_RASTER)
	{
		return 1;
	}

	if (!dynamic)
	{
		id<MTLBuffer> nb = world_nbuf ? rmb_static_buffer(world_nbuf) : nil;

		if (!nb)
		{
			return 0;
		}

		[enc setVertexBuffer:nb offset:0 atIndex:2];
		return 1;
	}

	{
		float (*n)[4] = rmb_gbuf_ring2(enc, (size_t)nv * sizeof(float[4]));

		if (!n)
		{
			dyn_n = NULL;
			return 0;
		}

		if (dyn_n && (dyn_nn == nv))
		{
			memcpy(n, dyn_n, (size_t)nv * sizeof(float[4]));
		}
		else
		{
			memset(n, 0, (size_t)nv * sizeof(float[4]));
		}
	}

	dyn_n = NULL;
	return 1;
}

/*
 * An alias draw whose vertex function is rm_vertex_alias_gb (the raster's,
 * and the opaque ones of a motion pass): the per-vertex data at buffer 2,
 * the set data when it has nv entries, else each vertex's own position
 * (no motion of its own) and no normal. 0 when the ring is full.
 */
int
rmb_gbuf_bind_alias(id<MTLRenderCommandEncoder> enc, int pipe, const rm_avert_t *v, unsigned nv)
{
	rm_gb_avx_t *x;
	unsigned i;
	const int opaque = (pipe == RM_PIPE_ALIAS) || (pipe == RM_PIPE_ALIAS_ATEST) || (pipe == RM_PIPE_ALIAS_SHELL);

	if (!((pass_kind == RMB_GB_PASS_RASTER) || ((pass_kind == RMB_GB_PASS_MOTION) && opaque)))
	{
		alias_x = NULL;
		return 1;
	}

	x = rmb_gbuf_ring2(enc, (size_t)(nv ? nv : 1) * sizeof(rm_gb_avx_t));

	if (!x)
	{
		alias_x = NULL;
		return 0;
	}

	if (alias_x && (alias_nx == nv))
	{
		memcpy(x, alias_x, (size_t)nv * sizeof(rm_gb_avx_t));
	}
	else
	{
		for (i = 0; i < nv; i++)
		{
			memcpy(x[i].prev, v[i].pos, sizeof(x[i].prev));
			memset(x[i].normal, 0, sizeof(x[i].normal));
		}
	}

	alias_x = NULL;
	return 1;
}

/* the frame ended: no pass of a view is open */
void
rmb_gbuf_frame_reset(void)
{
	pass_kind = RMB_GB_PASS_NONE;
	mv_set = 0;
	alias_x = NULL;
	dyn_n = NULL;
}

/* the attachments and pipelines, released with the frame's targets (rmb_encode_release) */
void
rmb_gbuf_release(void)
{
	@autoreleasepool
	{
		rmb_gbuf_free_textures();
		rmb_gbuf_free_pipes();
		pipes_state = 0;
	}

	rmb_gbuf_frame_reset();
}

int
rmb_gbuf_size(int *w, int *h)
{
	*w = t_w;
	*h = t_h;
	return t_depth != nil;
}

id<MTLTexture>
rmb_gbuf_depth(void)
{
	return t_depth;
}

id<MTLTexture>
rmb_gbuf_motion(void)
{
	return t_motion;
}
