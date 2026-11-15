/*
 * The host test of ref_metal's MetalFX decisions (TASK111): rm_metalfx.c,
 * the pure part (header/rm_metalfx.h). Built and run by
 * tests/check-ref-metal.sh with the system compiler, ASAN and UBSAN where
 * they exist; no Metal, no SDL, no game data. Each guard's operands are
 * held one by one, so a mutant of any of them fails a case.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../../src/client/refresh/metal/header/rm_metalfx.h"

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

#define VM_HAS (RM_FX_HAS(0) | RM_FX_HAS(RM_FX_SPATIAL))   /* this VM: the spatial scaler only */

/* r_metalfx's value as a mode: clamped, truncated, 0 when not finite */
static void
test_mode_of(void)
{
	CHECK(rm_fx_mode_of(0) == 0);
	CHECK(rm_fx_mode_of(1) == 1);
	CHECK(rm_fx_mode_of(2) == 2);
	CHECK(rm_fx_mode_of(3) == 3);
	CHECK(rm_fx_mode_of(1.9f) == 1);
	CHECK(rm_fx_mode_of(3.9f) == 3);
	CHECK(rm_fx_mode_of(4) == 3);
	CHECK(rm_fx_mode_of(1e30f) == 3);
	CHECK(rm_fx_mode_of(-0.5f) == 0);
	CHECK(rm_fx_mode_of(-1) == 0);
	CHECK(rm_fx_mode_of(-2.5f) == 0);
	CHECK(rm_fx_mode_of(-1e30f) == 0);
	CHECK(rm_fx_mode_of(NAN) == 0);
	CHECK(rm_fx_mode_of(INFINITY) == 0);
	CHECK(rm_fx_mode_of(-INFINITY) == 0);
}

/* the fallback: the best supported mode at or below the one asked */
static void
test_supported(void)
{
	int m;

	for (m = 0; m <= RM_FX_MAX; m++)
	{
		CHECK(rm_fx_supported(m, RM_FX_HAS_ALL) == m);
		CHECK(rm_fx_supported(m, 0) == 0);
		CHECK(rm_fx_supported(m, RM_FX_HAS_INTERP) == 0);
	}

	CHECK(rm_fx_supported(0, VM_HAS) == 0);
	CHECK(rm_fx_supported(1, VM_HAS) == 1);
	CHECK(rm_fx_supported(2, VM_HAS) == 1);
	CHECK(rm_fx_supported(3, VM_HAS) == 1);
	CHECK(rm_fx_supported(1, RM_FX_HAS(2)) == 0);
	CHECK(rm_fx_supported(3, RM_FX_HAS(2)) == 2);
	CHECK(rm_fx_supported(3, RM_FX_HAS(1) | RM_FX_HAS(3)) == 3);
	CHECK(rm_fx_supported(2, RM_FX_HAS(1) | RM_FX_HAS(3)) == 1);
	CHECK(rm_fx_supported(-1, RM_FX_HAS_ALL) == 0);
	CHECK(rm_fx_supported(4, RM_FX_HAS_ALL) == 0);
	CHECK(rm_fx_supported(1000, RM_FX_HAS_ALL) == 0);
	CHECK(rm_fx_supported(2, 0xffffffffu) == 2);
}

/* the mode a view runs: denoised only with a ray-traced effect */
static void
test_run(void)
{
	CHECK(rm_fx_run(3, 0, RM_FX_HAS_ALL) == 2);
	CHECK(rm_fx_run(3, 1, RM_FX_HAS_ALL) == 3);
	CHECK(rm_fx_run(2, 0, RM_FX_HAS_ALL) == 2);
	CHECK(rm_fx_run(2, 1, RM_FX_HAS_ALL) == 2);
	CHECK(rm_fx_run(1, 0, RM_FX_HAS_ALL) == 1);
	CHECK(rm_fx_run(0, 1, RM_FX_HAS_ALL) == 0);
	CHECK(rm_fx_run(3, 0, RM_FX_HAS(1) | RM_FX_HAS(3)) == 1);
	CHECK(rm_fx_run(3, 1, RM_FX_HAS(1) | RM_FX_HAS(3)) == 3);
	CHECK(rm_fx_run(3, 0, VM_HAS) == 1);
	CHECK(rm_fx_run(3, 1, VM_HAS) == 1);
	CHECK(rm_fx_run(2, 1, 0) == 0);
	CHECK(rm_fx_run(-1, 1, RM_FX_HAS_ALL) == 0);
	CHECK(rm_fx_run(4, 1, RM_FX_HAS_ALL) == 0);
}

