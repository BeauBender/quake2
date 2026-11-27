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
 * The Metal back end of RT-primary's light on rastered alias models
 * (TASK191 slice 2; header/rm_rtmodelscene.h): rm_rtm_trace's pipeline,
 * its dispatch on the frame's command buffer after the direct light's
 * (the effect table's row after it), over the trace's own inputs read
 * through rmb_rtdir_frame, the frame's counts and the pass's GPU time,
 * read once the frame completed, in rmb_rtdir.m's shape. It writes the
 * model pixels of the light the resolve reads (io->light) and of the
 * trace's visibility; no other pixel. Every object is counted in the
 * ledger as RM_OBJ_RT.
 *
 * =======================================================================
 */

#import <Metal/Metal.h>

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "header/rm_gbuf.h"
#include "header/rm_rt.h"
#include "header/rm_rtdir.h"
#include "header/rm_rtmodelscene.h"
#include "header/rmb_internal.h"

#if !__has_feature(objc_arc)
#error "rmb_rtmodel.m is written for ARC: compile it with -fobjc-arc"
#endif

/* rmb_device.m */
extern id<MTLDevice> rmb_device;
extern rm_ledger_t rmb_ledger;

#define RMB_RTM_GROUP 8         /* the kernel's threadgroup side */
#define RMB_RTM_COUNTS 4        /* the model pixels, their shadow rays, their sky, sun and bounce rays; slice 4: those that kept a history */

_Static_assert(sizeof(rm_rtm_params_t) == 80, "the parameters are rm_rtmodel.metal's 80 bytes");

static int rtm_open;
static id<MTLComputePipelineState> rtm_pipe;
static id<MTLComputePipelineState> rtm_tpipe;   /* slice 4: rm_rtm_temporal, nil when it cannot be made (one sample, as rtm_pipe) */
static id<MTLBuffer> counts;            /* RMB_FRAMES_IN_FLIGHT slots of RMB_RTM_COUNTS */
static id<MTLCounterSampleBuffer> samples;
static atomic_ulong slot_done[RMB_FRAMES_IN_FLIGHT];
static int slot_pending[RMB_FRAMES_IN_FLIGHT];
static int slot_timed[RMB_FRAMES_IN_FLIGHT];
static int slot_acc[RMB_FRAMES_IN_FLIGHT];      /* slice 4: the slot's frame accumulated its model pixels */
static unsigned long slot_frame[RMB_FRAMES_IN_FLIGHT];
static rm_rtm_stats_t done[RMB_FRAMES_IN_FLIGHT];
static int ndone;
static rm_rtm_params_t params;
static unsigned char tabs[RM_RTM_TABLES];
static int view_set;

/* Sets a strong slot to obj (nil releases it), counting both in the ledger as RM_OBJ_RT. */
#define RMB_RTM_SET(slot, obj) \
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
rmb_rtmodel_open(char *err, unsigned long errlen)
{
	if (rtm_open)
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
		id<MTLFunction> k = [rmb_library() newFunctionWithName:@"rm_rtm_trace"];
		id<MTLCounterSet> ts = nil;

		if (!k)
		{
			snprintf(err, errlen, "the shader library has no rm_rtm_trace");
			return 0;
		}

		RMB_RTM_SET(rtm_pipe, [rmb_device newComputePipelineStateWithFunction:k error:&e]);
		RMB_RTM_SET(counts, [rmb_device newBufferWithLength:RMB_FRAMES_IN_FLIGHT * RMB_RTM_COUNTS * sizeof(uint32_t)
				options:MTLResourceStorageModeShared]);

		/* slice 4: the accumulating kernel; without it a model pixel takes one sample a view, as before */
		{
			id<MTLFunction> kt = [rmb_library() newFunctionWithName:@"rm_rtm_temporal"];
			NSError *et = nil;

			RMB_RTM_SET(rtm_tpipe, kt ? [rmb_device newComputePipelineStateWithFunction:kt error:&et] : nil);

			if (!rtm_tpipe)
			{
				fprintf(stderr, "ref_metal: rt models: no temporal pipeline; model pixels are not accumulated\n");
			}
		}

		if (!rtm_pipe || !counts)
		{
			snprintf(err, errlen, "cannot build the model pass's pipeline%s%s", e ? ": " : "",
					e ? e.localizedDescription.UTF8String : "");
			rmb_rtmodel_close();
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
			RMB_RTM_SET(samples, [rmb_device newCounterSampleBufferWithDescriptor:sd error:&e]);
		}
	}

	for (int i = 0; i < RMB_FRAMES_IN_FLIGHT; i++)
	{
		atomic_store(&slot_done[i], ~0UL);   /* no frame */
		slot_pending[i] = 0;
		slot_frame[i] = 0;
		slot_acc[i] = 0;
	}

	ndone = 0;
	view_set = 0;
	rtm_open = 1;
	return 1;
}

