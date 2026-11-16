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
 * The host test of the pure part of ref_metal's ray-traced water
 * reflections (RT6, TASK58): rm_rtwater.c and common/mtl_rtw_math.h,
 * which the kernel shares. Run by tests/check-ref-metal.sh, linked with
 * rm_rtwater.c, rm_rtshadow.c (the view's inverse), rm_rt.c (the
 * instances the rays test) and rm_pure.c (gl1's turbulence table and
 * index):
 *
 * - the material mask and the eye ray's steps;
 * - the warp's phase as gl1 indexes its table, and the table;
 * - the perturbed normal and the reflected direction, and that a ray
 *   from the water never meets the water's plane;
 * - the sky lookup's choice;
 * - the blend, which keeps the alpha and leaves a pixel without a
 *   reflection bit for bit;
 * - the cvars, the view, the instances and the stats line.
 *
 * =======================================================================
 */

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/client/refresh/metal/header/rm_rtwater.h"
#include "../../src/client/refresh/metal/header/rm_rtshadow.h"
#include "../../src/client/refresh/metal/header/rm_rt.h"
#include "../../src/client/refresh/metal/common/mtl_matrix.h"

/* src/common/shared/shared.c, linked for rm_pure.c, prints through the engine */
void
Com_Printf(const char *msg, ...)
{
	(void)msg;
}

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

/* a deterministic generator: the same cases every run */
static unsigned rng = 97531u;

static float
frand(float lo, float hi)
{
	rng = rng * 1103515245u + 12345u;
	return lo + (hi - lo) * (float)((rng >> 8) & 0xffffff) / 16777216.0f;
}

static mtl_rtw_v3_t
rand_unit(void)
{
	for (;;)
	{
		const mtl_rtw_v3_t v = mtl_rtw_v3(frand(-1, 1), frand(-1, 1), frand(-1, 1));
		const float l = sqrtf(mtl_rtw_dot(v, v));

		if ((l > 0.1f) && (l <= 1.0f))
		{
			return mtl_rtw_v3(v.x / l, v.y / l, v.z / l);
		}
	}
}

static float
len3(mtl_rtw_v3_t v)
{
	return sqrtf(mtl_rtw_dot(v, v));
}

/* ---- the material mask ------------------------------------------------------------------ */

static void
test_mask(void)
{
	unsigned m;
	int ok_water = 1, ok_eye = 1;

	/* only the raster's warped water is a water pixel of the G-buffer */
	for (m = 0; m < 256; m++)
	{
		if (mtl_rtw_is_water(m) != (m == MTL_GB_MAT_WATER))
		{
			ok_water = 0;
		}
	}

	CHECK(ok_water);
	CHECK(MTL_GB_MAT_WATER == 3);

	/* the kernel reads the map's ids (rm_rt_materials) with the G-buffer's names: the same numbers */
	CHECK(RM_RT_MAT_WATER == MTL_GB_MAT_WATER);
	CHECK(RM_RT_MAT_SKY == MTL_GB_MAT_SKY);
	CHECK(RM_RT_MAT_ALPHA == MTL_GB_MAT_TRANS);
	CHECK(RM_RT_MAT_WORLD == MTL_GB_MAT_WORLD);
	CHECK(MTL_RTW_MAT_ALIAS >= RM_RT_MATS);

	/* an eye ray: water found, a translucent face or a model passed, the rest stops it */
	for (m = 0; m < 256; m++)
	{
		const int want = (m == MTL_GB_MAT_WATER) ? MTL_RTW_EYE_WATER :
			((m == MTL_GB_MAT_TRANS) || (m == MTL_RTW_MAT_ALIAS)) ? MTL_RTW_EYE_PASS : MTL_RTW_EYE_STOP;

		if (mtl_rtw_eye_step(m) != want)
		{
			ok_eye = 0;
		}
	}

	CHECK(ok_eye);
	CHECK(mtl_rtw_eye_step(MTL_GB_MAT_SKY) == MTL_RTW_EYE_STOP);
	CHECK(mtl_rtw_eye_step(MTL_GB_MAT_WORLD) == MTL_RTW_EYE_STOP);

	/* the eye ray stops short of the raster's surface; with none it looks as far as a map goes */
	CHECK(mtl_rtw_eye_max(0) == MTL_RTW_FAR);
	CHECK(mtl_rtw_eye_max(-5) == MTL_RTW_FAR);
	CHECK(mtl_rtw_eye_max(NAN) == MTL_RTW_FAR);
	CHECK(mtl_rtw_eye_max(100) == 98.0f);
	CHECK(fabsf(mtl_rtw_eye_max(5000) - 4990.0f) < 1e-3f);
	CHECK(mtl_rtw_eye_max(1.5f) <= 0.0f);

	/* the class byte: not water is 0, and the bits come back */
	CHECK(mtl_rtw_class(0, 1, MTL_RTW_LIT) == 0);
	CHECK(mtl_rtw_class(1, 0, MTL_RTW_MISS) == MTL_RTW_CLASS_WATER);
	CHECK(mtl_rtw_class(1, 1, MTL_RTW_SKY) == (MTL_RTW_CLASS_WATER | MTL_RTW_CLASS_TRANS | MTL_RTW_SKY));
	CHECK((mtl_rtw_class(1, 0, MTL_RTW_LIT) & MTL_RTW_CLASS_FROM) == MTL_RTW_LIT);
	CHECK(mtl_rtw_class(1, 1, MTL_RTW_SKY) < 256);

	/* a translucent fragment takes the reflection only where it is the water found at its pixel */
	{
		const unsigned t = mtl_rtw_class(1, 1, MTL_RTW_LIT);

		CHECK(mtl_rtw_is_trans_water(t, 500, 500));
		CHECK(mtl_rtw_is_trans_water(t, 500, 501.9f));
		CHECK(mtl_rtw_is_trans_water(t, 500, 498.1f));
		CHECK(!mtl_rtw_is_trans_water(t, 500, 492));    /* a pane 8 units in front */
		CHECK(!mtl_rtw_is_trans_water(t, 500, 510));    /* a face behind */
		CHECK(mtl_rtw_is_trans_water(t, 4000, 4007));   /* 0.2 % */
		CHECK(!mtl_rtw_is_trans_water(t, 4000, 4009));
		CHECK(!mtl_rtw_is_trans_water(mtl_rtw_class(1, 0, MTL_RTW_LIT), 500, 500));   /* opaque water: the resolve's */
		CHECK(!mtl_rtw_is_trans_water(0, 500, 500));
		CHECK(!mtl_rtw_is_trans_water(t, 0, 0));
	}
}

/* ---- the warp's phase -------------------------------------------------------------------- */

