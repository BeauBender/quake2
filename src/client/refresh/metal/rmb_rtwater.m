/*
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
 * The Metal back end of ref_metal's ray-traced water reflections (RT6,
 * TASK58; header/rm_rtwater.h): the kernels', the blend's, the
 * development view's, the water marks' and the forward pass's translucent
 * pipelines, the kernel's outputs and the pixel list at the render size,
 * the passes on the frame's command buffer after the lighting resolve
 * (rmb_gbuf_resolve's hook) over the frame's instance structure
 * (rmb_rt_bind_scene), the map's per-triangle materials
 * (rmb_rt_bind_materials) and its vertex and index buffers
 * (rmb_rt_bind_geometry), the blend over the opaque water, the
 * translucent faces' draw of the forward pass, and the counts and GPU
 * time of each frame's passes for the r_rt_stats 2 line. TASK158 adds the
 * map's water triangles' draw (rmb_rt_bind_water_raster; TASK162: the marks),
 * the compaction and the indirect trace over its list, and the world-space
 * shading's bindings: the triangles' shading records, the frame's texture
 * table (an argument buffer of the world's images and the sky box's six),
 * the lightmap array and the dynamic lights the lightmaps left out. Every
 * object is counted in the ledger as RM_OBJ_RT. TASK162 moves the
 * candidates' raster into the G-buffer raster (rmb_rtw_mark, its last draw:
 * a mark in the normal attachment's alpha), so the world-space path has no
 * render pass of its own.
 *
 * =======================================================================
 */

#import <Metal/Metal.h>

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "header/rm_gbuf.h"
#include "header/rm_rt.h"
#include "header/rm_rtwater.h"
#include "header/rmb_internal.h"
#include "common/mtl_rtw_math.h"
#include "common/mtl_rtd_math.h"
#include "common/mtl_rtup_math.h"

#if !__has_feature(objc_arc)
#error "rmb_rtwater.m is written for ARC: compile it with -fobjc-arc"
#endif

/* rmb_device.m */
extern id<MTLDevice> rmb_device;
extern rm_ledger_t rmb_ledger;
extern rmb_stats_t rmb_counts;

#define RMB_RTW_GROUP 8         /* the compaction's threadgroup side */
#define RMB_RTW_SLOT 16         /* uint32s a frame slot of the counts buffer (rm_rtwater.metal's RM_RTW_C_*) */
#define RMB_RTW_C_FALLBACK 8    /* TASK126 slice 5: the upsample's fallbacks (rm_rtw_up) */
#define RMB_RTW_C_LISTED 9
#define RMB_RTW_C_ARGS 10
#define RMB_RTW_SAMPLES 4       /* per slot (world space): 0 and 1 the compaction's start and end, 2 and 3 the trace's
                                   (TASK58's path writes 0 and 1); FIX98: the 4 TASK158's raster had are gone */

/* rm_rtwater.metal's rm_rtw_lo (TASK126 slice 5): the view's divisor, beside the view, which keeps its 288 bytes */
typedef struct
{
	uint32_t div;
	uint32_t pad[3];
} rmb_rtw_lo_t;

_Static_assert(sizeof(rm_rtw_view_t) == 288, "the view is rm_rtwater.metal's 288 bytes");
_Static_assert(sizeof(rmb_rtw_lo_t) == 16, "the divisor's block is rm_rtwater.metal's 16 bytes");
_Static_assert(RMB_RTW_C_ARGS * 4 % 4 == 0, "the indirect arguments are 4-byte aligned");

static int rtw_open;
static id<MTLComputePipelineState> trace_pipe;   /* TASK58's kernel over every pixel (r_rt_water_hits 0) */
static id<MTLComputePipelineState> trace_ws_pipe, compact_pipe, args_pipe;   /* TASK158's, over the list */
static int made_ws;                     /* the outputs were made for the world-space path (rmb_rtw_prepare) */
static int ws_ok;                       /* the world-space path's pipelines and bindings were built (rmb_rtw_ws_ok) */
static char ws_err[192];                /* why not */
static id<MTLRenderPipelineState> blend_pipe, show_pipe, trans_pipe;
static id<MTLRenderPipelineState> mark_pipe[2];   /* TASK162: the marks in the G-buffer raster, without and with its surface */
static id<MTLDepthStencilState> mark_depth;
static id<MTLTexture> t_refl;           /* the reflection and the class, RGBA8Unorm at the render size */
static id<MTLTexture> t_wdist;          /* the translucent water's distance from the eye, R32Float */
static id<MTLBuffer> list;              /* the compaction's pixels, one uint32 a pixel of the render size */
static unsigned long list_cap;
static id<MTLBuffer> counts;            /* RMB_FRAMES_IN_FLIGHT slots of RMB_RTW_SLOT */
static id<MTLCounterSampleBuffer> samples;
static float turb[MTL_RTW_TURB_SIZE];   /* rm_rtw_turb_table */

/* TASK158: the world-space shading's bindings */
static id<MTLArgumentEncoder> tex_enc;  /* of rm_rtw_trace's texture table (buffer 12) */
static id<MTLBuffer> tex_ab;            /* RMB_FRAMES_IN_FLIGHT tables of tex_len bytes */
static NSUInteger tex_len;
static id<MTLTexture> white2d, white_arr;   /* where an image or the lightmap array is missing */
static id<MTLBuffer> shade;             /* the map's triangles' shading records (rmb_rtw_set_shade) */
static unsigned long shade_n;
static int tex_tids[MTL_RTW_TEX_WORLD], tex_n, lm_tid, sky_tids[6], sky_set;
static float lights[MTL_RTS_MAX_LIGHTS * 8];
static int nlights;

/* per slot: the frame whose pass completed (set by its handler), the frame whose counts are awaited */
static atomic_ulong slot_done[RMB_FRAMES_IN_FLIGHT];
static int slot_pending[RMB_FRAMES_IN_FLIGHT];
static int slot_timed[RMB_FRAMES_IN_FLIGHT];
static int slot_ws[RMB_FRAMES_IN_FLIGHT];      /* TASK158: the slot's frame took the world-space path */
static unsigned long slot_frame[RMB_FRAMES_IN_FLIGHT], slot_pixels[RMB_FRAMES_IN_FLIGHT];
static rm_rtw_stats_t done[RMB_FRAMES_IN_FLIGHT];
static int ndone;

/* the view the next G-buffer view traces with; whether this one traced (for the blend, then the forward pass) */
static rm_rtw_view_t view;
static int view_set, traced, forward, shown;   /* shown: this view's r_rt_water_show view was drawn */
static int marked;                      /* TASK162: this view's raster drew the water's marks (rmb_rtw_mark) */

/*
 * TASK59 slice 2b: the reflections' 3 x 3 blur (rm_rtwdenoise.metal),
 * while r_rt_temporal is above 0 (rmb_rtw_set_blur). Its pipeline and
 * output are made on the first view that blurs and released when one
 * does not, so at r_rt_temporal 0 nothing of it exists.
 */
static id<MTLComputePipelineState> blur_pipe;
static id<MTLTexture> t_rblur;          /* the blurred reflection and the class, as t_refl */
static int blur_on;                     /* the next view blurs */
static int blur_bad;                    /* its pipeline or output could not be made: latched until the size changes or the pass closes */
static NSUInteger blur_bad_w, blur_bad_h; /* the render size blur_bad was latched at */
static int blurred;                     /* this view's reflection was blurred into t_rblur */

/*
 * TASK126 slice 5: r_rt_water_scale below 1. The divisor rmb_rtw_prepare
 * was last given; the traced size's reflection, distance and (while the
 * blur is on) blurred reflection, made by it and released at divisor 1;
 * the five pipelines of rm_rtwater_lo.metal, made at the first such view,
 * a failure latched until the pass is closed. The stock outputs stay at
 * the render size: rm_rtw_up writes them.
 */
static unsigned prep_div = 1;
static id<MTLTexture> t_refl_lo, t_wdist_lo, t_rblur_lo;
static id<MTLComputePipelineState> trace_lo_pipe, trace_ws_lo_pipe, compact_lo_pipe, blur_lo_pipe, up_pipe;
static int lo_refused;
static unsigned slot_div[RMB_FRAMES_IN_FLIGHT];                 /* the view's divisor */
static unsigned long slot_tw[RMB_FRAMES_IN_FLIGHT], slot_th[RMB_FRAMES_IN_FLIGHT];  /* its traced size */

/* Sets a strong slot to obj (nil releases it), counting both in the ledger as RM_OBJ_RT. */
#define RMB_RTW_SET(slot, obj) \
	do \
	{ \
		if (slot) \
		{ \
			rm_ledger_destroy(&rmb_ledger, RM_OBJ_RT); \
		} \
		(slot) = (obj); \
		if (slot) \
		{ \
			rm_ledger_create(&rmb_ledger, RM_OBJ_RT); \
		} \
	} while (0)

/* a full-screen pipeline over the target alone (BGRA8Unorm, as the resolve's), blended by the source alpha or not */
static id<MTLRenderPipelineState>
rmb_rtw_fullscreen(NSString *fs, int blend, NSError **e)
{
	MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];

	d.vertexFunction = [rmb_library() newFunctionWithName:@"rm_vertex_fullscreen"];
	d.fragmentFunction = [rmb_library() newFunctionWithName:fs];

	if (!d.vertexFunction || !d.fragmentFunction)
	{
		return nil;
	}

	d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

	if (blend)
	{
		/* the target's RGB mixed toward the reflection by the weight; its alpha not written */
		d.colorAttachments[0].blendingEnabled = YES;
		d.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
		d.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
		d.colorAttachments[0].writeMask = MTLColorWriteMaskRed | MTLColorWriteMaskGreen | MTLColorWriteMaskBlue;
	}

	return rmb_new_render_pipe(d, e);
}