/* the interpolator: asked and supported */
static void
test_interp(void)
{
	CHECK(rm_fx_interp(1, RM_FX_HAS_ALL) == 1);
	CHECK(rm_fx_interp(1, RM_FX_HAS_INTERP) == 1);
	CHECK(rm_fx_interp(0.5f, RM_FX_HAS_INTERP) == 1);
	CHECK(rm_fx_interp(-1, RM_FX_HAS_INTERP) == 1);
	CHECK(rm_fx_interp(1, RM_FX_HAS_ALL & ~RM_FX_HAS_INTERP) == 0);
	CHECK(rm_fx_interp(1, VM_HAS) == 0);
	CHECK(rm_fx_interp(0, RM_FX_HAS_ALL) == 0);
	CHECK(rm_fx_interp(NAN, RM_FX_HAS_ALL) == 0);
	CHECK(rm_fx_interp(INFINITY, RM_FX_HAS_ALL) == 0);
}

static void
test_name(void)
{
	CHECK(strcmp(rm_fx_name(0), "off") == 0);
	CHECK(strcmp(rm_fx_name(1), "spatial") == 0);
	CHECK(strcmp(rm_fx_name(2), "temporal") == 0);
	CHECK(strcmp(rm_fx_name(3), "temporal denoised") == 0);
	CHECK(strcmp(rm_fx_name(-1), "unknown") == 0);
	CHECK(strcmp(rm_fx_name(4), "unknown") == 0);
}

/* the per-family default height: 1080 rows from apple9 on, 720 below */
static void
test_default_rows(void)
{
	CHECK(rm_fx_default_rows(RMB_FAMILY_BIT(9)) == RM_FX_ROWS_APPLE9);
	CHECK(rm_fx_default_rows(RMB_FAMILY_BIT(5) | RMB_FAMILY_BIT(6) | RMB_FAMILY_BIT(7) | RMB_FAMILY_BIT(8) |
			RMB_FAMILY_BIT(9)) == 1080);
	CHECK(rm_fx_default_rows(RMB_FAMILY_BIT(10)) == 1080);
	CHECK(rm_fx_default_rows(RMB_FAMILY_BIT(5) | RMB_FAMILY_BIT(6) | RMB_FAMILY_BIT(7)) == RM_FX_ROWS_OTHER);
	CHECK(rm_fx_default_rows(RMB_FAMILY_BIT(8)) == 720);
	CHECK(rm_fx_default_rows(0) == 720);
}

/* the scale: asked or the default, clamped to 1 / ratio .. 1, the ratio to 1 .. 3 */
static void
test_scale(void)
{
	CHECK(near(rm_fx_scale(0.5f, 720, 480, 3), 0.5, 1e-6));
	CHECK(near(rm_fx_scale(1, 720, 480, 3), 1, 1e-6));
	CHECK(near(rm_fx_scale(0.75f, 720, 480, 3), 0.75, 1e-6));
	CHECK(near(rm_fx_scale(2, 720, 480, 3), 1, 1e-6));
	CHECK(near(rm_fx_scale(0.2f, 720, 480, 3), 1.0 / 3, 1e-6));
	CHECK(near(rm_fx_scale(0.2f, 720, 480, 2), 0.5, 1e-6));
	CHECK(near(rm_fx_scale(0.6f, 720, 480, 2), 0.6, 1e-6));
	CHECK(near(rm_fx_scale(0.2f, 720, 480, 10), 1.0 / 3, 1e-6));
	CHECK(near(rm_fx_scale(0.2f, 720, 480, 0.5f), 1, 1e-6));
	CHECK(near(rm_fx_scale(0.2f, 720, 480, NAN), 1.0 / 3, 1e-6));
	CHECK(near(rm_fx_scale(0.2f, 720, 480, -INFINITY), 1.0 / 3, 1e-6));
	/* the default: rows over the output's height */
	CHECK(near(rm_fx_scale(0, 720, 1440, 3), 0.5, 1e-6));
	CHECK(near(rm_fx_scale(0, 1080, 2880, 3), 0.375, 1e-6));
	CHECK(near(rm_fx_scale(0, 720, 2880, 3), 1.0 / 3, 1e-6));
	CHECK(near(rm_fx_scale(0, 720, 2880, 4), 1.0 / 3, 1e-6));
	CHECK(near(rm_fx_scale(0, 720, 480, 3), 1, 1e-6));
	CHECK(near(rm_fx_scale(-1, 720, 1440, 3), 0.5, 1e-6));
	CHECK(near(rm_fx_scale(NAN, 720, 1440, 3), 0.5, 1e-6));
	CHECK(near(rm_fx_scale(INFINITY, 720, 1440, 3), 0.5, 1e-6));
	CHECK(near(rm_fx_scale(0, 0, 1440, 3), 1, 1e-6));
	CHECK(near(rm_fx_scale(0, -720, 1440, 3), 1, 1e-6));
	CHECK(near(rm_fx_scale(0, 720, 0, 3), 1, 1e-6));
	CHECK(near(rm_fx_scale(0.5f, 720, 0, 3), 1, 1e-6));
	CHECK(near(rm_fx_scale(0.5f, 720, -480, 3), 1, 1e-6));
}

static rm_viewport_t
vp(double x, double y, double w, double h)
{
	rm_viewport_t v = { x, y, w, h, 0.25, 0.75 };

	return v;
}

