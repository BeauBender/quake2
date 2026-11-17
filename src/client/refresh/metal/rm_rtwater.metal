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
 *   viewer's own model); every face it meets stops it but an alias model
 *   the picture does not show, which it passes (mtl_rtw_ss_next, FIX81:
 *   no hole in the model's outline, within the bound of MTL_RTW_LAYERS
 *   faces, see the pass-over block). Its colour is the
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
#include "common/mtl_rts_math.h"
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
	int env;
	float scale;
	float lod_k;
	float flow;
	uint nlights;
	uint fullbright;
	uint sky;
	float4 warp;
	float4 trans;
	float4 sky_rows[3];
};

/* TASK158: the frame's texture table (rmb_rtwater.m's argument buffer): the world's images, then the sky box's six */
struct rm_rtw_texs
{
	array<texture2d<float>, MTL_RTW_TEX_COUNT> t [[id(0)]];
};

/* rm_rtshadow.h's rm_rts_light_t: the origin and intensity, the colour */
struct rm_rtw_light
{
	float4 origin;
	float4 color;
};

constexpr sampler rm_rtw_smp(filter::linear, mip_filter::linear, address::repeat);
constexpr sampler rm_rtw_csmp(filter::linear, address::clamp_to_edge);

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
#define RM_RTW_C_WORLD 6    /* TASK158: a face shaded in world space */
#define RM_RTW_C_SKYBOX 7   /* the sky box's texel in world space */
#define RM_RTW_C_LISTED 9   /* the compaction's list: its length, then the trace's threadgroups (x, y, z) */
#define RM_RTW_C_ARGS 10

/* 1 when the pixel is inside the view's viewport and the target */
static bool
rm_rtw_in_view(constant rm_rtw_view &v, uint2 px, uint w, uint h)
{
	const float2 pc = float2(px) + 0.5f;

	return (px.x < w) && (px.y < h) && (pc.x >= v.vp.x) && (pc.x < v.vp.x + v.vp.z) && (pc.y >= v.vp.y) &&
		(pc.y < v.vp.y + v.vp.w);
}

/*
 * TASK158, TASK162: the water's marks, the G-buffer raster's last draw
 * (rmb_rtw_mark). Every water triangle of the map (the world's and the
 * inline models', where gl1 draws them: R_DrawAlphaSurfaces uses the
 * world's matrix) with the view's matrix, depth-tested against the
 * raster's depth and not writing it: a fragment marks its pixel in the
 * normal attachment's alpha (mtl_rtw_mark_blend), where water lies in
 * front of the raster's surface. The compaction lists the marked pixels
 * and their neighbours; nothing else is traced.
 */
struct rm_rtw_cand_v
{
	float4 position [[position]];
};

vertex rm_rtw_cand_v
rm_vertex_rtw_cand(uint vid [[vertex_id]],
		device const uint *wtris [[buffer(0)]],
		device const uint *idx [[buffer(1)]],
		device const float4 *verts [[buffer(2)]],
		constant uint4 &g [[buffer(3)]],
		constant rm_rtw_view &v [[buffer(4)]])
{
	rm_rtw_cand_v out;
	const uint tri = vid / 3, corner = vid % 3;
	uint t, i;
	float4 p;

	/* outside the buffers: a point at w 0, which no triangle covers */
	out.position = float4(0.0f);

	if (tri >= g.w)
	{
		return out;
	}

	t = wtris[tri];

	if (t >= g.y)
	{
		return out;
	}

	i = idx[3 * t + corner];

	if (i >= g.x)
	{
		return out;
	}

	p = v.mvp * float4(verts[i * g.z].xyz, 1.0f);
	p.z = mtl_gl_clip_z(p.z, p.w);
	out.position = p;
	return out;
}

/* the mark's alpha into the normal attachment (colour 1); rmb_rtw_mark's pipeline writes that alpha alone, blended */
struct rm_rtw_mark_out
{
	float4 normal [[color(1)]];
};

fragment rm_rtw_mark_out
rm_fragment_rtw_mark(rm_rtw_cand_v in [[stage_in]])
{
	rm_rtw_mark_out out;

	(void)in;
	out.normal = float4(0.0f, 0.0f, 0.0f, MTL_RTW_MARK_A);
	return out;
}

