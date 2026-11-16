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
 * The Metal back end of ref_metal's ray tracing, RT1 (TASK53;
 * docs/plans/metal-raytracing-design.md, sections 1 and 3): the map's
 * primitive acceleration structures (the world's and one per inline
 * model, from the retained world buffers, at map load, on a command
 * buffer of their own that is waited for), each alias model's frames and
 * triangle list, a pool of per-instance structures that are built once
 * and then refit from the GPU lerp of the instance's vertices, and the
 * instance structure rebuilt every frame. The frame's work is encoded into
 * the frame's command buffer before the 3D pass: a compute encoder (the
 * lerps), an acceleration-structure encoder (the alias builds and refits)
 * and another one (the instance structure), with GPU timestamps at their
 * boundaries where the device samples them. Nothing reads the structures
 * yet. Every object is counted in the ledger as RM_OBJ_RT, so the
 * teardown line shows them all released.
 *
 * =======================================================================
 */

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <mach/mach_time.h>
#include <math.h>
#include <stdatomic.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "header/rm_rt.h"
#include "common/mtl_shader_math.h"
#include "common/mtl_rts_math.h"
#include "common/mtl_rtao_math.h"
#include "common/mtl_gbuf_math.h"
#include "common/mtl_rtw_math.h"

#if !__has_feature(objc_arc)
#error "rmb_rt.m is written for ARC: compile it with -fobjc-arc"
#endif

/* rmb_device.m */
extern id<MTLDevice> rmb_device;
extern id<MTLCommandQueue> rmb_queue;
extern rm_ledger_t rmb_ledger;
extern rmb_stats_t rmb_counts;
void rmb_watch(id<MTLCommandBuffer> cmd);
int rmb_commit_wait(id<MTLCommandBuffer> cmd);

/* rmb_resource.m */
void rmb_upload_flush(void);
id<MTLBuffer> rmb_static_buffer(int bid);
id<MTLLibrary> rmb_library(void);

/* rmb_encode.m */
id<MTLCommandBuffer> rmb_frame_compute(unsigned long *frame);
void *rmb_frame_ring_take(size_t n, __strong id<MTLBuffer> *buf, size_t *ofs);

#define RMB_RT_MAX_MAP (1 + 1024)      /* the world and MAX_MAP_MODELS inline models */
#define RMB_RT_MAX_MODELS 512          /* MAX_MOD_KNOWN */
#define RMB_RT_POOL (2 * RM_MAX_ENTITIES)
#define RMB_RT_ALIGN 256               /* of each scratch region */
#define RMB_RT_SAMPLES 6               /* per frame: start and end of the three encoders */
#define RMB_RT_LERP_GROUP 64

/* rm_rt.metal's rm_rt_lerp_u */
typedef struct
{
	float move[4], frontv[4], backv[4];
	unsigned frame, oldframe, n, pad;
} rmb_rt_lerp_u;

_Static_assert(sizeof(rmb_rt_lerp_u) == 64, "the lerp's uniforms are rm_rt.metal's 64 bytes");
_Static_assert(sizeof(MTLAccelerationStructureInstanceDescriptor) == RM_RT_INSTANCE_BYTES,
		"an instance descriptor is RM_RT_INSTANCE_BYTES");

/* an alias model's buffers and the sizes of one of its structures */
typedef struct
{
	id<MTLBuffer> frames, idx;
	long num_xyz, num_tris;
	size_t as_size, build_scratch, refit_scratch;
} rmb_rt_model_t;

/* a per-instance structure of the pool: its model, its lerped vertices, built yet, the frame it was last used in */
typedef struct
{
	int mid;
	int built;
	unsigned long used;
	id<MTLBuffer> verts;
	id<MTLAccelerationStructure> as;
} rmb_rt_entry_t;

static int rt_open;
static id<MTLComputePipelineState> lerp_pipe;

static id<MTLAccelerationStructure> map_as[RMB_RT_MAX_MAP];
static size_t map_bytes[RMB_RT_MAX_MAP];
static id<MTLBuffer> map_mat;   /* the world's per-triangle material ids */
static unsigned long map_first[RMB_RT_MAX_MAP];   /* each structure's first triangle of the world's index buffer */
static id<MTLBuffer> map_trans; /* the world's per-triangle transmission (RT3, rmb_rt_set_transmit) */
static unsigned long map_ntris;
static int map_vbuf, map_ibuf;     /* the retained buffers the map's structures were built from (RT6, rmb_rt_bind_geometry) */
static unsigned long map_water;   /* the map's water triangles (RT6, rmb_rt_map_water) */
static id<MTLBuffer> map_wlist;   /* their triangle ids, uint32 (TASK158, rmb_rt_bind_water_raster) */

/* the last frame's scene for a ray query (RT3): its structures, the instances' first triangles, its frame */
static NSArray<id<MTLAccelerationStructure>> *scene_structs;
static id<MTLBuffer> scene_base;
static size_t scene_base_ofs;
static unsigned long scene_frame;
static int scene_ok;

static rmb_rt_model_t models[RMB_RT_MAX_MODELS];
static rmb_rt_entry_t pool[RMB_RT_POOL];

static id<MTLAccelerationStructure> ias;
static size_t ias_size;
static id<MTLBuffer> scratch;
static size_t scratch_size;

