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
 * RT-primary's 5 x 5 filter of its accumulated light (TASK190 slice 2,
 * PM8; docs/plans/presentation-modes.md, section 4.3) that
 * rm_rtpdenoise.metal and the host test (tests/metal/rm_rtpdenoise_test.c)
 * share. The weights, the strength by the count and the sums are RT7's
 * (common/mtl_rtd_math.h, included unchanged), applied to each of red,
 * green and blue of the summed demodulated light (the card's default 3:
 * the sum, not each term). Plain C99 and Metal Shading Language alike, as
 * mtl_rtp_math.h; no pointer, so no address space.
 *
 * =======================================================================
 */

#ifndef MTL_RTPD_MATH_H
#define MTL_RTPD_MATH_H

#include "mtl_rtp_math.h"
#include "mtl_rtd_math.h"

/* the running sums of red, green and blue, each RT7's */
typedef struct
{
	mtl_rtd_acc_t r, g, b;
} mtl_rtpd_acc_t;

static inline mtl_rtpd_acc_t
mtl_rtpd_acc(void)
{
	mtl_rtpd_acc_t a;

	a.r = mtl_rtd_acc();
	a.g = mtl_rtd_acc();
	a.b = mtl_rtd_acc();
	return a;
}

/*
 * 1 when the filter writes the pixel: a world pixel (mtl_rtp_world) whose
 * history count n leaves the blur a strength above 0 (mtl_rtd_strength:
 * 1 to under 16 samples). Every other pixel keeps the accumulation's.
 */
static inline int
mtl_rtpd_on(int world, float n)
{
	return world && (mtl_rtd_strength(n) > 0.0f);
}

/*
 * The weight of the neighbour at (dx, dy): RT7's (mtl_rtd_weight) against
 * the pixel's distance d, normal n and material m, from the neighbour's
 * dn, nn, mn and count cn; 0 for a neighbour whose normal was not written
 * (nn_a, the G-buffer normal's alpha, 0.5 or below).
 */
static inline float
mtl_rtpd_weight(int dx, int dy, float d, float dn, mtl_rtao_v3_t n, mtl_rtao_v3_t nn, unsigned m, unsigned mn,
		float cn, float nn_a)
{
	/* written so that a NaN fails */
	if (!(nn_a > 0.5f))
	{
		return 0.0f;
	}

	return mtl_rtd_weight(dx, dy, d, dn, n, nn, m, mn, cn);
}

/* a's sums with the neighbour's light h at the weight w, each channel by mtl_rtd_add */
static inline mtl_rtpd_acc_t
mtl_rtpd_add(mtl_rtpd_acc_t a, float w, mtl_rtp_rgba_t h)
{
	a.r = mtl_rtd_add(a.r, w, h.r);
	a.g = mtl_rtd_add(a.g, w, h.g);
	a.b = mtl_rtd_add(a.b, w, h.b);
	return a;
}

/*
 * The filtered light of a pixel whose history is h (h.a its count): each
 * of red, green and blue moved toward its own weighted mean by the
 * strength of the count (mtl_rtd_result), clamped to [0, 1] with a NaN
 * as 0 (mtl_rtp_clamp, the light's format); a is the trace's alpha.
 */
static inline mtl_rtp_rgba_t
mtl_rtpd_result(mtl_rtpd_acc_t s, mtl_rtp_rgba_t h, float a)
{
	mtl_rtp_rgba_t r;

	r.r = mtl_rtp_clamp(mtl_rtd_result(s.r, h.r, h.a));
	r.g = mtl_rtp_clamp(mtl_rtd_result(s.g, h.g, h.a));
	r.b = mtl_rtp_clamp(mtl_rtd_result(s.b, h.b, h.a));
	r.a = a;
	return r;
}

#endif /* MTL_RTPD_MATH_H */
