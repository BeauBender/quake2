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
 * The Metal back end of RT-primary's direct light (TASK188, PM6 slice 1;
 * header/rm_rtdir.h): the kernel's pipeline and the development view's,
 * the map's lights in a buffer, the outputs at the render size (the
 * visibility, RGBA8Unorm, and the light the resolve reads, RGBA16Unorm),
 * the dispatch on the frame's command buffer first among the effects'
 * kernels (rmb_gbuf_resolve's trace walk), the frame's counts and the
 * pass's GPU time, read once the frame completed, in the sun's shape
 * (rmb_rtsun.m). Every object is counted in the ledger as RM_OBJ_RT.
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
#include "header/rm_rtdir.h"
#include "header/rmb_internal.h"

#if !__has_feature(objc_arc)
#error "rmb_rtdir.m is written for ARC: compile it with -fobjc-arc"
#endif

/* rmb_device.m */
extern id<MTLDevice> rmb_device;
extern rm_ledger_t rmb_ledger;
extern rmb_stats_t rmb_counts;

#define RMB_RTDIR_GROUP 8       /* the kernel's threadgroup side */
/* world, in range, rays, lit (their low words), the fit's lightmap and raw sums and samples, the three high words, capped */
#define RMB_RTDIR_COUNTS 11

_Static_assert(sizeof(rm_rtdir_view_t) == 112, "the view is rm_rtdir.metal's 112 bytes");
_Static_assert(sizeof(rm_rtdir_light_t) == 48, "a light is rm_rtdir.metal's 48 bytes");

static int dir_open;
static id<MTLComputePipelineState> trace_pipe;
static id<MTLRenderPipelineState> show_pipe;
static id<MTLTexture> t_vis;            /* the visibility, RGBA8Unorm at the render size */
static id<MTLTexture> t_light;          /* the light the resolve reads, RGBA16Unorm at the render size */
static id<MTLBuffer> lights;            /* the map's lights, at least one record long */
static unsigned long nlights;
static id<MTLBuffer> counts;            /* RMB_FRAMES_IN_FLIGHT slots of RMB_RTDIR_COUNTS */
static id<MTLCounterSampleBuffer> samples;

/* per slot: the frame whose pass completed (set by its handler), the frame whose counts are awaited */
static atomic_ulong slot_done[RMB_FRAMES_IN_FLIGHT];
static int slot_pending[RMB_FRAMES_IN_FLIGHT];
static int slot_timed[RMB_FRAMES_IN_FLIGHT];
static unsigned long slot_frame[RMB_FRAMES_IN_FLIGHT], slot_pixels[RMB_FRAMES_IN_FLIGHT];
static float slot_exposure[RMB_FRAMES_IN_FLIGHT];
static rm_rtdir_stats_t done[RMB_FRAMES_IN_FLIGHT];
static int ndone;
static unsigned long last_frame;        /* the frame of the last pass encoded, 0 for none since the open */

/* the view and styles the next G-buffer view traces with, and whether this one traced */
static rm_rtdir_view_t view;
static float styles[3 * RM_RTL_STYLES];
static int view_set, traced;

/* Sets a strong slot to obj (nil releases it), counting both in the ledger as RM_OBJ_RT. */
#define RMB_RTDIR_SET(slot, obj) \
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
rmb_rtdir_open(char *err, size_t errlen)
{
	if (dir_open)
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
		id<MTLFunction> k = [rmb_library() newFunctionWithName:@"rm_rtdir_trace"];
		MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
		id<MTLCounterSet> ts = nil;

		d.vertexFunction = [rmb_library() newFunctionWithName:@"rm_vertex_fullscreen"];
		d.fragmentFunction = [rmb_library() newFunctionWithName:@"rm_fragment_rtdir_show"];
		d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

		if (!k || !d.vertexFunction || !d.fragmentFunction)
		{
			snprintf(err, errlen, "the shader library has no rm_rtdir_trace or rm_fragment_rtdir_show");
			return 0;
		}

		RMB_RTDIR_SET(trace_pipe, [rmb_device newComputePipelineStateWithFunction:k error:&e]);
		RMB_RTDIR_SET(show_pipe, trace_pipe ? rmb_new_render_pipe(d, &e) : nil);
		RMB_RTDIR_SET(counts, [rmb_device newBufferWithLength:RMB_FRAMES_IN_FLIGHT * RMB_RTDIR_COUNTS * sizeof(uint32_t)
				options:MTLResourceStorageModeShared]);

		if (!trace_pipe || !show_pipe || !counts)
		{
			snprintf(err, errlen, "cannot build the direct light's pipelines%s%s", e ? ": " : "",
					e ? e.localizedDescription.UTF8String : "");
			rmb_rtdir_close();
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
			RMB_RTDIR_SET(samples, [rmb_device newCounterSampleBufferWithDescriptor:sd error:&e]);
		}
	}

	for (int i = 0; i < RMB_FRAMES_IN_FLIGHT; i++)
	{
		atomic_store(&slot_done[i], ~0UL);   /* no frame */
		slot_pending[i] = 0;
	}

	ndone = 0;
	nlights = 0;
	last_frame = 0;
	view_set = traced = 0;
	dir_open = 1;
	return 1;
}