/* the GPU timestamps: one slot of RMB_RT_SAMPLES per frame slot, its frame, and whether it is to be read */
static id<MTLCounterSampleBuffer> samples;
static atomic_int slot_done[RMB_FRAMES_IN_FLIGHT];
static int slot_pending[RMB_FRAMES_IN_FLIGHT];
static int slot_alias[RMB_FRAMES_IN_FLIGHT];
static unsigned long last_frame;
static MTLTimestamp cal_cpu, cal_gpu;
static uint64_t cal_ns, cal_mach;
static double times_alias[RMB_FRAMES_IN_FLIGHT], times_ias[RMB_FRAMES_IN_FLIGHT];
static int times_n;

/* Sets a strong slot to obj (nil releases it), counting both in the ledger as RM_OBJ_RT. */
#define RMB_RT_SET(slot, obj) \
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

static size_t
rmb_rt_align(size_t n)
{
	return (n + RMB_RT_ALIGN - 1) & ~(size_t)(RMB_RT_ALIGN - 1);
}

int
rmb_rt_open(char *err, size_t errlen)
{
	if (rt_open)
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
		id<MTLFunction> f = [rmb_library() newFunctionWithName:@"rm_rt_alias_lerp"];
		id<MTLCounterSet> ts = nil;

		if (!f)
		{
			snprintf(err, errlen, "the shader library has no rm_rt_alias_lerp");
			return 0;
		}

		RMB_RT_SET(lerp_pipe, [rmb_device newComputePipelineStateWithFunction:f error:&e]);

		if (!lerp_pipe)
		{
			snprintf(err, errlen, "cannot build the lerp pipeline%s%s", e ? ": " : "",
					e ? e.localizedDescription.UTF8String : "");
			return 0;
		}

		/* GPU timestamps at the encoders' boundaries, where the device samples them there */
		for (id<MTLCounterSet> cs in rmb_device.counterSets)
		{
			if ([cs.name isEqualToString:MTLCommonCounterSetTimestamp])
			{
				ts = cs;
			}
		}

		if (ts && [rmb_device supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary])
		{
			MTLCounterSampleBufferDescriptor *d = [MTLCounterSampleBufferDescriptor new];

			d.counterSet = ts;
			d.storageMode = MTLStorageModeShared;
			d.sampleCount = RMB_FRAMES_IN_FLIGHT * RMB_RT_SAMPLES;
			RMB_RT_SET(samples, [rmb_device newCounterSampleBufferWithDescriptor:d error:&e]);
			[rmb_device sampleTimestamps:&cal_cpu gpuTimestamp:&cal_gpu];
			cal_ns = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
			cal_mach = mach_absolute_time();
		}
	}

	memset(slot_pending, 0, sizeof(slot_pending));

	for (int i = 0; i < RMB_FRAMES_IN_FLIGHT; i++)
	{
		atomic_store(&slot_done[i], 0);
	}

	last_frame = 0;
	times_n = 0;
	rt_open = 1;
	return 1;
}

void
rmb_rt_free_map(void)
{
	int i;

	for (i = 0; i < RMB_RT_MAX_MAP; i++)
	{
		RMB_RT_SET(map_as[i], nil);
		map_bytes[i] = 0;
		map_first[i] = 0;
	}

	RMB_RT_SET(map_mat, nil);
	RMB_RT_SET(map_trans, nil);
	RMB_RT_SET(map_wlist, nil);
	map_ntris = 0;
	map_vbuf = map_ibuf = 0;
	map_water = 0;
	scene_ok = 0;
	scene_structs = nil;
	scene_base = nil;
}

static void
rmb_rt_entry_free(rmb_rt_entry_t *p)
{
	RMB_RT_SET(p->as, nil);
	RMB_RT_SET(p->verts, nil);
	p->mid = 0;
	p->built = 0;
	p->used = 0;
}

void
rmb_rt_model_free(int mid)
{
	int i;

	if ((mid <= 0) || (mid >= RMB_RT_MAX_MODELS))
	{
		return;
	}

	for (i = 0; i < RMB_RT_POOL; i++)
	{
		if (pool[i].mid == mid)
		{
			rmb_rt_entry_free(&pool[i]);
		}
	}

	RMB_RT_SET(models[mid].frames, nil);
	RMB_RT_SET(models[mid].idx, nil);
	models[mid].num_xyz = models[mid].num_tris = 0;
	models[mid].as_size = models[mid].build_scratch = models[mid].refit_scratch = 0;
}

/* The frames still in flight keep what they use: a command buffer retains its resources. */
void
rmb_rt_close(void)
{
	int i;

	@autoreleasepool
	{
		rmb_rt_free_map();

		for (i = 1; i < RMB_RT_MAX_MODELS; i++)
		{
			rmb_rt_model_free(i);
		}

		RMB_RT_SET(ias, nil);
		ias_size = 0;
		RMB_RT_SET(scratch, nil);
		scratch_size = 0;
		RMB_RT_SET(samples, nil);
		RMB_RT_SET(lerp_pipe, nil);
		scene_ok = 0;
		scene_structs = nil;
		scene_base = nil;
	}

	rt_open = 0;
}