/*
 * The forward pass's translucent faces (rmb_gbuf.m's motion variant of
 * RM_PIPE_WORLD_TEXC_BLEND): the stock vertex function and blend, the
 * motion attachment not written, with rm_fragment_rtw_trans's colour.
 */
static id<MTLRenderPipelineState>
rmb_rtw_trans_pipe(NSError **e)
{
	MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];

	d.vertexFunction = [rmb_library() newFunctionWithName:@"rm_vertex_world"];
	d.fragmentFunction = [rmb_library() newFunctionWithName:@"rm_fragment_rtw_trans"];

	if (!d.vertexFunction || !d.fragmentFunction)
	{
		return nil;
	}

	d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
	d.colorAttachments[0].blendingEnabled = YES;
	d.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
	d.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
	d.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
	d.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
	d.colorAttachments[1].pixelFormat = MTLPixelFormatRG16Float;
	d.colorAttachments[1].writeMask = MTLColorWriteMaskNone;
	d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
	d.stencilAttachmentPixelFormat = MTLPixelFormatStencil8;
	return rmb_new_render_pipe(d, e);
}

/* FIX98: the MTLBlendFactor of one of common/mtl_rtw_math.h's MTL_RTW_BF_ values */
_Static_assert(MTL_RTW_BF_COUNT == 3, "a new MTL_RTW_BF_ value needs a case in rmb_rtw_factor");
_Static_assert(MTL_RTW_MARK_SRC_FACTOR >= 0 && MTL_RTW_MARK_SRC_FACTOR < MTL_RTW_BF_COUNT, "the mark's source factor is not a MTL_RTW_BF_ value");
_Static_assert(MTL_RTW_MARK_DST_FACTOR >= 0 && MTL_RTW_MARK_DST_FACTOR < MTL_RTW_BF_COUNT, "the mark's destination factor is not a MTL_RTW_BF_ value");

static MTLBlendFactor
rmb_rtw_factor(int f)
{
	switch (f)
	{
		case MTL_RTW_BF_ONE:
			return MTLBlendFactorOne;
		case MTL_RTW_BF_SRC_ALPHA:
			return MTLBlendFactorSourceAlpha;
		case MTL_RTW_BF_ZERO:
			return MTLBlendFactorZero;
		default:
			/* unreachable while the _Static_assert holds; never silent */
			NSLog(@"ref_metal: unknown water mark blend factor %d, using Zero", f);
			return MTLBlendFactorZero;
	}
}

/*
 * TASK162: the water's marks, a draw of the G-buffer raster (its
 * attachments, rmb_gbuf_raster_formats, with the surface attachment or
 * not), depth-tested by mark_depth as gl1 tests (less or equal) and not
 * written. Only the normal attachment's alpha is written, blended to
 * MTL_RTW_MARK_A + MTL_RTW_MARK_A x its alpha (mtl_rtw_mark_blend: the
 * source alpha is MTL_RTW_MARK_A, the destination's factor the source's
 * alpha), so its "has a normal" (above 0.5) is kept; every other
 * attachment is left as the raster wrote it.
 */
static id<MTLRenderPipelineState>
rmb_rtw_mark_pipe(int surf, NSError **e)
{
	MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
	int i;

	d.vertexFunction = [rmb_library() newFunctionWithName:@"rm_vertex_rtw_cand"];
	d.fragmentFunction = [rmb_library() newFunctionWithName:@"rm_fragment_rtw_mark"];

	if (!d.vertexFunction || !d.fragmentFunction)
	{
		return nil;
	}

	rmb_gbuf_raster_formats(d, surf);

	for (i = 0; i < 6; i++)
	{
		d.colorAttachments[i].writeMask = MTLColorWriteMaskNone;
	}

	d.colorAttachments[1].writeMask = MTLColorWriteMaskAlpha;
	d.colorAttachments[1].blendingEnabled = YES;
	d.colorAttachments[1].rgbBlendOperation = MTLBlendOperationAdd;
	d.colorAttachments[1].sourceRGBBlendFactor = MTLBlendFactorZero;
	d.colorAttachments[1].destinationRGBBlendFactor = MTLBlendFactorOne;
	d.colorAttachments[1].alphaBlendOperation = MTLBlendOperationAdd;
	d.colorAttachments[1].sourceAlphaBlendFactor = rmb_rtw_factor(MTL_RTW_MARK_SRC_FACTOR);
	d.colorAttachments[1].destinationAlphaBlendFactor = rmb_rtw_factor(MTL_RTW_MARK_DST_FACTOR);
	return rmb_new_render_pipe(d, e);
}

/* a 1 x 1 white texture, 2D or a 2D array of one slice: what the table holds where an image is missing */
static id<MTLTexture>
rmb_rtw_white(int array)
{
	const uint32_t px = 0xffffffffu;
	MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
			width:1 height:1 mipmapped:NO];
	id<MTLTexture> t;

	d.usage = MTLTextureUsageShaderRead;
	d.storageMode = MTLStorageModeShared;

	if (array)
	{
		d.textureType = MTLTextureType2DArray;
		d.arrayLength = 1;
	}

	t = [rmb_device newTextureWithDescriptor:d];
	[t replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 slice:0 withBytes:&px bytesPerRow:4 bytesPerImage:4];
	return t;
}

