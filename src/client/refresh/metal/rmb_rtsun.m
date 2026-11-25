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
 * The Metal back end of ref_metal's ray-traced sun and sky shadow (RT4,
 * TASK56; header/rm_rtsun.h): the kernel's pipeline and the development
 * view's, the outputs at the render size (the class, R8Uint, and the
 * light the resolve reads, RGBA16Unorm), the dispatch on the frame's
 * command buffer after RT5's AO kernel (rmb_gbuf_resolve's hook), the
 * frame's counts and the pass's GPU time, read once the frame completed,
 * in TASK57's shape (rmb_rtao.m). Every object is counted in the ledger
 * as RM_OBJ_RT.
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
#include "common/mtl_rtd_math.h"
#include "header/rm_rtsun.h"
#include "header/rmb_internal.h"

#if !__has_feature(objc_arc)
#error "rmb_rtsun.m is written for ARC: compile it with -fobjc-arc"
#endif

/* rmb_device.m */
extern id<MTLDevice> rmb_device;
extern rm_ledger_t rmb_ledger;
extern rmb_stats_t rmb_counts;

#define RMB_RTSUN_GROUP 8       /* the kernel's threadgroup side */
#define RMB_RTSUN_COUNTS 5      /* world, bright, facing away, rays, lit (rm_rtsun.metal) */

_Static_assert(sizeof(rm_rtsun_view_t) == 144, "the view is rm_rtsun.metal's 144 bytes");

static int sun_open;
static id<MTLComputePipelineState> trace_pipe;
static id<MTLRenderPipelineState> show_pipe;
static id<MTLTexture> t_class;          /* the class, R8Uint at the render size */
static id<MTLTexture> t_light;          /* the light the resolve reads, RGBA16Unorm at the render size */
static id<MTLBuffer> counts;            /* RMB_FRAMES_IN_FLIGHT slots of RMB_RTSUN_COUNTS */
static id<MTLCounterSampleBuffer> samples;

/* per slot: the frame whose pass completed (set by its handler), the frame whose counts are awaited */
static atomic_ulong slot_done[RMB_FRAMES_IN_FLIGHT];
static int slot_pending[RMB_FRAMES_IN_FLIGHT];
static int slot_timed[RMB_FRAMES_IN_FLIGHT];
static unsigned long slot_frame[RMB_FRAMES_IN_FLIGHT], slot_pixels[RMB_FRAMES_IN_FLIGHT];
static rm_rtsun_stats_t done[RMB_FRAMES_IN_FLIGHT];
static int ndone;

/* the view the next G-buffer view traces with, and whether this one traced */
static rm_rtsun_view_t view;
static int view_set, traced;

/*
 * RT7's accumulation (TASK59 slice 1b), made only while r_rt_temporal is
 * above 0: rm_rtsun_temporal's pipeline, at the first view that
 * accumulates, and the two histories, RGBA32Float at the render size,
 * read and written in turn.
 */
static id<MTLComputePipelineState> tmp_pipe;
static id<MTLTexture> t_hist[2];
static int hist_cur;                    /* the history this view reads; the other is written */
static rm_rtt_hist_t thist;             /* the last view encoded into it: a skipped one resets the next */
static int tmp_refused;                 /* the pipeline could not be made: not tried again until the pass is closed */
static id<MTLComputePipelineState> dn_pipe;  /* the denoiser (TASK59 slice 2), made with the accumulation */
static int dn_refused;                  /* the denoiser's pipeline could not be made: the frames are accumulated alone */
static unsigned long tmp_bad_w, tmp_bad_h;  /* a render size whose histories could not be made: not tried again */
static rm_rtt_view_t tview;
static int tview_set;

