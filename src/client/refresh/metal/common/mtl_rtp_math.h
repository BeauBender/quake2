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
 * The per-pixel arithmetic of RT-primary's temporal accumulation (TASK190
 * slice 1, PM8; docs/plans/presentation-modes.md, section 4.3) that
 * rm_rtptemporal.metal and the host test (tests/metal/rm_rtptemporal_test.c)
 * share. The light it blends is the direct-light kernel's output, the
 * demodulated irradiance (no albedo, clamped to [0, 1]); the reprojection,
 * the rejection rule, the count and the blend are RT7's
 * (common/mtl_rtt_math.h, included unchanged), with no light-set reject:
 * RT-primary's light is the sum of every light, so the set of lights that
 * reach a pixel is not one of its guides. Plain C99 and Metal Shading
 * Language alike, as mtl_rtt_math.h; no pointer, so no address space.
 *
 * =======================================================================
 */

#ifndef MTL_RTP_MATH_H
#define MTL_RTP_MATH_H

#include "mtl_rtt_math.h"
#include "mtl_shader_math.h"   /* mtl_gbuf_math.h's types */
#include "mtl_gbuf_math.h"

/* a reason of mtl_rtp_reject beside RT7's (MTL_RTT_REJ_*): the history's light is not a finite value in [0, 1], or its count is past MTL_RTT_COUNT_MAX */
#define MTL_RTP_REJ_BAD 64

/* one pixel's light, or a history's light with its count in a */
typedef struct
{
	float r, g, b, a;
} mtl_rtp_rgba_t;

/* the pixel's output (the light the resolve reads) and the history written for it */
typedef struct
{
	mtl_rtp_rgba_t out;
	mtl_rtp_rgba_t hist;
} mtl_rtp_px_t;

#define MTL_RTP_BITS_HALF 0x3f000000u   /* 0.5f */

/* 1 when f < 1 (every negative value, -inf and -0 included; not a NaN, not 1 or more, not +inf) */
static inline int
mtl_rtp_below1(float f)
{
	const unsigned u = MTL_RTT_BITS(f);

	return (u < MTL_RTT_BITS_1) || ((u >= MTL_RTT_SIGN) && (u <= (MTL_RTT_SIGN | MTL_RTT_BITS_INF)));
}

/* 1 when f > 0.5 (+inf included; not a NaN, not 0.5, not a negative value or -0) */
static inline int
mtl_rtp_above_half(float f)
{
	const unsigned u = MTL_RTT_BITS(f);

	return (u > MTL_RTP_BITS_HALF) && (u <= MTL_RTT_BITS_INF);
}

/*
 * 1 when the pixel is one the direct-light kernel traced: the G-buffer's
 * world material, a depth short of the far plane and a written normal
 * (rm_rtdir_trace's rule). Every other pixel's light is passed through.
 * FIX213: the depth and normal alpha tests are integer tests of the bit
 * pattern, as FIX212's guards (see MTL_RTT_BITS), so under the shaders'
 * fast math a NaN depth or normal alpha is still not a world pixel; on
 * every other input, -0 and the infinities included, the answer is the
 * float compares' (depth < 1, normal_a > 0.5).
 */
static inline int
mtl_rtp_world(unsigned mat, float depth, float normal_a)
{
	return (mat == MTL_GB_MAT_WORLD) && mtl_rtp_below1(depth) && mtl_rtp_above_half(normal_a);
}

/* 1 when v is a light the history can hold: finite and in [0, 1] (a NaN fails; an integer test of the pattern, see MTL_RTT_BITS) */
static inline int
mtl_rtp_light_ok(float v)
{
	return mtl_rtt_unit(v);
}

/* a light as the blend takes it: v in [0, 1], 0 for a NaN */
static inline float
mtl_rtp_clamp(float v)
{
	const unsigned u = MTL_RTT_BITS(v);

	/* a NaN, -inf and every negative value are 0; -0 stays -0, as (v >= 0) holds for it */
	if (!mtl_rtt_nonneg(v))
	{
		return 0.0f;
	}

	return ((u > MTL_RTT_BITS_1) && (u != MTL_RTT_SIGN)) ? 1.0f : v;
}

/*
 * The reasons (MTL_RTT_REJ_*, MTL_RTP_REJ_BAD) the history of a world
 * pixel is rejected; 0 when it is kept. reset: the view's; ok:
 * mtl_rtt_reproject's; dist and mat: this frame's distance from the eye
 * and material id; hdist, hmat and h: the history's at the reprojected
 * texel (h.a its count); move: the eye's move since the history's frame.
 * RT7's rule with the light sets equal (0, 0): no light-set reject.
 */
static inline unsigned
mtl_rtp_reject(unsigned reset, int ok, float dist, float hdist, float move, unsigned mat, unsigned hmat,
		mtl_rtp_rgba_t h)
{
	unsigned r = mtl_rtt_reject(reset, ok, dist, hdist, move, mat, hmat, 0u, 0u, h.a);

	/* TASK190 slice 2: a count past MTL_RTT_COUNT_MAX, infinite included, is a bad history too (no NaN blend) */
	if (ok && !(mtl_rtp_light_ok(h.r) && mtl_rtp_light_ok(h.g) && mtl_rtp_light_ok(h.b) && mtl_rtt_atmost_max(h.a)))
	{
		r |= MTL_RTP_REJ_BAD;
	}

	return r;
}

/*
 * One pixel: cur the trace's light, h the history's (h.a its count), rej
 * mtl_rtp_reject's reasons, alpha the view's (rm_rtt_alpha). A pixel that
 * is not a world one (world 0) is passed through bit for bit, and its
 * history holds no sample (count 0). A world pixel's red, green and blue
 * are each blended with the history's own (mtl_rtt_blend), its alpha kept
 * as the trace wrote it, and the history holds the blend with its count
 * (mtl_rtt_count).
 */
static inline mtl_rtp_px_t
mtl_rtp_pixel(int world, unsigned rej, mtl_rtp_rgba_t cur, mtl_rtp_rgba_t h, float alpha)
{
	mtl_rtp_px_t p;

	if (!world)
	{
		p.out = cur;
		p.hist.r = 0.0f;
		p.hist.g = 0.0f;
		p.hist.b = 0.0f;
		p.hist.a = 0.0f;
		return p;
	}

	p.out.r = mtl_rtt_blend(rej, mtl_rtp_clamp(cur.r), h.r, h.a, alpha);
	p.out.g = mtl_rtt_blend(rej, mtl_rtp_clamp(cur.g), h.g, h.a, alpha);
	p.out.b = mtl_rtt_blend(rej, mtl_rtp_clamp(cur.b), h.b, h.a, alpha);
	p.out.a = cur.a;
	p.hist.r = p.out.r;
	p.hist.g = p.out.g;
	p.hist.b = p.out.b;
	p.hist.a = mtl_rtt_count(rej, h.a);
	return p;
}

#endif /* MTL_RTP_MATH_H */
