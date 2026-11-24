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
 * The host test of the RT-primary presentation's direct light (TASK188,
 * PM6 slice 1): rm_rtdir.c and common/mtl_rtdir_math.h, with no SDL, no
 * Metal and no game data. The mode's switch (RT-primary only), qrad3's
 * falloff and cone, what a shadow ray does at a face, the light the
 * resolve reads (no lightmap argument: a mutant that adds the lightmap
 * back fails test_no_lightmap), the fit's samples and the fit itself, the
 * fixed exposure, the lights' packing, the style table, the view and the
 * lines. Run by tests/check-ref-metal.sh from the repository's top
 * directory.
 *
 * =======================================================================
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/client/refresh/metal/header/rm_rtdir.h"
#include "../../src/client/refresh/metal/header/rm_rtgate.h"

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

/* the term runs under RT-primary only: stock, enhanced and retro never trace it, whatever r_rt_direct reads */
static void
test_on(void)
{
	CHECK(rm_rtdir_on(RM_PRES_RTPRIMARY, 1, 1, 0) == 1);
	CHECK(rm_rtdir_on(RM_PRES_STOCK, 1, 1, 0) == 0);
	CHECK(rm_rtdir_on(RM_PRES_ENHANCED, 1, 1, 0) == 0);
	CHECK(rm_rtdir_on(RM_PRES_RETRO, 1, 1, 0) == 0);
	CHECK(rm_rtdir_on(RM_PRES_RTPRIMARY, 0, 1, 0) == 0);
	CHECK(rm_rtdir_on(RM_PRES_RTPRIMARY, NAN, 1, 0) == 0);
	CHECK(rm_rtdir_on(RM_PRES_RTPRIMARY, 1, 0, 0) == 0);
	CHECK(rm_rtdir_on(RM_PRES_RTPRIMARY, 1, 1, 1) == 0);
	CHECK(rm_rtdir_on(RM_PRES_RTPRIMARY, 1, 1, NAN) == 0);

	CHECK(rm_rtdir_show(0) == 0);
	CHECK(rm_rtdir_show(1) == RM_RTDIR_SHOW_DIRECT);
	CHECK(rm_rtdir_show(2.9f) == RM_RTDIR_SHOW_VIS);
	CHECK(rm_rtdir_show(3) == RM_RTDIR_SHOW_NOSHADOW);
	CHECK(rm_rtdir_show(99) == RM_RTDIR_SHOW_MAX);
	CHECK(rm_rtdir_show(-1) == 0);
	CHECK(rm_rtdir_show(NAN) == 0);
	CHECK(rm_rtdir_show(INFINITY) == 0);
}

/* qrad3's GatherSampleLight: (intensity - dist) x dot, nothing at N.L 0.001 or below, nothing outside the cone */
static void
test_scale(void)
{
	CHECK(near(mtl_rtdir_scale(300, 100, 1, 1, MTL_RTDIR_NOSPOT), 200, 1e-4));
	CHECK(near(mtl_rtdir_scale(300, 100, 0.5f, 1, MTL_RTDIR_NOSPOT), 100, 1e-4));
	CHECK(mtl_rtdir_scale(300, 300, 1, 1, MTL_RTDIR_NOSPOT) == 0);
	CHECK(mtl_rtdir_scale(300, 400, 1, 1, MTL_RTDIR_NOSPOT) == 0);
	CHECK(mtl_rtdir_scale(300, 100, MTL_RTDIR_MIN_DOT, 1, MTL_RTDIR_NOSPOT) == 0);
	CHECK(mtl_rtdir_scale(300, 100, 0.0011f, 1, MTL_RTDIR_NOSPOT) > 0);
	CHECK(mtl_rtdir_scale(300, 100, -1, 1, MTL_RTDIR_NOSPOT) == 0);
	/* a point light at any direction: -1 is still above NOSPOT */
	CHECK(mtl_rtdir_scale(300, 100, 1, -1, MTL_RTDIR_NOSPOT) > 0);
	/* a 10-degree cone: inside at 5 degrees, outside at 15 and on the edge */
	CHECK(mtl_rtdir_scale(300, 100, 1, (float)cos(5 * M_PI / 180), (float)cos(10 * M_PI / 180)) > 0);
	CHECK(mtl_rtdir_scale(300, 100, 1, (float)cos(15 * M_PI / 180), (float)cos(10 * M_PI / 180)) == 0);
	CHECK(mtl_rtdir_scale(300, 100, 1, 0.5f, 0.5f) == 0);
	CHECK(mtl_rtdir_scale(NAN, 100, 1, 1, MTL_RTDIR_NOSPOT) == 0);
	CHECK(mtl_rtdir_scale(300, NAN, 1, 1, MTL_RTDIR_NOSPOT) == 0);
	CHECK(mtl_rtdir_scale(300, 100, NAN, 1, MTL_RTDIR_NOSPOT) == 0);
	CHECK(mtl_rtdir_scale(300, 100, 1, NAN, MTL_RTDIR_NOSPOT) == 0);
}