/* the input and output sizes */
static void
test_sizes(void)
{
	rm_fx_size_t s;
	rm_viewport_t o = vp(0, 0, 640, 480), in;

	CHECK(rm_fx_sizes(&o, 0.5f, &s) == 1);
	CHECK(s.iw == 320 && s.ih == 240 && s.ow == 640 && s.oh == 480 && s.ox == 0 && s.oy == 0);
	o = vp(64, 48, 512, 384);
	CHECK(rm_fx_sizes(&o, 0.5f, &s) == 1);
	CHECK(s.iw == 256 && s.ih == 192 && s.ow == 512 && s.oh == 384 && s.ox == 64 && s.oy == 48);
	o = vp(0, 0, 640, 480);
	/* rounded up: 640 / 3 is 213.3, so 214; 480 / 3 is 160 within a float's error, so 160 */
	CHECK(rm_fx_sizes(&o, 1.0f / 3, &s) == 1);
	CHECK(s.iw == 214 && s.ih == 160);
	/* the ratio never above the clamp's 3: 1000 / 3 is 333.3 (333 would be 3.003), 2161 / 3 is 720.3 */
	o = vp(0, 0, 1000, 2161);
	CHECK(rm_fx_sizes(&o, rm_fx_scale(0.1f, 720, 2161, 3), &s) == 1);
	CHECK(s.iw == 334 && s.ih == 721);
	CHECK(rm_fx_sizes(&o, 1.0f / 3, &s) == 1);
	CHECK(s.iw == 334 && s.ih == 721);
	o = vp(0, 0, 1, 2);
	CHECK(rm_fx_sizes(&o, 1.0f / 3, &s) == 1);
	CHECK(s.iw == 1 && s.ih == 1);
	/* a product inside the slack rounds to 0: still 1 */
	o = vp(0, 0, 1, 1);
	CHECK(rm_fx_sizes(&o, 0.0005f, &s) == 1);
	CHECK(s.iw == 1 && s.ih == 1);
	o = vp(0, 0, 640, 480);
	CHECK(rm_fx_sizes(&o, 0.0001f, &s) == 1);
	CHECK(s.iw == 1 && s.ih == 1);
	CHECK(rm_fx_sizes(&o, 1, &s) == 1);
	CHECK(s.iw == 640 && s.ih == 480);
	CHECK(rm_fx_sizes(&o, 2, &s) == 1);
	CHECK(s.iw == 640 && s.ih == 480);
	CHECK(rm_fx_sizes(&o, NAN, &s) == 1);
	CHECK(s.iw == 640 && s.ih == 480);
	CHECK(rm_fx_sizes(&o, 0, &s) == 1);
	CHECK(s.iw == 640 && s.ih == 480);
	CHECK(rm_fx_sizes(&o, -1, &s) == 1);
	CHECK(s.iw == 640 && s.ih == 480);
	/* rounded, not truncated: 641 x 0.75 is 480.75 */
	o = vp(0, 0, 641, 481);
	CHECK(rm_fx_sizes(&o, 0.75f, &s) == 1);
	CHECK(s.iw == 481 && s.ih == 361);
	o = vp(0.4, 0.6, 639.6, 479.4);
	CHECK(rm_fx_sizes(&o, 1, &s) == 1);
	CHECK(s.ox == 0 && s.oy == 1 && s.ow == 640 && s.oh == 479);
	o = vp(0, 0, RM_FX_MAX_SIDE, 1);
	CHECK(rm_fx_sizes(&o, 0.5f, &s) == 1);
	CHECK(s.iw == RM_FX_MAX_SIDE / 2 && s.ih == 1);

	/* refused: each operand of the rectangle's bounds on its own */
	o = vp(0, 0, 0, 480);
	CHECK(rm_fx_sizes(&o, 0.5f, &s) == 0 && s.ow == 0 && s.iw == 0);
	o = vp(0, 0, 640, 0);
	CHECK(rm_fx_sizes(&o, 0.5f, &s) == 0 && s.oh == 0);
	o = vp(0, 0, RM_FX_MAX_SIDE + 1, 480);
	CHECK(rm_fx_sizes(&o, 0.5f, &s) == 0);
	o = vp(0, 0, 640, RM_FX_MAX_SIDE + 1);
	CHECK(rm_fx_sizes(&o, 0.5f, &s) == 0);
	o = vp(-1, 0, 640, 480);
	CHECK(rm_fx_sizes(&o, 0.5f, &s) == 0 && s.ox == 0);
	o = vp(-0.4, 0, 640, 480);
	CHECK(rm_fx_sizes(&o, 0.5f, &s) == 0);
	o = vp(0, -0.4, 640, 480);
	CHECK(rm_fx_sizes(&o, 0.5f, &s) == 0);
	o = vp(0, -1, 640, 480);
	CHECK(rm_fx_sizes(&o, 0.5f, &s) == 0);
	o = vp(NAN, 0, 640, 480);
	CHECK(rm_fx_sizes(&o, 0.5f, &s) == 0);
	o = vp(0, NAN, 640, 480);
	CHECK(rm_fx_sizes(&o, 0.5f, &s) == 0);
	o = vp(0, 0, NAN, 480);
	CHECK(rm_fx_sizes(&o, 0.5f, &s) == 0);
	o = vp(0, 0, 640, INFINITY);
	CHECK(rm_fx_sizes(&o, 0.5f, &s) == 0);
	o = vp(1e300, 0, 640, 480);
	CHECK(rm_fx_sizes(&o, 0.5f, &s) == 0);
	o = vp(0, 1e300, 640, 480);
	CHECK(rm_fx_sizes(&o, 0.5f, &s) == 0);

	/* the input viewport: at the origin, the depth range kept */
	o = vp(64, 48, 512, 384);
	rm_fx_sizes(&o, 0.5f, &s);
	rm_fx_input_viewport(&o, &s, &in);
	CHECK(in.x == 0 && in.y == 0 && in.w == 256 && in.h == 192 && in.znear == 0.25 && in.zfar == 0.75);
}

