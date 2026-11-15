/*
 * tests/metal/rm_rtshadow_test.c -- the pure part of ref_metal's
 * ray-traced dynamic-light shadows (RT3, TASK55): rm_rtshadow.c and the
 * per-pixel arithmetic the shaders share, common/mtl_rts_math.h. Run by
 * tests/check-ref-metal.sh with rm_rt.c and rm_pure.c (gl1's
 * R_AddDynamicLights, rm_lm_dlight, and R_BuildLightMap's store,
 * rm_lm_store, are the references the falloff and the resolve's light are
 * held to). No SDL, no Metal, no game data.
 */

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static int
near(double a, double b, double eps)
{
	return fabs(a - b) <= eps;
}

/* a deterministic generator: the same cases every run */
static unsigned rng = 12345u;

static float
frand(float lo, float hi)
{
	rng = rng * 1103515245u + 12345u;
	return lo + (hi - lo) * (float)((rng >> 8) & 0xffffff) / 16777216.0f;
}

static void
test_on(void)
{
	CHECK(rm_rts_on(1, 1, 0, 1, 0) == 1);
	CHECK(rm_rts_on(2, 1, 0, 1, 0) == 1);
	CHECK(rm_rts_on(-1, 1, 0, 1, 0) == 1);
	CHECK(rm_rts_on(0, 1, 0, 1, 0) == 0);
	CHECK(rm_rts_on(1, 0, 0, 1, 0) == 0);           /* no ray tracing on the device */
	CHECK(rm_rts_on(1, 1, 1, 1, 0) == 0);           /* r_fullbright: gl1 adds no dynamic light */
	CHECK(rm_rts_on(1, 1, 0, 0, 0) == 0);           /* gl1_dynamic 0 */
	CHECK(rm_rts_on(1, 1, 0, 1, 1) == 0);           /* gl1_flashblend: the blobs instead */
	CHECK(rm_rts_on(NAN, 1, 0, 1, 0) == 0);
	CHECK(rm_rts_on(INFINITY, 1, 0, 1, 0) == 0);
	CHECK(rm_rts_on(1, 1, NAN, 1, 0) == 1);         /* a setting that is not finite counts as 0 */
	CHECK(rm_rts_on(1, 1, 0, NAN, 0) == 0);
	CHECK(rm_rts_on(1, 1, 0, 1, NAN) == 1);
}

static void
test_transmit_one(void)
{
	CHECK(rm_rts_transmit_one(0) == 0);
	CHECK(rm_rts_transmit_one(RM_TEX_FLOWING) == 0);
	CHECK(rm_rts_transmit_one(RM_TEX_SKY) == 0);
	CHECK(rm_rts_transmit_one(RM_TEX_SKY | RM_TEX_TRANS33) == 0);   /* the sky first, as its material id */
	CHECK(rm_rts_transmit_one(RM_TEX_WARP) == 0);                   /* water drawn opaque stops it */
	CHECK(rm_rts_transmit_one(RM_TEX_WARP | RM_TEX_TRANS33) == 171);
	CHECK(rm_rts_transmit_one(RM_TEX_WARP | RM_TEX_TRANS66) == 87);
	CHECK(rm_rts_transmit_one(RM_TEX_TRANS33) == 171);
	CHECK(rm_rts_transmit_one(RM_TEX_TRANS66) == 87);
	CHECK(rm_rts_transmit_one(RM_TEX_TRANS33 | RM_TEX_TRANS66) == 171);   /* 33 first, as rm_trans_alpha */
	CHECK(rm_rts_transmit_one(RM_TEX_TRANS66 | RM_TEX_FLOWING) == 87);
}

