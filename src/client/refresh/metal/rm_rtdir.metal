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
 * The shaders of RT-primary's direct light (TASK188, PM6 slice 1;
 * docs/plans/presentation-modes.md, section 4.1), linked into
 * ref_metal.metallib beside rm_rtsun.metal:
 *
 * - rm_rtdir_trace, the compute pass after the G-buffer raster and before
 *   the other effects' kernels and the resolve: for each lightmapped world
 *   pixel (material 1), the sum over the map's lights in range of qrad3's
 *   (intensity - dist) x N.L (mtl_rtdir_scale) times the light's colour
 *   and lightstyle, each light with one shadow ray from the surface
 *   offset along its normal (TASK57's mtl_rtao_bias) to the light,
 *   against the frame's instance structure with MTL_RTAO_MASK. Out: the
 *   light the resolve reads (RGBA16Unorm): that sum in the lightmap's
 *   units times the exposure (mtl_rtdir_light), never the pixel's
 *   lightmap; every other pixel's light as it was. The visibility
 *   (RGBA8Unorm: the share of the lights in range seen, whether any was,
 *   whether the pixel is a traced world pixel) for the development view.
 *   The counts and the exposure fit's sums;
 * - rm_fragment_rtdir_show, r_rt_direct_show's views over the resolve.
 *
 * =======================================================================
 */

#include <metal_stdlib>
#include <metal_raytracing>

using namespace metal;
using namespace metal::raytracing;

#include "common/mtl_shader_math.h"
#include "common/mtl_gbuf_math.h"
#include "common/mtl_rtao_math.h"
#include "common/mtl_rtdir_math.h"

/* rm_rtdir.h's rm_rtdir_view_t */
struct rm_rtdir_view
{
	float4x4 inv;
	float4 vp;
	float4 eye;
	float bias_k;
	float exposure;
	uint nlights;
	uint show;
};

/* rm_rtdir.h's rm_rtdir_light_t */
struct rm_rtdir_light
{
	float4 pos;     /* origin, intensity */
	float4 color;   /* rgb, style */
	float4 spot;    /* direction, cos(cone) or MTL_RTDIR_NOSPOT */
};

/* rm_gbuf.metal's rm_fs_varying (rm_vertex_fullscreen's output) */
struct rm_fs_varying
{
	float4 position [[position]];
	float2 uv;
};

/* the world position of a pixel's centre at its depth, as rm_rts_unproject (TASK55) */
static float3
rm_rtdir_position(constant rm_rtdir_view &v, uint2 px, float depth)
{
	const float cx = (float(px.x) + 0.5f - v.vp.x) / v.vp.z * 2.0f - 1.0f;
	const float cy = 1.0f - (float(px.y) + 0.5f - v.vp.y) / v.vp.w * 2.0f;
	const float4 h = v.inv * float4(cx, cy, depth * 2.0f - 1.0f, 1.0f);

	return h.xyz / h.w;
}

/*
 * 1 when r reaches its end (the light) unblocked, 0 when a face shadows
 * it: the nearest face decides (mtl_rtdir_at); a water or translucent
 * face is passed, the nearest first, up to MTL_RTDIR_LAYERS of them.
 * inst holds two words an instance (rmb_rt_frame): its first triangle of
 * the map's buffers (~0 for an alias model) and the lights it carries;
 * tmat the map's material id of each of its ntris triangles.
 */
static float
rm_rtdir_seen(ray r, instance_acceleration_structure ias, device const uint *inst, device const uchar *tmat, uint ntris)
{
	intersector<triangle_data, instancing> isect;

	isect.assume_geometry_type(geometry_type::triangle);

	for (int layer = 0; layer <= MTL_RTDIR_LAYERS; layer++)
	{
		intersection_result<triangle_data, instancing> hit = isect.intersect(r, ias, MTL_RTAO_MASK);
		uint b, m = 0xffu;

		if (hit.type == intersection_type::none)
		{
			return 1.0f;
		}

		b = inst[2 * hit.instance_id];

		/* a map triangle outside the table (none uploaded) counts as the world's */
		if (b != 0xffffffffu)
		{
			m = (b + hit.primitive_id < ntris) ? (uint)tmat[b + hit.primitive_id] : (uint)MTL_GB_MAT_WORLD;
		}

		if (mtl_rtdir_at(m, layer) != MTL_RTDIR_AT_PASS)
		{
			return 0.0f;
		}

		r.min_distance = hit.distance + 0.01f;

		if (!(r.min_distance < r.max_distance))
		{
			return 1.0f;
		}
	}

	return 0.0f;
}

/* the frame's counts (rmb_rtdir.m's RMB_RTDIR_COUNTS) */
#define RM_RTDIR_C_WORLD 0
#define RM_RTDIR_C_INRANGE 1
#define RM_RTDIR_C_RAYS 2
#define RM_RTDIR_C_LIT 3
#define RM_RTDIR_C_FIT_LM 4
#define RM_RTDIR_C_FIT_RAW 5
#define RM_RTDIR_C_FIT_N 6

kernel void
rm_rtdir_trace(depth2d<float, access::read> depth [[texture(0)]],
		texture2d<float, access::read> normal [[texture(1)]],
		texture2d<uint, access::read> mat [[texture(2)]],
		texture2d<float, access::read> lm [[texture(3)]],
		texture2d<float, access::read> base [[texture(4)]],
		texture2d<float, access::write> vis_out [[texture(5)]],
		texture2d<float, access::write> light_out [[texture(6)]],
		constant rm_rtdir_view &v [[buffer(0)]],
		constant packed_float3 *styles [[buffer(1)]],
		instance_acceleration_structure ias [[buffer(2)]],
		device const uint *inst [[buffer(3)]],
		device const uchar *tmat [[buffer(4)]],
		constant uint &ntris [[buffer(5)]],
		device atomic_uint *counts [[buffer(6)]],
		device const rm_rtdir_light *lights [[buffer(9)]],
		uint2 px [[thread_position_in_grid]])
{
	const bool inside = (px.x < light_out.get_width()) && (px.y < light_out.get_height());
	uint m = MTL_GB_MAT_NONE, world = 0u, inrange = 0u, rays = 0u, lit = 0u, fit_lm = 0u, fit_raw = 0u, fit_n = 0u;
	float3 raw = float3(0.0f);

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
			const float3 p = rm_rtdir_position(v, px, d);
			const float3 o = p + n * mtl_rtao_bias(distance(p, v.eye.xyz), v.bias_k);

			world = 1u;

			for (uint i = 0; i < v.nlights; i++)
			{
				const rm_rtdir_light l = lights[i];
				const float3 dv = l.pos.xyz - p;
				const float dist = length(dv);
				float3 dir;
				float s;

				/* the linear falloff ends at the intensity: the cheap test first */
				if (!(dist < l.pos.w) || !(dist > 0.0f))
				{
					continue;
				}

				dir = dv / dist;
				s = mtl_rtdir_scale(l.pos.w, dist, dot(n, dir), -dot(dir, l.spot.xyz), l.spot.w);

				if (!(s > 0.0f))
				{
					continue;
				}

				inrange++;

				if (v.show != 3u)
				{
					const float3 to = l.pos.xyz - o;
					const float len = length(to);

					rays++;

					/* the ray stops a unit short of the light: an entity's origin is in open space */
					if ((len > 1.0f) && (rm_rtdir_seen(ray(o, to / len, 0.0f, len - 1.0f), ias, inst, tmat, ntris) <
						0.5f))
					{
						continue;
					}

					lit++;
				}

				raw += s * l.color.rgb * float3(styles[(uint)l.color.w & 255u]);
			}

			raw *= MTL_RTDIR_UNIT;

			/* the fit's samples: the lightmap enters the frame's sums, never this pixel's light */
			if (((px.x % MTL_RTDIR_FIT_STEP) == 0u) && ((px.y % MTL_RTDIR_FIT_STEP) == 0u))
			{
				const float4 s = lm.read(px);

				fit_lm = mtl_rtdir_q(s.r, s.g, s.b);
				fit_raw = mtl_rtdir_q(raw.r, raw.g, raw.b);
				fit_n = 1u;
			}
		}
	}

	if (inside)
	{
		if (world)
		{
			const mtl_rgba_t c = mtl_rtdir_light(raw.r, raw.g, raw.b, v.exposure);
			const float seen = (v.show == 3u) ? 1.0f : (rays ? float(lit) / float(rays) : 0.0f);

			light_out.write(float4(c.r, c.g, c.b, c.a), px);
			vis_out.write(float4(seen, inrange ? 1.0f : 0.0f, 1.0f, 1.0f), px);
		}
		else
		{
			/* not a traced world pixel: its light as it was, bit for bit */
			light_out.write(base.read(px), px);
			vis_out.write(float4(0.0f), px);
		}
	}

	/* the frame's counts, one atomic per SIMD group and count */
	{
		const uint c_w = simd_sum(world), c_i = simd_sum(inrange), c_r = simd_sum(rays), c_l = simd_sum(lit);
		const uint c_fl = simd_sum(fit_lm), c_fr = simd_sum(fit_raw), c_fn = simd_sum(fit_n);

		if (simd_is_first())
		{
			atomic_fetch_add_explicit(&counts[RM_RTDIR_C_WORLD], c_w, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[RM_RTDIR_C_INRANGE], c_i, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[RM_RTDIR_C_RAYS], c_r, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[RM_RTDIR_C_LIT], c_l, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[RM_RTDIR_C_FIT_LM], c_fl, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[RM_RTDIR_C_FIT_RAW], c_fr, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[RM_RTDIR_C_FIT_N], c_fn, memory_order_relaxed);
		}
	}
}

/*
 * r_rt_direct_show over the resolved view: 1 and 3, the direct term
 * alone (the light the resolve reads, on white albedo); 2, the shadow
 * visibility (grey, the share of the lights in range whose ray reached
 * them; dark blue where no light is in range); black on every pixel that
 * is not a traced world pixel.
 */
fragment float4
rm_fragment_rtdir_show(rm_fs_varying in [[stage_in]],
		constant rm_rtdir_view &v [[buffer(0)]],
		texture2d<float> vis [[texture(0)]],
		texture2d<float> light [[texture(1)]])
{
	const uint2 px = uint2(in.position.xy);
	const float4 s = vis.read(px);

	if (s.b < 0.5f)
	{
		return float4(0.0f, 0.0f, 0.0f, 1.0f);
	}

	if (v.show == 2u)
	{
		return (s.g < 0.5f) ? float4(0.0f, 0.0f, 0.25f, 1.0f) : float4(s.r, s.r, s.r, 1.0f);
	}

	return float4(light.read(px).rgb, 1.0f);
}