static void
test_turb(void)
{
	float tab[MTL_RTW_TURB_SIZE], m = 0;
	int i, same = 0, near1 = 0, far = 0, n = 0, ok = 1;

	CHECK(rm_rtw_turb_table(tab) == 1);

	for (i = 0; i < MTL_RTW_TURB_SIZE; i++)
	{
		m = (fabsf(rm_turb_sin(i)) > m) ? fabsf(rm_turb_sin(i)) : m;
	}

	for (i = 0; i < MTL_RTW_TURB_SIZE; i++)
	{
		if ((tab[i] < -1) || (tab[i] > 1) || (tab[i] != rm_turb_sin(i) / m))
		{
			ok = 0;
		}
	}

	CHECK(ok);
	CHECK(m > 0);
	CHECK(RM_TURB_SIZE == MTL_RTW_TURB_SIZE);
	CHECK(fabsf((float)(256.0 / (2 * M_PI)) - MTL_RTW_TURBSCALE) < 1e-5f);

	/*
	 * gl1's index (rm_warp_index, in doubles) over the coordinates and
	 * times of a map and a session: the same index, but where the value
	 * lies within a float's error of an integer (a thousandth, and 2.5e-7
	 * of the value: the sum and the product rounded), where it may round
	 * to the next (one step of the table either way).
	 */
	for (i = 0; i < 200000; i++)
	{
		const float c = frand(-8192, 8192), time = frand(0, 7200);
		const double x = ((double)c * 0.125 + time) * (256.0 / (2 * M_PI));
		const double fr = x - floor(x), tol = 1e-3 + fabs(x) * 2.5e-7;
		const int a = mtl_rtw_warp_index(c, time), b = rm_warp_index(c, time);
		const int d = (a - b) & 255;

		n++;

		if (a == b)
		{
			same++;
		}
		else if (((d == 1) || (d == 255)) && ((fr < tol) || (fr > 1 - tol) || (fabs(x) < 1)))
		{
			near1++;
		}
		else
		{
			far++;
		}
	}

	CHECK(far == 0);
	CHECK(same > n - n / 20);
	CHECK(near1 + same == n);

	/* truncation toward 0, then the mask of a two's complement int: -0.5 is 0, -1.5 is 255 */
	CHECK(mtl_rtw_warp_index(0, -0.5f / MTL_RTW_TURBSCALE) == 0);
	CHECK(mtl_rtw_warp_index(0, -1.5f / MTL_RTW_TURBSCALE) == 255);
	CHECK(mtl_rtw_warp_index(0, 300.5f / MTL_RTW_TURBSCALE) == 44);
	CHECK(mtl_rtw_warp_index(NAN, 1) == 0);
	CHECK(mtl_rtw_warp_index(1, INFINITY) == 0);

	/*
	 * The range guard (the value taken modulo 256 before the cast): at
	 * |x| of 2^31 and beyond, where a float's cast to int is undefined in C
	 * (a saturating 255 or 0 on arm64, flagged by UBSan), every float is a
	 * multiple of 256, so the index is 0. The engine's inputs stay below it
	 * (cl.time wraps at 2^31 ms, x below about 9e7); the function is
	 * defined for every finite input all the same. Just below 2^31 the
	 * floats are multiples of 128, the index 128 or 0 with or without it.
	 */
	{
		static const float times[] = { 5.3e7f, 6e7f, 1e8f, 1e9f, 3.0e10f, 1e15f, 1e30f };
		int bad = 0;

		for (i = 0; i < (int)(sizeof(times) / sizeof(times[0])); i++)
		{
			const float xp = (0.125f * 8192.0f + times[i]) * MTL_RTW_TURBSCALE;

			bad += !(fabsf(xp) >= 2147483648.0f);
			bad += mtl_rtw_warp_index(8192.0f, times[i]) != 0;
			bad += mtl_rtw_warp_index(-8192.0f, times[i]) != 0;
			bad += mtl_rtw_warp_index(8192.0f, -times[i]) != 0;
			bad += mtl_rtw_warp_index(0.0f, -times[i]) != 0;
		}

		CHECK(bad == 0);
		CHECK(mtl_rtw_warp_index(0, 2147483648.0f / MTL_RTW_TURBSCALE) == 0);
		CHECK(mtl_rtw_warp_index(0, 2147483520.0f / MTL_RTW_TURBSCALE) ==
			(int)fmod((double)((2147483520.0f / MTL_RTW_TURBSCALE) * MTL_RTW_TURBSCALE), 256.0));
	}
}

/* ---- the direction ----------------------------------------------------------------------- */

static void
test_axes(void)
{
	const mtl_rtw_axes_t f = mtl_rtw_axes(mtl_rtw_v3(0, 0, 1));
	const mtl_rtw_axes_t c = mtl_rtw_axes(mtl_rtw_v3(0, 0, -1));
	const mtl_rtw_axes_t w = mtl_rtw_axes(mtl_rtw_v3(-1, 0, 0));
	const mtl_rtw_axes_t s = mtl_rtw_axes(mtl_rtw_v3(0, 1, 0));
	const mtl_rtw_axes_t tie = mtl_rtw_axes(mtl_rtw_v3(0.70710678f, 0, 0.70710678f));
	int i, ok = 1;

	/* qbsp's base axes: floor and ceiling (1 0 0) (0 -1 0); x walls (0 1 0) (0 0 -1); y walls (1 0 0) (0 0 -1) */
	CHECK((f.s.x == 1) && (f.s.y == 0) && (f.s.z == 0) && (f.t.x == 0) && (f.t.y == -1) && (f.t.z == 0));
	CHECK((c.s.x == 1) && (c.t.y == -1));
	CHECK((w.s.y == 1) && (w.t.z == -1) && (w.s.x == 0));
	CHECK((s.s.x == 1) && (s.t.z == -1) && (s.s.y == 0));
	CHECK((tie.s.x == 1) && (tie.t.y == -1));   /* a tie goes to the floor, the first */

	/* the axes lie in the base face's plane, so never along the normal of a face near it */
	for (i = 0; i < 2000; i++)
	{
		const mtl_rtw_v3_t n = rand_unit();
		const mtl_rtw_axes_t a = mtl_rtw_axes(n);

		if ((fabsf(mtl_rtw_dot(a.s, n)) > 0.8f) || (fabsf(mtl_rtw_dot(a.t, n)) > 0.8f) ||
			(mtl_rtw_dot(a.s, a.t) != 0))
		{
			ok = 0;
		}
	}

	CHECK(ok);
}

