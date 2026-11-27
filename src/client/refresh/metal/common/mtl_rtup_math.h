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
 * The joint bilateral upsample of a ray-traced effect traced below the
 * size the 3D view is drawn at (option E, RT13, TASK126 slice 1;
 * docs/plans/metal-raytracing-design.md, section 2, "Option E, from first
 * principles"): the effect's scale as a divisor, the traced size and the
 * pixel each traced pixel takes its G-buffer sample from, and the weights
 * of the four traced pixels about a drawn pixel. Each weight is the
 * bilinear one times RT7's depth and normal guides (common/mtl_rtd_math.h),
 * and 0 for a traced pixel of another material or with no sample. Where no
 * traced pixel matches (thin geometry, a silhouette), the one nearest in
 * depth is taken, and the caller counts it for r_rt_stats 1. The block
 * layout is r_rt_ao_scale's (TASK157, common/mtl_rtao_math.h), which this
 * reuses. Slice 2 adds RT7's reprojection at the effect size
 * (mtl_rtup_reproject). Plain C99 and Metal Shading Language alike, as
 * common/mtl_rtd_math.h; the host test is tests/metal/rm_rtup_test.c.
 *
 * =======================================================================
 */

#ifndef MTL_RTUP_MATH_H
#define MTL_RTUP_MATH_H

#include "mtl_rtd_math.h"
#include "mtl_rtt_math.h"

/* the four traced pixels about a drawn pixel: (lo0x, lo0y), (lo1x, lo0y), (lo0x, lo1y), (lo1x, lo1y) */
#define MTL_RTUP_TAPS 4

/*
 * An effect scale as its divisor: the nearest of 1, 2 and MTL_RTAO_DIV_MAX
 * (3) to 1 / v, as rm_rtao_div rounds r_rt_ao_scale (1.5 goes up to 2,
 * 2.5 down to 2). 0, a negative value or one that is not finite is 1, the
 * effect at the size the view is drawn at: every effect scale's design
 * default.
 */
static inline unsigned
mtl_rtup_div(float v)
{
	float d;

	/* written so that a NaN fails */
	if (!(v > 0.0f) || !isfinite(v))
	{
		return 1u;
	}

	d = 1.0f / v;

	if (!(d < 1.5f))
	{
		return (d <= 2.5f) ? 2u : (unsigned)MTL_RTAO_DIV_MAX;
	}

	return 1u;
}

/* the traced size of one side of full pixels at divisor d: full itself at d 1, at least 1 */
static inline unsigned
mtl_rtup_size(unsigned full, unsigned d)
{
	return mtl_rtao_lo_size(full, d);
}

/* the full pixel whose G-buffer sample traced pixel lo takes, along one side: lo itself at d 1 */
static inline unsigned
mtl_rtup_src(unsigned lo, unsigned d, unsigned full)
{
	return mtl_rtao_lo_src(lo, d, full);
}

/*
 * What the weights need of a drawn pixel and its four traced pixels: the
 * pixel's distance from the eye d, unit normal n and material id m; each
 * traced pixel's bilinear weight b (mtl_rtup_bilinear), distance dn,
 * normal nn, material mn, and ok 1 where it holds a traced sample.
 */
typedef struct
{
	float d;
	mtl_rtao_v3_t n;
	unsigned m;
	float b[MTL_RTUP_TAPS];
	float dn[MTL_RTUP_TAPS];
	mtl_rtao_v3_t nn[MTL_RTUP_TAPS];
	unsigned mn[MTL_RTUP_TAPS];
	int ok[MTL_RTUP_TAPS];
} mtl_rtup_in_t;

/*
 * The weights: w the four taps' weights, summing to 1, or all 0 where no
 * tap holds a sample (empty 1: the caller writes the effect's neutral
 * value); fallback 1 where no tap matched the pixel and the one nearest in
 * depth carries the whole weight.
 */
typedef struct
{
	float w[MTL_RTUP_TAPS];
	int fallback;
	int empty;
} mtl_rtup_out_t;

/* in with the four taps' bilinear weights from the two axes' pairs (mtl_rtao_up_pair), in the taps' order */
static inline mtl_rtup_in_t
mtl_rtup_bilinear(mtl_rtup_in_t in, mtl_rtao_pair_t qx, mtl_rtao_pair_t qy)
{
	in.b[0] = (1.0f - qx.f) * (1.0f - qy.f);
	in.b[1] = qx.f * (1.0f - qy.f);
	in.b[2] = (1.0f - qx.f) * qy.f;
	in.b[3] = qx.f * qy.f;
	return in;
}

/*
 * The weight of tap i: its bilinear weight times RT7's depth guide
 * (mtl_rtd_depth_w, 0 across a depth step) and normal guide
 * (mtl_rtd_normal_w, 0 across a crease); 0 for a tap of another material
 * or with no sample.
 */
static inline float
mtl_rtup_tap_w(mtl_rtup_in_t in, int i)
{
	float w;

	if (!in.ok[i] || (in.mn[i] != in.m) || !(in.b[i] > 0.0f))
	{
		return 0.0f;
	}

	w = in.b[i] * mtl_rtd_depth_w(in.d, in.dn[i]) * mtl_rtd_normal_w(in.n, in.nn[i]);

	/* written so that a NaN fails */
	return (w > 0.0f) ? w : 0.0f;
}

/*
 * The weights of a drawn pixel at divisor d. At d 1 the effect was traced
 * at the drawn size, so the pixel takes its own sample, tap 0, whole: the
 * identity, and no fallback. Below that, the taps' weights over their sum;
 * where they sum to 0, the tap with a sample nearest the pixel in depth
 * (the first of equals), counted as a fallback; where no tap has a sample,
 * empty. A pixel whose own depth is not finite is empty as well, whatever
 * its taps hold, the effect's neutral value being the safe result for it.
 */
static inline mtl_rtup_out_t
mtl_rtup_weights(mtl_rtup_in_t in, unsigned d)
{
	mtl_rtup_out_t r;
	float ws = 0.0f, best = 0.0f;
	int i, k = -1;

	for (i = 0; i < MTL_RTUP_TAPS; i++)
	{
		r.w[i] = 0.0f;
	}

	r.fallback = 0;
	r.empty = 0;

	if (d <= 1u)
	{
		r.w[0] = 1.0f;
		return r;
	}

	for (i = 0; i < MTL_RTUP_TAPS; i++)
	{
		r.w[i] = mtl_rtup_tap_w(in, i);
		ws += r.w[i];
	}

	if (ws > 0.0f)
	{
		for (i = 0; i < MTL_RTUP_TAPS; i++)
		{
			r.w[i] /= ws;
		}

		return r;
	}

	for (i = 0; i < MTL_RTUP_TAPS; i++)
	{
		const float e = (in.dn[i] > in.d) ? in.dn[i] - in.d : in.d - in.dn[i];

		if (in.ok[i] && isfinite(e) && ((k < 0) || (e < best)))
		{
			k = i;
			best = e;
		}
	}

	if (k < 0)
	{
		r.empty = 1;
		return r;
	}

	r.w[k] = 1.0f;
	r.fallback = 1;
	return r;
}

/*
 * One channel upsampled: the taps' values v0 to v3 under the weights (a
 * tap of weight 0 adds nothing, so its value may be anything), or neutral
 * where the weights are empty.
 */
static inline float
mtl_rtup_value(mtl_rtup_out_t o, float v0, float v1, float v2, float v3, float neutral)
{
	float s = 0.0f;

	if (o.empty)
	{
		return neutral;
	}

	s += (o.w[0] > 0.0f) ? o.w[0] * v0 : 0.0f;
	s += (o.w[1] > 0.0f) ? o.w[1] * v1 : 0.0f;
	s += (o.w[2] > 0.0f) ? o.w[2] * v2 : 0.0f;
	s += (o.w[3] > 0.0f) ? o.w[3] * v3 : 0.0f;
	return s;
}

/*
 * RT7's reprojection at the effect size (slice 2): traced pixel (x, y) of
 * a w x h traced size at divisor d, with the render-size motion vector
 * (mx, my) read at its source pixel (mtl_rtup_src). The vector over d is
 * the move in traced pixels: the block layout puts traced pixel x's
 * centre at x d + o + 1/2 render pixels, so the traced pixel whose source
 * is nearest the point the vector names is the one holding x + 1/2 +
 * mx / d. At d 1, mtl_rtt_reproject itself.
 */
static inline mtl_rtt_px_t
mtl_rtup_reproject(unsigned x, unsigned y, float mx, float my, unsigned d, unsigned w, unsigned h)
{
	const float k = (d > 1u) ? (float)d : 1.0f;

	return mtl_rtt_reproject(x, y, mx / k, my / k, w, h);
}

/*
 * One light's visibility at a drawn pixel (slice 4, the dlight shadows):
 * vis, and fallback 1 where no traced pixel gave it.
 */
typedef struct
{
	float vis;
	int fallback;
} mtl_rtup_light_t;

/*
 * The visibility of one light under the pixel's weights o. Each traced
 * pixel traces only the lights of its own packed set, so tap i weighs for
 * this light only when its set holds it (h0 to h3), and its visibility of
 * it is v0 to v3 (unread otherwise). The weights of the taps that hold it
 * are taken over their own sum. A light that no weighted tap traced, the
 * weights being empty or every holder's weight 0, takes visibility 1: the
 * light unshadowed, the stock dlight, which the caller counts as a
 * fallback.
 */
static inline mtl_rtup_light_t
mtl_rtup_light(mtl_rtup_out_t o, int h0, int h1, int h2, int h3, float v0, float v1, float v2, float v3)
{
	mtl_rtup_light_t r;
	float ws = 0.0f, s = 0.0f;

	if (!o.empty)
	{
		if (h0 && (o.w[0] > 0.0f))
		{
			ws += o.w[0];
			s += o.w[0] * v0;
		}

		if (h1 && (o.w[1] > 0.0f))
		{
			ws += o.w[1];
			s += o.w[1] * v1;
		}

		if (h2 && (o.w[2] > 0.0f))
		{
			ws += o.w[2];
			s += o.w[2] * v2;
		}

		if (h3 && (o.w[3] > 0.0f))
		{
			ws += o.w[3];
			s += o.w[3] * v3;
		}
	}

	/* written so that a NaN fails */
	if (!(ws > 0.0f))
	{
		r.vis = 1.0f;
		r.fallback = 1;
		return r;
	}

	r.vis = s / ws;
	r.fallback = 0;
	return r;
}

#endif