/*
 * TASK158: the compaction, over every pixel of the target: a pixel of the
 * view is listed (mtl_rtw_listed) where the raster drew opaque water or
 * the G-buffer's marks (TASK162, mtl_rtw_marked) hold it or a pixel next
 * to it. The list's length is counted once per SIMD group; the trace runs
 * on the list alone and writes every listed pixel of the reflection
 * target, so the compaction writes the others 0 (no water).
 */
kernel void
rm_rtw_compact(texture2d<uint, access::read> mat [[texture(0)]],
		texture2d<float, access::write> refl [[texture(1)]],
		texture2d<float, access::read> normal [[texture(2)]],
		constant rm_rtw_view &v [[buffer(0)]],
		device uint *list [[buffer(1)]],
		device atomic_uint *counts [[buffer(2)]],
		constant uint &cap [[buffer(3)]],
		uint2 px [[thread_position_in_grid]])
{
	const uint w = refl.get_width(), h = refl.get_height();
	bool listed = false;
	uint base = 0, k, total;

	if (rm_rtw_in_view(v, px, w, h) && (px.x < mat.get_width()) && (px.y < mat.get_height()) &&
		(w <= normal.get_width()) && (h <= normal.get_height()))
	{
		uint marks = 0;

		for (int dy = -1; dy <= 1; dy++)
		{
			for (int dx = -1; dx <= 1; dx++)
			{
				const int qx = int(px.x) + dx, qy = int(px.y) + dy;

				if ((qx >= 0) && (qy >= 0) && (qx < int(w)) && (qy < int(h)))
				{
					marks += mtl_rtw_marked(normal.read(uint2(qx, qy)).a) ? 1u : 0u;
				}
			}
		}

		listed = mtl_rtw_listed(mat.read(px).r, marks);
	}

	k = simd_prefix_exclusive_sum(listed ? 1u : 0u);
	total = simd_sum(listed ? 1u : 0u);

	if (simd_is_first() && (total > 0))
	{
		base = atomic_fetch_add_explicit(&counts[RM_RTW_C_LISTED], total, memory_order_relaxed);
	}

	base = simd_broadcast_first(base);

	if (listed && (base + k < cap))
	{
		list[base + k] = mtl_rtw_list_pack(px.x, px.y);
	}
	else if ((px.x < w) && (px.y < h))
	{
		/* not traced: no water, as the cleared target of TASK158's raster read */
		refl.write(float4(0.0f), px);
	}
}

/* TASK158: the list's length held to its size, and the trace's threadgroups for an indirect dispatch */
kernel void
rm_rtw_args(device uint *counts [[buffer(0)]],
		constant uint &cap [[buffer(1)]],
		uint tid [[thread_position_in_grid]])
{
	if (tid == 0)
	{
		const uint n = min(counts[RM_RTW_C_LISTED], cap);

		counts[RM_RTW_C_LISTED] = n;
		counts[RM_RTW_C_ARGS] = mtl_rtw_groups(n);
		counts[RM_RTW_C_ARGS + 1] = 1;
		counts[RM_RTW_C_ARGS + 2] = 1;
	}
}

/*
 * TASK158: the attributes of a map triangle at a hit: its texture and
 * lightmap coordinates by the hit's barycentrics. 0 when the triangle or
 * a vertex is outside the buffers.
 */
static bool
rm_rtw_tri_uv(device const float4 *verts, device const uint *idx, constant rm_rtw_geom &g, uint tri, float2 bc,
		thread float4 &uvlm)
{
	uint i0, i1, i2;
	float4 a0, a1, a2;

	if ((tri >= g.ntris) || (g.stride4 < 2))
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

	a0 = verts[i0 * g.stride4 + 1];
	a1 = verts[i1 * g.stride4 + 1];
	a2 = verts[i2 * g.stride4 + 1];
	uvlm = float4(mtl_rtw_bary(a0.x, a1.x, a2.x, bc.x, bc.y), mtl_rtw_bary(a0.y, a1.y, a2.y, bc.x, bc.y),
			mtl_rtw_bary(a0.z, a1.z, a2.z, bc.x, bc.y), mtl_rtw_bary(a0.w, a1.w, a2.w, bc.x, bc.y));
	return true;
}