/* The frames still in flight keep what they use: a command buffer retains its resources. */
void
rmb_rtdir_close(void)
{
	@autoreleasepool
	{
		RMB_RTDIR_SET(trace_pipe, nil);
		RMB_RTDIR_SET(show_pipe, nil);
		RMB_RTDIR_SET(t_vis, nil);
		RMB_RTDIR_SET(t_light, nil);
		RMB_RTDIR_SET(lights, nil);
		RMB_RTDIR_SET(counts, nil);
		RMB_RTDIR_SET(samples, nil);
	}

	dir_open = 0;
	nlights = 0;
	last_frame = 0;
	view_set = traced = 0;
	ndone = 0;
}

int
rmb_rtdir_set_lights(const rm_rtdir_light_t *l, long n)
{
	const size_t one = sizeof(rm_rtdir_light_t);

	nlights = 0;

	/* the map's lights and its emissive faces (TASK188 slice 2) */
	if (!dir_open || (n < 0) || (n > RM_RTDIR_MAX_UPLOAD) || ((n > 0) && !l))
	{
		return 0;
	}

	@autoreleasepool
	{
		/* a new buffer a map: a frame in flight keeps the old one; one record at least, so index 9 is bound */
		RMB_RTDIR_SET(lights, [rmb_device newBufferWithLength:(n > 0 ? (size_t)n : 1) * one
				options:MTLResourceStorageModeShared]);

		if (!lights)
		{
			return 0;
		}

		memset(lights.contents, 0, one);

		if (n > 0)
		{
			memcpy(lights.contents, l, (size_t)n * one);
		}
	}

	nlights = (unsigned long)n;
	return 1;
}

int
rmb_rtdir_prepare(unsigned long w, unsigned long h)
{
	if (!dir_open || !trace_pipe || !counts || !lights || (w < 1) || (h < 1) || (w > 0xffffffffUL) ||
		(h > 0xffffffffUL))
	{
		return 0;
	}

	/* the outputs at the render size, made again when it changes */
	if (t_vis && t_light && (t_vis.width == w) && (t_vis.height == h))
	{
		return 1;
	}

	@autoreleasepool
	{
		MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
				width:w height:h mipmapped:NO];

		d.storageMode = MTLStorageModePrivate;
		d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
		RMB_RTDIR_SET(t_vis, [rmb_device newTextureWithDescriptor:d]);
		d.pixelFormat = MTLPixelFormatRGBA16Unorm;
		RMB_RTDIR_SET(t_light, [rmb_device newTextureWithDescriptor:d]);

		if (!t_vis || !t_light)
		{
			RMB_RTDIR_SET(t_vis, nil);
			RMB_RTDIR_SET(t_light, nil);
			return 0;
		}

		fprintf(stderr, "ref_metal: rt direct %lux%lu: %lu bytes allocated\n", w, h,
				(unsigned long)(t_vis.allocatedSize + t_light.allocatedSize));
	}

	return 1;
}

