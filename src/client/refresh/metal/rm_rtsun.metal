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
 * The shaders of ref_metal's ray-traced sun and sky shadow (RT4, TASK56;
 * docs/plans/metal-raytracing-design.md, sections 2 and 3), linked into
 * ref_metal.metallib beside rm_rtao.metal:
 *
 * - rm_rtsun_trace, the compute pass after RT5's AO kernel and before the
 *   resolve: for each lightmapped world pixel (material 1) whose stylized
 *   lightmap is below r_rt_sunfloor (mtl_rtsun_gated) and which faces the
 *   sun (mtl_rtsun_ndotl), one ray toward the sun from the surface offset
 *   along its normal (TASK57's mtl_rtao_bias), against the frame's
 *   instance structure with MTL_RTAO_MASK (the world, the brush entities
 *   and the alias models but the view weapon and the viewer's own model).
 *   The nearest face decides (mtl_rtsun_at): the sky lights the pixel,
 *   water and translucent faces let the ray on (up to MTL_RTSUN_LAYERS),
 *   anything else, or nothing met, shadows it. Out: the pixel's class
 *   (R8Uint, MTL_RTSUN_C_*, which holds the one ray's visibility), the
 *   light the resolve reads (RGBA16Unorm): the light it would have read
 *   plus sky_color x r_rt_sunscale x visibility x N.L where the class is
 *   lit (mtl_rtsun_light), every other pixel's as it was; and the counts;
 * - rm_rtsun_temporal, RT7's accumulation of the sun (TASK59 slice 1b),
 *   after the trace in the same encoder while r_rt_temporal is above 0;
 * - rm_fragment_rtsun_show, r_rt_sun_show's two views over the resolve.
 *
 * =======================================================================
 */

#include <metal_stdlib>
#include <metal_raytracing>

using namespace metal;
using namespace metal::raytracing;

#include "common/mtl_shader_math.h"
#include "common/mtl_gbuf_math.h"
#include "common/mtl_rtp_math.h"   /* FIX219: the G-buffer depth and normal tests (mtl_rtp_below1, mtl_rtp_above_half) */
#include "common/mtl_rtsun_math.h"
#include "common/mtl_rtt_math.h"

/* rm_rtsun.h's rm_rtsun_view_t */
struct rm_rtsun_view
{
	float4x4 inv;
	float4 vp;
	float4 eye;
	float4 dir;
	float4 sun;
	float bias_k;
	float floor_;
	uint show;
	uint div;           /* TASK126: r_rt_sun_scale's divisor; the stock kernels do not read it (they run at 1) */
};

/* rm_gbuf.metal's rm_fs_varying (rm_vertex_fullscreen's output) */
struct rm_fs_varying
{
	float4 position [[position]];
	float2 uv;
};

/* the world position of a pixel's centre at its depth, as rm_rts_unproject (TASK55) */
static float3
rm_rtsun_position(constant rm_rtsun_view &v, uint2 px, float depth)
{
	const float cx = (float(px.x) + 0.5f - v.vp.x) / v.vp.z * 2.0f - 1.0f;
	const float cy = 1.0f - (float(px.y) + 0.5f - v.vp.y) / v.vp.w * 2.0f;
	const float4 h = v.inv * float4(cx, cy, depth * 2.0f - 1.0f, 1.0f);

	return h.xyz / h.w;
}

/*
 * 1 when r reaches the sky, 0 when it is shadowed: the nearest face
 * decides (mtl_rtsun_at); a water or translucent face is passed, the
 * nearest first, up to MTL_RTSUN_LAYERS of them, and what is behind the
 * last still decides; nothing met, or one more such face, is shadowed
 * (mtl_rtsun_step), so at most MTL_RTSUN_LAYERS + 1 intersections
 * (tests/metal/rm_rtsun_test.c mirrors this loop). inst holds two words an instance
 * (rmb_rt_frame): its first triangle of the map's buffers (~0 for an
 * alias model) and the lights it carries; tmat the map's material id of
 * each of its ntris triangles.
 */
static float
rm_rtsun_hit(ray r, instance_acceleration_structure ias, device const uint *inst, device const uchar *tmat, uint ntris)
{
	intersector<triangle_data, instancing> isect;

	isect.assume_geometry_type(geometry_type::triangle);

	for (int layer = 0; layer <= MTL_RTSUN_LAYERS; layer++)
	{
		intersection_result<triangle_data, instancing> hit = isect.intersect(r, ias, MTL_RTAO_MASK);
		uint b, m = 0xffu, at;

		if (hit.type == intersection_type::none)
		{
			return 0.0f;
		}

		b = inst[2 * hit.instance_id];

		/* a map triangle outside the table (none uploaded) counts as the world's */
		if (b != 0xffffffffu)
		{
			m = (b + hit.primitive_id < ntris) ? (uint)tmat[b + hit.primitive_id] : (uint)MTL_GB_MAT_WORLD;
		}

		at = mtl_rtsun_step(mtl_rtsun_at(m), layer);

		if (at != MTL_RTSUN_AT_PASS)
		{
			return (at == MTL_RTSUN_AT_LIT) ? 1.0f : 0.0f;
		}

		r.min_distance = hit.distance + 0.01f;

		if (!(r.min_distance < r.max_distance))
		{
			return 0.0f;
		}
	}

	return 0.0f;
}

/* the frame's counts (rmb_rtsun.m's RMB_RTSUN_COUNTS) */
#define RM_RTSUN_C_WORLD 0
#define RM_RTSUN_C_BRIGHT 1
#define RM_RTSUN_C_FACING 2
#define RM_RTSUN_C_RAYS 3
#define RM_RTSUN_C_LIT 4

/* rm_rttemporal.h's rm_rtt_view_t (common/mtl_rtt_math.h's mtl_rtt_view_t) */
struct rm_rtt_view
{
	float alpha;
	float move;
	uint reset;
	uint seq;
};

#ifndef RM_RTSUN_LO_TU

kernel void
rm_rtsun_trace(depth2d<float, access::read> depth [[texture(0)]],
		texture2d<float, access::read> normal [[texture(1)]],
		texture2d<uint, access::read> mat [[texture(2)]],
		texture2d<float, access::read> lm [[texture(3)]],
		texture2d<float, access::read> base [[texture(4)]],
		texture2d<uint, access::write> cls_out [[texture(5)]],
		texture2d<float, access::write> light_out [[texture(6)]],
		constant rm_rtsun_view &v [[buffer(0)]],
		instance_acceleration_structure ias [[buffer(2)]],
		device const uint *inst [[buffer(3)]],
		device const uchar *tmat [[buffer(4)]],
		constant uint &ntris [[buffer(5)]],
		device atomic_uint *counts [[buffer(6)]],
		uint2 px [[thread_position_in_grid]])
{
	const bool inside = (px.x < light_out.get_width()) && (px.y < light_out.get_height());
	uint m = MTL_GB_MAT_NONE, cls = MTL_RTSUN_C_NONE;
	float vis = 0.0f, nl = 0.0f;

	if (inside)
	{
		m = mat.read(px).r;
	}

	if (inside && (m == MTL_GB_MAT_WORLD))
	{
		const float d = depth.read(px);
		const float4 nn = normal.read(px);

		if (mtl_rtp_below1(d) && mtl_rtp_above_half(nn.a))
		{
			const float3 n = normalize(nn.xyz * 2.0f - 1.0f);
			const float4 l = lm.read(px);
			mtl_rgba_t c;

			c.r = l.r;
			c.g = l.g;
			c.b = l.b;
			c.a = l.a;

			if (!mtl_rtsun_gated(c, v.floor_))
			{
				cls = MTL_RTSUN_C_BRIGHT;
			}
			else
			{
				nl = mtl_rtsun_ndotl(mtl_rtao_v3(n.x, n.y, n.z), mtl_rtao_v3(v.dir.x, v.dir.y, v.dir.z));

				if (!(nl > 0.0f))
				{
					cls = MTL_RTSUN_C_FACING;
				}
				else
				{
					const float3 p = rm_rtsun_position(v, px, d);
					const float3 o = p + n * mtl_rtao_bias(distance(p, v.eye.xyz), v.bias_k);

					/* the 8-bit visibility; one ray, so 0 or 1 */
					vis = rint(rm_rtsun_hit(ray(o, v.dir.xyz, 0.0f, MTL_RTSUN_RANGE), ias, inst, tmat, ntris) * 255.0f) /
						255.0f;
					cls = (vis > 0.0f) ? MTL_RTSUN_C_LIT : MTL_RTSUN_C_SHADOW;
				}
			}
		}
	}

	if (inside)
	{
		const float4 b = base.read(px);
		mtl_rgba_t c;

		c.r = b.r;
		c.g = b.g;
		c.b = b.b;
		c.a = b.a;
		c = mtl_rtsun_light(c, m, cls, vis, nl, v.sun.x, v.sun.y, v.sun.z);
		cls_out.write(uint4(cls, 0u, 0u, 0u), px);
		light_out.write(float4(c.r, c.g, c.b, c.a), px);
	}

	/* the frame's counts, one atomic per SIMD group and count */
	{
		const uint world = (cls != MTL_RTSUN_C_NONE) ? 1u : 0u, bright = (cls == MTL_RTSUN_C_BRIGHT) ? 1u : 0u;
		const uint facing = (cls == MTL_RTSUN_C_FACING) ? 1u : 0u, lit = (cls == MTL_RTSUN_C_LIT) ? 1u : 0u;
		const uint rays = ((cls == MTL_RTSUN_C_SHADOW) || lit) ? 1u : 0u;
		const uint c_w = simd_sum(world), c_b = simd_sum(bright), c_f = simd_sum(facing);
		const uint c_r = simd_sum(rays), c_l = simd_sum(lit);

		if (simd_is_first())
		{
			atomic_fetch_add_explicit(&counts[RM_RTSUN_C_WORLD], c_w, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[RM_RTSUN_C_BRIGHT], c_b, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[RM_RTSUN_C_FACING], c_f, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[RM_RTSUN_C_RAYS], c_r, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[RM_RTSUN_C_LIT], c_l, memory_order_relaxed);
		}
	}
}

/*
 * RT7's temporal accumulation of the sun (TASK59 slice 1b), after the
 * trace in the same encoder, only while r_rt_temporal is above 0: each
 * traced pixel's visibility (class lit or shadowed) blended with its
 * history, read where the unjittered motion vector says the point was
 * (mtl_rtt_reproject) and rejected by mtl_rtt_reject, then the light
 * written again from the base with the accumulated visibility. The
 * history (RGBA32Float): the accumulated visibility, the distance from the
 * eye, the material id and the sample count, 0 where the pixel was not
 * traced (at or above the floor, facing away), so it is not read there.
 * The sun reaches no dynamic light, so its light set is 0 on both sides.
 */
kernel void
rm_rtsun_temporal(depth2d<float, access::read> depth [[texture(0)]],
		texture2d<float, access::read> normal [[texture(1)]],
		texture2d<uint, access::read> mat [[texture(2)]],
		texture2d<float, access::read> base [[texture(4)]],
		texture2d<uint, access::read> cls_in [[texture(5)]],
		texture2d<float, access::write> light_out [[texture(6)]],
		texture2d<float, access::read> motion [[texture(7)]],
		texture2d<float, access::read> hist_in [[texture(8)]],
		texture2d<float, access::write> hist_out [[texture(9)]],
		constant rm_rtsun_view &v [[buffer(0)]],
		constant rm_rtt_view &t [[buffer(9)]],
		uint2 px [[thread_position_in_grid]])
{
	const uint w = hist_out.get_width(), hh = hist_out.get_height();

	if ((px.x >= w) || (px.y >= hh))
	{
		return;
	}

	{
		const uint m = mat.read(px).r, cls = cls_in.read(px).r;
		const bool traced = (cls == MTL_RTSUN_C_LIT) || (cls == MTL_RTSUN_C_SHADOW);
		const float a = (cls == MTL_RTSUN_C_LIT) ? 1.0f : 0.0f;
		const float d = depth.read(px);
		const float2 mv = motion.read(px).xy;
		const mtl_rtt_px_t r = mtl_rtt_reproject(px.x, px.y, mv.x, mv.y, w, hh);
		const float4 h = r.ok ? hist_in.read(uint2(r.x, r.y)) : float4(0.0f);
		const uint hm = (uint)h.z;
		float dist = 0.0f;

		if (mtl_rtp_below1(d))
		{
			dist = distance(rm_rtsun_position(v, px, d), v.eye.xyz);
		}

		{
			const uint rej = mtl_rtt_reject(t.reset, r.ok, dist, h.y, t.move, m, hm, 0u, 0u, h.w);
			const float acc = mtl_rtt_blend(rej, a, h.x, h.w, t.alpha);

			hist_out.write(float4(acc, dist, (float)m, traced ? mtl_rtt_count(rej, h.w) : 0.0f), px);

			if (traced)
			{
				const float3 n = normalize(normal.read(px).xyz * 2.0f - 1.0f);
				const float nl = mtl_rtsun_ndotl(mtl_rtao_v3(n.x, n.y, n.z), mtl_rtao_v3(v.dir.x, v.dir.y, v.dir.z));
				const float4 b = base.read(px);
				mtl_rgba_t c;

				c.r = b.r;
				c.g = b.g;
				c.b = b.b;
				c.a = b.a;
				c = mtl_rtsun_light(c, m, (acc > 0.0f) ? MTL_RTSUN_C_LIT : MTL_RTSUN_C_SHADOW, acc, nl, v.sun.x, v.sun.y,
						v.sun.z);
				light_out.write(float4(c.r, c.g, c.b, c.a), px);
			}
		}
	}
}

/*
 * r_rt_sun_show over the resolved view: show 1, the mask of the masked
 * comparison, black where the term was added (class lit), white on every
 * other pixel; show 2, the classes: black not a traced world pixel, grey
 * at or above the floor, blue facing away, red shadowed, white lit.
 */
fragment float4
rm_fragment_rtsun_show(rm_fs_varying in [[stage_in]],
		constant rm_rtsun_view &v [[buffer(0)]],
		texture2d<uint> cls [[texture(0)]])
{
	const uint c = cls.read(uint2(in.position.xy)).r;

	if (v.show == 1)
	{
		return (c == MTL_RTSUN_C_LIT) ? float4(0.0f, 0.0f, 0.0f, 1.0f) : float4(1.0f, 1.0f, 1.0f, 1.0f);
	}

	switch (c)
	{
		case MTL_RTSUN_C_BRIGHT:
			return float4(0.5f, 0.5f, 0.5f, 1.0f);
		case MTL_RTSUN_C_FACING:
			return float4(0.0f, 0.0f, 1.0f, 1.0f);
		case MTL_RTSUN_C_SHADOW:
			return float4(1.0f, 0.0f, 0.0f, 1.0f);
		case MTL_RTSUN_C_LIT:
			return float4(1.0f, 1.0f, 1.0f, 1.0f);
		default:
			return float4(0.0f, 0.0f, 0.0f, 1.0f);
	}
}

#else /* RM_RTSUN_LO_TU */

/*
 * r_rt_sun_scale below 1 (TASK126 slice 3, option E), compiled apart by
 * rm_rtsun_lo.metal so that the stock kernels' unit holds no new caller
 * of the helpers above. At divisor d the rays are traced from one pixel
 * of each d x d block (TASK157's layout, common/mtl_rtup_math.h), RT7
 * accumulates and denoises the visibility at that size, and rm_rtsun_up
 * upsamples it to the render size and composes the light per drawn
 * pixel. The bindings are the stock trace's, with the traced size's class
 * at texture 10 and visibility at texture 11.
 */

#include "common/mtl_rtd_math.h"
#include "common/mtl_rtup_math.h"

/* the counts' slot of the upsample's fallbacks (rmb_rtsun.m's RMB_RTSUN_COUNTS) */
#define RM_RTSUN_C_FALLBACK 5

/* the render-size pixel traced pixel px takes its G-buffer sample from */
static uint2
rm_rtsun_lo_src(uint2 px, uint div, uint fw, uint fh)
{
	return uint2(mtl_rtup_src(px.x, div, fw), mtl_rtup_src(px.y, div, fh));
}

/* a normal as the G-buffer stores it, as a unit vector */
static mtl_rtao_v3_t
rm_rtsun_lo_normal(float4 nv)
{
	const float3 n = normalize(nv.xyz * 2.0f - 1.0f);

	return mtl_rtao_v3(n.x, n.y, n.z);
}

/*
 * The trace at the traced size: rm_rtsun_trace's class and ray for each
 * traced pixel, from its source pixel's G-buffer sample and lightmap. Out:
 * the class and the visibility (0 or 1; 0 where no ray was cast), no light;
 * and the frame's counts, of the traced pixels.
 */
kernel void
rm_rtsun_trace_lo(depth2d<float, access::read> depth [[texture(0)]],
		texture2d<float, access::read> normal [[texture(1)]],
		texture2d<uint, access::read> mat [[texture(2)]],
		texture2d<float, access::read> lm [[texture(3)]],
		texture2d<uint, access::write> cls_lo [[texture(10)]],
		texture2d<float, access::write> vis_lo [[texture(11)]],
		constant rm_rtsun_view &v [[buffer(0)]],
		instance_acceleration_structure ias [[buffer(2)]],
		device const uint *inst [[buffer(3)]],
		device const uchar *tmat [[buffer(4)]],
		constant uint &ntris [[buffer(5)]],
		device atomic_uint *counts [[buffer(6)]],
		uint2 px [[thread_position_in_grid]])
{
	const bool inside = (px.x < cls_lo.get_width()) && (px.y < cls_lo.get_height());
	uint cls = MTL_RTSUN_C_NONE;
	float vis = 0.0f;

	if (inside)
	{
		const uint2 sp = rm_rtsun_lo_src(px, v.div, depth.get_width(), depth.get_height());

		if (mat.read(sp).r == MTL_GB_MAT_WORLD)
		{
			const float d = depth.read(sp);
			const float4 nn = normal.read(sp);

			if (mtl_rtp_below1(d) && mtl_rtp_above_half(nn.a))
			{
				const mtl_rtao_v3_t n = rm_rtsun_lo_normal(nn);
				const float4 l = lm.read(sp);
				mtl_rgba_t c;

				c.r = l.r;
				c.g = l.g;
				c.b = l.b;
				c.a = l.a;

				if (!mtl_rtsun_gated(c, v.floor_))
				{
					cls = MTL_RTSUN_C_BRIGHT;
				}
				else if (!(mtl_rtsun_ndotl(n, mtl_rtao_v3(v.dir.x, v.dir.y, v.dir.z)) > 0.0f))
				{
					cls = MTL_RTSUN_C_FACING;
				}
				else
				{
					const float3 n3 = float3(n.x, n.y, n.z);
					const float3 p = rm_rtsun_position(v, sp, d);
					const float3 o = p + n3 * mtl_rtao_bias(distance(p, v.eye.xyz), v.bias_k);

					vis = rm_rtsun_hit(ray(o, v.dir.xyz, 0.0f, MTL_RTSUN_RANGE), ias, inst, tmat, ntris);
					cls = (vis > 0.0f) ? MTL_RTSUN_C_LIT : MTL_RTSUN_C_SHADOW;
				}
			}
		}

		cls_lo.write(uint4(cls, 0u, 0u, 0u), px);
		vis_lo.write(float4(vis, 0.0f, 0.0f, 0.0f), px);
	}

	{
		const uint world = (cls != MTL_RTSUN_C_NONE) ? 1u : 0u, bright = (cls == MTL_RTSUN_C_BRIGHT) ? 1u : 0u;
		const uint facing = (cls == MTL_RTSUN_C_FACING) ? 1u : 0u, lit = (cls == MTL_RTSUN_C_LIT) ? 1u : 0u;
		const uint rays = ((cls == MTL_RTSUN_C_SHADOW) || lit) ? 1u : 0u;
		const uint c_w = simd_sum(world), c_b = simd_sum(bright), c_f = simd_sum(facing);
		const uint c_r = simd_sum(rays), c_l = simd_sum(lit);

		if (simd_is_first())
		{
			atomic_fetch_add_explicit(&counts[RM_RTSUN_C_WORLD], c_w, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[RM_RTSUN_C_BRIGHT], c_b, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[RM_RTSUN_C_FACING], c_f, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[RM_RTSUN_C_RAYS], c_r, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[RM_RTSUN_C_LIT], c_l, memory_order_relaxed);
		}
	}
}

/*
 * RT7 at the traced size, as rm_rtsun_temporal: each traced pixel's
 * visibility blended with its history, read where the render-size motion
 * vector at its source pixel, over the divisor, says it was
 * (mtl_rtup_reproject), rejected by mtl_rtt_reject. The history
 * (RGBA32Float at the traced size) as rm_rtsun_temporal's; the visibility
 * written again from the accumulation, no light.
 */
kernel void
rm_rtsun_temporal_lo(depth2d<float, access::read> depth [[texture(0)]],
		texture2d<uint, access::read> mat [[texture(2)]],
		texture2d<float, access::read> motion [[texture(7)]],
		texture2d<float, access::read> hist_in [[texture(8)]],
		texture2d<float, access::write> hist_out [[texture(9)]],
		texture2d<uint, access::read> cls_lo [[texture(10)]],
		texture2d<float, access::write> vis_lo [[texture(11)]],
		constant rm_rtsun_view &v [[buffer(0)]],
		constant rm_rtt_view &t [[buffer(9)]],
		uint2 px [[thread_position_in_grid]])
{
	const uint w = hist_out.get_width(), hh = hist_out.get_height();

	if ((px.x >= w) || (px.y >= hh))
	{
		return;
	}

	{
		const uint2 sp = rm_rtsun_lo_src(px, v.div, depth.get_width(), depth.get_height());
		const uint m = mat.read(sp).r, cls = cls_lo.read(px).r;
		const bool traced = (cls == MTL_RTSUN_C_LIT) || (cls == MTL_RTSUN_C_SHADOW);
		const float a = (cls == MTL_RTSUN_C_LIT) ? 1.0f : 0.0f;
		const float d = depth.read(sp);
		const float2 mv = motion.read(sp).xy;
		const mtl_rtt_px_t r = mtl_rtup_reproject(px.x, px.y, mv.x, mv.y, v.div, w, hh);
		const float4 h = r.ok ? hist_in.read(uint2(r.x, r.y)) : float4(0.0f);
		const uint hm = (uint)h.z;
		float dist = 0.0f;

		if (mtl_rtp_below1(d))
		{
			dist = distance(rm_rtsun_position(v, sp, d), v.eye.xyz);
		}

		{
			const uint rej = mtl_rtt_reject(t.reset, r.ok, dist, h.y, t.move, m, hm, 0u, 0u, h.w);
			const float acc = mtl_rtt_blend(rej, a, h.x, h.w, t.alpha);

			hist_out.write(float4(acc, dist, (float)m, traced ? mtl_rtt_count(rej, h.w) : 0.0f), px);

			if (traced)
			{
				vis_lo.write(float4(acc, 0.0f, 0.0f, 0.0f), px);
			}
		}
	}
}

/*
 * RT7's 5 x 5 denoiser at the traced size, as rm_rtsun_denoise: on the
 * traced pixels whose count leaves a strength above 0, the visibility
 * moved toward its neighbours' (in traced pixels), each weighed by
 * mtl_rtd_weight against the history's distance and material and the
 * normal at its source pixel; the visibility written again, no light.
 */
kernel void
rm_rtsun_denoise_lo(depth2d<float, access::read> depth [[texture(0)]],
		texture2d<float, access::read> normal [[texture(1)]],
		texture2d<float, access::read> hist [[texture(8)]],
		texture2d<float, access::write> vis_lo [[texture(11)]],
		constant rm_rtsun_view &v [[buffer(0)]],
		uint2 px [[thread_position_in_grid]])
{
	const uint w = hist.get_width(), hh = hist.get_height(), fw = depth.get_width(), fh = depth.get_height();

	if ((px.x >= w) || (px.y >= hh))
	{
		return;
	}

	{
		const float4 hc = hist.read(px);
		const float4 nv = normal.read(rm_rtsun_lo_src(px, v.div, fw, fh));

		if (!(mtl_rtd_strength(hc.w) > 0.0f) || !mtl_rtp_above_half(nv.a))
		{
			return;
		}

		{
			const mtl_rtao_v3_t nc = rm_rtsun_lo_normal(nv);
			const uint mc = (uint)hc.z;
			mtl_rtd_acc_t acc = mtl_rtd_acc();

			for (int dy = -MTL_RTD_RADIUS; dy <= MTL_RTD_RADIUS; dy++)
			{
				for (int dx = -MTL_RTD_RADIUS; dx <= MTL_RTD_RADIUS; dx++)
				{
					const int sx = (int)px.x + dx, sy = (int)px.y + dy;

					if ((sx < 0) || (sy < 0) || (sx >= (int)w) || (sy >= (int)hh))
					{
						continue;
					}

					{
						const uint2 sp = uint2((uint)sx, (uint)sy);
						const float4 hn = hist.read(sp);
						const float4 nn = normal.read(rm_rtsun_lo_src(sp, v.div, fw, fh));
						const float wt = mtl_rtp_above_half(nn.a) ?
							mtl_rtd_weight(dx, dy, hc.y, hn.y, nc, rm_rtsun_lo_normal(nn), mc, (uint)hn.z, hn.w) : 0.0f;

						acc = mtl_rtd_add(acc, wt, hn.x);
					}
				}
			}

			vis_lo.write(float4(mtl_rtd_result(acc, hc.x, hc.w), 0.0f, 0.0f, 0.0f), px);
		}
	}
}

/*
 * The upsample at the render size: on each lightmapped world pixel with a
 * depth and a normal (a pixel without one is skipped before the rule, so
 * the fallbacks are not inflated), the floor gate and N.L of the pixel
 * itself, as rm_rtsun_trace; below the floor and facing the sun, the
 * visibility of the four traced pixels about it under the rule's weights
 * (mtl_rtup_weights: bilinear, RT7's depth and normal guides, the same
 * material, a traced sample), the one nearest in depth where none matches,
 * counted as a fallback. Where no traced pixel about it has a sample, the
 * sun's neutral value, 0: no term, as with the effect off. Then the class
 * and the light as rm_rtsun_trace writes them, with the pixel's own
 * max(0, N.L).
 */
kernel void
rm_rtsun_up(depth2d<float, access::read> depth [[texture(0)]],
		texture2d<float, access::read> normal [[texture(1)]],
		texture2d<uint, access::read> mat [[texture(2)]],
		texture2d<float, access::read> lm [[texture(3)]],
		texture2d<float, access::read> base [[texture(4)]],
		texture2d<uint, access::write> cls_out [[texture(5)]],
		texture2d<float, access::write> light_out [[texture(6)]],
		texture2d<uint, access::read> cls_lo [[texture(10)]],
		texture2d<float, access::read> vis_lo [[texture(11)]],
		constant rm_rtsun_view &v [[buffer(0)]],
		device atomic_uint *counts [[buffer(6)]],
		uint2 px [[thread_position_in_grid]])
{
	const uint fw = light_out.get_width(), fh = light_out.get_height();
	const bool inside = (px.x < fw) && (px.y < fh);
	uint m = MTL_GB_MAT_NONE, cls = MTL_RTSUN_C_NONE, fb = 0u;
	float vis = 0.0f, nl = 0.0f;

	if (inside)
	{
		m = mat.read(px).r;
	}

	if (inside && (m == MTL_GB_MAT_WORLD))
	{
		const float d = depth.read(px);
		const float4 nn = normal.read(px);

		if (mtl_rtp_below1(d) && mtl_rtp_above_half(nn.a))
		{
			const mtl_rtao_v3_t n = rm_rtsun_lo_normal(nn);
			const float4 l = lm.read(px);
			mtl_rgba_t c;

			c.r = l.r;
			c.g = l.g;
			c.b = l.b;
			c.a = l.a;

			if (!mtl_rtsun_gated(c, v.floor_))
			{
				cls = MTL_RTSUN_C_BRIGHT;
			}
			else
			{
				nl = mtl_rtsun_ndotl(n, mtl_rtao_v3(v.dir.x, v.dir.y, v.dir.z));

				if (!(nl > 0.0f))
				{
					cls = MTL_RTSUN_C_FACING;
				}
				else
				{
					const uint lw = cls_lo.get_width(), lh = cls_lo.get_height();
					const mtl_rtao_pair_t qx = mtl_rtao_up_pair(px.x, v.div, lw), qy = mtl_rtao_up_pair(px.y, v.div, lh);
					const uint tx[MTL_RTUP_TAPS] = { qx.lo0, qx.lo1, qx.lo0, qx.lo1 };
					const uint ty[MTL_RTUP_TAPS] = { qy.lo0, qy.lo0, qy.lo1, qy.lo1 };
					float tv[MTL_RTUP_TAPS];
					mtl_rtup_in_t in;
					mtl_rtup_out_t o;

					in.d = distance(rm_rtsun_position(v, px, d), v.eye.xyz);
					in.n = n;
					in.m = m;
					in = mtl_rtup_bilinear(in, qx, qy);

					for (int i = 0; i < MTL_RTUP_TAPS; i++)
					{
						const uint2 tp = uint2(tx[i], ty[i]);
						const uint2 sp = rm_rtsun_lo_src(tp, v.div, fw, fh);
						const uint tc = cls_lo.read(tp).r;
						const float ds = depth.read(sp);

						in.ok[i] = ((tc == MTL_RTSUN_C_LIT) || (tc == MTL_RTSUN_C_SHADOW)) ? 1 : 0;
						in.dn[i] = mtl_rtp_below1(ds) ? distance(rm_rtsun_position(v, sp, ds), v.eye.xyz) : 0.0f;
						in.nn[i] = rm_rtsun_lo_normal(normal.read(sp));
						in.mn[i] = mat.read(sp).r;
						tv[i] = vis_lo.read(tp).r;
					}

					o = mtl_rtup_weights(in, v.div);
					vis = mtl_rtup_value(o, tv[0], tv[1], tv[2], tv[3], 0.0f);
					fb = o.fallback ? 1u : 0u;
					cls = (vis > 0.0f) ? MTL_RTSUN_C_LIT : MTL_RTSUN_C_SHADOW;
				}
			}
		}
	}

	if (inside)
	{
		const float4 b = base.read(px);
		mtl_rgba_t c;

		c.r = b.r;
		c.g = b.g;
		c.b = b.b;
		c.a = b.a;
		c = mtl_rtsun_light(c, m, cls, vis, nl, v.sun.x, v.sun.y, v.sun.z);
		cls_out.write(uint4(cls, 0u, 0u, 0u), px);
		light_out.write(float4(c.r, c.g, c.b, c.a), px);
	}

	/* the upsample's fallbacks, one atomic per SIMD group */
	{
		const uint c_fb = simd_sum(fb);

		if (simd_is_first())
		{
			atomic_fetch_add_explicit(&counts[RM_RTSUN_C_FALLBACK], c_fb, memory_order_relaxed);
		}
	}
}

#endif /* RM_RTSUN_LO_TU */
