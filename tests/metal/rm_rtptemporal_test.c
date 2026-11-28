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

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../../src/client/refresh/metal/common/mtl_rtp_math.h"
#include "rm_gbuf_edges.h"

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

/*
 * FIX212: the guards of the temporal history and the light clamp, against
 * the float compares they replace. The Metal shaders build with fast math,
 * which may fold "a NaN fails this compare" away, so the guards test the
 * float's bit pattern as an integer. For every input the result must be
 * the old compare's, bit for bit (-0 included: -0 >= 0 holds, so -0 is a
 * light, a count of -0 is empty, and clamp keeps -0). The host build has no
 * fast math (the NAN macros do not even compile under it), so these pass
 * on a header that still uses the compare; they pin what the integer form
 * must keep, and the Metal compile (check-ref-metal) pins the integer form.
 */
static int
ref_light_ok(float v)
{
	return (v >= 0.0f) && (v <= 1.0f);
}

static float
ref_clamp(float v)
{
	if (!(v >= 0.0f))
	{
		return 0.0f;
	}

	return (v > 1.0f) ? 1.0f : v;
}

static int
ref_depth_ok(float dist, float hdist, float move)
{
	const float d = dist - hdist;
	const float tol = move + MTL_RTT_DEPTH_ABS + MTL_RTT_DEPTH_REL * dist;

	return (d <= tol) && (-d <= tol);
}

static float
ref_count(unsigned rejected, float hcount)
{
	if (rejected || !(hcount >= 1.0f))
	{
		return 1.0f;
	}

	return (hcount + 1.0f < MTL_RTT_COUNT_MAX) ? hcount + 1.0f : MTL_RTT_COUNT_MAX;
}

static float
ref_blend(unsigned rejected, float cur, float hist, float hcount, float alpha)
{
	float w;

	if (rejected || !(hcount >= 1.0f) || !(alpha > 0.0f))
	{
		return cur;
	}

	w = hcount / (hcount + 1.0f);
	w = (alpha < w) ? alpha : w;
	return w * hist + (1.0f - w) * cur;
}

static mtl_rtt_px_t
ref_reproject(unsigned x, unsigned y, float mx, float my, unsigned w, unsigned h)
{
	mtl_rtt_px_t r;
	const float hx = (float)x + 0.5f + mx, hy = (float)y + 0.5f + my;

	r.ok = 0;
	r.x = 0;
	r.y = 0;

	if (!((hx >= 0.0f) && (hy >= 0.0f) && (hx < (float)w) && (hy < (float)h)))
	{
		return r;
	}

	r.ok = 1;
	r.x = (unsigned)hx;
	r.y = (unsigned)hy;
	return r;
}

/* the reject's bad-history rule as it was written */
static unsigned
ref_bad(int ok, mtl_rtp_rgba_t h)
{
	if (ok && !(ref_light_ok(h.r) && ref_light_ok(h.g) && ref_light_ok(h.b) && (h.a <= MTL_RTT_COUNT_MAX)))
	{
		return MTL_RTP_REJ_BAD;
	}

	return 0u;
}

static float
from_bits(unsigned u)
{
	float f;

	memcpy(&f, &u, sizeof(f));
	return f;
}

static unsigned
to_bits(float f)
{
	unsigned u;

	memcpy(&u, &f, sizeof(u));
	return u;
}

/* the edges: 0, -0, 1 and its neighbours, denormals, FLT_MAX, the count's limit, infinities, NaNs, and a few between */
static const unsigned edge_bits[] = {
	0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u, 0x007fffffu, 0x807fffffu, 0x00800000u, 0x80800000u,
	0x3f7fffffu, 0x3f800000u, 0x3f800001u, 0xbf7fffffu, 0xbf800000u, 0xbf800001u,
	0x3f000000u, 0xbf000000u, 0x437effffu, 0x437f0000u, 0x437f0001u, 0xc37f0000u, 0x43800000u,
	0x3c23d70au, 0x40000000u, 0x42c80000u, 0x7f7fffffu, 0xff7fffffu,
	0x7f800000u, 0xff800000u, 0x7f800001u, 0x7fc00000u, 0xffc00000u, 0x7fffffffu, 0xffffffffu, 0xff800001u
};
#define NEDGE ((int)(sizeof(edge_bits) / sizeof(edge_bits[0])))

