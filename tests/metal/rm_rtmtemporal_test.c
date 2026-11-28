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
 * Host test of the model pixels' accumulation under RT-primary (TASK191
 * slice 4): the rule of common/mtl_rtmt_math.h that rm_rtmtemporal.metal
 * calls. The pixels it accumulates are the model pass's and never the
 * world's; the history's key keeps model history and world history
 * apart, and the weapon's apart from every other model's; a model
 * pixel's light is blended by TASK190's rule with an opaque alpha; and
 * gl1's model colour taken of the blend keeps the glow pulse and the
 * minlight floor of each frame. Built and run by tests/check-ref-metal.sh,
 * which also builds it against mutants of the header that it must fail.
 *
 * =======================================================================
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../../src/client/refresh/metal/common/mtl_rtmt_math.h"
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

static int
same_bits(float a, float b)
{
	return !memcmp(&a, &b, sizeof(a));
}

/* the pixels: the model pass's (rm_rtm_trace's test), never a world one */
static void
test_model(void)
{
	unsigned m;

	CHECK(mtl_rtmt_model(MTL_GB_MAT_ALIAS, 0.5f, 1.0f));
	CHECK(mtl_rtmt_model(MTL_GB_MAT_ALIAS, 0.0f, 0.51f));
	CHECK(!mtl_rtmt_model(MTL_GB_MAT_ALIAS, 1.0f, 1.0f));
	CHECK(!mtl_rtmt_model(MTL_GB_MAT_ALIAS, 0.5f, 0.5f));
	CHECK(!mtl_rtmt_model(MTL_GB_MAT_ALIAS, NAN, 1.0f));
	CHECK(!mtl_rtmt_model(MTL_GB_MAT_ALIAS, 0.5f, NAN));

	for (m = 0; m < 256; m++)
	{
		const int model = mtl_rtmt_model(m, 0.5f, 1.0f);

		CHECK(model == (m == MTL_GB_MAT_ALIAS));
		/* never both: the world's accumulation and the model pass write disjoint pixels */
		CHECK(!(model && mtl_rtp_world(m, 0.5f, 1.0f)));
	}
}

/* the key: no model key is a G-buffer material id, and each set of flags has its own */
static void
test_key(void)
{
	unsigned f, g, m;

	for (f = 0; f <= MTL_RTM_FLAGS; f++)
	{
		const unsigned k = mtl_rtmt_key(f);

		/* the G-buffer's material ids are R8Uint: below 256, the world's among them */
		for (m = 0; m < 256; m++)
		{
			CHECK((m == MTL_GB_MAT_ALIAS) ? ((k == m) == (f == 0u)) : (k != m));
		}

		for (g = 0; g <= MTL_RTM_FLAGS; g++)
		{
			CHECK((mtl_rtmt_key(g) == k) == (g == f));
		}

		/* flags past the tag's are not a key of their own */
		CHECK(mtl_rtmt_key(f | 0x100u) == k);
	}

	/* the weapon's history is never another model's */
	CHECK(mtl_rtmt_key(MTL_RTM_WEAPON) != mtl_rtmt_key(0u));
	CHECK(mtl_rtmt_key(MTL_RTM_WEAPON | MTL_RTM_MINLIGHT) != mtl_rtmt_key(MTL_RTM_MINLIGHT));
	CHECK(mtl_rtmt_key(MTL_RTM_GLOW) != MTL_GB_MAT_WORLD);
}