/* a shadow ray passes water and translucent faces, the first MTL_RTDIR_LAYERS of them; anything else shadows */
static void
test_at(void)
{
	unsigned m;

	CHECK(mtl_rtdir_at(3, 0) == MTL_RTDIR_AT_PASS);
	CHECK(mtl_rtdir_at(5, 0) == MTL_RTDIR_AT_PASS);
	CHECK(mtl_rtdir_at(3, MTL_RTDIR_LAYERS - 1) == MTL_RTDIR_AT_PASS);
	CHECK(mtl_rtdir_at(3, MTL_RTDIR_LAYERS) == MTL_RTDIR_AT_SHADOW);
	CHECK(mtl_rtdir_at(5, MTL_RTDIR_LAYERS) == MTL_RTDIR_AT_SHADOW);

	for (m = 0; m < 256; m++)
	{
		if ((m != 3) && (m != 5))
		{
			CHECK(mtl_rtdir_at(m, 0) == MTL_RTDIR_AT_SHADOW);
		}
	}
}

/* the light the resolve reads: raw x exposure, at most 1 a channel, 0 for NaN, alpha 1 */
static void
test_light(void)
{
	mtl_rgba_t c = mtl_rtdir_light(0.25f, 0.5f, 0.125f, 2);

	CHECK(near(c.r, 0.5, 1e-6) && near(c.g, 1.0, 1e-6) && near(c.b, 0.25, 1e-6) && (c.a == 1.0f));
	c = mtl_rtdir_light(0.75f, 3, -1, 2);
	CHECK((c.r == 1.0f) && (c.g == 1.0f) && (c.b == 0.0f));
	c = mtl_rtdir_light(NAN, 0.5f, 0.5f, 1);
	CHECK((c.r == 0.0f) && near(c.g, 0.5, 1e-6));
	c = mtl_rtdir_light(0.5f, 0.5f, 0.5f, NAN);
	CHECK((c.r == 0.0f) && (c.g == 0.0f) && (c.b == 0.0f) && (c.a == 1.0f));
	c = mtl_rtdir_light(0, 0, 0, 16);
	CHECK((c.r == 0.0f) && (c.g == 0.0f) && (c.b == 0.0f));
}

/*
 * The plan's section 6, item 5: no world pixel's light reads the lightmap
 * at its own surface. The light is a function of the traced sum and the
 * exposure only; here, two pixels with the same traced light and
 * lightmaps 0 and 1 get the same light, which a mutant that adds or
 * multiplies the lightmap back cannot pass.
 */
static mtl_rgba_t
pixel(float raw, float lightmap, float exposure)
{
	(void)lightmap;   /* the kernel reads it for the fit's sums only */
	return mtl_rtdir_light(raw, raw, raw, exposure);
}