/* Sets a strong slot to obj (nil releases it), counting both in the ledger as RM_OBJ_RT. */
#define RMB_RTSUN_SET(slot, obj) \
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
rmb_rtsun_open(char *err, size_t errlen)
{
	if (sun_open)
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
		id<MTLFunction> k = [rmb_library() newFunctionWithName:@"rm_rtsun_trace"];
		MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
		id<MTLCounterSet> ts = nil;

		d.vertexFunction = [rmb_library() newFunctionWithName:@"rm_vertex_fullscreen"];
		d.fragmentFunction = [rmb_library() newFunctionWithName:@"rm_fragment_rtsun_show"];
		d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

		if (!k || !d.vertexFunction || !d.fragmentFunction)
		{
			snprintf(err, errlen, "the shader library has no rm_rtsun_trace or rm_fragment_rtsun_show");
			return 0;
		}

		RMB_RTSUN_SET(trace_pipe, [rmb_device newComputePipelineStateWithFunction:k error:&e]);
		RMB_RTSUN_SET(show_pipe, trace_pipe ? rmb_new_render_pipe(d, &e) : nil);
		RMB_RTSUN_SET(counts, [rmb_device newBufferWithLength:RMB_FRAMES_IN_FLIGHT * RMB_RTSUN_COUNTS * sizeof(uint32_t)
				options:MTLResourceStorageModeShared]);

		if (!trace_pipe || !show_pipe || !counts)
		{
			snprintf(err, errlen, "cannot build the sun pipelines%s%s", e ? ": " : "",
					e ? e.localizedDescription.UTF8String : "");
			rmb_rtsun_close();
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
			RMB_RTSUN_SET(samples, [rmb_device newCounterSampleBufferWithDescriptor:sd error:&e]);
		}
	}

	for (int i = 0; i < RMB_FRAMES_IN_FLIGHT; i++)
	{
		atomic_store(&slot_done[i], ~0UL);   /* no frame */
		slot_pending[i] = 0;
	}

	ndone = 0;
	view_set = traced = 0;
	sun_open = 1;
	return 1;
}

/* The frames still in flight keep what they use: a command buffer retains its resources. */
void
rmb_rtsun_close(void)
{
	@autoreleasepool
	{
		RMB_RTSUN_SET(trace_pipe, nil);
		RMB_RTSUN_SET(show_pipe, nil);
		RMB_RTSUN_SET(t_class, nil);
		RMB_RTSUN_SET(t_light, nil);
		RMB_RTSUN_SET(counts, nil);
		RMB_RTSUN_SET(samples, nil);
		RMB_RTSUN_SET(tmp_pipe, nil);
		RMB_RTSUN_SET(dn_pipe, nil);
		RMB_RTSUN_SET(t_hist[0], nil);
		RMB_RTSUN_SET(t_hist[1], nil);
	}

	tview_set = hist_cur = tmp_refused = dn_refused = 0;
	tmp_bad_w = tmp_bad_h = 0;
	rm_rtt_hist_reset(&thist);
	sun_open = 0;
	view_set = traced = 0;
	ndone = 0;
}

int
rmb_rtsun_prepare(unsigned long w, unsigned long h)
{
	if (!sun_open || !trace_pipe || !counts || (w < 1) || (h < 1) || (w > 0xffffffffUL) || (h > 0xffffffffUL))
	{
		return 0;
	}

	/* the outputs at the render size, made again when it changes */
	if (t_class && t_light && (t_class.width == w) && (t_class.height == h))
	{
		return 1;
	}

	@autoreleasepool
	{
		MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Uint
				width:w height:h mipmapped:NO];

		d.storageMode = MTLStorageModePrivate;
		d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
		RMB_RTSUN_SET(t_class, [rmb_device newTextureWithDescriptor:d]);
		d.pixelFormat = MTLPixelFormatRGBA16Unorm;
		RMB_RTSUN_SET(t_light, [rmb_device newTextureWithDescriptor:d]);

		if (!t_class || !t_light)
		{
			RMB_RTSUN_SET(t_class, nil);
			RMB_RTSUN_SET(t_light, nil);
			return 0;
		}

		fprintf(stderr, "ref_metal: rt sun %lux%lu: %lu bytes allocated\n", w, h,
				(unsigned long)(t_class.allocatedSize + t_light.allocatedSize));
	}

	return 1;
}