/* the per-value guards over the edges, then every pattern at a stride that visits ~1M of the 2^32 */
static void
test_guards_sweep(void)
{
	unsigned u;
	int i, bad = 0;

	for (i = 0, u = 0u; i < NEDGE + 1048576; i++, u += 4099u)
	{
		const float v = (i < NEDGE) ? from_bits(edge_bits[i]) : from_bits(u);
		mtl_rtp_rgba_t h;

		if (mtl_rtp_light_ok(v) != ref_light_ok(v) ||
				to_bits(mtl_rtp_clamp(v)) != to_bits(ref_clamp(v)))
		{
			bad++;
		}

		/* a count: the empty test, the count and the blend it steers; an alpha likewise */
		if (to_bits(mtl_rtt_count(0u, v)) != to_bits(ref_count(0u, v)) ||
				to_bits(mtl_rtt_blend(0u, 0.25f, 0.75f, v, 0.9f)) != to_bits(ref_blend(0u, 0.25f, 0.75f, v, 0.9f)) ||
				to_bits(mtl_rtt_blend(0u, 0.25f, 0.75f, 10.0f, v)) != to_bits(ref_blend(0u, 0.25f, 0.75f, 10.0f, v)) ||
				((mtl_rtt_reject(0u, 1, 1.0f, 1.0f, 0.0f, 0u, 0u, 0u, 0u, v) & MTL_RTT_REJ_EMPTY) != 0u) != !(v >= 1.0f))
		{
			bad++;
		}

		/* the bad-history rule: the count in a, then the value in a colour channel */
		h = rgba(0.5f, 0.5f, 0.5f, v);

		if ((mtl_rtp_reject(0u, 1, 1.0f, 1.0f, 0.0f, 0u, 0u, h) & MTL_RTP_REJ_BAD) != ref_bad(1, h))
		{
			bad++;
		}

		h = rgba(v, 0.5f, 0.5f, 10.0f);

		if (mtl_rtp_reject(0u, 1, 1.0f, 1.0f, 0.0f, 0u, 0u, h) != ref_bad(1, h))
		{
			bad++;
		}
	}

	CHECK(bad == 0);
	CHECK(mtl_rtp_light_ok(-0.0f));         /* -0 is a light, as -0 >= 0 */
	CHECK(to_bits(mtl_rtp_clamp(-0.0f)) == 0x80000000u);
	CHECK(!mtl_rtp_light_ok(NAN) && !mtl_rtp_light_ok(INFINITY) && !mtl_rtp_light_ok(-INFINITY) && !mtl_rtp_light_ok(-0.25f));
	CHECK((mtl_rtp_clamp(NAN) == 0.0f) && (mtl_rtp_clamp(-INFINITY) == 0.0f) && (mtl_rtp_clamp(-3.0f) == 0.0f));
	CHECK((mtl_rtp_clamp(INFINITY) == 1.0f) && (mtl_rtp_clamp(FLT_MAX) == 1.0f));
	CHECK((mtl_rtp_clamp(1.0f) == 1.0f) && (mtl_rtp_clamp(from_bits(0x3f7fffffu)) == from_bits(0x3f7fffffu)));
	CHECK(mtl_rtp_clamp(from_bits(0x3f800001u)) == 1.0f);
	CHECK(mtl_rtp_clamp(from_bits(0x00000001u)) == from_bits(0x00000001u));     /* the smallest denormal stays */
}

