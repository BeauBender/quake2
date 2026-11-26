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
 * Host test of RT-primary's temporal accumulation (TASK190 slice 1, PM8):
 * the per-pixel rule of common/mtl_rtp_math.h, which rm_rtptemporal.metal
 * calls: the blend of each of red, green and blue with its own history,
 * the rejections (the view's reset, off the screen, the depth, the
 * material, a history that is not a light), the count and its saturation,
 * NaN in the trace's light and in the history, and the pass-through of
 * every pixel that is not a traced world pixel. Built and run by
 * tests/check-ref-metal.sh, which also builds it against mutants of the
 * header that it must fail.
 *
 * =======================================================================
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../../src/client/refresh/metal/common/mtl_rtp_math.h"

static int checks, failures;

#define CHECK(cond) \
	do { \
		checks++; \
		if (!(cond)) \
		{ \
			failures++; \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		} \
	} while (0)

static int
near(double a, double b, double eps)
{
	return fabs(a - b) <= eps;
}

static mtl_rtp_rgba_t
rgba(float r, float g, float b, float a)
{
	mtl_rtp_rgba_t c;

	c.r = r;
	c.g = g;
	c.b = b;
	c.a = a;
	return c;
}

/* the same bits */
static int
same_bits(mtl_rtp_rgba_t a, mtl_rtp_rgba_t b)
{
	return !memcmp(&a, &b, sizeof(a));
}

/* a kept history: on the screen, the same depth and material, a count */
static unsigned
kept(mtl_rtp_rgba_t h)
{
	return mtl_rtp_reject(0u, 1, 100.0f, 100.0f, 0.0f, MTL_GB_MAT_WORLD, MTL_GB_MAT_WORLD, h);
}

static void
test_world(void)
{
	CHECK(mtl_rtp_world(MTL_GB_MAT_WORLD, 0.5f, 1.0f));
	CHECK(!mtl_rtp_world(MTL_GB_MAT_NONE, 0.5f, 1.0f));
	CHECK(!mtl_rtp_world(MTL_GB_MAT_ALIAS, 0.5f, 1.0f));
	CHECK(!mtl_rtp_world(MTL_GB_MAT_WATER, 0.5f, 1.0f));
	CHECK(!mtl_rtp_world(MTL_GB_MAT_SKY, 0.5f, 1.0f));
	CHECK(!mtl_rtp_world(MTL_GB_MAT_WORLD, 1.0f, 1.0f));     /* the far plane */
	CHECK(!mtl_rtp_world(MTL_GB_MAT_WORLD, 0.5f, 0.0f));     /* no normal written */
	CHECK(!mtl_rtp_world(MTL_GB_MAT_WORLD, NAN, 1.0f));
	CHECK(!mtl_rtp_world(MTL_GB_MAT_WORLD, 0.5f, NAN));
}

/* each channel blends with its own history, alpha capped by the count's mean */
static void
test_blend(void)
{
	const mtl_rtp_rgba_t cur = rgba(0.2f, 0.4f, 0.8f, 0.75f);
	const mtl_rtp_rgba_t h = rgba(0.6f, 0.1f, 0.3f, 100.0f);
	mtl_rtp_px_t p;

	CHECK(kept(h) == 0u);
	p = mtl_rtp_pixel(1, kept(h), cur, h, 0.9f);
	CHECK(near(p.out.r, 0.9 * 0.6 + 0.1 * 0.2, 1e-6));
	CHECK(near(p.out.g, 0.9 * 0.1 + 0.1 * 0.4, 1e-6));
	CHECK(near(p.out.b, 0.9 * 0.3 + 0.1 * 0.8, 1e-6));
	CHECK(p.out.a == 0.75f);                /* the trace's alpha, kept */
	CHECK((p.hist.r == p.out.r) && (p.hist.g == p.out.g) && (p.hist.b == p.out.b));
	CHECK(p.hist.a == 101.0f);

	/* a young history is the plain mean: one sample, weight 1/2 */
	p = mtl_rtp_pixel(1, 0u, cur, rgba(0.6f, 0.6f, 0.6f, 1.0f), 0.9f);
	CHECK(near(p.out.r, 0.4, 1e-6) && near(p.out.g, 0.5, 1e-6) && near(p.out.b, 0.7, 1e-6));
	CHECK(p.hist.a == 2.0f);

	/* distinct channels: a channel mixed with another's history shows */
	p = mtl_rtp_pixel(1, 0u, rgba(0.0f, 0.0f, 0.0f, 1.0f), rgba(1.0f, 0.5f, 0.25f, 3.0f), 0.5f);
	CHECK(near(p.out.r, 0.5, 1e-6) && near(p.out.g, 0.25, 1e-6) && near(p.out.b, 0.125, 1e-6));

	/* alpha 0 accumulates nothing: the trace's light, clamped */
	p = mtl_rtp_pixel(1, 0u, cur, h, 0.0f);
	CHECK((p.out.r == cur.r) && (p.out.g == cur.g) && (p.out.b == cur.b) && (p.out.a == cur.a));

	/* a steady light stays where it is */
	p = mtl_rtp_pixel(1, 0u, rgba(0.5f, 0.5f, 0.5f, 1.0f), rgba(0.5f, 0.5f, 0.5f, 50.0f), 0.98f);
	CHECK((p.out.r == 0.5f) && (p.out.g == 0.5f) && (p.out.b == 0.5f));
}

