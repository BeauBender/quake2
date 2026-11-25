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
 * The shaders of RT-primary's direct light (TASK188, PM6 slices 1 to 3;
 * docs/plans/presentation-modes.md, section 4.1), linked into
 * ref_metal.metallib beside rm_rtsun.metal:
 *
 * - rm_rtdir_trace, the compute pass after the G-buffer raster and before
 *   the other effects' kernels and the resolve: for each lightmapped world
 *   pixel (material 1), the sum over the map's lights in range of qrad3's
 *   (intensity - dist) x N.L (mtl_rtdir_scale) and over its emissive
 *   faces in range of qrad3's intensity / dist^2 x both cosines
 *   (mtl_rtdir_emit_scale, slice 2) times the light's colour and
 *   lightstyle, each light with one shadow ray from the surface
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
#include "common/mtl_rts_math.h"
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
	uint ndlights;
	uint nself;
	uint pad0;
	uint pad1;
};

/* rm_rtdir.h's rm_rtdir_light_t */
struct rm_rtdir_light
{
	float4 pos;     /* origin, intensity */
	float4 color;   /* rgb, style */
	float4 spot;    /* direction, cos(cone) or MTL_RTDIR_NOSPOT */
};

/* rm_rtdir.h's rm_rtdir_dlight_t (TASK188 slice 3b) */
struct rm_rtdir_dlight
{
	float4 pos;     /* origin, intensity */
	float4 color;   /* rgb, 0 */
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
 * face is passed, the nearest first, up to MTL_RTDIR_LAYERS of them, and
 * so is an alias model that carries the light (TASK188 slice 3b: carry,
 * the dynamic light's mtl_rtdir_dlight_bit; 0 for a map light).
 * inst holds two words an instance (rmb_rt_frame): its first triangle of
 * the map's buffers (~0 for an alias model) and the lights it carries;
 * tmat the map's material id of each of its ntris triangles.
 */
static float
rm_rtdir_seen(ray r, instance_acceleration_structure ias, device const uint *inst, device const uchar *tmat, uint ntris,
		uint carry)
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
		else if ((carry & inst[2 * hit.instance_id + 1]) != 0u)
		{
			m = MTL_RTDIR_MAT_CARRIER;
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

/*
 * The frame's counts (rmb_rtdir.m's RMB_RTDIR_COUNTS). The lights in
 * range, the rays and the lit ones are 64-bit, a low word and a high one
 * (rm_rtdir_add64): with up to RM_RTDIR_MAX_UPLOAD lights in range a pixel,
 * 32 bits would wrap at 4K.
 */
#define RM_RTDIR_C_WORLD 0
#define RM_RTDIR_C_INRANGE 1
#define RM_RTDIR_C_RAYS 2
#define RM_RTDIR_C_LIT 3
#define RM_RTDIR_C_FIT_LM 4
#define RM_RTDIR_C_FIT_RAW 5
#define RM_RTDIR_C_FIT_N 6
#define RM_RTDIR_C_INRANGE_HI 7
#define RM_RTDIR_C_RAYS_HI 8
#define RM_RTDIR_C_LIT_HI 9
#define RM_RTDIR_C_CAPPED 10

/* a pixel's counts: the lights in range and facing, the shadow rays, the ones that reached their light, the cap */
struct rm_rtdir_tally
{
	uint inrange;
	uint rays;
	uint lit;
	uint capped;
};

/*
 * TASK188 slice 3b: the light of the face a world pixel at p is on, its
 * own (qrad3 adds an emissive face's patch light to its own lightmap), in
 * the lightmap's units: the word of the triangle the ray from the eye to
 * p meets (self, the map's per-triangle table, mtl_rtdir_self), passing
 * water and translucent faces as a shadow ray does, at most
 * MTL_RTDIR_LAYERS + 1 intersections; 0 for a triangle past the table, an
 * alias model or nothing met.
 */
static float3
rm_rtdir_own(constant rm_rtdir_view &v, float3 p, instance_acceleration_structure ias, device const uint *inst,
		device const uchar *tmat, uint ntris, device const uint *self)
{
	intersector<triangle_data, instancing> isect;
	const float3 to = p - v.eye.xyz;
	const float len = length(to);
	ray r;

	if (!(len > 0.0f) || (v.nself == 0u))
	{
		return float3(0.0f);
	}

	isect.assume_geometry_type(geometry_type::triangle);
	r = ray(v.eye.xyz, to / len, 0.0f, len + 1.0f);

	for (int hop = 0; hop <= MTL_RTDIR_LAYERS; hop++)
	{
		intersection_result<triangle_data, instancing> hit = isect.intersect(r, ias, MTL_RTAO_MASK);
		uint b, tri, m;

		if (hit.type == intersection_type::none)
		{
			return float3(0.0f);
		}

		b = inst[2 * hit.instance_id];

		if (b == 0xffffffffu)
		{
			return float3(0.0f);
		}

		tri = b + hit.primitive_id;
		m = (tri < ntris) ? (uint)tmat[tri] : (uint)MTL_GB_MAT_WORLD;

		if (mtl_rtdir_at(m, hop) != MTL_RTDIR_AT_PASS)
		{
			const mtl_rgba_t c = mtl_rtdir_self((tri < v.nself) ? self[tri] : 0u);

			return float3(c.r, c.g, c.b);
		}

		r.min_distance = hit.distance + 0.01f;
	}

	return float3(0.0f);
}

/*
 * TASK188 slice 3b, RT3's dynamic lights as a term of RT-primary's light,
 * in the lightmap's units: each of the frame's dynamic lights (at most
 * MTL_RTDIR_MAX_DLIGHTS, mtl_rtdir_dlight_walk) that adds at p by gl1's
 * rule (mtl_rtdir_dlight_add, its radius rule as RT3 measures it) adds
 * that times its colour where its one shadow ray, from o, reaches it,
 * passing a model that carries it. A term of its own beside the map's
 * lights: none of them takes a dynamic light's place, so a dynamic light
 * reaches as far as in the stock lightmaps; under r_rt_direct_show 3 no
 * ray is traced. At most MTL_RTDIR_MAX_DLIGHTS rays a pixel.
 */
static float3
rm_rtdir_dyn(constant rm_rtdir_view &v, float3 p, float3 n, float3 o, instance_acceleration_structure ias,
		device const uint *inst, device const uchar *tmat, uint ntris, constant rm_rtdir_dlight *dl,
		thread rm_rtdir_tally &t)
{
	float3 sum = float3(0.0f);

	for (uint i = 0; mtl_rtdir_dlight_walk(i, v.ndlights); i++)
	{
		const rm_rtdir_dlight d = dl[i];
		const float a = mtl_rtdir_dlight_add(mtl_rts_v3(p.x, p.y, p.z), mtl_rts_v3(n.x, n.y, n.z),
				mtl_rts_v3(d.pos.x, d.pos.y, d.pos.z), d.pos.w);

		if (!(a > 0.0f))
		{
			continue;
		}

		t.inrange++;

		if (v.show != 3u)
		{
			const float3 to = d.pos.xyz - o;
			const float len = length(to);

			t.rays++;

			if ((len > 1.0f) &&
				(rm_rtdir_seen(ray(o, to / len, 0.0f, len - 1.0f), ias, inst, tmat, ntris, mtl_rtdir_dlight_bit(i)) < 0.5f))
			{
				continue;
			}

			t.lit++;
		}

		sum += a * d.color.rgb;
	}

	return sum;
}

/*
 * The direct light at the world point p of unit normal n, in the
 * lightmap's units: of the lights in range and facing, point lights and
 * emissive faces alike (mtl_rtdir_reach, mtl_rtdir_contrib), the
 * MTL_RTDIR_MAX_RAYS strongest are the candidates (mtl_rtdir_keep);
 * traced strongest first (mtl_rtdir_keep_rank, TASK188 slice 3), the first
 * MTL_RTDIR_MAX_TRACED whose shadow ray reaches the point (mtl_rtdir_walk)
 * are summed, each one's contribution times its colour and style, so an
 * occluded light takes no place; under r_rt_direct_show 3 no ray is
 * traced and the first MTL_RTDIR_MAX_TRACED are. At most
 * MTL_RTDIR_MAX_RAYS rays a pixel; past the bound a pixel is capped
 * (mtl_rtdir_capped). TASK188 slice 3b adds the frame's dynamic lights
 * (rm_rtdir_dyn, their own term) and the light of an emissive face the
 * pixel is on (rm_rtdir_own, times style 0, as qrad3 adds it to the
 * style-0 lightmap); r_rt_direct_show 4 is the dynamic lights alone.
 * Under views 3 and 4 the own light's eye ray is not cast (TASK188
 * slice 3c): view 3 traces no ray at all, and view 4 does not show it. It
 * takes no texture: the light a world pixel is written with comes from
 * the lights alone. A ray stops a unit short of its light: an entity's
 * origin is in open space.
 */
static float3
rm_rtdir_direct(constant rm_rtdir_view &v, float3 p, float3 n, constant packed_float3 *styles,
		instance_acceleration_structure ias, device const uint *inst, device const uchar *tmat, uint ntris,
		device const rm_rtdir_light *lights, constant rm_rtdir_dlight *dl, device const uint *self,
		thread rm_rtdir_tally &t)
{
	const float3 o = p + n * mtl_rtao_bias(distance(p, v.eye.xyz), v.bias_k);
	mtl_rtdir_keep_t k;
	float3 raw = float3(0.0f);
	uint seen = 0u;

	k.n = 0u;
	k.capped = 0u;

	for (uint i = 0; i < v.nlights; i++)
	{
		const rm_rtdir_light l = lights[i];
		const float3 dv = l.pos.xyz - p;
		const float dist = length(dv);
		float3 dir;
		float s;

		/* the cheap test first: a point light's linear falloff ends at its intensity, a face's at its cut */
		if (!mtl_rtdir_reach(l.pos.w, dist, l.spot.w))
		{
			continue;
		}

		/* a point or spot light (qrad3's emit_point, emit_spotlight) or an emissive face (emit_surface) */
		dir = dv / dist;
		s = mtl_rtdir_contrib(l.pos.w, dist, dot(n, dir), -dot(dir, l.spot.xyz), l.spot.w);

		if (!(s > 0.0f))
		{
			continue;
		}

		t.inrange++;
		k = mtl_rtdir_keep(k, i, s);
	}

	k = mtl_rtdir_keep_rank(k);

	for (uint j = 0; mtl_rtdir_walk(j, k.n, seen); j++)
	{
		const rm_rtdir_light l = lights[k.i[j]];

		if (v.show != 3u)
		{
			const float3 to = l.pos.xyz - o;
			const float len = length(to);

			t.rays++;

			if ((len > 1.0f) && (rm_rtdir_seen(ray(o, to / len, 0.0f, len - 1.0f), ias, inst, tmat, ntris, 0u) < 0.5f))
			{
				continue;
			}

			t.lit++;
		}

		seen++;
		raw += k.s[j] * l.color.rgb * float3(styles[(uint)l.color.w & 255u]);
	}

	t.capped = mtl_rtdir_capped(k, seen);

	{
		const float3 dyn = rm_rtdir_dyn(v, p, n, o, ias, inst, tmat, ntris, dl, t);
		const float3 own = ((v.show == 3u) || (v.show == 4u)) ? float3(0.0f) :
				rm_rtdir_own(v, p, ias, inst, tmat, ntris, self) * float3(styles[0]);

		return ((v.show == 4u) ? dyn : (raw + dyn + own)) * MTL_RTDIR_UNIT;
	}
}

/* the exposure fit's samples: every MTL_RTDIR_FIT_STEP-th pixel a row and a column */
static bool
rm_rtdir_fit_at(uint2 px)
{
	return ((px.x % MTL_RTDIR_FIT_STEP) == 0u) && ((px.y % MTL_RTDIR_FIT_STEP) == 0u);
}

/*
 * The exposure fit's sample at px, the one read of the lightmap: its
 * quantized value and the traced light's (mtl_rtdir_q), for the frame's
 * sums. raw comes by value: nothing here reaches a pixel's light.
 */
static uint2
rm_rtdir_fit_sample(texture2d<float, access::read> lm, uint2 px, float3 raw)
{
	const float4 lms = lm.read(px);

	return uint2(mtl_rtdir_q(lms.r, lms.g, lms.b), mtl_rtdir_q(raw.r, raw.g, raw.b));
}

/* the light a traced world pixel is written with: mtl_rtdir_light of its traced light and the exposure */
static float4
rm_rtdir_out(float3 raw, float exposure)
{
	const mtl_rgba_t c = mtl_rtdir_light(raw.r, raw.g, raw.b, exposure);

	return float4(c.r, c.g, c.b, c.a);
}

/* the visibility a traced world pixel is written with: the share of its rays that reached their light, whether any light was in range */
static float4
rm_rtdir_vis(uint show, rm_rtdir_tally t)
{
	const float seen = (show == 3u) ? 1.0f : (t.rays ? float(t.lit) / float(t.rays) : 0.0f);

	return float4(seen, t.inrange ? 1.0f : 0.0f, 1.0f, 1.0f);
}

/* a 64-bit count in two words: the carry into hi when this add wraps lo */
static void
rm_rtdir_add64(device atomic_uint *lo, device atomic_uint *hi, uint n)
{
	const uint was = atomic_fetch_add_explicit(lo, n, memory_order_relaxed);

	if (was + n < was)
	{
		atomic_fetch_add_explicit(hi, 1u, memory_order_relaxed);
	}
}

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
		constant rm_rtdir_dlight *dl [[buffer(10)]],
		device const uint *self [[buffer(11)]],
		uint2 px [[thread_position_in_grid]])
{
	const bool inside = (px.x < light_out.get_width()) && (px.y < light_out.get_height());
	uint m = MTL_GB_MAT_NONE, world = 0u;
	float d = 1.0f;
	float4 nn = float4(0.0f);
	rm_rtdir_tally t = { 0u, 0u, 0u, 0u };

	if (inside)
	{
		m = mat.read(px).r;
	}

	if (inside && (m == MTL_GB_MAT_WORLD))
	{
		d = depth.read(px);
		nn = normal.read(px);
		world = ((d < 1.0f) && (nn.a > 0.5f)) ? 1u : 0u;
	}

	/* the traced light, from the lights alone (no texture reaches rm_rtdir_direct) */
	const float3 raw = world ? rm_rtdir_direct(v, rm_rtdir_position(v, px, d), normalize(nn.xyz * 2.0f - 1.0f), styles, ias,
			inst, tmat, ntris, lights, dl, self, t) : float3(0.0f);

	/* the fit's sample: the lightmap enters the frame's sums, never a pixel's light */
	const uint sampled = (world && rm_rtdir_fit_at(px)) ? 1u : 0u;
	const uint2 fit = sampled ? rm_rtdir_fit_sample(lm, px, raw) : uint2(0u);

	if (inside)
	{
		/* a traced world pixel takes the traced light; any other pixel its light as it was, bit for bit */
		if (world)
		{
			light_out.write(rm_rtdir_out(raw, v.exposure), px);
			vis_out.write(rm_rtdir_vis(v.show, t), px);
		}
		else
		{
			light_out.write(base.read(px), px);
			vis_out.write(float4(0.0f), px);
		}
	}

	/* the frame's counts, one atomic per SIMD group and count */
	{
		const uint c_w = simd_sum(world), c_i = simd_sum(t.inrange), c_r = simd_sum(t.rays), c_l = simd_sum(t.lit);
		const uint c_fl = simd_sum(fit.x), c_fr = simd_sum(fit.y), c_fn = simd_sum(sampled), c_c = simd_sum(t.capped);

		if (simd_is_first())
		{
			atomic_fetch_add_explicit(&counts[RM_RTDIR_C_WORLD], c_w, memory_order_relaxed);
			rm_rtdir_add64(&counts[RM_RTDIR_C_INRANGE], &counts[RM_RTDIR_C_INRANGE_HI], c_i);
			rm_rtdir_add64(&counts[RM_RTDIR_C_RAYS], &counts[RM_RTDIR_C_RAYS_HI], c_r);
			rm_rtdir_add64(&counts[RM_RTDIR_C_LIT], &counts[RM_RTDIR_C_LIT_HI], c_l);
			atomic_fetch_add_explicit(&counts[RM_RTDIR_C_FIT_LM], c_fl, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[RM_RTDIR_C_FIT_RAW], c_fr, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[RM_RTDIR_C_FIT_N], c_fn, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[RM_RTDIR_C_CAPPED], c_c, memory_order_relaxed);
		}
	}
}

/*
 * r_rt_direct_show over the resolved view: 1, 3 and 4, the direct term
 * alone (the light the resolve reads, on white albedo; 4 the dynamic
 * lights' term only); 2, the shadow
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