/* the depth rule over the edge triples, a pattern walk of the three arguments, and the tolerance's own edge */
static void
test_depth_sweep(void)
{
	unsigned s = 12345u;
	int i, j, k, bad = 0;

	for (i = 0; i < NEDGE; i++)
	{
		for (j = 0; j < NEDGE; j++)
		{
			for (k = 0; k < NEDGE; k++)
			{
				const float a = from_bits(edge_bits[i]), b = from_bits(edge_bits[j]), c = from_bits(edge_bits[k]);

				if (mtl_rtt_depth_ok(a, b, c) != ref_depth_ok(a, b, c))
				{
					bad++;
				}
			}
		}
	}

	for (i = 0; i < 400000; i++)
	{
		float a, b, c;

		s = s * 1664525u + 1013904223u;
		a = from_bits(s);
		s = s * 1664525u + 1013904223u;
		b = (i & 1) ? from_bits(s) : a + from_bits(s & 0x3fffffffu) * 1e-9f;
		s = s * 1664525u + 1013904223u;
		c = from_bits(s);

		if (mtl_rtt_depth_ok(a, b, c) != ref_depth_ok(a, b, c))
		{
			bad++;
		}
	}

	/* dist 100, move 0: the tolerance is 2 + 0.02 * 100; hdist at its edge and a few ulps either side */
	for (i = -3; i <= 3; i++)
	{
		const float tol = MTL_RTT_DEPTH_ABS + MTL_RTT_DEPTH_REL * 100.0f;
		const float hd = 100.0f - from_bits(to_bits(tol) + (unsigned)(i + 3));

		if (mtl_rtt_depth_ok(100.0f, hd, 0.0f) != ref_depth_ok(100.0f, hd, 0.0f))
		{
			bad++;
		}
	}

	CHECK(bad == 0);
	CHECK(mtl_rtt_depth_ok(100.0f, 100.0f, 0.0f));
	CHECK(mtl_rtt_depth_ok(0.0f, -0.0f, -0.0f) == ref_depth_ok(0.0f, -0.0f, -0.0f));
	CHECK(!mtl_rtt_depth_ok(NAN, 100.0f, 0.0f) && !mtl_rtt_depth_ok(100.0f, NAN, 0.0f) && !mtl_rtt_depth_ok(100.0f, 100.0f, NAN));
	CHECK(!mtl_rtt_depth_ok(INFINITY, INFINITY, 0.0f));         /* inf - inf is a NaN */
	CHECK(!mtl_rtt_depth_ok(100.0f, INFINITY, 0.0f) && !mtl_rtt_depth_ok(100.0f, -INFINITY, 0.0f));
	CHECK(!mtl_rtt_depth_ok(100.0f, 100.0f, -INFINITY) && !mtl_rtt_depth_ok(100.0f, 100.0f, -1000.0f));
}