static void
test_dir(void)
{
	/* the axes projected into a sloped plane need not stay orthogonal: |ps s + pt t| is at most |ps| + |pt| */
	const float amp = 0.08f, maxtilt = atanf(amp * 2.0f);
	int i, ok_unit = 1, ok_tilt = 1, ok_mirror = 1, ok_up = 1, ok_flat = 1, ok_moves = 0;

	/* amp 0 (or below, or not a number): the flat normal itself */
	{
		const mtl_rtw_v3_t n = mtl_rtw_v3(0, 0, 1);
		const mtl_rtw_v3_t a = mtl_rtw_normal(n, mtl_rtw_axes(n), 1, -1, 0);
		const mtl_rtw_v3_t b = mtl_rtw_normal(n, mtl_rtw_axes(n), 1, -1, NAN);

		CHECK((a.x == 0) && (a.y == 0) && (a.z == 1));
		CHECK((b.x == 0) && (b.y == 0) && (b.z == 1));
	}

	for (i = 0; i < 20000; i++)
	{
		mtl_rtw_v3_t n = rand_unit(), d = rand_unit();
		const float ps = frand(-1, 1), pt = frand(-1, 1);
		mtl_rtw_v3_t np, r, r0, m;

		/* the plane faces the eye: d . n < 0 */
		if (mtl_rtw_dot(d, n) > 0)
		{
			d = mtl_rtw_v3(-d.x, -d.y, -d.z);
		}

		np = mtl_rtw_normal(n, mtl_rtw_axes(n), ps, pt, amp);
		r = mtl_rtw_reflect(d, np, n);
		r0 = mtl_rtw_reflect(d, n, n);
		m = mtl_rtw_mad(d, -2.0f * mtl_rtw_dot(d, n), n);

		/* unit vectors, the tilt within amp's bound */
		if ((fabsf(len3(np) - 1) > 1e-5f) || (fabsf(len3(r) - 1) > 1e-5f))
		{
			ok_unit = 0;
		}

		if (acosf(fminf(1.0f, mtl_rtw_dot(np, n))) > maxtilt + 1e-4f)
		{
			ok_tilt = 0;
		}

		/* never below MTL_RTW_MIN_COS above the flat plane */
		if (mtl_rtw_dot(r, n) < MTL_RTW_MIN_COS - 1e-6f)
		{
			ok_up = 0;
		}

		/* with the flat normal and a view not grazing, the mirror direction */
		if ((mtl_rtw_dot(m, n) >= MTL_RTW_MIN_COS) &&
			((fabsf(r0.x - m.x) > 1e-5f) || (fabsf(r0.y - m.y) > 1e-5f) || (fabsf(r0.z - m.z) > 1e-5f)))
		{
			ok_mirror = 0;
		}

		/* a grazing one is lifted to the minimum in its own vertical plane */
		if (mtl_rtw_dot(m, n) < MTL_RTW_MIN_COS)
		{
			const mtl_rtw_v3_t h = mtl_rtw_mad(m, -mtl_rtw_dot(m, n), n), hr = mtl_rtw_mad(r0, -mtl_rtw_dot(r0, n), n);
			const float c = mtl_rtw_dot(h, hr) / (len3(h) * len3(hr));

			if ((fabsf(mtl_rtw_dot(r0, n) - MTL_RTW_MIN_COS) > 1e-5f) || (len3(h) > 1e-3f && c < 0.9999f))
			{
				ok_flat = 0;
			}
		}

		if (fabsf(mtl_rtw_dot(r, r0)) < 0.9999f)
		{
			ok_moves = 1;
		}
	}

	CHECK(ok_unit);
	CHECK(ok_tilt);
	CHECK(ok_up);
	CHECK(ok_mirror);
	CHECK(ok_flat);
	CHECK(ok_moves);   /* the warp does move the reflection */

	/* straight down at a floor: straight back up; the tilt follows the phase's sign */
	{
		const mtl_rtw_v3_t n = mtl_rtw_v3(0, 0, 1), d = mtl_rtw_v3(0, 0, -1);
		const mtl_rtw_v3_t r = mtl_rtw_reflect(d, n, n);
		const mtl_rtw_v3_t np = mtl_rtw_normal(n, mtl_rtw_axes(n), 1, 0, 0.1f);

		CHECK((r.x == 0) && (r.y == 0) && (r.z == 1));
		CHECK((np.x > 0.099f * 0.99f) && (fabsf(np.y) < 1e-6f) && (np.z > 0.99f));
		CHECK(mtl_rtw_normal(n, mtl_rtw_axes(n), 0, 1, 0.1f).y < -0.09f);   /* t is (0 -1 0) on a floor */
	}
}

/*
 * A ray from the water never meets the water: for opaque water the
 * point comes from the depth, within four float steps of the true one
 * along the eye's ray, the normal from rgb10a2. The start, moved along the
 * stored normal by mtl_rtao_bias, is above the true plane, and the
 * reflected direction (any phase) climbs away from it, so the ray's
 * signed distance to the plane only grows. For translucent water the
 * point is the eye ray's hit and the normal the triangle's: the same.
 */
static void
test_no_self_hit(void)
{
	const float znear = 4, k = 4.0f * (1.0f / 16777216.0f) / znear;
	int i, starts = 0, below = 0, down = 0;

	for (i = 0; i < 50000; i++)
	{
		const mtl_rtw_v3_t n = rand_unit();
		const mtl_rtw_v3_t p = mtl_rtw_v3(frand(-4000, 4000), frand(-4000, 4000), frand(-4000, 4000));
		const float dist = frand(8, 3000);
		mtl_rtw_v3_t dir = rand_unit(), eye, pd, nq, o, r;
		float err, steps;
		mtl_rgba_t q;

		/* the eye on the plane's front, at dist, at any angle above 2 degrees */
		if (mtl_rtw_dot(dir, n) > 0)
		{
			dir = mtl_rtw_v3(-dir.x, -dir.y, -dir.z);
		}

		if (mtl_rtw_dot(dir, n) > -0.035f)
		{
			continue;
		}

		eye = mtl_rtw_mad(p, -dist, dir);

		/* the depth's point: up to four steps (2^-24 dist^2 / znear each) along the eye's ray */
		steps = frand(-4, 4);
		err = steps * dist * dist * (1.0f / 16777216.0f) / znear;
		pd = mtl_rtw_mad(p, err, dir);

		/* the normal as the G-buffer keeps it */
		q = mtl_gb_normal(n.x, n.y, n.z);
		nq = mtl_rtw_unit(mtl_rtw_v3(roundf((q.r) * 1023) / 1023 * 2 - 1, roundf((q.g) * 1023) / 1023 * 2 - 1,
				roundf((q.b) * 1023) / 1023 * 2 - 1));

		if (mtl_rtw_dot(nq, mtl_rtw_v3(eye.x - pd.x, eye.y - pd.y, eye.z - pd.z)) < 0)
		{
			nq = mtl_rtw_v3(-nq.x, -nq.y, -nq.z);
		}

		o = mtl_rtw_mad(pd, mtl_rtao_bias(len3(mtl_rtw_v3(pd.x - eye.x, pd.y - eye.y, pd.z - eye.z)), k), nq);
		r = mtl_rtw_reflect(mtl_rtw_unit(mtl_rtw_v3(pd.x - eye.x, pd.y - eye.y, pd.z - eye.z)),
				mtl_rtw_normal(nq, mtl_rtw_axes(nq), frand(-1, 1), frand(-1, 1), 0.08f), nq);
		starts++;

		if (mtl_rtw_dot(mtl_rtw_v3(o.x - p.x, o.y - p.y, o.z - p.z), n) <= 0)
		{
			below++;
		}

		if (mtl_rtw_dot(r, n) <= 0)
		{
			down++;
		}
	}

	CHECK(starts > 40000);
	CHECK(below == 0);
	CHECK(down == 0);
}

/* ---- the sky lookup's choice ------------------------------------------------------------- */

static void
test_choose(void)
{
	int hit;

	/* off the screen: nothing, whatever was met */
	for (hit = 0; hit < 3; hit++)
	{
		CHECK(mtl_rtw_choose(hit, 0, MTL_GB_MAT_SKY, 0, 100) == MTL_RTW_MISS);
		CHECK(mtl_rtw_choose(hit, 0, MTL_GB_MAT_WORLD, 0, 100) == MTL_RTW_MISS);
	}

	/* the sky (or nothing met): the sky's texel where the screen shows the sky in that direction */
	CHECK(mtl_rtw_choose(1, 1, MTL_GB_MAT_SKY, -1, 0) == MTL_RTW_SKY);
	CHECK(mtl_rtw_choose(0, 1, MTL_GB_MAT_SKY, -1, 0) == MTL_RTW_SKY);
	CHECK(mtl_rtw_choose(1, 1, MTL_GB_MAT_WORLD, 0, 0) == MTL_RTW_MISS);   /* a wall hides that sky */
	CHECK(mtl_rtw_choose(1, 1, MTL_GB_MAT_NONE, 0, 0) == MTL_RTW_MISS);
	CHECK(mtl_rtw_choose(0, 1, MTL_GB_MAT_WATER, 0, 0) == MTL_RTW_MISS);

	/* a face: the lit pixel where it is the one seen there */
	CHECK(mtl_rtw_choose(2, 1, MTL_GB_MAT_WORLD, 0, 100) == MTL_RTW_LIT);
	CHECK(mtl_rtw_choose(2, 1, MTL_GB_MAT_ALIAS, 3.9f, 100) == MTL_RTW_LIT);
	CHECK(mtl_rtw_choose(2, 1, MTL_GB_MAT_WATER, 1, 100) == MTL_RTW_LIT);   /* its stock colour: one bounce */
	CHECK(mtl_rtw_choose(2, 1, MTL_GB_MAT_WORLD, 4.1f, 100) == MTL_RTW_MISS);   /* something nearer hides it */
	CHECK(mtl_rtw_choose(2, 1, MTL_GB_MAT_WORLD, 19, 2000) == MTL_RTW_LIT);    /* 1 % far off */
	CHECK(mtl_rtw_choose(2, 1, MTL_GB_MAT_WORLD, 21, 2000) == MTL_RTW_MISS);
	CHECK(mtl_rtw_choose(2, 1, MTL_GB_MAT_WORLD, -1, 100) == MTL_RTW_MISS);   /* no depth there */
	CHECK(mtl_rtw_choose(2, 1, MTL_GB_MAT_WORLD, NAN, 100) == MTL_RTW_MISS);
	CHECK(mtl_rtw_choose(2, 1, MTL_GB_MAT_SKY, 0, 100) == MTL_RTW_MISS);    /* the sky box, not the face */
	CHECK(mtl_rtw_choose(2, 1, MTL_GB_MAT_NONE, 0, 100) == MTL_RTW_MISS);
	CHECK(mtl_rtw_seen_tol(10) == 4.0f);
	CHECK(mtl_rtw_seen_tol(1000) == 10.0f);
}

