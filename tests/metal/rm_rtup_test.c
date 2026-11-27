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
 * Host test of the joint bilateral upsample of a ray-traced effect
 * (option E, RT13, TASK126 slice 1): common/mtl_rtup_math.h. An effect
 * scale's divisor; a scale of 1 is the identity (the sizes, the source
 * pixel, the weights and the value, with no fallback counted whatever the
 * pixel holds); a traced pixel across a depth step, across a material-id
 * change, across a crease or with no sample gets no weight; where none
 * matches, the one nearest in depth is taken and counted, and where none
 * has a sample the neutral value; and a small image upsampled as a kernel
 * would, with a depth step and a thin line no traced pixel lies on. Built
 * and run by tests/check-ref-metal.sh, which also builds it against
 * mutants of the header that it must fail.
 *
 * =======================================================================
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../../src/client/refresh/metal/common/mtl_shader_math.h"
#include "../../src/client/refresh/metal/common/mtl_rtup_math.h"

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

static const mtl_rtao_v3_t up = { 0.0f, 0.0f, 1.0f };
static const mtl_rtao_v3_t side = { 1.0f, 0.0f, 0.0f };

/* a world pixel at distance d facing up, its four taps on its own surface with equal bilinear weights */
static mtl_rtup_in_t
flat(float d)
{
	mtl_rtup_in_t in;
	int i;

	memset(&in, 0, sizeof(in));
	in.d = d;
	in.n = up;
	in.m = 1u;

	for (i = 0; i < MTL_RTUP_TAPS; i++)
	{
		in.b[i] = 0.25f;
		in.dn[i] = d;
		in.nn[i] = up;
		in.mn[i] = 1u;
		in.ok[i] = 1;
	}

	return in;
}

static float
wsum(mtl_rtup_out_t o)
{
	return o.w[0] + o.w[1] + o.w[2] + o.w[3];
}

/* the scale as a divisor: the nearest of 1, 2 and 3 to its inverse; a bad value is 1, the design default */
static void
test_div(void)
{
	CHECK(mtl_rtup_div(1.0f) == 1u);
	CHECK(mtl_rtup_div(0.67f) == 1u);
	CHECK(mtl_rtup_div(0.5f) == 2u);
	CHECK(mtl_rtup_div(0.4f) == 2u);
	CHECK(mtl_rtup_div(0.39f) == 3u);
	CHECK(mtl_rtup_div(0.33f) == 3u);
	CHECK(mtl_rtup_div(0.1f) == 3u);
	CHECK(mtl_rtup_div(2.0f) == 1u);
	CHECK(mtl_rtup_div(0.0f) == 1u);
	CHECK(mtl_rtup_div(-0.5f) == 1u);
	CHECK(mtl_rtup_div(NAN) == 1u);
	CHECK(mtl_rtup_div(INFINITY) == 1u);
	CHECK(mtl_rtup_div(-INFINITY) == 1u);
}

/* a scale of 1: the traced size is the drawn size, each pixel its own source, its own sample whole, nothing counted */
static void
test_identity(void)
{
	mtl_rtup_in_t in;
	mtl_rtup_out_t o;
	unsigned x;

	CHECK(mtl_rtup_size(640u, 1u) == 640u);
	CHECK(mtl_rtup_size(1u, 1u) == 1u);

	for (x = 0; x < 9u; x++)
	{
		CHECK(mtl_rtup_src(x, 1u, 9u) == x);
	}

	/* a plain pixel */
	o = mtl_rtup_weights(flat(100.0f), 1u);
	CHECK((o.w[0] == 1.0f) && (o.w[1] == 0.0f) && (o.w[2] == 0.0f) && (o.w[3] == 0.0f));
	CHECK(!o.fallback && !o.empty);
	CHECK(mtl_rtup_value(o, 0.3125f, 0.9f, 0.1f, 0.7f, 1.0f) == 0.3125f);
	CHECK(mtl_rtup_value(o, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f) == 0.0f);

	/* a pixel with no normal, which no tap would match below 1: still itself, no fallback */
	in = flat(100.0f);
	in.n = mtl_rtao_v3(0.0f, 0.0f, 0.0f);
	o = mtl_rtup_weights(in, 1u);
	CHECK((o.w[0] == 1.0f) && !o.fallback && !o.empty);

	/* a pixel the effect wrote its neutral value at (no sample): itself, not empty */
	in = flat(100.0f);
	in.ok[0] = 0;
	o = mtl_rtup_weights(in, 1u);
	CHECK((o.w[0] == 1.0f) && !o.fallback && !o.empty);
	CHECK(mtl_rtup_value(o, 0.75f, 0.0f, 0.0f, 0.0f, 1.0f) == 0.75f);

	/* the same pixel below 1 is not the identity: its plain taps share the weight */
	o = mtl_rtup_weights(flat(100.0f), 2u);
	CHECK(near(o.w[0], 0.25f) && near(o.w[3], 0.25f) && !o.fallback);
}