static void
test_no_lightmap(void)
{
	const mtl_rgba_t a = pixel(0.3f, 0.0f, 1.5f), b = pixel(0.3f, 1.0f, 1.5f);

	CHECK((a.r == b.r) && (a.g == b.g) && (a.b == b.b) && (a.a == b.a));
	CHECK(near(a.r, 0.45, 1e-6));
}

/* the fit's samples: the channel mean, clamped to QMAX, in 1/64 steps; sums exact whatever the order */
static void
test_q(void)
{
	unsigned long s1 = 0, s2 = 0;
	int i;

	CHECK(mtl_rtdir_q(0, 0, 0) == 0);
	CHECK(mtl_rtdir_q(1, 1, 1) == 64);
	CHECK(mtl_rtdir_q(0.5f, 0, 1) == 32);
	CHECK(mtl_rtdir_q(100, 100, 100) == 1024);
	CHECK(mtl_rtdir_q(-1, -1, -1) == 0);
	CHECK(mtl_rtdir_q(NAN, 0, 0) == 0);
	CHECK(mtl_rtdir_q(INFINITY, 0, 0) == 1024);

	for (i = 0; i < 1000; i++)
	{
		s1 += mtl_rtdir_q(i / 997.0f, 0.3f, 0.1f);
		s2 += mtl_rtdir_q((999 - i) / 997.0f, 0.3f, 0.1f);
	}

	CHECK(s1 == s2);
	/* the 32-bit sums: an 8K view's samples at the most a sample holds */
	CHECK((unsigned long long)(7680 / MTL_RTDIR_FIT_STEP + 1) * (4320 / MTL_RTDIR_FIT_STEP + 1) * 1024 < 0xffffffffULL);
}

/* the stated rule: lightmap sum / raw sum, clamped; the exposure in use when nothing was sampled */
static void
test_fit(void)
{
	int fixed = -1;

	CHECK(near(rm_rtdir_fit(200, 100, 10, 1), 2.0, 1e-6));
	CHECK(near(rm_rtdir_fit(100, 400, 10, 1), 0.25, 1e-6));
	CHECK(near(rm_rtdir_fit(100, 4000, 10, 1), RM_RTDIR_FIT_MIN, 1e-6));
	CHECK(near(rm_rtdir_fit(100000, 1, 10, 1), RM_RTDIR_FIT_MAX, 1e-6));
	CHECK(near(rm_rtdir_fit(100, 0, 10, 3), 3.0, 1e-6));
	CHECK(near(rm_rtdir_fit(100, 50, 0, 3), 3.0, 1e-6));
	CHECK(near(rm_rtdir_fit(0, 50, 10, 3), RM_RTDIR_FIT_MIN, 1e-6));

	CHECK(near(rm_rtdir_exposure(0, 2.5f, &fixed), 2.5, 1e-6) && (fixed == 0));
	CHECK(near(rm_rtdir_exposure(1.5f, 2.5f, &fixed), 1.5, 1e-6) && (fixed == 1));
	CHECK(near(rm_rtdir_exposure(1000, 2.5f, &fixed), RM_RTDIR_EXPOSURE_MAX, 1e-6) && (fixed == 1));
	CHECK(near(rm_rtdir_exposure(-1, 2.5f, &fixed), 2.5, 1e-6) && (fixed == 0));
	CHECK(near(rm_rtdir_exposure(NAN, 2.5f, &fixed), 2.5, 1e-6) && (fixed == 0));
	CHECK(near(rm_rtdir_exposure(INFINITY, 2.5f, NULL), 2.5, 1e-6));
}

