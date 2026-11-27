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
 * rm_rtm_temporal, RT-primary's light on rastered alias models accumulated
 * over the views (TASK191 slice 4, PM9), in a translation unit of its own,
 * linked into ref_metal.metallib beside rm_rtmodel.metal, which it leaves
 * as it was: it includes that file for its helpers, its three entry
 * points renamed (the three #defines below, and no other), so rm_rtm_trace
 * compiles as before and still runs on every view that accumulates nothing.
 *
 * On a view whose world light was accumulated (rmb_rtdir_accumulate), for
 * each model pixel (material 2) with a normal: the traced light at its own
 * point, as rm_rtm_trace's, blended with the history where the G-buffer's
 * motion vector says the pixel was (the weapon's is its own projection's)
 * by TASK190's rule, under a key of its own (mtl_rtmt_key: the material
 * plus the tag's flags, never a world id) and the shading point's distance
 * from the eye. The history keeps the blended light with its count; the
 * light the resolve reads takes gl1's model colour of the blend
 * (rm_rtm_out), so the glow pulse and the minlight floor are each frame's.
 * The world's accumulation has already written every other pixel of the
 * history, the light and the key, and its filter has read them: this
 * kernel writes model pixels only.
 *
 * =======================================================================
 */

#define rm_rtm_unused_trace rm_rtmt_unused_trace
#define rm_rtm_unused_show rm_rtmt_unused_show
#define rm_rtm_trace rm_rtmt_unused_model
#include "rm_rtmodel.metal"
#include "common/mtl_rtmt_math.h"

/* rm_rttemporal.h's rm_rtt_view_t (common/mtl_rtt_math.h's mtl_rtt_view_t), as the world's accumulation took it */
struct rm_rtmt_view
{
	float alpha;
	float move;
	uint reset;
	uint seq;
};

/* the fourth count (rmb_rtmodel.m's RMB_RTM_COUNTS): the model pixels whose history was kept */
enum
{
	RM_RTMT_C_KEPT = 3
};

kernel void
rm_rtm_temporal(depth2d<float, access::read> depth [[texture(0)]],
		texture2d<float, access::read> normal [[texture(1)]],
		texture2d<uint, access::read> mat [[texture(2)]],
		texture2d<float, access::read> gbl [[texture(3)]],
		texture2d<float, access::write> vis_out [[texture(5)]],
		texture2d<float, access::write> light_out [[texture(6)]],
		texture2d_array<float> atlas [[texture(7)]],
		texture2d<float, access::read> motion [[texture(8)]],
		texture2d<float, access::read> hist_in [[texture(9)]],
		texture2d<uint, access::read> key_in [[texture(10)]],
		texture2d<float, access::write> hist_out [[texture(11)]],
		texture2d<uint, access::write> key_out [[texture(12)]],
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
		device const float4 *verts [[buffer(13)]],
		device const uint *idx [[buffer(14)]],
		constant rm_rtdir_geom &g [[buffer(15)]],
		device const uint *brec [[buffer(16)]],
		constant rm_rtm_params &p [[buffer(17)]],
		constant uchar *tabs [[buffer(18)]],
		constant rm_rtmt_view &tv [[buffer(19)]],
		uint2 px [[thread_position_in_grid]])
{
	const uint w = light_out.get_width(), hh = light_out.get_height();
	const bool inside = (px.x < w) && (px.y < hh);
	uint m = MTL_GB_MAT_NONE, model = 0u, kept = 0u;
	float d = 1.0f;
	float4 nn = float4(0.0f);
	rm_rtdir_tally t = { 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u };

	if (inside)
	{
		m = mat.read(px).r;
	}

	/* the model pass's pixels (rm_rtm_trace's test): a model pixel with a normal */
	if (inside && (m == MTL_GB_MAT_ALIAS))
	{
		d = depth.read(px);
		nn = normal.read(px);
		model = mtl_rtmt_model(m, d, nn.a) ? 1u : 0u;
	}

	if (model)
	{
		/* the light attachment gives the tag's flags alone, never light; a weapon pixel's point is its own view's */
		const uint f = rm_rtm_px_flags(m, gbl.read(px));
		const float3 pt = rm_rtm_point(v, p, px, d, f, ias, inst, tmat, ntris);
		const float3 n = normalize(nn.xyz * 2.0f - 1.0f);

		/* the world's terms at the model's point and normal, as rm_rtm_trace's */
		const float3 raw = rm_rtdir_direct(v, pt, n, styles, ias, inst, tmat, ntris, lights, dl, self, t);
		const float3 open = rm_rtdir_open(v, px, pt, n, ias, inst, tmat, ntris, tile, t);
		const float3 bounce = rm_rtdir_bounce(v, px, pt, n, ias, inst, tmat, ntris, tile, atlas, verts, idx, g, brec, t);
		const float4 l = rm_rtdir_out_open(rm_rtdir_out(raw, v.exposure), open + bounce, v.show);

		/* the history where the pixel was, under the model's key and its shading point's distance */
		const uint k = mtl_rtmt_key(f);
		const float dist = distance(pt, v.eye.xyz);
		const float2 mv = motion.read(px).xy;
		const mtl_rtt_px_t r = mtl_rtt_reproject(px.x, px.y, mv.x, mv.y, w, hh);
		const uint2 hk = r.ok ? key_in.read(uint2(r.x, r.y)).xy : uint2(0u);
		const float4 hl = r.ok ? hist_in.read(uint2(r.x, r.y)) : float4(0.0f);
		const mtl_rtp_rgba_t hv = { hl.r, hl.g, hl.b, hl.a };
		const uint rej = mtl_rtp_reject(tv.reset, r.ok, dist, as_type<float>(hk.x), tv.move, k, hk.y, hv);
		const mtl_rtp_px_t q = mtl_rtmt_pixel(rej, mtl_rtm_v3(l.r, l.g, l.b), hv, tv.alpha);

		/* gl1's model colour of the blend; the history keeps the blended light */
		light_out.write(rm_rtm_out(float3(q.out.r, q.out.g, q.out.b), f, p, tabs), px);
		vis_out.write(rm_rtdir_vis(v.show, t), px);
		hist_out.write(float4(q.hist.r, q.hist.g, q.hist.b, q.hist.a), px);
		key_out.write(uint4(as_type<uint>(dist), k, 0u, 0u), px);
		kept = (rej == 0u) ? 1u : 0u;
	}

	/* the frame's counts, one atomic per SIMD group and count */
	{
		const uint c_m = simd_sum(model), c_r = simd_sum(t.rays);
		const uint c_o = simd_sum(t.sky_rays + t.sun_rays + t.bounce_rays);
		const uint c_k = simd_sum(kept);

		if (simd_is_first())
		{
			atomic_fetch_add_explicit(&counts[RM_RTM_C_PIXELS], c_m, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[RM_RTM_C_RAYS], c_r, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[RM_RTM_C_OPEN], c_o, memory_order_relaxed);
			atomic_fetch_add_explicit(&counts[RM_RTMT_C_KEPT], c_k, memory_order_relaxed);
		}
	}
}