int
rmb_rtsun_temporal(const rm_rtt_view_t *t, unsigned long w, unsigned long h)
{
	tview_set = 0;

	/* off: no history is kept */
	if (!t || !(t->alpha > 0.0f) || !sun_open || (w < 1) || (h < 1) || (w > 0xffffffffUL) || (h > 0xffffffffUL))
	{
		if (t_hist[0] || t_hist[1])
		{
			RMB_RTSUN_SET(t_hist[0], nil);
			RMB_RTSUN_SET(t_hist[1], nil);
		}

		rm_rtt_hist_reset(&thist);
		return 0;
	}

	/* a failure is latched: the pipeline until the pass is closed, the histories for that size */
	if (tmp_refused || ((w == tmp_bad_w) && (h == tmp_bad_h)))
	{
		return 0;
	}

	@autoreleasepool
	{
		if (!tmp_pipe)
		{
			NSError *e = nil;
			id<MTLFunction> k = [rmb_library() newFunctionWithName:@"rm_rtsun_temporal"];

			RMB_RTSUN_SET(tmp_pipe, k ? [rmb_device newComputePipelineStateWithFunction:k error:&e] : nil);

			if (!tmp_pipe)
			{
				tmp_refused = 1;
				fprintf(stderr, "ref_metal: rt temporal: no sun accumulation pipeline\n");
				return 0;
			}
		}

		/* TASK59 slice 2: the denoiser; without it the frames are accumulated alone */
		if (!dn_pipe && !dn_refused)
		{
			NSError *e = nil;
			id<MTLFunction> k = [rmb_library() newFunctionWithName:@"rm_rtsun_denoise"];

			RMB_RTSUN_SET(dn_pipe, k ? [rmb_device newComputePipelineStateWithFunction:k error:&e] : nil);

			if (!dn_pipe)
			{
				dn_refused = 1;
				fprintf(stderr, "ref_metal: rt temporal: no sun denoise pipeline\n");
			}
		}

		if (!t_hist[0] || !t_hist[1] || (t_hist[0].width != w) || (t_hist[0].height != h))
		{
			MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
					width:w height:h mipmapped:NO];

			d.storageMode = MTLStorageModePrivate;
			d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
			RMB_RTSUN_SET(t_hist[0], [rmb_device newTextureWithDescriptor:d]);
			RMB_RTSUN_SET(t_hist[1], [rmb_device newTextureWithDescriptor:d]);
			rm_rtt_hist_reset(&thist);

			if (!t_hist[0] || !t_hist[1])
			{
				RMB_RTSUN_SET(t_hist[0], nil);
				RMB_RTSUN_SET(t_hist[1], nil);
				tmp_bad_w = w;
				tmp_bad_h = h;
				fprintf(stderr, "ref_metal: rt temporal %lux%lu: the sun's histories cannot be made\n", w, h);
				return 0;
			}

			hist_cur = 0;
			fprintf(stderr, "ref_metal: rt temporal sun %lux%lu: %lu bytes allocated\n", w, h,
					(unsigned long)(t_hist[0].allocatedSize + t_hist[1].allocatedSize));
		}
	}

	tview = *t;
	tview_set = 1;
	return 1;
}

void
rmb_rtsun_set_view(const rm_rtsun_view_t *v)
{
	view_set = 0;

	if (!sun_open || !v)
	{
		return;
	}

	view = *v;
	view_set = 1;
}