/* the history across the key: world and model history never blend into each other */
static void
test_reject(void)
{
	const mtl_rtp_rgba_t h = rgba(0.5f, 0.5f, 0.5f, 10.0f);
	unsigned f;

	for (f = 0; f <= MTL_RTM_FLAGS; f++)
	{
		const unsigned k = mtl_rtmt_key(f);

		/* the same model's history is kept */
		CHECK(mtl_rtp_reject(0u, 1, 50.0f, 50.0f, 0.0f, k, k, h) == 0u);
		/* a model pixel on world history, and a world pixel on model history */
		CHECK(mtl_rtp_reject(0u, 1, 50.0f, 50.0f, 0.0f, k, MTL_GB_MAT_WORLD, h) & MTL_RTT_REJ_MAT);
		CHECK(mtl_rtp_reject(0u, 1, 50.0f, 50.0f, 0.0f, MTL_GB_MAT_WORLD, k, h) & MTL_RTT_REJ_MAT);
		/* the view's reset, off the screen, the depth */
		CHECK(mtl_rtp_reject(MTL_RTT_REJ_RESET, 1, 50.0f, 50.0f, 0.0f, k, k, h) & MTL_RTT_REJ_RESET);
		CHECK(mtl_rtp_reject(0u, 0, 50.0f, 50.0f, 0.0f, k, k, h) & MTL_RTT_REJ_OFF);
		CHECK(mtl_rtp_reject(0u, 1, 50.0f, 60.0f, 0.0f, k, k, h) & MTL_RTT_REJ_DEPTH);
	}

	/* the weapon's history and another model's */
	CHECK(mtl_rtp_reject(0u, 1, 20.0f, 20.0f, 0.0f, mtl_rtmt_key(MTL_RTM_WEAPON), mtl_rtmt_key(0u), h) &
			MTL_RTT_REJ_MAT);
	/* a texel the world's accumulation passed through (count 0, material 2) is an empty history */
	CHECK(mtl_rtp_reject(0u, 1, 50.0f, 0.0f, 0.0f, mtl_rtmt_key(0u), MTL_GB_MAT_ALIAS, rgba(0.0f, 0.0f, 0.0f, 0.0f)) &
			MTL_RTT_REJ_EMPTY);
}

/* one model pixel: blended as a world pixel, its alpha an opaque model's, its history the blend */
static void
test_pixel(void)
{
	const mtl_rtp_rgba_t h = rgba(0.5f, 0.25f, 1.0f, 3.0f);
	const mtl_rtm_v3_t l = mtl_rtm_v3(0.1f, 0.7f, 0.3f);
	mtl_rtp_px_t p;

	/* a rejected history: the traced light itself, bit for bit, and a count of 1 */
	p = mtl_rtmt_pixel(MTL_RTT_REJ_RESET, l, h, 0.95f);
	CHECK(same_bits(p.out.r, l.x) && same_bits(p.out.g, l.y) && same_bits(p.out.b, l.z));
	CHECK(p.out.a == 1.0f);
	CHECK(p.hist.a == 1.0f);
	CHECK(same_bits(p.hist.r, l.x) && same_bits(p.hist.g, l.y) && same_bits(p.hist.b, l.z));

	/* a kept history of 3 samples: the plain mean of 4 below 0.75, each channel its own */
	p = mtl_rtmt_pixel(0u, l, h, 0.95f);
	CHECK(fabsf(p.out.r - (0.75f * 0.5f + 0.25f * 0.1f)) < 1e-6f);
	CHECK(fabsf(p.out.g - (0.75f * 0.25f + 0.25f * 0.7f)) < 1e-6f);
	CHECK(fabsf(p.out.b - (0.75f * 1.0f + 0.25f * 0.3f)) < 1e-6f);
	CHECK(p.out.a == 1.0f);
	CHECK(p.hist.a == 4.0f);
	CHECK(same_bits(p.hist.r, p.out.r) && same_bits(p.hist.g, p.out.g) && same_bits(p.hist.b, p.out.b));

	/* the history's weight is at most r_rt_temporal's */
	p = mtl_rtmt_pixel(0u, l, rgba(0.5f, 0.5f, 0.5f, 200.0f), 0.5f);
	CHECK(fabsf(p.out.r - 0.3f) < 1e-6f);

	/* r_rt_temporal 0 keeps the trace's light */
	p = mtl_rtmt_pixel(0u, l, h, 0.0f);
	CHECK(same_bits(p.out.g, l.y));

	/* a NaN or a light outside 0 .. 1 is clamped before the blend; the output stays a light */
	p = mtl_rtmt_pixel(MTL_RTT_REJ_EMPTY, mtl_rtm_v3(NAN, 2.0f, -1.0f), h, 0.95f);
	CHECK((p.out.r == 0.0f) && (p.out.g == 1.0f) && (p.out.b == 0.0f) && (p.out.a == 1.0f));
}

/*
 * The model colour of the blend (the kernel's order: mtl_rtmt_pixel, then
 * mtl_rtm_color at MTL_RTM_DOT_TRACED): the history holds the light, so a
 * settled light's colour follows the glow pulse frame by frame, and the
 * minlight floor holds on every frame, as an unaccumulated pixel's.
 */
