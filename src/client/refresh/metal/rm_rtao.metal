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
 * The shaders of ref_metal's ray-traced ambient occlusion (RT5, TASK57;
 * docs/plans/metal-raytracing-design.md, sections 2 and 3), linked into
 * ref_metal.metallib beside rm_gbuf.metal and rm_rtshadow.metal:
 *
 * - rm_rtao_trace, the compute pass between the G-buffer raster (and
 *   RT3's kernel) and the resolve: for each lightmapped world pixel
 *   (material 1), its position from the depth and its normal from the
 *   G-buffer; two cosine-weighted rays (mtl_rtao_dir) from the surface
 *   offset along its normal (mtl_rtao_bias), r_rt_ao_range long, their
 *   numbers from the blue-noise tile at the pixel moved on by the frame
 *   (mtl_rtao_noise), against the frame's instance structure (TASK53)
 *   with MTL_RTAO_MASK: the world, the brush entities and the alias
 *   models but the view weapon and the viewer's own model. A sky, water
 *   or translucent triangle passes the ray (mtl_rtao_occludes, the map's
 *   per-triangle material ids). Out: the AO, R8Unorm; the light the
 *   resolve reads, RGBA16Unorm (which holds every 8-bit value exactly):
 *   a world pixel's lightmap sample times the factor (the 8-bit AO
 *   clamped to [r_rt_ao_min, 1]), every other pixel's light as the
 *   raster wrote it; and the frame's counts;
 * - rm_fragment_rtao_show, r_rt_ao_show's two views over the resolve.
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

/* rm_rtao.h's rm_rtao_view_t */
struct rm_rtao_view
{
	float4x4 inv;
	float4 vp;
	float4 eye;
	float bias_k;
	float range;
	float floor_;
	uint frame;
	uint show;
	uint pad[3];
};

/* rm_gbuf.metal's rm_fs_varying (rm_vertex_fullscreen's output) */
struct rm_fs_varying
{
	float4 position [[position]];
	float2 uv;
};

/* the world position of a pixel's centre at its depth, as rm_rts_unproject (TASK55) */
static float3
rm_rtao_position(constant rm_rtao_view &v, uint2 px, float depth)
{
	const float cx = (float(px.x) + 0.5f - v.vp.x) / v.vp.z * 2.0f - 1.0f;
	const float cy = 1.0f - (float(px.y) + 0.5f - v.vp.y) / v.vp.w * 2.0f;
	const float4 h = v.inv * float4(cx, cy, depth * 2.0f - 1.0f, 1.0f);

	return h.xyz / h.w;
}

/*
 * The distance along r to the first occluder, or -1 when it meets none
 * within its length. inst holds two words an instance (rmb_rt_frame): its
 * first triangle of the map's buffers (~0 for an alias model, which
 * occludes) and the lights it carries; tmat the map's material id of each
 * of its ntris triangles. A triangle that does not occlude is passed, the
 * nearest first; past MTL_RTAO_LAYERS of them the ray counts as open.
 */
static float
rm_rtao_hit(ray r, instance_acceleration_structure ias, device const uint *inst, device const uchar *tmat, uint ntris)
{
	intersector<triangle_data, instancing> isect;

	isect.assume_geometry_type(geometry_type::triangle);

	for (int layer = 0; layer < MTL_RTAO_LAYERS; layer++)
	{
		intersection_result<triangle_data, instancing> hit = isect.intersect(r, ias, MTL_RTAO_MASK);
		uint b, m = 0xffu;

		if (hit.type == intersection_type::none)
		{
			return -1.0f;
		}

		b = inst[2 * hit.instance_id];

		/* a map triangle outside the table (none uploaded) counts as the world's */
		if (b != 0xffffffffu)
		{
			m = (b + hit.primitive_id < ntris) ? (uint)tmat[b + hit.primitive_id] : (uint)MTL_GB_MAT_WORLD;
		}

		if (mtl_rtao_occludes(m))
		{
			return hit.distance;
		}

		r.min_distance = hit.distance + 0.01f;

		if (!(r.min_distance < r.max_distance))
		{
			return -1.0f;
		}
	}

	return -1.0f;
}

/* the frame's counts (rmb_rtao.m's RMB_RTAO_COUNTS) */
#define RM_RTAO_C_TRACED 0
#define RM_RTAO_C_OCCLUDED 1
#define RM_RTAO_C_FLOORED 2
#define RM_RTAO_C_UP 3
#define RM_RTAO_C_UP_OCC 4
#define RM_RTAO_C_RAYS 5
#define RM_RTAO_C_HITS 6

