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
 * rm_rtdenoise.metal's rm_rtd_filter runs it; slice 2b's 3 x 3 blur of
 * the water reflections, its taps and depth-guided weight and the blur as
 * rm_rtwdenoise.metal's rm_rtw_denoise runs it. Built and run by
 * tests/check-ref-metal.sh, which also builds it against mutants of the
 * header that it must fail.
 *
 * =======================================================================
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../../src/client/refresh/metal/common/mtl_rtd_math.h"
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

/*
 * Slice 2b: the reflections' 3 x 3 blur. The image's v is a reflection's
 * colour channel, d its distance from the eye, m 1 where it holds a
 * reflection (the kernel's class test, rm_rtd_rok) and 0 where not; the
 * count and the normal are unused: no history, no normal guide.
 */
static float
img_rblur(int x, int y)
{
	mtl_rtd_acc_t a = mtl_rtd_acc();
	const px_t *c = &img[y][x];

	for (int dy = -MTL_RTD_REFL_RADIUS; dy <= MTL_RTD_REFL_RADIUS; dy++)
	{
		for (int dx = -MTL_RTD_REFL_RADIUS; dx <= MTL_RTD_REFL_RADIUS; dx++)
		{
			const int qx = x + dx, qy = y + dy;
			const px_t *q;
			const int ok = (qx >= 0) && (qy >= 0) && (qx < W) && (qy < H) && img[qy][qx].m;

			if ((qx < 0) || (qy < 0) || (qx >= W) || (qy >= H))
			{
				continue;
			}
			q = &img[qy][qx];
			a = mtl_rtd_add(a, mtl_rtd_rweight(dx, dy, c->d, ok ? q->d : c->d, ok), q->v);
		}
	}

	return mtl_rtd_rresult(a, c->v);
}

static void
test_rtap(void)
{
	float sum = 0.0f;

	for (int dy = -1; dy <= 1; dy++)
	{
		for (int dx = -1; dx <= 1; dx++)
		{
			sum += mtl_rtd_rtap(dx, dy);
			CHECK(mtl_rtd_rtap(dx, dy) == mtl_rtd_rtap(-dx, dy));
			CHECK(mtl_rtd_rtap(dx, dy) == mtl_rtd_rtap(dy, dx));
		}
	}

	CHECK(near(sum, 1.0f));
	CHECK(mtl_rtd_rtap(0, 0) == 4.0f / 16.0f);
	CHECK(mtl_rtd_rtap(1, 0) == 2.0f / 16.0f);
	CHECK(mtl_rtd_rtap(1, -1) == 1.0f / 16.0f);
	CHECK(mtl_rtd_rtap(2, 0) == 0.0f);
	CHECK(mtl_rtd_rtap(0, -2) == 0.0f);
	CHECK(MTL_RTD_REFL_RADIUS == 1);
}

static void
test_rweight(void)
{
	/* the tap times the depth guide; nothing for a neighbour without a reflection */
	CHECK(mtl_rtd_rweight(0, 0, 100.0f, 100.0f, 1) == 0.25f);
	CHECK(near(mtl_rtd_rweight(1, 0, 100.0f, 100.0f, 1), 0.125f));
	CHECK(mtl_rtd_rweight(0, 0, 100.0f, 100.0f, 0) == 0.0f);
	CHECK(mtl_rtd_rweight(1, 1, 100.0f, 100.0f, 0) == 0.0f);

	/* the depth guide: half way inside the tolerance (2 + 2 % of 100 = 4), none at it and past it, none on NaN */
	CHECK(near(mtl_rtd_rweight(0, 0, 100.0f, 102.0f, 1), 0.125f));
	CHECK(mtl_rtd_rweight(0, 0, 100.0f, 104.0f, 1) == 0.0f);
	CHECK(mtl_rtd_rweight(1, 0, 100.0f, 96.0f, 1) == 0.0f);
	CHECK(mtl_rtd_rweight(1, 0, 100.0f, 300.0f, 1) == 0.0f);
	CHECK(mtl_rtd_rweight(1, 0, 100.0f, NAN, 1) == 0.0f);
	CHECK(mtl_rtd_rweight(1, 0, NAN, 100.0f, 1) == 0.0f);
	CHECK(mtl_rtd_rweight(1, 0, 100.0f, INFINITY, 1) == 0.0f);

	/* outside the 3 x 3 */
	CHECK(mtl_rtd_rweight(2, 0, 100.0f, 100.0f, 1) == 0.0f);

	/* full strength: the mean itself, the value when nothing was added */
	{
		mtl_rtd_acc_t a = mtl_rtd_acc();

		CHECK(mtl_rtd_rresult(a, 0.3f) == 0.3f);
		a = mtl_rtd_add(a, 0.5f, 1.0f);
		a = mtl_rtd_add(a, 0.5f, 0.0f);
		CHECK(near(mtl_rtd_rresult(a, 0.0f), 0.5f));
		CHECK(near(mtl_rtd_rresult(a, 1.0f), 0.5f));
		CHECK(near(mtl_rtd_rresult(a, NAN), 0.5f));
	}
}

