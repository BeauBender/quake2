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
 * The shaders of ref_metal's ray-traced water reflections (RT6, TASK58;
 * docs/plans/metal-raytracing-design.md, sections 2 and 3), linked into
 * ref_metal.metallib beside rm_gbuf.metal, rm_rtshadow.metal and
 * rm_rtao.metal:
 *
 * - rm_rtw_trace, the compute pass after the lighting resolve, the last
 *   of the ray passes: each pixel's water, the opaque water the raster
 *   drew (material 3: its point from the depth, its normal from the
 *   G-buffer) or, on another pixel of the view, translucent water a ray
 *   from the eye meets in front of the raster's surface (mtl_rtw_eye_step:
 *   it passes translucent faces and models, stops at the world and the
 *   sky brush; its normal from the triangle's vertices). From it one
 *   reflection ray (mtl_rtw_reflect) about the flat normal tilted by the
 *   stock warp's phase (mtl_rtw_normal, gl1's table at gl1's index), from
 *   the point moved off the water (mtl_rtao_bias), against the frame's
 *   instance structure with MTL_RTW_MASK (all but the view weapon and the
 *   viewer's own model); every face it meets stops it. Its colour is the
 *   lit pixel (the resolve's) where the screen shows the hit, the sky
 *   pixel in its direction for the sky brush or nothing met, else none
 *   (mtl_rtw_choose). Out: the reflection and the pixel's class
 *   (RGBA8Unorm), the translucent water's distance (R32Float) and the
 *   frame's counts;
 * - rm_fragment_rtw_blend, the reflection over the opaque water in a pass
 *   after the kernel (r_rt_water_reflect, by the blend: the target's
 *   alpha kept), and rm_fragment_rtw_show, r_rt_water_show's two views;
 * - rm_fragment_rtw_trans, the forward pass's translucent faces:
 *   rm_fragment_world_color's colour, with the reflection blended in
 *   (mtl_rtw_blend, the alpha kept) where the face is the water the
 *   kernel found at that pixel.
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
#include "common/mtl_rtw_math.h"

/* rm_rtwater.h's rm_rtw_view_t */
struct rm_rtw_view
{
	float4x4 inv;
	float4x4 mvp;
	float4 vp;
	float4 eye;
	float bias_k;
	float time;
	float amp;
	float reflect;
	uint show;
	uint pad[3];
};

/* the map's buffers (rmb_rt_bind_geometry): vertices, triangles of the index buffer, the vertex stride in float4s */
struct rm_rtw_geom
{
	uint nverts;
	uint ntris;
	uint stride4;
	uint pad;
};

/* rm_gbuf.metal's rm_fs_varying (rm_vertex_fullscreen's output) */
struct rm_fs_varying
{
	float4 position [[position]];
	float2 uv;
};

/* rm_shaders.metal's rm_varying3d (rm_vertex_world's output), member for member */
struct rm_varying3d
{
	float4 position [[position]];
	float2 uv;
	float2 lmuv;
	float4 color;
	float4 pt;
	float4 lmrect [[flat]];
};

static mtl_rgba_t
rm_rtw_rgba(float4 v)
{
	mtl_rgba_t c;

	c.r = v.r;
	c.g = v.g;
	c.b = v.b;
	c.a = v.a;
	return c;
}

static mtl_rtw_v3_t
rm_rtw_v(float3 v)
{
	return mtl_rtw_v3(v.x, v.y, v.z);
}

static float3
rm_rtw_f3(mtl_rtw_v3_t v)
{
	return float3(v.x, v.y, v.z);
}

/* the world point at pixel coordinates pc (a centre is n + 0.5) and depth, as rm_rts_unproject (TASK55) */
static float3
rm_rtw_unproject(constant rm_rtw_view &v, float2 pc, float depth)
{
	const float cx = (pc.x - v.vp.x) / v.vp.z * 2.0f - 1.0f;
	const float cy = 1.0f - (pc.y - v.vp.y) / v.vp.w * 2.0f;
	const float4 h = v.inv * float4(cx, cy, depth * 2.0f - 1.0f, 1.0f);

	return h.xyz / h.w;
}

/* the class byte the kernel wrote in the reflection's alpha */
static uint
rm_rtw_cls(float a)
{
	return uint(rint(a * 255.0f));
}

/* the map's material of an instance's primitive: an alias model's MTL_RTW_MAT_ALIAS, a face outside the table the world's */
static uint
rm_rtw_mat(uint b, uint prim, device const uchar *tmat, uint ntris)
{
	if (b == 0xffffffffu)
	{
		return MTL_RTW_MAT_ALIAS;
	}

	return (b + prim < ntris) ? (uint)tmat[b + prim] : (uint)MTL_GB_MAT_WORLD;
}

/* the unit normal of the map's triangle tri in world space; 0 when it is outside the buffers or has no area */
static bool
rm_rtw_tri_normal(device const float4 *verts, device const uint *idx, constant rm_rtw_geom &g, uint tri,
		float4x3 o2w, thread float3 &n)
{
	uint i0, i1, i2;
	float3 c;
	float l;

	if ((tri >= g.ntris) || (g.stride4 == 0))
	{
		return false;
	}

	i0 = idx[3 * tri];
	i1 = idx[3 * tri + 1];
	i2 = idx[3 * tri + 2];

	if ((i0 >= g.nverts) || (i1 >= g.nverts) || (i2 >= g.nverts))
	{
		return false;
	}

	c = cross(verts[i1 * g.stride4].xyz - verts[i0 * g.stride4].xyz, verts[i2 * g.stride4].xyz - verts[i0 * g.stride4].xyz);
	c = o2w * float4(c, 0.0f);
	l = length(c);

	if (!(l > 0.0f))
	{
		return false;
	}

	n = c / l;
	return true;
}

/* the frame's counts (rmb_rtwater.m's RMB_RTW_COUNTS) */
#define RM_RTW_C_OPAQUE 0
#define RM_RTW_C_TRANS 1
#define RM_RTW_C_EYE 2
#define RM_RTW_C_LIT 3
#define RM_RTW_C_SKY 4
#define RM_RTW_C_MISS 5

kernel void
rm_rtw_trace(depth2d<float, access::read> depth [[texture(0)]],
		texture2d<float, access::read> normal [[texture(1)]],
		texture2d<uint, access::read> mat [[texture(2)]],
		texture2d<float, access::read> lit [[texture(3)]],
		texture2d<float, access::write> refl [[texture(4)]],
		texture2d<float, access::write> wdist [[texture(5)]],
		constant rm_rtw_view &v [[buffer(0)]],
		constant float *turb [[buffer(1)]],
		instance_acceleration_structure ias [[buffer(2)]],
		device const uint *inst [[buffer(3)]],
		device const uchar *tmat [[buffer(4)]],
		constant uint &ntris [[buffer(5)]],
		device atomic_uint *counts [[buffer(6)]],
		device const float4 *verts [[buffer(7)]],
		device const uint *idx [[buffer(8)]],
		constant rm_rtw_geom &g [[buffer(9)]],
		uint2 px [[thread_position_in_grid]])
{
	const uint w = refl.get_width(), h = refl.get_height();
	const bool inside = (px.x < w) && (px.y < h);
	const float2 pc = float2(px) + 0.5f;
	const bool in_view = inside && (pc.x >= v.vp.x) && (pc.x < v.vp.x + v.vp.z) && (pc.y >= v.vp.y) &&
		(pc.y < v.vp.y + v.vp.w);
	uint m = MTL_GB_MAT_NONE, water = 0, trans = 0, eyes = 0, from = MTL_RTW_MISS;
	float3 p = float3(0.0f), n = float3(0.0f), rc = float3(0.0f);
	float wd = 0.0f;

	if (in_view)
	{
		const float d = depth.read(px);

		m = mat.read(px).r;

		if (mtl_rtw_is_water(m))
		{
			const float4 nn = normal.read(px);

			if ((d < 1.0f) && (nn.a > 0.5f))
			{
				n = normalize(nn.xyz * 2.0f - 1.0f);
				p = rm_rtw_unproject(v, pc, d);
				water = 1;
			}
		}
		else
		{
			/* translucent water in front of the raster's surface: a ray from the eye through the pixel */
			const bool surf = (d < 1.0f) && (m != MTL_GB_MAT_NONE) && (m != MTL_GB_MAT_SKY);
			const float3 dv = rm_rtw_unproject(v, pc, surf ? d : 0.5f) - v.eye.xyz;
			const float dl = length(dv);
			const float lim = mtl_rtw_eye_max(surf ? dl : 0.0f);

			if ((dl > 0.0f) && (lim > 0.0f))
			{
				const float3 dir = dv / dl;
				intersector<triangle_data, instancing, world_space_data> isect;
				ray r(v.eye.xyz, dir, 0.0f, lim);

				isect.assume_geometry_type(geometry_type::triangle);
				eyes = 1;

				for (int layer = 0; layer < MTL_RTW_LAYERS; layer++)
				{
					intersection_result<triangle_data, instancing, world_space_data> hit = isect.intersect(r, ias, MTL_RTW_MASK);
					uint b;
					int step;

					if (hit.type == intersection_type::none)
					{
						break;
					}

					b = inst[2 * hit.instance_id];
					step = mtl_rtw_eye_step(rm_rtw_mat(b, hit.primitive_id, tmat, ntris));

					if (step == MTL_RTW_EYE_STOP)
					{
						break;
					}

					if (step == MTL_RTW_EYE_WATER)
					{
						float3 tn;

						if (rm_rtw_tri_normal(verts, idx, g, b + hit.primitive_id, hit.object_to_world_transform, tn))
						{
							n = tn;
							p = v.eye.xyz + dir * hit.distance;
							wd = hit.distance;
							water = 1;
							trans = 1;
						}

						break;
					}

					r.min_distance = hit.distance + 0.01f;

					if (!(r.min_distance < r.max_distance))
					{
						break;
					}
				}
			}
		}
	}

	if (water)
	{
		const float3 dv = p - v.eye.xyz;
		const float dist = length(dv);

		if (dist > 0.0f)
		{
			const float3 dir = dv / dist;
			intersector<triangle_data, instancing> isect;
			intersection_result<triangle_data, instancing> hit;
			mtl_rtw_v3_t nf, rd;
			mtl_rtw_axes_t ax;
			float3 o, r3;
			float4 c;
			uint qm = MTL_GB_MAT_NONE;
			int kind = 0, on = 0;
			float off = -1.0f, hd = 0.0f;
			uint2 q = uint2(0);

			/* the flat normal facing the eye, tilted by the stock warp's phase at the point (s and t swapped, as gl1's) */
			if (dot(n, dir) > 0.0f)
			{
				n = -n;
			}

			nf = rm_rtw_v(n);
			ax = mtl_rtw_axes(nf);
			rd = mtl_rtw_reflect(rm_rtw_v(dir), mtl_rtw_normal(nf, ax,
					turb[mtl_rtw_warp_index(dot(p, rm_rtw_f3(ax.t)), v.time)],
					turb[mtl_rtw_warp_index(dot(p, rm_rtw_f3(ax.s)), v.time)], v.amp), nf);
			r3 = rm_rtw_f3(rd);
			o = p + n * mtl_rtao_bias(dist, v.bias_k);

			/* one bounce: every face stops the ray, the water and translucent faces too (design section 1) */
			isect.assume_geometry_type(geometry_type::triangle);
			hit = isect.intersect(ray(o, r3, 0.0f, MTL_RTW_FAR), ias, MTL_RTW_MASK);

			if (hit.type != intersection_type::none)
			{
				kind = (rm_rtw_mat(inst[2 * hit.instance_id], hit.primitive_id, tmat, ntris) == MTL_GB_MAT_SKY) ? 1 : 2;
			}

			/* where the screen shows it: the hit, or for the sky the direction from the eye (the box is around it) */
			c = (kind == 2) ? (v.mvp * float4(o + r3 * hit.distance, 1.0f)) : (v.mvp * float4(r3, 0.0f));

			if (c.w > 0.0f)
			{
				const float2 nd = c.xy / c.w;
				const float2 qp = float2(v.vp.x + (nd.x * 0.5f + 0.5f) * v.vp.z, v.vp.y + (0.5f - nd.y * 0.5f) * v.vp.w);

				if ((qp.x >= v.vp.x) && (qp.x < v.vp.x + v.vp.z) && (qp.y >= v.vp.y) && (qp.y < v.vp.y + v.vp.w) &&
					(qp.x >= 0.0f) && (qp.y >= 0.0f) && (qp.x < float(w)) && (qp.y < float(h)))
				{
					q = uint2(qp);
					on = 1;
				}
			}

			if (on)
			{
				qm = mat.read(q).r;

				if (kind == 2)
				{
					const float qd = depth.read(q);

					hd = distance(o + r3 * hit.distance, v.eye.xyz);

					if (qd < 1.0f)
					{
						off = fabs(distance(rm_rtw_unproject(v, float2(q) + 0.5f, qd), v.eye.xyz) - hd);
					}
				}
			}

			from = mtl_rtw_choose(kind, on, qm, off, hd);

			if (from != MTL_RTW_MISS)
			{
				rc = lit.read(q).rgb;
			}
		}
	}

	if (inside)
	{
		refl.write(float4(rc, float(mtl_rtw_class(water != 0, trans != 0, from)) / 255.0f), px);
		wdist.write(float4(trans ? wd : 0.0f, 0.0f, 0.0f, 0.0f), px);
	}

	/* the frame's counts, one atomic per SIMD group and count */
	{
		const uint c_op = simd_sum((water && !trans) ? 1u : 0u), c_tr = simd_sum(trans);
		const uint c_ey = simd_sum(eyes), c_li = simd_sum((water && (from == MTL_RTW_LIT)) ? 1u : 0u);
		const uint c_sk = simd_sum((water && (from == MTL_RTW_SKY)) ? 1u : 0u);
		const uint c_mi = simd_sum((water && (from == MTL_RTW_MISS)) ? 1u : 0u);

		if (simd_is_first())
		{
			atomic_fetch_add_explicit(&counts[RM_RTW_C_OPAQUE], c_op, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[RM_RTW_C_TRANS], c_tr, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[RM_RTW_C_EYE], c_ey, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[RM_RTW_C_LIT], c_li, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[RM_RTW_C_SKY], c_sk, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[RM_RTW_C_MISS], c_mi, memory_order_relaxed);
		}
	}
}

/*
 * The reflection over the opaque water, drawn over the resolved target
 * with the blend (source alpha the weight, the target's alpha not
 * written): every other pixel is discarded, so it keeps its colour.
 */
fragment float4
rm_fragment_rtw_blend(rm_fs_varying in [[stage_in]],
		constant rm_rtw_view &v [[buffer(0)]],
		texture2d<float> refl [[texture(0)]],
		texture2d<uint> mat [[texture(1)]])
{
	const uint2 px = uint2(in.position.xy);
	const float4 r = refl.read(px);
	const uint cls = rm_rtw_cls(r.a);
	const float k = mtl_rtw_weight(v.reflect);

	if (!(cls & MTL_RTW_CLASS_WATER) || (cls & MTL_RTW_CLASS_TRANS) || ((cls & MTL_RTW_CLASS_FROM) == MTL_RTW_MISS) ||
		!mtl_rtw_is_water(mat.read(px).r) || (k == 0.0f))
	{
		discard_fragment();
	}

	return float4(r.rgb, k);
}

/*
 * r_rt_water_show over every pixel of the target: show 1, the mask of the
 * pixels that must keep their colour (white where no water was found,
 * black on the water); show 2, the classes: black not water, green a
 * reflection from a lit pixel, blue from the sky, red none.
 */
fragment float4
rm_fragment_rtw_show(rm_fs_varying in [[stage_in]],
		constant rm_rtw_view &v [[buffer(0)]],
		texture2d<float> refl [[texture(0)]])
{
	const uint cls = rm_rtw_cls(refl.read(uint2(in.position.xy)).a);

	if (v.show == 1)
	{
		return (cls & MTL_RTW_CLASS_WATER) ? float4(0.0f, 0.0f, 0.0f, 1.0f) : float4(1.0f, 1.0f, 1.0f, 1.0f);
	}

	if (!(cls & MTL_RTW_CLASS_WATER))
	{
		return float4(0.0f, 0.0f, 0.0f, 1.0f);
	}

	switch (cls & MTL_RTW_CLASS_FROM)
	{
		case MTL_RTW_LIT:
			return float4(0.0f, 1.0f, 0.0f, 1.0f);
		case MTL_RTW_SKY:
			return float4(0.0f, 0.0f, 1.0f, 1.0f);
		default:
			return float4(1.0f, 0.0f, 0.0f, 1.0f);
	}
}

/*
 * The forward pass's translucent faces while the view traced: the stock
 * rm_fragment_world_color (GL_MODULATE of the colour and the texel), and
 * where the face is the translucent water the kernel found at the pixel
 * (mtl_rtw_is_trans_water: within TASK55's tolerance of its distance)
 * its RGB mixed toward the reflection, the alpha kept, so the blend over
 * what lies behind is the stock one. Every other fragment is the stock
 * colour bit for bit.
 */
fragment float4
rm_fragment_rtw_trans(rm_varying3d in [[stage_in]],
		texture2d<float> tex [[texture(0)]],
		sampler smp [[sampler(0)]],
		constant rm_rtw_view &v [[buffer(6)]],
		texture2d<float> refl [[texture(6)]],
		texture2d<float> wdist [[texture(7)]])
{
	mtl_rgba_t c = mtl_world_color(rm_rtw_rgba(saturate(in.color)), rm_rtw_rgba(tex.sample(smp, in.uv)));
	const uint2 px = uint2(in.position.xy);

	if ((px.x < refl.get_width()) && (px.y < refl.get_height()))
	{
		const float4 r = refl.read(px);
		const uint cls = rm_rtw_cls(r.a);

		if ((cls & MTL_RTW_CLASS_TRANS) && ((cls & MTL_RTW_CLASS_FROM) != MTL_RTW_MISS) &&
			mtl_rtw_is_trans_water(cls, wdist.read(px).r,
				distance(rm_rtw_unproject(v, in.position.xy, in.position.z), v.eye.xyz)))
		{
			c = mtl_rtw_blend(c, r.r, r.g, r.b, v.reflect, cls & MTL_RTW_CLASS_FROM);
		}
	}

	return float4(c.r, c.g, c.b, c.a);
}