/* a triangle geometry over a vertex buffer of the given stride and a uint16 or uint32 index range */
static MTLPrimitiveAccelerationStructureDescriptor *
rmb_rt_prim(id<MTLBuffer> vb, NSUInteger stride, id<MTLBuffer> ib, MTLIndexType type, NSUInteger ofs,
		NSUInteger ntris, MTLAccelerationStructureUsage usage)
{
	MTLAccelerationStructureTriangleGeometryDescriptor *g = [MTLAccelerationStructureTriangleGeometryDescriptor descriptor];
	MTLPrimitiveAccelerationStructureDescriptor *d = [MTLPrimitiveAccelerationStructureDescriptor descriptor];

	g.vertexBuffer = vb;
	g.vertexBufferOffset = 0;
	g.vertexStride = stride;

	if (@available(macOS 13.0, *))
	{
		g.vertexFormat = MTLAttributeFormatFloat3;
	}

	g.indexBuffer = ib;
	g.indexBufferOffset = ofs;
	g.indexType = type;
	g.triangleCount = ntris;
	g.opaque = YES;
	d.geometryDescriptors = @[ g ];
	d.usage = usage;
	return d;
}

int
rmb_rt_build_map(int vbuf, int ibuf, const rmb_rt_range_t *ranges, int nranges, const unsigned char *mat,
		unsigned long ntris_all, rm_rt_map_stats_t *s)
{
	id<MTLBuffer> vb = rmb_static_buffer(vbuf), ib = rmb_static_buffer(ibuf);
	int i, ok = 1;

	rmb_rt_free_map();
	s->world_bytes = s->inline_bytes = s->scratch_bytes = s->material_bytes = 0;
	s->build_ms = -1;

	if (!rt_open || !vb || !ib || !mat || (nranges < 1) || (nranges > RMB_RT_MAX_MAP) || (ntris_all == 0) ||
		(ib.length / 12 < ntris_all) || (ranges[0].ntris == 0))
	{
		return 0;
	}

	/* the retained buffers' blits are on the upload command buffer: committed first, so the builds read them */
	rmb_upload_flush();

	@autoreleasepool
	{
		MTLPrimitiveAccelerationStructureDescriptor *d[RMB_RT_MAX_MAP];
		size_t ofs[RMB_RT_MAX_MAP], total = 0;
		id<MTLBuffer> scr = nil;
		id<MTLCommandBuffer> cmd;
		id<MTLAccelerationStructureCommandEncoder> enc;

		for (i = 0; i < nranges && ok; i++)
		{
			MTLAccelerationStructureSizes sz;

			d[i] = nil;

			if (ranges[i].ntris == 0)
			{
				continue;
			}

			if ((ranges[i].first_tri > ntris_all) || (ranges[i].ntris > ntris_all - ranges[i].first_tri))
			{
				ok = 0;
				break;
			}

			d[i] = rmb_rt_prim(vb, RM_WVERT_STRIDE, ib, MTLIndexTypeUInt32, ranges[i].first_tri * 12,
					ranges[i].ntris, MTLAccelerationStructureUsageNone);
			sz = [rmb_device accelerationStructureSizesWithDescriptor:d[i]];
			RMB_RT_SET(map_as[i], [rmb_device newAccelerationStructureWithSize:sz.accelerationStructureSize]);

			if (!map_as[i])
			{
				ok = 0;
				break;
			}

			map_bytes[i] = map_as[i].size;
			map_first[i] = ranges[i].first_tri;
			ofs[i] = total;
			total += rmb_rt_align(sz.buildScratchBufferSize);
		}

		if (ok)
		{
			RMB_RT_SET(scr, [rmb_device newBufferWithLength:(total ? total : RMB_RT_ALIGN)
					options:MTLResourceStorageModePrivate]);
			RMB_RT_SET(map_mat, [rmb_device newBufferWithBytes:mat length:ntris_all
					options:MTLResourceStorageModeShared]);
			ok = scr && map_mat;
		}

		cmd = ok ? [rmb_queue commandBuffer] : nil;
		enc = cmd ? [cmd accelerationStructureCommandEncoder] : nil;

		if (!enc)
		{
			ok = 0;
		}

		for (i = 0; i < nranges && ok; i++)
		{
			if (d[i])
			{
				[enc buildAccelerationStructure:map_as[i] descriptor:d[i] scratchBuffer:scr scratchBufferOffset:ofs[i]];
			}
		}

		if (ok)
		{
			[enc endEncoding];
			ok = rmb_commit_wait(cmd);

			if (ok && (cmd.GPUEndTime > cmd.GPUStartTime))
			{
				s->build_ms = (cmd.GPUEndTime - cmd.GPUStartTime) * 1000.0;
			}
		}
		else if (enc)
		{
			[enc endEncoding];
		}

		s->scratch_bytes = total;
		RMB_RT_SET(scr, nil);
	}

	if (!ok)
	{
		rmb_rt_free_map();
		return 0;
	}

	s->world_bytes = map_bytes[0];
	s->material_bytes = ntris_all;
	map_ntris = ntris_all;
	map_vbuf = vbuf;
	map_ibuf = ibuf;
	map_water = 0;

	for (unsigned long t = 0; t < ntris_all; t++)
	{
		map_water += (mat[t] == RM_RT_MAT_WATER) ? 1 : 0;
	}

	/* TASK158: the water triangles' ids, which the reflections' candidates raster draws; none, no raster */
	if (map_water && (ntris_all <= 0xffffffffUL))
	{
		uint32_t *ids = malloc(map_water * sizeof(uint32_t));
		unsigned long k = 0;

		if (ids)
		{
			for (unsigned long t = 0; t < ntris_all; t++)
			{
				if (mat[t] == RM_RT_MAT_WATER)
				{
					ids[k++] = (uint32_t)t;
				}
			}

			@autoreleasepool
			{
				RMB_RT_SET(map_wlist, [rmb_device newBufferWithBytes:ids length:k * sizeof(uint32_t)
						options:MTLResourceStorageModeShared]);
			}

			free(ids);
		}
	}

	for (i = 1; i < nranges; i++)
	{
		s->inline_bytes += map_bytes[i];
	}

	return 1;
}

