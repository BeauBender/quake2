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
 * The Metal back end of RT-primary's direct light (TASK188, PM6 slices 1 to 3;
 * header/rm_rtdir.h): the kernel's pipeline and the development view's,
 * the map's lights in a buffer, the outputs at the render size (the
 * visibility, RGBA8Unorm, and the light the resolve reads, RGBA16Unorm),
 * the dispatch on the frame's command buffer first among the effects'
 * kernels (rmb_gbuf_resolve's trace walk), the frame's counts and the
 * pass's GPU time, read once the frame completed, in the sun's shape
 * (rmb_rtsun.m). The exposure is the view's: r_rt_direct_exposure's or the
 * map's fit at its load (rm_rtdirscene.c, slice 3c). TASK189 adds the sky
 * ray's blue-noise tile (rm_rtao_bluenoise, made again for this pass) and
 * the sky's and the sun's counts. TASK190 slice 1 (PM8) adds the light's
 * temporal accumulation, rm_rtp_temporal (rm_rtptemporal.metal), after the
 * trace in an encoder of its own with its own GPU time, its histories made
 * only while r_rt_temporal is above 0; slice 2 adds its 5 x 5 filter,
 * rm_rtp_denoise (rm_rtpdenoise.metal), in the same encoder. Every object is counted in the
 * ledger as RM_OBJ_RT.
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
#include "header/rm_rtao.h"
#include "header/rmb_internal.h"

#if !__has_feature(objc_arc)
#error "rmb_rtdir.m is written for ARC: compile it with -fobjc-arc"
#endif

/* rmb_device.m */
extern id<MTLDevice> rmb_device;
extern rm_ledger_t rmb_ledger;
extern rmb_stats_t rmb_counts;

#define RMB_RTDIR_GROUP 8       /* the kernel's threadgroup side */
/*
 * world, in range, rays, lit (their low words), the fit's lightmap and raw sums and samples, the three high words,
 * capped; TASK189's sky rays, sky hits, sun rays and sun hits; slice 2's bounce rays and shaded hits
 */
#define RMB_RTDIR_COUNTS 17

_Static_assert(sizeof(rm_rtdir_view_t) == 192, "the view is rm_rtdir.metal's 192 bytes");
_Static_assert(sizeof(rm_rtdir_light_t) == 48, "a light is rm_rtdir.metal's 48 bytes");
_Static_assert(sizeof(rm_rtdir_dlight_t) == 32, "a dynamic light is rm_rtdir.metal's 32 bytes");
/* the frame's dynamic lights go by setBytes, inside its 4 KB */
_Static_assert(MTL_RTDIR_MAX_DLIGHTS * sizeof(rm_rtdir_dlight_t) <= 4096, "the dynamic lights fit in setBytes");

static int dir_open;
static id<MTLComputePipelineState> trace_pipe;
static id<MTLRenderPipelineState> show_pipe;
static id<MTLTexture> t_vis;            /* the visibility, RGBA8Unorm at the render size */
static id<MTLTexture> t_light;          /* the light the resolve reads, RGBA16Unorm at the render size */
static id<MTLBuffer> lights;            /* the map's lights, at least one record long */
static unsigned long nlights;
static id<MTLBuffer> counts;            /* RMB_FRAMES_IN_FLIGHT slots of RMB_RTDIR_COUNTS */
static id<MTLBuffer> selftab;           /* the map's per-triangle own light (TASK188 slice 3b), at least one word long */
static id<MTLBuffer> tile;              /* TASK189: the sky ray's blue-noise ranks, MTL_RTAO_TILE_N ushorts */
static unsigned long nself;
static id<MTLBuffer> brec;              /* slice 2: the map's per-triangle bounce records, at least one word long */
static unsigned long nbrec;
static int brec_lmtex;                  /* the world's lightmap array the bounce reads, 0 for none */
static id<MTLTexture> white_arr;        /* a 1 x 1 x 1 array bound when the world has no lightmap array */
static rm_rtdir_dlight_t dlights[MTL_RTDIR_MAX_DLIGHTS];   /* the frame's dynamic lights, all of them bound */
static unsigned ndlights;
static id<MTLCounterSampleBuffer> samples;

/* per slot: the frame whose pass completed (set by its handler), the frame whose counts are awaited */
static atomic_ulong slot_done[RMB_FRAMES_IN_FLIGHT];
static int slot_pending[RMB_FRAMES_IN_FLIGHT];
static int slot_timed[RMB_FRAMES_IN_FLIGHT];
static unsigned long slot_frame[RMB_FRAMES_IN_FLIGHT], slot_pixels[RMB_FRAMES_IN_FLIGHT];
static float slot_exposure[RMB_FRAMES_IN_FLIGHT];
static rm_rtdir_stats_t done[RMB_FRAMES_IN_FLIGHT];
static int ndone;

/* the view and styles the next G-buffer view traces with, and whether this one traced */
static rm_rtdir_view_t view;
static float styles[3 * RM_RTL_STYLES];
static int view_set, traced;

/*
 * TASK190 slice 1 (PM8): the accumulation of the light, made only while
 * r_rt_temporal is above 0 (rmb_rtdir_temporal): rm_rtp_temporal's
 * pipeline and timestamps, at the first view that accumulates, and at
 * the render size the two histories of the light with its count
 * (RGBA16Float) and of the key (the distance's bits and the material id,
 * RG32Uint), read and written in turn, and the light the resolve reads
 * (RGBA16Unorm, t_light's format).
 */
static id<MTLComputePipelineState> tmp_pipe;
static id<MTLComputePipelineState> dn_pipe;  /* slice 2: the 5 x 5 filter (rm_rtp_denoise), made with the accumulation */
static int dn_refused;                  /* its pipeline could not be made: the light is accumulated alone */
static id<MTLCounterSampleBuffer> tsamples;
static id<MTLTexture> t_hist[2];
static id<MTLTexture> t_key[2];
static id<MTLTexture> t_acc;
static int hist_cur;                    /* the history this view reads; the other is written */
static rm_rtt_hist_t thist;             /* the last view encoded into it: a skipped one resets the next */
static int tmp_refused;                 /* the pipeline could not be made: not tried again until the pass is closed */
static unsigned long tmp_bad_w, tmp_bad_h;  /* a render size whose histories could not be made: not tried again */
static rm_rtt_view_t tview;
static int tview_set;
static int acc_view;                    /* this view's light was accumulated into t_acc: the development view shows it */
static unsigned tshow;                  /* the r_rt_direct_show of the last view accumulated */
static unsigned long slot_acc[RMB_FRAMES_IN_FLIGHT];   /* per slot: the frame that accumulated in it */
static int slot_ttimed[RMB_FRAMES_IN_FLIGHT];

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

		/* TASK189: the sky ray's tile, made in code as the AO's (rm_rtao_bluenoise) */
		{
			unsigned short *ranks = malloc(MTL_RTAO_TILE_N * sizeof(*ranks));

			if (ranks && rm_rtao_bluenoise(ranks))
			{
				RMB_RTDIR_SET(tile, [rmb_device newBufferWithBytes:ranks length:MTL_RTAO_TILE_N * sizeof(*ranks)
						options:MTLResourceStorageModeShared]);
			}

			free(ranks);
		}

		/* slice 2: bound at the bounce's lightmap index when the world has none (the kernel then reads none: view.nbrec 0) */
		{
			MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
					width:1 height:1 mipmapped:NO];

			td.textureType = MTLTextureType2DArray;
			td.arrayLength = 1;
			td.usage = MTLTextureUsageShaderRead;
			RMB_RTDIR_SET(white_arr, [rmb_device newTextureWithDescriptor:td]);
		}

		if (trace_pipe && show_pipe && counts && (!tile || !white_arr))
		{
			snprintf(err, errlen, "cannot make the sky ray's blue-noise tile or the bounce's empty lightmap");
			rmb_rtdir_close();
			return 0;
		}

		if (!trace_pipe || !show_pipe || !counts || !tile || !white_arr)
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
		slot_frame[i] = 0;
		slot_acc[i] = ~0UL;
	}

	ndone = 0;
	nlights = 0;
	nself = 0;
	nbrec = 0;
	brec_lmtex = 0;
	ndlights = 0;
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
		RMB_RTDIR_SET(selftab, nil);
		RMB_RTDIR_SET(brec, nil);
		RMB_RTDIR_SET(white_arr, nil);
		RMB_RTDIR_SET(tile, nil);
		RMB_RTDIR_SET(counts, nil);
		RMB_RTDIR_SET(samples, nil);
		RMB_RTDIR_SET(tmp_pipe, nil);
		RMB_RTDIR_SET(dn_pipe, nil);
		RMB_RTDIR_SET(tsamples, nil);
		RMB_RTDIR_SET(t_hist[0], nil);
		RMB_RTDIR_SET(t_hist[1], nil);
		RMB_RTDIR_SET(t_key[0], nil);
		RMB_RTDIR_SET(t_key[1], nil);
		RMB_RTDIR_SET(t_acc, nil);
	}

	rm_rtt_hist_reset(&thist);
	tview_set = hist_cur = tmp_refused = dn_refused = acc_view = 0;
	tmp_bad_w = tmp_bad_h = 0;
	tshow = 0;
	dir_open = 0;
	nlights = 0;
	nself = 0;
	nbrec = 0;
	brec_lmtex = 0;
	ndlights = 0;
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
rmb_rtdir_set_self(const unsigned *t, unsigned long n)
{
	nself = 0;

	if (!dir_open || ((n > 0) && !t) || (n > 0xffffffffUL))
	{
		return 0;
	}

	@autoreleasepool
	{
		/* a new buffer a map: a frame in flight keeps the old one; one word at least, so index 11 is bound */
		RMB_RTDIR_SET(selftab, [rmb_device newBufferWithLength:(n > 0 ? (size_t)n : 1) * sizeof(uint32_t)
				options:MTLResourceStorageModeShared]);

		if (!selftab)
		{
			return 0;
		}

		memset(selftab.contents, 0, sizeof(uint32_t));

		if (n > 0)
		{
			memcpy(selftab.contents, t, (size_t)n * sizeof(uint32_t));
		}
	}

	nself = n;
	return 1;
}