/* a tap across a depth step weighs nothing; one within RT7's tolerance (2 + 0.02 of the distance) weighs */
static void
test_depth(void)
{
	mtl_rtup_in_t in = flat(100.0f);
	mtl_rtup_out_t o;

	in.dn[2] = 100.0f + 4.5f;
	CHECK(mtl_rtup_tap_w(in, 2) == 0.0f);
	in.dn[2] = 100.0f - 4.5f;
	CHECK(mtl_rtup_tap_w(in, 2) == 0.0f);
	in.dn[2] = 300.0f;
	CHECK(mtl_rtup_tap_w(in, 2) == 0.0f);
	in.dn[2] = 101.0f;
	CHECK(mtl_rtup_tap_w(in, 2) > 0.0f);
	CHECK(mtl_rtup_tap_w(in, 2) < mtl_rtup_tap_w(in, 0));
	CHECK(near(mtl_rtup_tap_w(in, 0), 0.25f));

	in.dn[2] = 300.0f;
	o = mtl_rtup_weights(in, 2u);
	CHECK(o.w[2] == 0.0f);
	CHECK(near(o.w[0], 1.0f / 3.0f) && near(wsum(o), 1.0f) && !o.fallback);
	CHECK(near(mtl_rtup_value(o, 0.2f, 0.2f, 0.9f, 0.2f, 1.0f), 0.2f));
}

/* a tap of another material id weighs nothing, at the pixel's own depth and normal */
static void
test_material(void)
{
	mtl_rtup_in_t in = flat(100.0f);
	mtl_rtup_out_t o;

	in.mn[1] = 2u;
	CHECK(mtl_rtup_tap_w(in, 1) == 0.0f);
	in.mn[1] = 3u;
	CHECK(mtl_rtup_tap_w(in, 1) == 0.0f);
	in.mn[1] = 0u;
	CHECK(mtl_rtup_tap_w(in, 1) == 0.0f);

	in.mn[1] = 2u;
	o = mtl_rtup_weights(in, 2u);
	CHECK(o.w[1] == 0.0f && near(wsum(o), 1.0f) && !o.fallback);
	CHECK(near(mtl_rtup_value(o, 0.5f, 0.0f, 0.5f, 0.5f, 1.0f), 0.5f));

	/* the pixel's own material other than the world's matches its own kind */
	in = flat(100.0f);
	in.m = 2u;
	CHECK(mtl_rtup_tap_w(in, 0) == 0.0f);
	in.mn[0] = 2u;
	CHECK(mtl_rtup_tap_w(in, 0) > 0.0f);
}

/* a tap across a crease, with no sample, or with no bilinear weight weighs nothing */
static void
test_other(void)
{
	mtl_rtup_in_t in = flat(100.0f);

	in.nn[3] = side;
	CHECK(mtl_rtup_tap_w(in, 3) == 0.0f);
	in.nn[3] = mtl_rtao_v3(0.0f, 0.3f, 0.954f);
	CHECK(mtl_rtup_tap_w(in, 3) > 0.0f);

	in = flat(100.0f);
	in.ok[3] = 0;
	CHECK(mtl_rtup_tap_w(in, 3) == 0.0f);

	in = flat(100.0f);
	in.b[3] = 0.0f;
	CHECK(mtl_rtup_tap_w(in, 3) == 0.0f);
	in.dn[3] = NAN;
	in.b[3] = 0.25f;
	CHECK(mtl_rtup_tap_w(in, 3) == 0.0f);
}