int
rmb_rt_model(const void *frames, size_t frame_bytes, long num_xyz, const unsigned short *idx, long num_tris)
{
	rmb_rt_model_t *m;
	int mid;

	if (!rt_open || !frames || !idx || (frame_bytes == 0) || (rm_rt_avert_bytes(num_xyz) == 0) ||
		(rm_rt_aindex_bytes(num_tris) == 0) || (frame_bytes > RMB_STATIC_MAX_BYTES))
	{
		return 0;
	}

	for (mid = 1; mid < RMB_RT_MAX_MODELS && models[mid].frames; mid++)
	{
	}

	if (mid == RMB_RT_MAX_MODELS)
	{
		return 0;
	}

	m = &models[mid];

	@autoreleasepool
	{
		MTLAccelerationStructureSizes sz;

		RMB_RT_SET(m->frames, [rmb_device newBufferWithBytes:frames length:frame_bytes
				options:MTLResourceStorageModeShared]);
		RMB_RT_SET(m->idx, [rmb_device newBufferWithBytes:idx length:rm_rt_aindex_bytes(num_tris)
				options:MTLResourceStorageModeShared]);

		if (!m->frames || !m->idx)
		{
			rmb_rt_model_free(mid);
			return 0;
		}

		/* the sizes of one instance's structure: its vertices are a buffer of the instance's own */
		sz = [rmb_device accelerationStructureSizesWithDescriptor:rmb_rt_prim(m->frames, RM_RT_AVERT_BYTES, m->idx,
				MTLIndexTypeUInt16, 0, (NSUInteger)num_tris, MTLAccelerationStructureUsageRefit)];
		m->num_xyz = num_xyz;
		m->num_tris = num_tris;
		m->as_size = sz.accelerationStructureSize;
		m->build_scratch = sz.buildScratchBufferSize;
		m->refit_scratch = sz.refitScratchBufferSize;
	}

	return mid;
}

/*
 * GPU ticks to milliseconds, through the device's correlation of its CPU
 * and GPU timestamps since rmb_rt_open. The CPU timestamp's unit is taken
 * from the clocks it moved with: nanoseconds, or mach_absolute_time ticks.
 * Without a correlation yet (no GPU tick passed), a tick is a nanosecond.
 */
static double
rmb_rt_ticks_ms(uint64_t ticks)
{
	static mach_timebase_info_data_t tb;
	MTLTimestamp cpu, gpu;
	double ns_per_tick = 1.0;

	if (tb.denom == 0)
	{
		mach_timebase_info(&tb);
	}

	[rmb_device sampleTimestamps:&cpu gpuTimestamp:&gpu];

	if ((gpu > cal_gpu) && (cpu > cal_cpu))
	{
		const double dcpu = (double)(cpu - cal_cpu);
		const double dns = (double)(clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - cal_ns);
		const double dmach = (double)(mach_absolute_time() - cal_mach);
		const double cpu_ns = (fabs(dcpu - dns) <= fabs(dcpu - dmach)) ? dcpu : dcpu * tb.numer / tb.denom;

		ns_per_tick = cpu_ns / (double)(gpu - cal_gpu);
	}

	return (double)ticks * ns_per_tick / 1.0e6;
}

/* Reads the timestamps of the frame slots whose frames completed since the last read. */
static void
rmb_rt_read_times(void)
{
	int k;

	if (!samples)
	{
		return;
	}

	for (k = 0; k < RMB_FRAMES_IN_FLIGHT; k++)
	{
		NSData *d;
		const MTLCounterResultTimestamp *t;

		if (!slot_pending[k] || !atomic_load(&slot_done[k]))
		{
			continue;
		}

		slot_pending[k] = 0;
		d = [samples resolveCounterRange:NSMakeRange((NSUInteger)k * RMB_RT_SAMPLES, RMB_RT_SAMPLES)];

		if (!d || (d.length < RMB_RT_SAMPLES * sizeof(MTLCounterResultTimestamp)) || (times_n >= RMB_FRAMES_IN_FLIGHT))
		{
			continue;
		}

		t = (const MTLCounterResultTimestamp *)d.bytes;

		/* samples 0 and 3: the lerps' start and the refits' end; 4 and 5: the instance rebuild's */
		if ((t[4].timestamp == MTLCounterErrorValue) || (t[5].timestamp == MTLCounterErrorValue) ||
			(t[5].timestamp < t[4].timestamp))
		{
			continue;
		}

		if (slot_alias[k])
		{
			if ((t[0].timestamp == MTLCounterErrorValue) || (t[3].timestamp == MTLCounterErrorValue) ||
				(t[3].timestamp < t[0].timestamp))
			{
				continue;
			}

			times_alias[times_n] = rmb_rt_ticks_ms(t[3].timestamp - t[0].timestamp);
		}
		else
		{
			times_alias[times_n] = 0;
		}

		times_ias[times_n] = rmb_rt_ticks_ms(t[5].timestamp - t[4].timestamp);
		times_n++;
	}
}