kernel void
rm_rtao_trace(depth2d<float, access::read> depth [[texture(0)]],
		texture2d<float, access::read> normal [[texture(1)]],
		texture2d<uint, access::read> mat [[texture(2)]],
		texture2d<float, access::read> light [[texture(3)]],
		texture2d<float, access::write> ao_out [[texture(4)]],
		texture2d<float, access::write> light_out [[texture(5)]],
		constant rm_rtao_view &v [[buffer(0)]],
		device const ushort *tile [[buffer(1)]],
		instance_acceleration_structure ias [[buffer(2)]],
		device const uint *inst [[buffer(3)]],
		device const uchar *tmat [[buffer(4)]],
		constant uint &ntris [[buffer(5)]],
		device atomic_uint *counts [[buffer(6)]],
		uint2 px [[thread_position_in_grid]])
{
	const bool inside = (px.x < ao_out.get_width()) && (px.y < ao_out.get_height());
	uint traced = 0, rays = 0, hits = 0, up = 0, m = MTL_GB_MAT_NONE;
	float ao = 1.0f;

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
			const float3 p = rm_rtao_position(v, px, d);
			const float3 o = p + n * mtl_rtao_bias(distance(p, v.eye.xyz), v.bias_k);
			float occ[MTL_RTAO_RAYS];

			traced = 1;
			up = (n.z > 0.99f) ? 1u : 0u;

			for (uint k = 0; k < MTL_RTAO_RAYS; k++)
			{
				const float u = mtl_rtao_noise(tile[mtl_rtao_tile_index(px.x, px.y, 2 * k)], v.frame, 2 * k);
				const float w = mtl_rtao_noise(tile[mtl_rtao_tile_index(px.x, px.y, 2 * k + 1)], v.frame, 2 * k + 1);
				const mtl_rtao_v3_t dd = mtl_rtao_dir(mtl_rtao_v3(n.x, n.y, n.z), k, u, w);
				const float t = rm_rtao_hit(ray(o, float3(dd.x, dd.y, dd.z), 0.0f, v.range), ias, inst, tmat, ntris);

				occ[k] = mtl_rtao_occlusion(t >= 0.0f, t, v.range);
				rays++;
				hits += (t >= 0.0f) ? 1u : 0u;
			}

			ao = mtl_rtao_ao(occ[0], occ[1]);
		}
	}

	/* the 8-bit AO, which the factor is taken from as the stored value */
	ao = rint(ao * 255.0f) / 255.0f;

	if (inside)
	{
		const float4 l = light.read(px);
		mtl_rgba_t c;

		c.r = l.r;
		c.g = l.g;
		c.b = l.b;
		c.a = l.a;
		c = mtl_rtao_light(c, m, mtl_rtao_factor(ao, v.floor_));
		ao_out.write(float4(ao, 0.0f, 0.0f, 1.0f), px);
		light_out.write(float4(c.r, c.g, c.b, c.a), px);
	}

	/* the frame's counts, one atomic per SIMD group and count */
	{
		const uint occluded = (traced && (ao < 1.0f)) ? 1u : 0u;
		const uint floored = (traced && (ao < v.floor_)) ? 1u : 0u;
		const uint c_tr = simd_sum(traced), c_oc = simd_sum(occluded), c_fl = simd_sum(floored);
		const uint c_up = simd_sum(up), c_uo = simd_sum(up & occluded), c_ry = simd_sum(rays), c_hi = simd_sum(hits);

		if (simd_is_first())
		{
			atomic_fetch_add_explicit(&counts[RM_RTAO_C_TRACED], c_tr, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[RM_RTAO_C_OCCLUDED], c_oc, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[RM_RTAO_C_FLOORED], c_fl, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[RM_RTAO_C_UP], c_up, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[RM_RTAO_C_UP_OCC], c_uo, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[RM_RTAO_C_RAYS], c_ry, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[RM_RTAO_C_HITS], c_hi, memory_order_relaxed);
		}
	}
}

/*
 * r_rt_ao_show over the resolved view, on every pixel the raster wrote:
 * show 1, the factor the lightmap term took, grey, on the world's pixels
 * and black on the others; show 2, the classes: black not a world pixel,
 * white unoccluded (AO 1), blue occluded above the floor, red at the
 * floor (the factor is r_rt_ao_min).
 */
fragment float4
rm_fragment_rtao_show(rm_fs_varying in [[stage_in]],
		constant rm_rtao_view &v [[buffer(0)]],
		texture2d<float> ao [[texture(0)]],
		texture2d<uint> mat [[texture(1)]])
{
	const uint2 px = uint2(in.position.xy);
	const uint m = mat.read(px).r;
	float a, f;

	if (m == MTL_GB_MAT_NONE)
	{
		discard_fragment();
	}

	if (m != MTL_GB_MAT_WORLD)
	{
		return float4(0.0f, 0.0f, 0.0f, 1.0f);
	}

	a = ao.read(px).r;
	f = mtl_rtao_factor(a, v.floor_);

	if (v.show == 1)
	{
		return float4(f, f, f, 1.0f);
	}

	if (a >= 1.0f)
	{
		return float4(1.0f, 1.0f, 1.0f, 1.0f);
	}

	return (a < v.floor_) ? float4(1.0f, 0.0f, 0.0f, 1.0f) : float4(0.0f, 0.0f, 1.0f, 1.0f);
}
