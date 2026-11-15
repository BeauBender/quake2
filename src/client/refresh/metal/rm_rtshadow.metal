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
 * The shaders of ref_metal's ray-traced dynamic-light shadows (RT3,
 * TASK55; docs/plans/metal-raytracing-design.md, sections 2 and 3),
 * linked into ref_metal.metallib beside rm_gbuf.metal:
 *
 * - rm_rts_trace, the compute pass between the G-buffer raster and the
 *   resolve: for each lightmapped world pixel (material 1), its position
 *   from the depth and its normal from the G-buffer; its surface from a
 *   ray from the eye to that point (the triangle it meets, through the
 *   map's triangle-to-surface table), whose texture axes and shifts give
 *   gl1's dynamic light at the pixel as the lightmap's sample holds it
 *   (mtl_rts_add_lm: the four luxels' adds, interpolated); the four
 *   lights that add the most (common/mtl_rts_math.h) and one ray query
 *   per light against the frame's instance structure (TASK53), from the
 *   surface offset along its normal (mtl_rts_bias) to the light; a
 *   triangle of water or a translucent surface passes the ray on with
 *   its transmission, any other stops it. Out: an RG32Uint per pixel,
 *   the four visibilities at 8 bits each and the four light indices with
 *   the in-range and shadowed flags; an RGBA16Float, the term; and the
 *   frame's counts;
 * - rm_fragment_resolve_rts, the resolve with the term: the stock
 *   product of rm_fragment_resolve (mtl_gb_resolve) with the light of a
 *   world pixel raised by the term (mtl_rts_light, R_BuildLightMap's
 *   store), and r_rt_shadows_show's two views.
 *
 * =======================================================================
 */

#include <metal_stdlib>
#include <metal_raytracing>

using namespace metal;
using namespace metal::raytracing;

#include "common/mtl_shader_math.h"
#include "common/mtl_gbuf_math.h"
#include "common/mtl_rts_math.h"

/* the passes a ray takes through water and translucent surfaces before it counts as stopped */
#define RM_RTS_LAYERS 8

/* rm_rtshadow.h's rm_rts_light_t and rm_rts_view_t */
struct rm_rts_light
{
	float4 origin;      /* w: the intensity */
	float4 color;
};

struct rm_rts_view
{
	float4x4 inv;
	float4 vp;
	float4 eye;
	float bias_k;
	uint nlights;
	uint trans;
	uint show;
};

/* rm_gbuf.metal's rm_uniforms_resolve and rm_fs_varying (rm_vertex_fullscreen's output) */
struct rm_uniforms_resolve
{
	int env;
	float scale;
	uint pad[2];
};

struct rm_fs_varying
{
	float4 position [[position]];
	float2 uv;
};

static mtl_rts_v3_t
rm_rts_v(float3 v)
{
	return mtl_rts_v3(v.x, v.y, v.z);
}

static mtl_rgba_t
rm_rts_rgba(float4 v)
{
	mtl_rgba_t c;

	c.r = v.r;
	c.g = v.g;
	c.b = v.b;
	c.a = v.a;
	return c;
}

/* the world position of a pixel's centre at its depth, as rm_rts_unproject */
static float3
rm_rts_position(constant rm_rts_view &v, uint2 px, float depth)
{
	const float cx = (float(px.x) + 0.5f - v.vp.x) / v.vp.z * 2.0f - 1.0f;
	const float cy = 1.0f - (float(px.y) + 0.5f - v.vp.y) / v.vp.w * 2.0f;
	const float4 h = v.inv * float4(cx, cy, depth * 2.0f - 1.0f, 1.0f);

	return h.xyz / h.w;
}

/*
 * The light a shadow ray keeps from its start to the light: 1 when it
 * reaches it, 0 when an opaque triangle is in the way; through water and
 * translucent surfaces (trans set: the map holds such triangles) the
 * product of their transmissions, the nearest first.
 */
static float
rm_rts_visibility(ray r, instance_acceleration_structure ias, device const uint *base,
		device const uchar *transmit, uint ntris, uint trans)
{
	intersector<triangle_data, instancing> isect;
	float vis = 1.0f;

	isect.assume_geometry_type(geometry_type::triangle);

	if (!trans)
	{
		isect.accept_any_intersection(true);
		return (isect.intersect(r, ias, 0xff).type == intersection_type::none) ? 1.0f : 0.0f;
	}

	for (int layer = 0; layer < RM_RTS_LAYERS; layer++)
	{
		intersection_result<triangle_data, instancing> hit = isect.intersect(r, ias, 0xff);
		uint b, t = 0;

		if (hit.type == intersection_type::none)
		{
			return vis;
		}

		/* an alias instance (base ~0) is opaque; a map triangle has its transmission */
		b = base[hit.instance_id];

		if ((b != 0xffffffffu) && (b + hit.primitive_id < ntris))
		{
			t = transmit[b + hit.primitive_id];
		}

		if (t == 0)
		{
			return 0.0f;
		}

		vis *= float(t) / 255.0f;
		r.min_distance = hit.distance + 0.01f;

		if (!(r.min_distance < r.max_distance))
		{
			return vis;
		}
	}

	return 0.0f;
}

static mtl_rts_v4_t
rm_rts_v4(float4 v)
{
	mtl_rts_v4_t r;

	r.x = v.x;
	r.y = v.y;
	r.z = v.z;
	r.w = v.w;
	return r;
}

/* the map's tables (rmb_rts_set_map): triangles in tri_surf, surfaces in vecs */
struct rm_rts_mapn
{
	uint ntris;
	uint nsurf;
};

kernel void
rm_rts_trace(depth2d<float, access::read> depth [[texture(0)]],
		texture2d<float, access::read> normal [[texture(1)]],
		texture2d<uint, access::read> mat [[texture(2)]],
		texture2d<uint, access::write> out [[texture(3)]],
		texture2d<float, access::write> term [[texture(4)]],
		constant rm_rts_view &v [[buffer(0)]],
		constant rm_rts_light *lights [[buffer(1)]],
		instance_acceleration_structure ias [[buffer(2)]],
		device const uint *base [[buffer(3)]],
		device const uchar *transmit [[buffer(4)]],
		constant uint &ntris [[buffer(5)]],
		device atomic_uint *counts [[buffer(6)]],
		device const uint *tri_surf [[buffer(7)]],
		device const float4 *vecs [[buffer(8)]],
		constant rm_rts_mapn &mapn [[buffer(9)]],
		uint2 px [[thread_position_in_grid]])
{
	const bool inside = (px.x < out.get_width()) && (px.y < out.get_height());
	mtl_rts_top_t top = mtl_rts_top_init();
	uint flags = 0, rays = 0, primary = 0;
	float vis[MTL_RTS_CAP] = { 1.0f, 1.0f, 1.0f, 1.0f };
	float3 t = float3(0.0f);

	if (inside && (v.nlights > 0) && (mat.read(px).r == MTL_GB_MAT_WORLD))
	{
		const float d = depth.read(px);
		const float4 nn = normal.read(px);

		if ((d < 1.0f) && (nn.a > 0.5f))
		{
			const float3 n = normalize(nn.xyz * 2.0f - 1.0f);
			const float3 p = rm_rts_position(v, px, d);
			const uint nl = min(v.nlights, (uint)MTL_RTS_MAX_LIGHTS);
			const float3 o = p + n * mtl_rts_bias(distance(p, v.eye.xyz), v.bias_k);
			float adds[MTL_RTS_MAX_LIGHTS];
			bool slab = false, have = false;
			mtl_rts_v4_t sv, tv;

			/* a light whose plane distance leaves it light to add (gl1's surface test) */
			for (uint i = 0; i < nl; i++)
			{
				const float pd = dot(lights[i].origin.xyz - p, n);

				slab = slab || ((pd >= 0.0f) && (lights[i].origin.w - pd >= MTL_RTS_CUTOFF));
			}

			/* the pixel's surface, for its texture axes: the triangle a ray from the eye meets at the depth's point */
			if (slab && (mapn.nsurf > 0))
			{
				const float3 dv = p - v.eye.xyz;
				const float dl = length(dv);

				if (dl > 1.0f)
				{
					intersector<triangle_data, instancing> isect;
					intersection_result<triangle_data, instancing> hit;

					isect.assume_geometry_type(geometry_type::triangle);
					hit = isect.intersect(ray(v.eye.xyz, dv / dl, 0.0f, dl + 4.0f), ias, 0xff);
					primary = 1;

					if ((hit.type == intersection_type::triangle) && (fabs(hit.distance - dl) <= max(2.0f, dl * 0.002f)))
					{
						const uint b = base[hit.instance_id];

						if ((b != 0xffffffffu) && (b + hit.primitive_id < mapn.ntris))
						{
							const uint sidx = tri_surf[b + hit.primitive_id];

							if (sidx < mapn.nsurf)
							{
								sv = rm_rts_v4(vecs[2 * sidx]);
								tv = rm_rts_v4(vecs[2 * sidx + 1]);
								have = true;
							}
						}
					}
				}
			}

			for (uint i = 0; i < nl; i++)
			{
				const mtl_rts_v3_t lo = rm_rts_v(lights[i].origin.xyz);
				mtl_rts_reach_t r;

				adds[i] = 0.0f;

				if (!slab)
				{
					continue;
				}

				if (have)
				{
					r = mtl_rts_reach_st(rm_rts_v(p), rm_rts_v(n), lo, lights[i].origin.w, mtl_rts_v3(sv.x, sv.y, sv.z),
							mtl_rts_v3(tv.x, tv.y, tv.z));
					adds[i] = mtl_rts_add_lm(rm_rts_v(p), rm_rts_v(n), lo, lights[i].origin.w, sv, tv);
				}
				else
				{
					r = mtl_rts_reach(rm_rts_v(p), rm_rts_v(n), lo, lights[i].origin.w);
					adds[i] = mtl_rts_add(r);
				}

				if (mtl_rts_in_range(r))
				{
					flags |= MTL_RTS_IN_RANGE;
				}

				top = mtl_rts_top_add(top, i, adds[i]);
			}

			for (int k = 0; k < MTL_RTS_CAP; k++)
			{
				float3 dir;
				float len;

				if (top.idx[k] == MTL_RTS_NO_LIGHT)
				{
					continue;
				}

				dir = lights[top.idx[k]].origin.xyz - o;
				len = length(dir);

				/* a light at the start itself: nothing between them */
				if (!(len > 0.5f))
				{
					continue;
				}

				vis[k] = rm_rts_visibility(ray(o, dir / len, 0.0f, len - 0.5f), ias, base, transmit, ntris, v.trans);
				rays++;

				if (vis[k] < 1.0f)
				{
					flags |= MTL_RTS_SHADOWED;
				}
			}

			/* the term: each light's add, times its visibility where traced (1 past the four) */
			{
				const uint pidx = mtl_rts_pack_idx(top, flags), pvis = mtl_rts_pack_vis(vis[0], vis[1], vis[2], vis[3]);

				for (uint i = 0; i < nl; i++)
				{
					if (adds[i] > 0.0f)
					{
						t += mtl_rts_light_vis(pidx, pvis, i) * adds[i] * lights[i].color.rgb;
					}
				}
			}
		}
	}

	if (inside)
	{
		out.write(uint4(mtl_rts_pack_vis(vis[0], vis[1], vis[2], vis[3]), mtl_rts_pack_idx(top, flags), 0, 0), px);
		term.write(float4(t, 0.0f), px);
	}

	/* the frame's counts, one atomic per SIMD group (rays: the shadow rays and the primary ones) */
	{
		const uint c_in = simd_sum((flags & MTL_RTS_IN_RANGE) ? 1u : 0u);
		const uint c_tr = simd_sum(rays ? 1u : 0u);
		const uint c_sh = simd_sum((flags & MTL_RTS_SHADOWED) ? 1u : 0u);
		const uint c_ry = simd_sum(rays + primary);

		if (simd_is_first())
		{
			atomic_fetch_add_explicit(&counts[0], c_in, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[1], c_tr, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[2], c_sh, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[3], c_ry, memory_order_relaxed);
		}
	}
}

/*
 * The resolve with the shadows on: rm_fragment_resolve's stock product,
 * where a world pixel's light is the static lightmap's sample (the
 * dynamic lights are out of the lightmaps) raised by the kernel's term,
 * each light's visibility x (intensity - dist) x colour (rm_rts_term),
 * through R_BuildLightMap's store (mtl_rts_light). show 1 is the mask of
 * the double-count check over every pixel of the target (white: no light
 * in range), show 2 the classes of rm_rts_show.
 */
fragment float4
rm_fragment_resolve_rts(rm_fs_varying in [[stage_in]],
		constant rm_uniforms_resolve &r [[buffer(0)]],
		constant rm_rts_view &v [[buffer(1)]],
		texture2d<float> albedo [[texture(0)]],
		texture2d<float> light [[texture(1)]],
		texture2d<uint> mat [[texture(2)]],
		texture2d<uint> sh [[texture(5)]],
		texture2d<float> term [[texture(6)]])
{
	const uint2 px = uint2(in.position.xy);
	const uint m = mat.read(px).r;
	const uint2 s = sh.read(px).rg;
	mtl_rgba_t l;

	if (v.show == 1)
	{
		return (s.g & MTL_RTS_IN_RANGE) ? float4(0, 0, 0, 1) : float4(1, 1, 1, 1);
	}

	if (v.show == 2)
	{
		if (!(s.g & MTL_RTS_IN_RANGE))
		{
			return float4(0, 0, 0, 1);
		}

		if (mtl_rts_idx(s.g, 0) == MTL_RTS_NO_LIGHT)
		{
			return float4(0, 0, 1, 1);
		}

		return (s.g & MTL_RTS_SHADOWED) ? float4(1, 0, 0, 1) : float4(0, 1, 0, 1);
	}

	if (m == MTL_GB_MAT_NONE)
	{
		discard_fragment();
	}

	l = rm_rts_rgba(light.read(px));

	if (m == MTL_GB_MAT_WORLD)
	{
		const float3 tt = term.read(px).rgb;

		l = mtl_rts_light(l, tt.r, tt.g, tt.b);
	}

	{
		const mtl_rgba_t c = mtl_gb_resolve(rm_rts_rgba(albedo.read(px)), l, m, r.env, r.scale);

		return float4(c.r, c.g, c.b, c.a);
	}
}
