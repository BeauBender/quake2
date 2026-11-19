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
 * The Metal back end of ref_metal's ray-traced dynamic-light shadows (RT3,
 * TASK55; header/rm_rtshadow.h): the kernel's and the resolve's
 * pipelines, the kernel's output (an RG32Uint at the render size), the
 * dispatch between the G-buffer raster and the resolve on the frame's
 * command buffer (rmb_gbuf_resolve calls rmb_rts_trace and
 * rmb_rts_resolve), and per frame slot the GPU counts and the pass's
 * timestamps, read once the frame completed. Nothing here is made before
 * the effect is first on (rmb_rts_open); every object is counted in the
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
#include "header/rm_rtshadow.h"

#if !__has_feature(objc_arc)
#error "rmb_rtshadow.m is written for ARC: compile it with -fobjc-arc"
#endif

/* rmb_device.m */
extern id<MTLDevice> rmb_device;
extern rm_ledger_t rmb_ledger;
extern rmb_stats_t rmb_counts;

/* rmb_resource.m */
id<MTLLibrary> rmb_library(void);

/* rmb_encode.m */
id<MTLCommandBuffer> rmb_frame_compute(unsigned long *frame);

#define RMB_RTS_GROUP 8         /* the kernel's threadgroup side */
#define RMB_RTS_COUNTS 4        /* in range, traced, shadowed, rays */

_Static_assert(sizeof(rm_rts_light_t) == 32, "a light is rm_rtshadow.metal's 32 bytes");
_Static_assert(sizeof(rm_rts_view_t) == 128, "the view is rm_rtshadow.metal's 128 bytes");

static int rts_open;
static id<MTLComputePipelineState> trace_pipe;
static id<MTLRenderPipelineState> resolve_pipe;
static id<MTLTexture> t_out;
static id<MTLTexture> t_term;           /* the term, RGBA16Float at the render size */
static id<MTLBuffer> m_tri, m_vecs;     /* the map's triangle-to-surface table and surfaces' texture axes */
static id<MTLBuffer> m_vert;            /* its vertex-to-surface table (TASK131), nil without one */
static uint32_t m_n[4];                 /* their triangles, surfaces and vertices (rm_rtshadow.metal's rm_rts_mapn) */
static id<MTLBuffer> m_ents;            /* RMB_FRAMES_IN_FLIGHT slots of the entity table (TASK131) */
static float ents[MTL_RTS_GB_ENTS * RM_RTS_ROW_FLOATS];
static int nents;
static id<MTLBuffer> counts;            /* RMB_FRAMES_IN_FLIGHT slots of RMB_RTS_COUNTS */
static id<MTLCounterSampleBuffer> samples;

/* per slot: the frame whose pass completed (set by its handler), the frame whose counts are awaited */
static atomic_ulong slot_done[RMB_FRAMES_IN_FLIGHT];
static int slot_pending[RMB_FRAMES_IN_FLIGHT];
static int slot_timed[RMB_FRAMES_IN_FLIGHT];
static unsigned long slot_frame[RMB_FRAMES_IN_FLIGHT], slot_pixels[RMB_FRAMES_IN_FLIGHT];
static rm_rts_stats_t done[RMB_FRAMES_IN_FLIGHT];
static int ndone;

/* the view the next G-buffer view traces with, whether this one traced, and its r_rt_shadows_show */
static rm_rts_view_t view;
static rm_rts_light_t lights[MTL_RTS_MAX_LIGHTS];
static int view_set, traced;
static unsigned view_show;