/* the rejections, each alone, and what a rejected pixel writes */
static void
test_reject(void)
{
	const mtl_rtp_rgba_t cur = rgba(0.2f, 0.4f, 0.8f, 1.0f);
	const mtl_rtp_rgba_t h = rgba(0.6f, 0.1f, 0.3f, 40.0f);
	const unsigned W = MTL_GB_MAT_WORLD;
	unsigned rej;
	mtl_rtp_px_t p;

	/* the view's reset */
	rej = mtl_rtp_reject(MTL_RTT_REJ_RESET, 1, 100.0f, 100.0f, 0.0f, W, W, h);
	CHECK(rej == MTL_RTT_REJ_RESET);
	rej = mtl_rtp_reject(5u, 1, 100.0f, 100.0f, 0.0f, W, W, h);
	CHECK(rej & MTL_RTT_REJ_RESET);
	p = mtl_rtp_pixel(1, rej, cur, h, 0.9f);
	CHECK((p.out.r == cur.r) && (p.out.g == cur.g) && (p.out.b == cur.b));
	CHECK(p.hist.a == 1.0f);

	/* off the screen in the history's frame */
	rej = mtl_rtp_reject(0u, 0, 100.0f, 100.0f, 0.0f, W, W, h);
	CHECK(rej == MTL_RTT_REJ_OFF);
	p = mtl_rtp_pixel(1, rej, cur, h, 0.9f);
	CHECK((p.out.r == cur.r) && (p.hist.a == 1.0f));

	/* the depth: within the tolerance kept, beyond it rejected, the eye's move widening it */
	CHECK(mtl_rtp_reject(0u, 1, 100.0f, 103.0f, 0.0f, W, W, h) == 0u);       /* 2 + 2 % of 100 = 4 */
	CHECK(mtl_rtp_reject(0u, 1, 100.0f, 105.0f, 0.0f, W, W, h) == MTL_RTT_REJ_DEPTH);
	CHECK(mtl_rtp_reject(0u, 1, 100.0f, 95.0f, 0.0f, W, W, h) == MTL_RTT_REJ_DEPTH);
	CHECK(mtl_rtp_reject(0u, 1, 100.0f, 105.0f, 2.0f, W, W, h) == 0u);
	CHECK(mtl_rtp_reject(0u, 1, 100.0f, NAN, 0.0f, W, W, h) == MTL_RTT_REJ_DEPTH);
	p = mtl_rtp_pixel(1, MTL_RTT_REJ_DEPTH, cur, h, 0.9f);
	CHECK((p.out.g == cur.g) && (p.hist.a == 1.0f));

	/* the material: a pixel that was not world in the history */
	CHECK(mtl_rtp_reject(0u, 1, 100.0f, 100.0f, 0.0f, W, MTL_GB_MAT_ALIAS, h) == MTL_RTT_REJ_MAT);
	CHECK(mtl_rtp_reject(0u, 1, 100.0f, 100.0f, 0.0f, W, MTL_GB_MAT_NONE, h) == MTL_RTT_REJ_MAT);
	p = mtl_rtp_pixel(1, MTL_RTT_REJ_MAT, cur, h, 0.9f);
	CHECK((p.out.b == cur.b) && (p.hist.a == 1.0f));

	/* an empty history (the count a pass-through pixel writes) */
	CHECK(mtl_rtp_reject(0u, 1, 100.0f, 100.0f, 0.0f, W, W, rgba(0.5f, 0.5f, 0.5f, 0.0f)) == MTL_RTT_REJ_EMPTY);

	/* no light-set reject: the rule never says MTL_RTT_REJ_LIGHTS */
	CHECK(!(mtl_rtp_reject(MTL_RTT_REJ_RESET, 1, 100.0f, 500.0f, 0.0f, W, MTL_GB_MAT_SKY, h) & MTL_RTT_REJ_LIGHTS));
	CHECK(mtl_rtp_reject(0u, 1, 100.0f, 100.0f, 0.0f, W, W, h) == 0u);
}

/* the count: one more a frame, at most MTL_RTT_COUNT_MAX, 1 after a rejection */
static void
test_count(void)
{
	const mtl_rtp_rgba_t cur = rgba(0.3f, 0.3f, 0.3f, 1.0f);
	mtl_rtp_rgba_t h = rgba(0.3f, 0.3f, 0.3f, 0.0f);
	mtl_rtp_px_t p;
	int i;

	/* from empty: 1, then one more each frame */
	p = mtl_rtp_pixel(1, kept(h), cur, h, 0.9f);
	CHECK(p.hist.a == 1.0f);

	for (i = 0; i < 400; i++)
	{
		h = p.hist;
		p = mtl_rtp_pixel(1, kept(h), cur, h, 0.9f);

		if (i == 0)
		{
			CHECK(p.hist.a == 2.0f);
		}

		if (i == 9)
		{
			CHECK(p.hist.a == 11.0f);
		}
	}

	CHECK(p.hist.a == MTL_RTT_COUNT_MAX);

	/* saturated: it stays */
	h = p.hist;
	p = mtl_rtp_pixel(1, kept(h), cur, h, 0.9f);
	CHECK(p.hist.a == MTL_RTT_COUNT_MAX);

	/* 255 is exact in RGBA16Float: every count up to it is an integer a half holds */
	CHECK(MTL_RTT_COUNT_MAX <= 2048.0f);
}