static void
test_color(void)
{
	const mtl_rtm_v3_t l = mtl_rtm_v3(0.4f, 0.4f, 0.4f);
	mtl_rtp_rgba_t h = rgba(0.0f, 0.0f, 0.0f, 0.0f);
	unsigned rej = MTL_RTT_REJ_EMPTY;
	int frame, same = 0, n = 0;

	for (frame = 0; frame < 60; frame++)
	{
		const float glow = 0.1f * sinf(7.0f * (float)frame / 60.0f);
		const mtl_rtp_px_t p = mtl_rtmt_pixel(rej, l, h, 0.95f);
		const mtl_rtm_idx_t a = mtl_rtm_color(mtl_rtm_v3(p.out.r, p.out.g, p.out.b), MTL_RTM_DOT_TRACED,
				MTL_RTM_GLOW, glow, 2.0f);
		const mtl_rtm_idx_t b = mtl_rtm_color(l, MTL_RTM_DOT_TRACED, MTL_RTM_GLOW, glow, 2.0f);

		n++;
		same += (a.r == b.r) && (a.g == b.g) && (a.b == b.b);
		h = p.hist;
		rej = 0u;
	}

	/* a constant light's blend is the light, so every frame's pulse is the unaccumulated pixel's */
	CHECK(same == n);
	/* and the history holds the light, not the pulsed colour */
	CHECK(fabsf(h.r - 0.4f) < 1e-6f);

	/* the minlight floor of a dark blend, as an unaccumulated pixel's */
	{
		const mtl_rtp_px_t p = mtl_rtmt_pixel(0u, mtl_rtm_v3(0.0f, 0.0f, 0.0f), rgba(0.02f, 0.02f, 0.02f, 9.0f), 0.95f);
		const mtl_rtm_idx_t a = mtl_rtm_color(mtl_rtm_v3(p.out.r, p.out.g, p.out.b), MTL_RTM_DOT_TRACED,
				MTL_RTM_MINLIGHT, 0.0f, 0.0f);

		CHECK((a.r == 25) && (a.g == 25) && (a.b == 25));
	}
}

/*
 * FIX219: the model pixel's depth and normal alpha over the shared edges,
 * each crossed with every edge of the other, and the 2^12-stride sweep of
 * each against every edge of the other: the old compare's verdict
 * ((depth < 1.0f) && (normal_a > 0.5f), here without fast math) on every
 * input with no NaN, and not a model pixel where either is a NaN.
 */
static int
gbe_model_one(unsigned d, unsigned n)
{
	const float df = gbe_float(d), nf = gbe_float(n);
	const int got = mtl_rtmt_model(MTL_GB_MAT_ALIAS, df, nf);

	if (gbe_is_nan(d) || gbe_is_nan(n))
	{
		return got == 0;
	}

	return got == ((df < 1.0f) && (nf > 0.5f));
}

static void
test_model_edges(void)
{
	unsigned i;
	int j, k, bad = 0;

	for (j = 0; j < GBE_NEDGE; j++)
	{
		printf("gbe: mtl_rtmt_model depth %-22s 0x%08x: %d, normal alpha: %d\n", gbe_edges[j].name, gbe_edges[j].bits,
			mtl_rtmt_model(MTL_GB_MAT_ALIAS, gbe_float(gbe_edges[j].bits), 1.0f),
			mtl_rtmt_model(MTL_GB_MAT_ALIAS, 0.5f, gbe_float(gbe_edges[j].bits)));

		for (k = 0; k < GBE_NEDGE; k++)
		{
			CHECK(gbe_model_one(gbe_edges[j].bits, gbe_edges[k].bits));
		}
	}

	for (i = 0u; i < GBE_SWEEP_N; i++)
	{
		for (j = 0; j < GBE_NEDGE; j++)
		{
			bad += !gbe_model_one(i << GBE_SWEEP_SHIFT, gbe_edges[j].bits);
			bad += !gbe_model_one(gbe_edges[j].bits, i << GBE_SWEEP_SHIFT);
		}
	}

	printf("gbe: mtl_rtmt_model sweep %u inputs, %d wrong\n", GBE_SWEEP_N * 2u * (unsigned)GBE_NEDGE, bad);
	CHECK(bad == 0);
}

int
main(void)
{
	test_model_edges();
	test_model();
	test_key();
	test_reject();
	test_pixel();
	test_color();
	printf("rm_rtmtemporal_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