/* over every side, at the clamp of a ratio of 3 and of 2, the output-to-input ratio stays at or below it */
static void
test_ratio_bound(void)
{
	const float ratios[] = { 3.0f, 2.0f, 1.5f };
	rm_fx_size_t s;
	rm_viewport_t o;
	int side, r, over = 0, small = 0;

	for (r = 0; r < 3; r++)
	{
		const float k = rm_fx_scale(0.01f, 720, 1000, ratios[r]);

		for (side = 1; side <= RM_FX_MAX_SIDE; side++)
		{
			o = vp(0, 0, side, side);

			if (!rm_fx_sizes(&o, k, &s))
			{
				over++;
				continue;
			}

			/* ow / iw <= ratio, with the slack of a scale that is not exact in a float */
			over += (double)s.ow > (double)s.iw * ratios[r] * (1.0 + 1e-6);
			/* and no side larger than needed: one less would be over the ratio */
			small += (s.iw > 1) && ((double)s.ow <= (double)(s.iw - 1) * ratios[r]);
		}
	}

	CHECK(over == 0);
	CHECK(small == 0);
}

static void
test_halton(void)
{
	CHECK(near(rm_fx_halton(1, 2), 0.5, 1e-7));
	CHECK(near(rm_fx_halton(2, 2), 0.25, 1e-7));
	CHECK(near(rm_fx_halton(3, 2), 0.75, 1e-7));
	CHECK(near(rm_fx_halton(4, 2), 0.125, 1e-7));
	CHECK(near(rm_fx_halton(1, 3), 1.0 / 3, 1e-7));
	CHECK(near(rm_fx_halton(2, 3), 2.0 / 3, 1e-7));
	CHECK(near(rm_fx_halton(3, 3), 1.0 / 9, 1e-7));
	CHECK(near(rm_fx_halton(4, 3), 4.0 / 9, 1e-7));
	CHECK(near(rm_fx_halton(5, 3), 7.0 / 9, 1e-7));
	CHECK(near(rm_fx_halton(0, 2), 0, 0));
	CHECK(near(rm_fx_halton(5, 1), 0, 0));
	CHECK(near(rm_fx_halton(5, 0), 0, 0));
	CHECK(rm_fx_halton(0xffffffffu, 2) < 1.0f);
	CHECK(rm_fx_halton(0xffffffffu, 3) < 1.0f);
}

/* the jitter: Halton (2, 3) minus 0.5 from index 1, a period of RM_FX_JITTER_PHASES, none without a temporal mode */
static void
test_jitter(void)
{
	float x, y, ax[RM_FX_JITTER_PHASES], ay[RM_FX_JITTER_PHASES];
	unsigned n, k;
	int distinct = 1;

	rm_fx_jitter(RM_FX_TEMPORAL, 0, &x, &y);
	CHECK(near(x, 0, 1e-6) && near(y, 1.0 / 3 - 0.5, 1e-6));
	rm_fx_jitter(RM_FX_TEMPORAL, 1, &x, &y);
	CHECK(near(x, -0.25, 1e-6) && near(y, 2.0 / 3 - 0.5, 1e-6));
	rm_fx_jitter(RM_FX_DENOISED, 1, &x, &y);
	CHECK(near(x, -0.25, 1e-6) && near(y, 2.0 / 3 - 0.5, 1e-6));
	rm_fx_jitter(RM_FX_TEMPORAL, RM_FX_JITTER_PHASES, &x, &y);
	CHECK(near(x, 0, 1e-6) && near(y, 1.0 / 3 - 0.5, 1e-6));
	rm_fx_jitter(RM_FX_TEMPORAL, RM_FX_JITTER_PHASES - 1, &x, &y);
	CHECK(near(x, rm_fx_halton(RM_FX_JITTER_PHASES, 2) - 0.5, 1e-6));
	rm_fx_jitter(RM_FX_TEMPORAL, 0xffffffffu, &x, &y);
	CHECK(near(x, rm_fx_halton((0xffffffffu % RM_FX_JITTER_PHASES) + 1, 2) - 0.5, 1e-6));

	for (n = 0; n < RM_FX_JITTER_PHASES; n++)
	{
		rm_fx_jitter(RM_FX_TEMPORAL, n, &ax[n], &ay[n]);
		CHECK(ax[n] >= -0.5f && ax[n] < 0.5f && ay[n] >= -0.5f && ay[n] < 0.5f);

		for (k = 0; k < n; k++)
		{
			distinct &= !((ax[k] == ax[n]) && (ay[k] == ay[n]));
		}
	}

	CHECK(distinct);

	x = y = 9;
	rm_fx_jitter(RM_FX_SPATIAL, 1, &x, &y);
	CHECK(x == 0 && y == 0);
	x = y = 9;
	rm_fx_jitter(RM_FX_OFF, 1, &x, &y);
	CHECK(x == 0 && y == 0);
	x = y = 9;
	rm_fx_jitter(4, 1, &x, &y);
	CHECK(x == 0 && y == 0);
	x = y = 9;
	rm_fx_jitter(-1, 1, &x, &y);
	CHECK(x == 0 && y == 0);
}

