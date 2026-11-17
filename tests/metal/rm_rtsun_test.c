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
 * The host test of ref_metal's ray-traced sun and sky shadow (RT4,
 * TASK56): rm_rtsun.c and common/mtl_rtsun_math.h, with no SDL, no Metal
 * and no game data. The per-map table's parser (bad lines, unknown map,
 * the pitch's and yaw's bounds) and the committed table itself, the
 * direction from pitch and yaw, the sky-area share and its 2 % threshold,
 * the lightmap floor's gate, the term's arithmetic, sky_color, the view
 * and the stats lines. Run by tests/check-ref-metal.sh from the
 * repository's top directory.
 *
 * =======================================================================
 */

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/client/refresh/metal/header/rm_rtsun.h"
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

static void
test_cvars(void)
{
	CHECK(rm_rtsun_on(1, 1, 0) == 1);
	CHECK(rm_rtsun_on(-1, 1, 0) == 1);
	CHECK(rm_rtsun_on(0, 1, 0) == 0);
	CHECK(rm_rtsun_on(1, 0, 0) == 0);       /* no ray tracing on the device */
	CHECK(rm_rtsun_on(1, 1, 1) == 0);       /* r_fullbright: no lightmap for the floor gate */
	CHECK(rm_rtsun_on(NAN, 1, 0) == 0);
	CHECK(rm_rtsun_on(INFINITY, 1, 0) == 0);
	/* r_fullbright as rm_surf.c reads it (any value but 0, NaN among them, drops the lightmaps): no sun */
	CHECK(rm_rtsun_on(1, 1, NAN) == 0);
	CHECK(rm_rtsun_on(1, 1, INFINITY) == 0);
	CHECK(rm_rtsun_on(1, 1, -1) == 0);

	/* the scene the sun asks for: none on a map under 2 % sky, so nothing is built or kept for it */
	CHECK(rm_rtsun_wanted(1, 1, 1) == 1);
	CHECK(rm_rtsun_wanted(-1, 1, 1) == 1);
	CHECK(rm_rtsun_wanted(1, 1, 0) == 0);
	CHECK(rm_rtsun_wanted(1, 1, rm_rtsun_sky_enough(1.999, 100)) == 0);
	CHECK(rm_rtsun_wanted(1, 1, rm_rtsun_sky_enough(2, 100)) == 1);
	CHECK(rm_rtsun_wanted(0, 1, 1) == 0);
	CHECK(rm_rtsun_wanted(1, 0, 1) == 0);
	CHECK(rm_rtsun_wanted(NAN, 1, 1) == 0);

	CHECK(rm_rtsun_scale(0.35f) == 0.35f);
	CHECK(rm_rtsun_scale(0) == 0.0f);
	CHECK(rm_rtsun_scale(-1) == 0.0f);
	CHECK(rm_rtsun_scale(100) == RM_RTSUN_SCALE_MAX);
	CHECK(rm_rtsun_scale(NAN) == RM_RTSUN_SCALE);
	CHECK(rm_rtsun_scale(INFINITY) == RM_RTSUN_SCALE);

	CHECK(rm_rtsun_floor(0.5f) == 0.5f);
	CHECK(rm_rtsun_floor(-0.5f) == 0.0f);
	CHECK(rm_rtsun_floor(2) == 1.0f);
	CHECK(rm_rtsun_floor(NAN) == RM_RTSUN_FLOOR);

	CHECK(rm_rtsun_show(0) == 0);
	CHECK(rm_rtsun_show(1) == 1);
	CHECK(rm_rtsun_show(1.9f) == 1);
	CHECK(rm_rtsun_show(2) == 2);
	CHECK(rm_rtsun_show(9) == RM_RTSUN_SHOW_MAX);
	CHECK(rm_rtsun_show(-3) == 0);
	CHECK(rm_rtsun_show(NAN) == 0);
}

static void
test_angles(void)
{
	static const char *const bad[] = {
		"", "   ", "55", "55 ", "55 45 1", "55,45", "a b", "55 b", "55 45x", "x55 45",
		"91 0", "90.001 0", "-1 0", "-0.5 0", "55 360", "55 -1", "55 720", "nan 0", "55 nan",
		"inf 0", "1e9 0", "55 1e30", "0x10 0", "55 45 #",
	};
	float p, y;
	size_t i;

	CHECK(rm_rtsun_parse_angles("55 45", &p, &y) == 1 && (p == 55.0f) && (y == 45.0f));
	CHECK(rm_rtsun_parse_angles("  90\t0 ", &p, &y) == 1 && (p == 90.0f) && (y == 0.0f));
	CHECK(rm_rtsun_parse_angles("0 359.5", &p, &y) == 1 && (p == 0.0f) && (y == 359.5f));
	CHECK(rm_rtsun_parse_angles("12.25 270", &p, &y) == 1 && (p == 12.25f) && (y == 270.0f));
	CHECK(rm_rtsun_parse_angles("+30 +60", &p, &y) == 1 && (p == 30.0f) && (y == 60.0f));

	for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
	{
		p = y = -7;

		if (rm_rtsun_parse_angles(bad[i], &p, &y) != 0 || (p != -7) || (y != -7))
		{
			printf("bad angles taken: \"%s\"\n", bad[i]);
			CHECK(0);
		}
		else
		{
			CHECK(1);
		}
	}

	CHECK(rm_rtsun_parse_angles(NULL, &p, &y) == 0);
}