/* TASK158: releases the world-space path's pipelines and bindings (TASK58's path needs none of them) */
static void
rmb_rtw_close_ws(void)
{
	RMB_RTW_SET(trace_ws_pipe, nil);
	RMB_RTW_SET(compact_pipe, nil);
	RMB_RTW_SET(args_pipe, nil);
	RMB_RTW_SET(mark_pipe[0], nil);
	RMB_RTW_SET(mark_pipe[1], nil);
	RMB_RTW_SET(mark_depth, nil);
	RMB_RTW_SET(tex_enc, nil);
	RMB_RTW_SET(tex_ab, nil);
	RMB_RTW_SET(white2d, nil);
	RMB_RTW_SET(white_arr, nil);
	tex_len = 0;
	ws_ok = 0;
}

/*
 * TASK158: the world-space path's pipelines and bindings from its three
 * kernels (r_rt_water_hits 1): 1, or 0 with err set and none of them
 * kept, the pass left open for TASK58's path.
 */
static int
rmb_rtw_open_ws(id<MTLFunction> kw, id<MTLFunction> kc, id<MTLFunction> ka, char *err, size_t errlen)
{
	MTLDepthStencilDescriptor *dd = [MTLDepthStencilDescriptor new];
	NSError *e = nil;

	if (!kw || !kc || !ka)
	{
		snprintf(err, errlen, "the shader library has no rm_rtw_trace_ws, rm_rtw_compact or rm_rtw_args");
		return 0;
	}

	dd.depthCompareFunction = MTLCompareFunctionLessEqual;
	dd.depthWriteEnabled = NO;

	RMB_RTW_SET(trace_ws_pipe, [rmb_device newComputePipelineStateWithFunction:kw error:&e]);
	RMB_RTW_SET(compact_pipe, trace_ws_pipe ? [rmb_device newComputePipelineStateWithFunction:kc error:&e] : nil);
	RMB_RTW_SET(args_pipe, compact_pipe ? [rmb_device newComputePipelineStateWithFunction:ka error:&e] : nil);
	RMB_RTW_SET(mark_pipe[0], args_pipe ? rmb_rtw_mark_pipe(0, &e) : nil);
	RMB_RTW_SET(mark_pipe[1], mark_pipe[0] ? rmb_rtw_mark_pipe(1, &e) : nil);
	RMB_RTW_SET(mark_depth, [rmb_device newDepthStencilStateWithDescriptor:dd]);
	RMB_RTW_SET(tex_enc, [kw newArgumentEncoderWithBufferIndex:12]);
	RMB_RTW_SET(white2d, rmb_rtw_white(0));
	RMB_RTW_SET(white_arr, rmb_rtw_white(1));

	if (tex_enc)
	{
		const NSUInteger a = tex_enc.alignment ? tex_enc.alignment : 256;

		tex_len = (tex_enc.encodedLength + a - 1) / a * a;
		RMB_RTW_SET(tex_ab, [rmb_device newBufferWithLength:RMB_FRAMES_IN_FLIGHT * tex_len
				options:MTLResourceStorageModeShared]);
	}

	if (!trace_ws_pipe || !compact_pipe || !args_pipe || !mark_pipe[0] || !mark_pipe[1] || !mark_depth || !tex_enc ||
		!tex_ab || !white2d || !white_arr)
	{
		snprintf(err, errlen, "cannot build the world-space hits' pipelines%s%s", e ? ": " : "",
				e ? e.localizedDescription.UTF8String : "");
		rmb_rtw_close_ws();
		return 0;
	}

	return 1;
}

int
rmb_rtw_ws_ok(char *err, size_t errlen)
{
	if (rtw_open && ws_ok)
	{
		return 1;
	}

	snprintf(err, errlen, "%s", !rtw_open ? "the pass is not open" : ws_err);
	return 0;
}

int
rmb_rtw_open(char *err, size_t errlen)
{
	if (rtw_open)
	{
		return 1;
	}

	if (!rmb_device || !rmb_device.supportsRaytracing || !rmb_library())
	{
		snprintf(err, errlen, "%s", !rmb_device ? "no Metal device" :
				!rmb_library() ? "no shader library" : "the device has no ray tracing");
		return 0;
	}

	if (!rm_rtw_turb_table(turb))
	{
		snprintf(err, errlen, "the turbulence table is empty");
		return 0;
	}

	@autoreleasepool
	{
		NSError *e = nil;
		id<MTLFunction> k = [rmb_library() newFunctionWithName:@"rm_rtw_trace"];
		id<MTLFunction> kw = [rmb_library() newFunctionWithName:@"rm_rtw_trace_ws"];
		id<MTLFunction> kc = [rmb_library() newFunctionWithName:@"rm_rtw_compact"];
		id<MTLFunction> ka = [rmb_library() newFunctionWithName:@"rm_rtw_args"];
		id<MTLCounterSet> ts = nil;

		/* TASK58's path (r_rt_water_hits 0) needs its kernel, the blend, the show view, the translucent draw and the counts */
		if (!k)
		{
			snprintf(err, errlen, "the shader library has no rm_rtw_trace");
			return 0;
		}

		RMB_RTW_SET(trace_pipe, [rmb_device newComputePipelineStateWithFunction:k error:&e]);
		RMB_RTW_SET(blend_pipe, trace_pipe ? rmb_rtw_fullscreen(@"rm_fragment_rtw_blend", 1, &e) : nil);
		RMB_RTW_SET(show_pipe, blend_pipe ? rmb_rtw_fullscreen(@"rm_fragment_rtw_show", 0, &e) : nil);
		RMB_RTW_SET(trans_pipe, show_pipe ? rmb_rtw_trans_pipe(&e) : nil);
		RMB_RTW_SET(counts, [rmb_device newBufferWithLength:RMB_FRAMES_IN_FLIGHT * RMB_RTW_SLOT * sizeof(uint32_t)
				options:MTLResourceStorageModeShared]);

		if (!trace_pipe || !blend_pipe || !show_pipe || !trans_pipe || !counts)
		{
			snprintf(err, errlen, "cannot build the water pipelines%s%s", e ? ": " : "",
					e ? e.localizedDescription.UTF8String : "");
			rmb_rtw_close();
			return 0;
		}

		/* TASK158: the world-space path's; without them only r_rt_water_hits 1 is refused (rm_rtw_path) */
		ws_err[0] = 0;
		ws_ok = rmb_rtw_open_ws(kw, kc, ka, ws_err, sizeof(ws_err));

		/* the passes' GPU time, where the device samples timestamps at the encoders' boundaries */
		for (id<MTLCounterSet> cs in rmb_device.counterSets)
		{
			if ([cs.name isEqualToString:MTLCommonCounterSetTimestamp])
			{
				ts = cs;
			}
		}

		if (ts && [rmb_device supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary])
		{
			MTLCounterSampleBufferDescriptor *sd = [MTLCounterSampleBufferDescriptor new];

			sd.counterSet = ts;
			sd.storageMode = MTLStorageModeShared;
			sd.sampleCount = RMB_FRAMES_IN_FLIGHT * RMB_RTW_SAMPLES;
			RMB_RTW_SET(samples, [rmb_device newCounterSampleBufferWithDescriptor:sd error:&e]);
		}
	}

	for (int i = 0; i < RMB_FRAMES_IN_FLIGHT; i++)
	{
		atomic_store(&slot_done[i], ~0UL);   /* no frame */
		slot_pending[i] = 0;
	}

	ndone = 0;
	view_set = traced = forward = shown = marked = 0;
	tex_n = lm_tid = sky_set = nlights = 0;
	rtw_open = 1;
	return 1;
}

