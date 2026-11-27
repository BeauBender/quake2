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
#include "../../src/client/refresh/metal/common/mtl_gbuf_math.h"
#include "../../src/client/refresh/metal/common/mtl_shader_math.h"
#include "../../src/client/refresh/metal/common/mtl_rtup_math.h"

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

	/* the step: a pass past the first MTL_RTSUN_LAYERS shadows; the sky and a shadow stand */
	CHECK(mtl_rtsun_step(MTL_RTSUN_AT_PASS, 0) == MTL_RTSUN_AT_PASS);
	CHECK(mtl_rtsun_step(MTL_RTSUN_AT_PASS, MTL_RTSUN_LAYERS - 1) == MTL_RTSUN_AT_PASS);
	CHECK(mtl_rtsun_step(MTL_RTSUN_AT_PASS, MTL_RTSUN_LAYERS) == MTL_RTSUN_AT_SHADOW);
	CHECK(mtl_rtsun_step(MTL_RTSUN_AT_LIT, MTL_RTSUN_LAYERS) == MTL_RTSUN_AT_LIT);
	CHECK(mtl_rtsun_step(MTL_RTSUN_AT_SHADOW, 0) == MTL_RTSUN_AT_SHADOW);
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

	for (layer = 0; layer <= MTL_RTSUN_LAYERS; layer++)
	{
		unsigned at;

		(*casts)++;

		if (layer >= n)
		{
			return 0;
		}

		at = mtl_rtsun_step(mtl_rtsun_at(mat[layer]), layer);

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
	/* TASK126 slice 3: the view's divisor is 1, every pixel traced, until the scene sets r_rt_sun_scale's */
	CHECK((v.floor_ == 0.5f) && (v.show == 2) && (v.div == 1));

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

	/* TASK126 slice 3: at divisor 1 (or 0, a stats slot never traced) the line is the one above */
	s.ms = 0.25;
	s.div = 1;
	s.tw = 640;
	s.th = 480;
	s.fallbacks = 9;
	CHECK(rm_rtsun_stats_format(&s, buf, sizeof(buf)) == 1 && !strcmp(buf, want));
	s.div = 0;
	CHECK(rm_rtsun_stats_format(&s, buf, sizeof(buf)) == 1 && !strcmp(buf, want));

	/* below it, the traced size and the upsample's fallbacks */
	s.div = 2;
	s.tw = 320;
	s.th = 240;
	CHECK(rm_rtsun_stats_format(&s, buf, sizeof(buf)) == 1);
	CHECK(!strcmp(buf, "ref_metal: rt sun: frame 7: 307200 pixels, 200000 world, 50000 bright, 30000 facing away, "
			"120000 rays, 80000 lit, 0.250 ms, traced at 320x240, 9 upsample fallbacks"));
	CHECK(rm_rtsun_stats_format(&s, buf, strlen(want) + 4) == 0 && (buf[0] == 0));

	CHECK(rm_rtsun_map_format("base1", 12000, 300000, 1, 60, 135, buf, sizeof(buf)) == 1);
	CHECK(!strcmp(buf, "ref_metal: rt sun: base1: sky 12000 of 300000 square units (4.00 %), the pass runs; "
			"the sun at pitch 60 yaw 135 (scripts/rt/sun-angles.txt)"));
	CHECK(rm_rtsun_map_format("base1", 5000, 300000, 0, 55, 45, buf, sizeof(buf)) == 1);
	CHECK(!strcmp(buf, "ref_metal: rt sun: base1: sky 5000 of 300000 square units (1.67 %), under 2 %: the pass "
			"is skipped; the sun at pitch 55 yaw 45 (r_rt_sun_angles)"));
	CHECK(rm_rtsun_map_format("base1", 0, 0, 0, 55, 45, buf, sizeof(buf)) == 1 && strstr(buf, "(0.00 %)"));
	CHECK(rm_rtsun_map_format("base1", 1, 2, 0, 55, 45, small, sizeof(small)) == 0 && (small[0] == 0));
}