int
rmb_rtdir_set_bounce(const unsigned *t, unsigned long n, int lmtex)
{
	nbrec = 0;
	brec_lmtex = 0;

	if (!dir_open || ((n > 0) && !t) || (n > 0xffffffffUL))
	{
		return 0;
	}

	@autoreleasepool
	{
		/* a new buffer a map, as the own-light table's: one word at least, so index 16 is bound */
		RMB_RTDIR_SET(brec, [rmb_device newBufferWithLength:(n > 0 ? (size_t)n : 1) * sizeof(uint32_t)
				options:MTLResourceStorageModeShared]);

		if (!brec)
		{
			return 0;
		}

		memset(brec.contents, 0, sizeof(uint32_t));

		if (n > 0)
		{
			memcpy(brec.contents, t, (size_t)n * sizeof(uint32_t));
		}
	}

	nbrec = n;
	brec_lmtex = lmtex;
	return 1;
}

void
rmb_rtdir_set_dlights(const rm_rtdir_dlight_t *d, int n)
{
	ndlights = 0;
	memset(dlights, 0, sizeof(dlights));

	if (!d || (n < 1))
	{
		return;
	}

	n = (n > MTL_RTDIR_MAX_DLIGHTS) ? MTL_RTDIR_MAX_DLIGHTS : n;
	memcpy(dlights, d, (size_t)n * sizeof(*d));
	ndlights = (unsigned)n;
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

/* the histories and the accumulated light released: the next view that accumulates resets (FIRST) */
static void
rmb_rtdir_temporal_free(void)
{
	@autoreleasepool
	{
		RMB_RTDIR_SET(t_hist[0], nil);
		RMB_RTDIR_SET(t_hist[1], nil);
		RMB_RTDIR_SET(t_key[0], nil);
		RMB_RTDIR_SET(t_key[1], nil);
		RMB_RTDIR_SET(t_acc, nil);
	}

	rm_rtt_hist_reset(&thist);
	hist_cur = 0;
}

/* a timestamp pair a slot at the encoders' boundaries, or nil where the device samples none there */
static id<MTLCounterSampleBuffer>
rmb_rtdir_new_samples(void)
{
	MTLCounterSampleBufferDescriptor *sd;
	id<MTLCounterSet> ts = nil;
	NSError *e = nil;

	for (id<MTLCounterSet> cs in rmb_device.counterSets)
	{
		if ([cs.name isEqualToString:MTLCommonCounterSetTimestamp])
		{
			ts = cs;
		}
	}

	if (!ts || ![rmb_device supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary])
	{
		return nil;
	}

	sd = [MTLCounterSampleBufferDescriptor new];
	sd.counterSet = ts;
	sd.storageMode = MTLStorageModeShared;
	sd.sampleCount = RMB_FRAMES_IN_FLIGHT * 2;
	return [rmb_device newCounterSampleBufferWithDescriptor:sd error:&e];
}

int
rmb_rtdir_temporal(const rm_rtt_view_t *t, unsigned long w, unsigned long h)
{
	tview_set = 0;

	/* off: no history is kept, and the trace's light is the resolve's */
	if (!t || !(t->alpha > 0.0f) || !dir_open || (w < 1) || (h < 1) || (w > 0xffffffffUL) || (h > 0xffffffffUL))
	{
		if (t_hist[0] || t_hist[1] || t_key[0] || t_key[1] || t_acc)
		{
			rmb_rtdir_temporal_free();
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
			id<MTLFunction> k = [rmb_library() newFunctionWithName:@"rm_rtp_temporal"];

			RMB_RTDIR_SET(tmp_pipe, k ? [rmb_device newComputePipelineStateWithFunction:k error:&e] : nil);

			if (!tmp_pipe)
			{
				tmp_refused = 1;
				fprintf(stderr, "ref_metal: rt temporal: no direct-light accumulation pipeline\n");
				return 0;
			}

			RMB_RTDIR_SET(tsamples, rmb_rtdir_new_samples());
		}

		/* TASK190 slice 2: the 5 x 5 filter; without it the light is accumulated alone */
		if (!dn_pipe && !dn_refused)
		{
			NSError *e = nil;
			id<MTLFunction> k = [rmb_library() newFunctionWithName:@"rm_rtp_denoise"];

			RMB_RTDIR_SET(dn_pipe, k ? [rmb_device newComputePipelineStateWithFunction:k error:&e] : nil);

			if (!dn_pipe)
			{
				dn_refused = 1;
				fprintf(stderr, "ref_metal: rt temporal: no direct-light denoise pipeline\n");
			}
		}

		/* made again when the render size changes, the old ones released first */
		if (!t_acc || (t_acc.width != w) || (t_acc.height != h))
		{
			MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
					width:w height:h mipmapped:NO];

			rmb_rtdir_temporal_free();
			d.storageMode = MTLStorageModePrivate;
			d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
			RMB_RTDIR_SET(t_hist[0], [rmb_device newTextureWithDescriptor:d]);
			RMB_RTDIR_SET(t_hist[1], [rmb_device newTextureWithDescriptor:d]);
			d.pixelFormat = MTLPixelFormatRG32Uint;
			RMB_RTDIR_SET(t_key[0], [rmb_device newTextureWithDescriptor:d]);
			RMB_RTDIR_SET(t_key[1], [rmb_device newTextureWithDescriptor:d]);
			d.pixelFormat = MTLPixelFormatRGBA16Unorm;
			RMB_RTDIR_SET(t_acc, [rmb_device newTextureWithDescriptor:d]);

			if (!t_hist[0] || !t_hist[1] || !t_key[0] || !t_key[1] || !t_acc)
			{
				rmb_rtdir_temporal_free();
				tmp_bad_w = w;
				tmp_bad_h = h;
				fprintf(stderr, "ref_metal: rt temporal %lux%lu: the direct light's histories cannot be made\n", w, h);
				return 0;
			}

			fprintf(stderr, "ref_metal: rt temporal direct %lux%lu: %lu bytes allocated\n", w, h,
					(unsigned long)(t_hist[0].allocatedSize + t_hist[1].allocatedSize + t_key[0].allocatedSize +
					t_key[1].allocatedSize + t_acc.allocatedSize));
		}
	}

	tview = *t;
	tview_set = 1;
	return 1;
}

