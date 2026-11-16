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
 * The Metal back end of ref_metal's ray-traced ambient occlusion (RT5,
 * TASK57; header/rm_rtao.h): the kernel's and r_rt_ao_show's pipelines,
 * the blue-noise tile, the kernel's outputs at the render size (the AO
 * and the light the resolve reads), the dispatch on the frame's command
 * buffer between the G-buffer raster and the resolve (rmb_gbuf_resolve's
 * hook) over the frame's instance structure (rmb_rt_bind_scene) and the
 * map's per-triangle materials (rmb_rt_bind_materials), and the counts
 * and GPU time of each frame's pass for the r_rt_stats 2 line. Every
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
#include "header/rm_rtao.h"

#if !__has_feature(objc_arc)
#error "rmb_rtao.m is written for ARC: compile it with -fobjc-arc"
#endif

/* rmb_device.m */
extern id<MTLDevice> rmb_device;
extern rm_ledger_t rmb_ledger;
extern rmb_stats_t rmb_counts;

/* rmb_resource.m */
id<MTLLibrary> rmb_library(void);

/* rmb_encode.m */
id<MTLCommandBuffer> rmb_frame_compute(unsigned long *frame);

#define RMB_RTAO_GROUP 8        /* the kernel's threadgroup side */
#define RMB_RTAO_COUNTS 7       /* traced, occluded, at the floor, up, up and occluded, rays, hits (rm_rtao.metal) */

_Static_assert(sizeof(rm_rtao_view_t) == 128, "the view is rm_rtao.metal's 128 bytes");

static int ao_open;
static id<MTLComputePipelineState> trace_pipe;
static id<MTLComputePipelineState> lo_pipe;     /* r_rt_ao_scale below 1 (TASK157): the reduced-size rays */
static id<MTLComputePipelineState> up_pipe;     /* and their upsample to the render size */
static id<MTLRenderPipelineState> show_pipe;
static id<MTLBuffer> tile;              /* the blue-noise ranks, MTL_RTAO_TILE_N ushorts */
static id<MTLTexture> t_ao;             /* the AO, R8Unorm at the render size */
static id<MTLTexture> t_light;          /* the light the resolve reads, RGBA16Unorm at the render size */
static id<MTLTexture> t_ao_lo;          /* the traced AO at the reduced size, R8Unorm (TASK157); nil at div 1 */
static id<MTLBuffer> counts;            /* RMB_FRAMES_IN_FLIGHT slots of RMB_RTAO_COUNTS */
static id<MTLCounterSampleBuffer> samples;

/* per slot: the frame whose pass completed (set by its handler), the frame whose counts are awaited */
static atomic_ulong slot_done[RMB_FRAMES_IN_FLIGHT];
static int slot_pending[RMB_FRAMES_IN_FLIGHT];
static int slot_timed[RMB_FRAMES_IN_FLIGHT];
static unsigned long slot_frame[RMB_FRAMES_IN_FLIGHT], slot_pixels[RMB_FRAMES_IN_FLIGHT];
static rm_rtao_stats_t done[RMB_FRAMES_IN_FLIGHT];
static int ndone;

/* the view the next G-buffer view traces with, and whether this one traced */
static rm_rtao_view_t view;
static int view_set, traced;

/* Sets a strong slot to obj (nil releases it), counting both in the ledger as RM_OBJ_RT. */
#define RMB_RTAO_SET(slot, obj) \
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