static void
test_dir(void)
{
	float d[3];
	int pi, yi;

	rm_rtsun_dir(90, 0, d);
	CHECK(near(d[0], 0, 1e-6) && near(d[1], 0, 1e-6) && near(d[2], 1, 1e-6));
	rm_rtsun_dir(0, 0, d);
	CHECK(near(d[0], 1, 1e-6) && near(d[1], 0, 1e-6) && near(d[2], 0, 1e-6));
	rm_rtsun_dir(0, 90, d);
	CHECK(near(d[0], 0, 1e-6) && near(d[1], 1, 1e-6) && near(d[2], 0, 1e-6));
	rm_rtsun_dir(0, 180, d);
	CHECK(near(d[0], -1, 1e-6) && near(d[1], 0, 1e-6));
	rm_rtsun_dir(0, 270, d);
	CHECK(near(d[0], 0, 1e-6) && near(d[1], -1, 1e-6));

	/* the default, 55 45: up and toward +x +y */
	rm_rtsun_dir(RM_RTSUN_PITCH, RM_RTSUN_YAW, d);
	CHECK(near(d[0], cos(55 * M_PI / 180) * cos(45 * M_PI / 180), 1e-6));
	CHECK(near(d[1], cos(55 * M_PI / 180) * sin(45 * M_PI / 180), 1e-6));
	CHECK(near(d[2], sin(55 * M_PI / 180), 1e-6));

	/* a unit vector at every pitch and yaw of the bounds, never below the horizon */
	for (pi = 0; pi <= 90; pi += 5)
	{
		for (yi = 0; yi < 360; yi += 15)
		{
			rm_rtsun_dir((float)pi, (float)yi, d);
			CHECK(near(d[0] * d[0] + d[1] * d[1] + d[2] * d[2], 1, 1e-5) && (d[2] >= 0));
		}
	}
}

static void
test_table(void)
{
	static const char text[] =
		"# the table's own comment\n"
		"\n"
		"base1 55 45\n"
		"  base2\t30   120   # a comment after the line\n"
		"q2dm1 90 0\r\n"
		"bad name 10 10\n"                  /* 5: an extra field */
		"maps/base3 10 10\n"                /* 6: '/' is not in a name */
		"base3 10\n"                        /* 7: no yaw */
		"base3 91 10\n"                     /* 8: pitch past 90 */
		"base3 10 360\n"                    /* 9: yaw at 360 */
		"base3 -1 10\n"                     /* 10: pitch below 0 */
		"BASE1 20 20\n"                     /* 11: base1 again (names have no case) */
		"x123456789012345678901234567890123456789012345678901234567890abcd 1 1\n"   /* 12: 64 characters */
		"base3 10 350.5\n"
		"#base4 10 10\n"
		"   \t \n"
		"unit-1_b 0 0";                     /* the last line without a newline */
	rm_rtsun_entry_t t[8], small[2];
	const rm_rtsun_entry_t *e;
	int n, nbad, first;

	n = rm_rtsun_table_parse(text, t, 8, &nbad, &first);
	CHECK(n == 5);
	CHECK(nbad == 8);
	CHECK(first == 6);

	e = rm_rtsun_table_find(t, n, "base1");
	CHECK(e && (e->pitch == 55.0f) && (e->yaw == 45.0f));
	e = rm_rtsun_table_find(t, n, "BASE2");
	CHECK(e && (e->pitch == 30.0f) && (e->yaw == 120.0f));
	e = rm_rtsun_table_find(t, n, "q2dm1");
	CHECK(e && (e->pitch == 90.0f) && (e->yaw == 0.0f));
	e = rm_rtsun_table_find(t, n, "base3");
	CHECK(e && (e->pitch == 10.0f) && (e->yaw == 350.5f));
	e = rm_rtsun_table_find(t, n, "unit-1_b");
	CHECK(e && (e->pitch == 0.0f) && (e->yaw == 0.0f));

	/* an unknown map, a part of a name, an empty one: no entry */
	CHECK(rm_rtsun_table_find(t, n, "base4") == NULL);
	CHECK(rm_rtsun_table_find(t, n, "base") == NULL);
	CHECK(rm_rtsun_table_find(t, n, "base11") == NULL);
	CHECK(rm_rtsun_table_find(t, n, "") == NULL);
	CHECK(rm_rtsun_table_find(t, n, NULL) == NULL);
	CHECK(rm_rtsun_table_find(t, 0, "base1") == NULL);

	/* past cap: the good lines that do not fit are bad */
	n = rm_rtsun_table_parse("a 1 1\nb 2 2\nc 3 3\n", small, 2, &nbad, &first);
	CHECK((n == 2) && (nbad == 1) && (first == 3));

	/* nothing: no entry, nothing bad */
	n = rm_rtsun_table_parse("", t, 8, &nbad, &first);
	CHECK((n == 0) && (nbad == 0) && (first == 0));
	n = rm_rtsun_table_parse(NULL, t, 8, &nbad, &first);
	CHECK((n == 0) && (nbad == 0) && (first == 0));
}