/*
 * TASK158: a face shaded in world space, as the stock draw shades it
 * (mtl_rtw_face_color): its texel at the hit's texture coordinates (the
 * flowing scroll added) at the mip level of the path's footprint
 * (mtl_rtw_lod), its lightmap's sample at the lightmap coordinates in its
 * slice (white without one), raised by each dynamic light the lightmaps
 * left out (RT3 traced: gl1's add at the hit on the face's side the ray
 * came from, unshadowed, mtl_rts_light).
 */
static float3
rm_rtw_shade_face(constant rm_rtw_view &v, constant rm_rtw_texs &texs, texture2d_array<float> lm,
		constant rm_rtw_light *lights, uint sh, float4 uvlm, float path, float3 hp, float3 hn)
{
	const uint flags = mtl_rtw_shade_flags(sh), slice = mtl_rtw_shade_slice(sh);
	float2 uv = uvlm.xy;
	float4 a, l = float4(1.0f);
	mtl_rgba_t c;

	if (flags & MTL_RTW_SF_FLOW)
	{
		uv.x += v.flow;
	}

	a = texs.t[mtl_rtw_shade_tex(sh)].sample(rm_rtw_smp, uv, level(mtl_rtw_lod(path, v.lod_k)));

	if ((slice != MTL_RTW_SHADE_NOLM) && (slice < lm.get_array_size()))
	{
		l = lm.sample(rm_rtw_csmp, uvlm.zw, slice, level(0.0f));

		if ((v.nlights > 0) && !(flags & (MTL_RTW_SF_WARP | MTL_RTW_SF_TRANS)))
		{
			float3 t = float3(0.0f);
			mtl_rgba_t lr;

			for (uint i = 0; i < min(v.nlights, (uint)MTL_RTS_MAX_LIGHTS); i++)
			{
				const float add = mtl_rts_add(mtl_rts_reach(mtl_rts_v3(hp.x, hp.y, hp.z), mtl_rts_v3(hn.x, hn.y, hn.z),
						mtl_rts_v3(lights[i].origin.x, lights[i].origin.y, lights[i].origin.z), lights[i].origin.w));

				t += add * lights[i].color.rgb;
			}

			lr = mtl_rts_light(rm_rtw_rgba(l), t.r, t.g, t.b);
			l = float4(lr.r, lr.g, lr.b, lr.a);
		}
	}

	c = mtl_rtw_face_color(rm_rtw_rgba(a), rm_rtw_rgba(l), flags, v.env, v.scale, rm_rtw_rgba(v.warp),
			rm_rtw_rgba(v.trans), v.fullbright != 0);
	return float3(c.r, c.g, c.b);
}

/* TASK158: the sky box's texel in world direction d (the box turned back by the sky's rotation) */
static float3
rm_rtw_shade_sky(constant rm_rtw_view &v, constant rm_rtw_texs &texs, float3 d)
{
	const mtl_rtw_sky_t k = mtl_rtw_sky_st(mtl_rtw_sky_dir(rm_rtw_v(d), rm_rtw_v(v.sky_rows[0].xyz),
			rm_rtw_v(v.sky_rows[1].xyz), rm_rtw_v(v.sky_rows[2].xyz)));

	if (!k.ok)
	{
		return float3(0.0f);
	}

	return texs.t[MTL_RTW_TEX_SKY + uint(k.face)].sample(rm_rtw_csmp, float2(k.s, k.t), level(0.0f)).rgb;
}

/*
 * FIX81: rm_rtw_trace's choice for a reflection ray's hit past a hidden
 * model, made as the kernel makes it for the first hit: met 0 for none, b
 * the hit's instance's first map triangle, prim and dist its primitive and
 * distance along r3 from o. Out: kind (0 nothing met, 1 the sky brush, 2 a
 * face), q the pixel the screen shows it at; the choice, mtl_rtw_choose's.
 */