/* The frames still in flight keep what they use: a command buffer retains its resources. */
void
rmb_rtmodel_close(void)
{
	@autoreleasepool
	{
		RMB_RTM_SET(rtm_pipe, nil);
		RMB_RTM_SET(rtm_tpipe, nil);
		RMB_RTM_SET(counts, nil);
		RMB_RTM_SET(samples, nil);
	}

	rtm_open = 0;
	view_set = 0;
	ndone = 0;
}

void
rmb_rtmodel_set_view(const rm_rtm_params_t *p, const unsigned char *t)
{
	view_set = 0;

	if (!rtm_open || !p || !t)
	{
		return;
	}

	params = *p;
	memcpy(tabs, t, sizeof(tabs));
	view_set = 1;
}

/* the counts and times of the slots whose frames completed since the last read */
static void
rmb_rtmodel_read(void)
{
	int k;

	for (k = 0; k < RMB_FRAMES_IN_FLIGHT; k++)
	{
		const uint32_t *c;
		rm_rtm_stats_t *s;

		if (!slot_pending[k] || (atomic_load(&slot_done[k]) != slot_frame[k]))
		{
			continue;
		}

		slot_pending[k] = 0;

		if (ndone >= RMB_FRAMES_IN_FLIGHT)
		{
			continue;
		}

		c = (const uint32_t *)counts.contents + k * RMB_RTM_COUNTS;
		s = &done[ndone++];
		memset(s, 0, sizeof(*s));
		s->frame = slot_frame[k];
		s->pixels = c[0];
		s->rays = c[1];
		s->open = c[2];
		s->accumulated = slot_acc[k];
		s->kept = slot_acc[k] ? c[3] : 0;
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
rmb_rtmodel_stats(rm_rtm_stats_t *s, int cap)
{
	int i, n;

	if (!rtm_open)
	{
		return 0;
	}

	rmb_rtmodel_read();
	n = (ndone < cap) ? ndone : cap;

	for (i = 0; i < n; i++)
	{
		s[i] = done[i];
	}

	ndone = 0;
	return n;
}

/*
 * The kernel on the frame's command buffer, after the direct light's
 * trace (which it reads through rmb_rtdir_frame): the G-buffer's depth,
 * normal and material, the raster's light attachment (for a model's tag
 * alone), and writes the model pixels of light (the light the resolve
 * reads, the trace's or its accumulation) and of the trace's visibility.
 * Slice 4: on a view whose world light was accumulated (f.acc is the
 * light), rm_rtm_temporal in rm_rtm_trace's place, with the motion vectors
 * and the accumulation's histories and view, writes the model pixels of
 * the history and the key as well; otherwise rm_rtm_trace, bound as before.
 */
static void
rmb_rtmodel_trace(id<MTLTexture> depth, id<MTLTexture> normal, id<MTLTexture> material, id<MTLTexture> gbl,
		id<MTLTexture> light)
{
	unsigned long fno = 0;
	id<MTLCommandBuffer> cmd;
	id<MTLComputeCommandEncoder> ce;
	rmb_rtdir_frame_t f;
	rm_rtdir_view_t mv;
	id<MTLTexture> motion;
	const NSUInteger w = light.width, h = light.height;
	int slot, timed, acc;

	if (!view_set)
	{
		return;
	}

	view_set = 0;

	if (!rtm_open || !depth || !normal || !material || !gbl || !light || !rmb_rtdir_frame(&f))
	{
		return;
	}

	rmb_rtmodel_read();

	if ((depth.width != w) || (depth.height != h) || (gbl.width != w) || (gbl.height != h) || (f.vis.width != w) ||
		(f.vis.height != h))
	{
		return;
	}

	/* slice 4: the world's accumulation wrote this light, at this size, with the motion vectors at it */
	{
		id<MTLTexture> mvt = rmb_gbuf_motion();

		acc = rtm_tpipe && f.acc && (light == f.acc) && f.hist_in && f.key_in && f.hist_out && f.key_out && mvt &&
			(mvt.width == w) && (mvt.height == h) && (f.hist_in.width == w) && (f.hist_in.height == h);
		motion = acc ? mvt : nil;
	}

	@autoreleasepool
	{
		/* ends the trace's encoder (or the accumulation's): this one writes over what it wrote */
		cmd = rmb_frame_compute(&fno);

		if (!cmd)
		{
			return;
		}

		slot = (int)(fno % RMB_FRAMES_IN_FLIGHT);
		timed = (samples != nil);

		/* the frame that used this slot before has completed on the GPU (the frame semaphore); see rmb_rtsun.m */
		memset((uint32_t *)counts.contents + slot * RMB_RTM_COUNTS, 0, RMB_RTM_COUNTS * sizeof(uint32_t));

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
			return;
		}

		[ce setComputePipelineState:(acc ? rtm_tpipe : rtm_pipe)];

		/* the scene; its transmission and count go to slots 7 and 8, which the kernel does not read */
		if (!rmb_rt_bind_scene(ce, fno, 2, 3, 7, 8))
		{
			[ce endEncoding];
			return;
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
		[ce setTexture:gbl atIndex:3];
		[ce setTexture:f.vis atIndex:5];
		[ce setTexture:light atIndex:6];
		[ce setTexture:f.atlas atIndex:7];

		/* the view as the trace bound it; the bounce reads no record without the map's buffers */
		mv = f.view;

		if (!rmb_rt_bind_geometry(ce, 13, 14, 15))
		{
			const uint32_t g0[4] = { 0, 0, 0, 0xffffffffu };

			[ce setBuffer:counts offset:0 atIndex:13];
			[ce setBuffer:counts offset:0 atIndex:14];
			[ce setBytes:g0 length:sizeof(g0) atIndex:15];
			mv.nbrec = 0u;
		}

		[ce setBuffer:(f.brec ? f.brec : counts) offset:0 atIndex:16];

		if (!f.brec)
		{
			mv.nbrec = 0u;
		}

		/* a model is no emissive face: no own-light table, so rm_rtdir_own returns at once */
		mv.nself = 0u;
		[ce setBytes:&mv length:sizeof(mv) atIndex:0];
		[ce setBytes:f.styles length:3 * RM_RTL_STYLES * sizeof(float) atIndex:1];
		[ce setBuffer:counts offset:(NSUInteger)slot * RMB_RTM_COUNTS * sizeof(uint32_t) atIndex:6];
		[ce setBuffer:f.lights offset:0 atIndex:9];
		[ce setBytes:f.dlights length:MTL_RTDIR_MAX_DLIGHTS * sizeof(rm_rtdir_dlight_t) atIndex:10];
		[ce setBuffer:counts offset:0 atIndex:11];
		[ce setBuffer:f.tile offset:0 atIndex:12];
		[ce setBytes:&params length:sizeof(params) atIndex:17];
		[ce setBytes:tabs length:sizeof(tabs) atIndex:18];

		/* slice 4: the motion vectors and the accumulation's histories, read where the pixel was and written at it */
		if (acc)
		{
			[ce setTexture:motion atIndex:8];
			[ce setTexture:f.hist_in atIndex:9];
			[ce setTexture:f.key_in atIndex:10];
			[ce setTexture:f.hist_out atIndex:11];
			[ce setTexture:f.key_out atIndex:12];
			[ce setBytes:&f.tview length:sizeof(f.tview) atIndex:19];
		}

		[ce dispatchThreads:MTLSizeMake(w, h, 1) threadsPerThreadgroup:MTLSizeMake(RMB_RTM_GROUP, RMB_RTM_GROUP, 1)];
		[ce endEncoding];

		slot_pending[slot] = 1;
		slot_timed[slot] = timed;
		slot_acc[slot] = acc;
		slot_frame[slot] = fno;
		[cmd addCompletedHandler:^(id<MTLCommandBuffer> b) {
			(void)b;
			atomic_store(&slot_done[slot], fno);
		}];
	}
}

/* ---- the effect table's row (rm_rtfx.c) -------------------------------------------- */

/* after the direct light's trace and its accumulation: the model pixels of the light the resolve reads (slice 4: and of its history) */
void
rmb_rtmodel_fx_trace(rmb_rtfx_io_t *io)
{
	rmb_rtmodel_trace(io->depth, io->normal, io->material, io->lightmap, io->light);
}