/* Sets a strong slot to obj (nil releases it), counting both in the ledger as RM_OBJ_RT. */
#define RMB_RTS_SET(slot, obj) \
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
rmb_rts_open(char *err, size_t errlen)
{
	if (rts_open)
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
		id<MTLFunction> k = [rmb_library() newFunctionWithName:@"rm_rts_trace"];
		MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
		id<MTLCounterSet> ts = nil;

		d.vertexFunction = [rmb_library() newFunctionWithName:@"rm_vertex_fullscreen"];
		d.fragmentFunction = [rmb_library() newFunctionWithName:@"rm_fragment_resolve_rts"];
		d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

		if (!k || !d.vertexFunction || !d.fragmentFunction)
		{
			snprintf(err, errlen, "the shader library has no rm_rts_trace or rm_fragment_resolve_rts");
			return 0;
		}

		RMB_RTS_SET(trace_pipe, [rmb_device newComputePipelineStateWithFunction:k error:&e]);
		RMB_RTS_SET(resolve_pipe, trace_pipe ? rmb_new_render_pipe(d, &e) : nil);
		RMB_RTS_SET(counts, [rmb_device newBufferWithLength:RMB_FRAMES_IN_FLIGHT * RMB_RTS_COUNTS * sizeof(uint32_t)
				options:MTLResourceStorageModeShared]);
		RMB_RTS_SET(m_ents, [rmb_device newBufferWithLength:RMB_FRAMES_IN_FLIGHT * sizeof(ents)
				options:MTLResourceStorageModeShared]);

		if (!trace_pipe || !resolve_pipe || !counts || !m_ents)
		{
			snprintf(err, errlen, "cannot build the shadow pipelines%s%s", e ? ": " : "",
					e ? e.localizedDescription.UTF8String : "");
			rmb_rts_close();
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
			RMB_RTS_SET(samples, [rmb_device newCounterSampleBufferWithDescriptor:sd error:&e]);
		}
	}

	for (int i = 0; i < RMB_FRAMES_IN_FLIGHT; i++)
	{
		atomic_store(&slot_done[i], ~0UL);   /* no frame */
		slot_pending[i] = 0;
	}

	ndone = 0;
	view_set = traced = 0;
	view_show = 0;
	rts_open = 1;
	return 1;
}

/* The frames still in flight keep what they use: a command buffer retains its resources. */
void
rmb_rts_close(void)
{
	@autoreleasepool
	{
		RMB_RTS_SET(trace_pipe, nil);
		RMB_RTS_SET(resolve_pipe, nil);
		RMB_RTS_SET(t_out, nil);
		RMB_RTS_SET(t_term, nil);
		RMB_RTS_SET(m_tri, nil);
		RMB_RTS_SET(m_vecs, nil);
		RMB_RTS_SET(m_vert, nil);
		RMB_RTS_SET(m_ents, nil);
		RMB_RTS_SET(counts, nil);
		RMB_RTS_SET(samples, nil);
	}

	rts_open = 0;
	view_set = traced = 0;
	view_show = 0;
	ndone = 0;
	nents = 0;
	m_n[0] = m_n[1] = m_n[2] = m_n[3] = 0;
}

int
rmb_rts_prepare(unsigned long w, unsigned long h)
{
	if (!rts_open || !trace_pipe || !resolve_pipe || !counts || (w < 1) || (h < 1))
	{
		return 0;
	}

	/* the outputs at the render size, made again when it changes */
	if (t_out && t_term && (t_out.width == w) && (t_out.height == h))
	{
		return 1;
	}

	@autoreleasepool
	{
		MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRG32Uint
				width:w height:h mipmapped:NO];

		d.storageMode = MTLStorageModePrivate;
		d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
		RMB_RTS_SET(t_out, [rmb_device newTextureWithDescriptor:d]);
		d.pixelFormat = MTLPixelFormatRGBA16Float;
		RMB_RTS_SET(t_term, [rmb_device newTextureWithDescriptor:d]);

		if (!t_out || !t_term)
		{
			RMB_RTS_SET(t_out, nil);
			RMB_RTS_SET(t_term, nil);
			return 0;
		}

		fprintf(stderr, "ref_metal: rt shadows %lux%lu: %lu bytes allocated\n", w, h,
				(unsigned long)(t_out.allocatedSize + t_term.allocatedSize));
	}

	return 1;
}