void
rmb_rtdir_set_view(const rm_rtdir_view_t *v, const float *st)
{
	view_set = 0;
	tview_set = 0;          /* rmb_rtdir_temporal, after this, hands the view's accumulation over */

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

	/* nor a dynamic light past the frame's (TASK188 slice 3b) */
	view.ndlights = ndlights;

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
		s->sky_rays = c[11];
		s->sky_hits = c[12];
		s->sun_rays = c[13];
		s->sun_lit = c[14];
		s->bounce_rays = c[15];
		s->bounce_hits = c[16];
		s->exposure = slot_exposure[k];
		s->ms = -1;
		s->accumulated = (slot_acc[k] == slot_frame[k]);
		s->tms = -1;

		/* TASK190 slice 1: the accumulation's own encoder, timed apart */
		if (s->accumulated && slot_ttimed[k] && tsamples)
		{
			NSData *d = [tsamples resolveCounterRange:NSMakeRange((NSUInteger)k * 2, 2)];
			const MTLCounterResultTimestamp *t = d ? (const MTLCounterResultTimestamp *)d.bytes : NULL;

			if (t && (d.length >= 2 * sizeof(*t)) && (t[0].timestamp != MTLCounterErrorValue) &&
				(t[1].timestamp != MTLCounterErrorValue) && (t[1].timestamp >= t[0].timestamp))
			{
				s->tms = rmb_rt_gpu_ms(t[1].timestamp - t[0].timestamp);
			}
		}

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

/*
 * The kernel on the frame's command buffer over the render-size depth,
 * normal and material attachments, the raster's light (lm, read for the
 * view's fit sums on the r_rt_stats 2 line only) and the light the
 * resolve would read (base, kept on
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

		/*
		 * slice 2: the bounce's lightmap array (the one the stock draw samples, marked used by this frame), the map's
		 * buffers and the bounce records; without any of them the kernel reads no record (view.nbrec 0)
		 */
		{
			id<MTLTexture> atlas = brec_lmtex ? rmb_texture_for_draw(brec_lmtex) : nil;
			const int lm_ok = atlas && (atlas.textureType == MTLTextureType2DArray);
			const int geom_ok = rmb_rt_bind_geometry(ce, 13, 14, 15);

			if (!geom_ok)
			{
				const uint32_t g0[4] = { 0, 0, 0, 0xffffffffu };

				[ce setBuffer:counts offset:0 atIndex:13];
				[ce setBuffer:counts offset:0 atIndex:14];
				[ce setBytes:g0 length:sizeof(g0) atIndex:15];
			}

			[ce setTexture:(lm_ok ? atlas : white_arr) atIndex:7];
			view.nbrec = (brec && lm_ok && geom_ok) ? (unsigned)nbrec : 0u;
			[ce setBuffer:(brec ? brec : counts) offset:0 atIndex:16];
		}

		/* nor a triangle past the table bound at index 11, its count taken here with it (TASK188 slice 3c) */
		view.nself = selftab ? (unsigned)nself : 0u;
		[ce setBytes:&view length:sizeof(view) atIndex:0];
		[ce setBytes:styles length:sizeof(styles) atIndex:1];
		[ce setBuffer:counts offset:(NSUInteger)slot * RMB_RTDIR_COUNTS * sizeof(uint32_t) atIndex:6];
		[ce setBuffer:lights offset:0 atIndex:9];
		[ce setBytes:dlights length:sizeof(dlights) atIndex:10];
		/* the own-light table, or (none made) a word the kernel does not read: view.nself is then 0 */
		[ce setBuffer:(selftab ? selftab : counts) offset:0 atIndex:11];
		/* TASK189: the sky ray's noise */
		[ce setBuffer:tile offset:0 atIndex:12];
		[ce dispatchThreads:MTLSizeMake(w, h, 1) threadsPerThreadgroup:MTLSizeMake(RMB_RTDIR_GROUP, RMB_RTDIR_GROUP, 1)];
		[ce endEncoding];

		slot_pending[slot] = 1;
		slot_timed[slot] = timed;
		slot_frame[slot] = fno;
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
	/* TASK190 slice 1: the term as the resolve reads it, accumulated where the view accumulated */
	[enc setFragmentTexture:((acc_view && t_acc) ? t_acc : t_light) atIndex:1];
	[enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
	rmb_counts.draws++;
	return 1;
}

/*
 * TASK190 slice 1 (PM8): after the trace, in an encoder of its own (its
 * own timestamp pair), the traced light (the trace's out) accumulated
 * over the views by rm_rtp_temporal into t_acc, which the resolve then
 * reads; nil, and the trace's light read as it is, when the view does not
 * accumulate (r_rt_temporal 0, the histories not made, no motion vectors
 * at the render size).
 */
static id<MTLTexture>
rmb_rtdir_accumulate(id<MTLTexture> depth, id<MTLTexture> normal, id<MTLTexture> material, id<MTLTexture> light)
{
	unsigned long fno = 0;
	id<MTLCommandBuffer> cmd;
	id<MTLComputeCommandEncoder> ce;
	id<MTLTexture> mv = rmb_gbuf_motion();
	const NSUInteger w = light.width, h = light.height;
	rm_rtt_view_t tv;
	int slot, timed;

	acc_view = 0;

	if (!tview_set)
	{
		return nil;
	}

	tview_set = 0;

	if (!dir_open || !tmp_pipe || !t_hist[0] || !t_hist[1] || !t_key[0] || !t_key[1] || !t_acc || (t_acc.width != w) ||
		(t_acc.height != h) || !mv || (mv.width != w) || (mv.height != h) || (depth.width != w) || (depth.height != h))
	{
		return nil;
	}

	@autoreleasepool
	{
		/* ends the trace's encoder: this one reads what it wrote */
		cmd = rmb_frame_compute(&fno);

		if (!cmd)
		{
			return nil;
		}

		slot = (int)(fno % RMB_FRAMES_IN_FLIGHT);
		timed = (tsamples != nil);

		if (timed)
		{
			MTLComputePassDescriptor *cp = [MTLComputePassDescriptor computePassDescriptor];

			cp.sampleBufferAttachments[0].sampleBuffer = tsamples;
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

		/* the view is encoded into the history here, and only here */
		tv = tview;
		tv.reset = rm_rtt_hist_take(&thist, &tview);

		/* another r_rt_direct_show changes what the light holds: the history starts again */
		if (view.show != tshow)
		{
			tv.reset |= RM_RTT_RESET_FIRST;
		}

		tshow = view.show;
		[ce setComputePipelineState:tmp_pipe];
		[ce setTexture:depth atIndex:0];
		[ce setTexture:normal atIndex:1];
		[ce setTexture:material atIndex:2];
		[ce setTexture:mv atIndex:3];
		[ce setTexture:light atIndex:4];
		[ce setTexture:t_hist[hist_cur] atIndex:5];
		[ce setTexture:t_key[hist_cur] atIndex:6];
		[ce setTexture:t_acc atIndex:7];
		[ce setTexture:t_hist[1 - hist_cur] atIndex:8];
		[ce setTexture:t_key[1 - hist_cur] atIndex:9];
		/* the whole view: the kernel reads its start (the inverse, the rectangle, the eye) */
		[ce setBytes:&view length:sizeof(view) atIndex:0];
		[ce setBytes:&tv length:sizeof(tv) atIndex:1];
		[ce dispatchThreads:MTLSizeMake(w, h, 1) threadsPerThreadgroup:MTLSizeMake(RMB_RTDIR_GROUP, RMB_RTDIR_GROUP, 1)];

		/*
		 * TASK190 slice 2: the 5 x 5 filter of the history and key just
		 * written (still at 8 and 9), t_acc written again from it on the
		 * world pixels whose count leaves a strength; the history keeps
		 * the accumulation alone. Same bindings, in this encoder and its
		 * timestamp pair.
		 */
		if (dn_pipe)
		{
			[ce memoryBarrierWithScope:MTLBarrierScopeTextures];
			[ce setComputePipelineState:dn_pipe];
			[ce dispatchThreads:MTLSizeMake(w, h, 1) threadsPerThreadgroup:MTLSizeMake(RMB_RTDIR_GROUP, RMB_RTDIR_GROUP, 1)];
		}

		[ce endEncoding];
		hist_cur = 1 - hist_cur;
		slot_acc[slot] = fno;
		slot_ttimed[slot] = timed;
		acc_view = 1;
	}

	return t_acc;
}

/* ---- TASK179: the effect table's rows (rm_rtfx.c) ---------------------------------- */

/* the direct light in the lightmap's place on the world, first of the traces: the others start from it */
void
rmb_rtdir_fx_trace(rmb_rtfx_io_t *io)
{
	id<MTLTexture> dl = rmb_rtdir_trace(io->depth, io->normal, io->material, io->lightmap, io->light);

	if (dl)
	{
		/* TASK190 slice 1: accumulated over the views where r_rt_temporal is above 0, else the trace's as it is */
		id<MTLTexture> acc = rmb_rtdir_accumulate(io->depth, io->normal, io->material, dl);

		io->light = acc ? acc : dl;
	}
}

void
rmb_rtdir_fx_show(rmb_rtfx_io_t *io)
{
	rmb_rtdir_show(io->enc);
}