/* ---- the blend --------------------------------------------------------------------------- */

static int
same_bits(mtl_rgba_t a, mtl_rgba_t b)
{
	return memcmp(&a, &b, sizeof(a)) == 0;
}

static void
test_blend(void)
{
	int i, ok_keep = 1, ok_alpha = 1, ok_mix = 1;

	for (i = 0; i < 20000; i++)
	{
		mtl_rgba_t c, o;
		const float rr = frand(0, 1), rg = frand(0, 1), rb = frand(0, 1), k = frand(0, 1);
		const float alphas[3] = { 0.33f, 0.66f, 1.0f };

		c.r = roundf(frand(0, 255)) / 255;
		c.g = roundf(frand(0, 255)) / 255;
		c.b = roundf(frand(0, 255)) / 255;
		c.a = alphas[i % 3];

		/* no reflection, or a weight of 0: the stock colour bit for bit */
		if (!same_bits(mtl_rtw_blend(c, rr, rg, rb, k, MTL_RTW_MISS), c) ||
			!same_bits(mtl_rtw_blend(c, rr, rg, rb, 0, MTL_RTW_LIT), c) ||
			!same_bits(mtl_rtw_blend(c, rr, rg, rb, -1, MTL_RTW_SKY), c) ||
			!same_bits(mtl_rtw_blend(c, rr, rg, rb, NAN, MTL_RTW_LIT), c))
		{
			ok_keep = 0;
		}

		o = mtl_rtw_blend(c, rr, rg, rb, k, (i & 1) ? MTL_RTW_LIT : MTL_RTW_SKY);

		/* SURF_TRANS's alpha stays */
		if (o.a != c.a)
		{
			ok_alpha = 0;
		}

		if ((fabsf(o.r - (c.r * (1 - k) + rr * k)) > 1e-6f) || (fabsf(o.g - (c.g * (1 - k) + rg * k)) > 1e-6f) ||
			(fabsf(o.b - (c.b * (1 - k) + rb * k)) > 1e-6f))
		{
			ok_mix = 0;
		}
	}

	CHECK(ok_keep);
	CHECK(ok_alpha);
	CHECK(ok_mix);

	/* the weight is clamped to 1: the reflection alone */
	{
		mtl_rgba_t c, o;

		c.r = 0.2f;
		c.g = 0.4f;
		c.b = 0.6f;
		c.a = 0.33f;
		o = mtl_rtw_blend(c, 1, 0, 0.5f, 7, MTL_RTW_LIT);
		CHECK((o.r == 1) && (o.g == 0) && (o.b == 0.5f) && (o.a == 0.33f));
		o = mtl_rtw_blend(c, 1, 0, 0.5f, 0.5f, MTL_RTW_LIT);
		CHECK((fabsf(o.r - 0.6f) < 1e-6f) && (fabsf(o.g - 0.2f) < 1e-6f) && (fabsf(o.b - 0.55f) < 1e-6f));
		CHECK(mtl_rtw_weight(0.5f) == 0.5f);
		CHECK(mtl_rtw_weight(2) == 1);
		CHECK(mtl_rtw_weight(NAN) == 0);
	}
}

/* ---- the cvars, the view, the instances, the stats --------------------------------------- */

static void
test_cvars(void)
{
	CHECK(rm_rtw_on(1, 1, 10) == 1);
	CHECK(rm_rtw_on(0, 1, 10) == 0);
	CHECK(rm_rtw_on(1, 0, 10) == 0);
	CHECK(rm_rtw_on(1, 1, 0) == 0);    /* a map without water stays on the stock path */
	CHECK(rm_rtw_on(NAN, 1, 10) == 0);
	CHECK(rm_rtw_on(-1, 1, 10) == 1);

	CHECK(rm_rtw_amp(0.08f) == 0.08f);
	CHECK(rm_rtw_amp(-1) == 0);
	CHECK(rm_rtw_amp(5) == RM_RTW_NORMAL_MAX);
	CHECK(rm_rtw_amp(NAN) == RM_RTW_NORMAL);
	CHECK(RM_RTW_NORMAL == 0.08f);

	CHECK(rm_rtw_reflect(0.5f) == 0.5f);
	CHECK(rm_rtw_reflect(-1) == 0);
	CHECK(rm_rtw_reflect(3) == 1);
	CHECK(rm_rtw_reflect(INFINITY) == RM_RTW_REFLECT);
	CHECK(RM_RTW_REFLECT == 0.5f);

	CHECK(rm_rtw_show(0) == 0);
	CHECK(rm_rtw_show(1) == 1);
	CHECK(rm_rtw_show(2.9f) == 2);
	CHECK(rm_rtw_show(9) == RM_RTW_SHOW_MAX);
	CHECK(rm_rtw_show(-1) == 0);
	CHECK(rm_rtw_show(NAN) == 0);

	/* TASK158: r_rt_water_hits, 0 TASK58's screen-space path, 1 world space from 1 up */
	CHECK(rm_rtw_hits(0) == 0);
	CHECK(rm_rtw_hits(0.9f) == 0);
	CHECK(rm_rtw_hits(1) == 1);
	CHECK(rm_rtw_hits(2.5f) == 1);
	CHECK(rm_rtw_hits(-1) == 0);
	CHECK(rm_rtw_hits(NAN) == 0);
	CHECK(rm_rtw_hits(INFINITY) == 0);
}

