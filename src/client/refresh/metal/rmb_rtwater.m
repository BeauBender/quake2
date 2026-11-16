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
 * TASK58; header/rm_rtwater.h): the kernel's, the blend's, the
 * development view's and the forward pass's translucent pipelines, the
 * kernel's outputs at the render size, the dispatch on the frame's
 * command buffer after the lighting resolve (rmb_gbuf_resolve's hook)
 * over the frame's instance structure (rmb_rt_bind_scene), the map's
 * per-triangle materials (rmb_rt_bind_materials) and its vertex and
 * index buffers (rmb_rt_bind_geometry), the blend over the opaque water,
 * the translucent faces' draw of the forward pass, and the counts and GPU
 * time of each frame's pass for the r_rt_stats 2 line. Every object is
 * counted in the ledger as RM_OBJ_RT.
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

/* rmb_encode.m */
id<MTLCommandBuffer> rmb_frame_compute(unsigned long *frame);

#define RMB_RTW_GROUP 8         /* the kernel's threadgroup side */
#define RMB_RTW_COUNTS 6        /* opaque, translucent, eye rays, lit, sky, none (rm_rtwater.metal) */

_Static_assert(sizeof(rm_rtw_view_t) == 192, "the view is rm_rtwater.metal's 192 bytes");

static int rtw_open;
static id<MTLComputePipelineState> trace_pipe;
static id<MTLRenderPipelineState> blend_pipe, show_pipe, trans_pipe;
static id<MTLTexture> t_refl;           /* the reflection and the class, RGBA8Unorm at the render size */
static id<MTLTexture> t_wdist;          /* the translucent water's distance from the eye, R32Float */
static id<MTLBuffer> counts;            /* RMB_FRAMES_IN_FLIGHT slots of RMB_RTW_COUNTS */
static id<MTLCounterSampleBuffer> samples;
static float turb[MTL_RTW_TURB_SIZE];   /* rm_rtw_turb_table */

/* per slot: the frame whose pass completed (set by its handler), the frame whose counts are awaited */
static atomic_ulong slot_done[RMB_FRAMES_IN_FLIGHT];
static int slot_pending[RMB_FRAMES_IN_FLIGHT];
static int slot_timed[RMB_FRAMES_IN_FLIGHT];
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
		id<MTLCounterSet> ts = nil;

		if (!k)
		{
			snprintf(err, errlen, "the shader library has no rm_rtw_trace");
			return 0;
		}

		RMB_RTW_SET(trace_pipe, [rmb_device newComputePipelineStateWithFunction:k error:&e]);
		RMB_RTW_SET(blend_pipe, trace_pipe ? rmb_rtw_fullscreen(@"rm_fragment_rtw_blend", 1, &e) : nil);
		RMB_RTW_SET(show_pipe, blend_pipe ? rmb_rtw_fullscreen(@"rm_fragment_rtw_show", 0, &e) : nil);
		RMB_RTW_SET(trans_pipe, show_pipe ? rmb_rtw_trans_pipe(&e) : nil);
		RMB_RTW_SET(counts, [rmb_device newBufferWithLength:RMB_FRAMES_IN_FLIGHT * RMB_RTW_COUNTS * sizeof(uint32_t)
				options:MTLResourceStorageModeShared]);

		if (!trace_pipe || !blend_pipe || !show_pipe || !trans_pipe || !counts)
		{
			snprintf(err, errlen, "cannot build the water pipelines%s%s", e ? ": " : "",
					e ? e.localizedDescription.UTF8String : "");
			rmb_rtw_close();
			return 0;
		}

		/* the pass's GPU time, where the device samples timestamps at the encoders' boundaries */
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
			sd.sampleCount = RMB_FRAMES_IN_FLIGHT * 2;
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
		RMB_RTW_SET(counts, nil);
		RMB_RTW_SET(samples, nil);
	}

	rtw_open = 0;
	view_set = traced = forward = shown = 0;
	ndone = 0;
}

