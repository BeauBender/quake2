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
 * rm_rtm_trace, RT-primary's light on rastered alias models (TASK191
 * slice 2, PM9), in a translation unit of its own, linked into
 * ref_metal.metallib beside rm_rtdir.metal. It takes that file's static
 * helpers by including it, its two entry points renamed (the two
 * #defines below, and no other: rm_rtdir.metal's own unit is left as it
 * was, so the stock and RT-primary kernels compile as before).
 *
 * After the world's trace (rmb_rtmodel.m), for each model pixel
 * (material 2) with a normal: the terms a world pixel takes at its own
 * point and normal, the map's lights with their shadow rays, the
 * dynamic lights, the sky, the sun and the bounce (rm_rtdir_direct,
 * rm_rtdir_open, rm_rtdir_bounce), under the world's exposure and its
 * r_rt_direct_show view. The view the host binds has no own-light table
 * (nself 0), so rm_rtdir_own returns at once: a model is no emissive
 * face. That light takes the place of gl1's shade light and shadedot in
 * gl1's model colour (mtl_rtm_color, at MTL_RTM_DOT_TRACED), through the
 * host's minlight and gamma tables. No lightmap is read: the model's
 * own light attachment (gbl) is read for its tag alone, never as light.
 * It writes the light and the visibility of model pixels only, every
 * other pixel as the world's trace left it, and adds to no sum of the
 * exposure fit: its counts are its own.
 *
 * =======================================================================
 */

#define rm_rtdir_trace rm_rtm_unused_trace
#define rm_fragment_rtdir_show rm_rtm_unused_show
#include "rm_rtdir.metal"
#include "common/mtl_rtm_math.h"

/* rm_rtmodelscene.h's rm_rtm_params_t */
struct rm_rtm_params
{
	float4x4 winv;      /* slice 3: the view weapon's GL clip coordinates to world, when weapon is 1 */
	float glow;         /* RF_GLOW's pulse at the frame's time (rm_rtm_glow_scale) */
	float overbright;   /* gl1_overbrightbits as rm_alias_shade reads it, 0 for none */
	uint weapon;        /* 1 when winv holds the frame's weapon view */
	uint pad1;
};

/* the counts, one word each (rmb_rtmodel.m's RMB_RTM_COUNTS): the model pixels traced, their shadow rays, their sky, sun and bounce rays */
enum
{
	RM_RTM_C_PIXELS = 0,
	RM_RTM_C_RAYS = 1,
	RM_RTM_C_OPEN = 2
};

/*
 * The flags of a model pixel's tag (mtl_rtm_tag_flags) in its light
 * attachment c, at material 2 only: a world face's lightmap texel is
 * never read as a tag. 0 for any other colour (nothing tags a model
 * before slice 3).
 */
static uint
rm_rtm_px_flags(uint m, float4 c)
{
	mtl_rgba_t t;

	t.r = c.r;
	t.g = c.g;
	t.b = c.b;
	t.a = c.a;
	return (m == MTL_GB_MAT_ALIAS) ? mtl_rtm_tag_flags(t) : 0u;
}

/*
 * A model pixel's light from the traced light l: gl1's colour index
 * (mtl_rtm_color, the traced light in place of the shade light times the
 * shadedot), then the minlight table (tabs[0 .. 255]) and the gamma table
 * (tabs[256 .. 511]), as R_DrawAliasFrameLerp; alpha an opaque model's.
 */
static float4
rm_rtm_out(float3 l, uint f, constant rm_rtm_params &p, constant uchar *tabs)
{
	const mtl_rtm_idx_t i = mtl_rtm_color(mtl_rtm_v3(l.r, l.g, l.b), MTL_RTM_DOT_TRACED, f, p.glow, p.overbright);

	return float4(float(tabs[256 + tabs[i.r]]), float(tabs[256 + tabs[i.g]]), float(tabs[256 + tabs[i.b]]), 255.0f) / 255.0f;
}

kernel void
rm_rtm_trace(depth2d<float, access::read> depth [[texture(0)]],
		texture2d<float, access::read> normal [[texture(1)]],
		texture2d<uint, access::read> mat [[texture(2)]],
		texture2d<float, access::read> gbl [[texture(3)]],
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
		device const ushort *tile [[buffer(12)]],
		texture2d_array<float> atlas [[texture(7)]],
		device const float4 *verts [[buffer(13)]],
		device const uint *idx [[buffer(14)]],
		constant rm_rtdir_geom &g [[buffer(15)]],
		device const uint *brec [[buffer(16)]],
		constant rm_rtm_params &p [[buffer(17)]],
		constant uchar *tabs [[buffer(18)]],
		uint2 px [[thread_position_in_grid]])
{
	const bool inside = (px.x < light_out.get_width()) && (px.y < light_out.get_height());
	uint m = MTL_GB_MAT_NONE, model = 0u;
	float d = 1.0f;
	float4 nn = float4(0.0f);
	rm_rtdir_tally t = { 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u };

	if (inside)
	{
		m = mat.read(px).r;
	}

	/* a model pixel with a normal (rm_gb_alias_extra wrote it); one without keeps its stock light */
	if (inside && (m == MTL_GB_MAT_ALIAS))
	{
		d = depth.read(px);
		nn = normal.read(px);
		model = ((d < 1.0f) && (nn.a > 0.5f)) ? 1u : 0u;
	}

	if (model)
	{
		const float3 pt = rm_rtdir_position(v, px, d);
		const float3 n = normalize(nn.xyz * 2.0f - 1.0f);

		/* the world's terms at the model's point and normal, from their rays alone (no texture reaches them) */
		const float3 raw = rm_rtdir_direct(v, pt, n, styles, ias, inst, tmat, ntris, lights, dl, self, t);
		const float3 open = rm_rtdir_open(v, px, pt, n, ias, inst, tmat, ntris, tile, t);
		const float3 bounce = rm_rtdir_bounce(v, px, pt, n, ias, inst, tmat, ntris, tile, atlas, verts, idx, g, brec, t);
		const float4 l = rm_rtdir_out_open(rm_rtdir_out(raw, v.exposure), open + bounce, v.show);

		/* the light attachment gives the tag's flags alone, never light */
		const uint f = rm_rtm_px_flags(m, gbl.read(px));

		light_out.write(rm_rtm_out(l.rgb, f, p, tabs), px);
		vis_out.write(rm_rtdir_vis(v.show, t), px);
	}

	/* the frame's counts, one atomic per SIMD group and count */
	{
		const uint c_m = simd_sum(model), c_r = simd_sum(t.rays);
		const uint c_o = simd_sum(t.sky_rays + t.sun_rays + t.bounce_rays);

		if (simd_is_first())
		{
			atomic_fetch_add_explicit(&counts[RM_RTM_C_PIXELS], c_m, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[RM_RTM_C_RAYS], c_r, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[RM_RTM_C_OPEN], c_o, memory_order_relaxed);
		}
	}
}
