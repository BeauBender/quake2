/*
 * tests/metal/rm_rtao_test.c -- the pure part of ref_metal's ray-traced
 * ambient occlusion (RT5, TASK57): rm_rtao.c and the per-pixel arithmetic
 * the kernel shares, common/mtl_rtao_math.h, with rm_rt.c's instance list
 * (which instances an AO ray tests). Run by tests/check-ref-metal.sh with
 * rm_rtshadow.c (the view's inverse, TASK55's rm_rts_view, and its
 * unprojection), rm_rt.c and rm_pure.c. No SDL, no Metal, no game data.
 */

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/client/refresh/metal/header/rm_rtao.h"
#include "../../src/client/refresh/metal/header/rm_rtshadow.h"
#include "../../src/client/refresh/metal/header/rm_rt.h"
#include "../../src/client/refresh/metal/common/mtl_matrix.h"
#include "../../src/client/refresh/metal/common/mtl_gbuf_math.h"

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
static unsigned rng = 24680u;

static float
frand(float lo, float hi)
{
	rng = rng * 1103515245u + 12345u;
	return lo + (hi - lo) * (float)((rng >> 8) & 0xffffff) / 16777216.0f;
}

static void
test_on(void)
{
	CHECK(rm_rtao_on(1, 1, 0) == 1);
	CHECK(rm_rtao_on(2, 1, 0) == 1);
	CHECK(rm_rtao_on(-1, 1, 0) == 1);
	CHECK(rm_rtao_on(0, 1, 0) == 0);
	CHECK(rm_rtao_on(1, 0, 0) == 0);        /* no ray tracing on the device */
	CHECK(rm_rtao_on(1, 1, 1) == 0);        /* r_fullbright: no lightmap term to scale */
	CHECK(rm_rtao_on(NAN, 1, 0) == 0);
	CHECK(rm_rtao_on(INFINITY, 1, 0) == 0);
	CHECK(rm_rtao_on(1, 1, NAN) == 1);      /* a setting that is not finite counts as 0 */
}

static void
test_cvars(void)
{
	CHECK(rm_rtao_range(128) == 128.0f);
	CHECK(rm_rtao_range(64.5f) == 64.5f);
	CHECK(rm_rtao_range(0) == RM_RTAO_RANGE_MIN);
	CHECK(rm_rtao_range(-5) == RM_RTAO_RANGE_MIN);
	CHECK(rm_rtao_range(1e9f) == RM_RTAO_RANGE_MAX);
	CHECK(rm_rtao_range(NAN) == RM_RTAO_RANGE);
	CHECK(rm_rtao_range(INFINITY) == RM_RTAO_RANGE);
	CHECK(rm_rtao_range(-INFINITY) == RM_RTAO_RANGE);

	CHECK(rm_rtao_floor(0.4f) == 0.4f);
	CHECK(rm_rtao_floor(0) == 0.0f);
	CHECK(rm_rtao_floor(1) == 1.0f);
	CHECK(rm_rtao_floor(-0.5f) == 0.0f);
	CHECK(rm_rtao_floor(3) == 1.0f);
	CHECK(rm_rtao_floor(NAN) == RM_RTAO_FLOOR);
	CHECK(rm_rtao_floor(INFINITY) == RM_RTAO_FLOOR);

	/* the seed: fixed at 1 or more, the frame count otherwise, always below 65536 */
	CHECK(rm_rtao_frame(1, 77) == 1);
	CHECK(rm_rtao_frame(1, 78) == 1);
	CHECK(rm_rtao_frame(5.9f, 3) == 5);
	CHECK(rm_rtao_frame(1e9f, 3) == 65535);
	CHECK(rm_rtao_frame(0, 77) == 77);
	CHECK(rm_rtao_frame(0, 78) == 78);
	CHECK(rm_rtao_frame(-3, 78) == 78);
	CHECK(rm_rtao_frame(0.5f, 78) == 78);   /* below 1: not a seed */
	CHECK(rm_rtao_frame(NAN, 78) == 78);
	CHECK(rm_rtao_frame(INFINITY, 78) == 78);
	CHECK(rm_rtao_frame(0, 0x12345UL) == 0x2345u);

	CHECK(rm_rtao_show(0) == 0);
	CHECK(rm_rtao_show(1) == 1);
	CHECK(rm_rtao_show(2) == 2);
	CHECK(rm_rtao_show(1.9f) == 1);
	CHECK(rm_rtao_show(9) == RM_RTAO_SHOW_MAX);
	CHECK(rm_rtao_show(-1) == 0);
	CHECK(rm_rtao_show(NAN) == 0);
	CHECK(rm_rtao_show(INFINITY) == 0);
}