/* NaN in the trace's light (clamped to 0) and in the history (rejected) */
static void
test_nan(void)
{
	const mtl_rtp_rgba_t h = rgba(0.5f, 0.5f, 0.5f, 10.0f);
	mtl_rtp_rgba_t bad;
	unsigned rej;
	mtl_rtp_px_t p;

	/* the trace's light: never a NaN into the output or the history */
	p = mtl_rtp_pixel(1, kept(h), rgba(NAN, 0.2f, 0.2f, 1.0f), h, 0.5f);
	CHECK(near(p.out.r, 0.25, 1e-6));       /* 0.5 * 0.5 + 0.5 * 0 */
	CHECK(!isnan(p.hist.r));
	p = mtl_rtp_pixel(1, MTL_RTT_REJ_RESET, rgba(NAN, 2.0f, -1.0f, 1.0f), h, 0.5f);
	CHECK((p.out.r == 0.0f) && (p.out.g == 1.0f) && (p.out.b == 0.0f));

	/* the history's light: NaN, infinite, negative or above 1 in any channel is rejected */
	bad = h;
	bad.r = NAN;
	rej = kept(bad);
	CHECK(rej == MTL_RTP_REJ_BAD);
	p = mtl_rtp_pixel(1, rej, rgba(0.2f, 0.3f, 0.4f, 1.0f), bad, 0.9f);
	CHECK((p.out.r == 0.2f) && (p.out.g == 0.3f) && (p.out.b == 0.4f) && (p.hist.a == 1.0f));
	bad = h;
	bad.g = INFINITY;
	CHECK(kept(bad) == MTL_RTP_REJ_BAD);
	bad = h;
	bad.b = -0.5f;
	CHECK(kept(bad) == MTL_RTP_REJ_BAD);
	bad = h;
	bad.b = 1.5f;
	CHECK(kept(bad) == MTL_RTP_REJ_BAD);

	/* a NaN count is an empty history */
	bad = h;
	bad.a = NAN;
	CHECK(kept(bad) & MTL_RTT_REJ_EMPTY);
	p = mtl_rtp_pixel(1, kept(bad), rgba(0.2f, 0.3f, 0.4f, 1.0f), bad, 0.9f);
	CHECK((p.out.r == 0.2f) && (p.hist.a == 1.0f));

	/* TASK190 slice 2: an infinite count, or one past MTL_RTT_COUNT_MAX, is a bad history, never a NaN blend */
	bad = h;
	bad.a = INFINITY;
	CHECK(kept(bad) & MTL_RTP_REJ_BAD);
	p = mtl_rtp_pixel(1, kept(bad), rgba(0.2f, 0.3f, 0.4f, 1.0f), bad, 0.9f);
	CHECK((p.out.r == 0.2f) && (p.out.g == 0.3f) && (p.out.b == 0.4f) && (p.hist.a == 1.0f));
	bad.a = MTL_RTT_COUNT_MAX + 1.0f;
	CHECK(kept(bad) & MTL_RTP_REJ_BAD);
	bad.a = MTL_RTT_COUNT_MAX;
	CHECK(kept(bad) == 0u);
}

/* every pixel that is not a traced world pixel: its light bit for bit, a history with no sample */
static void
test_passthrough(void)
{
	const mtl_rtp_rgba_t h = rgba(0.5f, 0.5f, 0.5f, 10.0f);
	mtl_rtp_rgba_t cur = rgba(0.123f, 0.456f, 0.789f, 0.25f);
	mtl_rtp_px_t p;

	p = mtl_rtp_pixel(0, 0u, cur, h, 0.9f);
	CHECK(same_bits(p.out, cur));
	CHECK((p.hist.r == 0.0f) && (p.hist.g == 0.0f) && (p.hist.b == 0.0f) && (p.hist.a == 0.0f));

	/* even a value the blend would clamp, or a NaN, goes through unchanged */
	cur = rgba(NAN, 3.0f, -2.0f, NAN);
	p = mtl_rtp_pixel(0, MTL_RTT_REJ_RESET, cur, h, 0.9f);
	CHECK(same_bits(p.out, cur));

	/* and a world pixel that is not rejected is not passed through */
	p = mtl_rtp_pixel(1, 0u, rgba(0.0f, 0.0f, 0.0f, 0.25f), h, 0.9f);
	CHECK(p.out.r > 0.0f);
}

int
main(void)
{
	test_world();
	test_blend();
	test_reject();
	test_count();
	test_nan();
	test_passthrough();
	printf("rm_rtptemporal_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
