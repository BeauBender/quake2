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
 * rm_rtp_denoise, RT-primary's 5 x 5 filter of its accumulated light
 * (TASK190 slice 2, PM8; docs/plans/presentation-modes.md, section 4.3),
 * in a translation unit of its own, so rm_rtptemporal.metal and the
 * stock, Enhanced and Retro units are unchanged. rmb_rtdir.m dispatches
 * it in the accumulation's encoder right after rm_rtp_temporal, only
 * while r_rt_temporal is above 0, with the same bindings: it reads the
 * history and its key that kernel has just written and the G-buffer's
 * depth, normal and material, and writes the light the resolve reads
 * again on the world pixels whose count leaves a strength
 * (common/mtl_rtpd_math.h over RT7's weights, host-tested by
 * tests/metal/rm_rtpdenoise_test.c). It writes no history, so the history
 * keeps the accumulation alone, as RT7's denoiser.
 *
 * =======================================================================
 */

#include <metal_stdlib>

using namespace metal;

#include "common/mtl_rtpd_math.h"

/* a history texel as the rule takes it */
static mtl_rtp_rgba_t
mtl_rtpd_rgba(float4 v)
{
	const mtl_rtp_rgba_t c = { v.r, v.g, v.b, v.a };

	return c;
}

/* the G-buffer's normal of a texel, unpacked and normalized */
static mtl_rtao_v3_t
rm_rtpd_normal(float4 v)
{
	const float3 n = normalize(v.xyz * 2.0f - 1.0f);

	return mtl_rtao_v3(n.x, n.y, n.z);
}

kernel void
rm_rtp_denoise(depth2d<float, access::read> depth [[texture(0)]],
		texture2d<float, access::read> normal [[texture(1)]],
		texture2d<uint, access::read> mat [[texture(2)]],
		texture2d<float, access::read> light_in [[texture(4)]],
		texture2d<float, access::write> light_out [[texture(7)]],
		texture2d<float, access::read> hist [[texture(8)]],
		texture2d<uint, access::read> key [[texture(9)]],
		uint2 px [[thread_position_in_grid]])
{
	const uint w = light_out.get_width(), hh = light_out.get_height();

	if ((px.x >= w) || (px.y >= hh))
	{
		return;
	}

	const float4 hc = hist.read(px);
	const float4 ncv = normal.read(px);

	if (!mtl_rtpd_on(mtl_rtp_world(mat.read(px).r, depth.read(px), ncv.a), hc.a))
	{
		return;
	}

	const uint2 kc = key.read(px).xy;
	const float d = as_type<float>(kc.x);
	const uint m = kc.y;
	const mtl_rtao_v3_t nc = rm_rtpd_normal(ncv);
	mtl_rtpd_acc_t acc = mtl_rtpd_acc();

	for (int dy = -MTL_RTD_RADIUS; dy <= MTL_RTD_RADIUS; dy++)
	{
		for (int dx = -MTL_RTD_RADIUS; dx <= MTL_RTD_RADIUS; dx++)
		{
			const int sx = (int)px.x + dx, sy = (int)px.y + dy;

			if ((sx < 0) || (sy < 0) || (sx >= (int)w) || (sy >= (int)hh))
			{
				continue;
			}

			const uint2 sp = uint2((uint)sx, (uint)sy);
			const float4 hn = hist.read(sp);
			const uint2 kn = key.read(sp).xy;
			const float4 nv = normal.read(sp);
			const mtl_rtao_v3_t nn = rm_rtpd_normal(nv);
			const float wt = mtl_rtpd_weight(dx, dy, d, as_type<float>(kn.x), nc, nn, m, kn.y, hn.a, nv.a);

			acc = mtl_rtpd_add(acc, wt, mtl_rtpd_rgba(hn));
		}
	}

	{
		const mtl_rtp_rgba_t r = mtl_rtpd_result(acc, mtl_rtpd_rgba(hc), light_in.read(px).a);

		light_out.write(float4(r.r, r.g, r.b, r.a), px);
	}
}
