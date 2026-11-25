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
 * rm_rtp_temporal, RT-primary's temporal accumulation of its light
 * (TASK190 slice 1, PM8; docs/plans/presentation-modes.md, section 4.3),
 * in a translation unit of its own, linked into ref_metal.metallib beside
 * rm_rtdir.metal, which it leaves as it was: the direct-light kernel's
 * output (the demodulated irradiance, no albedo, clamped) blended with its
 * history, read where the unjittered motion vector says the point was,
 * by common/mtl_rtp_math.h's rule (RT7's, with no light-set reject). It
 * reads the G-buffer's depth, normal, material and motion and the trace's
 * light, and no lightmap; it writes the light the resolve reads, the
 * history's light with its count (RGBA16Float) and the history's key
 * (the distance from the eye's bits and the material id, RG32Uint). Every
 * pixel that is not a traced world pixel is copied bit for bit.
 *
 * =======================================================================
 */

#include <metal_stdlib>

using namespace metal;

#include "common/mtl_rtp_math.h"

/*
 * The part of rm_rtdir.h's rm_rtdir_view_t (rm_rtdir.metal's rm_rtdir_view)
 * that the accumulation reads, at its start: the inverse of the view's
 * projection, its rectangle and the eye. The back end binds the whole view.
 */
struct rm_rtp_dirview
{
	float4x4 inv;
	float4 vp;
	float4 eye;
};

/* rm_rttemporal.h's rm_rtt_view_t (common/mtl_rtt_math.h's mtl_rtt_view_t) */
struct rm_rtt_view
{
	float alpha;
	float move;
	uint reset;
	uint seq;
};

/* the world position of a pixel's centre at its depth, as rm_rtdir_position */
static float3
rm_rtp_position(constant rm_rtp_dirview &v, uint2 px, float depth)
{
	const float cx = (float(px.x) + 0.5f - v.vp.x) / v.vp.z * 2.0f - 1.0f;
	const float cy = 1.0f - (float(px.y) + 0.5f - v.vp.y) / v.vp.w * 2.0f;
	const float4 h = v.inv * float4(cx, cy, depth * 2.0f - 1.0f, 1.0f);

	return h.xyz / h.w;
}

kernel void
rm_rtp_temporal(depth2d<float, access::read> depth [[texture(0)]],
		texture2d<float, access::read> normal [[texture(1)]],
		texture2d<uint, access::read> mat [[texture(2)]],
		texture2d<float, access::read> motion [[texture(3)]],
		texture2d<float, access::read> light_in [[texture(4)]],
		texture2d<float, access::read> hist_in [[texture(5)]],
		texture2d<uint, access::read> key_in [[texture(6)]],
		texture2d<float, access::write> light_out [[texture(7)]],
		texture2d<float, access::write> hist_out [[texture(8)]],
		texture2d<uint, access::write> key_out [[texture(9)]],
		constant rm_rtp_dirview &v [[buffer(0)]],
		constant rm_rtt_view &t [[buffer(1)]],
		uint2 px [[thread_position_in_grid]])
{
	const uint w = light_out.get_width(), hh = light_out.get_height();

	if ((px.x >= w) || (px.y >= hh))
	{
		return;
	}

	const uint m = mat.read(px).r;
	const float d = depth.read(px);
	const float4 c = light_in.read(px);
	const int world = mtl_rtp_world(m, d, normal.read(px).a);
	const mtl_rtp_rgba_t cur = { c.r, c.g, c.b, c.a };
	float dist = 0.0f;
	uint rej = 0u;
	mtl_rtp_rgba_t hv = { 0.0f, 0.0f, 0.0f, 0.0f };

	if (world)
	{
		const float2 mv = motion.read(px).xy;
		const mtl_rtt_px_t r = mtl_rtt_reproject(px.x, px.y, mv.x, mv.y, w, hh);
		const uint2 hk = r.ok ? key_in.read(uint2(r.x, r.y)).xy : uint2(0u);
		const float4 hl = r.ok ? hist_in.read(uint2(r.x, r.y)) : float4(0.0f);
		const float hd = as_type<float>(hk.x);
		const uint hm = hk.y;

		hv = { hl.r, hl.g, hl.b, hl.a };
		dist = distance(rm_rtp_position(v, px, d), v.eye.xyz);
		rej = mtl_rtp_reject(t.reset, r.ok, dist, hd, t.move, m, hm, hv);
	}

	{
		const mtl_rtp_px_t p = mtl_rtp_pixel(world, rej, cur, hv, t.alpha);

		light_out.write(float4(p.out.r, p.out.g, p.out.b, p.out.a), px);
		hist_out.write(float4(p.hist.r, p.hist.g, p.hist.b, p.hist.a), px);
		key_out.write(uint4(as_type<uint>(dist), m, 0u, 0u), px);
	}
}
