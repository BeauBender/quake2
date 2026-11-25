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
 * Host test of ref_metal's denoiser (RT7, TASK59 slice 2): the weight
 * rule of common/mtl_rtd_math.h, which the kernels call: the strength
 * from the sample count, the spatial, depth and normal weights, the
 * running sums and the result, and a 5 x 5 blur over a small image as
 * rm_rtdenoise.metal's rm_rtd_filter runs it. Built and run by
 * tests/check-ref-metal.sh, which also builds it against mutants of the
 * header that it must fail.
 *
 * =======================================================================
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../../src/client/refresh/metal/common/mtl_rtd_math.h"

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

/* the image of the blur test: W x H pixels, each its value, distance, material, count and normal */
#define W 9
#define H 7

typedef struct
{
	float v, d, n;
	unsigned m;
	mtl_rtao_v3_t nrm;
} px_t;

static px_t img[H][W];

static void
img_fill(float v, float d, unsigned m, float n)
{
	int x, y;

	for (y = 0; y < H; y++)
	{
		for (x = 0; x < W; x++)
		{
			img[y][x].v = v;
			img[y][x].d = d;
			img[y][x].m = m;
			img[y][x].n = n;
			img[y][x].nrm = mtl_rtao_v3(0.0f, 0.0f, 1.0f);
		}
	}
}

/* the blur of pixel (x, y), as rm_rtd_filter: the taps inside the image, the pixel's own included */
static float
img_blur(int x, int y)
{
	const px_t *c = &img[y][x];
	mtl_rtd_acc_t a = mtl_rtd_acc();
	int dx, dy;

	for (dy = -MTL_RTD_RADIUS; dy <= MTL_RTD_RADIUS; dy++)
	{
		for (dx = -MTL_RTD_RADIUS; dx <= MTL_RTD_RADIUS; dx++)
		{
			const int sx = x + dx, sy = y + dy;
			const px_t *s;

			if ((sx < 0) || (sy < 0) || (sx >= W) || (sy >= H))
			{
				continue;
			}

			s = &img[sy][sx];
			a = mtl_rtd_add(a, mtl_rtd_weight(dx, dy, c->d, s->d, c->nrm, s->nrm, c->m, s->m, s->n), s->v);
		}
	}

	return mtl_rtd_result(a, c->v, c->n);
}

static void
test_strength(void)
{
	CHECK(mtl_rtd_strength(1.0f) == 1.0f);
	CHECK(near(mtl_rtd_strength(2.0f), 14.0f / 15.0f));
	CHECK(near(mtl_rtd_strength(8.5f), 0.5f));
	CHECK(near(mtl_rtd_strength(15.0f), 1.0f / 15.0f));
	CHECK(mtl_rtd_strength(16.0f) == 0.0f);
	CHECK(mtl_rtd_strength(17.0f) == 0.0f);
	CHECK(mtl_rtd_strength(255.0f) == 0.0f);
	/* no sample, or a count that is not finite: no blur */
	CHECK(mtl_rtd_strength(0.0f) == 0.0f);
	CHECK(mtl_rtd_strength(0.5f) == 0.0f);
	CHECK(mtl_rtd_strength(-1.0f) == 0.0f);
	CHECK(mtl_rtd_strength(NAN) == 0.0f);
	CHECK(mtl_rtd_strength(INFINITY) == 0.0f);
	CHECK(mtl_rtd_strength(-INFINITY) == 0.0f);
}

static void
test_tap(void)
{
	float sum = 0.0f;
	int dx, dy;

	for (dy = -2; dy <= 2; dy++)
	{
		for (dx = -2; dx <= 2; dx++)
		{
			sum += mtl_rtd_tap(dx, dy);
			CHECK(mtl_rtd_tap(dx, dy) == mtl_rtd_tap(-dx, dy));
			CHECK(mtl_rtd_tap(dx, dy) == mtl_rtd_tap(dy, dx));
		}
	}

	CHECK(near(sum, 1.0f));
	CHECK(mtl_rtd_tap(0, 0) == 36.0f / 256.0f);
	CHECK(mtl_rtd_tap(2, 2) == 1.0f / 256.0f);
	CHECK(mtl_rtd_tap(1, 0) == 24.0f / 256.0f);
	CHECK(mtl_rtd_tap(3, 0) == 0.0f);
	CHECK(mtl_rtd_tap(0, -3) == 0.0f);
}