/* what the G-buffer path must give: MOTION for any scaler or the interpolator, depth and motion for temporal */
static void
test_wants(void)
{
	CHECK(rm_fx_wants_motion(0, 0) == 0);
	CHECK(rm_fx_wants_motion(1, 0) == 1);
	CHECK(rm_fx_wants_motion(2, 0) == 1);
	CHECK(rm_fx_wants_motion(3, 0) == 1);
	CHECK(rm_fx_wants_motion(0, 1) == 1);
	CHECK(rm_fx_wants_motion(1, 1) == 1);
	CHECK(rm_fx_wants_motion(4, 0) == 0);
	CHECK(rm_fx_wants_motion(-1, 0) == 0);
	CHECK(rm_fx_wants_motion(-1, 1) == 1);
	CHECK(rm_fx_wants_depth(0) == 0);
	CHECK(rm_fx_wants_depth(1) == 0);
	CHECK(rm_fx_wants_depth(2) == 1);
	CHECK(rm_fx_wants_depth(3) == 1);
	CHECK(rm_fx_wants_depth(4) == 0);
	CHECK(rm_fx_wants_depth(-1) == 0);
}

/* the reset events, each on its own, and what does not reset */
static void
test_reset(void)
{
	rm_fx_last_t last;
	rm_fx_size_t s = { 320, 240, 640, 480, 0, 0 }, t;
	float org[3] = { 100, 200, 300 }, o[3];

	memset(&last, 0, sizeof(last));
	CHECK(rm_fx_reset(&last, 1, &s, org, RM_FX_CUT_DIST) == RM_FX_RESET_FIRST);
	rm_fx_keep(&last, 1, &s, org);
	CHECK(last.valid == 1);
	CHECK(rm_fx_reset(&last, 1, &s, org, RM_FX_CUT_DIST) == 0);
	CHECK(rm_fx_reset(&last, 2, &s, org, RM_FX_CUT_DIST) == RM_FX_RESET_MODE);

	t = s;
	t.iw = 321;
	CHECK(rm_fx_reset(&last, 1, &t, org, RM_FX_CUT_DIST) == RM_FX_RESET_SIZE);
	t = s;
	t.ih = 241;
	CHECK(rm_fx_reset(&last, 1, &t, org, RM_FX_CUT_DIST) == RM_FX_RESET_SIZE);
	t = s;
	t.ow = 641;
	CHECK(rm_fx_reset(&last, 1, &t, org, RM_FX_CUT_DIST) == RM_FX_RESET_SIZE);
	t = s;
	t.oh = 481;
	CHECK(rm_fx_reset(&last, 1, &t, org, RM_FX_CUT_DIST) == RM_FX_RESET_SIZE);
	t = s;
	t.ox = 1;
	CHECK(rm_fx_reset(&last, 1, &t, org, RM_FX_CUT_DIST) == RM_FX_RESET_RECT);
	t = s;
	t.oy = 1;
	CHECK(rm_fx_reset(&last, 1, &t, org, RM_FX_CUT_DIST) == RM_FX_RESET_RECT);

	/* a cut: more than the distance on any axis; up to it, none */
	memcpy(o, org, sizeof(o));
	o[0] += 256;
	CHECK(rm_fx_reset(&last, 1, &s, o, RM_FX_CUT_DIST) == 0);
	o[0] += 1;
	CHECK(rm_fx_reset(&last, 1, &s, o, RM_FX_CUT_DIST) == RM_FX_RESET_CUT);
	memcpy(o, org, sizeof(o));
	o[1] -= 257;
	CHECK(rm_fx_reset(&last, 1, &s, o, RM_FX_CUT_DIST) == RM_FX_RESET_CUT);
	memcpy(o, org, sizeof(o));
	o[2] += 300;
	CHECK(rm_fx_reset(&last, 1, &s, o, RM_FX_CUT_DIST) == RM_FX_RESET_CUT);
	memcpy(o, org, sizeof(o));
	o[0] += 200;
	o[1] += 200;
	o[2] -= 200;
	CHECK(rm_fx_reset(&last, 1, &s, o, RM_FX_CUT_DIST) == 0);
	memcpy(o, org, sizeof(o));
	o[1] = NAN;
	CHECK(rm_fx_reset(&last, 1, &s, o, RM_FX_CUT_DIST) == RM_FX_RESET_CUT);
	CHECK(rm_fx_reset(&last, 1, &s, org, NAN) == RM_FX_RESET_CUT);
	CHECK(rm_fx_reset(&last, 1, &s, org, 0) == 0);
	o[1] = org[1] + 0.5f;
	CHECK(rm_fx_reset(&last, 1, &s, o, 0) == RM_FX_RESET_CUT);

	/* several at once */
	t = s;
	t.iw = 1;
	t.ox = 2;
	memcpy(o, org, sizeof(o));
	o[0] += 1000;
	CHECK(rm_fx_reset(&last, 3, &t, o, RM_FX_CUT_DIST) ==
			(RM_FX_RESET_MODE | RM_FX_RESET_SIZE | RM_FX_RESET_RECT | RM_FX_RESET_CUT));

	/* forgotten (a map, a vid_restart, a frame without a world view): FIRST alone */
	last.valid = 0;
	CHECK(rm_fx_reset(&last, 3, &t, o, RM_FX_CUT_DIST) == RM_FX_RESET_FIRST);

	/* kept: the new view is the last */
	rm_fx_keep(&last, 3, &t, o);
	CHECK(rm_fx_reset(&last, 3, &t, o, RM_FX_CUT_DIST) == 0);
	CHECK(last.mode == 3 && last.size.iw == 1 && last.size.ox == 2 && last.org[0] == o[0]);
}