/* The frames still in flight keep what they use: a command buffer retains its resources. */
void
rmb_rtw_close(void)
{
	@autoreleasepool
	{
		RMB_RTW_SET(trace_pipe, nil);
		RMB_RTW_SET(blend_pipe, nil);
		RMB_RTW_SET(show_pipe, nil);
		RMB_RTW_SET(trans_pipe, nil);
		RMB_RTW_SET(t_refl, nil);
		RMB_RTW_SET(t_wdist, nil);
		RMB_RTW_SET(list, nil);
		RMB_RTW_SET(counts, nil);
		RMB_RTW_SET(samples, nil);
		RMB_RTW_SET(shade, nil);
		RMB_RTW_SET(blur_pipe, nil);
		RMB_RTW_SET(t_rblur, nil);
		RMB_RTW_SET(t_refl_lo, nil);
		RMB_RTW_SET(t_wdist_lo, nil);
		RMB_RTW_SET(t_rblur_lo, nil);
		RMB_RTW_SET(trace_lo_pipe, nil);
		RMB_RTW_SET(trace_ws_lo_pipe, nil);
		RMB_RTW_SET(compact_lo_pipe, nil);
		RMB_RTW_SET(blur_lo_pipe, nil);
		RMB_RTW_SET(up_pipe, nil);
		rmb_rtw_close_ws();
	}

	prep_div = 1;
	lo_refused = 0;
	blur_on = blur_bad = blurred = 0;
	blur_bad_w = blur_bad_h = 0;
	rtw_open = 0;
	made_ws = 0;
	list_cap = shade_n = 0;
	view_set = traced = forward = shown = marked = 0;
	tex_n = lm_tid = sky_set = nlights = 0;
	ndone = 0;
}

/* TASK126 slice 5: a traced-size texture of format f, private and read and written by the kernels */
static id<MTLTexture>
rmb_rtw_lo_tex(MTLPixelFormat f, unsigned long w, unsigned long h)
{
	MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:f width:w height:h mipmapped:NO];

	d.storageMode = MTLStorageModePrivate;
	d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
	return [rmb_device newTextureWithDescriptor:d];
}

/*
 * TASK126 slice 5: for divisor div above 1, the traced size's outputs at
 * mtl_rtup_size of the render size w x h (the blurred one while the blur
 * is on) and the five pipelines; 1 when they are there. At divisor 1 the
 * textures are released and nothing else is needed: the stock kernels run.
 */
static int
rmb_rtw_prepare_lo(unsigned long w, unsigned long h, unsigned div)
{
	const unsigned long tw = mtl_rtup_size((unsigned)w, div), th = mtl_rtup_size((unsigned)h, div);

	prep_div = (div > 1) ? div : 1;

	@autoreleasepool
	{
		if ((div <= 1) || !blur_on)
		{
			RMB_RTW_SET(t_rblur_lo, nil);
		}

		if (div <= 1)
		{
			RMB_RTW_SET(t_refl_lo, nil);
			RMB_RTW_SET(t_wdist_lo, nil);
			return 1;
		}

		if (lo_refused)
		{
			return 0;
		}

		if (!trace_lo_pipe || !trace_ws_lo_pipe || !compact_lo_pipe || !blur_lo_pipe || !up_pipe)
		{
			NSError *e = nil;
			id<MTLFunction> k[5];
			NSString *names[5] = { @"rm_rtw_trace_lo", @"rm_rtw_trace_ws_lo", @"rm_rtw_compact_lo", @"rm_rtw_denoise_lo",
				@"rm_rtw_up" };
			int i;

			for (i = 0; i < 5; i++)
			{
				k[i] = [rmb_library() newFunctionWithName:names[i]];
			}

			RMB_RTW_SET(trace_lo_pipe, k[0] ? [rmb_device newComputePipelineStateWithFunction:k[0] error:&e] : nil);
			RMB_RTW_SET(trace_ws_lo_pipe, k[1] ? [rmb_device newComputePipelineStateWithFunction:k[1] error:&e] : nil);
			RMB_RTW_SET(compact_lo_pipe, k[2] ? [rmb_device newComputePipelineStateWithFunction:k[2] error:&e] : nil);
			RMB_RTW_SET(blur_lo_pipe, k[3] ? [rmb_device newComputePipelineStateWithFunction:k[3] error:&e] : nil);
			RMB_RTW_SET(up_pipe, k[4] ? [rmb_device newComputePipelineStateWithFunction:k[4] error:&e] : nil);

			if (!trace_lo_pipe || !trace_ws_lo_pipe || !compact_lo_pipe || !blur_lo_pipe || !up_pipe)
			{
				RMB_RTW_SET(trace_lo_pipe, nil);
				RMB_RTW_SET(trace_ws_lo_pipe, nil);
				RMB_RTW_SET(compact_lo_pipe, nil);
				RMB_RTW_SET(blur_lo_pipe, nil);
				RMB_RTW_SET(up_pipe, nil);
				lo_refused = 1;
				fprintf(stderr, "ref_metal: rt water: no pipelines for r_rt_water_scale below 1%s%s; no water reflections\n",
						e ? ": " : "", e ? e.localizedDescription.UTF8String : "");
				return 0;
			}
		}

		if (!t_refl_lo || (t_refl_lo.width != tw) || (t_refl_lo.height != th) || !t_wdist_lo)
		{
			RMB_RTW_SET(t_refl_lo, rmb_rtw_lo_tex(MTLPixelFormatRGBA8Unorm, tw, th));
			RMB_RTW_SET(t_wdist_lo, rmb_rtw_lo_tex(MTLPixelFormatR32Float, tw, th));
			RMB_RTW_SET(t_rblur_lo, nil);

			if (!t_refl_lo || !t_wdist_lo)
			{
				RMB_RTW_SET(t_refl_lo, nil);
				RMB_RTW_SET(t_wdist_lo, nil);
				return 0;
			}

			fprintf(stderr, "ref_metal: rt water %lux%lu traced at %lux%lu: %lu bytes allocated\n", w, h, tw, th,
					(unsigned long)(t_refl_lo.allocatedSize + t_wdist_lo.allocatedSize));
		}

		/* the blur's output; without it the view is not blurred (rmb_rtw_lo_up) */
		if (blur_on && !t_rblur_lo)
		{
			RMB_RTW_SET(t_rblur_lo, rmb_rtw_lo_tex(MTLPixelFormatRGBA8Unorm, tw, th));
		}
	}

	return 1;
}

int
rmb_rtw_prepare(unsigned long w, unsigned long h, int ws, unsigned div)
{
	/* TASK126 slice 5: the list holds the traced pixels, w x h at divisor 1 */
	const unsigned long ln = (div > 1) ? (unsigned long)mtl_rtup_size((unsigned)w, div) * mtl_rtup_size((unsigned)h, div) :
		w * h;

	ws = ws != 0;

	if (!rtw_open || !trace_pipe || !counts || (ws && !ws_ok) || (w < 1) || (h < 1) || (w > 0xffffUL) || (h > 0xffffUL))
	{
		return 0;
	}

	/* the outputs (and on the world-space path the list) at the render size, made again when it or the path changes */
	if (t_refl && t_wdist && (t_refl.width == w) && (t_refl.height == h) && (made_ws == ws) &&
		(!ws || (list && (list_cap == ln))))
	{
		return rmb_rtw_prepare_lo(w, h, div);
	}

	@autoreleasepool
	{
		MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
				width:w height:h mipmapped:NO];

		/* TASK162: on either path the kernels alone write the reflection target (the world-space compaction its 0s) */
		d.storageMode = MTLStorageModePrivate;
		d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
		RMB_RTW_SET(t_refl, [rmb_device newTextureWithDescriptor:d]);
		d.pixelFormat = MTLPixelFormatR32Float;
		d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
		RMB_RTW_SET(t_wdist, [rmb_device newTextureWithDescriptor:d]);
		RMB_RTW_SET(list, ws ? [rmb_device newBufferWithLength:ln * sizeof(uint32_t)
				options:MTLResourceStorageModePrivate] : nil);
		list_cap = list ? ln : 0;
		made_ws = ws;

		if (!t_refl || !t_wdist || (ws && !list))
		{
			RMB_RTW_SET(t_refl, nil);
			RMB_RTW_SET(t_wdist, nil);
			RMB_RTW_SET(list, nil);
			list_cap = 0;
			made_ws = 0;
			return 0;
		}

		fprintf(stderr, "ref_metal: rt water %lux%lu: %lu bytes allocated\n", w, h,
				(unsigned long)(t_refl.allocatedSize + t_wdist.allocatedSize + (list ? list.allocatedSize : 0)));
	}

	return rmb_rtw_prepare_lo(w, h, div);
}