/* the reprojection over the edge motions and a pattern walk, with the screen sizes 0, 1 and a few real ones */
static void
test_reproject_sweep(void)
{
	static const unsigned sizes[] = { 0u, 1u, 7u, 640u, 2560u, 16777216u };
	unsigned s = 777u;
	unsigned z;
	int i, j, bad = 0;

	for (i = 0; i < NEDGE; i++)
	{
		for (j = 0; j < NEDGE; j++)
		{
			for (z = 0; z < sizeof(sizes) / sizeof(sizes[0]); z++)
			{
				const mtl_rtt_px_t a = mtl_rtt_reproject(3u, 0u, from_bits(edge_bits[i]), from_bits(edge_bits[j]), sizes[z], sizes[z]);
				const mtl_rtt_px_t b = ref_reproject(3u, 0u, from_bits(edge_bits[i]), from_bits(edge_bits[j]), sizes[z], sizes[z]);

				if (a.ok != b.ok || a.x != b.x || a.y != b.y)
				{
					bad++;
				}
			}
		}
	}

	for (i = 0; i < 400000; i++)
	{
		const unsigned w = sizes[i % 6];
		mtl_rtt_px_t a, b;
		float mx, my;

		s = s * 1664525u + 1013904223u;
		mx = (i & 1) ? from_bits(s) : (float)(int)(s >> 16) / 64.0f - 512.0f;
		s = s * 1664525u + 1013904223u;
		my = (i & 2) ? from_bits(s) : (float)(int)(s >> 16) / 64.0f - 512.0f;
		a = mtl_rtt_reproject(s & 1023u, (s >> 10) & 1023u, mx, my, w, w);
		b = ref_reproject(s & 1023u, (s >> 10) & 1023u, mx, my, w, w);

		if (a.ok != b.ok || a.x != b.x || a.y != b.y)
		{
			bad++;
		}
	}

	CHECK(bad == 0);

	/* the texel edges: 0 is in, the size is out, the last texel's far edge just under it, NaN and the infinities off */
	CHECK(mtl_rtt_reproject(0u, 0u, -0.5f, -0.5f, 8u, 8u).ok == 1);
	CHECK(mtl_rtt_reproject(0u, 0u, -0.5f - 1e-3f, 0.0f, 8u, 8u).ok == 0);
	CHECK(mtl_rtt_reproject(7u, 7u, 0.0f, 0.0f, 8u, 8u).ok == 1);
	CHECK(mtl_rtt_reproject(7u, 7u, 0.5f, 0.0f, 8u, 8u).ok == 0);
	CHECK(mtl_rtt_reproject(3u, 3u, NAN, 0.0f, 8u, 8u).ok == 0);
	CHECK(mtl_rtt_reproject(3u, 3u, 0.0f, NAN, 8u, 8u).ok == 0);
	CHECK(mtl_rtt_reproject(3u, 3u, INFINITY, 0.0f, 8u, 8u).ok == 0);
	CHECK(mtl_rtt_reproject(3u, 3u, 0.0f, -INFINITY, 8u, 8u).ok == 0);
	CHECK(mtl_rtt_reproject(0u, 0u, 0.0f, 0.0f, 0u, 8u).ok == 0);
}