int
rmb_rts_set_map(const unsigned *tri_surf, unsigned long ntris, const unsigned *vert_surf, unsigned long nverts,
		const float *vecs, unsigned long nsurf)
{
	@autoreleasepool
	{
		RMB_RTS_SET(m_tri, nil);
		RMB_RTS_SET(m_vecs, nil);
		RMB_RTS_SET(m_vert, nil);
		m_n[0] = m_n[1] = m_n[2] = m_n[3] = 0;

		if (!rts_open || !tri_surf || !vecs || !ntris || !nsurf || (ntris > 0xffffffffu) || (nsurf > 0xffffffffu))
		{
			return 0;
		}

		RMB_RTS_SET(m_tri, [rmb_device newBufferWithBytes:tri_surf length:ntris * sizeof(unsigned)
				options:MTLResourceStorageModeShared]);
		RMB_RTS_SET(m_vecs, [rmb_device newBufferWithBytes:vecs length:nsurf * 12 * sizeof(float)
				options:MTLResourceStorageModeShared]);

		if (!m_tri || !m_vecs)
		{
			RMB_RTS_SET(m_tri, nil);
			RMB_RTS_SET(m_vecs, nil);
			return 0;
		}

		m_n[0] = (uint32_t)ntris;
		m_n[1] = (uint32_t)nsurf;

		/* the G-buffer's lookup (TASK131); without its table every pixel takes the eye ray */
		if (vert_surf && nverts && (nverts <= MTL_RTS_GB_VMAX))
		{
			RMB_RTS_SET(m_vert, [rmb_device newBufferWithBytes:vert_surf length:nverts * sizeof(unsigned)
					options:MTLResourceStorageModeShared]);
			m_n[2] = m_vert ? (uint32_t)nverts : 0;
		}
	}

	return 1;
}

void
rmb_rts_set_ents(const float *rows, int n)
{
	nents = 0;

	if (!rows || (n <= 0))
	{
		return;
	}

	if (n > (int)MTL_RTS_GB_ENTS)
	{
		n = (int)MTL_RTS_GB_ENTS;
	}

	memcpy(ents, rows, (size_t)n * RM_RTS_ROW_FLOATS * sizeof(float));
	nents = n;
}

void
rmb_rts_set_view(const rm_rts_view_t *v, const rm_rts_light_t *l, int n)
{
	view_set = 0;
	view_show = (rts_open && v) ? v->show : 0;

	if (!rts_open || !v || !l || (n <= 0))
	{
		return;
	}

	if (n > MTL_RTS_MAX_LIGHTS)
	{
		n = MTL_RTS_MAX_LIGHTS;
	}

	view = *v;
	view.nlights = (unsigned)n;
	memcpy(lights, l, (size_t)n * sizeof(lights[0]));
	view_set = 1;
}