static void
test_transmit(void)
{
	rm_surfrec_t r[3];
	unsigned char out[8];
	unsigned long nt = 99;

	memset(r, 0, sizeof(r));
	r[0].first_index = 0;
	r[0].num_indices = 6;
	r[0].texflags = 0;
	r[1].first_index = 6;
	r[1].num_indices = 3;
	r[1].texflags = RM_TEX_TRANS66;
	r[2].first_index = 9;
	r[2].num_indices = 9;
	r[2].texflags = RM_TEX_WARP | RM_TEX_TRANS33;
	memset(out, 0xee, sizeof(out));
	CHECK(rm_rts_transmit(r, 3, 18, out, 8, &nt) == 6);
	CHECK(out[0] == 0 && out[1] == 0 && out[2] == 87 && out[3] == 171 && out[4] == 171 && out[5] == 171);
	CHECK(out[6] == 0xee);
	CHECK(nt == 4);
	CHECK(rm_rts_transmit(r, 3, 18, out, 5, &nt) == -1);   /* does not fit */
	CHECK(rm_rts_transmit(r, 3, 21, out, 8, &nt) == -1);   /* the records leave indices out */
	r[1].first_index = 7;
	CHECK(rm_rts_transmit(r, 3, 18, out, 8, &nt) == -1);   /* a gap */
	r[1].first_index = 6;
	r[1].num_indices = 4;
	CHECK(rm_rts_transmit(r, 3, 18, out, 8, &nt) == -1);   /* not whole triangles */
	CHECK(rm_rts_transmit(r, -1, 18, out, 8, &nt) == -1);
	CHECK(rm_rts_transmit(NULL, 0, 0, out, 8, &nt) == 0 && nt == 0);
}

static void
test_axis(void)
{
	const float h = 0.70710678f;

	CHECK(mtl_rts_axis(mtl_rts_v3(0, 0, 1)) == 2);
	CHECK(mtl_rts_axis(mtl_rts_v3(0, 0, -1)) == 2);
	CHECK(mtl_rts_axis(mtl_rts_v3(1, 0, 0)) == 0);
	CHECK(mtl_rts_axis(mtl_rts_v3(-1, 0, 0)) == 0);
	CHECK(mtl_rts_axis(mtl_rts_v3(0, 1, 0)) == 1);
	CHECK(mtl_rts_axis(mtl_rts_v3(0, -1, 0)) == 1);
	CHECK(mtl_rts_axis(mtl_rts_v3(h, 0, h)) == 2);        /* a tie with the floor: the floor's axes */
	CHECK(mtl_rts_axis(mtl_rts_v3(0, -h, -h)) == 2);
	CHECK(mtl_rts_axis(mtl_rts_v3(h, h, 0)) == 0);        /* x before y */
	CHECK(mtl_rts_axis(mtl_rts_v3(0.6f, 0.8f, 0)) == 1);
	CHECK(mtl_rts_axis(mtl_rts_v3(0.8f, 0.6f, 0)) == 0);
	CHECK(mtl_rts_axis(mtl_rts_v3(0.5f, 0.5f, 0.7071f)) == 2);
}

/*
 * The falloff against gl1's R_AddDynamicLights (rm_lm_dlight) at every
 * luxel of axial surfaces whose texture axes are the compiler's base axes
 * at scale 1: the point of luxel (s, t) is where the texture coordinate
 * less texturemins is (16 s, 16 t). Each surface is taken from its front
 * and, flipped (SURF_PLANEBACK), from its back.
 */
typedef struct
{
	float n[3];                 /* the plane's normal */
	float s[3], t[3];           /* the base axes (vecs[0], vecs[1] without the shift) */
} axial_t;

