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
 * development view's, the candidates' and the forward pass's translucent
 * pipelines, the kernel's outputs and the pixel list at the render size,
 * the passes on the frame's command buffer after the lighting resolve
 * (rmb_gbuf_resolve's hook) over the frame's instance structure
 * (rmb_rt_bind_scene), the map's per-triangle materials
 * (rmb_rt_bind_materials) and its vertex and index buffers
 * (rmb_rt_bind_geometry), the blend over the opaque water, the
 * translucent faces' draw of the forward pass, and the counts and GPU
 * time of each frame's passes for the r_rt_stats 2 line. TASK158 adds the
 * candidates' raster of the map's water triangles (rmb_rt_bind_water_raster),
 * the compaction and the indirect trace over its list, and the world-space
 * shading's bindings: the triangles' shading records, the frame's texture
 * table (an argument buffer of the world's images and the sky box's six),
 * the lightmap array and the dynamic lights the lightmaps left out. Every
 * object is counted in the ledger as RM_OBJ_RT.
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

#if !__has_feature(objc_arc)
#error "rmb_rtwater.m is written for ARC: compile it with -fobjc-arc"
#endif

/* rmb_device.m */
extern id<MTLDevice> rmb_device;
extern rm_ledger_t rmb_ledger;
extern rmb_stats_t rmb_counts;

/* rmb_resource.m */
id<MTLLibrary> rmb_library(void);
id<MTLTexture> rmb_texture_for_draw(int tid);

/* rmb_encode.m */
id<MTLCommandBuffer> rmb_frame_compute(unsigned long *frame);

/* rmb_rt.m (TASK158) */
unsigned long rmb_rt_bind_water_raster(id<MTLRenderCommandEncoder> enc, int list_at, int idx_at, int vert_at,
		int geom_at);

#define RMB_RTW_GROUP 8         /* the compaction's threadgroup side */
#define RMB_RTW_SLOT 16         /* uint32s a frame slot of the counts buffer (rm_rtwater.metal's RM_RTW_C_*) */
#define RMB_RTW_C_LISTED 9
#define RMB_RTW_C_ARGS 10
#define RMB_RTW_SAMPLES 6       /* per slot (world space): the raster's start and end, the compaction's, the trace's */

_Static_assert(sizeof(rm_rtw_view_t) == 288, "the view is rm_rtwater.metal's 288 bytes");
_Static_assert(RMB_RTW_C_ARGS * 4 % 4 == 0, "the indirect arguments are 4-byte aligned");

static int rtw_open;
static id<MTLComputePipelineState> trace_pipe;   /* TASK58's kernel over every pixel (r_rt_water_hits 0) */
static id<MTLComputePipelineState> trace_ws_pipe, compact_pipe, args_pipe;   /* TASK158's, over the list */
static int made_ws;                     /* the outputs were made for the world-space path (rmb_rtw_prepare) */
static id<MTLRenderPipelineState> blend_pipe, show_pipe, trans_pipe, cand_pipe;
static id<MTLDepthStencilState> cand_depth;
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

	return [rmb_device newRenderPipelineStateWithDescriptor:d error:e];
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
	return [rmb_device newRenderPipelineStateWithDescriptor:d error:e];
}

/*
 * TASK158: the candidates' raster over the reflection target (RGBA8Unorm)
 * with the G-buffer's depth (Depth32Float, as the forward pass's), tested
 * as gl1 tests (less or equal) and not written.
 */