static void
test_view(void)
{
	float proj[16], world[16], id[16];
	rm_viewport_t vp = { 3, 5, 640, 480, 0, 1 };
	const float eye[3] = { 10, 20, 30 };
	rm_rtw_view_t v;
	rm_rts_view_t s;
	int i, ok = 1;

	mtl_mat_frustum(proj, -4, 4, -3, 3, 4, 4096);
	mtl_mat_identity(world);
	mtl_mat_rotate(world, -90, 1, 0, 0);
	mtl_mat_rotate(world, 90, 0, 0, 1);
	mtl_mat_translate(world, -eye[0], -eye[1], -eye[2]);

	CHECK(rm_rtw_view(proj, world, &vp, eye, 4, 12.5f, 0.08f, 0.5f, 2, &v) == 1);
	CHECK(rm_rts_view(proj, world, &vp, eye, 4, &s) == 1);
	CHECK(memcmp(v.inv, s.inv, sizeof(v.inv)) == 0);
	CHECK(memcmp(v.vp, s.vp, sizeof(v.vp)) == 0);
	CHECK(memcmp(v.eye, s.eye, sizeof(v.eye)) == 0);
	CHECK(v.bias_k == s.bias_k);
	CHECK((v.time == 12.5f) && (v.amp == 0.08f) && (v.reflect == 0.5f) && (v.show == 2));
	CHECK((v.env == 0) && (v.scale == 0) && (v.lod_k == 0) && (v.nlights == 0) && (v.sky == 0) && (v.fullbright == 0));

	/* the projection is the matrix the inverse inverts: their product is the identity */
	mtl_mat_mul(id, v.mvp, v.inv);

	for (i = 0; i < 16; i++)
	{
		if (fabsf(id[i] - (((i % 5) == 0) ? 1.0f : 0.0f)) > 1e-3f)
		{
			ok = 0;
		}
	}

	CHECK(ok);

	CHECK(rm_rtw_view(proj, world, &vp, eye, 4, NAN, 0.08f, 0.5f, 0, &v) == 1);
	CHECK(v.time == 0);
	CHECK(rm_rtw_view(proj, world, &vp, eye, 0, 1, 0.08f, 0.5f, 0, &v) == 0);
	CHECK(sizeof(rm_rtw_view_t) == 288);
}

static void
test_instances(void)
{
	rm_rt_ent_t e[4];
	rm_rt_inst_t out[8];
	rm_rt_excl_t ex;
	int n, i;

	memset(e, 0, sizeof(e));

	for (i = 0; i < 4; i++)
	{
		e[i].kind = RM_RT_ENT_ALIAS;
		e[i].model = 1 + i;
		e[i].alpha = 1;
	}

	e[1].flags = RM_RF_WEAPONMODEL;
	e[2].flags = RM_RT_RF_VIEWERMODEL;
	e[3].kind = RM_RT_ENT_BRUSH;

	/* with r_rt_gunshadow 1 the gun is an instance: reflection rays pass it, and the viewer's own model */
	n = rm_rt_instances(e, 4, 1, 1, 0, out, 8, &ex);
	CHECK(n == 5);

	for (i = 0; i < n; i++)
	{
		const int gun = (out[i].ent == 1), self = (out[i].ent == 2);
		unsigned carry;

		/* rmb_rt_frame's mask: the shadows', with the AO bit, with the reflections' bit */
		for (carry = 0; carry < 2; carry++)
		{
			const unsigned ao = mtl_rtao_inst_mask(mtl_rts_inst_mask(carry), out[i].noao);
			const unsigned m = mtl_rtw_inst_mask(ao, out[i].noao);

			CHECK(((m & MTL_RTW_MASK) != 0) == !(gun || self));

			/* the other rays' bits as before: the shadow rays' and the AO rays' queries meet what they met */
			CHECK((m & ~MTL_RTW_MASK) == ao);
			CHECK((m & (MTL_RTS_MASK_OTHER | MTL_RTS_MASK_CARRIER)) == mtl_rts_inst_mask(carry));
			CHECK(((m & MTL_RTAO_MASK) != 0) == !(gun || self));
			CHECK(((m & 0xffu) != 0) == ((ao & 0xffu) != 0));
		}
	}

	/* the world (ent -1), the brush entity and the other model are met */
	CHECK((out[0].ent == -1) &&
		((mtl_rtw_inst_mask(mtl_rtao_inst_mask(mtl_rts_inst_mask(0), out[0].noao), out[0].noao) & MTL_RTW_MASK) != 0));

	/* without it the gun is no instance at all */
	n = rm_rt_instances(e, 4, 1, 0, 0, out, 8, &ex);
	CHECK((n == 4) && (ex.weapon == 1));

	/* the reflections' bit is one bit of the eight, its own: not the AO rays' nor the shadow rays' */
	CHECK((MTL_RTW_MASK != 0) && ((MTL_RTW_MASK & (MTL_RTW_MASK - 1)) == 0) && (MTL_RTW_MASK <= 0x80u));
	CHECK((MTL_RTW_MASK & (MTL_RTAO_MASK | MTL_RTS_MASK_OTHER | MTL_RTS_MASK_CARRIER)) == 0);
}

static void
test_stats(void)
{
	rm_rtw_stats_t s;
	char buf[RM_STATS_LINE], small[16];

	memset(&s, 0, sizeof(s));
	s.frame = 9;
	s.pixels = 2073600;
	s.listed = 301200;
	s.opaque = 1200;
	s.trans = 300000;
	s.eye = 300000;
	s.lit = 200000;
	s.world = 40000;
	s.sky = 60000;
	s.skybox = 1000;
	s.miss = 200;
	s.ms = 0.875;

	CHECK(rm_rtw_stats_format(&s, buf, sizeof(buf)) == 1);
	CHECK(strcmp(buf, "ref_metal: rt water: frame 9: 2073600 pixels, 301200 listed, 1200 opaque water, "
			"300000 translucent water, 300000 eye rays, 200000 lit, 40000 world, 60000 sky, 1000 sky box, 200 none, "
			"0.875 ms") == 0);
	CHECK(strlen(buf) < RM_STATS_LINE);
	CHECK(strchr(buf, '\n') == NULL);

	/* TASK158: r_rt_water_hits 1 adds its passes' split and the span from the raster's start to the trace's end */
	s.ws = 1;
	s.raster_ms = 0.25;
	s.compact_ms = 0.125;
	s.trace_ms = 0.5;
	s.span_ms = 1.5;
	CHECK(rm_rtw_stats_format(&s, buf, sizeof(buf)) == 1);
	CHECK(strstr(buf, ", 0.875 ms, world space: raster 0.250, compaction 0.125, trace 0.500, span 1.500 ms") != NULL);
	CHECK(strlen(buf) < RM_STATS_LINE);
	s.raster_ms = s.compact_ms = s.trace_ms = s.span_ms = -1;
	CHECK(rm_rtw_stats_format(&s, buf, sizeof(buf)) == 1);
	CHECK(strstr(buf, "world space: raster -, compaction -, trace -, span - ms") != NULL);
	s.ws = 0;

	s.ms = -1;
	CHECK(rm_rtw_stats_format(&s, buf, sizeof(buf)) == 1);
	CHECK(strstr(buf, ", - ms") != NULL);
	CHECK(strstr(buf, "world space") == NULL);
	CHECK(rm_rtw_stats_format(&s, small, sizeof(small)) == 0);
	CHECK(small[0] == 0);
	CHECK(rm_rtw_stats_format(&s, small, 0) == 0);
}