int
rmb_rt_times(double *alias_ms, double *ias_ms, int cap)
{
	int i, n = (times_n < cap) ? times_n : cap;

	for (i = 0; i < n; i++)
	{
		alias_ms[i] = times_alias[i];
		ias_ms[i] = times_ias[i];
	}

	times_n = 0;
	return n;
}

/* a pool entry for an instance of model mid in frame fno: one of the model's not used yet in this frame, else a free one, else the stalest */
static rmb_rt_entry_t *
rmb_rt_take(int mid, unsigned long fno)
{
	rmb_rt_entry_t *free_e = NULL, *old = NULL;
	int i;

	for (i = 0; i < RMB_RT_POOL; i++)
	{
		rmb_rt_entry_t *p = &pool[i];

		if ((p->mid == mid) && (p->used != fno))
		{
			p->used = fno;
			return p;
		}

		if (!p->mid && !free_e)
		{
			free_e = p;
		}
		else if (p->mid && (p->used != fno) && (!old || (p->used < old->used)))
		{
			old = p;
		}
	}

	if (!free_e)
	{
		if (!old)
		{
			return NULL;
		}

		rmb_rt_entry_free(old);
		free_e = old;
	}

	RMB_RT_SET(free_e->verts, [rmb_device newBufferWithLength:rm_rt_avert_bytes(models[mid].num_xyz)
			options:MTLResourceStorageModePrivate]);
	RMB_RT_SET(free_e->as, [rmb_device newAccelerationStructureWithSize:models[mid].as_size]);

	if (!free_e->verts || !free_e->as)
	{
		rmb_rt_entry_free(free_e);
		return NULL;
	}

	free_e->mid = mid;
	free_e->built = 0;
	free_e->used = fno;
	return free_e;
}