/* the sizes alike: each side and the place on its own */
static void
test_same_size(void)
{
	const rm_fx_size_t a = { 320, 240, 640, 480, 64, 48 };
	rm_fx_size_t b = a;

	CHECK(rm_fx_same_size(&a, &b));
	b.iw++;
	CHECK(!rm_fx_same_size(&a, &b));
	b = a;
	b.ih++;
	CHECK(!rm_fx_same_size(&a, &b));
	b = a;
	b.ow++;
	CHECK(!rm_fx_same_size(&a, &b));
	b = a;
	b.oh++;
	CHECK(!rm_fx_same_size(&a, &b));
	b = a;
	b.ox++;
	CHECK(!rm_fx_same_size(&a, &b));
	b = a;
	b.oy++;
	CHECK(!rm_fx_same_size(&a, &b));
}

/* the per-view step over a sequence of views and frames, as rm_fxscene.c drives it */
static void
test_step(void)
{
	rm_fx_state_t st;
	rm_fx_size_t s = { 320, 240, 640, 480, 0, 0 }, t;
	float org[3] = { 100, 200, 300 }, far[3] = { 100, 200, 300 + 257 }, jx, jy, ex, ey;
	unsigned i;

	rm_fx_state_reset(&st);
	CHECK(st.last.valid == 0 && st.n == 0 && st.drew == 0);

	/* the first view: FIRST, the jitter at index 0, the index 1 after it */
	CHECK(rm_fx_step(&st, RM_FX_TEMPORAL, &s, org, RM_FX_CUT_DIST, &jx, &jy) == RM_FX_RESET_FIRST);
	rm_fx_jitter(RM_FX_TEMPORAL, 0, &ex, &ey);
	CHECK(jx == ex && jy == ey && st.n == 1 && st.drew == 1 && st.last.valid == 1);
	rm_fx_frame_end(&st);
	CHECK(st.drew == 0 && st.last.valid == 1);

	/* views that keep the history: the index goes on */
	for (i = 1; i < 5; i++)
	{
		CHECK(rm_fx_step(&st, RM_FX_TEMPORAL, &s, org, RM_FX_CUT_DIST, &jx, &jy) == 0);
		rm_fx_jitter(RM_FX_TEMPORAL, i, &ex, &ey);
		CHECK(jx == ex && jy == ey && st.n == i + 1);
		rm_fx_frame_end(&st);
	}

	/* a move over 256: CUT, the index starts again */
	CHECK(rm_fx_step(&st, RM_FX_TEMPORAL, &s, far, RM_FX_CUT_DIST, &jx, &jy) == RM_FX_RESET_CUT);
	rm_fx_jitter(RM_FX_TEMPORAL, 0, &ex, &ey);
	CHECK(jx == ex && jy == ey && st.n == 1);
	rm_fx_frame_end(&st);
	CHECK(rm_fx_step(&st, RM_FX_TEMPORAL, &s, far, RM_FX_CUT_DIST, &jx, &jy) == 0 && st.n == 2);
	rm_fx_frame_end(&st);

	/* a frame without a scaled view (a cinematic, the console, a mode turned off), then a view: FIRST */
	rm_fx_frame_end(&st);
	CHECK(st.last.valid == 0);
	CHECK(rm_fx_step(&st, RM_FX_TEMPORAL, &s, far, RM_FX_CUT_DIST, &jx, &jy) == RM_FX_RESET_FIRST && st.n == 1);
	rm_fx_frame_end(&st);

	/* a mode change: MODE */
	CHECK(rm_fx_step(&st, RM_FX_SPATIAL, &s, far, RM_FX_CUT_DIST, &jx, &jy) == RM_FX_RESET_MODE && st.n == 1);
	CHECK(jx == 0 && jy == 0);
	rm_fx_frame_end(&st);

	/* the size, then the place of the rectangle */
	t = s;
	t.iw = 480;
	CHECK(rm_fx_step(&st, RM_FX_SPATIAL, &t, far, RM_FX_CUT_DIST, &jx, &jy) == RM_FX_RESET_SIZE);
	rm_fx_frame_end(&st);
	t.ox = 64;
	CHECK(rm_fx_step(&st, RM_FX_SPATIAL, &t, far, RM_FX_CUT_DIST, &jx, &jy) == RM_FX_RESET_RECT);
	rm_fx_frame_end(&st);
	CHECK(rm_fx_step(&st, RM_FX_SPATIAL, &t, far, RM_FX_CUT_DIST, &jx, &jy) == 0 && st.n == 2);

	/* two views in one frame (a second world view) keep it, and the frame ends with one drawn */
	CHECK(rm_fx_step(&st, RM_FX_SPATIAL, &t, far, RM_FX_CUT_DIST, &jx, &jy) == 0 && st.n == 3);
	rm_fx_frame_end(&st);
	CHECK(st.last.valid == 1);

	/* a map load or vid_restart: the state forgotten, FIRST */
	rm_fx_state_reset(&st);
	CHECK(rm_fx_step(&st, RM_FX_SPATIAL, &t, far, RM_FX_CUT_DIST, &jx, &jy) == RM_FX_RESET_FIRST && st.n == 1);
}