/* the counts and times of the slots whose frames completed since the last read */
static void
rmb_rts_read(void)
{
	int k;

	for (k = 0; k < RMB_FRAMES_IN_FLIGHT; k++)
	{
		const uint32_t *c;
		rm_rts_stats_t *s;

		if (!slot_pending[k] || (atomic_load(&slot_done[k]) != slot_frame[k]))
		{
			continue;
		}

		slot_pending[k] = 0;

		if (ndone >= RMB_FRAMES_IN_FLIGHT)
		{
			continue;
		}

		c = (const uint32_t *)counts.contents + k * RMB_RTS_COUNTS;
		s = &done[ndone++];
		memset(s, 0, sizeof(*s));
		s->frame = slot_frame[k];
		s->pixels = slot_pixels[k];
		s->inrange = c[0];
		s->traced = c[1];
		s->shadowed = c[2];
		s->rays = c[3];
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
rmb_rts_stats(rm_rts_stats_t *s, int cap)
{
	int i, n;

	if (!rts_open)
	{
		return 0;
	}

	rmb_rts_read();
	n = (ndone < cap) ? ndone : cap;

	for (i = 0; i < n; i++)
	{
		s[i] = done[i];
	}

	ndone = 0;
	return n;
}

int
rmb_rts_trace(id<MTLTexture> depth, id<MTLTexture> normal, id<MTLTexture> material, id<MTLTexture> surface)
{
	unsigned long fno = 0;
	id<MTLCommandBuffer> cmd;
	id<MTLComputeCommandEncoder> ce;
	const NSUInteger w = depth.width, h = depth.height;
	int slot, timed;

	traced = 0;

	if (!rts_open || !view_set || !depth || !normal || !material)
	{
		return 0;
	}

	view_set = 0;
	rmb_rts_read();

	/* the outputs, made by rmb_rts_prepare before the raster at this size */
	if (!t_out || !t_term || (t_out.width != w) || (t_out.height != h))
	{
		return 0;
	}

	@autoreleasepool
	{
		/* ends the raster: the kernel reads what it wrote */
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
		memset((uint32_t *)counts.contents + slot * RMB_RTS_COUNTS, 0, RMB_RTS_COUNTS * sizeof(uint32_t));

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

		if (!rmb_rt_bind_scene(ce, fno, 2, 3, 4, 5))
		{
			[ce endEncoding];
			return 0;
		}

		/*
		 * TASK131: the surface attachment at the same size, and the frame's
		 * entity table in this slot of the ring (the frame that used the slot
		 * before has completed, as for the counts). Without the attachment
		 * the material stands in for it, of the same type, unread (gsurf 0).
		 */
		view.gsurf = (surface && (surface.width == w) && (surface.height == h) &&
			(surface.pixelFormat == MTLPixelFormatR32Uint)) ? 1u : 0u;
		view.nents = (unsigned)nents;
		memcpy((unsigned char *)m_ents.contents + (size_t)slot * sizeof(ents), ents,
				(size_t)nents * RM_RTS_ROW_FLOATS * sizeof(float));

		[ce setTexture:depth atIndex:0];
		[ce setTexture:normal atIndex:1];
		[ce setTexture:material atIndex:2];
		[ce setTexture:t_out atIndex:3];
		[ce setTexture:t_term atIndex:4];
		[ce setTexture:(view.gsurf ? surface : material) atIndex:5];
		[ce setBytes:&view length:sizeof(view) atIndex:0];
		[ce setBytes:lights length:sizeof(lights) atIndex:1];
		[ce setBuffer:counts offset:(NSUInteger)slot * RMB_RTS_COUNTS * sizeof(uint32_t) atIndex:6];

		/* the map's tables; without them (0 surfaces) the kernel takes the base axes and reads neither */
		[ce setBuffer:(m_tri ? m_tri : counts) offset:0 atIndex:7];
		[ce setBuffer:(m_vecs ? m_vecs : counts) offset:0 atIndex:8];
		[ce setBytes:m_n length:sizeof(m_n) atIndex:9];
		[ce setBuffer:(m_vert ? m_vert : counts) offset:0 atIndex:10];
		[ce setBuffer:m_ents offset:(NSUInteger)slot * sizeof(ents) atIndex:11];
		[ce dispatchThreads:MTLSizeMake(w, h, 1) threadsPerThreadgroup:MTLSizeMake(RMB_RTS_GROUP, RMB_RTS_GROUP, 1)];
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
	return 1;
}

int
rmb_rts_resolve(id<MTLRenderCommandEncoder> enc, const void *resolve, size_t resolve_len,
		id<MTLTexture> albedo, id<MTLTexture> light, id<MTLTexture> material, id<MTLTexture> depth,
		id<MTLTexture> normal)
{
	const int did = traced && t_out && t_term;
	const unsigned show = view_show;
	rm_rts_view_t none;

	(void)depth;
	(void)normal;
	traced = 0;
	view_show = 0;

	/* nothing traced and no development view asked for: the stock resolve */
	if (!rts_open || !enc || !resolve_pipe || (!did && !show))
	{
		return 0;
	}

	[enc setRenderPipelineState:rmb_fmt_pipe(resolve_pipe)];
	[enc setFragmentBytes:resolve length:resolve_len atIndex:0];
	[enc setFragmentTexture:albedo atIndex:0];
	[enc setFragmentTexture:light atIndex:1];
	[enc setFragmentTexture:material atIndex:2];

	if (did)
	{
		[enc setFragmentBytes:&view length:sizeof(view) atIndex:1];
		[enc setFragmentTexture:t_out atIndex:5];
		[enc setFragmentTexture:t_term atIndex:6];
	}
	else
	{
		/*
		 * r_rt_shadows_show on a view that traced nothing (no light, or the
		 * view could not be inverted): its view with no light, so no pixel
		 * is in range (the shader reads no slot then); the material and the
		 * light stand in for the two outputs, of the same types, unread.
		 */
		memset(&none, 0, sizeof(none));
		none.show = show;
		[enc setFragmentBytes:&none length:sizeof(none) atIndex:1];
		[enc setFragmentTexture:material atIndex:5];
		[enc setFragmentTexture:light atIndex:6];
	}

	[enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
	rmb_counts.draws++;
	return 1;
}