static id<MTLRenderPipelineState>
rmb_rtw_cand_pipe(NSError **e)
{
	MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];

	d.vertexFunction = [rmb_library() newFunctionWithName:@"rm_vertex_rtw_cand"];
	d.fragmentFunction = [rmb_library() newFunctionWithName:@"rm_fragment_rtw_cand"];

	if (!d.vertexFunction || !d.fragmentFunction)
	{
		return nil;
	}

	d.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
	d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
	return [rmb_device newRenderPipelineStateWithDescriptor:d error:e];
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
		MTLDepthStencilDescriptor *dd = [MTLDepthStencilDescriptor new];
		id<MTLCounterSet> ts = nil;

		if (!k || !kw || !kc || !ka)
		{
			snprintf(err, errlen, "the shader library has no rm_rtw_trace, rm_rtw_trace_ws, rm_rtw_compact or rm_rtw_args");
			return 0;
		}

		dd.depthCompareFunction = MTLCompareFunctionLessEqual;
		dd.depthWriteEnabled = NO;

		RMB_RTW_SET(trace_pipe, [rmb_device newComputePipelineStateWithFunction:k error:&e]);
		RMB_RTW_SET(trace_ws_pipe, trace_pipe ? [rmb_device newComputePipelineStateWithFunction:kw error:&e] : nil);
		RMB_RTW_SET(compact_pipe, trace_ws_pipe ? [rmb_device newComputePipelineStateWithFunction:kc error:&e] : nil);
		RMB_RTW_SET(args_pipe, compact_pipe ? [rmb_device newComputePipelineStateWithFunction:ka error:&e] : nil);
		RMB_RTW_SET(blend_pipe, args_pipe ? rmb_rtw_fullscreen(@"rm_fragment_rtw_blend", 1, &e) : nil);
		RMB_RTW_SET(show_pipe, blend_pipe ? rmb_rtw_fullscreen(@"rm_fragment_rtw_show", 0, &e) : nil);
		RMB_RTW_SET(trans_pipe, show_pipe ? rmb_rtw_trans_pipe(&e) : nil);
		RMB_RTW_SET(cand_pipe, trans_pipe ? rmb_rtw_cand_pipe(&e) : nil);
		RMB_RTW_SET(cand_depth, [rmb_device newDepthStencilStateWithDescriptor:dd]);
		RMB_RTW_SET(counts, [rmb_device newBufferWithLength:RMB_FRAMES_IN_FLIGHT * RMB_RTW_SLOT * sizeof(uint32_t)
				options:MTLResourceStorageModeShared]);
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

		if (!trace_pipe || !trace_ws_pipe || !compact_pipe || !args_pipe || !blend_pipe || !show_pipe || !trans_pipe || !cand_pipe ||
			!cand_depth || !counts || !tex_enc || !tex_ab || !white2d || !white_arr)
		{
			snprintf(err, errlen, "cannot build the water pipelines%s%s", e ? ": " : "",
					e ? e.localizedDescription.UTF8String : "");
			rmb_rtw_close();
			return 0;
		}

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
	view_set = traced = forward = shown = 0;
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
		RMB_RTW_SET(trace_ws_pipe, nil);
		RMB_RTW_SET(compact_pipe, nil);
		RMB_RTW_SET(args_pipe, nil);
		RMB_RTW_SET(blend_pipe, nil);
		RMB_RTW_SET(show_pipe, nil);
		RMB_RTW_SET(trans_pipe, nil);
		RMB_RTW_SET(cand_pipe, nil);
		RMB_RTW_SET(cand_depth, nil);
		RMB_RTW_SET(t_refl, nil);
		RMB_RTW_SET(t_wdist, nil);
		RMB_RTW_SET(list, nil);
		RMB_RTW_SET(counts, nil);
		RMB_RTW_SET(samples, nil);
		RMB_RTW_SET(tex_enc, nil);
		RMB_RTW_SET(tex_ab, nil);
		RMB_RTW_SET(white2d, nil);
		RMB_RTW_SET(white_arr, nil);
		RMB_RTW_SET(shade, nil);
	}

	rtw_open = 0;
	made_ws = 0;
	list_cap = shade_n = 0;
	tex_len = 0;
	view_set = traced = forward = shown = 0;
	tex_n = lm_tid = sky_set = nlights = 0;
	ndone = 0;
}

int
rmb_rtw_prepare(unsigned long w, unsigned long h, int ws)
{
	ws = ws != 0;

	if (!rtw_open || !trace_pipe || !counts || (w < 1) || (h < 1) || (w > 0xffffUL) || (h > 0xffffUL))
	{
		return 0;
	}

	/* the outputs (and on the world-space path the list) at the render size, made again when it or the path changes */
	if (t_refl && t_wdist && (t_refl.width == w) && (t_refl.height == h) && (made_ws == ws) &&
		(!ws || (list && (list_cap == w * h))))
	{
		return 1;
	}

	@autoreleasepool
	{
		MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
				width:w height:h mipmapped:NO];

		/* TASK158: on the world-space path the reflection target is also the candidates' raster's colour attachment */
		d.storageMode = MTLStorageModePrivate;
		d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite | (ws ? MTLTextureUsageRenderTarget : 0);
		RMB_RTW_SET(t_refl, [rmb_device newTextureWithDescriptor:d]);
		d.pixelFormat = MTLPixelFormatR32Float;
		d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
		RMB_RTW_SET(t_wdist, [rmb_device newTextureWithDescriptor:d]);
		RMB_RTW_SET(list, ws ? [rmb_device newBufferWithLength:w * h * sizeof(uint32_t)
				options:MTLResourceStorageModePrivate] : nil);
		list_cap = list ? w * h : 0;
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

	return 1;
}