static void
test_rblur(void)
{
	/* a constant is kept */
	img_fill(0.6f, 100.0f, 1, 0.0f);
	CHECK(near(img_rblur(4, 3), 0.6f));
	CHECK(near(img_rblur(0, 0), 0.6f));

	/* a checkerboard is smoothed: the centre 4, the edges 2 x 4, the corners 4 x 1, over 16 */
	for (int y = 0; y < H; y++)
	{
		for (int x = 0; x < W; x++)
		{
			img[y][x].v = ((x + y) & 1) ? 1.0f : 0.0f;
		}
	}
	CHECK(near(img_rblur(4, 3), 8.0f / 16.0f));
	CHECK(near(img_rblur(3, 3), 8.0f / 16.0f));

	/* one bright neighbour: its tap's share */
	img_fill(0.0f, 100.0f, 1, 0.0f);
	img[3][5].v = 1.0f;
	CHECK(near(img_rblur(4, 3), 2.0f / 16.0f));
	img[3][5].v = 0.0f;
	img[2][5].v = 1.0f;
	CHECK(near(img_rblur(4, 3), 1.0f / 16.0f));

	/* a depth edge stops it: the far neighbour is left out */
	img[2][5].d = 200.0f;
	CHECK(img_rblur(4, 3) == 0.0f);

	/* a neighbour without a reflection is left out, however near */
	img[2][5].d = 100.0f;
	img[2][5].m = 0;
	CHECK(img_rblur(4, 3) == 0.0f);

	/* a column of bright neighbours across a depth edge: none of it reaches the pixel */
	img_fill(0.0f, 100.0f, 1, 0.0f);
	for (int y = 0; y < H; y++)
	{
		img[y][5].v = 1.0f;
		img[y][5].d = 50.0f;
	}
	CHECK(img_rblur(4, 3) == 0.0f);
	CHECK(near(img_rblur(5, 3), 1.0f));

	/* the same column at the pixel's depth: 4 of 16 */
	for (int y = 0; y < H; y++)
	{
		img[y][5].d = 100.0f;
	}
	CHECK(near(img_rblur(4, 3), 4.0f / 16.0f));
}

/*
 * FIX222: the history weight's tests over FIX219's edges, the floats
 * beside 16 and the 2^12-stride sweep. Each reference is main's float
 * code (compiled here without fast math, rm_gbuf_edges.h refuses it):
 * mtl_rtd_counted gives "the strength is above 0", mtl_rtd_strength,
 * mtl_rtd_weight's count and mtl_rtd_result give main's answer bit for
 * bit on every pattern that is not a NaN; a NaN is refused by each (not
 * counted, strength 0, weight 0, the value kept).
 */
static float
hw_ref_strength(float n)
{
	if (!(n >= MTL_RTD_COUNT_FULL) || !(n < MTL_RTD_COUNT_NONE))
	{
		return 0.0f;
	}

	return (MTL_RTD_COUNT_NONE - n) / (MTL_RTD_COUNT_NONE - MTL_RTD_COUNT_FULL);
}

static float
hw_ref_result(mtl_rtd_acc_t a, float v, float n)
{
	const float s = hw_ref_strength(n);
	float mean;

	if (!(s > 0.0f) || !(a.wsum > 0.0f))
	{
		return v;
	}

	mean = a.sum / a.wsum;

	if (!isfinite(mean))
	{
		return v;
	}

	if (!isfinite(v))
	{
		return mean;
	}

	return v + s * (mean - v);
}