/* TASK158: the choice with world-space shading behind the picture */
static void
test_choose_ws(void)
{
	/* the picture's colour wherever the screen-space choice finds one, whatever the world could do */
	CHECK(mtl_rtw_choose_ws(2, 1, MTL_GB_MAT_WORLD, 1.0f, 100.0f, 1, 1) == MTL_RTW_LIT);
	CHECK(mtl_rtw_choose_ws(2, 1, MTL_GB_MAT_WORLD, 1.0f, 100.0f, 0, 0) == MTL_RTW_LIT);
	CHECK(mtl_rtw_choose_ws(1, 1, MTL_GB_MAT_SKY, -1.0f, 0.0f, 1, 1) == MTL_RTW_SKY);
	CHECK(mtl_rtw_choose_ws(0, 1, MTL_GB_MAT_SKY, -1.0f, 0.0f, 0, 1) == MTL_RTW_SKY);

	/* a face off the screen or hidden: in world space where it can be shaded, else none */
	CHECK(mtl_rtw_choose_ws(2, 0, MTL_GB_MAT_NONE, -1.0f, 100.0f, 1, 0) == (MTL_RTW_LIT | MTL_RTW_WS));
	CHECK(mtl_rtw_choose_ws(2, 1, MTL_GB_MAT_WORLD, 50.0f, 100.0f, 1, 0) == (MTL_RTW_LIT | MTL_RTW_WS));
	CHECK(mtl_rtw_choose_ws(2, 1, MTL_GB_MAT_SKY, 0.0f, 100.0f, 1, 0) == (MTL_RTW_LIT | MTL_RTW_WS));
	CHECK(mtl_rtw_choose_ws(2, 0, MTL_GB_MAT_NONE, -1.0f, 100.0f, 0, 1) == MTL_RTW_MISS);
	CHECK(mtl_rtw_choose_ws(2, 1, MTL_GB_MAT_WORLD, 50.0f, 100.0f, 0, 1) == MTL_RTW_MISS);

	/* the sky brush or nothing met, where the screen does not show the sky: the box, or none without it */
	CHECK(mtl_rtw_choose_ws(1, 0, MTL_GB_MAT_NONE, -1.0f, 0.0f, 1, 1) == (MTL_RTW_SKY | MTL_RTW_WS));
	CHECK(mtl_rtw_choose_ws(0, 1, MTL_GB_MAT_WORLD, -1.0f, 0.0f, 0, 1) == (MTL_RTW_SKY | MTL_RTW_WS));
	CHECK(mtl_rtw_choose_ws(1, 0, MTL_GB_MAT_NONE, -1.0f, 0.0f, 1, 0) == MTL_RTW_MISS);

	/* the class keeps the world-space bit; the blend takes such a colour; the candidates' byte has no water */
	CHECK(mtl_rtw_class(1, 0, MTL_RTW_LIT | MTL_RTW_WS) == (MTL_RTW_CLASS_WATER | MTL_RTW_LIT | MTL_RTW_CLASS_WS));
	CHECK(mtl_rtw_class(1, 1, MTL_RTW_SKY | MTL_RTW_WS) ==
			(MTL_RTW_CLASS_WATER | MTL_RTW_CLASS_TRANS | MTL_RTW_SKY | MTL_RTW_CLASS_WS));
	CHECK(mtl_rtw_class(0, 1, MTL_RTW_LIT | MTL_RTW_WS) == 0);
	CHECK((mtl_rtw_class(1, 0, MTL_RTW_LIT | MTL_RTW_WS) & MTL_RTW_CLASS_FROM) == MTL_RTW_LIT);
	CHECK(!(MTL_RTW_CLASS_CAND & (MTL_RTW_CLASS_WATER | MTL_RTW_CLASS_TRANS | MTL_RTW_CLASS_FROM | MTL_RTW_CLASS_WS)));
	CHECK(MTL_RTW_CLASS_CAND < 256u);
	CHECK(!mtl_rtw_is_trans_water(MTL_RTW_CLASS_CAND, 100.0f, 100.0f));

	{
		mtl_rgba_t c = { 0.25f, 0.5f, 0.75f, 0.33f }, o;

		o = mtl_rtw_blend(c, 1.0f, 1.0f, 1.0f, 0.5f, MTL_RTW_LIT | MTL_RTW_WS);
		CHECK((o.r == 0.625f) && (o.g == 0.75f) && (o.b == 0.875f) && (o.a == 0.33f));
		o = mtl_rtw_blend(c, 1.0f, 1.0f, 1.0f, 0.5f, MTL_RTW_SKY | MTL_RTW_WS);
		CHECK(o.r == 0.625f);
	}
}

/* TASK158: the shading record, the barycentrics, the mip level, the list */
static void
test_shade_record(void)
{
	unsigned s;
	int ok = 1;

	for (unsigned tex = 0; tex < 0x1000u; tex += 37)
	{
		for (unsigned slice = 0; slice < 256u; slice += 11)
		{
			for (unsigned f = 0; f < 16u; f++)
			{
				s = mtl_rtw_shade_pack(tex, slice, f);

				if ((mtl_rtw_shade_tex(s) != tex) || (mtl_rtw_shade_slice(s) != slice) || (mtl_rtw_shade_flags(s) != f))
				{
					ok = 0;
				}
			}
		}
	}

	CHECK(ok);
	CHECK(mtl_rtw_shade_ok(mtl_rtw_shade_pack(0, MTL_RTW_SHADE_NOLM, 0)));
	CHECK(mtl_rtw_shade_ok(mtl_rtw_shade_pack(MTL_RTW_TEX_WORLD - 1, 3, MTL_RTW_SF_WARP | MTL_RTW_SF_FLOW)));
	CHECK(!mtl_rtw_shade_ok(mtl_rtw_shade_pack(MTL_RTW_SHADE_NOTEX, 3, 0)));
	CHECK(!mtl_rtw_shade_ok(mtl_rtw_shade_pack(MTL_RTW_TEX_WORLD, 3, 0)));
	CHECK(!mtl_rtw_shade_ok(mtl_rtw_shade_pack(5, 3, MTL_RTW_SF_SKY)));
	CHECK(MTL_RTW_TEX_COUNT == MTL_RTW_TEX_SKY + 6u);

	/* an attribute at the vertices, the edges' middles and the centroid */
	CHECK(mtl_rtw_bary(1.0f, 2.0f, 4.0f, 0.0f, 0.0f) == 1.0f);
	CHECK(mtl_rtw_bary(1.0f, 2.0f, 4.0f, 1.0f, 0.0f) == 2.0f);
	CHECK(mtl_rtw_bary(1.0f, 2.0f, 4.0f, 0.0f, 1.0f) == 4.0f);
	CHECK(mtl_rtw_bary(1.0f, 2.0f, 4.0f, 0.5f, 0.5f) == 3.0f);
	CHECK(fabsf(mtl_rtw_bary(1.0f, 2.0f, 4.0f, 1.0f / 3.0f, 1.0f / 3.0f) - 7.0f / 3.0f) < 1e-6f);

	/* the mip level: a texel or less is 0, then log2 of the footprint, bounded; not finite is 0 */
	CHECK(mtl_rtw_lod(100.0f, 0.001f) == 0.0f);
	CHECK(mtl_rtw_lod(1000.0f, 0.001f) == 0.0f);
	CHECK(fabsf(mtl_rtw_lod(2000.0f, 0.001f) - 1.0f) < 1e-5f);
	CHECK(fabsf(mtl_rtw_lod(8000.0f, 0.001f) - 3.0f) < 1e-4f);
	CHECK(mtl_rtw_lod(1e9f, 1.0f) == MTL_RTW_LOD_MAX);
	CHECK(mtl_rtw_lod(NAN, 1.0f) == 0.0f);
	CHECK(mtl_rtw_lod(100.0f, NAN) == 0.0f);
	CHECK(mtl_rtw_lod(-100.0f, 1.0f) == 0.0f);
	CHECK(mtl_rtw_lod(INFINITY, 1.0f) == MTL_RTW_LOD_MAX);

	/* the trace's threadgroups and the list's entries */
	CHECK(mtl_rtw_groups(0) == 0);
	CHECK(mtl_rtw_groups(1) == 1);
	CHECK(mtl_rtw_groups(MTL_RTW_LIST_GROUP) == 1);
	CHECK(mtl_rtw_groups(MTL_RTW_LIST_GROUP + 1) == 2);
	CHECK(mtl_rtw_groups(5120u * 2880u) == (5120u * 2880u) / MTL_RTW_LIST_GROUP);
	CHECK(mtl_rtw_list_pack(0, 0) == 0);
	CHECK(mtl_rtw_list_pack(5119, 2879) == (5119u | (2879u << 16)));
	CHECK((mtl_rtw_list_pack(16383, 16383) & 0xffffu) == 16383u);
	CHECK((mtl_rtw_list_pack(16383, 16383) >> 16) == 16383u);

	/* the list: opaque water, or a mark in the nine; nothing else */
	CHECK(mtl_rtw_listed(MTL_GB_MAT_WATER, 0));
	CHECK(mtl_rtw_listed(MTL_GB_MAT_WORLD, 1));
	CHECK(mtl_rtw_listed(MTL_GB_MAT_NONE, 9));
	CHECK(!mtl_rtw_listed(MTL_GB_MAT_WORLD, 0));
	CHECK(!mtl_rtw_listed(MTL_GB_MAT_SKY, 0));
	CHECK(!mtl_rtw_listed(MTL_GB_MAT_TRANS, 0));
}