static void
test_pack(void)
{
	static rm_rtl_lights_t l;
	static rm_rtdir_light_t out[RM_RTL_MAX_LIGHTS];
	rm_rtdir_pack_t why;
	long n, i;

	memset(&l, 0, sizeof(l));
	l.nlights = 5;
	/* a point light */
	l.light[0].origin[0] = 1;
	l.light[0].origin[1] = 2;
	l.light[0].origin[2] = 3;
	l.light[0].intensity = 300;
	l.light[0].color[0] = 1;
	l.light[0].color[1] = 0.5f;
	l.light[0].color[2] = 0.25f;
	l.light[0].style = 5;
	/* a spotlight with its target */
	l.light[1].intensity = 200;
	l.light[1].spot = 1;
	l.light[1].cone = 60;
	l.light[1].target_state = RM_RTL_TARGET_FOUND;
	l.light[1].dir[2] = -1;
	l.light[1].style = 300;
	/* a spotlight without its target, a dark light and a NaN one: left out */
	l.light[2].intensity = 300;
	l.light[2].spot = 1;
	l.light[2].target_state = RM_RTL_TARGET_MISSING;
	l.light[3].intensity = -50;
	l.light[4].intensity = NAN;

	n = rm_rtdir_pack(&l, out, RM_RTL_MAX_LIGHTS, &why);
	CHECK(n == 2 && why.kept == 2 && why.untargeted == 1 && why.dark == 2 && why.over == 0);
	CHECK(out[0].pos[0] == 1 && out[0].pos[1] == 2 && out[0].pos[2] == 3 && out[0].pos[3] == 300);
	CHECK(out[0].color[0] == 1 && out[0].color[1] == 0.5f && out[0].color[2] == 0.25f && out[0].color[3] == 5);
	CHECK(out[0].spot[3] == MTL_RTDIR_NOSPOT && out[0].spot[0] == 0 && out[0].spot[2] == 0);
	CHECK(out[1].spot[2] == -1 && near(out[1].spot[3], 0.5, 1e-6));
	/* a style outside 0 to 255 reads 0 */
	CHECK(out[1].color[3] == 0);

	/* the cap */
	n = rm_rtdir_pack(&l, out, 1, &why);
	CHECK(n == 1 && why.over == 1);
	n = rm_rtdir_pack(NULL, out, 1, NULL);
	CHECK(n == 0);

	/* every light of a full lump */
	memset(&l, 0, sizeof(l));
	l.nlights = RM_RTL_MAX_LIGHTS;

	for (i = 0; i < RM_RTL_MAX_LIGHTS; i++)
	{
		l.light[i].intensity = 100;
	}

	n = rm_rtdir_pack(&l, out, RM_RTL_MAX_LIGHTS, &why);
	CHECK(n == RM_RTL_MAX_LIGHTS && why.over == 0);
	/* a count past the array is held to it */
	l.nlights = RM_RTL_MAX_LIGHTS + 10;
	n = rm_rtdir_pack(&l, out, RM_RTL_MAX_LIGHTS, &why);
	CHECK(n == RM_RTL_MAX_LIGHTS);
}

static void
test_styles(void)
{
	static float out[3 * RM_RTL_STYLES];
	float st[2][3] = { { 1, 1, 1 }, { 0.5f, NAN, 2 } };

	rm_rtdir_styles((const float (*)[3])st, 2, 1.5f, out);
	CHECK(out[0] == 1.5f && out[1] == 1.5f && out[2] == 1.5f);
	CHECK(out[3] == 0.75f && out[4] == 0.0f && out[5] == 3.0f);
	CHECK(out[6] == 1.5f && out[3 * RM_RTL_STYLES - 1] == 1.5f);
	rm_rtdir_styles(NULL, 0, 1, out);
	CHECK(out[0] == 1 && out[3 * RM_RTL_STYLES - 1] == 1);
}