static void
test_depth(void)
{
	/* tolerance at 100 units: 2 + 2 = 4 units */
	CHECK(mtl_rtd_depth_w(100.0f, 100.0f) == 1.0f);
	CHECK(near(mtl_rtd_depth_w(100.0f, 102.0f), 0.5f));
	CHECK(near(mtl_rtd_depth_w(100.0f, 98.0f), 0.5f));
	CHECK(mtl_rtd_depth_w(100.0f, 104.0f) == 0.0f);
	CHECK(mtl_rtd_depth_w(100.0f, 96.0f) == 0.0f);
	CHECK(mtl_rtd_depth_w(100.0f, 400.0f) == 0.0f);
	CHECK(mtl_rtd_depth_w(100.0f, NAN) == 0.0f);
	CHECK(mtl_rtd_depth_w(NAN, 100.0f) == 0.0f);
	CHECK(mtl_rtd_depth_w(100.0f, INFINITY) == 0.0f);
}

static void
test_normal(void)
{
	const mtl_rtao_v3_t up = mtl_rtao_v3(0.0f, 0.0f, 1.0f);
	const float s = sqrtf(1.0f - 0.95f * 0.95f);

	CHECK(mtl_rtd_normal_w(up, up) == 1.0f);
	CHECK(near(mtl_rtd_normal_w(up, mtl_rtao_v3(s, 0.0f, 0.95f)), 0.5f));
	CHECK(mtl_rtd_normal_w(up, mtl_rtao_v3(1.0f, 0.0f, 0.0f)) == 0.0f);
	CHECK(mtl_rtd_normal_w(up, mtl_rtao_v3(0.0f, 0.0f, -1.0f)) == 0.0f);
	CHECK(mtl_rtd_normal_w(up, mtl_rtao_v3(0.5f, 0.0f, 0.866f)) == 0.0f);
	CHECK(mtl_rtd_normal_w(up, mtl_rtao_v3(NAN, 0.0f, 1.0f)) == 0.0f);
}

static void
test_weight(void)
{
	const mtl_rtao_v3_t up = mtl_rtao_v3(0.0f, 0.0f, 1.0f), side = mtl_rtao_v3(1.0f, 0.0f, 0.0f);

	CHECK(mtl_rtd_weight(0, 0, 100.0f, 100.0f, up, up, 1, 1, 1.0f) == 36.0f / 256.0f);
	CHECK(near(mtl_rtd_weight(1, 0, 100.0f, 102.0f, up, up, 1, 1, 5.0f), 0.5f * 24.0f / 256.0f));
	/* the edges: another material, no sample, a depth or a normal edge */
	CHECK(mtl_rtd_weight(1, 0, 100.0f, 100.0f, up, up, 1, 2, 5.0f) == 0.0f);
	CHECK(mtl_rtd_weight(1, 0, 100.0f, 100.0f, up, up, 1, 1, 0.0f) == 0.0f);
	CHECK(mtl_rtd_weight(1, 0, 100.0f, 100.0f, up, up, 1, 1, NAN) == 0.0f);
	CHECK(mtl_rtd_weight(1, 0, 100.0f, 120.0f, up, up, 1, 1, 5.0f) == 0.0f);
	CHECK(mtl_rtd_weight(1, 0, 100.0f, 100.0f, up, side, 1, 1, 5.0f) == 0.0f);
	CHECK(mtl_rtd_weight(3, 0, 100.0f, 100.0f, up, up, 1, 1, 5.0f) == 0.0f);
	CHECK(mtl_rtd_weight(1, 0, NAN, 100.0f, up, up, 1, 1, 5.0f) == 0.0f);
}