/* TASK158: a face's colour in world space is the stock product of its kind */
static void
test_face_color(void)
{
	const mtl_rgba_t a = { 0.4f, 0.6f, 0.8f, 1.0f }, l = { 0.5f, 0.25f, 0.75f, 1.0f };
	const mtl_rgba_t warp = { 0.5f, 0.5f, 0.5f, 1.0f }, trans = { 0.7f, 0.7f, 0.7f, 0.33f };
	mtl_rgba_t c, r;
	int env;

	for (env = 0; env < 4; env++)
	{
		const float scale = (env == 1) ? 2.0f : 1.0f;

		c = mtl_rtw_face_color(a, l, 0, env, scale, warp, trans, 0);
		r = mtl_gb_resolve(a, l, MTL_GB_MAT_WORLD, env, scale);
		CHECK(memcmp(&c, &r, sizeof(c)) == 0);
		c = mtl_rtw_face_color(a, l, MTL_RTW_SF_FLOW, env, scale, warp, trans, 0);
		CHECK(memcmp(&c, &r, sizeof(c)) == 0);
	}

	/* the warp's colour, as the resolve shades water; the translucent faces' inverse intensity */
	c = mtl_rtw_face_color(a, l, MTL_RTW_SF_WARP, 0, 1.0f, warp, trans, 0);
	r = mtl_gb_resolve(a, warp, MTL_GB_MAT_WATER, 0, 1.0f);
	CHECK(memcmp(&c, &r, sizeof(c)) == 0);
	c = mtl_rtw_face_color(a, l, MTL_RTW_SF_WARP | MTL_RTW_SF_TRANS, 0, 1.0f, warp, trans, 0);
	CHECK(memcmp(&c, &r, sizeof(c)) == 0);
	c = mtl_rtw_face_color(a, l, MTL_RTW_SF_TRANS, 0, 1.0f, warp, trans, 0);
	r = mtl_world_color(trans, a);
	CHECK(memcmp(&c, &r, sizeof(c)) == 0);

	/* r_fullbright: the texel alone */
	c = mtl_rtw_face_color(a, l, 0, 0, 1.0f, warp, trans, 1);
	CHECK(memcmp(&c, &a, sizeof(c)) == 0);
	c = mtl_rtw_face_color(a, l, MTL_RTW_SF_WARP, 0, 1.0f, warp, trans, 1);
	CHECK(memcmp(&c, &a, sizeof(c)) == 0);
}

/* TASK158: the sky box's texel in a direction is the one gl1 draws there */
static void
test_sky(void)
{
	int face, ok_face = 1, ok_st = 1, ok_dir = 1, ok_rows = 1, n = 0;
	float rows[12];
	const float axis[3] = { 0.0f, 0.6f, 0.8f }, zero[3] = { 0, 0, 0 };

	/* every face's inner points as rm_sky_vec places them */
	for (face = 0; face < 6; face++)
	{
		for (int i = 0; i <= 20; i++)
		{
			for (int j = 0; j <= 20; j++)
			{
				const float s = -0.95f + 1.9f * (float)i / 20.0f, t = -0.95f + 1.9f * (float)j / 20.0f;
				float v[3], st[2];
				mtl_rtw_sky_t k;

				if (!rm_sky_vec(s, t, face, 0, v, st))
				{
					ok_face = 0;
					continue;
				}

				k = mtl_rtw_sky_st(mtl_rtw_v3(v[0], v[1], v[2]));
				n++;

				if (!k.ok || (k.face != face))
				{
					ok_face = 0;
				}
				else if ((fabsf(k.s - st[0]) > 1e-5f) || (fabsf(k.t - st[1]) > 1e-5f))
				{
					ok_st = 0;
				}
			}
		}
	}

	CHECK(n == 6 * 21 * 21);
	CHECK(ok_face);
	CHECK(ok_st);
	CHECK(!mtl_rtw_sky_st(mtl_rtw_v3(0, 0, 0)).ok);
	CHECK(!mtl_rtw_sky_st(mtl_rtw_v3(NAN, 0, 0)).ok);

	/* the clamp: a direction on a face's edge is inside 1/512 .. 511/512 */
	{
		const mtl_rtw_sky_t k = mtl_rtw_sky_st(mtl_rtw_v3(1.0f, 1.0f - 1e-6f, -1.0f + 1e-6f));

		CHECK(k.ok && (k.s >= 1.0f / 512.0f) && (k.s <= 511.0f / 512.0f) && (k.t >= 1.0f / 512.0f) &&
				(k.t <= 511.0f / 512.0f));
	}

	/* the rotation: rm_sky_matrix's about the origin, a direction turned back into the box's frame */
	rm_rtw_sky_rows(12.5f, 10.0f, axis, rows);

	{
		float m[16], id[16];
		const float o[3] = { 0, 0, 0 };

		mtl_mat_identity(id);
		rm_sky_matrix(m, id, o, 12.5f, 10.0f, axis);

		for (int r = 0; r < 3; r++)
		{
			for (int c = 0; c < 3; c++)
			{
				if (fabsf(rows[r * 4 + c] - m[c * 4 + r]) > 1e-6f)
				{
					ok_rows = 0;
				}
			}

			if (rows[r * 4 + 3] != 0.0f)
			{
				ok_rows = 0;
			}
		}

		for (int i = 0; i < 1000; i++)
		{
			const mtl_rtw_v3_t b = rand_unit();
			const mtl_rtw_v3_t w = mtl_rtw_v3(m[0] * b.x + m[4] * b.y + m[8] * b.z, m[1] * b.x + m[5] * b.y + m[9] * b.z,
					m[2] * b.x + m[6] * b.y + m[10] * b.z);
			const mtl_rtw_v3_t back = mtl_rtw_sky_dir(w, mtl_rtw_v3(rows[0], rows[1], rows[2]),
					mtl_rtw_v3(rows[4], rows[5], rows[6]), mtl_rtw_v3(rows[8], rows[9], rows[10]));

			if ((fabsf(back.x - b.x) > 1e-5f) || (fabsf(back.y - b.y) > 1e-5f) || (fabsf(back.z - b.z) > 1e-5f))
			{
				ok_dir = 0;
			}
		}
	}

	CHECK(ok_rows);
	CHECK(ok_dir);

	/* no rotation, a zero axis or a value not finite: the identity */
	rm_rtw_sky_rows(12.5f, 0.0f, axis, rows);
	CHECK((rows[0] == 1) && (rows[5] == 1) && (rows[10] == 1) && (rows[1] == 0) && (rows[4] == 0) && (rows[9] == 0));
	rm_rtw_sky_rows(12.5f, 10.0f, zero, rows);
	CHECK((rows[0] == 1) && (rows[5] == 1) && (rows[10] == 1) && (rows[2] == 0) && (rows[8] == 0));
	rm_rtw_sky_rows(NAN, 10.0f, axis, rows);
	CHECK((rows[0] == 1) && (rows[5] == 1) && (rows[10] == 1));
	rm_rtw_sky_rows(12.5f, INFINITY, axis, rows);
	CHECK((rows[0] == 1) && (rows[5] == 1) && (rows[10] == 1));
}