int
rmb_rt_frame(const rm_rt_inst_t *inst, int n, const rmb_rt_lerp_t *lerps, int nlerps, int shadows,
		rm_rt_frame_stats_t *s)
{
	unsigned long fno = 0;
	id<MTLCommandBuffer> cmd;
	rmb_rt_entry_t *ent[RM_RT_MAX_INSTANCES];
	int i, j, slot, timed, nalias = 0;

	rmb_rt_read_times();

	if (!rt_open || (n <= 0) || (n > RM_RT_MAX_INSTANCES) || (nlerps < 0) || (nlerps > n))
	{
		return 0;
	}

	scene_ok = 0;
	cmd = rmb_frame_compute(&fno);

	if (!cmd)
	{
		return 0;
	}

	slot = (int)(fno % RMB_FRAMES_IN_FLIGHT);
	/* one timed rebuild per frame (a second view in a frame is built, not timed), in a slot read already */
	timed = samples && (fno != last_frame) && !slot_pending[slot];
	last_frame = fno;

	@autoreleasepool
	{
		NSMutableArray<id<MTLAccelerationStructure>> *structs = [NSMutableArray arrayWithCapacity:(NSUInteger)n];
		static int inline_at[RMB_RT_MAX_MAP];
		static unsigned long inline_stamp[RMB_RT_MAX_MAP], stamp;
		MTLAccelerationStructureInstanceDescriptor *desc;
		MTLInstanceAccelerationStructureDescriptor *id_desc;
		MTLAccelerationStructureSizes isz;
		id<MTLBuffer> ring = nil, bring = nil;
		size_t ring_ofs = 0, bring_ofs = 0, need = 0, at;
		uint32_t *ibase = NULL;
		unsigned base = (unsigned)slot * RMB_RT_SAMPLES;

		stamp++;

		/* the pool entries of the alias instances, and the scratch they and the instance structure need */
		for (i = 0, j = 0; i < n; i++)
		{
			ent[i] = NULL;

			if (inst[i].kind == RM_RT_INST_ALIAS)
			{
				const rmb_rt_lerp_t *l = (j < nlerps) ? &lerps[j] : NULL;
				const rmb_rt_model_t *m;

				j++;

				if (!l || (l->mid <= 0) || (l->mid >= RMB_RT_MAX_MODELS) || !models[l->mid].frames ||
					(l->frame < 0) || (l->oldframe < 0))
				{
					s->skipped++;
					continue;
				}

				m = &models[l->mid];

				/* the lerp reads num_xyz vertices from each frame: inside the frames' buffer */
				if (((size_t)l->frame + (size_t)m->num_xyz > m->frames.length / 4) ||
					((size_t)l->oldframe + (size_t)m->num_xyz > m->frames.length / 4) ||
					!(ent[i] = rmb_rt_take(l->mid, fno)))
				{
					s->skipped++;
					continue;
				}

				need += rmb_rt_align(ent[i]->built ? m->refit_scratch : m->build_scratch);
				nalias++;
			}
			else if ((inst[i].model < 0) || (inst[i].model >= RMB_RT_MAX_MAP) || !map_as[inst[i].model])
			{
				s->skipped++;
			}
		}

		/* the instance descriptors, in the frame ring */
		desc = rmb_frame_ring_take(rm_rt_instance_bytes(n), &ring, &ring_ofs);

		/*
		 * with RT3's shadows, two words an instance for the ray queries: its
		 * first triangle of the map's buffers (~0 for an alias model) and the
		 * lights it carries (rm_rt_inst_t's carry)
		 */
		if (desc && shadows)
		{
			ibase = rmb_frame_ring_take((size_t)n * 2 * sizeof(uint32_t), &bring, &bring_ofs);
		}

		if (!desc || (shadows && !ibase))
		{
			s->skipped += (unsigned long)n;
			return 0;
		}

		for (i = 0, j = 0; i < n; i++)
		{
			id<MTLAccelerationStructure> as = nil;
			int c, k;

			if (inst[i].kind == RM_RT_INST_ALIAS)
			{
				as = ent[i] ? ent[i]->as : nil;
			}
			else if ((inst[i].model >= 0) && (inst[i].model < RMB_RT_MAX_MAP))
			{
				as = map_as[inst[i].model];
			}

			if (!as)
			{
				continue;
			}

			/* an inline model's structure once in the array, however many entities use it */
			if (inst[i].kind == RM_RT_INST_ALIAS)
			{
				k = (int)structs.count;
				[structs addObject:as];
			}
			else if (inline_stamp[inst[i].model] == stamp)
			{
				k = inline_at[inst[i].model];
			}
			else
			{
				k = (int)structs.count;
				[structs addObject:as];
				inline_stamp[inst[i].model] = stamp;
				inline_at[inst[i].model] = k;
			}

			memset(&desc[j], 0, sizeof(desc[j]));

			for (c = 0; c < 4; c++)
			{
				desc[j].transformationMatrix.columns[c].x = inst[i].xform[3 * c];
				desc[j].transformationMatrix.columns[c].y = inst[i].xform[3 * c + 1];
				desc[j].transformationMatrix.columns[c].z = inst[i].xform[3 * c + 2];
			}

			desc[j].options = MTLAccelerationStructureInstanceOptionDisableTriangleCulling;
			desc[j].mask = 0xff;
			desc[j].intersectionFunctionTableOffset = 0;
			desc[j].accelerationStructureIndex = (uint32_t)k;
			if (ibase)
			{
				const uint32_t carry = (inst[i].kind == RM_RT_INST_ALIAS) ? (uint32_t)inst[i].carry : 0;

				ibase[2 * j] = (inst[i].kind == RM_RT_INST_ALIAS) ? 0xffffffffu : (uint32_t)map_first[inst[i].model];
				ibase[2 * j + 1] = carry;

				/*
				 * A model that carries a light has its own mask: the shadow rays of
				 * that light test it apart and pass it. Every instance but the view
				 * weapon and the viewer's own model also has the AO rays' bit (RT5)
				 * and the reflection rays' (RT6, TASK58), each its own.
				 */
				desc[j].mask = mtl_rtw_inst_mask(mtl_rtao_inst_mask(mtl_rts_inst_mask(carry), inst[i].noao),
					inst[i].noao);
			}

			j++;

			if (inst[i].kind == RM_RT_INST_INLINE)
			{
				s->inlines++;
			}
		}

		if (j == 0)
		{
			return 0;
		}

		id_desc = [MTLInstanceAccelerationStructureDescriptor descriptor];
		id_desc.instanceDescriptorBuffer = ring;
		id_desc.instanceDescriptorBufferOffset = ring_ofs;
		id_desc.instanceDescriptorStride = RM_RT_INSTANCE_BYTES;
		id_desc.instanceCount = (NSUInteger)j;
		id_desc.instancedAccelerationStructures = structs;
		id_desc.instanceDescriptorType = MTLAccelerationStructureInstanceDescriptorTypeDefault;
		isz = [rmb_device accelerationStructureSizesWithDescriptor:id_desc];

		/* the instance structure grows when a frame needs more; it is rebuilt in place every frame */
		if (!ias || (ias_size < isz.accelerationStructureSize))
		{
			RMB_RT_SET(ias, [rmb_device newAccelerationStructureWithSize:isz.accelerationStructureSize]);
			ias_size = ias ? isz.accelerationStructureSize : 0;
		}

		at = need;
		need += rmb_rt_align(isz.buildScratchBufferSize);

		if (!scratch || (scratch_size < need))
		{
			RMB_RT_SET(scratch, [rmb_device newBufferWithLength:need options:MTLResourceStorageModePrivate]);
			scratch_size = scratch ? need : 0;
		}

		if (!ias || !scratch)
		{
			s->skipped += (unsigned long)n;
			return 0;
		}

		/* 1: the lerps of the alias instances' vertices */
		if (nalias)
		{
			id<MTLComputeCommandEncoder> ce;
			id<MTLAccelerationStructureCommandEncoder> ae;
			size_t sofs = 0;

			if (timed)
			{
				MTLComputePassDescriptor *cp = [MTLComputePassDescriptor computePassDescriptor];

				cp.sampleBufferAttachments[0].sampleBuffer = samples;
				cp.sampleBufferAttachments[0].startOfEncoderSampleIndex = base + 0;
				cp.sampleBufferAttachments[0].endOfEncoderSampleIndex = base + 1;
				ce = [cmd computeCommandEncoderWithDescriptor:cp];
			}
			else
			{
				ce = [cmd computeCommandEncoder];
			}

			[ce setComputePipelineState:lerp_pipe];

			for (i = 0, j = 0; i < n; i++)
			{
				const rmb_rt_lerp_t *l;
				rmb_rt_lerp_u u;
				int c;

				if (inst[i].kind != RM_RT_INST_ALIAS)
				{
					continue;
				}

				l = &lerps[j++];

				if (!ent[i])
				{
					continue;
				}

				memset(&u, 0, sizeof(u));

				for (c = 0; c < 3; c++)
				{
					u.move[c] = l->move[c];
					u.frontv[c] = l->frontv[c];
					u.backv[c] = l->backv[c];
				}

				u.frame = (unsigned)l->frame;
				u.oldframe = (unsigned)l->oldframe;
				u.n = (unsigned)models[l->mid].num_xyz;
				[ce setBuffer:models[l->mid].frames offset:0 atIndex:0];
				[ce setBytes:&u length:sizeof(u) atIndex:1];
				[ce setBuffer:ent[i]->verts offset:0 atIndex:2];
				[ce dispatchThreadgroups:MTLSizeMake((u.n + RMB_RT_LERP_GROUP - 1) / RMB_RT_LERP_GROUP, 1, 1)
					threadsPerThreadgroup:MTLSizeMake(RMB_RT_LERP_GROUP, 1, 1)];
			}

			[ce endEncoding];

			/* 2: each alias structure built on its first use, refit after (design section 1) */
			if (timed)
			{
				if (@available(macOS 13.0, *))
				{
					MTLAccelerationStructurePassDescriptor *ap = [MTLAccelerationStructurePassDescriptor
							accelerationStructurePassDescriptor];

					ap.sampleBufferAttachments[0].sampleBuffer = samples;
					ap.sampleBufferAttachments[0].startOfEncoderSampleIndex = base + 2;
					ap.sampleBufferAttachments[0].endOfEncoderSampleIndex = base + 3;
					ae = [cmd accelerationStructureCommandEncoderWithDescriptor:ap];
				}
				else
				{
					ae = [cmd accelerationStructureCommandEncoder];
					timed = 0;
				}
			}
			else
			{
				ae = [cmd accelerationStructureCommandEncoder];
			}

			for (i = 0, j = 0; i < n; i++)
			{
				const rmb_rt_lerp_t *l;
				const rmb_rt_model_t *m;
				MTLPrimitiveAccelerationStructureDescriptor *pd;

				if (inst[i].kind != RM_RT_INST_ALIAS)
				{
					continue;
				}

				l = &lerps[j++];

				if (!ent[i])
				{
					continue;
				}

				m = &models[l->mid];
				pd = rmb_rt_prim(ent[i]->verts, RM_RT_AVERT_BYTES, m->idx, MTLIndexTypeUInt16, 0,
						(NSUInteger)m->num_tris, MTLAccelerationStructureUsageRefit);

				if (ent[i]->built)
				{
					[ae refitAccelerationStructure:ent[i]->as descriptor:pd destination:ent[i]->as
						scratchBuffer:(m->refit_scratch ? scratch : nil) scratchBufferOffset:(m->refit_scratch ? sofs : 0)];
					sofs += rmb_rt_align(m->refit_scratch);
					s->refit++;
				}
				else
				{
					[ae buildAccelerationStructure:ent[i]->as descriptor:pd scratchBuffer:scratch scratchBufferOffset:sofs];
					sofs += rmb_rt_align(m->build_scratch);
					ent[i]->built = 1;
					s->built++;
				}

				s->aliases++;
				s->alias_tris += (unsigned long)m->num_tris;
			}

			[ae endEncoding];
		}

		/* 3: the instance structure, rebuilt */
		{
			id<MTLAccelerationStructureCommandEncoder> ae = nil;

			if (timed)
			{
				if (@available(macOS 13.0, *))
				{
					MTLAccelerationStructurePassDescriptor *ap = [MTLAccelerationStructurePassDescriptor
							accelerationStructurePassDescriptor];

					ap.sampleBufferAttachments[0].sampleBuffer = samples;
					ap.sampleBufferAttachments[0].startOfEncoderSampleIndex = base + 4;
					ap.sampleBufferAttachments[0].endOfEncoderSampleIndex = base + 5;
					ae = [cmd accelerationStructureCommandEncoderWithDescriptor:ap];
				}
			}

			if (!ae)
			{
				ae = [cmd accelerationStructureCommandEncoder];
				timed = 0;
			}

			[ae buildAccelerationStructure:ias descriptor:id_desc scratchBuffer:scratch scratchBufferOffset:at];
			[ae endEncoding];
		}

		if (timed)
		{
			atomic_store(&slot_done[slot], 0);
			slot_pending[slot] = 1;
			slot_alias[slot] = nalias != 0;
			[cmd addCompletedHandler:^(id<MTLCommandBuffer> b) {
				(void)b;
				atomic_store(&slot_done[slot], 1);
			}];
		}

		/* the scene of RT3's ray queries, kept only for the shadows */
		if (shadows)
		{
			scene_structs = [structs copy];
			scene_base = bring;
			scene_base_ofs = bring_ofs;
			scene_frame = fno;
			scene_ok = 1;
		}
		else
		{
			scene_structs = nil;
			scene_base = nil;
		}

		s->frame = fno;
		s->instances = (unsigned long)id_desc.instanceCount;
		s->ias_bytes = ias_size;
		s->scratch_bytes = scratch_size;

		for (i = 0; i < RMB_RT_POOL; i++)
		{
			if (pool[i].mid)
			{
				s->alias_bytes += pool[i].as.size;
				s->vertex_bytes += pool[i].verts.length;
			}
		}

		for (i = 1; i < RMB_RT_MAX_MODELS; i++)
		{
			if (models[i].frames)
			{
				s->model_bytes += models[i].frames.length + models[i].idx.length;
			}
		}
	}

	return 1;
}