/* the count, the blend and the bad-history rule on NaN, +inf, -inf and negative inputs, each named */
static void
test_history_guards(void)
{
	static const float badv[] = { NAN, -INFINITY, -1.0f, -0.0f, 0.0f, 0.5f };
	unsigned i;

	/* a count that is not 1 or more (NaN, -inf, negative, 0, -0, under 1) restarts at 1 and is not blended */
	for (i = 0; i < sizeof(badv) / sizeof(badv[0]); i++)
	{
		CHECK(mtl_rtt_count(0u, badv[i]) == 1.0f);
		CHECK(mtl_rtt_blend(0u, 0.25f, 0.75f, badv[i], 0.9f) == 0.25f);
		CHECK(mtl_rtt_reject(0u, 1, 1.0f, 1.0f, 0.0f, 0u, 0u, 0u, 0u, badv[i]) & MTL_RTT_REJ_EMPTY);
	}

	/* an alpha that is NaN, -inf, negative, 0 or -0 takes the trace's light */
	CHECK(mtl_rtt_blend(0u, 0.25f, 0.75f, 10.0f, NAN) == 0.25f);
	CHECK(mtl_rtt_blend(0u, 0.25f, 0.75f, 10.0f, -INFINITY) == 0.25f);
	CHECK(mtl_rtt_blend(0u, 0.25f, 0.75f, 10.0f, -0.5f) == 0.25f);
	CHECK(mtl_rtt_blend(0u, 0.25f, 0.75f, 10.0f, 0.0f) == 0.25f);
	CHECK(mtl_rtt_blend(0u, 0.25f, 0.75f, 10.0f, -0.0f) == 0.25f);

	/* +inf count: it saturates, never a NaN, and the bad-history rule refuses it */
	CHECK(mtl_rtt_count(0u, INFINITY) == MTL_RTT_COUNT_MAX);
	CHECK(mtl_rtp_reject(0u, 1, 1.0f, 1.0f, 0.0f, 0u, 0u, rgba(0.5f, 0.5f, 0.5f, INFINITY)) & MTL_RTP_REJ_BAD);
	CHECK(mtl_rtp_reject(0u, 1, 1.0f, 1.0f, 0.0f, 0u, 0u, rgba(0.5f, 0.5f, 0.5f, NAN)) & MTL_RTP_REJ_BAD);
	CHECK(mtl_rtp_reject(0u, 1, 1.0f, 1.0f, 0.0f, 0u, 0u, rgba(0.5f, 0.5f, 0.5f, -INFINITY)) == MTL_RTT_REJ_EMPTY);
	CHECK(mtl_rtp_reject(0u, 1, 1.0f, 1.0f, 0.0f, 0u, 0u, rgba(0.5f, 0.5f, 0.5f, -1.0f)) == MTL_RTT_REJ_EMPTY);
	CHECK(mtl_rtp_reject(0u, 1, 1.0f, 1.0f, 0.0f, 0u, 0u, rgba(0.5f, 0.5f, 0.5f, 1.0f)) == 0u);
	CHECK(mtl_rtp_reject(0u, 1, 1.0f, 1.0f, 0.0f, 0u, 0u, rgba(0.5f, -INFINITY, 0.5f, 5.0f)) == MTL_RTP_REJ_BAD);
	CHECK(mtl_rtp_reject(0u, 1, 1.0f, 1.0f, 0.0f, 0u, 0u, rgba(0.5f, 0.5f, from_bits(0x7fc00001u), 5.0f)) == MTL_RTP_REJ_BAD);
}

/*
 * FIX213: the world-pixel test (mtl_rtp_world) under fast math, as FIX212's
 * guards: its depth and normal alpha tests are integer tests of the bit
 * pattern (mtl_rtp_below1, mtl_rtp_above_half). Against the float compares
 * they replace, compiled here without fast math (refused below if it is
 * on), on every input that is not a NaN, ±inf included; a NaN depth or
 * normal alpha is not a world pixel.
 */
#if defined(__FAST_MATH__) || (defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__)
#error "the reference compares of the world-pixel test need IEEE compares; build without fast math"
#endif

static int
ref_world(unsigned mat, float depth, float normal_a)
{
	return (mat == MTL_GB_MAT_WORLD) && (depth < 1.0f) && (normal_a > 0.5f);
}

/* 1 when the pattern is a NaN, either sign */
static int
is_nan_bits(unsigned u)
{
	return (u & ~MTL_RTT_SIGN) > MTL_RTT_BITS_INF;
}

/* an edge: its pattern, its name and the verdict wanted (1 a world pixel) */
typedef struct
{
	unsigned bits;
	const char *name;
	int want;
} rtw_edge_t;

/* the depth's edges, with a written normal (1.0) and the world material */
static const rtw_edge_t rtw_depth[] = {
	{ 0x80000000u, "-0", 1 },
	{ 0x00000000u, "+0", 1 },
	{ 0x3f7fffffu, "largest below 1", 1 },
	{ 0x3f800000u, "1", 0 },
	{ 0x3f800001u, "smallest above 1", 0 },
	{ 0x80000001u, "negative subnormal", 1 },
	{ 0x7f800000u, "+inf", 0 },
	{ 0xff800000u, "-inf", 1 },
	{ 0x7fc00000u, "quiet NaN", 0 },
	{ 0x7f800001u, "signalling NaN", 0 },
	{ 0xffc00000u, "negative NaN", 0 },
	{ 0xff800001u, "smallest negative NaN", 0 },
	{ 0xffffffffu, "largest negative NaN", 0 },
	{ 0x7fffffffu, "largest positive NaN", 0 },
};
#define RTW_NDEPTH ((int)(sizeof(rtw_depth) / sizeof(rtw_depth[0])))