int
rmb_rtao_open(char *err, size_t errlen)
{
	if (ao_open)
	{
		return 1;
	}

	if (!rmb_device || !rmb_device.supportsRaytracing || !rmb_library())
	{
		snprintf(err, errlen, "%s", !rmb_device ? "no Metal device" :
				!rmb_library() ? "no shader library" : "the device has no ray tracing");
		return 0;
	}

	@autoreleasepool
	{
		NSError *e = nil;
		id<MTLFunction> k = [rmb_library() newFunctionWithName:@"rm_rtao_trace"];
		id<MTLFunction> klo = [rmb_library() newFunctionWithName:@"rm_rtao_trace_lo"];
		id<MTLFunction> kup = [rmb_library() newFunctionWithName:@"rm_rtao_upsample"];
		MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
		id<MTLCounterSet> ts = nil;
		unsigned short *ranks = malloc(MTL_RTAO_TILE_N * sizeof(*ranks));

		d.vertexFunction = [rmb_library() newFunctionWithName:@"rm_vertex_fullscreen"];
		d.fragmentFunction = [rmb_library() newFunctionWithName:@"rm_fragment_rtao_show"];
		d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

		if (!k || !klo || !kup || !d.vertexFunction || !d.fragmentFunction)
		{
			free(ranks);
			snprintf(err, errlen, "the shader library has no rm_rtao_trace, rm_rtao_trace_lo, rm_rtao_upsample or rm_fragment_rtao_show");
			return 0;
		}

		/* the tile, made in code (rm_rtao_bluenoise) */
		if (!ranks || !rm_rtao_bluenoise(ranks))
		{
			free(ranks);
			snprintf(err, errlen, "cannot make the blue-noise tile");
			return 0;
		}

		RMB_RTAO_SET(tile, [rmb_device newBufferWithBytes:ranks length:MTL_RTAO_TILE_N * sizeof(*ranks)
				options:MTLResourceStorageModeShared]);
		free(ranks);
		RMB_RTAO_SET(trace_pipe, [rmb_device newComputePipelineStateWithFunction:k error:&e]);
		RMB_RTAO_SET(lo_pipe, trace_pipe ? [rmb_device newComputePipelineStateWithFunction:klo error:&e] : nil);
		RMB_RTAO_SET(up_pipe, lo_pipe ? [rmb_device newComputePipelineStateWithFunction:kup error:&e] : nil);
		RMB_RTAO_SET(show_pipe, up_pipe ? [rmb_device newRenderPipelineStateWithDescriptor:d error:&e] : nil);
		RMB_RTAO_SET(counts, [rmb_device newBufferWithLength:RMB_FRAMES_IN_FLIGHT * RMB_RTAO_COUNTS * sizeof(uint32_t)
				options:MTLResourceStorageModeShared]);

		if (!tile || !trace_pipe || !lo_pipe || !up_pipe || !show_pipe || !counts)
		{
			snprintf(err, errlen, "cannot build the AO pipelines%s%s", e ? ": " : "",
					e ? e.localizedDescription.UTF8String : "");
			rmb_rtao_close();
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
			RMB_RTAO_SET(samples, [rmb_device newCounterSampleBufferWithDescriptor:sd error:&e]);
		}
	}

	for (int i = 0; i < RMB_FRAMES_IN_FLIGHT; i++)
	{
		atomic_store(&slot_done[i], ~0UL);   /* no frame */
		slot_pending[i] = 0;
	}

	ndone = 0;
	view_set = traced = 0;
	ao_open = 1;
	return 1;
}

/* The frames still in flight keep what they use: a command buffer retains its resources. */
void
rmb_rtao_close(void)
{
	@autoreleasepool
	{
		RMB_RTAO_SET(trace_pipe, nil);
		RMB_RTAO_SET(lo_pipe, nil);
		RMB_RTAO_SET(up_pipe, nil);
		RMB_RTAO_SET(show_pipe, nil);
		RMB_RTAO_SET(tile, nil);
		RMB_RTAO_SET(t_ao, nil);
		RMB_RTAO_SET(t_light, nil);
		RMB_RTAO_SET(t_ao_lo, nil);
		RMB_RTAO_SET(counts, nil);
		RMB_RTAO_SET(samples, nil);
	}

	ao_open = 0;
	view_set = traced = 0;
	ndone = 0;
}