/* the committed table, read from the repository: every line good */
static void
test_committed_table(void)
{
	static char text[65536];
	rm_rtsun_entry_t t[256];
	FILE *f = fopen("scripts/rt/sun-angles.txt", "rb");
	size_t len;
	int n, nbad = -1, first = -1;

	CHECK(f != NULL);

	if (!f)
	{
		return;
	}

	len = fread(text, 1, sizeof(text) - 1, f);
	CHECK(!ferror(f) && (len < sizeof(text) - 1));
	fclose(f);
	text[len] = 0;

	n = rm_rtsun_table_parse(text, t, 256, &nbad, &first);
	CHECK(n >= 1);
	CHECK((nbad == 0) && (first == 0));
	printf("scripts/rt/sun-angles.txt: %d maps, %d bad lines\n", n, nbad);
}

static void
test_map_key(void)
{
	char k[16];

	CHECK(rm_rtsun_map_key("maps/base1.bsp", k, sizeof(k)) == 1 && !strcmp(k, "base1"));
	CHECK(rm_rtsun_map_key("base2.bsp", k, sizeof(k)) == 1 && !strcmp(k, "base2"));
	CHECK(rm_rtsun_map_key("maps/q2dm1.BSP", k, sizeof(k)) == 1 && !strcmp(k, "q2dm1"));
	CHECK(rm_rtsun_map_key("maps/sub/city1.bsp", k, sizeof(k)) == 1 && !strcmp(k, "city1"));
	CHECK(rm_rtsun_map_key("maps/noext", k, sizeof(k)) == 1 && !strcmp(k, "noext"));
	CHECK(rm_rtsun_map_key("maps/.bsp", k, sizeof(k)) == 0 && (k[0] == 0));
	CHECK(rm_rtsun_map_key("maps/", k, sizeof(k)) == 0 && (k[0] == 0));
	CHECK(rm_rtsun_map_key("", k, sizeof(k)) == 0 && (k[0] == 0));
	CHECK(rm_rtsun_map_key(NULL, k, sizeof(k)) == 0 && (k[0] == 0));
	CHECK(rm_rtsun_map_key("maps/a_very_long_map_name.bsp", k, sizeof(k)) == 0 && (k[0] == 0));
	CHECK(rm_rtsun_map_key("maps/abcdefghijklmnop.bsp", k, 16) == 0);      /* 16 characters: no room for the end */
	CHECK(rm_rtsun_map_key("maps/abcdefghijklmno.bsp", k, 16) == 1 && !strcmp(k, "abcdefghijklmno"));
}

/* a quad of the world's vertices, two triangles; its records are added by the caller */
static unsigned long
quad(rm_wvert_t *v, unsigned long nv, unsigned *idx, unsigned long ni, const float (*p)[3])
{
	int i;

	for (i = 0; i < 4; i++)
	{
		memset(&v[nv + i], 0, sizeof(v[0]));
		v[nv + i].pos[0] = p[i][0];
		v[nv + i].pos[1] = p[i][1];
		v[nv + i].pos[2] = p[i][2];
		v[nv + i].pos[3] = 1;
	}

	idx[ni + 0] = (unsigned)nv;
	idx[ni + 1] = (unsigned)nv + 1;
	idx[ni + 2] = (unsigned)nv + 2;
	idx[ni + 3] = (unsigned)nv;
	idx[ni + 4] = (unsigned)nv + 2;
	idx[ni + 5] = (unsigned)nv + 3;
	return nv + 4;
}

