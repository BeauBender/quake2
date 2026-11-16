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
 *   G-buffer; two (r_rt_ao_rays) cosine-weighted rays (mtl_rtao_dir) from the surface
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
 * - with r_rt_ao_scale below 1 (TASK157) in its place rm_rtao_trace_lo,
 *   the same rays from one pixel of each block at the reduced size, then
 *   rm_rtao_upsample at the render size: each world pixel's AO from the
 *   four traced pixels about it on its own surface (mtl_rtao_up_ao), and
 *   the AO and light as rm_rtao_trace writes them;
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
	uint div;
	uint rays;
	uint pad[1];
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

/*
 * A world pixel's AO from its rays: v.rays (1 or 2) rays from o about the
 * unit normal n, the numbers of pixel px of the tile. With 2 rays the
 * arithmetic is TASK57's; with 1 its azimuth's number is doubled, so the
 * one ray covers the whole circle (mtl_rtao_ao1).
 */
static float
rm_rtao_rays(constant rm_rtao_view &v, uint2 px, float3 o, float3 n, device const ushort *tile,
		instance_acceleration_structure ias, device const uint *inst, device const uchar *tmat, uint ntris,
		thread uint &rays, thread uint &hits)
{
	const uint nr = (v.rays >= 2u) ? 2u : 1u;
	float occ[MTL_RTAO_RAYS];

	occ[1] = 0.0f;

	for (uint k = 0; k < nr; k++)
	{
		const float u = mtl_rtao_noise(tile[mtl_rtao_tile_index(px.x, px.y, 2 * k)], v.frame, 2 * k);
		const float w = mtl_rtao_noise(tile[mtl_rtao_tile_index(px.x, px.y, 2 * k + 1)], v.frame, 2 * k + 1);
		const mtl_rtao_v3_t dd = (nr == 2u) ? mtl_rtao_dir(mtl_rtao_v3(n.x, n.y, n.z), k, u, w) :
			mtl_rtao_dir(mtl_rtao_v3(n.x, n.y, n.z), 0, 2.0f * u, w);
		const float t = rm_rtao_hit(ray(o, float3(dd.x, dd.y, dd.z), 0.0f, v.range), ias, inst, tmat, ntris);

		occ[k] = mtl_rtao_occlusion(t >= 0.0f, t, v.range);
		rays++;
		hits += (t >= 0.0f) ? 1u : 0u;
	}

	return (nr == 2u) ? mtl_rtao_ao(occ[0], occ[1]) : mtl_rtao_ao1(occ[0]);
}

/* the frame's counts (rmb_rtao.m's RMB_RTAO_COUNTS) */
#define RM_RTAO_C_TRACED 0
#define RM_RTAO_C_OCCLUDED 1
#define RM_RTAO_C_FLOORED 2
#define RM_RTAO_C_UP 3
#define RM_RTAO_C_UP_OCC 4
#define RM_RTAO_C_RAYS 5
#define RM_RTAO_C_HITS 6

/* adds a thread's counts, one atomic per SIMD group and count */
static void
rm_rtao_count(device atomic_uint *counts, uint traced, uint occluded, uint floored, uint up, uint rays, uint hits)
{
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

/* the AO and light a pixel's 8-bit AO gives (the two kernels that write the render size) */
static void
rm_rtao_store(constant rm_rtao_view &v, uint2 px, uint m, float ao, texture2d<float, access::read> light,
		texture2d<float, access::write> ao_out, texture2d<float, access::write> light_out)
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

			traced = 1;
			up = (n.z > 0.99f) ? 1u : 0u;
			ao = rm_rtao_rays(v, px, o, n, tile, ias, inst, tmat, ntris, rays, hits);
		}
	}

	/* the 8-bit AO, which the factor is taken from as the stored value */
	ao = rint(ao * 255.0f) / 255.0f;

	if (inside)
	{
		rm_rtao_store(v, px, m, ao, light, ao_out, light_out);
	}

	rm_rtao_count(counts, traced, (traced && (ao < 1.0f)) ? 1u : 0u, (traced && (ao < v.floor_)) ? 1u : 0u, up,
			rays, hits);
}

/*
 * r_rt_ao_scale below 1 (TASK157): the rays of traced pixel px, from the
 * render-size pixel mtl_rtao_lo_src gives, into the reduced-size AO
 * (8 bits; 1 where that pixel is not a world pixel that traces). Counts
 * the rays and hits; rm_rtao_upsample counts the pixels.
 */