/* the refusal memo: the same mode and sizes are not tried again, another mode or size is */
static void
test_refusal(void)
{
	rm_fx_refusal_t r;
	rm_fx_size_t s = { 320, 240, 640, 480, 0, 0 }, t = s;

	rm_fx_refusal_clear(&r);
	CHECK(!rm_fx_refused(&r, RM_FX_SPATIAL, &s));
	rm_fx_refuse(&r, RM_FX_SPATIAL, &s);
	CHECK(rm_fx_refused(&r, RM_FX_SPATIAL, &s));
	CHECK(!rm_fx_refused(&r, RM_FX_TEMPORAL, &s));
	t.iw = 321;
	CHECK(!rm_fx_refused(&r, RM_FX_SPATIAL, &t));
	t = s;
	t.oy = 1;
	CHECK(!rm_fx_refused(&r, RM_FX_SPATIAL, &t));
	rm_fx_refuse(&r, RM_FX_TEMPORAL, &s);
	CHECK(rm_fx_refused(&r, RM_FX_TEMPORAL, &s) && !rm_fx_refused(&r, RM_FX_SPATIAL, &s));
	rm_fx_refusal_clear(&r);
	CHECK(!rm_fx_refused(&r, RM_FX_TEMPORAL, &s));
	/* a cleared memo refuses nothing, not even a zero size in mode 0 */
	memset(&t, 0, sizeof(t));
	CHECK(!rm_fx_refused(&r, 0, &t));
}