static uint
rm_rtw_ss_at(constant rm_rtw_view &v, depth2d<float, access::read> depth, texture2d<uint, access::read> mat,
		device const uchar *tmat, uint ntris, float3 o, float3 r3, bool met, uint b, uint prim, float dist,
		uint w, uint h, thread int &kind, thread uint2 &q)
{
	uint qm = MTL_GB_MAT_NONE;
	int on = 0;
	float off = -1.0f, hd = 0.0f;
	float4 c;

	kind = met ? ((rm_rtw_mat(b, prim, tmat, ntris) == MTL_GB_MAT_SKY) ? 1 : 2) : 0;
	c = (kind == 2) ? (v.mvp * float4(o + r3 * dist, 1.0f)) : (v.mvp * float4(r3, 0.0f));

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

			hd = distance(o + r3 * dist, v.eye.xyz);

			if (qd < 1.0f)
			{
				off = fabs(distance(rm_rtw_unproject(v, float2(q) + 0.5f, qd), v.eye.xyz) - hd);
			}
		}
	}

	return mtl_rtw_choose(kind, on, qm, off, hd);
}

/*
 * r_rt_water_hits 0: TASK58's kernel (its frames and its cost are
 * TASK58's but where a ray met a hidden model): every pixel of the
 * target, a hit's colour the picture's where the screen shows it, else
 * none (mtl_rtw_choose); an alias model the picture does not show passed
 * over as at 1 (mtl_rtw_ss_next, FIX81). r_rt_water_hits 1 is
 * rm_rtw_trace_ws below.
 */
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

			/* the first hit: every face stops it, the water and translucent faces too (design section 1); past an alias model the picture does not show it goes on, see FIX81 below */
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

			/*
			 * FIX81: but an alias model the picture does not show (off the
			 * screen or hidden) does not stop it: the ray goes on past it
			 * (mtl_rtw_ss_next, r_rt_water_hits 1's rule), after
			 * MTL_RTW_LAYERS model faces with the map's instances alone
			 * (MTL_RTW_MASK_MAP), so it ends where it would end without the
			 * models and a hidden model does not leave its outline as a hole
			 * in the reflection. The bound: past MTL_RTW_LAYERS (four) hidden
			 * model faces the ray is traced against the map alone, so a model
			 * the picture shows behind them is passed too and the reflection
			 * there shows what lies behind it (at 0, no reflection); one
			 * hidden player with its weapon is already four faces (the rays
			 * cull none). It keeps the cost at five casts per water pixel at
			 * most. It works per face: a face of a model the view does not
			 * show at that pixel is passed, so a shown model's unseen far
			 * side is passed too. The first hit's choice above is TASK58's
			 * code as it was, and this block runs only past a hidden model,
			 * so every other ray is cast once and gives TASK58's colour bit
			 * for bit (the compiler's arithmetic included).
			 */
			{
				uint b = (hit.type != intersection_type::none) ? inst[2 * hit.instance_id] : 0u;
				uint qmask = MTL_RTW_MASK;
				int map = 0;
				int step = mtl_rtw_ss_next(kind, b == 0xffffffffu, from, 0, map);

				for (int layer = 1; step != MTL_RTW_NEXT_STOP; layer++)
				{
					if (step == MTL_RTW_NEXT_MAP)
					{
						qmask = MTL_RTW_MASK_MAP;
						map = 1;
					}

					hit = isect.intersect(ray(o, r3, hit.distance + 0.01f, MTL_RTW_FAR), ias, qmask);
					b = (hit.type != intersection_type::none) ? inst[2 * hit.instance_id] : 0u;
					from = rm_rtw_ss_at(v, depth, mat, tmat, ntris, o, r3, hit.type != intersection_type::none, b,
						hit.primitive_id, hit.distance, w, h, kind, q);
					step = mtl_rtw_ss_next(kind, b == 0xffffffffu, from, layer, map);
				}
			}

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
 * r_rt_water_hits 1 (TASK158): TASK58's kernel on the compaction's list,
 * one pixel a thread, with world-space shading of a hit the picture does
 * not show (mtl_rtw_choose_ws): an alias model it does not show passed
 * over (mtl_rtw_ws_next), a hidden brush model shaded.
 */