static void
rec(rm_surfrec_t *r, unsigned texflags, unsigned first, unsigned num)
{
	memset(r, 0, sizeof(*r));
	r->texflags = texflags;
	r->first_index = first;
	r->num_indices = num;
}

static void
test_sky_area(void)
{
	/* the floor 100 x 100, the sky above it w x 100, a wall 100 x 50, and an inline model's quad after the world */
	rm_wvert_t v[16];
	unsigned idx[24];
	rm_surfrec_t r[4];
	double sky, total;
	const float floor_[4][3] = { { 0, 0, 0 }, { 100, 0, 0 }, { 100, 100, 0 }, { 0, 100, 0 } };
	const float wall[4][3] = { { 0, 0, 0 }, { 0, 0, 50 }, { 0, 100, 50 }, { 0, 100, 0 } };
	const float door[4][3] = { { 10, 10, 10 }, { 90, 10, 10 }, { 90, 90, 10 }, { 10, 90, 10 } };
	float skyq[4][3] = { { 0, 0, 200 }, { 100, 0, 200 }, { 100, 100, 200 }, { 0, 100, 200 } };
	unsigned long nv;

	nv = quad(v, 0, idx, 0, floor_);
	nv = quad(v, nv, idx, 6, (const float (*)[3])skyq);
	nv = quad(v, nv, idx, 12, wall);
	nv = quad(v, nv, idx, 18, door);
	rec(&r[0], 0, 0, 6);
	rec(&r[1], RM_TEX_SKY, 6, 6);
	rec(&r[2], RM_TEX_TRANS33, 12, 6);
	rec(&r[3], RM_TEX_SKY, 18, 6);        /* the inline model's, outside the world's range */

	/* the world (indices 0 to 17): 10000 + 10000 + 5000, the sky 10000 */
	CHECK(rm_rtsun_sky_area(r, 4, v, nv, idx, 24, 0, 18, &sky, &total) == 1);
	CHECK(near(sky, 10000, 1e-6) && near(total, 25000, 1e-6));
	CHECK(rm_rtsun_sky_enough(sky, total) == 1);

	/* every triangle of the buffer: the inline model's sky counts */
	CHECK(rm_rtsun_sky_area(r, 4, v, nv, idx, 24, 0, 24, &sky, &total) == 1);
	CHECK(near(sky, 16400, 1e-6) && near(total, 31400, 1e-6));

	/* a sky of a given share of the world's area: the threshold at 2 % */
	{
		const double shares[] = { 0.0, 0.0199, 0.02, 0.0201, 0.5 };
		const int enough[] = { 0, 0, 1, 1, 1 };
		size_t i;

		for (i = 0; i < sizeof(shares) / sizeof(shares[0]); i++)
		{
			/* sky area s of a total 15000 + s: s = share x 15000 / (1 - share) */
			const double s = shares[i] * 15000.0 / (1.0 - shares[i]);
			const float w = (float)(s / 100.0);

			skyq[1][0] = skyq[2][0] = w;
			quad(v, 4, idx, 6, (const float (*)[3])skyq);
			CHECK(rm_rtsun_sky_area(r, 4, v, nv, idx, 24, 0, 18, &sky, &total) == 1);
			CHECK(near(sky / total, shares[i], 1e-6));
			/* the float corners put the 2 % case up to 1e-5 square units under: 1e-3 of a unit over it */
			CHECK(rm_rtsun_sky_enough(sky + ((shares[i] == 0.02) ? 1e-3 : 0), total) == enough[i]);
		}
	}

	CHECK(rm_rtsun_sky_enough(2, 100) == 1);
	CHECK(rm_rtsun_sky_enough(1.999, 100) == 0);
	CHECK(rm_rtsun_sky_enough(0, 0) == 0);
	CHECK(rm_rtsun_sky_enough(1, 0) == 0);
	CHECK(rm_rtsun_sky_enough(NAN, 100) == 0);
	CHECK(rm_rtsun_sky_enough(1, INFINITY) == 0);

	/* an index outside the vertices, a record outside the indices: refused, both 0 */
	idx[4] = 99;
	CHECK(rm_rtsun_sky_area(r, 4, v, nv, idx, 24, 0, 18, &sky, &total) == 0 && (sky == 0) && (total == 0));
	idx[4] = 2;
	rec(&r[2], 0, 12, 30);
	CHECK(rm_rtsun_sky_area(r, 4, v, nv, idx, 24, 0, 24, &sky, &total) == 0 && (sky == 0) && (total == 0));
	rec(&r[2], 0, 12, 5);   /* not a multiple of 3 */
	CHECK(rm_rtsun_sky_area(r, 4, v, nv, idx, 24, 0, 18, &sky, &total) == 0);
	rec(&r[2], 0, 12, 6);
	CHECK(rm_rtsun_sky_area(r, 4, v, nv, idx, 24, 0, 30, &sky, &total) == 0);   /* the range past the buffer */
	CHECK(rm_rtsun_sky_area(r, 0, v, nv, idx, 24, 0, 18, &sky, &total) == 1 && (total == 0));
	CHECK(rm_rtsun_sky_enough(sky, total) == 0);
}

