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
 * RT7's blur of the water reflections (TASK59 slice 2b;
 * docs/plans/metal-raytracing-design.md, section 2: "reflections get no
 * history and a 3 x 3 depth-guided blur"). The weight rule is
 * common/mtl_rtd_math.h's (mtl_rtd_rweight, host-tested by
 * tests/metal/rm_rtdenoise_test.c). rmb_rtwater.m dispatches it after the
 * reflections' trace, only while r_rt_temporal is above 0, and the blend
 * and the forward pass then read what it wrote in place of the trace's
 * output. A translation unit of its own, so the stock water kernels'
 * unit (rm_rtwater.metal) and slice 2's denoiser's are unchanged. It
 * reads this frame's reflection alone: no history is read or written.
 *
 * =======================================================================
 */

#include <metal_stdlib>

using namespace metal;

#include "common/mtl_shader_math.h"
#include "common/mtl_gbuf_math.h"
#include "common/mtl_rtao_math.h"
#include "common/mtl_rts_math.h"
#include "common/mtl_rtw_math.h"
#include "common/mtl_rtd_math.h"

/* the class byte the trace wrote in the reflection's alpha (rm_rtwater.metal's rm_rtw_cls) */
static uint
rm_rtd_rcls(float a)
{
	return uint(rint(a * 255.0f));
}

/* 1 when a pixel of class cn holds a reflection the pixel of class cc blurs with: water, a reflection, the same layer */
static int
rm_rtd_rok(uint cc, uint cn)
{
	return (cn & MTL_RTW_CLASS_WATER) && ((cn & MTL_RTW_CLASS_FROM) != MTL_RTW_MISS) &&
		((cn & MTL_RTW_CLASS_TRANS) == (cc & MTL_RTW_CLASS_TRANS));
}

/* the distance from the eye of the water at q: the trace's for translucent water, the depth's unprojected for the rest */
static float
rm_rtd_rdist(constant mtl_rtd_rview_t &v, depth2d<float, access::read> depth, texture2d<float, access::read> wdist,
		uint2 q, uint cls)
{
	const float4x4 inv = float4x4(float4(v.inv[0], v.inv[1], v.inv[2], v.inv[3]),
		float4(v.inv[4], v.inv[5], v.inv[6], v.inv[7]), float4(v.inv[8], v.inv[9], v.inv[10], v.inv[11]),
		float4(v.inv[12], v.inv[13], v.inv[14], v.inv[15]));
	const float2 pc = float2(q) + 0.5f;
	float cx, cy;
	float4 h;

	if (cls & MTL_RTW_CLASS_TRANS)
	{
		return wdist.read(q).r;
	}

	cx = (pc.x - v.vp[0]) / v.vp[2] * 2.0f - 1.0f;
	cy = 1.0f - (pc.y - v.vp[1]) / v.vp[3] * 2.0f;
	h = inv * float4(cx, cy, depth.read(q) * 2.0f - 1.0f, 1.0f);
	return distance(h.xyz / h.w, float3(v.eye[0], v.eye[1], v.eye[2]));
}

/*
 * One pixel of the blurred reflection: a pixel without a reflection is
 * copied as it is, so every pixel the blend or the forward pass reads is
 * written; one with a reflection gets the weighted mean of its 3 x 3
 * neighbours' colours that hold one, its class (the alpha) kept.
 */
kernel void
rm_rtw_denoise(texture2d<float, access::read> refl [[texture(0)]],
		depth2d<float, access::read> depth [[texture(1)]],
		texture2d<float, access::read> wdist [[texture(2)]],
		texture2d<float, access::write> refl_out [[texture(3)]],
		constant mtl_rtd_rview_t &v [[buffer(0)]],
		uint2 px [[thread_position_in_grid]])
{
	const uint w = min(min(refl.get_width(), depth.get_width()), wdist.get_width());
	const uint h = min(min(refl.get_height(), depth.get_height()), wdist.get_height());
	mtl_rtd_acc_t ar = mtl_rtd_acc(), ag = mtl_rtd_acc(), ab = mtl_rtd_acc();
	float4 c;
	uint cc;
	float dc;

	if ((px.x >= refl_out.get_width()) || (px.y >= refl_out.get_height()) || (px.x >= refl.get_width()) ||
		(px.y >= refl.get_height()))
	{
		return;
	}

	c = refl.read(px);
	cc = rm_rtd_rcls(c.a);

	if (!rm_rtd_rok(cc, cc) || (px.x >= w) || (px.y >= h))
	{
		refl_out.write(c, px);
		return;
	}

	dc = rm_rtd_rdist(v, depth, wdist, px, cc);

	for (int dy = -MTL_RTD_REFL_RADIUS; dy <= MTL_RTD_REFL_RADIUS; dy++)
	{
		for (int dx = -MTL_RTD_REFL_RADIUS; dx <= MTL_RTD_REFL_RADIUS; dx++)
		{
			const int2 q = int2(px) + int2(dx, dy);
			float4 n;
			uint cn;
			int ok;
			float dn, wt;

			if ((q.x < 0) || (q.y < 0) || (q.x >= int(w)) || (q.y >= int(h)))
			{
				continue;
			}

			n = refl.read(uint2(q));
			cn = rm_rtd_rcls(n.a);
			ok = rm_rtd_rok(cc, cn);
			dn = ok ? rm_rtd_rdist(v, depth, wdist, uint2(q), cn) : dc;
			wt = mtl_rtd_rweight(dx, dy, dc, dn, ok);
			ar = mtl_rtd_add(ar, wt, n.r);
			ag = mtl_rtd_add(ag, wt, n.g);
			ab = mtl_rtd_add(ab, wt, n.b);
		}
	}

	refl_out.write(float4(mtl_rtd_rresult(ar, c.r), mtl_rtd_rresult(ag, c.g), mtl_rtd_rresult(ab, c.b), c.a), px);
}