void
rmb_rtw_set_view(const rm_rtw_view_t *v)
{
	view_set = shown = marked = 0;

	if (!rtw_open || !v)
	{
		return;
	}

	view = *v;
	view_set = 1;
}

void
rmb_rtw_set_shade(const unsigned *rec, unsigned long n)
{
	if (!rtw_open)
	{
		return;
	}

	@autoreleasepool
	{
		RMB_RTW_SET(shade, (rec && n && (n <= 0xffffffffUL)) ? [rmb_device newBufferWithBytes:rec
				length:n * sizeof(unsigned) options:MTLResourceStorageModeShared] : nil);
	}

	shade_n = shade ? n : 0;
}

void
rmb_rtw_set_textures(const int *tids, int n, int lmtex, const int *sky)
{
	int i;

	tex_n = 0;
	lm_tid = lmtex;
	sky_set = sky != NULL;

	for (i = 0; tids && i < n && i < (int)MTL_RTW_TEX_WORLD; i++)
	{
		tex_tids[tex_n++] = tids[i];
	}

	for (i = 0; i < 6; i++)
	{
		sky_tids[i] = sky ? sky[i] : 0;
	}
}

void
rmb_rtw_set_lights(const float *l, int n)
{
	nlights = (!l || (n < 0)) ? 0 : (n > MTL_RTS_MAX_LIGHTS) ? MTL_RTS_MAX_LIGHTS : n;

	if (nlights)
	{
		memcpy(lights, l, (size_t)nlights * 8 * sizeof(float));
	}
}

/* the time between two timestamps of a resolved range, below 0 when either is missing */
static double
rmb_rtw_span(const MTLCounterResultTimestamp *t, NSUInteger n, NSUInteger a, NSUInteger b)
{
	if (!t || (a >= n) || (b >= n) || (t[a].timestamp == MTLCounterErrorValue) ||
		(t[b].timestamp == MTLCounterErrorValue) || (t[b].timestamp < t[a].timestamp))
	{
		return -1;
	}

	return rmb_rt_gpu_ms(t[b].timestamp - t[a].timestamp);
}

/* the counts and times of the slots whose frames completed since the last read */
static void
rmb_rtw_read(void)
{
	int k;

	for (k = 0; k < RMB_FRAMES_IN_FLIGHT; k++)
	{
		const uint32_t *c;
		rm_rtw_stats_t *s;

		if (!slot_pending[k] || (atomic_load(&slot_done[k]) != slot_frame[k]))
		{
			continue;
		}

		slot_pending[k] = 0;

		if (ndone >= RMB_FRAMES_IN_FLIGHT)
		{
			continue;
		}

		c = (const uint32_t *)counts.contents + k * RMB_RTW_SLOT;
		s = &done[ndone++];
		memset(s, 0, sizeof(*s));
		s->frame = slot_frame[k];
		s->pixels = slot_pixels[k];
		s->opaque = c[0];
		s->trans = c[1];
		s->eye = c[2];
		s->lit = c[3];
		s->sky = c[4];
		s->miss = c[5];
		s->world = c[6];
		s->skybox = c[7];
		s->ws = slot_ws[k];
		/* TASK58's path traces every pixel: the render size's, or below divisor 1 the traced size's (TASK126) */
		s->listed = slot_ws[k] ? c[RMB_RTW_C_LISTED] : slot_tw[k] * slot_th[k];
		s->div = slot_div[k];
		s->tw = slot_tw[k];
		s->th = slot_th[k];
		s->fallbacks = c[RMB_RTW_C_FALLBACK];
		s->ms = s->compact_ms = s->trace_ms = s->span_ms = -1;

		/*
		 * TASK58's path: its compute encoder. The world-space path (TASK158):
		 * the compaction's encoder (with the arguments) and the trace's, each
		 * on its own, and the whole span. TASK162: no raster of its own (its
		 * marks are a draw of the G-buffer raster, timed with it), so there
		 * are no raster figures (FIX98).
		 */
		if (slot_timed[k] && samples)
		{
			const NSUInteger ns = slot_ws[k] ? RMB_RTW_SAMPLES : 2;
			NSData *d = [samples resolveCounterRange:NSMakeRange((NSUInteger)k * RMB_RTW_SAMPLES, ns)];
			const MTLCounterResultTimestamp *t = d ? (const MTLCounterResultTimestamp *)d.bytes : NULL;
			const NSUInteger n = d ? d.length / sizeof(*t) : 0;

			if (!slot_ws[k])
			{
				s->ms = rmb_rtw_span(t, n, 0, 1);
			}
			else
			{
				s->compact_ms = rmb_rtw_span(t, n, 0, 1);
				s->trace_ms = rmb_rtw_span(t, n, 2, 3);
				s->span_ms = rmb_rtw_span(t, n, 0, 3);

				if ((s->compact_ms >= 0) && (s->trace_ms >= 0))
				{
					s->ms = s->compact_ms + s->trace_ms;
				}
			}
		}
	}
}

int
rmb_rtw_stats(rm_rtw_stats_t *s, int cap)
{
	int i, n;

	if (!rtw_open)
	{
		return 0;
	}

	rmb_rtw_read();
	n = (ndone < cap) ? ndone : cap;

	for (i = 0; i < n; i++)
	{
		s[i] = done[i];
	}

	ndone = 0;
	return n;
}

/*
 * TASK162: the water's marks on the G-buffer raster's encoder enc, its
 * last draw (rmb_gbuf_resolve, before the pass ends; surf: the raster has
 * the surface attachment): the map's water triangles with the view's
 * matrix and viewport, as TASK158's separate raster drew them, against
 * the G-buffer raster's own depth. Only when this view's outputs were made for
 * the world-space path; 1 when drawn (with no triangle there is nothing
 * to draw, and every pixel reads unmarked).
 */
int
rmb_rtw_mark(id<MTLRenderCommandEncoder> enc, int surf)
{
	id<MTLRenderPipelineState> pipe = mark_pipe[surf ? 1 : 0];
	MTLViewport vp;
	unsigned long nw;

	marked = 0;

	if (!rtw_open || !ws_ok || !view_set || !made_ws || !enc || !pipe || !mark_depth)
	{
		return 0;
	}

	vp.originX = view.vp[0];
	vp.originY = view.vp[1];
	vp.width = view.vp[2];
	vp.height = view.vp[3];
	vp.znear = 0.0;
	vp.zfar = 1.0;

	if (!rmb_fmt_bind(enc, pipe))
	{
		return 0;
	}

	[enc setDepthStencilState:mark_depth];
	[enc setCullMode:MTLCullModeNone];
	[enc setViewport:vp];
	nw = rmb_rt_bind_water_raster(enc, 0, 1, 2, 3);

	if (nw)
	{
		[enc setVertexBytes:&view length:sizeof(view) atIndex:4];
		[enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:nw * 3];
		rmb_counts.draws++;
	}

	marked = 1;
	return 1;
}