/*
 * TASK126 slice 6, the double-count invariant's rule for the sun at any
 * scale (design section 5), on mtl_rtsun_light alone: a pixel with
 * visibility 0, of any class, and a pixel of any class but lit, or of
 * another material than the world, at any visibility, takes the incoming
 * light bit for bit. test_up_no_double_count holds the upsample's half.
 */
static void
test_no_double_count(void)
{
	unsigned k, cls, m;
	int ok = 1;

	srand(126);

	for (k = 0; k < 20000; k++)
	{
		const mtl_rgba_t base = rgba((float)rand() / (float)RAND_MAX, (float)rand() / (float)RAND_MAX,
				(float)rand() / (float)RAND_MAX, (float)rand() / (float)RAND_MAX);
		const float nl = (float)rand() / (float)RAND_MAX, vis = (float)rand() / (float)RAND_MAX;
		mtl_rgba_t c;

		/* lit, but no visibility: nothing added */
		c = mtl_rtsun_light(base, 1u, MTL_RTSUN_C_LIT, 0.0f, nl, 0.35f, 0.4f, 0.45f);
		ok &= !memcmp(&c, &base, sizeof(c));

		/* any visibility, any material, every class but lit: nothing added */
		for (cls = MTL_RTSUN_C_NONE; cls < MTL_RTSUN_C_LIT; cls++)
		{
			for (m = 0; m < 6; m++)
			{
				c = mtl_rtsun_light(base, m, cls, vis, nl, 0.35f, 0.4f, 0.45f);
				ok &= !memcmp(&c, &base, sizeof(c));
			}
		}

		/* lit with a visibility, of another material than the world: nothing added */
		c = mtl_rtsun_light(base, 3u, MTL_RTSUN_C_LIT, vis, nl, 0.35f, 0.4f, 0.45f);
		ok &= !memcmp(&c, &base, sizeof(c));
	}

	CHECK(ok);
}

/*
 * Review round 1 (spec I1): a drawn pixel below divisor 1, composed as
 * rm_rtsun_up composes it after the floor and the facing test: the
 * weights at the view's divisor (mtl_rtup_weights), the visibility under
 * them with the sun's neutral value 0 (mtl_rtup_value), the class from
 * the visibility, then the light with the pixel's own N.L
 * (mtl_rtsun_light). check-ref-metal's sun kernel pins hold each of
 * these calls in the kernel (weights1, neutral1, vis1).
 */
static mtl_rgba_t
sun_up_pixel(mtl_rtup_in_t in, unsigned div, const float *tv, mtl_rgba_t base, float nl, unsigned *cls)
{
	const mtl_rtup_out_t o = mtl_rtup_weights(in, div);
	const float vis = mtl_rtup_value(o, tv[0], tv[1], tv[2], tv[3], 0.0f);

	*cls = (vis > 0.0f) ? MTL_RTSUN_C_LIT : MTL_RTSUN_C_SHADOW;
	return mtl_rtsun_light(base, in.m, *cls, vis, nl, 0.35f, 0.4f, 0.45f);
}

static float
frand(void)
{
	return (float)rand() / (float)RAND_MAX;
}

/*
 * The sun's half of the double-count invariant on the upsample's path, at
 * divisors 2 and 3: where every tap's visibility is 0 (each tap with a
 * sample or not, on the surface or across a depth step, so the weights
 * or the fallback), the visibility is 0 and the light is unchanged bit
 * for bit; a pixel no tap has a sample for (or whose own depth is not
 * finite) is empty, and the light is unchanged too. A tap of visibility 1
 * on the surface adds the term, so the composition is not vacuous.
 */