void
rmb_rtw_set_view(const rm_rtw_view_t *v)
{
	view_set = shown = 0;

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
		s->listed = slot_ws[k] ? c[RMB_RTW_C_LISTED] : slot_pixels[k];   /* TASK58's path traces every pixel */
		s->ms = s->raster_ms = s->compact_ms = s->trace_ms = s->span_ms = -1;

		/*
		 * TASK58's path: its compute encoder. The world-space path (TASK158):
		 * the candidates' raster, the compaction's encoder (with the
		 * arguments) and the trace's, each on its own, and the whole span.
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
				s->raster_ms = rmb_rtw_span(t, n, 0, 1);
				s->compact_ms = rmb_rtw_span(t, n, 2, 3);
				s->trace_ms = rmb_rtw_span(t, n, 4, 5);
				s->span_ms = rmb_rtw_span(t, n, 0, 5);

				if ((s->raster_ms >= 0) && (s->compact_ms >= 0) && (s->trace_ms >= 0))
				{
					s->ms = s->raster_ms + s->compact_ms + s->trace_ms;
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
 * TASK158: the candidates' raster on cmd: the map's water triangles over
 * the reflection target, cleared to 0 first (so every pixel the trace
 * does not list reads as no water), against the G-buffer's depth. 1 when
 * the pass was encoded (with no triangle it only clears).
 */
static int
rmb_rtw_cand(id<MTLCommandBuffer> cmd, id<MTLTexture> depth, int slot, int timed)
{
	MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
	id<MTLRenderCommandEncoder> re;
	MTLViewport vp;
	unsigned long nw;

	rp.colorAttachments[0].texture = t_refl;
	rp.colorAttachments[0].loadAction = MTLLoadActionClear;
	rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
	rp.colorAttachments[0].storeAction = MTLStoreActionStore;
	rp.depthAttachment.texture = depth;
	rp.depthAttachment.loadAction = MTLLoadActionLoad;
	rp.depthAttachment.storeAction = MTLStoreActionStore;

	if (timed)
	{
		rp.sampleBufferAttachments[0].sampleBuffer = samples;
		rp.sampleBufferAttachments[0].startOfVertexSampleIndex = (NSUInteger)slot * RMB_RTW_SAMPLES;
		rp.sampleBufferAttachments[0].endOfVertexSampleIndex = MTLCounterDontSample;
		rp.sampleBufferAttachments[0].startOfFragmentSampleIndex = MTLCounterDontSample;
		rp.sampleBufferAttachments[0].endOfFragmentSampleIndex = (NSUInteger)slot * RMB_RTW_SAMPLES + 1;
	}

	re = [cmd renderCommandEncoderWithDescriptor:rp];

	if (!re)
	{
		return 0;
	}

	vp.originX = view.vp[0];
	vp.originY = view.vp[1];
	vp.width = view.vp[2];
	vp.height = view.vp[3];
	vp.znear = 0.0;
	vp.zfar = 1.0;
	[re setRenderPipelineState:cand_pipe];
	[re setDepthStencilState:cand_depth];
	[re setCullMode:MTLCullModeNone];
	[re setViewport:vp];
	nw = rmb_rt_bind_water_raster(re, 0, 1, 2, 3);

	if (nw)
	{
		[re setVertexBytes:&view length:sizeof(view) atIndex:4];
		[re drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:nw * 3];
		rmb_counts.draws++;
	}

	[re endEncoding];
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

/* r_rt_water_hits 0: TASK58's pass, one compute encoder, its kernel over every pixel */
static int
rmb_rtw_trace_ss(id<MTLCommandBuffer> cmd, unsigned long fno, int slot, int timed, id<MTLTexture> target,
		id<MTLTexture> depth, id<MTLTexture> normal, id<MTLTexture> material)
{
	const NSUInteger w = depth.width, h = depth.height;
	id<MTLComputeCommandEncoder> ce = rmb_rtw_encoder(cmd, timed, slot, 0);

	if (!ce)
	{
		return 0;
	}

	[ce setComputePipelineState:trace_pipe];

	if (!rmb_rtw_bind_trace(ce, fno, slot, target, depth, normal, material))
	{
		return 0;
	}

	[ce dispatchThreads:MTLSizeMake(w, h, 1) threadsPerThreadgroup:MTLSizeMake(RMB_RTW_GROUP, RMB_RTW_GROUP, 1)];
	[ce endEncoding];
	return 1;
}

/*
 * r_rt_water_hits 1 (TASK158): the candidates' raster, the compaction and
 * the arguments in one compute encoder, the trace over the list in
 * another; each timed on its own (RMB_RTW_SAMPLES).
 */
static int
rmb_rtw_trace_ws(id<MTLCommandBuffer> cmd, unsigned long fno, int slot, int timed, id<MTLTexture> target,
		id<MTLTexture> depth, id<MTLTexture> normal, id<MTLTexture> material)
{
	const NSUInteger w = depth.width, h = depth.height;
	const uint32_t cap = (uint32_t)list_cap;
	const uint32_t ns = (uint32_t)shade_n;
	id<MTLComputeCommandEncoder> ce;

	/* the water triangles in front of the depth mark their pixels */
	if (!rmb_rtw_cand(cmd, depth, slot, timed))
	{
		return 0;
	}

	ce = rmb_rtw_encoder(cmd, timed, slot, 2);

	if (!ce)
	{
		return 0;
	}

	/* the compaction over every pixel, then the list's threadgroups (one serial encoder: each sees the last) */
	[ce setComputePipelineState:compact_pipe];
	[ce setTexture:material atIndex:0];
	[ce setTexture:t_refl atIndex:1];
	[ce setBytes:&view length:sizeof(view) atIndex:0];
	[ce setBuffer:list offset:0 atIndex:1];
	[ce setBuffer:counts offset:(NSUInteger)slot * RMB_RTW_SLOT * sizeof(uint32_t) atIndex:2];
	[ce setBytes:&cap length:sizeof(cap) atIndex:3];
	[ce dispatchThreads:MTLSizeMake(w, h, 1) threadsPerThreadgroup:MTLSizeMake(RMB_RTW_GROUP, RMB_RTW_GROUP, 1)];

	[ce setComputePipelineState:args_pipe];
	[ce setBuffer:counts offset:(NSUInteger)slot * RMB_RTW_SLOT * sizeof(uint32_t) atIndex:0];
	[ce setBytes:&cap length:sizeof(cap) atIndex:1];
	[ce dispatchThreads:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
	[ce endEncoding];

	/* the trace in an encoder of its own (the list and the arguments are tracked: it waits for them) */
	ce = rmb_rtw_encoder(cmd, timed, slot, 4);

	if (!ce)
	{
		return 0;
	}

	[ce setComputePipelineState:trace_ws_pipe];

	if (!rmb_rtw_bind_trace(ce, fno, slot, target, depth, normal, material))
	{
		return 0;
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

	traced = forward = shown = 0;

	if (!rtw_open || !view_set || !target || !depth || !normal || !material)
	{
		return 0;
	}

	view_set = 0;
	rmb_rtw_read();

	/*
	 * The outputs, made by rmb_rtw_prepare before the raster at this size
	 * for this view's path (and on the world-space path the list, and the
	 * depth the candidates' raster tests); the lit result at the same size.
	 */
	if (!t_refl || !t_wdist || (t_refl.width != w) || (t_refl.height != h) || (target.width != w) ||
		(target.height != h))
	{
		return 0;
	}

	if (ws && (!list || (list_cap != w * h) || (depth.pixelFormat != MTLPixelFormatDepth32Float)))
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

		slot_pending[slot] = 1;
		slot_timed[slot] = timed;
		slot_ws[slot] = ws;
		slot_frame[slot] = fno;
		slot_pixels[slot] = (unsigned long)(w * h);
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

	[enc setRenderPipelineState:view.show ? show_pipe : blend_pipe];
	[enc setFragmentBytes:&view length:sizeof(view) atIndex:0];
	[enc setFragmentTexture:t_refl atIndex:0];
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
	[enc setFragmentTexture:t_refl atIndex:6];
	[enc setFragmentTexture:t_wdist atIndex:7];
}

void
rmb_rtw_view_end(void)
{
	traced = forward = shown = 0;
}