static void
test_sky_color(void)
{
	unsigned char px[4 * 4];
	float m[3], c[3];
	float means[6][3];
	int have[6], i;

	for (i = 0; i < 4; i++)
	{
		px[i * 4 + 0] = (unsigned char)(i * 60);
		px[i * 4 + 1] = 100;
		px[i * 4 + 2] = 255;
		px[i * 4 + 3] = (unsigned char)(i * 10);   /* the alpha is left out */
	}

	rm_rtsun_mean(px, 4, m);
	CHECK(near(m[0], 90.0 / 255, 1e-6) && near(m[1], 100.0 / 255, 1e-6) && near(m[2], 1, 1e-6));
	rm_rtsun_mean(px, 0, m);
	CHECK((m[0] == 0) && (m[1] == 0) && (m[2] == 0));

	/* five faces above the horizon, the bottom one left out; brightest channel 1 */
	for (i = 0; i < 6; i++)
	{
		means[i][0] = 0.2f;
		means[i][1] = 0.4f;
		means[i][2] = 0.1f;
		have[i] = 1;
	}

	means[5][0] = 1;
	means[5][1] = 0;
	means[5][2] = 0;
	rm_rtsun_sky_color((const float (*)[3])means, have, c);
	CHECK(near(c[0], 0.5, 1e-6) && near(c[1], 1, 1e-6) && near(c[2], 0.25, 1e-6));

	/* a face without an image is left out of the mean */
	means[0][0] = 1;
	means[0][1] = 1;
	means[0][2] = 1;
	have[0] = 0;
	rm_rtsun_sky_color((const float (*)[3])means, have, c);
	CHECK(near(c[0], 0.5, 1e-6) && near(c[1], 1, 1e-6) && near(c[2], 0.25, 1e-6));

	/* no image, or a black sky: white */
	for (i = 0; i < 6; i++)
	{
		have[i] = 0;
	}

	rm_rtsun_sky_color((const float (*)[3])means, have, c);
	CHECK((c[0] == 1) && (c[1] == 1) && (c[2] == 1));

	for (i = 0; i < 6; i++)
	{
		have[i] = 1;
		means[i][0] = means[i][1] = means[i][2] = 0;
	}

	rm_rtsun_sky_color((const float (*)[3])means, have, c);
	CHECK((c[0] == 1) && (c[1] == 1) && (c[2] == 1));
	rm_rtsun_sky_color((const float (*)[3])means, NULL, c);
	CHECK((c[0] == 1) && (c[1] == 1) && (c[2] == 1));
}

static mtl_rgba_t
rgba(float r, float g, float b, float a)
{
	mtl_rgba_t c;

	c.r = r;
	c.g = g;
	c.b = b;
	c.a = a;
	return c;
}

static void
test_gate(void)
{
	CHECK(mtl_rtsun_gated(rgba(0.2f, 0.3f, 0.1f, 1), 0.5f) == 1);
	CHECK(mtl_rtsun_gated(rgba(0.5f, 0.3f, 0.1f, 1), 0.5f) == 0);    /* at the floor: no term */
	CHECK(mtl_rtsun_gated(rgba(0.2f, 0.6f, 0.1f, 1), 0.5f) == 0);    /* the brightest channel decides */
	CHECK(mtl_rtsun_gated(rgba(0.2f, 0.3f, 0.51f, 1), 0.5f) == 0);
	CHECK(mtl_rtsun_gated(rgba(0.499f, 0.499f, 0.499f, 0), 0.5f) == 1);
	CHECK(mtl_rtsun_gated(rgba(0, 0, 0, 1), 0) == 0);                /* floor 0: never */
	CHECK(mtl_rtsun_gated(rgba(1, 1, 1, 1), 1) == 0);
	CHECK(mtl_rtsun_gated(rgba(0.99f, 0.99f, 0.99f, 1), 1) == 1);
	CHECK(mtl_rtsun_gated(rgba(NAN, 0.1f, 0.1f, 1), 0.5f) == 0);
	CHECK(mtl_rtsun_gated(rgba(0.1f, NAN, 0.1f, 1), 0.5f) == 0);

	/* every 8-bit lightmap level against the default floor: below 128 / 255 */
	{
		int k, n = 0;

		for (k = 0; k < 256; k++)
		{
			n += mtl_rtsun_gated(rgba(k / 255.0f, 0, 0, 1), RM_RTSUN_FLOOR);
		}

		CHECK(n == 128);
	}
}