/* the squared toroidal distance of two tile positions */
static int
tdist2(int a, int b)
{
	int dx = abs((a % MTL_RTAO_TILE) - (b % MTL_RTAO_TILE)), dy = abs((a / MTL_RTAO_TILE) - (b / MTL_RTAO_TILE));

	dx = (dx > MTL_RTAO_TILE / 2) ? MTL_RTAO_TILE - dx : dx;
	dy = (dy > MTL_RTAO_TILE / 2) ? MTL_RTAO_TILE - dy : dy;
	return dx * dx + dy * dy;
}

static void
test_bluenoise(void)
{
	static unsigned short a[MTL_RTAO_TILE_N], b[MTL_RTAO_TILE_N];
	static int seen[MTL_RTAO_TILE_N], pos[MTL_RTAO_TILE_N];
	int i, j, perm = 1, worst2 = 1 << 30, blocks_ok = 1;
	double worst_block = 0;

	CHECK(rm_rtao_bluenoise(a) == 1);
	CHECK(rm_rtao_bluenoise(b) == 1);
	CHECK(memcmp(a, b, sizeof(a)) == 0);     /* the same tile on every call */

	memset(seen, 0, sizeof(seen));

	for (i = 0; i < MTL_RTAO_TILE_N; i++)
	{
		if ((a[i] >= MTL_RTAO_TILE_N) || seen[a[i]])
		{
			perm = 0;
			continue;
		}

		seen[a[i]] = 1;
		pos[a[i]] = i;
	}

	CHECK(perm);   /* every rank once */

	if (!perm)
	{
		return;
	}

	/* blue: the first eighth of the ranks lie apart, no two within 2 pixels (white noise has neighbours) */
	for (i = 0; i < MTL_RTAO_TILE_N / 8; i++)
	{
		for (j = 0; j < i; j++)
		{
			const int d2 = tdist2(pos[i], pos[j]);

			worst2 = (d2 < worst2) ? d2 : worst2;
		}
	}

	CHECK(worst2 >= 4);

	/* and no low frequencies: each 8 x 8 block's mean rank is within 0.03 of the middle (white noise: about 0.09) */
	for (i = 0; i < MTL_RTAO_TILE / 8; i++)
	{
		for (j = 0; j < MTL_RTAO_TILE / 8; j++)
		{
			double sum = 0, dev;
			int x, y;

			for (y = 0; y < 8; y++)
			{
				for (x = 0; x < 8; x++)
				{
					sum += (a[(i * 8 + y) * MTL_RTAO_TILE + j * 8 + x] + 0.5) / MTL_RTAO_TILE_N;
				}
			}

			dev = fabs(sum / 64.0 - 0.5);
			worst_block = (dev > worst_block) ? dev : worst_block;
			blocks_ok = blocks_ok && (dev < 0.03);
		}
	}

	CHECK(blocks_ok);

	if (!blocks_ok || (worst2 < 4))
	{
		printf("blue noise: nearest of the first eighth %d (squared), worst block %.4f\n", worst2, worst_block);
	}
}