int
rmb_rtw_prepare(unsigned long w, unsigned long h)
{
	if (!rtw_open || !trace_pipe || !counts || (w < 1) || (h < 1))
	{
		return 0;
	}

	/* the outputs at the render size, made again when it changes */
	if (t_refl && t_wdist && (t_refl.width == w) && (t_refl.height == h))
	{
		return 1;
	}

	@autoreleasepool
	{
		MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
				width:w height:h mipmapped:NO];

		d.storageMode = MTLStorageModePrivate;
		d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
		RMB_RTW_SET(t_refl, [rmb_device newTextureWithDescriptor:d]);
		d.pixelFormat = MTLPixelFormatR32Float;
		RMB_RTW_SET(t_wdist, [rmb_device newTextureWithDescriptor:d]);

		if (!t_refl || !t_wdist)
		{
			RMB_RTW_SET(t_refl, nil);
			RMB_RTW_SET(t_wdist, nil);
			return 0;
		}

		fprintf(stderr, "ref_metal: rt water %lux%lu: %lu bytes allocated\n", w, h,
				(unsigned long)(t_refl.allocatedSize + t_wdist.allocatedSize));
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

		c = (const uint32_t *)counts.contents + k * RMB_RTW_COUNTS;
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
		s->ms = -1;

		if (slot_timed[k] && samples)
		{
			NSData *d = [samples resolveCounterRange:NSMakeRange((NSUInteger)k * 2, 2)];
			const MTLCounterResultTimestamp *t = d ? (const MTLCounterResultTimestamp *)d.bytes : NULL;

			if (t && (d.length >= 2 * sizeof(*t)) && (t[0].timestamp != MTLCounterErrorValue) &&
				(t[1].timestamp != MTLCounterErrorValue) && (t[1].timestamp >= t[0].timestamp))
			{
				s->ms = rmb_rt_gpu_ms(t[1].timestamp - t[0].timestamp);
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

int
rmb_rtw_trace(id<MTLTexture> target, id<MTLTexture> depth, id<MTLTexture> normal, id<MTLTexture> material)
{
	unsigned long fno = 0;
	id<MTLCommandBuffer> cmd;
	id<MTLComputeCommandEncoder> ce;
	const NSUInteger w = depth.width, h = depth.height;
	int slot, timed;

	traced = forward = shown = 0;

	if (!rtw_open || !view_set || !target || !depth || !normal || !material)
	{
		return 0;
	}

	view_set = 0;
	rmb_rtw_read();

	/* the outputs, made by rmb_rtw_prepare before the raster at this size; the lit result at the same size */
	if (!t_refl || !t_wdist || (t_refl.width != w) || (t_refl.height != h) || (target.width != w) ||
		(target.height != h))
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
		memset((uint32_t *)counts.contents + slot * RMB_RTW_COUNTS, 0, RMB_RTW_COUNTS * sizeof(uint32_t));

		if (timed)
		{
			MTLComputePassDescriptor *cp = [MTLComputePassDescriptor computePassDescriptor];

			cp.sampleBufferAttachments[0].sampleBuffer = samples;
			cp.sampleBufferAttachments[0].startOfEncoderSampleIndex = (NSUInteger)slot * 2;
			cp.sampleBufferAttachments[0].endOfEncoderSampleIndex = (NSUInteger)slot * 2 + 1;
			ce = [cmd computeCommandEncoderWithDescriptor:cp];
		}
		else
		{
			ce = [cmd computeCommandEncoder];
		}

		if (!ce)
		{
			return 0;
		}

		[ce setComputePipelineState:trace_pipe];

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
		[ce setBuffer:counts offset:(NSUInteger)slot * RMB_RTW_COUNTS * sizeof(uint32_t) atIndex:6];
		[ce dispatchThreads:MTLSizeMake(w, h, 1) threadsPerThreadgroup:MTLSizeMake(RMB_RTW_GROUP, RMB_RTW_GROUP, 1)];
		[ce endEncoding];

		slot_pending[slot] = 1;
		slot_timed[slot] = timed;
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