/* the normal alpha's edges, at a depth of 0.5 and the world material */
static const rtw_edge_t rtw_normal[] = {
	{ 0x3f000000u, "0.5", 0 },
	{ 0x3f000001u, "next above 0.5", 1 },
	{ 0x3effffffu, "next below 0.5", 0 },
	{ 0x80000000u, "-0", 0 },
	{ 0x7f800000u, "+inf", 1 },
	{ 0xff800000u, "-inf", 0 },
	{ 0x7fc00000u, "quiet NaN", 0 },
	{ 0x7f800001u, "signalling NaN", 0 },
	{ 0xffc00000u, "negative NaN", 0 },
	{ 0xff800001u, "smallest negative NaN", 0 },
	{ 0xffffffffu, "largest negative NaN", 0 },
	{ 0x7fffffffu, "largest positive NaN", 0 },
};
#define RTW_NNORMAL ((int)(sizeof(rtw_normal) / sizeof(rtw_normal[0])))

/* one input: the verdict and the helper's, against the old compare unless a NaN is in it */
static int
rtw_one(unsigned mat, unsigned d, unsigned n)
{
	const float df = from_bits(d), nf = from_bits(n);
	const int got = mtl_rtp_world(mat, df, nf);

	if (got != (mtl_rtp_below1(df) && mtl_rtp_above_half(nf) && (mat == MTL_GB_MAT_WORLD)))
	{
		return 0;
	}

	if (is_nan_bits(d) || is_nan_bits(n))
	{
		/* a NaN is refused, by its own helper */
		return (got == 0) && (!is_nan_bits(d) || !mtl_rtp_below1(df)) && (!is_nan_bits(n) || !mtl_rtp_above_half(nf));
	}

	return (got == ref_world(mat, df, nf)) && (mtl_rtp_below1(df) == (df < 1.0f)) &&
		(mtl_rtp_above_half(nf) == (nf > 0.5f));
}

static void
test_world_edges(void)
{
	static const unsigned mats[] = { MTL_GB_MAT_NONE, MTL_GB_MAT_WORLD, MTL_GB_MAT_ALIAS, MTL_GB_MAT_WATER,
		MTL_GB_MAT_SKY };
	unsigned i;
	int j;

	for (j = 0; j < RTW_NDEPTH; j++)
	{
		const int got = mtl_rtp_world(MTL_GB_MAT_WORLD, from_bits(rtw_depth[j].bits), 1.0f);

		printf("rtw: depth %-20s 0x%08x: %d (want %d)\n", rtw_depth[j].name, rtw_depth[j].bits, got, rtw_depth[j].want);
		CHECK(got == rtw_depth[j].want);
		CHECK(rtw_one(MTL_GB_MAT_WORLD, rtw_depth[j].bits, 0x3f800000u));
	}

	for (j = 0; j < RTW_NNORMAL; j++)
	{
		const int got = mtl_rtp_world(MTL_GB_MAT_WORLD, 0.5f, from_bits(rtw_normal[j].bits));

		printf("rtw: normal %-19s 0x%08x: %d (want %d)\n", rtw_normal[j].name, rtw_normal[j].bits, got, rtw_normal[j].want);
		CHECK(got == rtw_normal[j].want);
		CHECK(rtw_one(MTL_GB_MAT_WORLD, 0x3f000000u, rtw_normal[j].bits));
	}

	/* the material as today: the world's alone, for every depth and normal edge */
	for (i = 0; i < sizeof(mats) / sizeof(mats[0]); i++)
	{
		for (j = 0; j < RTW_NDEPTH; j++)
		{
			int k;

			for (k = 0; k < RTW_NNORMAL; k++)
			{
				CHECK(rtw_one(mats[i], rtw_depth[j].bits, rtw_normal[k].bits));
			}
		}
	}

	CHECK(!mtl_rtp_world(MTL_GB_MAT_WORLD + 1u, 0.5f, 1.0f));
}

