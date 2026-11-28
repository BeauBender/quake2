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
 * RT7's denoiser (TASK59 slice 2; docs/plans/metal-raytracing-design.md,
 * section 2): a 5 x 5 bilateral blur of the accumulated AO and sun,
 * guided by the distance from the eye and the normal, its strength scaled
 * by the pixel's sample count (common/mtl_rtd_math.h, host-tested by
 * tests/metal/rm_rtdenoise_test.c). Each kernel runs in its effect's
 * encoder right after the temporal kernel (rm_rtao_temporal,
 * rm_rtsun_temporal), only while r_rt_temporal is above 0, and writes the
 * light the resolve reads again from the denoised value. It reads the
 * history the temporal kernel has just written and writes none, so the
 * history keeps the accumulation alone.
 *
 * A translation unit of its own (the warning of slice 1b,
 * docs/runbook/TASK59.md): the stock kernels' units are unchanged, so is
 * their compiled code.
 *
 * =======================================================================
 */

#include <metal_stdlib>

using namespace metal;

#include "common/mtl_shader_math.h"
#include "common/mtl_gbuf_math.h"
#include "common/mtl_rtp_math.h"   /* FIX219: the G-buffer depth and normal tests (mtl_rtp_below1, mtl_rtp_above_half) */
#include "common/mtl_rtsun_math.h"
#include "common/mtl_rtd_math.h"

/*
 * The denoised value of pixel px of a w x hh history: the history's
 * (value, distance from the eye, material id, sample count) at the
 * pixel and at its 5 x 5 neighbours inside the size, each neighbour
 * weighted by mtl_rtd_weight against the pixel's distance, normal and
 * material (a pixel without a normal, alpha below 0.5, weighs 0), and
 * the pixel's value moved toward their mean by the strength of its count.
 */
static float
rm_rtd_filter(texture2d<float, access::read> normal, texture2d<float, access::read> hist, uint2 px, uint w, uint hh,
		float4 hc)
{
	const float4 ncv = normal.read(px);
	const float3 n3 = normalize(ncv.xyz * 2.0f - 1.0f);
	const mtl_rtao_v3_t nc = mtl_rtao_v3(n3.x, n3.y, n3.z);
	const uint mc = (uint)hc.z;
	mtl_rtd_acc_t acc = mtl_rtd_acc();

	for (int dy = -MTL_RTD_RADIUS; dy <= MTL_RTD_RADIUS; dy++)
	{
		for (int dx = -MTL_RTD_RADIUS; dx <= MTL_RTD_RADIUS; dx++)
		{
			const int sx = (int)px.x + dx, sy = (int)px.y + dy;

			if ((sx < 0) || (sy < 0) || (sx >= (int)w) || (sy >= (int)hh))
			{
				continue;
			}

			{
				const uint2 sp = uint2((uint)sx, (uint)sy);
				const float4 hn = hist.read(sp);
				const float4 nv = normal.read(sp);
				const float3 m3 = normalize(nv.xyz * 2.0f - 1.0f);
				const mtl_rtao_v3_t nn = mtl_rtao_v3(m3.x, m3.y, m3.z);
				const uint mn = (uint)hn.z;
				const float wt = mtl_rtp_above_half(nv.a) ? mtl_rtd_weight(dx, dy, hc.y, hn.y, nc, nn, mc, mn, hn.w) : 0.0f;

				acc = mtl_rtd_add(acc, wt, hn.x);
			}
		}
	}

	return mtl_rtd_result(acc, hc.x, hc.w);
}

/*
 * The AO's: on the world's pixels (the ones rm_rtao_temporal lights)
 * whose count leaves a strength above 0, the light written again from the
 * denoised AO as rm_rtao_temporal writes it from the accumulated one.
 * The bindings are the AO encoder's: the history the temporal kernel
 * wrote at 8, the denoiser's view at 10.
 */
kernel void
rm_rtao_denoise(texture2d<float, access::read> normal [[texture(1)]],
		texture2d<float, access::read> light [[texture(3)]],
		texture2d<float, access::write> light_out [[texture(5)]],
		texture2d<float, access::read> hist [[texture(8)]],
		constant mtl_rtd_view_t &dv [[buffer(10)]],
		uint2 px [[thread_position_in_grid]])
{
	const uint w = hist.get_width(), hh = hist.get_height();

	if ((px.x >= w) || (px.y >= hh))
	{
		return;
	}

	{
		const float4 hc = hist.read(px);
		const uint m = (uint)hc.z;

		if ((m != MTL_GB_MAT_WORLD) || !mtl_rtd_counted(hc.w) || !mtl_rtp_above_half(normal.read(px).a))
		{
			return;
		}

		{
			const float ao = rm_rtd_filter(normal, hist, px, w, hh, hc);
			const float4 l = light.read(px);
			mtl_rgba_t c;

			c.r = l.r;
			c.g = l.g;
			c.b = l.b;
			c.a = l.a;
			c = mtl_rtao_light(c, m, mtl_rtao_factor(ao, dv.floor_));
			light_out.write(float4(c.r, c.g, c.b, c.a), px);
		}
	}
}

/*
 * The sun's: on the pixels the sun traced this frame (a count of 1 or
 * more; rm_rtsun_temporal writes 0 on the others and does not light
 * them) whose count leaves a strength above 0, the light written again
 * from the denoised visibility as rm_rtsun_temporal writes it from the
 * accumulated one. The bindings are the sun encoder's: the history the
 * temporal kernel wrote at 8, the denoiser's view at 10.
 */
kernel void
rm_rtsun_denoise(texture2d<float, access::read> normal [[texture(1)]],
		texture2d<float, access::read> base [[texture(4)]],
		texture2d<float, access::write> light_out [[texture(6)]],
		texture2d<float, access::read> hist [[texture(8)]],
		constant mtl_rtd_view_t &dv [[buffer(10)]],
		uint2 px [[thread_position_in_grid]])
{
	const uint w = hist.get_width(), hh = hist.get_height();

	if ((px.x >= w) || (px.y >= hh))
	{
		return;
	}

	{
		const float4 hc = hist.read(px);
		const float4 nv = normal.read(px);

		if (!mtl_rtd_counted(hc.w) || !mtl_rtp_above_half(nv.a))
		{
			return;
		}

		{
			const uint m = (uint)hc.z;
			const float vis = rm_rtd_filter(normal, hist, px, w, hh, hc);
			const float3 n = normalize(nv.xyz * 2.0f - 1.0f);
			const float nl = mtl_rtsun_ndotl(mtl_rtao_v3(n.x, n.y, n.z), mtl_rtao_v3(dv.dir[0], dv.dir[1], dv.dir[2]));
			const float4 b = base.read(px);
			mtl_rgba_t c;

			c.r = b.r;
			c.g = b.g;
			c.b = b.b;
			c.a = b.a;
			c = mtl_rtsun_light(c, m, (vis > 0.0f) ? MTL_RTSUN_C_LIT : MTL_RTSUN_C_SHADOW, vis, nl, dv.sun[0], dv.sun[1],
					dv.sun[2]);
			light_out.write(float4(c.r, c.g, c.b, c.a), px);
		}
	}
}