static void
test_ndotl_at(void)
{
	const mtl_rtao_v3_t up = mtl_rtao_v3(0, 0, 1);
	float d[3];

	rm_rtsun_dir(55, 45, d);
	CHECK(near(mtl_rtsun_ndotl(up, mtl_rtao_v3(d[0], d[1], d[2])), sin(55 * M_PI / 180), 1e-6));
	CHECK(mtl_rtsun_ndotl(up, mtl_rtao_v3(0, 0, 1)) == 1.0f);
	CHECK(mtl_rtsun_ndotl(up, mtl_rtao_v3(1, 0, 0)) == 0.0f);
	CHECK(mtl_rtsun_ndotl(up, mtl_rtao_v3(0, 0, -1)) == 0.0f);
	CHECK(mtl_rtsun_ndotl(up, mtl_rtao_v3(0, 0.99999f, 0.03f)) == 0.0f);     /* below 1/32: no ray */
	CHECK(mtl_rtsun_ndotl(up, mtl_rtao_v3(0, 0.99f, MTL_RTAO_MIN_COS)) == MTL_RTAO_MIN_COS);

	CHECK(mtl_rtsun_at(4) == MTL_RTSUN_AT_LIT);
	CHECK(mtl_rtsun_at(3) == MTL_RTSUN_AT_PASS);
	CHECK(mtl_rtsun_at(5) == MTL_RTSUN_AT_PASS);
	CHECK(mtl_rtsun_at(0) == MTL_RTSUN_AT_SHADOW);
	CHECK(mtl_rtsun_at(1) == MTL_RTSUN_AT_SHADOW);
	CHECK(mtl_rtsun_at(2) == MTL_RTSUN_AT_SHADOW);
	CHECK(mtl_rtsun_at(0xffu) == MTL_RTSUN_AT_SHADOW);       /* an alias model */
	CHECK(mtl_rtsun_at(6) == MTL_RTSUN_AT_SHADOW);
}

/*
 * rm_rtsun_hit's loop (rm_rtsun.metal) over the faces a ray meets, the
 * nearest first: mat[0..n) their material ids, nothing behind them. 1 lit,
 * 0 shadowed; *casts counts the intersections it asked for.
 */
static int
rtsun_hit(const unsigned *mat, int n, int *casts)
{
	int layer;

	*casts = 0;

	for (layer = 0; layer < MTL_RTSUN_LAYERS; layer++)
	{
		unsigned at;

		(*casts)++;

		if (layer >= n)
		{
			return 0;
		}

		at = mtl_rtsun_at(mat[layer]);

		if (at != MTL_RTSUN_AT_PASS)
		{
			return (at == MTL_RTSUN_AT_LIT) ? 1 : 0;
		}
	}

	return 0;
}

/* the kernel's text holds the loop rtsun_hit mirrors */
static void
test_layers_kernel(void)
{
	static char text[65536];
	FILE *f = fopen("src/client/refresh/metal/rm_rtsun.metal", "rb");
	size_t len;

	CHECK(f != NULL);

	if (!f)
	{
		return;
	}

	len = fread(text, 1, sizeof(text) - 1, f);
	CHECK(!ferror(f) && (len < sizeof(text) - 1));
	fclose(f);
	text[len] = 0;

	CHECK(strstr(text, "for (int layer = 0; layer <= MTL_RTSUN_LAYERS; layer++)") != NULL);
	CHECK(strstr(text, "at = mtl_rtsun_step(mtl_rtsun_at(m), layer);") != NULL);
}