static void
test_noise(void)
{
	unsigned d;

	/* the rank's centre at frame 0; in [0, 1) at every frame; the four numbers at four places */
	CHECK(near(mtl_rtao_noise(0, 0, 0), 0.5 / MTL_RTAO_TILE_N, 1e-9));
	CHECK(near(mtl_rtao_noise(MTL_RTAO_TILE_N - 1, 0, 2), 1.0 - 0.5 / MTL_RTAO_TILE_N, 1e-6));
	CHECK(near(mtl_rtao_noise(100, 1, 0), mtl_rtao_fract((100.5f / MTL_RTAO_TILE_N) + 0.61803399f), 1e-6));

	for (d = 0; d < 4; d++)
	{
		unsigned f, r, ok = 1;

		for (f = 0; f < 70000; f += 997)
		{
			for (r = 0; r < MTL_RTAO_TILE_N; r += 61)
			{
				const float u = mtl_rtao_noise(r, f, d);

				ok = ok && (u >= 0.0f) && (u < 1.0f);
			}
		}

		CHECK(ok);
	}

	CHECK(mtl_rtao_tile_index(0, 0, 0) == 0);
	CHECK(mtl_rtao_tile_index(MTL_RTAO_TILE, 2 * MTL_RTAO_TILE, 0) == 0);   /* the tile repeats */
	CHECK(mtl_rtao_tile_index(0, 0, 1) != mtl_rtao_tile_index(0, 0, 0));
	CHECK(mtl_rtao_tile_index(0, 0, 2) != mtl_rtao_tile_index(0, 0, 1));
	CHECK(mtl_rtao_tile_index(0, 0, 3) != mtl_rtao_tile_index(0, 0, 2));
	CHECK(mtl_rtao_tile_index(1919, 1079, 3) < MTL_RTAO_TILE_N);
}

static mtl_rtao_v3_t
unit(float x, float y, float z)
{
	const float l = sqrtf(x * x + y * y + z * z);

	return mtl_rtao_v3(x / l, y / l, z / l);
}