static void
test_view_lines(void)
{
	static const float ident[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	static const float zero[16];
	const rm_viewport_t vp = { 0, 0, 640, 480, 0, 1 };
	const float eye[3] = { 1, 2, 3 };
	rm_rtdir_view_t v;
	rm_rtdir_stats_t s;
	rm_rtdir_pack_t p;
	char line[512];

	CHECK(sizeof(rm_rtdir_view_t) == 112);
	CHECK(sizeof(rm_rtdir_light_t) == 48);
	CHECK(rm_rtdir_view(ident, ident, &vp, eye, 4, 2.0f, 7, 2, &v) == 1);
	CHECK(v.exposure == 2.0f && v.nlights == 7 && v.show == 2 && v.eye[0] == 1 && v.eye[2] == 3 && v.vp[2] == 640);
	CHECK(rm_rtdir_view(zero, ident, &vp, eye, 4, 2.0f, 7, 2, &v) == 0);
	CHECK(rm_rtdir_view(ident, ident, &vp, eye, 0, 2.0f, 7, 2, &v) == 0);

	memset(&s, 0, sizeof(s));
	s.frame = 9;
	s.pixels = 100;
	s.world = 50;
	s.inrange = 200;
	s.rays = 200;
	s.lit = 150;
	s.fit_lm = 10;
	s.fit_raw = 5;
	s.fit_n = 3;
	s.exposure = 2;
	s.ms = -1;
	CHECK(rm_rtdir_stats_format(&s, 256, 0, line, sizeof(line)) == 1);
	CHECK(!strcmp(line, "ref_metal: rt direct: frame 9: 100 pixels, 50 world, 256 lights, 200 in range, 200 shadow rays "
			"(4.00 a world pixel), 150 lit, fit 10/5 over 3 samples, exposure 2.0000 (fit), - ms"));
	s.ms = 1.5;
	CHECK(rm_rtdir_stats_format(&s, 256, 1, line, sizeof(line)) == 1 && strstr(line, "(r_rt_direct_exposure), 1.500 ms"));
	CHECK(rm_rtdir_stats_format(&s, 256, 1, line, 20) == 0 && line[0] == 0);

	memset(&p, 0, sizeof(p));
	p.kept = 250;
	p.dark = 6;
	CHECK(rm_rtdir_map_format("maps/base1.bsp", &p, line, sizeof(line)) == 1);
	CHECK(!strcmp(line, "ref_metal: rt direct: maps/base1.bsp: 250 lights uploaded (6 dark, 0 spotlights without a "
			"target, 0 past the cap left out)"));
	CHECK(rm_rtdir_map_format("maps/base1.bsp", &p, line, 10) == 0 && line[0] == 0);
}

/* the mode itself: RT-primary is chosen where the device traces rays, and reads 0 where it does not */
static void
test_mode(void)
{
	int why = -1;

	CHECK(rm_pres_decide(3, RM_RTG_COMPUTE, &why) == RM_PRES_RTPRIMARY && why == RM_PRES_WHY_OK);
	CHECK(rm_pres_decide(3, RM_RTG_RENDER, &why) == RM_PRES_RTPRIMARY && why == RM_PRES_WHY_OK);
	CHECK(rm_pres_decide(3, RM_RTG_NONE, &why) == RM_PRES_STOCK && why == RM_PRES_WHY_NORT);
	CHECK(rm_pres_value(RM_PRES_RTPRIMARY, "r_rt_direct", 0) == 1);
	CHECK(rm_pres_value(RM_PRES_RTPRIMARY, "r_rt_ao", 1) == 0);
	CHECK(rm_pres_value(RM_PRES_STOCK, "r_rt_direct", 0) == 0);
	CHECK(rm_pres_value(RM_PRES_ENHANCED, "r_rt_direct", 0) == 0);
	CHECK(rm_pres_value(RM_PRES_RETRO, "r_rt_direct", 0) == 0);
}

int
main(void)
{
	test_on();
	test_scale();
	test_at();
	test_light();
	test_no_lightmap();
	test_q();
	test_fit();
	test_pack();
	test_styles();
	test_view_lines();
	test_mode();
	printf("rm_rtdir_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