static const axial_t axials[6] = {
	{ { 0, 0, 1 }, { 1, 0, 0 }, { 0, -1, 0 } },
	{ { 0, 0, -1 }, { 1, 0, 0 }, { 0, -1, 0 } },
	{ { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, -1 } },
	{ { -1, 0, 0 }, { 0, 1, 0 }, { 0, 0, -1 } },
	{ { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
	{ { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
};

#define LUX 12

static void
test_falloff_stock(void)
{
	static float bl[LUX * LUX * 3];
	int c, cases = 0, lit = 0, wrong = 0, reach_wrong = 0;

	for (c = 0; c < 600; c++)
	{
		const axial_t *a = &axials[c % 6];
		const int back = (c / 6) & 1;
		float vecs0[4], vecs1[4], origin[3], color[3] = { 1, 1, 1 }, dist, n[3];
		float intensity = (float)(int)frand(80, 400) + 0.5f;   /* frad an integer and 0.29: no tie with fdist */
		int tm0 = 16 * (int)frand(-40, 40), tm1 = 16 * (int)frand(-40, 40), s, t, i;
		float shift0 = (float)(int)frand(-64, 64), shift1 = (float)(int)frand(-64, 64);

		for (i = 0; i < 3; i++)
		{
			vecs0[i] = a->s[i];
			vecs1[i] = a->t[i];
			n[i] = a->n[i];
		}

		vecs0[3] = shift0;
		vecs1[3] = shift1;
		dist = (float)(int)frand(-300, 300);

		/* a light in front of the face drawn (the flipped normal's front for a back face), off the integers */
		for (i = 0; i < 3; i++)
		{
			origin[i] = frand(-400, 400) + 0.37f;
		}

		{
			float d = origin[0] * n[0] + origin[1] * n[1] + origin[2] * n[2] - dist;
			float want = (back ? -1.0f : 1.0f) * ((float)(int)frand(1, intensity) + 0.21f);

			for (i = 0; i < 3; i++)
			{
				origin[i] += n[i] * (want - d);
			}
		}

		/* the light's foot near the surface, so the luxels see it */
		for (i = 0; i < 3; i++)
		{
			float centre = 0;

			if (a->s[i] != 0)
			{
				centre = (tm0 + 8 * LUX - shift0) * a->s[i];
			}
			else if (a->t[i] != 0)
			{
				centre = (tm1 + 8 * LUX - shift1) * a->t[i];
			}
			else
			{
				continue;
			}

			origin[i] = centre + (float)(int)frand(-150, 150) + 0.37f;   /* offsets off the integers gl1 truncates to */
		}

		memset(bl, 0, sizeof(bl));

		if (!rm_lm_dlight(bl, LUX, LUX, n, dist, vecs0, vecs1, tm0, tm1, origin, intensity, color))
		{
			continue;
		}

		cases++;

		for (t = 0; t < LUX; t++)
		{
			for (s = 0; s < LUX; s++)
			{
				/* the point of the luxel: vecs . p + shift - texturemins = 16 s, 16 t, on the plane */
				float p[3];
				mtl_rts_v3_t pn;
				mtl_rts_reach_t r;
				float got;

				for (i = 0; i < 3; i++)
				{
					p[i] = n[i] * dist + a->s[i] * (16 * s + tm0 - shift0) + a->t[i] * (16 * t + tm1 - shift1);
				}

				pn = back ? mtl_rts_v3(-n[0], -n[1], -n[2]) : mtl_rts_v3(n[0], n[1], n[2]);
				r = mtl_rts_reach(mtl_rts_v3(p[0], p[1], p[2]), pn, mtl_rts_v3(origin[0], origin[1], origin[2]),
						intensity);
				got = mtl_rts_add(r);

				if (bl[(t * LUX + s) * 3] > 0)
				{
					lit++;

					if (!mtl_rts_in_range(r))
					{
						reach_wrong++;
					}
				}

				if (!near(got, bl[(t * LUX + s) * 3], 1e-3))
				{
					wrong++;
				}
			}
		}
	}

	CHECK(cases > 500);
	CHECK(lit > 5000);
	CHECK(wrong == 0);
	CHECK(reach_wrong == 0);

	if (wrong || reach_wrong)
	{
		printf("falloff: %d cases, %d lit luxels, %d differ from rm_lm_dlight, %d lit but not in range\n", cases,
				lit, wrong, reach_wrong);
	}
}

/* the falloff's pieces one by one */
static void
test_falloff_cases(void)
{
	const mtl_rts_v3_t up = mtl_rts_v3(0, 0, 1), p = mtl_rts_v3(0, 0, 0);
	mtl_rts_reach_t r;

	/* straight above at 100: frad 200, fdist 0, adds 200 */
	r = mtl_rts_reach(p, up, mtl_rts_v3(0, 0, 100), 300);
	CHECK(near(r.frad, 200, 1e-4) && r.fdist == 0);
	CHECK(near(mtl_rts_add(r), 200, 1e-4));

	/* behind the plane: nothing, not in range */
	r = mtl_rts_reach(p, up, mtl_rts_v3(0, 0, -100), 300);
	CHECK(r.frad < 0 && mtl_rts_add(r) == 0 && !mtl_rts_in_range(r));

	/* on the plane: frad the intensity */
	r = mtl_rts_reach(p, up, mtl_rts_v3(10.5f, 0, 0), 300);
	CHECK(near(r.frad, 300, 1e-4) && r.fdist == 10);

	/* the offsets truncated, then the larger plus half the smaller: 30.9 and 21.7 give 30 + 10 */
	r = mtl_rts_reach(p, up, mtl_rts_v3(-30.9f, 21.7f, 50), 300);
	CHECK(r.fdist == 40);
	r = mtl_rts_reach(p, up, mtl_rts_v3(21.7f, -30.9f, 50), 300);
	CHECK(r.fdist == 40);
	r = mtl_rts_reach(p, up, mtl_rts_v3(21.0f, 21.0f, 50), 300);
	CHECK(r.fdist == 31);   /* equal: 21 + 21 / 2 */

	/* the cutoff on the plane: frad 63.9 adds nothing, 64 adds where fdist is below 0 + ... */
	r = mtl_rts_reach(p, up, mtl_rts_v3(0, 0, 100), 163.9f);
	CHECK(mtl_rts_add(r) == 0 && !mtl_rts_in_range(r));
	r = mtl_rts_reach(p, up, mtl_rts_v3(0, 0, 100), 164.0f);
	CHECK(mtl_rts_add(r) == 0);   /* fdist 0 is not below frad - 64 = 0 */
	CHECK(mtl_rts_in_range(r));   /* but a luxel by it may be lit */
	r = mtl_rts_reach(p, up, mtl_rts_v3(0.5f, 0, 100), 165.0f);
	CHECK(near(mtl_rts_add(r), 65, 1e-4));

	/* in the plane: lit below frad - 64, in range up to frad - 64 + 48 */
	r = mtl_rts_reach(p, up, mtl_rts_v3(135.5f, 0, 100), 300);   /* frad 200, fdist 135 */
	CHECK(near(mtl_rts_add(r), 65, 1e-4) && mtl_rts_in_range(r));
	r = mtl_rts_reach(p, up, mtl_rts_v3(136.5f, 0, 100), 300);   /* fdist 136 = frad - 64 */
	CHECK(mtl_rts_add(r) == 0 && mtl_rts_in_range(r));
	r = mtl_rts_reach(p, up, mtl_rts_v3(183.5f, 0, 100), 300);   /* 183 < 184 */
	CHECK(mtl_rts_add(r) == 0 && mtl_rts_in_range(r));
	r = mtl_rts_reach(p, up, mtl_rts_v3(184.5f, 0, 100), 300);
	CHECK(!mtl_rts_in_range(r));

	/* a wall facing x measures along y and z */
	r = mtl_rts_reach(p, mtl_rts_v3(-1, 0, 0), mtl_rts_v3(-50, 40.2f, 10.9f), 300);
	CHECK(near(r.frad, 250, 1e-4) && r.fdist == 45);
	r = mtl_rts_reach(p, mtl_rts_v3(0, 1, 0), mtl_rts_v3(40.2f, 50, 10.9f), 300);
	CHECK(near(r.frad, 250, 1e-4) && r.fdist == 45);

	/* a far offset stays inside an int */
	r = mtl_rts_reach(p, up, mtl_rts_v3(1e12f, 0, 10), 300);
	CHECK(r.fdist == MTL_RTS_MAX_OFFSET && mtl_rts_add(r) == 0);
	r = mtl_rts_reach(p, up, mtl_rts_v3(NAN, 0, 10), 300);
	CHECK(mtl_rts_add(r) == 0);
}

/*
 * The margin of the range: a sample within two luxels (32 units on each
 * axis) of a lit luxel, the farthest the cubic filter reads, is in range.
 */
static void
test_in_range_margin(void)
{
	int c, misses = 0, cases = 0;

	for (c = 0; c < 20000; c++)
	{
		const mtl_rts_v3_t up = mtl_rts_v3(0, 0, 1);
		const float intensity = frand(70, 400);
		const mtl_rts_v3_t o = mtl_rts_v3(frand(-10, 10), frand(-10, 10), frand(0.1f, intensity - 64));
		mtl_rts_v3_t l = mtl_rts_v3((float)(int)frand(-300, 300), (float)(int)frand(-300, 300), 0);
		mtl_rts_v3_t q;

		if (mtl_rts_add(mtl_rts_reach(l, up, o, intensity)) <= 0)
		{
			continue;
		}

		cases++;
		q = mtl_rts_v3(l.x + frand(-32, 32), l.y + frand(-32, 32), 0);

		if (!mtl_rts_in_range(mtl_rts_reach(q, up, o, intensity)))
		{
			misses++;
		}
	}

	CHECK(cases > 1000);
	CHECK(misses == 0);
}

static void
test_top(void)
{
	mtl_rts_top_t t = mtl_rts_top_init();
	int k;

	for (k = 0; k < MTL_RTS_CAP; k++)
	{
		CHECK(t.idx[k] == MTL_RTS_NO_LIGHT && t.key[k] == 0);
	}

	t = mtl_rts_top_add(t, 3, 0);       /* adds nothing: never kept */
	t = mtl_rts_top_add(t, 4, -5);
	t = mtl_rts_top_add(t, 5, NAN);
	CHECK(t.idx[0] == MTL_RTS_NO_LIGHT);

	t = mtl_rts_top_add(t, 0, 50);
	t = mtl_rts_top_add(t, 1, 80);
	t = mtl_rts_top_add(t, 2, 50);      /* a tie: after the first 50 */
	CHECK(t.idx[0] == 1 && t.idx[1] == 0 && t.idx[2] == 2 && t.idx[3] == MTL_RTS_NO_LIGHT);
	t = mtl_rts_top_add(t, 6, 10);
	CHECK(t.idx[3] == 6);
	t = mtl_rts_top_add(t, 7, 5);       /* below the four: dropped */
	CHECK(t.idx[0] == 1 && t.idx[1] == 0 && t.idx[2] == 2 && t.idx[3] == 6);
	t = mtl_rts_top_add(t, 8, 90);      /* above all: first, the smallest drops */
	CHECK(t.idx[0] == 8 && t.idx[1] == 1 && t.idx[2] == 0 && t.idx[3] == 2);
	CHECK(t.key[0] == 90 && t.key[1] == 80 && t.key[2] == 50 && t.key[3] == 50);
	t = mtl_rts_top_add(t, 9, 50);      /* equal to the last: not kept */
	CHECK(t.idx[3] == 2);
	t = mtl_rts_top_add(t, 10, 51);
	CHECK(t.idx[2] == 10 && t.idx[3] == 0);

	/* against a sort, random keys */
	for (k = 0; k < 2000; k++)
	{
		float keys[12];
		int i, j, n = 1 + (int)frand(0, 11.99f);
		mtl_rts_top_t u = mtl_rts_top_init();
		int ok = 1;

		for (i = 0; i < n; i++)
		{
			keys[i] = (float)(int)frand(-5, 20);
			u = mtl_rts_top_add(u, (unsigned)i, keys[i]);
		}

		/* slot j holds the j-th largest positive key, the earliest of equal ones */
		for (j = 0; j < MTL_RTS_CAP; j++)
		{
			int best = -1;

			for (i = 0; i < n; i++)
			{
				int used = 0, q;

				for (q = 0; q < j; q++)
				{
					used |= (u.idx[q] == (unsigned)i);
				}

				if (!used && (keys[i] > 0) && ((best < 0) || (keys[i] > keys[best])))
				{
					best = i;
				}
			}

			ok &= (best < 0) ? (u.idx[j] == MTL_RTS_NO_LIGHT) : (u.idx[j] == (unsigned)best);
		}

		CHECK(ok);
	}
}

static void
test_pack(void)
{
	mtl_rts_top_t t = mtl_rts_top_init();
	unsigned p, v;

	t = mtl_rts_top_add(t, 31, 9);
	t = mtl_rts_top_add(t, 0, 8);
	t = mtl_rts_top_add(t, 17, 7);
	p = mtl_rts_pack_idx(t, MTL_RTS_IN_RANGE);
	CHECK(mtl_rts_idx(p, 0) == 31 && mtl_rts_idx(p, 1) == 0 && mtl_rts_idx(p, 2) == 17 &&
			mtl_rts_idx(p, 3) == MTL_RTS_NO_LIGHT);
	CHECK((p & MTL_RTS_IN_RANGE) && !(p & MTL_RTS_SHADOWED));
	CHECK(p == (31u | (0u << 6) | (17u << 12) | (63u << 18) | MTL_RTS_IN_RANGE));
	p = mtl_rts_pack_idx(t, 0xffffffffu);   /* only the two flags */
	CHECK((p >> 26) == 0 && (p & MTL_RTS_SHADOWED) && (p & MTL_RTS_IN_RANGE));
	CHECK(mtl_rts_pack_idx(mtl_rts_top_init(), 0) == 0x00ffffffu);

	CHECK(mtl_rts_vis8(0) == 0 && mtl_rts_vis8(1) == 255 && mtl_rts_vis8(2) == 255 && mtl_rts_vis8(-1) == 0);
	CHECK(mtl_rts_vis8(0.5f) == 128 && mtl_rts_vis8(0.67f) == 171 && mtl_rts_vis8(0.34f) == 87);
	CHECK(mtl_rts_vis8(NAN) == 0);
	v = mtl_rts_pack_vis(1, 0, 0.5f, 0.2f);
	CHECK(v == (255u | (0u << 8) | (128u << 16) | (51u << 24)));
	CHECK(mtl_rts_vis(v, 0) == 1 && mtl_rts_vis(v, 1) == 0 && near(mtl_rts_vis(v, 2), 128 / 255.0, 1e-7) &&
			near(mtl_rts_vis(v, 3), 0.2, 1e-7));

	p = mtl_rts_pack_idx(t, 0);
	CHECK(near(mtl_rts_light_vis(p, v, 31), 1, 0) && near(mtl_rts_light_vis(p, v, 0), 0, 0));
	CHECK(near(mtl_rts_light_vis(p, v, 17), 128 / 255.0, 1e-7));
	CHECK(mtl_rts_light_vis(p, v, 5) == 1);   /* not traced: unshadowed */
}

static void
test_bias(void)
{
	const float k = 4.0f / 16777216.0f / 4.0f;   /* gl_znear 4 */

	CHECK(mtl_rts_bias(0, k) == 0.5f);
	CHECK(near(mtl_rts_bias(1000, k), 0.5 + 1e6 * k, 1e-6));
	CHECK(mtl_rts_bias(4000, k) > mtl_rts_bias(1000, k));
	/* four depth steps at 4096 units, near plane 4: 4 x 2^-24 x 4096^2 / 4 = 1 unit, plus the half */
	CHECK(near(mtl_rts_bias(4096, k), 1.5, 1e-5));
}

/* the resolve's light against R_BuildLightMap's store of the same sums (gamma and minlight the identity) */
static void
test_light(void)
{
	static unsigned char gamma[256];
	int i, c, worst = 0;
	mtl_rgba_t l, o;

	for (i = 0; i < 256; i++)
	{
		gamma[i] = (unsigned char)i;
	}

	l.r = 0.2f;
	l.g = 0.4f;
	l.b = 0.6f;
	l.a = 0.6f;
	o = mtl_rts_light(l, 0, 0, 0);
	CHECK(memcmp(&o, &l, sizeof(o)) == 0);   /* bit for bit without a term */
	o = mtl_rts_light(l, -5, 0, -1);         /* nothing to add */
	CHECK(memcmp(&o, &l, sizeof(o)) == 0);
	o = mtl_rts_light(l, 51, 0, 0);
	CHECK(near(o.r, 0.4, 1e-6) && o.g == l.g && o.b == l.b && near(o.a, 0.6, 1e-6));
	o = mtl_rts_light(l, 255, 0, 0);         /* 1.2 0.4 0.6: scaled by the red */
	CHECK(near(o.r, 1, 1e-6) && near(o.g, 0.4 / 1.2, 1e-6) && near(o.b, 0.5, 1e-6) && o.a == 1);

	for (c = 0; c < 20000; c++)
	{
		float bl[3];
		unsigned char dest[4];
		int s[3];
		float tr = frand(-20, 400), tg = frand(0, 400), tb = frand(0, 400);

		for (i = 0; i < 3; i++)
		{
			s[i] = (int)frand(0, 255.99f);
		}

		if (c & 1)
		{
			tg = tb = 0;
		}

		bl[0] = s[0] + (tr > 0 ? tr : 0);
		bl[1] = s[1] + tg;
		bl[2] = s[2] + tb;

		if (!rm_lm_store(1, 1, NULL, gamma, dest, 4, bl))
		{
			CHECK(0);
			continue;
		}

		l.r = s[0] / 255.0f;
		l.g = s[1] / 255.0f;
		l.b = s[2] / 255.0f;
		l.a = 1;
		o = mtl_rts_light(l, tr, tg, tb);

		/*
		 * the store truncates where the resolve keeps the fraction: under one
		 * level; where it scales a channel down it scales the truncated
		 * channel by the truncated brightest and truncates again: under two
		 */
		for (i = 0; i < 3; i++)
		{
			const float got = ((i == 0) ? o.r : (i == 1) ? o.g : o.b) * 255.0f;
			const int d = (int)fabsf(got - dest[i]);
			const float most = (bl[0] > bl[1]) ? ((bl[0] > bl[2]) ? bl[0] : bl[2]) : ((bl[1] > bl[2]) ? bl[1] : bl[2]);
			const float tol = (most >= 256.0f) ? 2.0f : 1.0f;

			if (!(fabsf(got - dest[i]) < tol + 1e-3f))
			{
				worst = (d > worst) ? d : worst;
				worst = worst ? worst : 1;
			}
		}
	}

	CHECK(worst == 0);
}

static void
test_invert(void)
{
	float m[16], inv[16], prod[16], id[16];
	int i;

	mtl_mat_identity(id);
	CHECK(mtl_mat_frustum(m, -2.4, 2.4, -1.8, 1.8, 4, 4096));
	CHECK(rm_rts_invert(m, inv));
	mtl_mat_mul(prod, m, inv);

	for (i = 0; i < 16; i++)
	{
		CHECK(near(prod[i], id[i], 1e-4));
	}

	memset(m, 0, sizeof(m));
	CHECK(!rm_rts_invert(m, inv));
	CHECK(memcmp(inv, id, sizeof(id)) == 0);
	mtl_mat_identity(m);
	m[5] = NAN;
	CHECK(!rm_rts_invert(m, inv));
}

/* a world point through the view's projection and viewport, then back */
static void
test_unproject(void)
{
	float proj[16], world[16], mvp[16];
	const float eye[3] = { 100, -50, 30 }, angles[3] = { 0, 20, 0 };
	rm_viewport_t vp = { 40, 20, 320, 240, 0, 1 };
	rm_rts_view_t v;
	int c, worst_ok = 1, tested = 0;

	CHECK(mtl_mat_frustum(proj, -4 * 1.333333, 4 * 1.333333, -4, 4, 4, 4096));
	rm_view_matrix(world, angles, eye);     /* gl1's R_SetupGL: x forward, z up, a yaw of 20 */
	mtl_mat_mul(mvp, proj, world);

	CHECK(rm_rts_view(proj, world, &vp, eye, 4, &v));
	CHECK(v.vp[0] == 40 && v.vp[1] == 20 && v.vp[2] == 320 && v.vp[3] == 240);
	CHECK(v.eye[0] == 100 && v.eye[1] == -50 && v.eye[2] == 30);
	CHECK(near(v.bias_k, 4.0 / 16777216.0 / 4.0, 1e-15));
	CHECK(!rm_rts_view(proj, world, &vp, eye, 0, &v));
	CHECK(!rm_rts_view(proj, world, &vp, eye, NAN, &v));

	CHECK(rm_rts_view(proj, world, &vp, eye, 4, &v));

	for (c = 0; c < 500; c++)
	{
		/* a point in front of the eye (its forward is about +x turned by the yaw) */
		const float dist = frand(10, 2000);
		const float yaw = (20 + frand(-25, 25)) * 3.14159265f / 180.0f, pitch = frand(-15, 15) * 3.14159265f / 180.0f;
		float p[3], clip[4], got[3], px, py, depth;
		int i;

		p[0] = eye[0] + dist * cosf(yaw) * cosf(pitch);
		p[1] = eye[1] + dist * sinf(yaw) * cosf(pitch);
		p[2] = eye[2] + dist * sinf(pitch);

		for (i = 0; i < 4; i++)
		{
			clip[i] = mvp[i] * p[0] + mvp[4 + i] * p[1] + mvp[8 + i] * p[2] + mvp[12 + i];
		}

		if (!(clip[3] > 0) || fabsf(clip[0]) > clip[3] || fabsf(clip[1]) > clip[3])
		{
			continue;
		}

		/* the window position a pixel centre takes, and the depth as mtl_gl_clip_z maps it */
		px = (float)(vp.x + (clip[0] / clip[3] * 0.5 + 0.5) * vp.w);
		py = (float)(vp.y + (0.5 - clip[1] / clip[3] * 0.5) * vp.h);
		depth = mtl_gl_clip_z(clip[2], clip[3]) / clip[3];
		CHECK(rm_rts_unproject(&v, px - 0.5f, py - 0.5f, depth, got));
		tested++;

		for (i = 0; i < 3; i++)
		{
			/* the depth's float step at that distance, and a margin for the rest */
			if (!near(got[i], p[i], 1e-3 + 4e-7 * dist * dist / 4))
			{
				worst_ok = 0;
			}
		}
	}

	CHECK(worst_ok);
	CHECK(tested > 300);

	if (tested <= 300)
	{
		printf("unproject: %d points in view\n", tested);
	}
}

static void
test_select_term(void)
{
	rm_rts_light_t l[6];
	const mtl_rts_v3_t p = mtl_rts_v3(0, 0, 0), up = mtl_rts_v3(0, 0, 1);
	mtl_rts_top_t t;
	unsigned flags = 99, pidx, pvis;
	float rgb[3], want[3] = { 0, 0, 0 };
	int i;

	memset(l, 0, sizeof(l));

	/* lights above the point at 10, 20, ..., 60, intensity 300: they add 290, 280, ..., 240 */
	for (i = 0; i < 6; i++)
	{
		l[i].origin[0] = 0.5f;
		l[i].origin[2] = 10.0f * (6 - i);
		l[i].origin[3] = 300;
		l[i].color[0] = 1;
		l[i].color[1] = 0.5f;
		l[i].color[2] = (float)i / 10;
	}

	t = rm_rts_select(p, up, l, 6, &flags);
	CHECK(flags == MTL_RTS_IN_RANGE);
	CHECK(t.idx[0] == 5 && t.idx[1] == 4 && t.idx[2] == 3 && t.idx[3] == 2);
	CHECK(near(t.key[0], 290, 1e-4) && near(t.key[3], 260, 1e-4));

	pidx = mtl_rts_pack_idx(t, flags);
	pvis = mtl_rts_pack_vis(1, 0, 1, 1);    /* light 4 shadowed */

	for (i = 0; i < 6; i++)
	{
		const float add = 300 - 10.0f * (6 - i);

		if (i == 4)
		{
			continue;
		}

		want[0] += add * l[i].color[0];
		want[1] += add * l[i].color[1];
		want[2] += add * l[i].color[2];
	}

	rm_rts_term(p, up, l, 6, pidx, pvis, rgb);
	CHECK(near(rgb[0], want[0], 1e-2) && near(rgb[1], want[1], 1e-2) && near(rgb[2], want[2], 1e-2));

	/* no light in range: nothing chosen, no flag, no term */
	l[0].origin[2] = -10;   /* behind */
	t = rm_rts_select(p, up, l, 1, &flags);
	CHECK(flags == 0 && t.idx[0] == MTL_RTS_NO_LIGHT);
	rm_rts_term(p, up, l, 1, mtl_rts_pack_idx(t, flags), 0, rgb);
	CHECK(rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0);

	/* in range by the margin only: flagged, nothing to trace or add */
	l[0].origin[0] = 150.5f;
	l[0].origin[2] = 100;   /* frad 200, fdist 150: past 136, inside 184 */
	t = rm_rts_select(p, up, l, 1, &flags);
	CHECK(flags == MTL_RTS_IN_RANGE && t.idx[0] == MTL_RTS_NO_LIGHT);

	/* more lights than the uniform holds: the first MTL_RTS_MAX_LIGHTS only, and none for a negative count */
	t = rm_rts_select(p, up, l, -3, &flags);
	CHECK(flags == 0 && t.idx[0] == MTL_RTS_NO_LIGHT);
	rm_rts_term(p, up, l, -3, 0, 0, rgb);
	CHECK(rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0);
}

static void
test_show(void)
{
	CHECK(rm_rts_show(0) == 0);
	CHECK(rm_rts_show(1) == 1);
	CHECK(rm_rts_show(2) == 2);
	CHECK(rm_rts_show(1.9f) == 1);
	CHECK(rm_rts_show(7) == RM_RTS_SHOW_MAX);
	CHECK(rm_rts_show(1e10f) == RM_RTS_SHOW_MAX);
	CHECK(rm_rts_show(-3) == 0);
	CHECK(rm_rts_show(NAN) == 0);
	CHECK(rm_rts_show(-INFINITY) == 0);
}

static void
test_stats_format(void)
{
	rm_rts_stats_t s;
	char buf[256];

	memset(&s, 0, sizeof(s));
	s.frame = 12;
	s.pixels = 307200;
	s.inrange = 1000;
	s.traced = 900;
	s.shadowed = 300;
	s.rays = 1200;
	s.ms = 0.25;
	CHECK(rm_rts_stats_format(&s, buf, sizeof(buf)));
	CHECK(strcmp(buf, "ref_metal: rt shadows: frame 12: 307200 pixels, 1000 in range, 900 traced, "
				"300 shadowed, 1200 rays, 0.250 ms") == 0);
	s.ms = -1;
	CHECK(rm_rts_stats_format(&s, buf, sizeof(buf)));
	CHECK(strstr(buf, "1200 rays, - ms") != NULL);
	CHECK(!rm_rts_stats_format(&s, buf, 20) && buf[0] == 0);
	CHECK(!rm_rts_stats_format(&s, buf, 0));
}

int
main(void)
{
	test_on();
	test_transmit_one();
	test_transmit();
	test_axis();
	test_falloff_stock();
	test_falloff_cases();
	test_in_range_margin();
	test_top();
	test_pack();
	test_bias();
	test_light();
	test_invert();
	test_unproject();
	test_select_term();
	test_show();
	test_stats_format();

	printf("rm_rtshadow_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