static void
test_format(void)
{
	char buf[256];
	rm_fx_size_t s = { 320, 240, 640, 480, 0, 0 };

	CHECK(rm_fx_fallback_format("r_metalfx", 2, 1, "is not supported on this device, it reads", buf, sizeof(buf)));
	CHECK(strcmp(buf, "ref_metal: r_metalfx 2 (temporal) is not supported on this device, it reads 1 (spatial)") == 0);
	CHECK(rm_fx_fallback_format("r_metalfx_interp", 1, 0, "is not supported on this device, it reads", buf,
			sizeof(buf)));
	CHECK(strcmp(buf, "ref_metal: r_metalfx_interp 1 is not supported on this device, it reads 0") == 0);
	CHECK(rm_fx_fallback_format("r_metalfx", 3, 2, "runs without a ray-traced effect as", buf, sizeof(buf)));
	CHECK(strcmp(buf, "ref_metal: r_metalfx 3 (temporal denoised) runs without a ray-traced effect as 2 (temporal)")
			== 0);
	CHECK(rm_fx_fallback_format("r_metalfx", 2, 1, "x", buf, 20) == 0 && buf[0] == '\0');
	CHECK(rm_fx_fallback_format("r_metalfx", 2, 1, "x", buf, 0) == 0);

	CHECK(rm_fx_stats_format(1, &s, RM_FX_RESET_FIRST | RM_FX_RESET_MODE, 0.1234, buf, sizeof(buf)));
	CHECK(strcmp(buf, "ref_metal: metal_stats: metalfx spatial: input 320x240, output 640x480 at 0,0, "
			"reset first mode, scaler 0.123 ms") == 0);
	s.ox = 64;
	s.oy = 48;
	CHECK(rm_fx_stats_format(2, &s, 0, -1, buf, sizeof(buf)));
	CHECK(strcmp(buf, "ref_metal: metal_stats: metalfx temporal: input 320x240, output 640x480 at 64,48, "
			"reset none, scaler not timed") == 0);
	CHECK(rm_fx_stats_format(2, &s, RM_FX_RESET_ALL, 2, buf, sizeof(buf)));
	CHECK(strcmp(buf, "ref_metal: metal_stats: metalfx temporal: input 320x240, output 640x480 at 64,48, "
			"reset first mode size rect cut, scaler 2.000 ms") == 0);
	CHECK(rm_fx_stats_format(2, &s, 0, 0, buf, sizeof(buf)) && strstr(buf, ", scaler 0.000 ms"));
		CHECK(rm_fx_stats_format(2, &s, 0, 1, buf, 30) == 0 && buf[0] == '\0');
	/* an exact fit leaves no room for the terminator: refused; one byte more fits */
	{
		const char *want = "ref_metal: r_metalfx 2 (temporal) x 1 (spatial)";

		CHECK(rm_fx_fallback_format("r_metalfx", 2, 1, "x", buf, strlen(want)) == 0 && buf[0] == '\0');
		CHECK(rm_fx_fallback_format("r_metalfx", 2, 1, "x", buf, strlen(want) + 1) && strcmp(buf, want) == 0);
	}
	CHECK(rm_fx_stats_format(2, &s, 0, 1, buf, 0) == 0);

	CHECK(rm_fx_support_format(VM_HAS, buf, sizeof(buf)));
	CHECK(strcmp(buf, "ref_metal: MetalFX supportsDevice: spatial yes, temporal no, temporal denoised no, "
			"frame interpolator no (r_metalfx_modes 3)") == 0);
	CHECK(rm_fx_support_format(RM_FX_HAS_ALL, buf, sizeof(buf)));
	CHECK(strcmp(buf, "ref_metal: MetalFX supportsDevice: spatial yes, temporal yes, temporal denoised yes, "
			"frame interpolator yes (r_metalfx_modes 31)") == 0);
	CHECK(rm_fx_support_format(0, buf, sizeof(buf)));
	CHECK(strcmp(buf, "ref_metal: MetalFX supportsDevice: spatial no, temporal no, temporal denoised no, "
			"frame interpolator no (r_metalfx_modes 1)") == 0);
	CHECK(rm_fx_support_format(RM_FX_HAS(2) | RM_FX_HAS(3) | 0x100u, buf, sizeof(buf)));
	CHECK(strstr(buf, "spatial no, temporal yes, temporal denoised yes, frame interpolator no (r_metalfx_modes 13)"));
	CHECK(rm_fx_support_format(RM_FX_HAS_INTERP, buf, sizeof(buf)) && strstr(buf, "interpolator yes (r_metalfx_modes 17)"));
	CHECK(rm_fx_support_format(0, buf, 10) == 0 && buf[0] == '\0');

	s.ox = s.oy = 0;
	CHECK(rm_fx_note_format(RM_FX_NOTE_UNMADE, 2, &s, buf, sizeof(buf)));
	CHECK(strcmp(buf, "ref_metal: the MetalFX temporal scaler from 320x240 to 640x480 cannot be made; "
			"the view is stretched") == 0);
	CHECK(rm_fx_note_format(RM_FX_NOTE_UNFIT, 1, &s, buf, sizeof(buf)));
	CHECK(strcmp(buf, "ref_metal: the view's targets do not fit the MetalFX spatial scaler from 320x240 to "
			"640x480; the view is stretched") == 0);
	buf[0] = 'x';
	CHECK(rm_fx_note_format(RM_FX_NOTE_NONE, 1, &s, buf, sizeof(buf)) == 0 && buf[0] == '\0');
	buf[0] = 'x';
	CHECK(rm_fx_note_format(3, 1, &s, buf, sizeof(buf)) == 0 && buf[0] == '\0');
	CHECK(rm_fx_note_format(RM_FX_NOTE_UNMADE, 1, &s, buf, 20) == 0 && buf[0] == '\0');
	CHECK(rm_fx_note_format(RM_FX_NOTE_UNMADE, 1, &s, buf, 0) == 0);
	CHECK(rm_fx_support_format(0, buf, 0) == 0);
}

int
main(void)
{
	test_mode_of();
	test_supported();
	test_run();
	test_interp();
	test_name();
	test_default_rows();
	test_scale();
	test_sizes();
	test_halton();
	test_jitter();
	test_wants();
	test_reset();
	test_ratio_bound();
	test_same_size();
	test_step();
	test_refusal();
	test_format();

	printf("rm_fx_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