static unsigned
hw_bits(float f)
{
	unsigned u;

	memcpy(&u, &f, sizeof(u));
	return u;
}

/* the floats beside the count's cut at 16 and inside the ramp, beyond FIX219's shared edges */
static const gbe_edge_t hw_edges[] = {
	{ 0x3f7fffffu, "largest below 1" },
	{ 0x40000000u, "2" },
	{ 0x41080000u, "8.5" },
	{ 0x41700000u, "15" },
	{ 0x417fffffu, "largest below 16" },
	{ 0x41800000u, "16" },
	{ 0x41800001u, "smallest above 16" },
	{ 0x437f0000u, "255" },
};
#define HW_NEDGE ((int)(sizeof(hw_edges) / sizeof(hw_edges[0])))

/* 1 when every history-weight test gives main's answer for the pattern u, and refuses it when a NaN */
static int
hw_one(unsigned u)
{
	const mtl_rtao_v3_t up = mtl_rtao_v3(0.0f, 0.0f, 1.0f);
	const float n = gbe_float(u);
	const float w = mtl_rtd_weight(1, 0, 100.0f, 100.0f, up, up, 1, 1, n);
	const float wfull = mtl_rtd_weight(1, 0, 100.0f, 100.0f, up, up, 1, 1, 1.0f);
	mtl_rtd_acc_t a = mtl_rtd_acc();

	a = mtl_rtd_add(a, 1.0f, 0.25f);

	if (gbe_is_nan(u))
	{
		return !mtl_rtd_counted(n) && (hw_bits(mtl_rtd_strength(n)) == 0u) && (w == 0.0f) &&
			(hw_bits(mtl_rtd_result(a, 0.75f, n)) == hw_bits(0.75f));
	}

	return (mtl_rtd_counted(n) == (hw_ref_strength(n) > 0.0f)) &&
		(mtl_rtd_counted(n) == ((n >= 1.0f) && (n < 16.0f))) &&
		(hw_bits(mtl_rtd_strength(n)) == hw_bits(hw_ref_strength(n))) &&
		(hw_bits(w) == hw_bits((n >= 1.0f) ? wfull : 0.0f)) &&
		(hw_bits(mtl_rtd_result(a, 0.75f, n)) == hw_bits(hw_ref_result(a, 0.75f, n)));
}

static void
test_history_weight(void)
{
	unsigned i;
	int j, bad = 0;

	CHECK(gbe_float(MTL_RTD_BITS_FULL) == MTL_RTD_COUNT_FULL);
	CHECK(gbe_float(MTL_RTD_BITS_NONE) == MTL_RTD_COUNT_NONE);

	for (j = 0; j < GBE_NEDGE + HW_NEDGE; j++)
	{
		const gbe_edge_t *e = (j < GBE_NEDGE) ? &gbe_edges[j] : &hw_edges[j - GBE_NEDGE];
		const float n = gbe_float(e->bits);

		printf("hw: %-22s 0x%08x: counted %d, strength 0x%08x\n", e->name, e->bits, mtl_rtd_counted(n),
			hw_bits(mtl_rtd_strength(n)));
		CHECK(hw_one(e->bits));
	}

	/* the verdicts the edges want: counted from 1 up to, not at, 16 */
	CHECK(!mtl_rtd_counted(gbe_float(0x3f7fffffu)));
	CHECK(mtl_rtd_counted(gbe_float(0x3f800000u)));
	CHECK(mtl_rtd_counted(gbe_float(0x417fffffu)));
	CHECK(!mtl_rtd_counted(gbe_float(0x41800000u)));
	CHECK(!mtl_rtd_counted(gbe_float(0x80000000u)));
	CHECK(!mtl_rtd_counted(gbe_float(0x7f800000u)));
	CHECK(!mtl_rtd_counted(gbe_float(0xff800000u)));

	for (i = 0u; i < GBE_SWEEP_N; i++)
	{
		bad += !hw_one(i << GBE_SWEEP_SHIFT);
	}

	printf("hw: history weight sweep %u patterns, %d wrong\n", GBE_SWEEP_N, bad);
	CHECK(bad == 0);
}

int
main(void)
{
	test_history_weight();
	test_strength();
	test_tap();
	test_depth();
	test_normal();
	test_weight();
	test_acc();
	test_blur();
	test_rtap();
	test_rweight();
	test_rblur();

	printf("rm_rtdenoise_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