/* no tap matches: the one with a sample nearest the pixel in depth, whole, counted; none with a sample: empty */
static void
test_fallback(void)
{
	mtl_rtup_in_t in = flat(380.0f);
	mtl_rtup_out_t o;

	/* a thin model pixel (material 2) over world taps: no match, tap 2 the nearest in depth */
	in.m = 2u;
	in.dn[0] = 100.0f;
	in.dn[1] = 120.0f;
	in.dn[2] = 400.0f;
	in.dn[3] = 410.0f;
	o = mtl_rtup_weights(in, 2u);
	CHECK(o.fallback && !o.empty);
	CHECK((o.w[2] == 1.0f) && (o.w[0] == 0.0f) && (o.w[1] == 0.0f) && (o.w[3] == 0.0f));
	CHECK(mtl_rtup_value(o, 0.2f, 0.2f, 0.9f, 0.8f, 1.0f) == 0.9f);

	/* nearer from below counts as well; a nearer tap with no sample is passed over */
	in.dn[1] = 375.0f;
	o = mtl_rtup_weights(in, 2u);
	CHECK(o.fallback && (o.w[1] == 1.0f));
	in.ok[1] = 0;
	o = mtl_rtup_weights(in, 2u);
	CHECK(o.fallback && (o.w[2] == 1.0f) && (o.w[1] == 0.0f));

	/* equals: the first */
	in = flat(380.0f);
	in.m = 2u;
	in.dn[0] = 500.0f;
	in.dn[1] = 400.0f;
	in.dn[2] = 360.0f;
	in.dn[3] = 400.0f;
	o = mtl_rtup_weights(in, 2u);
	CHECK(o.fallback && (o.w[1] == 1.0f));

	/* a silhouette: the pixel's own material, every tap across the depth step */
	in = flat(100.0f);
	in.dn[0] = in.dn[1] = 400.0f;
	in.dn[2] = in.dn[3] = 420.0f;
	o = mtl_rtup_weights(in, 3u);
	CHECK(o.fallback && (o.w[0] == 1.0f));

	/* a match is no fallback */
	o = mtl_rtup_weights(flat(100.0f), 2u);
	CHECK(!o.fallback && !o.empty);

	/* no tap with a sample (sky about it): empty, the neutral value, no fallback counted */
	in = flat(100.0f);
	in.m = 2u;
	in.ok[0] = in.ok[1] = in.ok[2] = in.ok[3] = 0;
	o = mtl_rtup_weights(in, 2u);
	CHECK(o.empty && !o.fallback && (wsum(o) == 0.0f));
	CHECK(mtl_rtup_value(o, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f) == 1.0f);
}

/*
 * An 8 x 8 view at divisor 2, upsampled as a kernel would: a near wall
 * (depth 100, value 0.2) in columns 0 to 2, a far one (depth 400, value
 * 0.9) in 4 to 7, both the world's, and a thin model line (material 2,
 * depth 380) in column 3, on which no traced pixel lies. Each wall keeps
 * its own value up to the step; the line takes the far wall's, the traced
 * pixels nearest it in depth, and is the only fallback.
 */
#define IMG 8

static void
test_image(void)
{
	float depth[IMG][IMG], val[IMG][IMG];
	unsigned mat[IMG][IMG];
	const unsigned d = 2u, n = mtl_rtup_size(IMG, d);
	int fallbacks = 0, x, y, ok = 1, line = 1;

	CHECK(n == 4u);

	for (y = 0; y < IMG; y++)
	{
		for (x = 0; x < IMG; x++)
		{
			depth[y][x] = (x < 3) ? 100.0f : (x == 3) ? 380.0f : 400.0f;
			mat[y][x] = (x == 3) ? 2u : 1u;
			val[y][x] = (x < 3) ? 0.2f : (x == 3) ? 0.5f : 0.9f;
		}
	}

	for (y = 0; y < IMG; y++)
	{
		for (x = 0; x < IMG; x++)
		{
			const mtl_rtao_pair_t qx = mtl_rtao_up_pair((unsigned)x, d, n);
			const mtl_rtao_pair_t qy = mtl_rtao_up_pair((unsigned)y, d, n);
			const unsigned tx[4] = { qx.lo0, qx.lo1, qx.lo0, qx.lo1 };
			const unsigned ty[4] = { qy.lo0, qy.lo0, qy.lo1, qy.lo1 };
			float v[4];
			mtl_rtup_in_t in;
			mtl_rtup_out_t o;
			float r;
			int i;

			memset(&in, 0, sizeof(in));
			in.d = depth[y][x];
			in.n = up;
			in.m = mat[y][x];
			in = mtl_rtup_bilinear(in, qx, qy);

			for (i = 0; i < 4; i++)
			{
				const unsigned sx = mtl_rtup_src(tx[i], d, IMG), sy = mtl_rtup_src(ty[i], d, IMG);

				in.dn[i] = depth[sy][sx];
				in.nn[i] = up;
				in.mn[i] = mat[sy][sx];
				in.ok[i] = 1;
				v[i] = val[sy][sx];
			}

			o = mtl_rtup_weights(in, d);
			r = mtl_rtup_value(o, v[0], v[1], v[2], v[3], 1.0f);
			fallbacks += o.fallback;

			if (x < 3)
			{
				ok = ok && near(r, 0.2f) && !o.fallback;
			}
			else if (x > 3)
			{
				ok = ok && near(r, 0.9f) && !o.fallback;
			}
			else
			{
				line = line && near(r, 0.9f) && o.fallback;
			}
		}
	}

	CHECK(ok);
	CHECK(line);
	CHECK(fallbacks == IMG);
}

int
main(void)
{
	test_div();
	test_identity();
	test_depth();
	test_material();
	test_other();
	test_fallback();
	test_image();
	printf("rm_rtup_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
