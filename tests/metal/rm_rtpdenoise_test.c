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
 * Host test of RT-primary's 5 x 5 filter of its accumulated light (TASK190
 * slice 2, PM8): common/mtl_rtpd_math.h, which rm_rtpdenoise.metal calls,
 * over RT7's weights (common/mtl_rtd_math.h): which pixels it writes (a
 * world pixel whose count leaves a strength), the neighbour's weight (RT7's,
 * and 0 without a normal), red, green and blue each filtered with their
 * own neighbours, the strength by the count, the trace's alpha kept, the
 * result clamped, and a small image filtered as the kernel does, across a
 * material edge and a depth edge. Built and run by tests/check-ref-metal.sh,
 * which also builds it against mutants of the header that it must fail.
 *
 * =======================================================================
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../../src/client/refresh/metal/common/mtl_rtpd_math.h"
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
near(float a, float b)
{
	return fabsf(a - b) <= 1e-5f;
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

static const mtl_rtao_v3_t up = { 0.0f, 0.0f, 1.0f };

/* the pixels the filter writes: world ones whose count leaves a strength, 1 to under 16 samples */
static void
test_on(void)
{
	CHECK(mtl_rtpd_on(1, 1.0f));
	CHECK(mtl_rtpd_on(1, 15.0f));
	CHECK(!mtl_rtpd_on(1, 16.0f));
	CHECK(!mtl_rtpd_on(1, 255.0f));
	CHECK(!mtl_rtpd_on(1, 0.0f));
	CHECK(!mtl_rtpd_on(1, NAN));
	CHECK(!mtl_rtpd_on(1, INFINITY));
	CHECK(!mtl_rtpd_on(0, 1.0f));
}

/* RT7's weight, and 0 for a neighbour whose normal was not written */
static void
test_weight(void)
{
	const mtl_rtao_v3_t side = { 1.0f, 0.0f, 0.0f };

	CHECK(mtl_rtpd_weight(1, -2, 100.0f, 100.5f, up, up, MTL_GB_MAT_WORLD, MTL_GB_MAT_WORLD, 3.0f, 1.0f) ==
		mtl_rtd_weight(1, -2, 100.0f, 100.5f, up, up, MTL_GB_MAT_WORLD, MTL_GB_MAT_WORLD, 3.0f));
	CHECK(mtl_rtpd_weight(0, 0, 100.0f, 100.0f, up, up, MTL_GB_MAT_WORLD, MTL_GB_MAT_WORLD, 1.0f, 1.0f) > 0.0f);
	CHECK(mtl_rtpd_weight(0, 1, 100.0f, 100.0f, up, up, MTL_GB_MAT_WORLD, MTL_GB_MAT_WORLD, 1.0f, 0.4f) == 0.0f);
	CHECK(mtl_rtpd_weight(0, 1, 100.0f, 100.0f, up, up, MTL_GB_MAT_WORLD, MTL_GB_MAT_WORLD, 1.0f, NAN) == 0.0f);
	CHECK(mtl_rtpd_weight(0, 1, 100.0f, 100.0f, up, up, MTL_GB_MAT_WORLD, MTL_GB_MAT_WORLD + 1u, 1.0f, 1.0f) == 0.0f);
	CHECK(mtl_rtpd_weight(0, 1, 100.0f, 100.0f, up, up, MTL_GB_MAT_WORLD, MTL_GB_MAT_WORLD, 0.0f, 1.0f) == 0.0f);
	CHECK(mtl_rtpd_weight(0, 1, 100.0f, 100.0f, up, side, MTL_GB_MAT_WORLD, MTL_GB_MAT_WORLD, 1.0f, 1.0f) == 0.0f);
	CHECK(mtl_rtpd_weight(0, 1, 100.0f, 200.0f, up, up, MTL_GB_MAT_WORLD, MTL_GB_MAT_WORLD, 1.0f, 1.0f) == 0.0f);
}

/* red, green and blue each move toward the mean of their own channel; the trace's alpha is kept */
static void
test_channels(void)
{
	mtl_rtpd_acc_t a = mtl_rtpd_acc();
	mtl_rtp_rgba_t r;

	a = mtl_rtpd_add(a, 1.0f, rgba(0.1f, 0.5f, 0.9f, 1.0f));
	a = mtl_rtpd_add(a, 3.0f, rgba(0.3f, 0.7f, 0.5f, 2.0f));

	/* one sample: full strength, the weighted mean itself */
	r = mtl_rtpd_result(a, rgba(0.0f, 0.0f, 0.0f, 1.0f), 0.25f);
	CHECK(near(r.r, 0.25f));        /* (0.1 + 0.9) / 4 */
	CHECK(near(r.g, 0.65f));        /* (0.5 + 2.1) / 4 */
	CHECK(near(r.b, 0.6f));         /* (0.9 + 1.5) / 4 */
	CHECK(r.a == 0.25f);

	/* 8.5 samples: half of the way, (16 - 8.5) / 15 */
	r = mtl_rtpd_result(a, rgba(1.0f, 0.0f, 1.0f, 8.5f), 1.0f);
	CHECK(near(r.r, 0.625f));
	CHECK(near(r.g, 0.325f));
	CHECK(near(r.b, 0.8f));
	CHECK(r.a == 1.0f);
}

/* a settled history (16 samples or more) is left as it is, and so is a pixel nothing was added for */
static void
test_settled(void)
{
	mtl_rtpd_acc_t a = mtl_rtpd_acc();
	const mtl_rtp_rgba_t h = rgba(0.123f, 0.456f, 0.789f, 16.0f);
	mtl_rtp_rgba_t r;

	a = mtl_rtpd_add(a, 1.0f, rgba(1.0f, 1.0f, 1.0f, 1.0f));
	r = mtl_rtpd_result(a, h, 0.5f);
	CHECK((r.r == h.r) && (r.g == h.g) && (r.b == h.b) && (r.a == 0.5f));
	r = mtl_rtpd_result(mtl_rtpd_acc(), rgba(0.123f, 0.456f, 0.789f, 1.0f), 0.5f);
	CHECK((r.r == h.r) && (r.g == h.g) && (r.b == h.b) && (r.a == 0.5f));
}

/* a neighbour that is not finite, or weighs nothing, adds nothing; the result is a light in [0, 1] */
static void
test_finite(void)
{
	mtl_rtpd_acc_t a = mtl_rtpd_acc();
	mtl_rtp_rgba_t r;

	a = mtl_rtpd_add(a, 1.0f, rgba(0.5f, 0.5f, 0.5f, 1.0f));
	a = mtl_rtpd_add(a, 1.0f, rgba(NAN, 0.1f, INFINITY, 1.0f));
	a = mtl_rtpd_add(a, 0.0f, rgba(0.9f, 0.9f, 0.9f, 1.0f));
	a = mtl_rtpd_add(a, NAN, rgba(0.9f, 0.9f, 0.9f, 1.0f));
	r = mtl_rtpd_result(a, rgba(0.0f, 0.0f, 0.0f, 1.0f), 1.0f);
	CHECK(near(r.r, 0.5f));
	CHECK(near(r.g, 0.3f));
	CHECK(near(r.b, 0.5f));

	/* the pixel's own value out of range with nothing to move it: clamped, NaN to 0 */
	r = mtl_rtpd_result(mtl_rtpd_acc(), rgba(1.5f, -0.5f, NAN, 1.0f), 1.0f);
	CHECK((r.r == 1.0f) && (r.g == 0.0f) && (r.b == 0.0f));
}

/*
 * A W x H image filtered as rm_rtp_denoise does it: each pixel's 5 x 5
 * neighbours inside the image, weighed by mtl_rtpd_weight, summed by
 * mtl_rtpd_add, the pixel's history moved by mtl_rtpd_result where
 * mtl_rtpd_on. The left three columns are another material; the bottom
 * row is far behind; the rest is one flat world face with noisy light.
 */
#define W 9
#define H 7

typedef struct
{
	mtl_rtp_rgba_t h;
	float d, na;
	unsigned m;
	int world;
} px_t;

static px_t img[H][W];
static mtl_rtp_rgba_t out[H][W];

static void
img_make(float count)
{
	int x, y;

	for (y = 0; y < H; y++)
	{
		for (x = 0; x < W; x++)
		{
			px_t *p = &img[y][x];
			const float n = (float)((x * 7 + y * 13) % 5) * 0.1f;   /* 0 to 0.4, no pattern the blur keeps */

			p->h = rgba(0.3f + n, 0.5f - n * 0.5f, 0.2f + n * 0.25f, count);
			p->d = (y == H - 1) ? 400.0f : 100.0f;
			p->na = 1.0f;
			p->m = MTL_GB_MAT_WORLD;
			p->world = 1;

			if (x < 3)
			{
				p->m = MTL_GB_MAT_WORLD + 1u;
				p->world = 0;
				p->h = rgba(0.9f, 0.9f, 0.9f, 0.0f);
			}
		}
	}
}

static void
img_filter(void)
{
	int x, y, dx, dy;

	for (y = 0; y < H; y++)
	{
		for (x = 0; x < W; x++)
		{
			const px_t *c = &img[y][x];
			mtl_rtpd_acc_t a = mtl_rtpd_acc();

			out[y][x] = c->h;

			if (!mtl_rtpd_on(c->world, c->h.a))
			{
				continue;
			}

			for (dy = -MTL_RTD_RADIUS; dy <= MTL_RTD_RADIUS; dy++)
			{
				for (dx = -MTL_RTD_RADIUS; dx <= MTL_RTD_RADIUS; dx++)
				{
					const int sx = x + dx, sy = y + dy;
					const px_t *n;

					if ((sx < 0) || (sy < 0) || (sx >= W) || (sy >= H))
					{
						continue;
					}

					n = &img[sy][sx];
					a = mtl_rtpd_add(a, mtl_rtpd_weight(dx, dy, c->d, n->d, up, up, c->m, n->m, n->h.a, n->na), n->h);
				}
			}

			out[y][x] = mtl_rtpd_result(a, c->h, 1.0f);
		}
	}
}

/* the spread of the red channel over the world face's rows 0 to H - 2, columns 3 on */
static float
spread(int filtered)
{
	float lo = 1.0f, hi = 0.0f;
	int x, y;

	for (y = 0; y < H - 1; y++)
	{
		for (x = 3; x < W; x++)
		{
			const float v = filtered ? out[y][x].r : img[y][x].h.r;

			lo = (v < lo) ? v : lo;
			hi = (v > hi) ? v : hi;
		}
	}

	return hi - lo;
}

static void
test_image(void)
{
	int x, y, same = 1, edge = 1, far = 1;

	/* a history that has just started: the face's noise smoothed, nothing taken across an edge */
	img_make(1.0f);
	img_filter();
	CHECK(spread(1) < 0.5f * spread(0));

	for (y = 0; y < H; y++)
	{
		for (x = 0; x < 3; x++)
		{
			same = same && !memcmp(&out[y][x], &img[y][x].h, sizeof(out[y][x]));
		}

		/* the column next to the other material: no 0.9 from it, its red stays at most the face's 0.7 */
		edge = edge && (out[y][3].r <= 0.7f + 1e-5f);
	}

	/* the far row only filters with itself: its mean stays within its own values */
	for (x = 3; x < W; x++)
	{
		float lo = 1.0f, hi = 0.0f;
		int k;

		for (k = 3; k < W; k++)
		{
			lo = (img[H - 1][k].h.r < lo) ? img[H - 1][k].h.r : lo;
			hi = (img[H - 1][k].h.r > hi) ? img[H - 1][k].h.r : hi;
		}

		far = far && (out[H - 1][x].r >= lo - 1e-5f) && (out[H - 1][x].r <= hi + 1e-5f);
	}

	CHECK(same);
	CHECK(edge);
	CHECK(far);

	/* a settled history: every pixel as it was */
	img_make(20.0f);
	img_filter();
	same = 1;

	for (y = 0; y < H; y++)
	{
		for (x = 3; x < W; x++)
		{
			same = same && (out[y][x].r == img[y][x].h.r) && (out[y][x].g == img[y][x].h.g) &&
				(out[y][x].b == img[y][x].h.b);
		}
	}

	CHECK(same);
}

/*
 * FIX219: the neighbour's normal alpha over the shared edges and the
 * 2^12-stride sweep: the weight is RT7's where the old compare
 * (nn_a > 0.5f, here without fast math) holds and 0 elsewhere, bit for bit,
 * and 0 for a NaN.
 */
static int
gbe_weight_one(unsigned u)
{
	const float a = gbe_float(u);
	const float got = mtl_rtpd_weight(0, 1, 100.0f, 100.0f, up, up, MTL_GB_MAT_WORLD, MTL_GB_MAT_WORLD, 3.0f, a);
	const float ref = mtl_rtd_weight(0, 1, 100.0f, 100.0f, up, up, MTL_GB_MAT_WORLD, MTL_GB_MAT_WORLD, 3.0f);
	unsigned gb, rb;

	if (gbe_is_nan(u) || !(a > 0.5f))
	{
		return got == 0.0f;
	}

	memcpy(&gb, &got, sizeof(gb));
	memcpy(&rb, &ref, sizeof(rb));
	return (gb == rb) && (ref > 0.0f);
}

static void
test_weight_edges(void)
{
	unsigned i;
	int j, bad = 0;

	for (j = 0; j < GBE_NEDGE; j++)
	{
		const int on = mtl_rtpd_weight(0, 1, 100.0f, 100.0f, up, up, MTL_GB_MAT_WORLD, MTL_GB_MAT_WORLD, 3.0f,
			gbe_float(gbe_edges[j].bits)) > 0.0f;

		printf("gbe: mtl_rtpd_weight nn_a %-22s 0x%08x: %d\n", gbe_edges[j].name, gbe_edges[j].bits, on);
		CHECK(gbe_weight_one(gbe_edges[j].bits));
	}

	for (i = 0u; i < GBE_SWEEP_N; i++)
	{
		bad += !gbe_weight_one(i << GBE_SWEEP_SHIFT);
	}

	printf("gbe: mtl_rtpd_weight sweep %u patterns, %d wrong\n", GBE_SWEEP_N, bad);
	CHECK(bad == 0);
}

int
main(void)
{
	test_weight_edges();
	test_on();
	test_weight();
	test_channels();
	test_settled();
	test_finite();
	test_image();
	printf("rm_rtpdenoise_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