/* TASK158: the frame's texture table in slot's argument buffer, every texture it names made resident on ce */
static void
rmb_rtw_bind_textures(id<MTLComputeCommandEncoder> ce, int slot)
{
	__unsafe_unretained id<MTLTexture> t[MTL_RTW_TEX_COUNT];
	id<MTLTexture> lm = lm_tid ? rmb_texture_for_draw(lm_tid) : nil;
	unsigned i;

	for (i = 0; i < MTL_RTW_TEX_COUNT; i++)
	{
		t[i] = white2d;
	}

	/* rmb_texture_for_draw marks each as used by this frame, so none is freed while the frame runs */
	for (i = 0; i < (unsigned)tex_n; i++)
	{
		id<MTLTexture> x = rmb_texture_for_draw(tex_tids[i]);

		t[i] = (x && (x.textureType == MTLTextureType2D)) ? x : white2d;
	}

	for (i = 0; sky_set && (i < 6); i++)
	{
		id<MTLTexture> x = rmb_texture_for_draw(sky_tids[i]);

		t[MTL_RTW_TEX_SKY + i] = (x && (x.textureType == MTLTextureType2D)) ? x : white2d;
	}

	[tex_enc setArgumentBuffer:tex_ab offset:(NSUInteger)slot * tex_len];
	[tex_enc setTextures:t withRange:NSMakeRange(0, MTL_RTW_TEX_COUNT)];
	[ce setBuffer:tex_ab offset:(NSUInteger)slot * tex_len atIndex:12];
	[ce useResources:t count:(NSUInteger)tex_n usage:MTLResourceUsageRead];
	[ce useResources:&t[MTL_RTW_TEX_SKY] count:6 usage:MTLResourceUsageRead];
	[ce useResource:white2d usage:MTLResourceUsageRead];
	[ce setTexture:((lm && (lm.textureType == MTLTextureType2DArray)) ? lm : white_arr) atIndex:6];
}

/* a compute encoder on cmd; timed, its start and end sampled at slot's samples first and first + 1 */
static id<MTLComputeCommandEncoder>
rmb_rtw_encoder(id<MTLCommandBuffer> cmd, int timed, int slot, NSUInteger first)
{
	MTLComputePassDescriptor *cp;

	if (!timed)
	{
		return [cmd computeCommandEncoder];
	}

	cp = [MTLComputePassDescriptor computePassDescriptor];
	cp.sampleBufferAttachments[0].sampleBuffer = samples;
	cp.sampleBufferAttachments[0].startOfEncoderSampleIndex = (NSUInteger)slot * RMB_RTW_SAMPLES + first;
	cp.sampleBufferAttachments[0].endOfEncoderSampleIndex = (NSUInteger)slot * RMB_RTW_SAMPLES + first + 1;
	return [cmd computeCommandEncoderWithDescriptor:cp];
}

/*
 * The trace's common bindings on ce (TASK58's): the scene, the map's
 * materials and geometry, the G-buffer, the lit result, the outputs, the
 * view, the turbulence table and the slot's counts. 0 when the scene,
 * the materials or the geometry cannot be bound (the encoder is ended).
 */
static int
rmb_rtw_bind_trace(id<MTLComputeCommandEncoder> ce, unsigned long fno, int slot, id<MTLTexture> target,
		id<MTLTexture> depth, id<MTLTexture> normal, id<MTLTexture> material)
{
	/* the scene; its transmission and count go to slots 10 and 11, which the kernel does not read */
	if (!rmb_rt_bind_scene(ce, fno, 2, 3, 10, 11))
	{
		[ce endEncoding];
		return 0;
	}

	/* the map's material ids and its vertices and triangles; without them there is no water to find */
	if (!rmb_rt_bind_materials(ce, 4, 5) || !rmb_rt_bind_geometry(ce, 7, 8, 9))
	{
		[ce endEncoding];
		return 0;
	}

	[ce setTexture:depth atIndex:0];
	[ce setTexture:normal atIndex:1];
	[ce setTexture:material atIndex:2];
	[ce setTexture:target atIndex:3];
	[ce setTexture:t_refl atIndex:4];
	[ce setTexture:t_wdist atIndex:5];
	[ce setBytes:&view length:sizeof(view) atIndex:0];
	[ce setBytes:turb length:sizeof(turb) atIndex:1];
	[ce setBuffer:counts offset:(NSUInteger)slot * RMB_RTW_SLOT * sizeof(uint32_t) atIndex:6];
	return 1;
}

/*
 * TASK126 slice 5: below divisor 1, on the trace's encoder ce, the traced
 * size's outputs where the stock kernels' are (textures 4 and 5) and the
 * divisor's block (buffer 17), for the trace at the traced size.
 */
static void
rmb_rtw_lo_bind(id<MTLComputeCommandEncoder> ce, const rmb_rtw_lo_t *lov)
{
	[ce setTexture:t_refl_lo atIndex:4];
	[ce setTexture:t_wdist_lo atIndex:5];
	[ce setBytes:lov length:sizeof(*lov) atIndex:17];
}

/*
 * TASK126 slice 5: after the trace at the traced size on ce (a serial
 * encoder, each dispatch behind a barrier), the 3 x 3 blur at that size
 * while it is on, then rm_rtw_up at the render size w x h, which writes
 * t_refl and t_wdist from the traced (or blurred) reflection, so the
 * blend and the forward pass read them as the stock trace's.
 */
static void
rmb_rtw_lo_up(id<MTLComputeCommandEncoder> ce, id<MTLTexture> depth, id<MTLTexture> normal, id<MTLTexture> material,
		NSUInteger w, NSUInteger h)
{
	const MTLSize g = MTLSizeMake(RMB_RTW_GROUP, RMB_RTW_GROUP, 1);
	const int blur = blur_on && (t_rblur_lo != nil);

	[ce memoryBarrierWithScope:MTLBarrierScopeTextures];

	if (blur)
	{
		[ce setComputePipelineState:blur_lo_pipe];
		[ce setTexture:t_refl_lo atIndex:0];
		[ce setTexture:depth atIndex:1];
		[ce setTexture:t_wdist_lo atIndex:2];
		[ce setTexture:t_rblur_lo atIndex:3];
		[ce dispatchThreads:MTLSizeMake(t_refl_lo.width, t_refl_lo.height, 1) threadsPerThreadgroup:g];
		[ce memoryBarrierWithScope:MTLBarrierScopeTextures];
	}

	[ce setComputePipelineState:up_pipe];
	[ce setTexture:depth atIndex:0];
	[ce setTexture:normal atIndex:1];
	[ce setTexture:material atIndex:2];
	[ce setTexture:t_refl atIndex:4];
	[ce setTexture:t_wdist atIndex:5];
	[ce setTexture:(blur ? t_rblur_lo : t_refl_lo) atIndex:7];
	[ce setTexture:t_wdist_lo atIndex:8];
	[ce dispatchThreads:MTLSizeMake(w, h, 1) threadsPerThreadgroup:g];
}