kernel void
rm_rtw_trace_ws(depth2d<float, access::read> depth [[texture(0)]],
		texture2d<float, access::read> normal [[texture(1)]],
		texture2d<uint, access::read> mat [[texture(2)]],
		texture2d<float, access::read> lit [[texture(3)]],
		texture2d<float, access::write> refl [[texture(4)]],
		texture2d<float, access::write> wdist [[texture(5)]],
		texture2d_array<float> lm [[texture(6)]],
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
		constant rm_rtw_texs &texs [[buffer(12)]],
		constant rm_rtw_light *lights [[buffer(13)]],
		device const uint *shade [[buffer(14)]],
		constant uint &nshade [[buffer(15)]],
		device const uint *list [[buffer(16)]],
		uint tid [[thread_position_in_grid]])
{
	const uint w = refl.get_width(), h = refl.get_height();
	const bool live = tid < atomic_load_explicit(&counts[RM_RTW_C_LISTED], memory_order_relaxed);
	const uint2 px = live ? uint2(list[tid] & 0xffffu, list[tid] >> 16) : uint2(0xffffu);
	const bool inside = live && (px.x < w) && (px.y < h);
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
			intersector<triangle_data, instancing, world_space_data> isect;
			intersection_result<triangle_data, instancing, world_space_data> hit;
			mtl_rtw_v3_t nf, rd;
			uint sh = 0, tri = 0;
			int face_ok = 0;
			float4 uvlm = float4(0.0f);
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

			/* the first hit: every face stops it, the water and translucent faces too (design section 1); past an alias model the picture does not show it goes on, see TASK158 below */
			isect.assume_geometry_type(geometry_type::triangle);
			hit = isect.intersect(ray(o, r3, 0.0f, MTL_RTW_FAR), ias, MTL_RTW_MASK);

			/*
			 * TASK158: what is passed over is exactly this: an alias model the
			 * picture does not show (off the screen or hidden). The ray goes on
			 * past it (mtl_rtw_ws_next), after MTL_RTW_LAYERS model faces with
			 * the map's instances alone (MTL_RTW_MASK_MAP), so it ends where it
			 * would end without the models, however many it crosses (but see
			 * the bound in mtl_rtw_ws_next: past MTL_RTW_LAYERS hidden faces a
			 * model the picture shows is passed too). A model is
			 * shaded only as the picture shows it, so a hidden one neither
			 * reflects nor leaves a hole in the world-space reflection behind
			 * it. A hidden brush model (a door, a plat) is not passed over: it
			 * is a map face and is shaded in world space where it stands.
			 */
			uint qmask = MTL_RTW_MASK;
			int map = 0;

			for (int layer = 0; ; layer++)
			{
				int step;
				uint b = 0xffffffffu;

				kind = on = face_ok = 0;
				qm = MTL_GB_MAT_NONE;
				off = -1.0f;
				hd = 0.0f;

				if (hit.type != intersection_type::none)
				{
					b = inst[2 * hit.instance_id];
					kind = (rm_rtw_mat(b, hit.primitive_id, tmat, ntris) == MTL_GB_MAT_SKY) ? 1 : 2;

					/* a map triangle with a shading record and a texture can be shaded in world space */
					if ((kind == 2) && (b != 0xffffffffu) && (b + hit.primitive_id < nshade))
					{
						tri = b + hit.primitive_id;
						sh = shade[tri];
						face_ok = mtl_rtw_shade_ok(sh) &&
							rm_rtw_tri_uv(verts, idx, g, tri, hit.triangle_barycentric_coord, uvlm);
					}
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

				/* the picture where it shows the hit (cheaper and exact), else world space */
				from = mtl_rtw_choose_ws(kind, on, qm, off, hd, face_ok, v.sky != 0);
				step = mtl_rtw_ws_next(kind, b == 0xffffffffu, from, layer, map);

				if (step == MTL_RTW_NEXT_STOP)
				{
					break;
				}

				if (step == MTL_RTW_NEXT_MAP)
				{
					qmask = MTL_RTW_MASK_MAP;
					map = 1;
				}

				hit = isect.intersect(ray(o, r3, hit.distance + 0.01f, MTL_RTW_FAR), ias, qmask);
			}

			if ((from == MTL_RTW_LIT) || (from == MTL_RTW_SKY))
			{
				rc = lit.read(q).rgb;
			}
			else if (from == (MTL_RTW_LIT | MTL_RTW_WS))
			{
				const float3 hp = o + r3 * hit.distance;
				float3 hn = float3(0.0f);

				/* the face's normal on the side the ray came from, for the dynamic lights */
				if ((v.nlights > 0) && rm_rtw_tri_normal(verts, idx, g, tri, hit.object_to_world_transform, hn) &&
					(dot(hn, r3) > 0.0f))
				{
					hn = -hn;
				}

				rc = rm_rtw_shade_face(v, texs, lm, lights, sh, uvlm, dist + hit.distance, hp, hn);
			}
			else if (from == (MTL_RTW_SKY | MTL_RTW_WS))
			{
				rc = rm_rtw_shade_sky(v, texs, r3);
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
		const uint c_wo = simd_sum((water && (from == (MTL_RTW_LIT | MTL_RTW_WS))) ? 1u : 0u);
		const uint c_sb = simd_sum((water && (from == (MTL_RTW_SKY | MTL_RTW_WS))) ? 1u : 0u);

		if (simd_is_first())
		{
			atomic_fetch_add_explicit(&counts[RM_RTW_C_WORLD], c_wo, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[RM_RTW_C_SKYBOX], c_sb, memory_order_relaxed);
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
 * written): every other pixel is discarded, so it keeps its colour, as
 * is one outside the reflection or the material attachment (they are the
 * target's size today; the read is not left to it).
 */
fragment float4
rm_fragment_rtw_blend(rm_fs_varying in [[stage_in]],
		constant rm_rtw_view &v [[buffer(0)]],
		texture2d<float> refl [[texture(0)]],
		texture2d<uint> mat [[texture(1)]])
{
	const uint2 px = uint2(in.position.xy);
	float4 r;
	uint cls;
	const float k = mtl_rtw_weight(v.reflect);

	if ((px.x >= refl.get_width()) || (px.y >= refl.get_height()) || (px.x >= mat.get_width()) ||
		(px.y >= mat.get_height()))
	{
		discard_fragment();
		return float4(0.0f);
	}

	r = refl.read(px);
	cls = rm_rtw_cls(r.a);

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
 * reflection from a lit pixel, blue from the sky, red none. A pixel
 * outside the reflection is shown as one without water.
 */
fragment float4
rm_fragment_rtw_show(rm_fs_varying in [[stage_in]],
		constant rm_rtw_view &v [[buffer(0)]],
		texture2d<float> refl [[texture(0)]])
{
	const uint2 px = uint2(in.position.xy);
	const uint cls = ((px.x < refl.get_width()) && (px.y < refl.get_height())) ? rm_rtw_cls(refl.read(px).a) : 0u;

	if (v.show == 1)
	{
		return (cls & MTL_RTW_CLASS_WATER) ? float4(0.0f, 0.0f, 0.0f, 1.0f) : float4(1.0f, 1.0f, 1.0f, 1.0f);
	}

	if (!(cls & MTL_RTW_CLASS_WATER))
	{
		return float4(0.0f, 0.0f, 0.0f, 1.0f);
	}

	/* TASK158: yellow a face shaded in world space, cyan the sky box's texel */
	switch (cls & (MTL_RTW_CLASS_FROM | MTL_RTW_CLASS_WS))
	{
		case MTL_RTW_LIT:
			return float4(0.0f, 1.0f, 0.0f, 1.0f);
		case MTL_RTW_SKY:
			return float4(0.0f, 0.0f, 1.0f, 1.0f);
		case MTL_RTW_LIT | MTL_RTW_WS:
			return float4(1.0f, 1.0f, 0.0f, 1.0f);
		case MTL_RTW_SKY | MTL_RTW_WS:
			return float4(0.0f, 1.0f, 1.0f, 1.0f);
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