static void
test_up_no_double_count(void)
{
	unsigned div, k, i, cls;
	int ok_zero = 1, ok_empty = 1, ok_lit = 1, empties = 0, fallbacks = 0, weighted = 0;

	srand(1261);

	for (div = 2; div <= 3; div++)
	{
		for (k = 0; k < 20000; k++)
		{
			const unsigned lw = 214, lh = 161, px = (unsigned)rand() % (lw * div), py = (unsigned)rand() % (lh * div);
			const mtl_rtao_pair_t qx = mtl_rtao_up_pair(px, div, lw), qy = mtl_rtao_up_pair(py, div, lh);
			const mtl_rgba_t base = rgba(0.9f * frand(), 0.9f * frand(), 0.9f * frand(), frand());
			const float nl = 0.05f + 0.95f * frand();
			const float zero[MTL_RTUP_TAPS] = { 0.0f, 0.0f, 0.0f, 0.0f };
			float one[MTL_RTUP_TAPS];
			mtl_rtup_in_t in;
			mtl_rtup_out_t o;
			mtl_rgba_t c;

			memset(&in, 0, sizeof(in));
			in.d = 64.0f + 2000.0f * frand();
			in.n = mtl_rtao_v3(0.0f, 0.0f, 1.0f);
			in.m = MTL_GB_MAT_WORLD;
			in = mtl_rtup_bilinear(in, qx, qy);

			for (i = 0; i < MTL_RTUP_TAPS; i++)
			{
				in.ok[i] = (rand() % 3) != 0;
				in.dn[i] = (rand() % 4) ? in.d * (1.0f + 0.001f * frand()) : in.d * 2.0f;   /* a depth step */
				in.nn[i] = in.n;
				in.mn[i] = (rand() % 8) ? MTL_GB_MAT_WORLD : 3u;
				one[i] = 1.0f;
			}

			/* every tap at 0: visibility 0, the light unchanged */
			o = mtl_rtup_weights(in, div);
			empties += o.empty;
			fallbacks += o.fallback;
			weighted += !o.empty && !o.fallback;
			c = sun_up_pixel(in, div, zero, base, nl, &cls);
			ok_zero &= (cls == MTL_RTSUN_C_SHADOW) && !memcmp(&c, &base, sizeof(c));

			/* no tap with a sample: empty, the light unchanged */
			for (i = 0; i < MTL_RTUP_TAPS; i++)
			{
				in.ok[i] = 0;
			}

			ok_empty &= mtl_rtup_weights(in, div).empty;
			c = sun_up_pixel(in, div, one, base, nl, &cls);
			ok_empty &= (cls == MTL_RTSUN_C_SHADOW) && !memcmp(&c, &base, sizeof(c));

			/* the pixel's own depth not finite: empty whatever its taps hold, the light unchanged */
			for (i = 0; i < MTL_RTUP_TAPS; i++)
			{
				in.ok[i] = 1;
				in.dn[i] = in.d;
				in.mn[i] = MTL_GB_MAT_WORLD;
			}

			in.d = INFINITY;
			ok_empty &= mtl_rtup_weights(in, div).empty;
			c = sun_up_pixel(in, div, one, base, nl, &cls);
			ok_empty &= (cls == MTL_RTSUN_C_SHADOW) && !memcmp(&c, &base, sizeof(c));

			/* every tap at 1 on the surface: lit, the term added */
			in.d = in.dn[0];
			c = sun_up_pixel(in, div, one, base, nl, &cls);
			ok_lit &= (cls == MTL_RTSUN_C_LIT) && (c.r > base.r) && (c.g > base.g) && (c.b > base.b) && (c.a == base.a);
		}
	}

	CHECK(ok_zero);
	CHECK(ok_empty);
	CHECK(ok_lit);
	/* the cases all occur among the random pixels with every tap at 0 */
	CHECK((empties > 0) && (fallbacks > 0) && (weighted > 0));
}