/* up to MTL_RTSUN_LAYERS water or translucent faces are passed, and what is behind them decides */
static void
test_layers(void)
{
	unsigned mat[16];
	int k, i, casts, ok_sky = 1, ok_wall = 1, ok_none = 1, ok_casts = 1;

	for (k = 0; k <= 8; k++)
	{
		for (i = 0; i < k; i++)
		{
			mat[i] = (i & 1) ? 5u : 3u;    /* water and translucent faces in turn */
		}

		/* the sky behind k of them: lit up to MTL_RTSUN_LAYERS, shadowed past that */
		mat[k] = MTL_GB_MAT_SKY;

		if (rtsun_hit(mat, k + 1, &casts) != ((k <= MTL_RTSUN_LAYERS) ? 1 : 0))
		{
			printf("FAIL the sky behind %d water or translucent faces\n", k);
			ok_sky = 0;
		}

		ok_casts &= (casts >= 1) && (casts <= MTL_RTSUN_LAYERS + 1);

		/* a wall behind them, or nothing at all: shadowed */
		mat[k] = MTL_GB_MAT_WORLD;
		ok_wall &= rtsun_hit(mat, k + 1, &casts) == 0;
		ok_casts &= (casts >= 1) && (casts <= MTL_RTSUN_LAYERS + 1);
		ok_none &= rtsun_hit(mat, k, &casts) == 0;
		ok_casts &= (casts >= 1) && (casts <= MTL_RTSUN_LAYERS + 1);
	}

	CHECK(ok_sky);
	CHECK(ok_wall);
	CHECK(ok_none);
	CHECK(ok_casts);

	/* the cases the review named: 3 and 4 faces before the sky lit, 5 shadowed */
	for (i = 0; i < 5; i++)
	{
		mat[i] = 5u;
	}

	mat[3] = MTL_GB_MAT_SKY;
	CHECK(rtsun_hit(mat, 4, &casts) == 1 && casts == 4);
	mat[3] = 5u;
	mat[4] = MTL_GB_MAT_SKY;
	CHECK(rtsun_hit(mat, 5, &casts) == 1 && casts == 5);
	mat[4] = 5u;
	mat[5] = MTL_GB_MAT_SKY;
	CHECK(rtsun_hit(mat, 6, &casts) == 0 && casts == 5);

	test_layers_kernel();
}

static int
same(mtl_rgba_t a, mtl_rgba_t b)
{
	return !memcmp(&a, &b, sizeof(a));
}

static void
test_term(void)
{
	const mtl_rgba_t lm = rgba(0.2f, 0.25f, 0.1f, 0.75f);
	const float s[3] = { 0.35f * 0.5f, 0.35f * 1.0f, 0.35f * 0.25f };   /* sky_color (0.5, 1, 0.25) x 0.35 */
	const float nl = 0.8f;
	mtl_rgba_t c;
	unsigned m, cls;

	/* sky_color x scale x vis x N.L, added */
	c = mtl_rtsun_light(lm, 1, MTL_RTSUN_C_LIT, 1.0f, nl, s[0], s[1], s[2]);
	CHECK(near(c.r, 0.2 + 0.175 * 0.8, 1e-6) && near(c.g, 0.25 + 0.35 * 0.8, 1e-6) && near(c.b, 0.1 + 0.0875 * 0.8, 1e-6));
	CHECK(c.a == lm.a);

	/* the 8-bit visibility scales it */
	c = mtl_rtsun_light(lm, 1, MTL_RTSUN_C_LIT, 128 / 255.0f, nl, s[0], s[1], s[2]);
	CHECK(near(c.g, 0.25 + 0.35 * 0.8 * 128 / 255.0, 1e-6));

	/* each channel at most 1 */
	c = mtl_rtsun_light(rgba(0.9f, 0.95f, 0.99f, 1), 1, MTL_RTSUN_C_LIT, 1, 1, 0.35f, 0.35f, 0.35f);
	CHECK((c.r == 1) && (c.g == 1) && (c.b == 1));

	/* no ray lit, no term: every other class and material bit for bit */
	for (m = 0; m < 8; m++)
	{
		for (cls = 0; cls <= MTL_RTSUN_C_LIT; cls++)
		{
			c = mtl_rtsun_light(lm, m, cls, 1, nl, s[0], s[1], s[2]);
			CHECK(((m == 1) && (cls == MTL_RTSUN_C_LIT)) ? !same(c, lm) : same(c, lm));
		}
	}

	/* a zero scale or N.L leaves the light as it was */
	c = mtl_rtsun_light(lm, 1, MTL_RTSUN_C_LIT, 1, nl, 0, 0, 0);
	CHECK(same(c, lm));
	c = mtl_rtsun_light(lm, 1, MTL_RTSUN_C_LIT, 1, 0, s[0], s[1], s[2]);
	CHECK(same(c, lm));

	/* after RT5: AO scales the lightmap term only, the sun is added to lightmap x AO */
	{
		const mtl_rgba_t ao = mtl_rtao_light(lm, 1, mtl_rtao_factor(0.5f, 0.4f));

		c = mtl_rtsun_light(ao, 1, MTL_RTSUN_C_LIT, 1, nl, s[0], s[1], s[2]);
		CHECK(near(c.r, 0.2 * 0.5 + 0.175 * 0.8, 1e-6) && near(c.g, 0.25 * 0.5 + 0.35 * 0.8, 1e-6));
	}
}