/* r_rt_water_hits 0: TASK58's pass, one compute encoder, its kernel over every pixel */
static int
rmb_rtw_trace_ss(id<MTLCommandBuffer> cmd, unsigned long fno, int slot, int timed, id<MTLTexture> target,
		id<MTLTexture> depth, id<MTLTexture> normal, id<MTLTexture> material)
{
	const NSUInteger w = depth.width, h = depth.height;
	const rmb_rtw_lo_t lov = { prep_div, { 0, 0, 0 } };
	id<MTLComputeCommandEncoder> ce = rmb_rtw_encoder(cmd, timed, slot, 0);

	if (!ce)
	{
		return 0;
	}

	[ce setComputePipelineState:((prep_div > 1) ? trace_lo_pipe : trace_pipe)];

	if (!rmb_rtw_bind_trace(ce, fno, slot, target, depth, normal, material))
	{
		return 0;
	}

	if (prep_div > 1)
	{
		/* TASK126 slice 5: every traced pixel, then the blur and the upsample */
		rmb_rtw_lo_bind(ce, &lov);
		[ce dispatchThreads:MTLSizeMake(t_refl_lo.width, t_refl_lo.height, 1)
				threadsPerThreadgroup:MTLSizeMake(RMB_RTW_GROUP, RMB_RTW_GROUP, 1)];
		rmb_rtw_lo_up(ce, depth, normal, material, w, h);
	}
	else
	{
		[ce dispatchThreads:MTLSizeMake(w, h, 1) threadsPerThreadgroup:MTLSizeMake(RMB_RTW_GROUP, RMB_RTW_GROUP, 1)];
	}

	[ce endEncoding];
	return 1;
}

/*
 * r_rt_water_hits 1 (TASK158): the compaction and the arguments in one
 * compute encoder, the trace over the list in another; each timed on its
 * own (RMB_RTW_SAMPLES). The marks the compaction reads were drawn by
 * the G-buffer raster (TASK162, rmb_rtw_mark).
 */
static int
rmb_rtw_trace_ws(id<MTLCommandBuffer> cmd, unsigned long fno, int slot, int timed, id<MTLTexture> target,
		id<MTLTexture> depth, id<MTLTexture> normal, id<MTLTexture> material)
{
	const NSUInteger w = depth.width, h = depth.height;
	const uint32_t cap = (uint32_t)list_cap;
	const uint32_t ns = (uint32_t)shade_n;
	const int lo = (prep_div > 1);
	const rmb_rtw_lo_t lov = { prep_div, { 0, 0, 0 } };
	id<MTLComputeCommandEncoder> ce = rmb_rtw_encoder(cmd, timed, slot, 0);

	if (!ce)
	{
		return 0;
	}

	/*
	 * The compaction over every pixel (the 0s where it lists none), then the
	 * list's threadgroups (one serial encoder). TASK126 slice 5: below
	 * divisor 1 over every traced pixel, into the traced size's list and
	 * reflection.
	 */
	[ce setComputePipelineState:(lo ? compact_lo_pipe : compact_pipe)];
	[ce setTexture:material atIndex:0];
	[ce setTexture:(lo ? t_refl_lo : t_refl) atIndex:1];
	[ce setTexture:normal atIndex:2];
	[ce setBytes:&view length:sizeof(view) atIndex:0];
	[ce setBuffer:list offset:0 atIndex:1];
	[ce setBuffer:counts offset:(NSUInteger)slot * RMB_RTW_SLOT * sizeof(uint32_t) atIndex:2];
	[ce setBytes:&cap length:sizeof(cap) atIndex:3];

	if (lo)
	{
		[ce setBytes:&lov length:sizeof(lov) atIndex:17];
		[ce dispatchThreads:MTLSizeMake(t_refl_lo.width, t_refl_lo.height, 1)
				threadsPerThreadgroup:MTLSizeMake(RMB_RTW_GROUP, RMB_RTW_GROUP, 1)];
	}
	else
	{
		[ce dispatchThreads:MTLSizeMake(w, h, 1) threadsPerThreadgroup:MTLSizeMake(RMB_RTW_GROUP, RMB_RTW_GROUP, 1)];
	}

	[ce setComputePipelineState:args_pipe];
	[ce setBuffer:counts offset:(NSUInteger)slot * RMB_RTW_SLOT * sizeof(uint32_t) atIndex:0];
	[ce setBytes:&cap length:sizeof(cap) atIndex:1];
	[ce dispatchThreads:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
	[ce endEncoding];

	/* the trace in an encoder of its own (the list and the arguments are tracked: it waits for them) */
	ce = rmb_rtw_encoder(cmd, timed, slot, 2);

	if (!ce)
	{
		return 0;
	}

	[ce setComputePipelineState:(lo ? trace_ws_lo_pipe : trace_ws_pipe)];

	if (!rmb_rtw_bind_trace(ce, fno, slot, target, depth, normal, material))
	{
		return 0;
	}

	if (lo)
	{
		rmb_rtw_lo_bind(ce, &lov);
	}

	/* the world-space shading: the texture table and lightmaps, the lights, the triangles' records */
	rmb_rtw_bind_textures(ce, slot);
	[ce setBytes:lights length:(nlights ? (NSUInteger)nlights : 1) * 8 * sizeof(float) atIndex:13];
	[ce setBuffer:(shade ? shade : counts) offset:0 atIndex:14];
	[ce setBytes:&ns length:sizeof(ns) atIndex:15];
	[ce setBuffer:list offset:0 atIndex:16];
	[ce dispatchThreadgroupsWithIndirectBuffer:counts
			indirectBufferOffset:((NSUInteger)slot * RMB_RTW_SLOT + RMB_RTW_C_ARGS) * sizeof(uint32_t)
			threadsPerThreadgroup:MTLSizeMake(MTL_RTW_LIST_GROUP, 1, 1)];

	if (lo)
	{
		/* TASK126 slice 5: the blur at the traced size and the upsample, in the trace's encoder */
		rmb_rtw_lo_up(ce, depth, normal, material, w, h);
	}

	[ce endEncoding];
	return 1;
}

void
rmb_rtw_set_blur(int on)
{
	blur_on = (on != 0);

	if (!blur_on && (blur_pipe || t_rblur || t_rblur_lo))
	{
		@autoreleasepool
		{
			RMB_RTW_SET(blur_pipe, nil);
			RMB_RTW_SET(t_rblur, nil);
			RMB_RTW_SET(t_rblur_lo, nil);
		}
	}
}

/* the reflection the blend and the forward pass read: the blurred one when this view blurred it */
static id<MTLTexture>
rmb_rtw_out(void)
{
	return (blurred && t_rblur) ? t_rblur : t_refl;
}

/*
 * TASK59 slice 2b: the 3 x 3 blur of the reflection the trace has just
 * written, into t_rblur, in an encoder of its own after the trace's on
 * the same command buffer (the textures are tracked, so it waits for the
 * trace). 1 when it was dispatched; 0 leaves the trace's output to the
 * blend. Its GPU time is not in the pass's stats.
 */
static int
rmb_rtw_blur(id<MTLCommandBuffer> cmd, id<MTLTexture> depth, NSUInteger w, NSUInteger h)
{
	id<MTLComputeCommandEncoder> ce;
	mtl_rtd_rview_t rv;

	/* round 2: a failure at one render size is tried again, once, at the next */
	if (blur_bad && ((w != blur_bad_w) || (h != blur_bad_h)))
	{
		blur_bad = 0;
	}

	if (!blur_on || blur_bad || !t_refl || !t_wdist)
	{
		return 0;
	}

	if (!blur_pipe)
	{
		NSError *e = nil;
		id<MTLFunction> k = [rmb_library() newFunctionWithName:@"rm_rtw_denoise"];

		RMB_RTW_SET(blur_pipe, k ? [rmb_device newComputePipelineStateWithFunction:k error:&e] : nil);

		if (!blur_pipe)
		{
			blur_bad = 1;
			blur_bad_w = w;
			blur_bad_h = h;
			fprintf(stderr, "ref_metal: rt water: no reflection blur pipeline%s%s\n", e ? ": " : "",
					e ? e.localizedDescription.UTF8String : "");
			return 0;
		}
	}

	if (!t_rblur || (t_rblur.width != w) || (t_rblur.height != h))
	{
		MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
				width:w height:h mipmapped:NO];

		d.storageMode = MTLStorageModePrivate;
		d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
		RMB_RTW_SET(t_rblur, [rmb_device newTextureWithDescriptor:d]);

		if (!t_rblur)
		{
			blur_bad = 1;
			blur_bad_w = w;
			blur_bad_h = h;
			fprintf(stderr, "ref_metal: rt water: cannot make the reflection blur's %lux%lu output\n",
					(unsigned long)w, (unsigned long)h);
			return 0;
		}

		fprintf(stderr, "ref_metal: rt water blur %lux%lu: %lu bytes allocated\n", (unsigned long)w,
				(unsigned long)h, (unsigned long)t_rblur.allocatedSize);
	}

	ce = [cmd computeCommandEncoder];

	if (!ce)
	{
		return 0;
	}

	memcpy(rv.inv, view.inv, sizeof(rv.inv));
	memcpy(rv.vp, view.vp, sizeof(rv.vp));
	memcpy(rv.eye, view.eye, sizeof(rv.eye));
	[ce setComputePipelineState:blur_pipe];
	[ce setTexture:t_refl atIndex:0];
	[ce setTexture:depth atIndex:1];
	[ce setTexture:t_wdist atIndex:2];
	[ce setTexture:t_rblur atIndex:3];
	[ce setBytes:&rv length:sizeof(rv) atIndex:0];
	[ce dispatchThreads:MTLSizeMake(w, h, 1) threadsPerThreadgroup:MTLSizeMake(RMB_RTW_GROUP, RMB_RTW_GROUP, 1)];
	[ce endEncoding];
	return 1;
}