/* every 2^12th pattern of each input, crossed with every edge of the other, against the old compare */
static void
test_world_sweep(void)
{
	unsigned i;
	int j, bad = 0, n = 0;

	for (i = 0u; i < (1u << 20); i++)
	{
		const unsigned u = i << 12;

		for (j = 0; j < RTW_NNORMAL; j++, n++)
		{
			bad += !rtw_one(MTL_GB_MAT_WORLD, u, rtw_normal[j].bits);
		}

		for (j = 0; j < RTW_NDEPTH; j++, n++)
		{
			bad += !rtw_one(MTL_GB_MAT_WORLD, rtw_depth[j].bits, u);
		}
	}

	printf("rtw: sweep %d inputs, %d wrong\n", n, bad);
	CHECK(n == (1 << 20) * (RTW_NDEPTH + RTW_NNORMAL));
	CHECK(bad == 0);
}

/*
 * FIX219: the shared G-buffer helpers every kernel's world-pixel and
 * normal-alpha tests call, each over its own edge table (the verdict wanted
 * per edge of rm_gbuf_edges.h, 1 the compare holds) and the 2^12-stride
 * sweep against the float compare it replaces: mtl_rtp_below1 (depth < 1),
 * mtl_rtp_above_half (normal alpha > 0.5) and mtl_rtt_positive (the water
 * mark's alpha > 0). A NaN is refused by each.
 */
static const int gbh_below1[GBE_NEDGE] = { 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0 };
static const int gbh_above_half[GBE_NEDGE] = { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0 };
static const int gbh_positive[GBE_NEDGE] = { 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0 };

/* the helper h's verdict on the pattern u: the float compare ref's, or 0 for a NaN */
static int
gbh_one(int which, unsigned u)
{
	const float f = from_bits(u);
	int got, ref;

	switch (which)
	{
		case 0:
			got = mtl_rtp_below1(f);
			ref = f < 1.0f;
			break;
		case 1:
			got = mtl_rtp_above_half(f);
			ref = f > 0.5f;
			break;
		default:
			got = mtl_rtt_positive(f);
			ref = f > 0.0f;
			break;
	}

	return gbe_is_nan(u) ? (got == 0) : (got == ref);
}

static void
test_gbuf_helpers(void)
{
	static const char *const names[] = { "mtl_rtp_below1", "mtl_rtp_above_half", "mtl_rtt_positive" };
	static const int *const wants[] = { gbh_below1, gbh_above_half, gbh_positive };
	int h, j;

	for (h = 0; h < 3; h++)
	{
		unsigned i;
		int bad = 0;

		for (j = 0; j < GBE_NEDGE; j++)
		{
			const float f = gbe_float(gbe_edges[j].bits);
			const int got = (h == 0) ? mtl_rtp_below1(f) : (h == 1) ? mtl_rtp_above_half(f) : mtl_rtt_positive(f);

			printf("gbh: %s %-22s 0x%08x: %d (want %d)\n", names[h], gbe_edges[j].name, gbe_edges[j].bits, got,
				wants[h][j]);
			CHECK(got == wants[h][j]);
			CHECK(gbh_one(h, gbe_edges[j].bits));
		}

		for (i = 0u; i < GBE_SWEEP_N; i++)
		{
			bad += !gbh_one(h, i << GBE_SWEEP_SHIFT);
		}

		printf("gbh: %s sweep %u patterns, %d wrong\n", names[h], GBE_SWEEP_N, bad);
		CHECK(bad == 0);
	}
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
	test_guards_sweep();
	test_depth_sweep();
	test_reproject_sweep();
	test_history_guards();
	test_world_edges();
	test_world_sweep();
	test_gbuf_helpers();
	printf("rm_rtptemporal_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