/*
 * TASK126 slice 6, hold round 2: the map summary's totals equal the sums
 * of the r_rt_stats 2 lines of the same frames. rm_rtsunscene.c hands each
 * stats slot to rm_rtt_up_frame (through RM_RT_UpFrame) and formats the
 * same slot's line; the slots come in slot order, not frame order, at a
 * divisor 1 between, with the old map's frames in flight at the map's
 * first read. Parsed back as the hold reads them ("frame N: P pixels" and
 * ", traced at TWxTH, F upsample fallbacks"), the lines of the frames
 * numbered from the map's first sum to the totals: a frame drawn below
 * the traced size has its line, and a line of the map before is the only
 * one the totals leave out.
 */
static void
test_up_lines_sum(void)
{
	static const struct
	{
		unsigned long frame;
		unsigned div;
		unsigned long tw, fallbacks;
	} in[] = {
		{ 8, 3u, 214, 40 }, { 9, 3u, 214, 41 },   /* the map before's, in flight */
		{ 10, 2u, 320, 933 }, { 12, 2u, 320, 670 }, { 11, 2u, 320, 0 },
		{ 14, 2u, 320, 5 }, { 13, 1u, 640, 0 },   /* r_rt_sun_scale 1 for a frame, handed over late */
		{ 18, 1u, 640, 0 }, { 19, 2u, 320, 2 },   /* and for a frame in order */
		{ 16, 3u, 214, 7 }, { 15, 2u, 320, 1 }, { 17, 3u, 214, 0 },
	};
	const unsigned long from = 10;
	rm_rtt_up_t u;
	rm_rtsun_stats_t s;
	char buf[RM_STATS_LINE];
	unsigned long long lp = 0, lf = 0;
	unsigned long n, p, tw, th, f, counted = 0;
	size_t i;
	const char *t;
	int ok_line = 1;

	memset(&u, 0, sizeof(u));
	rm_rtt_up_map(&u, from);

	for (i = 0; i < sizeof(in) / sizeof(in[0]); i++)
	{
		memset(&s, 0, sizeof(s));
		s.frame = in[i].frame;
		s.div = in[i].div;
		s.w = 640;
		s.h = 480;
		s.pixels = s.w * s.h;
		s.tw = in[i].tw;
		s.th = (in[i].div > 1u) ? s.h / in[i].div + ((s.h % in[i].div) != 0) : s.h;
		s.fallbacks = (in[i].div > 1u) ? in[i].fallbacks : 0;
		s.ms = 0.25;

		/* the scene's order: the totals, then the line */
		rm_rtt_up_frame(&u, s.frame, s.div, s.tw, s.th, s.w * s.h, s.fallbacks);
		ok_line &= rm_rtsun_stats_format(&s, buf, sizeof(buf));

		/* the hold's reading */
		t = strstr(buf, ", traced at ");
		ok_line &= sscanf(buf, "ref_metal: rt sun: frame %lu: %lu pixels,", &n, &p) == 2;
		ok_line &= (n == in[i].frame) && (p == s.pixels);

		if (in[i].div <= 1u)
		{
			ok_line &= (t == NULL);
			continue;
		}

		ok_line &= (t != NULL) && (sscanf(t, ", traced at %lux%lu, %lu upsample fallbacks", &tw, &th, &f) == 3);
		ok_line &= (t != NULL) && !strcmp(t + strlen(t) - strlen(" upsample fallbacks"), " upsample fallbacks");
		ok_line &= (tw == s.tw) && (th == s.th) && (f == s.fallbacks);

		if (n >= from)
		{
			lp += p;
			lf += f;
			counted++;
		}
	}

	CHECK(ok_line);
	CHECK(counted == 8);
	CHECK(u.pixels == lp);
	CHECK(u.fallbacks == lf);
	CHECK((u.pixels == 8ull * 307200) && (u.fallbacks == 933 + 670 + 0 + 5 + 2 + 7 + 1 + 0));
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
	test_no_double_count();
	test_up_no_double_count();
	test_up_lines_sum();

	printf("rm_rtsun_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