/* the counts and times of the slots whose frames completed since the last read */
static void
rmb_rtsun_read(void)
{
	int k;

	for (k = 0; k < RMB_FRAMES_IN_FLIGHT; k++)
	{
		const uint32_t *c;
		rm_rtsun_stats_t *s;

		if (!slot_pending[k] || (atomic_load(&slot_done[k]) != slot_frame[k]))
		{
			continue;
		}

		slot_pending[k] = 0;

		if (ndone >= RMB_FRAMES_IN_FLIGHT)
		{
			continue;
		}

		c = (const uint32_t *)counts.contents + k * RMB_RTSUN_COUNTS;
		s = &done[ndone++];
		memset(s, 0, sizeof(*s));
		s->frame = slot_frame[k];
		s->pixels = slot_pixels[k];
		s->world = c[0];
		s->bright = c[1];
		s->facing = c[2];
		s->rays = c[3];
		s->lit = c[4];
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
rmb_rtsun_stats(rm_rtsun_stats_t *s, int cap)
{
	int i, n;

	if (!sun_open)
	{
		return 0;
	}

	rmb_rtsun_read();
	n = (ndone < cap) ? ndone : cap;

	for (i = 0; i < n; i++)
	{
		s[i] = done[i];
	}

	ndone = 0;
	return n;
}

id<MTLTexture>
rmb_rtsun_trace(id<MTLTexture> depth, id<MTLTexture> normal, id<MTLTexture> material, id<MTLTexture> lm,
		id<MTLTexture> base)
{
	unsigned long fno = 0;
	id<MTLCommandBuffer> cmd;
	id<MTLComputeCommandEncoder> ce;
	const NSUInteger w = depth.width, h = depth.height;
	const int acc_set = tview_set;  /* RT7: this view accumulates, when everything it needs is at this size */
	int slot, timed;

	traced = 0;
	tview_set = 0;

	if (!sun_open || !view_set || !depth || !normal || !material || !lm || !base)
	{
		return nil;
	}

	view_set = 0;
	rmb_rtsun_read();

	/* the outputs, made by rmb_rtsun_prepare before the raster at this size */
	if (!t_class || !t_light || (t_class.width != w) || (t_class.height != h) || (lm.width != w) || (lm.height != h) ||
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

		/*
		 * The frame that used this slot before has completed on the GPU (the
		 * frame semaphore), though its handler may not have run yet: its
		 * counts are dropped, and its handler, naming its own frame, does not
		 * mark this one done.
		 */
		memset((uint32_t *)counts.contents + slot * RMB_RTSUN_COUNTS, 0, RMB_RTSUN_COUNTS * sizeof(uint32_t));

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
		[ce setTexture:t_class atIndex:5];
		[ce setTexture:t_light atIndex:6];
		[ce setBytes:&view length:sizeof(view) atIndex:0];
		[ce setBuffer:counts offset:(NSUInteger)slot * RMB_RTSUN_COUNTS * sizeof(uint32_t) atIndex:6];
		[ce dispatchThreads:MTLSizeMake(w, h, 1) threadsPerThreadgroup:MTLSizeMake(RMB_RTSUN_GROUP, RMB_RTSUN_GROUP, 1)];

		/*
		 * RT7 (TASK59 slice 1b): the sun's visibility accumulated over the
		 * views, the light written again from it. Only while r_rt_temporal is
		 * above 0 and the histories and the motion vectors are at the render
		 * size; otherwise nothing below runs and the frame is the trace's alone.
		 */
		if (acc_set && tmp_pipe && t_hist[0] && t_hist[1] && (t_hist[0].width == w) && (t_hist[0].height == h))
		{
			id<MTLTexture> mv = rmb_gbuf_motion();

			if (mv && (mv.width == w) && (mv.height == h))
			{
				rm_rtt_view_t tv = tview;

				/* the view is encoded into the history here, and only here */
				tv.reset = rm_rtt_hist_take(&thist, &tview);
				[ce memoryBarrierWithScope:MTLBarrierScopeTextures];
				[ce setComputePipelineState:tmp_pipe];
				[ce setTexture:mv atIndex:7];
				[ce setTexture:t_hist[hist_cur] atIndex:8];
				[ce setTexture:t_hist[1 - hist_cur] atIndex:9];
				[ce setBytes:&tv length:sizeof(tv) atIndex:9];
				[ce dispatchThreads:MTLSizeMake(w, h, 1)
						threadsPerThreadgroup:MTLSizeMake(RMB_RTSUN_GROUP, RMB_RTSUN_GROUP, 1)];
				hist_cur = 1 - hist_cur;

				/*
				 * TASK59 slice 2: the 5 x 5 bilateral denoiser of the history just
				 * written (now t_hist[hist_cur]), the light written again from it;
				 * the history itself keeps the accumulation alone.
				 */
				if (dn_pipe)
				{
					mtl_rtd_view_t dv;

					memset(&dv, 0, sizeof(dv));
					memcpy(dv.dir, view.dir, sizeof(dv.dir));
					memcpy(dv.sun, view.sun, sizeof(dv.sun));
					[ce memoryBarrierWithScope:MTLBarrierScopeTextures];
					[ce setComputePipelineState:dn_pipe];
					[ce setTexture:t_hist[hist_cur] atIndex:8];
					[ce setBytes:&dv length:sizeof(dv) atIndex:10];
					[ce dispatchThreads:MTLSizeMake(w, h, 1)
							threadsPerThreadgroup:MTLSizeMake(RMB_RTSUN_GROUP, RMB_RTSUN_GROUP, 1)];
				}
			}
		}

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
rmb_rtsun_show(id<MTLRenderCommandEncoder> enc)
{
	const int did = traced && t_class;

	traced = 0;

	if (!sun_open || !enc || !show_pipe || !did || !view.show)
	{
		return 0;
	}

	if (!rmb_fmt_bind(enc, show_pipe))
	{
		return 0;
	}

	[enc setFragmentBytes:&view length:sizeof(view) atIndex:0];
	[enc setFragmentTexture:t_class atIndex:0];
	[enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
	rmb_counts.draws++;
	return 1;
}

/* ---- TASK179: the effect table's rows (rm_rtfx.c) ---------------------------------- */

/* the sun's term added to the light the resolve would read (after the AO's); its floor gate reads the lightmap */
void
rmb_rtsun_fx_trace(rmb_rtfx_io_t *io)
{
	id<MTLTexture> sl = rmb_rtsun_trace(io->depth, io->normal, io->material, io->lightmap, io->light);

	if (sl)
	{
		io->light = sl;
	}
}

void
rmb_rtsun_fx_show(rmb_rtfx_io_t *io)
{
	rmb_rtsun_show(io->enc);
}