static float
dot3(mtl_rtao_v3_t a, mtl_rtao_v3_t b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

static void
test_dir(void)
{
	int i, ok_len = 1, ok_up = 1, ok_half = 1;
	double sum_cos = 0;
	int n = 0;

	for (i = 0; i < 20000; i++)
	{
		mtl_rtao_v3_t nn = (i < 6) ? (i == 0 ? mtl_rtao_v3(0, 0, 1) : i == 1 ? mtl_rtao_v3(0, 0, -1) :
				i == 2 ? mtl_rtao_v3(1, 0, 0) : i == 3 ? mtl_rtao_v3(0, -1, 0) : i == 4 ? mtl_rtao_v3(0, 0, -0.0f) :
				unit(0.0001f, 0, -1)) : unit(frand(-1, 1), frand(-1, 1), frand(-1, 1));
		const float u = frand(0, 1), v = (i % 97 == 0) ? 1.0f : frand(0, 1);
		unsigned k;

		if (i == 4)
		{
			nn = mtl_rtao_v3(0, 0, 1);
		}

		for (k = 0; k < 2; k++)
		{
			const mtl_rtao_v3_t d = mtl_rtao_dir(nn, k, u, v);
			const float l = sqrtf(dot3(d, d)), c = dot3(d, nn);
			const mtl_rtao_v3_t d0 = mtl_rtao_dir(nn, k, 0.25f, 0.5f), d1 = mtl_rtao_dir(nn, k ^ 1u, 0.25f, 0.5f);

			ok_len = ok_len && near(l, 1.0, 1e-4);
			ok_up = ok_up && (c >= MTL_RTAO_MIN_COS - 1e-5f);
			/* the two rays' azimuths half a turn apart for the same numbers: opposite about the normal */
			ok_half = ok_half && (dot3(d0, d1) < c * c + 1e-3f);
			sum_cos += c;
			n++;
		}
	}

	CHECK(ok_len);
	CHECK(ok_up);
	CHECK(ok_half);
	/* cosine-weighted: the mean cosine is 2/3 (a uniform hemisphere's is 1/2) */
	CHECK(near(sum_cos / n, 2.0 / 3.0, 0.01));

	/* the elevation number 0 is along the normal, the largest at the smallest cosine */
	{
		const mtl_rtao_v3_t up = mtl_rtao_v3(0, 0, 1);

		CHECK(near(mtl_rtao_dir(up, 0, 0.3f, 0).z, 1.0, 1e-6));
		CHECK(near(mtl_rtao_dir(up, 1, 0.3f, 1).z, MTL_RTAO_MIN_COS, 1e-5));
		CHECK(mtl_rtao_dir(up, 0, 0.25f, 0.5f).y > 0);   /* ray 0 in the first half (azimuth 0 to pi) */
		CHECK(mtl_rtao_dir(up, 1, 0.25f, 0.5f).y < 0);   /* ray 1 in the second */
	}
}

static void
test_occlusion(void)
{
	CHECK(mtl_rtao_occlusion(0, 10, 128) == 0.0f);
	CHECK(mtl_rtao_occlusion(1, 0, 128) == 1.0f);
	CHECK(near(mtl_rtao_occlusion(1, 64, 128), 0.25, 1e-6));
	CHECK(mtl_rtao_occlusion(1, 128, 128) == 0.0f);
	CHECK(mtl_rtao_occlusion(1, 500, 128) == 0.0f);
	CHECK(mtl_rtao_occlusion(1, NAN, 128) == 0.0f);
	CHECK(mtl_rtao_occlusion(1, 10, 0) == 0.0f);
	CHECK(mtl_rtao_occlusion(1, 10, NAN) == 0.0f);

	CHECK(mtl_rtao_ao(0, 0) == 1.0f);
	CHECK(mtl_rtao_ao(1, 1) == 0.0f);
	CHECK(near(mtl_rtao_ao(1, 0), 0.5, 1e-7));
	CHECK(near(mtl_rtao_ao(0.25f, 0.5f), 0.625, 1e-7));

	CHECK(mtl_rtao_occludes(MTL_GB_MAT_WORLD));
	CHECK(mtl_rtao_occludes(0xffu));                 /* an alias model */
	CHECK(mtl_rtao_occludes(MTL_GB_MAT_NONE));
	CHECK(!mtl_rtao_occludes(MTL_GB_MAT_WATER));
	CHECK(!mtl_rtao_occludes(MTL_GB_MAT_SKY));
	CHECK(!mtl_rtao_occludes(MTL_GB_MAT_TRANS));
}

static void
test_factor(void)
{
	mtl_rgba_t l = { 0.8f, 0.4f, 0.2f, 0.7f }, r;
	unsigned m;

	CHECK(mtl_rtao_factor(1, 0.4f) == 1.0f);
	CHECK(mtl_rtao_factor(0.7f, 0.4f) == 0.7f);
	CHECK(mtl_rtao_factor(0.4f, 0.4f) == 0.4f);
	CHECK(mtl_rtao_factor(0.1f, 0.4f) == 0.4f);
	CHECK(mtl_rtao_factor(0, 0.4f) == 0.4f);
	CHECK(mtl_rtao_factor(0, 0) == 0.0f);
	CHECK(mtl_rtao_factor(0.3f, 1) == 1.0f);         /* r_rt_ao_min 1: the AO does nothing */
	CHECK(mtl_rtao_factor(2, 0.4f) == 1.0f);
	CHECK(mtl_rtao_factor(NAN, 0.4f) == 1.0f);

	r = mtl_rtao_light(l, MTL_GB_MAT_WORLD, 0.5f);
	CHECK((r.r == 0.4f) && (r.g == 0.2f) && (r.b == 0.1f) && (r.a == 0.7f));

	/* factor 1 leaves the light bit for bit: an unoccluded pixel is the effect-off pixel */
	r = mtl_rtao_light(l, MTL_GB_MAT_WORLD, 1.0f);
	CHECK(memcmp(&r, &l, sizeof(r)) == 0);

	/* only the lightmap term: an alias model's vertex light, water, the sky keep theirs */
	for (m = 0; m < 8; m++)
	{
		if (m != MTL_GB_MAT_WORLD)
		{
			r = mtl_rtao_light(l, m, 0.5f);
			CHECK(memcmp(&r, &l, sizeof(r)) == 0);
		}
	}

	/*
	 * The floor: at every factor the resolve's world pixel is at least
	 * r_rt_ao_min times the effect-off pixel, per channel, in each
	 * lightmap environment of gl1 (modulate, combine with its scale,
	 * replace for r_lightmap).
	 */
	{
		static const int envs[3] = { MTL_LM_MODULATE, MTL_LM_COMBINE, MTL_LM_REPLACE };
		int i, e, ok = 1;

		for (i = 0; i < 5000; i++)
		{
			const mtl_rgba_t d = { frand(0, 1), frand(0, 1), frand(0, 1), 1 };
			const mtl_rgba_t lm = { frand(0, 1), frand(0, 1), frand(0, 1), 1 };
			const float fl = frand(0, 1), ao = frand(0, 1), f = mtl_rtao_factor(ao, fl);

			for (e = 0; e < 3; e++)
			{
				const float sc = (envs[e] == MTL_LM_COMBINE) ? frand(1, 4) : 1;
				const mtl_rgba_t off = mtl_gb_resolve(d, lm, MTL_GB_MAT_WORLD, envs[e], sc);
				const mtl_rgba_t on = mtl_gb_resolve(d, mtl_rtao_light(lm, MTL_GB_MAT_WORLD, f), MTL_GB_MAT_WORLD,
						envs[e], sc);

				ok = ok && (on.r >= fl * off.r - 1e-6f) && (on.g >= fl * off.g - 1e-6f) && (on.b >= fl * off.b - 1e-6f);
				ok = ok && (on.r <= off.r + 1e-6f) && (on.g <= off.g + 1e-6f) && (on.b <= off.b + 1e-6f);
			}
		}

		CHECK(ok);
	}
}

/* the rgb10a2 normal as the G-buffer stores it and the kernel reads it back */
static mtl_rtao_v3_t
stored_normal(mtl_rtao_v3_t n)
{
	const mtl_rgba_t c = mtl_gb_normal(n.x, n.y, n.z);
	const float x = roundf(c.r * 1023.0f) / 1023.0f * 2 - 1, y = roundf(c.g * 1023.0f) / 1023.0f * 2 - 1,
		z = roundf(c.b * 1023.0f) / 1023.0f * 2 - 1;

	return unit(x, y, z);
}

/* the kernel's unprojection (rm_rtao.metal's rm_rtao_position), in float */
static mtl_rtao_v3_t
position(const rm_rtao_view_t *v, float px, float py, float depth)
{
	const float cx = (px + 0.5f - v->vp[0]) / v->vp[2] * 2.0f - 1.0f;
	const float cy = 1.0f - (py + 0.5f - v->vp[1]) / v->vp[3] * 2.0f;
	const float cz = depth * 2.0f - 1.0f;
	float h[4];
	int i;

	for (i = 0; i < 4; i++)
	{
		h[i] = v->inv[i] * cx + v->inv[4 + i] * cy + v->inv[8 + i] * cz + v->inv[12 + i];
	}

	return mtl_rtao_v3(h[0] / h[3], h[1] / h[3], h[2] / h[3]);
}

/*
 * A pixel never occludes itself: on an open plane (nothing but the plane
 * within the range) every pixel reads AO 1. For planes of every
 * orientation seen from 8 to 3,000 units, at grazing angles too, each
 * pixel's depth as the depth32Float attachment holds it (and up to four
 * float steps off, the raster's interpolation), its normal as the
 * rgb10a2 attachment holds it, and 64 frames of noise: no ray from the
 * biased start meets the plane within the range, and the light is the
 * effect-off light bit for bit.
 */
static void
test_open_plane(void)
{
	static unsigned short rank[MTL_RTAO_TILE_N];
	const float znear = 4.0f, range = RM_RTAO_RANGE;
	int c, pixels = 0, rays = 0, hits = 0, below = 0, unlit = 0;

	CHECK(rm_rtao_bluenoise(rank) == 1);

	for (c = 0; c < 400; c++)
	{
		float proj[16], world[16], m[16];
		rm_viewport_t vp = { 0, 0, 1920, 1080, 0, 1 };
		rm_rtao_view_t v;
		const float eye[3] = { frand(-500, 500), frand(-500, 500), frand(-200, 200) };
		const float yaw = frand(0, 360), pitch = frand(-89, 89);
		/* the plane: through a point dist units ahead (at a random angle to the view), any orientation facing the eye */
		const float dist = (c < 100) ? frand(8, 64) : frand(64, 3000);
		mtl_rtao_v3_t pn = unit(frand(-1, 1), frand(-1, 1), frand(-1, 1));
		float pd, fwd[3];
		int k;

		mtl_mat_frustum(proj, -znear * 0.8, znear * 0.8, -znear * 0.45, znear * 0.45, znear, 4096);
		mtl_mat_identity(world);
		mtl_mat_rotate(world, -90, 1, 0, 0);
		mtl_mat_rotate(world, 90, 0, 0, 1);
		mtl_mat_rotate(world, -pitch, 0, 1, 0);
		mtl_mat_rotate(world, -yaw, 0, 0, 1);
		mtl_mat_translate(world, -eye[0], -eye[1], -eye[2]);
		mtl_mat_mul(m, proj, world);

		if (!rm_rtao_view(proj, world, &vp, eye, znear, range, RM_RTAO_FLOOR, 1, 0, &v))
		{
			CHECK(0);
			continue;
		}

		fwd[0] = cosf(pitch * 3.14159265f / 180) * cosf(yaw * 3.14159265f / 180);
		fwd[1] = cosf(pitch * 3.14159265f / 180) * sinf(yaw * 3.14159265f / 180);
		fwd[2] = -sinf(pitch * 3.14159265f / 180);

		/* faces the eye, at least 2 degrees off edge-on */
		if (pn.x * fwd[0] + pn.y * fwd[1] + pn.z * fwd[2] > -0.035f)
		{
			pn = mtl_rtao_v3(-pn.x, -pn.y, -pn.z);

			if (pn.x * fwd[0] + pn.y * fwd[1] + pn.z * fwd[2] > -0.035f)
			{
				pn = unit(-fwd[0], -fwd[1], -fwd[2]);
			}
		}

		pd = pn.x * (eye[0] + fwd[0] * dist) + pn.y * (eye[1] + fwd[1] * dist) + pn.z * (eye[2] + fwd[2] * dist);

		for (k = 0; k < 40; k++)
		{
			const float px = floorf(frand(0, 1920)), py = floorf(frand(0, 1080));
			/* the pixel centre's ray from the eye, met with the plane: the true point and its depth */
			const mtl_rtao_v3_t q = position(&v, px, py, 0.5f);
			const double rx = q.x - eye[0], ry = q.y - eye[1], rz = q.z - eye[2];
			const double den = pn.x * rx + pn.y * ry + pn.z * rz;
			double t, P[3], clip[4];
			float depth;
			int s, i;

			if (!(den < 0))
			{
				continue;
			}

			t = (pd - (pn.x * eye[0] + pn.y * eye[1] + pn.z * eye[2])) / den;
			P[0] = eye[0] + rx * t;
			P[1] = eye[1] + ry * t;
			P[2] = eye[2] + rz * t;

			for (i = 0; i < 4; i++)
			{
				clip[i] = m[i] * P[0] + m[4 + i] * P[1] + m[8 + i] * P[2] + m[12 + i];
			}

			if (!(clip[3] > znear * 0.5) || (fabs(clip[2] / clip[3]) >= 1))
			{
				continue;
			}

			depth = (float)(clip[2] / clip[3] * 0.5 + 0.5);

			for (s = -4; s <= 4; s += 2)
			{
				/* the depth the attachment holds, moved by s float steps */
				const float dd = (s < 0) ? nextafterf(nextafterf(depth, 0), 0) : (s > 0) ?
					nextafterf(nextafterf(depth, 1), 1) : depth;
				const mtl_rtao_v3_t p = position(&v, px, py, (s == -4 || s == 4) ?
						((s < 0) ? nextafterf(nextafterf(dd, 0), 0) : nextafterf(nextafterf(dd, 1), 1)) : dd);
				const mtl_rtao_v3_t n = stored_normal(pn);
				const float de = sqrtf((p.x - eye[0]) * (p.x - eye[0]) + (p.y - eye[1]) * (p.y - eye[1]) +
						(p.z - eye[2]) * (p.z - eye[2]));
				const float b = mtl_rtao_bias(de, v.bias_k);
				const mtl_rtao_v3_t o = mtl_rtao_v3(p.x + n.x * b, p.y + n.y * b, p.z + n.z * b);
				const double above = pn.x * o.x + pn.y * o.y + pn.z * o.z - pd;
				unsigned f;
				float occ[2] = { 0, 0 };

				pixels++;

				if (!(above > 0))
				{
					below++;
				}

				for (f = 0; f < 64; f++)
				{
					unsigned r;

					for (r = 0; r < 2; r++)
					{
						const float u = mtl_rtao_noise(rank[mtl_rtao_tile_index((unsigned)px, (unsigned)py, 2 * r)], f, 2 * r);
						const float w = mtl_rtao_noise(rank[mtl_rtao_tile_index((unsigned)px, (unsigned)py, 2 * r + 1)], f,
								2 * r + 1);
						const mtl_rtao_v3_t d = mtl_rtao_dir(n, r, u, w);
						const double dn = pn.x * d.x + pn.y * d.y + pn.z * d.z;

						rays++;

						/* the plane, met from above within the range */
						if ((dn < 0) && (above / -dn < range))
						{
							hits++;
							occ[r] = mtl_rtao_occlusion(1, (float)(above / -dn), range);
						}
					}

					{
						const mtl_rgba_t l = { 0.3f, 0.6f, 0.9f, 1.0f };
						const mtl_rgba_t lo = mtl_rtao_light(l, MTL_GB_MAT_WORLD,
								mtl_rtao_factor(mtl_rtao_ao(occ[0], occ[1]), RM_RTAO_FLOOR));

						unlit += memcmp(&lo, &l, sizeof(l)) != 0;
					}
				}
			}
		}
	}

	CHECK(pixels > 20000);
	CHECK(below == 0);
	CHECK(hits == 0);
	CHECK(unlit == 0);

	if (below || hits || unlit)
	{
		printf("open plane: %d pixels, %d starts below, %d of %d rays met it, %d lights changed\n", pixels, below, hits,
				rays, unlit);
	}
}

static void
test_view(void)
{
	float proj[16], world[16];
	rm_viewport_t vp = { 3, 5, 640, 480, 0, 1 };
	const float eye[3] = { 10, 20, 30 };
	rm_rtao_view_t v;
	rm_rts_view_t s;

	mtl_mat_frustum(proj, -4, 4, -3, 3, 4, 4096);
	mtl_mat_identity(world);
	mtl_mat_rotate(world, -90, 1, 0, 0);
	mtl_mat_rotate(world, 90, 0, 0, 1);
	mtl_mat_translate(world, -eye[0], -eye[1], -eye[2]);

	CHECK(rm_rtao_view(proj, world, &vp, eye, 4, 100, 0.25f, 7, 2, &v) == 1);
	CHECK(rm_rts_view(proj, world, &vp, eye, 4, &s) == 1);
	CHECK(memcmp(v.inv, s.inv, sizeof(v.inv)) == 0);
	CHECK(memcmp(v.vp, s.vp, sizeof(v.vp)) == 0);
	CHECK(memcmp(v.eye, s.eye, sizeof(v.eye)) == 0);
	CHECK(v.bias_k == s.bias_k);
	CHECK((v.range == 100) && (v.floor_ == 0.25f) && (v.frame == 7) && (v.show == 2));
	CHECK((v.pad[0] == 0) && (v.pad[1] == 0) && (v.pad[2] == 0));

	CHECK(rm_rtao_view(proj, world, &vp, eye, 0, 100, 0.25f, 7, 2, &v) == 0);
	CHECK(rm_rtao_view(proj, world, &vp, eye, NAN, 100, 0.25f, 7, 2, &v) == 0);
	CHECK(sizeof(rm_rtao_view_t) == 128);
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

	/* with r_rt_gunshadow 1 the gun is an instance, for the shadows; AO rays pass it, and the viewer's model */
	n = rm_rt_instances(e, 4, 1, 1, 0, out, 8, &ex);
	CHECK(n == 5);

	for (i = 0; i < n; i++)
	{
		const int gun = (out[i].ent == 1), self = (out[i].ent == 2);
		const unsigned m0 = mtl_rtao_inst_mask(mtl_rts_inst_mask(0), out[i].noao);
		const unsigned m1 = mtl_rtao_inst_mask(mtl_rts_inst_mask(1), out[i].noao);

		CHECK(out[i].noao == (unsigned)(gun || self));
		CHECK(((m0 & MTL_RTAO_MASK) != 0) == !(gun || self));
		CHECK(((m1 & MTL_RTAO_MASK) != 0) == !(gun || self));

		/* the shadow rays' three masks meet each instance as before the AO bit */
		CHECK(((m0 & 0xffu) != 0) && ((m0 & MTL_RTS_MASK_OTHER) != 0) && ((m0 & MTL_RTS_MASK_CARRIER) == 0));
		CHECK(((m1 & 0xffu) != 0) && ((m1 & MTL_RTS_MASK_OTHER) == 0) && ((m1 & MTL_RTS_MASK_CARRIER) != 0));
	}

	/* without it the gun is no instance at all */
	n = rm_rt_instances(e, 4, 1, 0, 0, out, 8, &ex);
	CHECK((n == 4) && (ex.weapon == 1));
}

static void
test_stats(void)
{
	rm_rtao_stats_t s;
	char buf[RM_STATS_LINE], small[16];

	memset(&s, 0, sizeof(s));
	s.frame = 12;
	s.pixels = 2073600;
	s.traced = 1500000;
	s.occluded = 400000;
	s.floored = 3000;
	s.up = 600000;
	s.up_occluded = 90000;
	s.rays = 3000000;
	s.hits = 500000;
	s.ms = 1.25;

	CHECK(rm_rtao_stats_format(&s, buf, sizeof(buf)) == 1);
	CHECK(strstr(buf, "rt ao: frame 12") == buf);
	CHECK(strstr(buf, "1500000 traced") != NULL);
	CHECK(strstr(buf, "3000 at the floor") != NULL);
	CHECK(strstr(buf, "1.250 ms") != NULL);
	CHECK(strchr(buf, '\n') == NULL);

	s.ms = -1;
	CHECK(rm_rtao_stats_format(&s, buf, sizeof(buf)) == 1);
	CHECK(strstr(buf, "not measured") != NULL);

	CHECK(rm_rtao_stats_format(&s, small, sizeof(small)) == 0);
	CHECK(small[0] == 0);
	CHECK(rm_rtao_stats_format(&s, buf, 0) == 0);
}

int
main(void)
{
	test_on();
	test_cvars();
	test_bluenoise();
	test_noise();
	test_dir();
	test_occlusion();
	test_factor();
	test_open_plane();
	test_view();
	test_instances();
	test_stats();

	printf("rm_rtao_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