static void
test_acc(void)
{
	mtl_rtd_acc_t a = mtl_rtd_acc();

	CHECK((a.sum == 0.0f) && (a.wsum == 0.0f));
	/* nothing added: the value is kept */
	CHECK(mtl_rtd_result(a, 0.25f, 1.0f) == 0.25f);
	/* a zero, negative or not finite weight, or a value not finite, adds nothing */
	a = mtl_rtd_add(a, 0.0f, 1.0f);
	a = mtl_rtd_add(a, -1.0f, 1.0f);
	a = mtl_rtd_add(a, NAN, 1.0f);
	a = mtl_rtd_add(a, INFINITY, 1.0f);
	a = mtl_rtd_add(a, 1.0f, NAN);
	a = mtl_rtd_add(a, 1.0f, INFINITY);
	CHECK((a.sum == 0.0f) && (a.wsum == 0.0f));
	CHECK(mtl_rtd_result(a, 0.25f, 1.0f) == 0.25f);
	a = mtl_rtd_add(a, 1.0f, 1.0f);
	a = mtl_rtd_add(a, 3.0f, 0.0f);
	CHECK((a.sum == 1.0f) && (a.wsum == 4.0f));
	/* full strength at 1 sample: the mean; half at 8.5; none at 16 or a count not finite */
	CHECK(near(mtl_rtd_result(a, 1.0f, 1.0f), 0.25f));
	CHECK(near(mtl_rtd_result(a, 1.0f, 8.5f), 0.625f));
	CHECK(mtl_rtd_result(a, 1.0f, 16.0f) == 1.0f);
	CHECK(mtl_rtd_result(a, 1.0f, 200.0f) == 1.0f);
	CHECK(mtl_rtd_result(a, 1.0f, NAN) == 1.0f);
	CHECK(mtl_rtd_result(a, 1.0f, 0.0f) == 1.0f);
}

static void
test_blur(void)
{
	int x;

	/* a constant image stays constant at any count */
	img_fill(0.5f, 100.0f, 1, 1.0f);
	CHECK(near(img_blur(4, 3), 0.5f));
	CHECK(near(img_blur(0, 0), 0.5f));

	/* a checkerboard at 1 sample is smoothed toward its mean; at 16 it is kept */
	img_fill(0.0f, 100.0f, 1, 1.0f);

	for (x = 0; x < W * H; x++)
	{
		img[x / W][x % W].v = (((x / W) + (x % W)) & 1) ? 1.0f : 0.0f;
	}

	CHECK(fabsf(img_blur(4, 3) - 0.5f) < 0.05f);
	img[3][4].n = 16.0f;
	CHECK(img_blur(4, 3) == img[3][4].v);
	img[3][4].n = 300.0f;
	CHECK(img_blur(4, 3) == img[3][4].v);
	img[3][4].n = NAN;
	CHECK(img_blur(4, 3) == img[3][4].v);

	/* a depth edge: the left half at 100 units 0, the right half at 300 units 1; neither side blurs into the other */
	img_fill(0.0f, 100.0f, 1, 1.0f);

	for (x = 0; x < W * H; x++)
	{
		if ((x % W) >= 5)
		{
			img[x / W][x % W].v = 1.0f;
			img[x / W][x % W].d = 300.0f;
		}
	}

	CHECK(img_blur(4, 3) == 0.0f);
	CHECK(img_blur(5, 3) == 1.0f);
	/* the edge within the tolerance blurs */
	img[3][5].d = 101.0f;
	CHECK(img_blur(4, 3) > 0.0f);

	/* a normal edge: the right half a wall, the left a floor */
	img_fill(0.0f, 100.0f, 1, 1.0f);

	for (x = 0; x < W * H; x++)
	{
		if ((x % W) >= 5)
		{
			img[x / W][x % W].v = 1.0f;
			img[x / W][x % W].nrm = mtl_rtao_v3(1.0f, 0.0f, 0.0f);
		}
	}

	CHECK(img_blur(4, 3) == 0.0f);
	CHECK(img_blur(5, 3) == 1.0f);

	/* a material edge and a neighbour with no sample */
	img_fill(0.0f, 100.0f, 1, 1.0f);
	img[3][5].v = 1.0f;
	img[3][5].m = 4;
	CHECK(img_blur(4, 3) == 0.0f);
	img[3][5].m = 1;
	img[3][5].n = 0.0f;
	CHECK(img_blur(4, 3) == 0.0f);

	/* a neighbour that is not finite is left out; the pixel's own value not finite is the mean of the others at a strength above 0 */
	img[3][5].n = 1.0f;
	img[3][5].v = NAN;
	CHECK(img_blur(4, 3) == 0.0f);
	img[3][4].v = NAN;
	img[3][5].v = 0.0f;
	CHECK(img_blur(4, 3) == 0.0f);
}

int
main(void)
{
	test_strength();
	test_tap();
	test_depth();
	test_normal();
	test_weight();
	test_acc();
	test_blur();

	printf("rm_rtdenoise_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