/* TASK158: the world's texture table and the triangles' records */
static void
test_shade_build(void)
{
	static int big[MTL_RTW_TEX_WORLD + 1], bigtex[MTL_RTW_TEX_WORLD + 1], bigslot[MTL_RTW_TEX_WORLD];
	const int img[6] = { 7, 3, 7, -1, 900, 3 };
	const int t4[4] = { 0, 5, -1, (int)MTL_RTW_TEX_WORLD };
	int slot[8], tex[6];
	rm_surfrec_t r[4];
	unsigned out[16];
	int i;

	CHECK(rm_rtw_tex_slots(img, 6, slot, 8, tex) == 3);
	CHECK((slot[0] == 7) && (slot[1] == 3) && (slot[2] == 900));
	CHECK((tex[0] == 0) && (tex[1] == 1) && (tex[2] == 0) && (tex[3] == -1) && (tex[4] == 2) && (tex[5] == 1));
	CHECK(rm_rtw_tex_slots(img, 6, slot, 2, tex) == -1);
	CHECK(rm_rtw_tex_slots(img, 0, slot, 8, tex) == 0);

	for (i = 0; i < (int)MTL_RTW_TEX_WORLD + 1; i++)
	{
		big[i] = i;
	}

	CHECK(rm_rtw_tex_slots(big, MTL_RTW_TEX_WORLD, bigslot, MTL_RTW_TEX_WORLD, bigtex) == (int)MTL_RTW_TEX_WORLD);
	CHECK(rm_rtw_tex_slots(big, MTL_RTW_TEX_WORLD + 1, bigslot, MTL_RTW_TEX_WORLD, bigtex) == -1);

	memset(r, 0, sizeof(r));
	r[0].num_indices = 6;
	r[0].lm_slice = 2;
	r[1].first_index = 6;
	r[1].num_indices = 3;
	r[1].lm_slice = -1;
	r[1].texflags = RM_TEX_WARP | RM_TEX_TRANS33;
	r[2].first_index = 9;
	r[2].num_indices = 3;
	r[2].lm_slice = 300;
	r[2].texflags = RM_TEX_FLOWING | RM_TEX_TRANS66;
	r[3].first_index = 12;
	r[3].num_indices = 3;
	r[3].texflags = RM_TEX_SKY;

	CHECK(rm_rtw_shade_build(r, 4, 15, t4, out, 16) == 5);
	CHECK((out[0] == mtl_rtw_shade_pack(0, 2, 0)) && (out[1] == out[0]));
	CHECK(out[2] == mtl_rtw_shade_pack(5, MTL_RTW_SHADE_NOLM, MTL_RTW_SF_WARP | MTL_RTW_SF_TRANS));
	CHECK(out[3] == mtl_rtw_shade_pack(MTL_RTW_SHADE_NOTEX, MTL_RTW_SHADE_NOLM, MTL_RTW_SF_FLOW | MTL_RTW_SF_TRANS));
	CHECK(out[4] == mtl_rtw_shade_pack(MTL_RTW_SHADE_NOTEX, 0, MTL_RTW_SF_SKY));
	CHECK(!mtl_rtw_shade_ok(out[3]) && !mtl_rtw_shade_ok(out[4]) && mtl_rtw_shade_ok(out[0]));

	/* the walk of rm_rt_materials: records short of the buffer, a short cap, a gap, a part triangle */
	CHECK(rm_rtw_shade_build(r, 4, 18, t4, out, 16) == -1);
	CHECK(rm_rtw_shade_build(r, 4, 15, t4, out, 4) == -1);
	CHECK(rm_rtw_shade_build(r, 4, 14, t4, out, 16) == -1);
	r[1].first_index = 7;
	CHECK(rm_rtw_shade_build(r, 4, 15, t4, out, 16) == -1);
	r[1].first_index = 6;
	r[2].num_indices = 4;
	CHECK(rm_rtw_shade_build(r, 4, 15, t4, out, 16) == -1);
	r[2].num_indices = 3;
	CHECK(rm_rtw_shade_build(r, 4, 15, t4, out, 16) == 5);
	CHECK(rm_rtw_shade_build(r, 0, 0, t4, out, 16) == 0);
	CHECK(rm_rtw_shade_build(r, -1, 0, t4, out, 16) == -1);
}

/* TASK158: the view's world-space shading */
static void
test_shading(void)
{
	float proj[16], world[16];
	rm_viewport_t vp = { 0, 0, 1920, 1080, 0, 1 };
	const float eye[3] = { 0, 0, 0 };
	rm_rtw_shading_t sh;
	rm_rtw_view_t v;

	mtl_mat_frustum(proj, -4, 4, -2.25f, 2.25f, 4, 4096);
	mtl_mat_identity(world);
	CHECK(rm_rtw_view(proj, world, &vp, eye, 4, 1, 0.08f, 0.5f, 0, &v) == 1);

	memset(&sh, 0, sizeof(sh));
	sh.env = 2;
	sh.scale = 2.0f;
	sh.flow = -12.5f;
	sh.warp[0] = sh.warp[1] = sh.warp[2] = 0.5f;
	sh.warp[3] = 1;
	sh.trans[0] = sh.trans[1] = sh.trans[2] = 0.7f;
	sh.trans[3] = 0.33f;
	sh.fullbright = 1;
	sh.nlights = 40;
	sh.sky = 1;
	rm_rtw_sky_rows(0, 0, eye, sh.sky_rows);
	rm_rtw_shading(&sh, proj, &v);

	CHECK((v.env == 2) && (v.scale == 2.0f) && (v.flow == -12.5f) && (v.fullbright == 1) && (v.sky == 1));
	CHECK(v.nlights == MTL_RTS_MAX_LIGHTS);
	CHECK((v.warp[0] == 0.5f) && (v.warp[3] == 1) && (v.trans[2] == 0.7f) && (v.trans[3] == 0.33f));
	CHECK((v.sky_rows[0] == 1) && (v.sky_rows[5] == 1) && (v.sky_rows[10] == 1));

	/* the side of a pixel one unit away: 2 / (height x proj[5]); proj[5] = 2 near / (top - bottom) */
	CHECK(fabsf(v.lod_k - 2.0f / (1080.0f * proj[5])) < 1e-9f);
	CHECK(fabsf(v.lod_k - 4.5f / (1080.0f * 4.0f)) < 1e-7f);

	sh.nlights = -3;
	rm_rtw_shading(&sh, proj, &v);
	CHECK(v.nlights == 0);

	/* no height or no projection: no mip factor (level 0) */
	v.vp[3] = 0;
	rm_rtw_shading(&sh, proj, &v);
	CHECK(v.lod_k == 0);
	v.vp[3] = 1080;
	proj[5] = NAN;
	rm_rtw_shading(&sh, proj, &v);
	CHECK(v.lod_k == 0);

	/* a flow not finite is no scroll; a scale not finite is 1 */
	proj[5] = 1;
	sh.flow = NAN;
	sh.scale = NAN;
	rm_rtw_shading(&sh, proj, &v);
	CHECK(v.flow == 0);
	CHECK(v.scale == 1);
}

int
main(void)
{
	test_mask();
	test_turb();
	test_axes();
	test_dir();
	test_no_self_hit();
	test_choose();
	test_blend();
	test_cvars();
	test_view();
	test_instances();
	test_stats();
	test_choose_ws();
	test_shade_record();
	test_face_color();
	test_sky();
	test_shade_build();
	test_shading();

	printf("rm_rtwater_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