/* the traced AO at the reduced size of div (TASK157): made again when that size changes, released at div 1 */
static int
rmb_rtao_prepare_lo(unsigned long w, unsigned long h, unsigned div)
{
	const unsigned long lw = mtl_rtao_lo_size((unsigned)w, div), lh = mtl_rtao_lo_size((unsigned)h, div);

	if (div <= 1)
	{
		RMB_RTAO_SET(t_ao_lo, nil);
		return 1;
	}

	if (t_ao_lo && (t_ao_lo.width == lw) && (t_ao_lo.height == lh))
	{
		return 1;
	}

	@autoreleasepool
	{
		MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
				width:lw height:lh mipmapped:NO];

		d.storageMode = MTLStorageModePrivate;
		d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
		RMB_RTAO_SET(t_ao_lo, [rmb_device newTextureWithDescriptor:d]);

		if (!t_ao_lo)
		{
			return 0;
		}

		fprintf(stderr, "ref_metal: rt ao traced at %lux%lu (1/%u): %lu bytes allocated\n", lw, lh, div,
				(unsigned long)t_ao_lo.allocatedSize);
	}

	return 1;
}

int
rmb_rtao_prepare(unsigned long w, unsigned long h, unsigned div)
{
	if (!ao_open || !trace_pipe || !lo_pipe || !up_pipe || !counts || (w < 1) || (h < 1) || (w > 0xffffffffUL) ||
		(h > 0xffffffffUL) || (div < 1) || (div > MTL_RTAO_DIV_MAX))
	{
		return 0;
	}

	if (!rmb_rtao_prepare_lo(w, h, div))
	{
		return 0;
	}

	/* the outputs at the render size, made again when it changes */
	if (t_ao && t_light && (t_ao.width == w) && (t_ao.height == h))
	{
		return 1;
	}

	@autoreleasepool
	{
		MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
				width:w height:h mipmapped:NO];

		d.storageMode = MTLStorageModePrivate;
		d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
		RMB_RTAO_SET(t_ao, [rmb_device newTextureWithDescriptor:d]);
		d.pixelFormat = MTLPixelFormatRGBA16Unorm;
		RMB_RTAO_SET(t_light, [rmb_device newTextureWithDescriptor:d]);

		if (!t_ao || !t_light)
		{
			RMB_RTAO_SET(t_ao, nil);
			RMB_RTAO_SET(t_light, nil);
			return 0;
		}

		fprintf(stderr, "ref_metal: rt ao %lux%lu: %lu bytes allocated\n", w, h,
				(unsigned long)(t_ao.allocatedSize + t_light.allocatedSize));
	}

	return 1;
}

void
rmb_rtao_set_view(const rm_rtao_view_t *v)
{
	view_set = 0;

	if (!ao_open || !v)
	{
		return;
	}

	view = *v;
	view_set = 1;
}