/* ---- RT3 (TASK55): the scene for the shadow rays ---------------------------------------- */

int
rmb_rt_set_transmit(const unsigned char *t, unsigned long ntris)
{
	if (!t)
	{
		@autoreleasepool
		{
			RMB_RT_SET(map_trans, nil);
		}

		return 0;
	}

	if (!rt_open || (ntris == 0) || (ntris != map_ntris))
	{
		return 0;
	}

	@autoreleasepool
	{
		RMB_RT_SET(map_trans, [rmb_device newBufferWithBytes:t length:ntris options:MTLResourceStorageModeShared]);
	}

	return map_trans != nil;
}

int
rmb_rt_bind_scene(id<MTLComputeCommandEncoder> ce, unsigned long fno, int ias_at, int base_at, int trans_at,
		int ntris_at)
{
	uint32_t nt;

	if (!rt_open || !scene_ok || (scene_frame != fno) || !ias || !scene_base || !scene_structs.count)
	{
		return 0;
	}

	/* the instance structure, and every structure it holds marked used: a query reads them through it */
	[ce setAccelerationStructure:ias atBufferIndex:(NSUInteger)ias_at];

	for (id<MTLAccelerationStructure> as in scene_structs)
	{
		[ce useResource:as usage:MTLResourceUsageRead];
	}

	[ce setBuffer:scene_base offset:scene_base_ofs atIndex:(NSUInteger)base_at];

	/* without a transmission buffer every triangle stops a ray (the kernel reads it only with trans set) */
	nt = map_trans ? (uint32_t)map_ntris : 0;
	[ce setBuffer:(map_trans ? map_trans : scene_base) offset:(map_trans ? 0 : scene_base_ofs) atIndex:(NSUInteger)trans_at];
	[ce setBytes:&nt length:sizeof(nt) atIndex:(NSUInteger)ntris_at];
	return 1;
}