int
rmb_rtw_trace(id<MTLTexture> target, id<MTLTexture> depth, id<MTLTexture> normal, id<MTLTexture> material)
{
	unsigned long fno = 0;
	id<MTLCommandBuffer> cmd;
	const NSUInteger w = depth.width, h = depth.height;
	const int ws = made_ws;
	int slot, timed;

	traced = forward = shown = blurred = 0;

	if (!rtw_open || !view_set || !target || !depth || !normal || !material)
	{
		return 0;
	}

	view_set = 0;
	rmb_rtw_read();

	/*
	 * The outputs, made by rmb_rtw_prepare before the raster at this size
	 * for this view's path (and on the world-space path the list, and the
	 * marks the raster drew, TASK162); the lit result at the same size.
	 */
	if (!t_refl || !t_wdist || (t_refl.width != w) || (t_refl.height != h) || (target.width != w) ||
		(target.height != h))
	{
		return 0;
	}

	/*
	 * TASK126 slice 5: below divisor 1, the traced size's outputs and the
	 * pipelines made by rmb_rtw_prepare at this size, and the list of the
	 * traced pixels.
	 */
	if ((prep_div > 1) && (!trace_lo_pipe || !trace_ws_lo_pipe || !compact_lo_pipe || !blur_lo_pipe || !up_pipe ||
		!t_refl_lo || !t_wdist_lo || (t_refl_lo.width != mtl_rtup_size((unsigned)w, prep_div)) ||
		(t_refl_lo.height != mtl_rtup_size((unsigned)h, prep_div))))
	{
		return 0;
	}

	if (ws && (!list || (list_cap != ((prep_div > 1) ? t_refl_lo.width * t_refl_lo.height : w * h)) || !marked ||
		(normal.width < w) || (normal.height < h)))
	{
		return 0;
	}

	@autoreleasepool
	{
		/* ends the resolve: the kernel reads its lit result */
		cmd = rmb_frame_compute(&fno);

		if (!cmd)
		{
			return 0;
		}

		slot = (int)(fno % RMB_FRAMES_IN_FLIGHT);
		timed = (samples != nil);

		/*
		 * The frame that used this slot before has completed on the GPU (the
		 * frame semaphore), though its handler may not have run yet: its
		 * counts are dropped, and its handler, naming its own frame, does not
		 * mark this one done.
		 */
		memset((uint32_t *)counts.contents + slot * RMB_RTW_SLOT, 0, RMB_RTW_SLOT * sizeof(uint32_t));

		if (!(ws ? rmb_rtw_trace_ws : rmb_rtw_trace_ss)(cmd, fno, slot, timed, target, depth, normal, material))
		{
			return 0;
		}

		/* TASK126 slice 5: below divisor 1 the blur ran at the traced size, before the upsample wrote t_refl */
		blurred = (prep_div > 1) ? 0 : rmb_rtw_blur(cmd, depth, w, h);
		slot_pending[slot] = 1;
		slot_timed[slot] = timed;
		slot_ws[slot] = ws;
		slot_frame[slot] = fno;
		slot_pixels[slot] = (unsigned long)(w * h);
		slot_div[slot] = prep_div;
		slot_tw[slot] = (prep_div > 1) ? (unsigned long)t_refl_lo.width : (unsigned long)w;
		slot_th[slot] = (prep_div > 1) ? (unsigned long)t_refl_lo.height : (unsigned long)h;
		[cmd addCompletedHandler:^(id<MTLCommandBuffer> b) {
			(void)b;
			atomic_store(&slot_done[slot], fno);
		}];
	}

	traced = 1;
	forward = !view.show;   /* the development view leaves the forward pass out */
	return 1;
}

int
rmb_rtw_blend(id<MTLRenderCommandEncoder> enc, id<MTLTexture> material)
{
	const int did = traced && t_refl;

	traced = 0;

	if (!rtw_open || !enc || !did || !material)
	{
		return 0;
	}

	if (!rmb_fmt_bind(enc, view.show ? show_pipe : blend_pipe))
	{
		return 0;
	}

	[enc setFragmentBytes:&view length:sizeof(view) atIndex:0];
	[enc setFragmentTexture:rmb_rtw_out() atIndex:0];
	[enc setFragmentTexture:material atIndex:1];
	[enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
	rmb_counts.draws++;
	shown = (view.show != 0);
	return 1;
}

int
rmb_rtw_shown(void)
{
	return rtw_open && shown;
}

id<MTLRenderPipelineState>
rmb_rtw_forward_pipe(void)
{
	return (rtw_open && forward && t_refl && t_wdist) ? trans_pipe : nil;
}

void
rmb_rtw_bind_forward(id<MTLRenderCommandEncoder> enc)
{
	if (!rmb_rtw_forward_pipe() || !enc)
	{
		return;
	}

	[enc setFragmentBytes:&view length:sizeof(view) atIndex:6];
	[enc setFragmentTexture:rmb_rtw_out() atIndex:6];
	[enc setFragmentTexture:t_wdist atIndex:7];
}

void
rmb_rtw_view_end(void)
{
	traced = forward = shown = marked = blurred = 0;
}

/* ---- TASK179: the effect table's rows (rm_rtfx.c) ---------------------------------- */

void
rmb_rtw_fx_mark(rmb_rtfx_io_t *io)
{
	rmb_rtw_mark(io->raster, io->surf);
}

/* the reflections, from the resolve's lit result; their blend over the opaque water in a pass of its own */
void
rmb_rtw_fx_post(rmb_rtfx_io_t *io)
{
	if (rmb_rtw_trace(io->target, io->depth, io->normal, io->material) && io->open && io->open(io))
	{
		rmb_rtw_blend(io->enc, io->material);
	}
}

int
rmb_rtw_fx_forward(rmb_rtfx_io_t *io)
{
	id<MTLRenderPipelineState> p = rmb_rtw_forward_pipe();

	if (!p)
	{
		return 0;
	}

	io->pipe = p;
	return 1;
}

void
rmb_rtw_fx_bind(rmb_rtfx_io_t *io)
{
	rmb_rtw_bind_forward(io->enc);
}