/* the counts and times of the slots whose frames completed since the last read */
static void
rmb_rtao_read(void)
{
	int k;

	for (k = 0; k < RMB_FRAMES_IN_FLIGHT; k++)
	{
		const uint32_t *c;
		rm_rtao_stats_t *s;

		if (!slot_pending[k] || (atomic_load(&slot_done[k]) != slot_frame[k]))
		{
			continue;
		}

		slot_pending[k] = 0;

		if (ndone >= RMB_FRAMES_IN_FLIGHT)
		{
			continue;
		}

		c = (const uint32_t *)counts.contents + k * RMB_RTAO_COUNTS;
		s = &done[ndone++];
		memset(s, 0, sizeof(*s));
		s->frame = slot_frame[k];
		s->pixels = slot_pixels[k];
		s->traced = c[0];
		s->occluded = c[1];
		s->floored = c[2];
		s->up = c[3];
		s->up_occluded = c[4];
		s->rays = c[5];
		s->hits = c[6];
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
rmb_rtao_stats(rm_rtao_stats_t *s, int cap)
{
	int i, n;

	if (!ao_open)
	{
		return 0;
	}

	rmb_rtao_read();
	n = (ndone < cap) ? ndone : cap;

	for (i = 0; i < n; i++)
	{
		s[i] = done[i];
	}

	ndone = 0;
	return n;
}

id<MTLTexture>
rmb_rtao_trace(id<MTLTexture> depth, id<MTLTexture> normal, id<MTLTexture> material, id<MTLTexture> light)
{
	unsigned long fno = 0;
	id<MTLCommandBuffer> cmd;
	id<MTLComputeCommandEncoder> ce;
	const NSUInteger w = depth.width, h = depth.height;
	int slot, timed;

	traced = 0;

	if (!ao_open || !view_set || !depth || !normal || !material || !light)
	{
		return nil;
	}

	view_set = 0;
	rmb_rtao_read();

	/* the outputs, made by rmb_rtao_prepare before the raster at this size (and the reduced one) */
	if (!t_ao || !t_light || (t_ao.width != w) || (t_ao.height != h) || (light.width != w) || (light.height != h))
	{
		return nil;
	}

	if ((view.div > 1) && (!t_ao_lo || (t_ao_lo.width != mtl_rtao_lo_size((unsigned)w, view.div)) ||
		(t_ao_lo.height != mtl_rtao_lo_size((unsigned)h, view.div))))
	{
		return nil;
	}

	@autoreleasepool
	{
		/* ends the raster (or RT3's kernel): the kernel reads what it wrote */
		cmd = rmb_frame_compute(&fno);

		if (!cmd)
		{
			return nil;
		}

		slot = (int)(fno % RMB_FRAMES_IN_FLIGHT);
		timed = (samples != nil);

		/*
		 * The frame that used this slot before has completed on the GPU (the
		 * frame semaphore), though its handler may not have run yet: its
		 * counts are dropped, and its handler, naming its own frame, does not
		 * mark this one done.
		 */
		memset((uint32_t *)counts.contents + slot * RMB_RTAO_COUNTS, 0, RMB_RTAO_COUNTS * sizeof(uint32_t));

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
			return nil;
		}

		/* every pixel traced (TASK57), or the rays at the reduced size first (TASK157) */
		[ce setComputePipelineState:(view.div > 1) ? lo_pipe : trace_pipe];

		/* the scene; its transmission and count go to slots 7 and 8, which the kernel does not read */
		if (!rmb_rt_bind_scene(ce, fno, 2, 3, 7, 8))
		{
			[ce endEncoding];
			return nil;
		}

		/* the map's material ids; without them (0 triangles) every map triangle occludes */
		if (!rmb_rt_bind_materials(ce, 4, 5))
		{
			const uint32_t none = 0;

			[ce setBuffer:counts offset:0 atIndex:4];
			[ce setBytes:&none length:sizeof(none) atIndex:5];
		}

		[ce setTexture:depth atIndex:0];
		[ce setTexture:normal atIndex:1];
		[ce setTexture:material atIndex:2];
		[ce setTexture:light atIndex:3];
		[ce setTexture:t_ao atIndex:4];
		[ce setTexture:t_light atIndex:5];
		[ce setBytes:&view length:sizeof(view) atIndex:0];
		[ce setBuffer:tile offset:0 atIndex:1];
		[ce setBuffer:counts offset:(NSUInteger)slot * RMB_RTAO_COUNTS * sizeof(uint32_t) atIndex:6];

		if (view.div > 1)
		{
			/*
			 * TASK157: the rays at the reduced size, then their upsample at the
			 * render size, which reads what they wrote: a serial encoder runs
			 * its dispatches in order.
			 */
			[ce setTexture:t_ao_lo atIndex:6];
			[ce dispatchThreads:MTLSizeMake(t_ao_lo.width, t_ao_lo.height, 1)
					threadsPerThreadgroup:MTLSizeMake(RMB_RTAO_GROUP, RMB_RTAO_GROUP, 1)];
			[ce memoryBarrierWithScope:MTLBarrierScopeTextures];
			[ce setComputePipelineState:up_pipe];
		}

		[ce dispatchThreads:MTLSizeMake(w, h, 1) threadsPerThreadgroup:MTLSizeMake(RMB_RTAO_GROUP, RMB_RTAO_GROUP, 1)];
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
	return t_light;
}

int
rmb_rtao_show(id<MTLRenderCommandEncoder> enc, id<MTLTexture> material)
{
	const int did = traced && t_ao;

	traced = 0;

	if (!ao_open || !enc || !show_pipe || !did || !view.show || !material)
	{
		return 0;
	}

	[enc setRenderPipelineState:show_pipe];
	[enc setFragmentBytes:&view length:sizeof(view) atIndex:0];
	[enc setFragmentTexture:t_ao atIndex:0];
	[enc setFragmentTexture:material atIndex:1];
	[enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
	rmb_counts.draws++;
	return 1;
}