int
rmb_rt_bind_materials(id<MTLComputeCommandEncoder> ce, int mat_at, int n_at)
{
	uint32_t nt;

	if (!rt_open || !map_mat || !map_ntris || (map_ntris > 0xffffffffUL))
	{
		return 0;
	}

	nt = (uint32_t)map_ntris;
	[ce setBuffer:map_mat offset:0 atIndex:(NSUInteger)mat_at];
	[ce setBytes:&nt length:sizeof(nt) atIndex:(NSUInteger)n_at];
	return 1;
}

int
rmb_rt_bind_geometry(id<MTLComputeCommandEncoder> ce, int vert_at, int idx_at, int geom_at)
{
	id<MTLBuffer> vb = map_vbuf ? rmb_static_buffer(map_vbuf) : nil, ib = map_ibuf ? rmb_static_buffer(map_ibuf) : nil;
	uint32_t g[4];

	if (!rt_open || !map_ntris || !vb || !ib || (vb.length / RM_WVERT_STRIDE > 0xffffffffUL) ||
		(ib.length / 12 > 0xffffffffUL))
	{
		return 0;
	}

	g[0] = (uint32_t)(vb.length / RM_WVERT_STRIDE);
	g[1] = (uint32_t)(ib.length / 12);
	g[2] = RM_WVERT_STRIDE / 16;
	g[3] = 0;
	[ce setBuffer:vb offset:0 atIndex:(NSUInteger)vert_at];
	[ce setBuffer:ib offset:0 atIndex:(NSUInteger)idx_at];
	[ce setBytes:g length:sizeof(g) atIndex:(NSUInteger)geom_at];
	return 1;
}

unsigned long
rmb_rt_map_water(void)
{
	return (rt_open && map_ntris) ? map_water : 0;
}

/*
 * TASK158: the water triangles for a raster of them (rmb_rtwater.m's
 * candidates pass): their ids, the world's index and vertex buffers and
 * the counts rmb_rt_bind_geometry gives, as vertex buffers. The number of
 * triangles to draw, 0 when the map has none or its buffers are gone.
 */
unsigned long
rmb_rt_bind_water_raster(id<MTLRenderCommandEncoder> enc, int list_at, int idx_at, int vert_at, int geom_at)
{
	id<MTLBuffer> vb = map_vbuf ? rmb_static_buffer(map_vbuf) : nil, ib = map_ibuf ? rmb_static_buffer(map_ibuf) : nil;
	uint32_t g[4];

	if (!rt_open || !enc || !map_ntris || !map_water || !map_wlist || !vb || !ib ||
		(vb.length / RM_WVERT_STRIDE > 0xffffffffUL) || (ib.length / 12 > 0xffffffffUL) ||
		(map_wlist.length / sizeof(uint32_t) < map_water) || (map_water > 0xffffffffUL / 3))
	{
		return 0;
	}

	g[0] = (uint32_t)(vb.length / RM_WVERT_STRIDE);
	g[1] = (uint32_t)(ib.length / 12);
	g[2] = RM_WVERT_STRIDE / 16;
	g[3] = (uint32_t)map_water;
	[enc setVertexBuffer:map_wlist offset:0 atIndex:(NSUInteger)list_at];
	[enc setVertexBuffer:ib offset:0 atIndex:(NSUInteger)idx_at];
	[enc setVertexBuffer:vb offset:0 atIndex:(NSUInteger)vert_at];
	[enc setVertexBytes:g length:sizeof(g) atIndex:(NSUInteger)geom_at];
	return map_water;
}

double
rmb_rt_gpu_ms(uint64_t ticks)
{
	return rmb_rt_ticks_ms(ticks);
}