kernel void
rm_rtao_trace_lo(depth2d<float, access::read> depth [[texture(0)]],
		texture2d<float, access::read> normal [[texture(1)]],
		texture2d<uint, access::read> mat [[texture(2)]],
		texture2d<float, access::write> ao_lo [[texture(6)]],
		constant rm_rtao_view &v [[buffer(0)]],
		device const ushort *tile [[buffer(1)]],
		instance_acceleration_structure ias [[buffer(2)]],
		device const uint *inst [[buffer(3)]],
		device const uchar *tmat [[buffer(4)]],
		constant uint &ntris [[buffer(5)]],
		device atomic_uint *counts [[buffer(6)]],
		uint2 px [[thread_position_in_grid]])
{
	const bool inside = (px.x < ao_lo.get_width()) && (px.y < ao_lo.get_height());
	uint rays = 0, hits = 0;
	float ao = 1.0f;

	if (inside)
	{
		const uint2 sp = uint2(mtl_rtao_lo_src(px.x, v.div, depth.get_width()),
				mtl_rtao_lo_src(px.y, v.div, depth.get_height()));

		if (mat.read(sp).r == MTL_GB_MAT_WORLD)
		{
			const float d = depth.read(sp);
			const float4 nn = normal.read(sp);

			if ((d < 1.0f) && (nn.a > 0.5f))
			{
				const float3 n = normalize(nn.xyz * 2.0f - 1.0f);
				const float3 p = rm_rtao_position(v, sp, d);
				const float3 o = p + n * mtl_rtao_bias(distance(p, v.eye.xyz), v.bias_k);

				ao = rm_rtao_rays(v, px, o, n, tile, ias, inst, tmat, ntris, rays, hits);
			}
		}

		ao_lo.write(float4(rint(ao * 255.0f) / 255.0f, 0.0f, 0.0f, 1.0f), px);
	}

	rm_rtao_count(counts, 0, 0, 0, 0, rays, hits);
}

/*
 * r_rt_ao_scale below 1 (TASK157), at the render size: each lightmapped
 * world pixel's AO from the four traced pixels about it (mtl_rtao_up_pair
 * on each axis), kept where they lie on its surface (mtl_rtao_up_same),
 * their bilinear mean (mtl_rtao_up_ao), stored at 8 bits; then the AO and
 * light as rm_rtao_trace writes them, and the pixel counts.
 */
kernel void
rm_rtao_upsample(depth2d<float, access::read> depth [[texture(0)]],
		texture2d<float, access::read> normal [[texture(1)]],
		texture2d<uint, access::read> mat [[texture(2)]],
		texture2d<float, access::read> light [[texture(3)]],
		texture2d<float, access::write> ao_out [[texture(4)]],
		texture2d<float, access::write> light_out [[texture(5)]],
		texture2d<float, access::read> ao_lo [[texture(6)]],
		constant rm_rtao_view &v [[buffer(0)]],
		device atomic_uint *counts [[buffer(6)]],
		uint2 px [[thread_position_in_grid]])
{
	const uint w = ao_out.get_width(), h = ao_out.get_height();
	const bool inside = (px.x < w) && (px.y < h);
	uint traced = 0, up = 0, m = MTL_GB_MAT_NONE;
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
			const float tol = mtl_rtao_up_tol(distance(p, v.eye.xyz));
			const mtl_rtao_pair_t qx = mtl_rtao_up_pair(px.x, v.div, ao_lo.get_width());
			const mtl_rtao_pair_t qy = mtl_rtao_up_pair(px.y, v.div, ao_lo.get_height());
			mtl_rtao_up_t u;

			for (int i = 0; i < 4; i++)
			{
				const uint2 lo = uint2((i & 1) ? qx.lo1 : qx.lo0, (i & 2) ? qy.lo1 : qy.lo0);
				const uint2 sp = uint2(mtl_rtao_lo_src(lo.x, v.div, w), mtl_rtao_lo_src(lo.y, v.div, h));
				const float fx = (i & 1) ? qx.f : 1.0f - qx.f, fy = (i & 2) ? qy.f : 1.0f - qy.f;
				const float ds = depth.read(sp);
				const float4 ns = normal.read(sp);

				u.w[i] = fx * fy;
				u.ao[i] = ao_lo.read(lo).r;
				u.world[i] = (mat.read(sp).r == MTL_GB_MAT_WORLD) && (ds < 1.0f) && (ns.a > 0.5f);
				u.same[i] = 0;

				if (u.world[i])
				{
					const float3 nsn = normalize(ns.xyz * 2.0f - 1.0f);
					const float3 ps = rm_rtao_position(v, sp, ds);

					u.same[i] = mtl_rtao_up_same(mtl_rtao_v3(p.x, p.y, p.z), mtl_rtao_v3(n.x, n.y, n.z),
							mtl_rtao_v3(ps.x, ps.y, ps.z), mtl_rtao_v3(nsn.x, nsn.y, nsn.z), tol);
				}
			}

			traced = 1;
			up = (n.z > 0.99f) ? 1u : 0u;
			ao = mtl_rtao_up_ao(u);
		}
	}

	ao = rint(ao * 255.0f) / 255.0f;

	if (inside)
	{
		rm_rtao_store(v, px, m, ao, light, ao_out, light_out);
	}

	rm_rtao_count(counts, traced, (traced && (ao < 1.0f)) ? 1u : 0u, (traced && (ao < v.floor_)) ? 1u : 0u, up, 0, 0);
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