void
rmb_rtdir_set_view(const rm_rtdir_view_t *v, const float *st)
{
	view_set = 0;

	if (!dir_open || !v || !st)
	{
		return;
	}

	view = *v;

	/* the kernel reads no light past the buffer */
	if (view.nlights > nlights)
	{
		view.nlights = (unsigned)nlights;
	}

	memcpy(styles, st, sizeof(styles));
	view_set = 1;
}

/* the counts and times of the slots whose frames completed since the last read */
static void
rmb_rtdir_read(void)
{
	int k;

	for (k = 0; k < RMB_FRAMES_IN_FLIGHT; k++)
	{
		const uint32_t *c;
		rm_rtdir_stats_t *s;

		if (!slot_pending[k] || (atomic_load(&slot_done[k]) != slot_frame[k]))
		{
			continue;
		}

		slot_pending[k] = 0;

		if (ndone >= RMB_FRAMES_IN_FLIGHT)
		{
			continue;
		}

		c = (const uint32_t *)counts.contents + k * RMB_RTDIR_COUNTS;
		s = &done[ndone++];
		memset(s, 0, sizeof(*s));
		s->frame = slot_frame[k];
		s->pixels = slot_pixels[k];
		s->world = c[0];
		s->inrange = (uint64_t)c[1] | ((uint64_t)c[7] << 32);
		s->rays = (uint64_t)c[2] | ((uint64_t)c[8] << 32);
		s->lit = (uint64_t)c[3] | ((uint64_t)c[9] << 32);
		s->fit_lm = c[4];
		s->fit_raw = c[5];
		s->fit_n = c[6];
		s->capped = c[10];
		s->exposure = slot_exposure[k];
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
rmb_rtdir_stats(rm_rtdir_stats_t *s, int cap)
{
	int i, n;

	if (!dir_open)
	{
		return 0;
	}

	rmb_rtdir_read();
	n = (ndone < cap) ? ndone : cap;

	for (i = 0; i < n; i++)
	{
		s[i] = done[i];
	}

	ndone = 0;
	return n;
}

unsigned long
rmb_rtdir_last_frame(void)
{
	return dir_open ? last_frame : 0;
}

/*
 * The kernel on the frame's command buffer over the render-size depth,
 * normal and material attachments, the raster's light (lm, read for the
 * fit's sums only) and the light the resolve would read (base, kept on
 * every pixel that is not a traced world pixel); out, the light with the
 * world's lightmap replaced by the direct light, or nil when nothing was
 * traced.
 */
static id<MTLTexture>
rmb_rtdir_trace(id<MTLTexture> depth, id<MTLTexture> normal, id<MTLTexture> material, id<MTLTexture> lm,
		id<MTLTexture> base)
{
	unsigned long fno = 0;
	id<MTLCommandBuffer> cmd;
	id<MTLComputeCommandEncoder> ce;
	const NSUInteger w = depth.width, h = depth.height;
	int slot, timed;

	traced = 0;

	if (!dir_open || !view_set || !depth || !normal || !material || !lm || !base || !lights)
	{
		return nil;
	}

	view_set = 0;
	rmb_rtdir_read();

	/* the outputs, made by rmb_rtdir_prepare before the raster at this size */
	if (!t_vis || !t_light || (t_vis.width != w) || (t_vis.height != h) || (lm.width != w) || (lm.height != h) ||
		(base.width != w) || (base.height != h))
	{
		return nil;
	}

	@autoreleasepool
	{
		/* ends the raster (or the kernel before this one): the kernel reads what it wrote */
		cmd = rmb_frame_compute(&fno);

		if (!cmd)
		{
			return nil;
		}

		slot = (int)(fno % RMB_FRAMES_IN_FLIGHT);
		timed = (samples != nil);

		/* the frame that used this slot before has completed on the GPU (the frame semaphore); see rmb_rtsun.m */
		memset((uint32_t *)counts.contents + slot * RMB_RTDIR_COUNTS, 0, RMB_RTDIR_COUNTS * sizeof(uint32_t));

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

		[ce setComputePipelineState:trace_pipe];

		/* the scene; its transmission and count go to slots 7 and 8, which the kernel does not read */
		if (!rmb_rt_bind_scene(ce, fno, 2, 3, 7, 8))
		{
			[ce endEncoding];
			return nil;
		}

		/* the map's material ids; without them (0 triangles) every map triangle shadows */
		if (!rmb_rt_bind_materials(ce, 4, 5))
		{
			const uint32_t none = 0;

			[ce setBuffer:counts offset:0 atIndex:4];
			[ce setBytes:&none length:sizeof(none) atIndex:5];
		}

		[ce setTexture:depth atIndex:0];
		[ce setTexture:normal atIndex:1];
		[ce setTexture:material atIndex:2];
		[ce setTexture:lm atIndex:3];
		[ce setTexture:base atIndex:4];
		[ce setTexture:t_vis atIndex:5];
		[ce setTexture:t_light atIndex:6];
		[ce setBytes:&view length:sizeof(view) atIndex:0];
		[ce setBytes:styles length:sizeof(styles) atIndex:1];
		[ce setBuffer:counts offset:(NSUInteger)slot * RMB_RTDIR_COUNTS * sizeof(uint32_t) atIndex:6];
		[ce setBuffer:lights offset:0 atIndex:9];
		[ce dispatchThreads:MTLSizeMake(w, h, 1) threadsPerThreadgroup:MTLSizeMake(RMB_RTDIR_GROUP, RMB_RTDIR_GROUP, 1)];
		[ce endEncoding];

		slot_pending[slot] = 1;
		slot_timed[slot] = timed;
		slot_frame[slot] = fno;
		last_frame = fno;
		slot_pixels[slot] = (unsigned long)(w * h);
		slot_exposure[slot] = view.exposure;
		[cmd addCompletedHandler:^(id<MTLCommandBuffer> b) {
			(void)b;
			atomic_store(&slot_done[slot], fno);
		}];
	}

	traced = 1;
	return t_light;
}

/* in the resolve's pass after the resolve: r_rt_direct_show's view, when the frame traced and it is on */
static int
rmb_rtdir_show(id<MTLRenderCommandEncoder> enc)
{
	const int did = traced && t_vis && t_light;

	traced = 0;

	if (!dir_open || !enc || !show_pipe || !did || !view.show)
	{
		return 0;
	}

	if (!rmb_fmt_bind(enc, show_pipe))
	{
		return 0;
	}

	[enc setFragmentBytes:&view length:sizeof(view) atIndex:0];
	[enc setFragmentTexture:t_vis atIndex:0];
	[enc setFragmentTexture:t_light atIndex:1];
	[enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
	rmb_counts.draws++;
	return 1;
}

/* ---- TASK179: the effect table's rows (rm_rtfx.c) ---------------------------------- */

/* the direct light in the lightmap's place on the world, first of the traces: the others start from it */
void
rmb_rtdir_fx_trace(rmb_rtfx_io_t *io)
{
	id<MTLTexture> dl = rmb_rtdir_trace(io->depth, io->normal, io->material, io->lightmap, io->light);

	if (dl)
	{
		io->light = dl;
	}
}

void
rmb_rtdir_fx_show(rmb_rtfx_io_t *io)
{
	rmb_rtdir_show(io->enc);
}