static void
test_view(void)
{
	float proj[16], world[16], dir[3];
	rm_viewport_t vp = { 3, 5, 640, 480, 0, 1 };
	const float eye[3] = { 10, 20, 30 };
	const float sky[3] = { 0.5f, 1, 0.25f };
	rm_rtsun_view_t v;
	rm_rts_view_t s;

	mtl_mat_frustum(proj, -4, 4, -3, 3, 4, 4096);
	mtl_mat_identity(world);
	mtl_mat_rotate(world, -90, 1, 0, 0);
	mtl_mat_rotate(world, 90, 0, 0, 1);
	mtl_mat_translate(world, -eye[0], -eye[1], -eye[2]);
	rm_rtsun_dir(55, 45, dir);

	CHECK(rm_rtsun_view(proj, world, &vp, eye, 4, dir, sky, 0.35f, 0.5f, 2, &v) == 1);
	CHECK(rm_rts_view(proj, world, &vp, eye, 4, &s) == 1);
	CHECK(memcmp(v.inv, s.inv, sizeof(v.inv)) == 0);
	CHECK(memcmp(v.vp, s.vp, sizeof(v.vp)) == 0);
	CHECK(memcmp(v.eye, s.eye, sizeof(v.eye)) == 0);
	CHECK(v.bias_k == s.bias_k);
	CHECK((v.dir[0] == dir[0]) && (v.dir[1] == dir[1]) && (v.dir[2] == dir[2]) && (v.dir[3] == 0));
	CHECK((v.sun[0] == 0.5f * 0.35f) && (v.sun[1] == 0.35f) && (v.sun[2] == 0.25f * 0.35f) && (v.sun[3] == 0));
	CHECK((v.floor_ == 0.5f) && (v.show == 2) && (v.pad == 0));

	CHECK(rm_rtsun_view(proj, world, &vp, eye, 0, dir, sky, 0.35f, 0.5f, 0, &v) == 0);
	CHECK(rm_rtsun_view(proj, world, &vp, eye, NAN, dir, sky, 0.35f, 0.5f, 0, &v) == 0);
	CHECK(sizeof(rm_rtsun_view_t) == 144);
}

static void
test_stats(void)
{
	rm_rtsun_stats_t s;
	char buf[RM_STATS_LINE], small[16];
	const char *want = "ref_metal: rt sun: frame 7: 307200 pixels, 200000 world, 50000 bright, 30000 facing away, "
		"120000 rays, 80000 lit, 0.250 ms";

	memset(&s, 0, sizeof(s));
	s.frame = 7;
	s.pixels = 307200;
	s.world = 200000;
	s.bright = 50000;
	s.facing = 30000;
	s.rays = 120000;
	s.lit = 80000;
	s.ms = 0.25;
	CHECK(rm_rtsun_stats_format(&s, buf, sizeof(buf)) == 1 && !strcmp(buf, want));
	s.ms = -1;
	CHECK(rm_rtsun_stats_format(&s, buf, sizeof(buf)) == 1 && strstr(buf, "80000 lit, - ms"));
	CHECK(rm_rtsun_stats_format(&s, small, sizeof(small)) == 0 && (small[0] == 0));
	CHECK(rm_rtsun_stats_format(&s, buf, 0) == 0);

	CHECK(rm_rtsun_map_format("base1", 12000, 300000, 1, 60, 135, buf, sizeof(buf)) == 1);
	CHECK(!strcmp(buf, "ref_metal: rt sun: base1: sky 12000 of 300000 square units (4.00 %), the pass runs; "
			"the sun at pitch 60 yaw 135 (scripts/rt/sun-angles.txt)"));
	CHECK(rm_rtsun_map_format("base1", 5000, 300000, 0, 55, 45, buf, sizeof(buf)) == 1);
	CHECK(!strcmp(buf, "ref_metal: rt sun: base1: sky 5000 of 300000 square units (1.67 %), under 2 %: the pass "
			"is skipped; the sun at pitch 55 yaw 45 (r_rt_sun_angles)"));
	CHECK(rm_rtsun_map_format("base1", 0, 0, 0, 55, 45, buf, sizeof(buf)) == 1 && strstr(buf, "(0.00 %)"));
	CHECK(rm_rtsun_map_format("base1", 1, 2, 0, 55, 45, small, sizeof(small)) == 0 && (small[0] == 0));
}

int
main(void)
{
	test_cvars();
	test_angles();
	test_dir();
	test_table();
	test_committed_table();
	test_map_key();
	test_sky_area();
	test_sky_color();
	test_gate();
	test_ndotl_at();
	test_layers();
	test_term();
	test_view();
	test_stats();

	printf("rm_rtsun_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
