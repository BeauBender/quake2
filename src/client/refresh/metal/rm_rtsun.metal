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
	uint pad;
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

		if ((d < 1.0f) && (nn.a > 0.5f))
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

/* rm_rttemporal.h's rm_rtt_view_t (common/mtl_rtt_math.h's mtl_rtt_view_t) */
struct rm_rtt_view
{
	float alpha;
	float move;
	uint reset;
	uint seq;
};

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

		if (d < 1.0f)
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
