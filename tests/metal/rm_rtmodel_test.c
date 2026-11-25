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
 * The host test of RT-primary's light on alias models (TASK191 slice 1):
 * rm_rtmodel.c and common/mtl_rtm_math.h, with no SDL, no Metal and no
 * game data. The colour rule held to gl1's rm_alias_shade and
 * rm_alias_color (rm_pure.c) to the bit, the split under the mode held to
 * rm_gb_entity_split, which models are traced, the tag through 8- and
 * 16-bit stores, the view weapon's unprojection against its own
 * projection and the pull-back point. Run by tests/check-ref-metal.sh
 * from the repository's top directory.
 *
 * =======================================================================
 */

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/client/refresh/metal/header/rm_rtmodel.h"
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

/* the flags the colour rule reads, every combination of which the parity runs */
static const int color_bits[] = {
	RM_RF_MINLIGHT, RM_RF_GLOW, RM_RF_FULLBRIGHT, RM_RF_WEAPONMODEL, RM_RF_IR_VISIBLE, RM_RF_SHELL_RED,
	RM_RF_SHELL_GREEN, RM_RF_SHELL_BLUE, RM_RF_SHELL_DOUBLE, RM_RF_SHELL_HALF_DAM
};
#define NCOLOR_BITS (int)(sizeof(color_bits) / sizeof(color_bits[0]))

/* lights around the minlight floor (0.1, and 0.1f, which is above 0.1), dim, bright and past overbright */
static const float lights[][3] = {
	{0, 0, 0}, {0.05f, 0.02f, 0.08f}, {0.1f, 0.1f, 0.1f}, {0.1f, 0.05f, 0.0f}, {0.099f, 0.1f, 0.0999f},
	{0.11f, 0.02f, 0.0f}, {0.35f, 0.3f, 0.25f}, {0.5f, 0.75f, 1.0f}, {0.8f, 0.8f, 0.8f}, {1.3f, 0.9f, 0.2f},
	{2.5f, 2.5f, 2.5f}, {0.6f, 0.0f, 0.95f}
};
#define NLIGHTS (int)(sizeof(lights) / sizeof(lights[0]))

static const float times[] = {0.0f, 0.3f, 1.1f, 7.77f};
static const float overbrights[] = {0.0f, 1.0f, 2.0f, 4.0f};

static unsigned char minlight16[256], gamma1[256], gamma12[256];

/* gl1_image.c, R_InitImages's gamma table, as rm_image.c builds it */
static void
gamma_table(float g, unsigned char *t)
{
	int i;

	if (g == 1)
	{
		for (i = 0; i < 256; i++)
		{
			t[i] = i;
		}

		return;
	}

	g = 1.0f / g;

	for (i = 0; i < 256; i++)
	{
		float inf = pow((float)(i + 0.5f) / 255.5f, g) * 255.0f + 0.5f;

		inf = inf < 0 ? 0 : (inf > 255 ? 255 : inf);
		t[i] = inf;
	}
}

/* gl1's colour of a model vertex, as R_DrawAliasModel builds it: rm_alias_shade, then rm_alias_color */
static void
gl1_color(const float *point, float dot, int flags, int rdflags, float time, float ob, const unsigned char *ml,
		const unsigned char *gm, float *rgba)
{
	float shadelight[3];
	const int shell = (flags & RM_RF_SHELLS_RGB) != 0;

	rm_alias_shade(flags, rdflags, point, time, ob, shadelight);
	rm_alias_color(shell ? 0 : dot, shadelight, shell, ml, gm, rm_alias_alpha(flags, 0.5f), rgba);
}

static int
same4(const float *a, const float *b)
{
	return (a[0] == b[0]) && (a[1] == b[1]) && (a[2] == b[2]) && (a[3] == b[3]);
}

static void
test_color_parity(void)
{
	float dots[9];
	int c, d, k;

	/* gl1's shadedots: a spread of r_avertexnormal_dots, 1 among them */
	for (d = 0; d < 8; d++)
	{
		dots[d] = rm_shadedot(d * 2, d * 31 + 7);
	}

	dots[8] = 1.0f;

	for (c = 0; c < (1 << NCOLOR_BITS); c++)
	{
		int flags = 0, rd, stock_bad = 0, traced_bad = 0, linear_bad = 0;

		for (k = 0; k < NCOLOR_BITS; k++)
		{
			if (c & (1 << k))
			{
				flags |= color_bits[k];
			}
		}

		for (rd = 0; rd < 2; rd++)
		{
			const int rdflags = rd ? RM_RDF_IRGOGGLES : 0;
			const int stock = rm_rtm_stock_light(flags, rdflags);
			const int linear = !stock && !(flags & (RM_RF_MINLIGHT | RM_RF_GLOW));
			int l, t, o, tb;

			for (l = 0; l < NLIGHTS; l++)
			for (t = 0; t < (int)(sizeof(times) / sizeof(times[0])); t++)
			for (o = 0; o < (int)(sizeof(overbrights) / sizeof(overbrights[0])); o++)
			for (tb = 0; tb < 3; tb++)
			{
				const unsigned char *ml = (tb == 1) ? minlight16 : NULL;
				const unsigned char *gm = (tb == 2) ? gamma12 : gamma1;

				for (d = 0; d < 9; d++)
				{
					float ref[4], got[4], lit[3];

					/* the rule with gl1's own light and shadedot is gl1's, for every flag combination */
					gl1_color(lights[l], dots[d], flags, rdflags, times[t], overbrights[o], ml, gm, ref);
					rm_rtm_color(lights[l], dots[d], flags, rdflags, times[t], overbrights[o], ml, gm,
							rm_alias_alpha(flags, 0.5f), got);
					stock_bad += !same4(ref, got);

					/* a traced light equal to point x shadedot, with the traced factor of 1 */
					lit[0] = lights[l][0] * dots[d];
					lit[1] = lights[l][1] * dots[d];
					lit[2] = lights[l][2] * dots[d];
					rm_rtm_color(lit, MTL_RTM_DOT_TRACED, flags, rdflags, times[t], overbrights[o], ml, gm,
							rm_alias_alpha(flags, 0.5f), got);

					/* the floor and the pulse come before gl1's shadedot: equal at a shadedot of 1 alone */
					if (dots[d] == 1.0f)
					{
						traced_bad += !stock && !same4(ref, got);
					}
					else if (linear)
					{
						linear_bad += !same4(ref, got);
					}
				}
			}
		}

		CHECK(stock_bad == 0);
		CHECK(traced_bad == 0);
		CHECK(linear_bad == 0);

		if (stock_bad || traced_bad || linear_bad)
		{
			printf("  flags 0x%x: %d %d %d\n", flags, stock_bad, traced_bad, linear_bad);
		}
	}
}

static void
test_color_rule(void)
{
	const float dim[3] = {0.02f, 0.05f, 0.08f};
	const float black[3] = {0, 0, 0};
	float got[4];
	mtl_rtm_idx_t i;

	/* the minlight floor on the traced light: a dim weapon pixel is 0.1, gl1's 25 */
	rm_rtm_color(dim, MTL_RTM_DOT_TRACED, RM_RF_MINLIGHT | RM_RF_WEAPONMODEL | RM_RF_DEPTHHACK, 0, 0, 0, NULL, gamma1,
			255, got);
	CHECK(got[0] == 25 / 255.0f && got[1] == 25 / 255.0f && got[2] == 25 / 255.0f && got[3] == 1.0f);
	rm_rtm_color(black, MTL_RTM_DOT_TRACED, RM_RF_MINLIGHT, 0, 0, 0, NULL, gamma1, 255, got);
	CHECK(got[0] == 25 / 255.0f);
	rm_rtm_color(black, MTL_RTM_DOT_TRACED, 0, 0, 0, 0, NULL, gamma1, 255, got);
	CHECK(got[0] == 0);

	/* the pulse: 0.1 sin(7 t) added, never below 0.8 of the light */
	i = mtl_rtm_color(mtl_rtm_v3(0.5f, 0.5f, 0.5f), 1.0f, MTL_RTM_GLOW, rm_rtm_glow_scale(0.3f), 0);
	CHECK(i.r == (int)((0.5f + (float)(0.1 * sin(0.3f * 7))) * 255.0f));
	CHECK(rm_rtm_glow_scale(0.3f) > 0.08f);
	i = mtl_rtm_color(mtl_rtm_v3(0.05f, 0.5f, 0.5f), 1.0f, MTL_RTM_GLOW, -0.1f, 0);
	CHECK(i.r == (int)(0.04f * 255.0f));

	/* overbright, the clamp and a NaN */
	i = mtl_rtm_color(mtl_rtm_v3(0.4f, 0.7f, NAN), 1.0f, 0, 0, 2.0f);
	CHECK(i.r == (int)(0.8f * 255.0f) && i.g == 255 && i.b == 0);
	i = mtl_rtm_color(mtl_rtm_v3(-1.0f, INFINITY, 0.5f), 1.0f, 0, 0, 0);
	CHECK(i.r == 0 && i.g == 255 && i.b == 127);

	CHECK(rm_rtm_flags(RM_RF_GLOW) == MTL_RTM_GLOW);
	CHECK(rm_rtm_flags(RM_RF_MINLIGHT | RM_RF_DEPTHHACK | RM_RF_WEAPONMODEL) == (MTL_RTM_MINLIGHT | MTL_RTM_WEAPON));
	CHECK(rm_rtm_flags(RM_RF_FULLBRIGHT | RM_RF_IR_VISIBLE) == 0);
}

#define KINDS 4

static void
test_split_off(void)
{
	int flags, kind, s, rd, bad = 0;

	/* every flag combination from RF_MINLIGHT to RF_USE_DISGUISE, each kind, both r_shadows */
	for (flags = 0; flags < (1 << 19); flags++)
	for (kind = 0; kind < KINDS; kind++)
	for (s = 0; s < 2; s++)
	for (rd = 0; rd < 2; rd++)
	{
		bad += rm_rtm_entity_split(flags, kind, (float)s, rd ? RM_RDF_IRGOGGLES : 0, 0) !=
			rm_gb_entity_split(flags, kind, (float)s);
	}

	CHECK(bad == 0);
}

static void
test_split_mode(void)
{
	const int A = RM_GB_KIND_ALIAS, B = RM_GB_KIND_BRUSH, R = RM_GB_ENT_RASTER, F = RM_GB_ENT_FORWARD;
	const int S = RM_GB_ENT_SHADOW, IR = RM_RDF_IRGOGGLES;
	const int gun = RM_RF_MINLIGHT | RM_RF_DEPTHHACK | RM_RF_WEAPONMODEL;
	int flags, kind, s, rd, bad = 0;

	/* the models that are traced: an opaque alias model, a glowing item, a minlight model, IR without goggles */
	CHECK(rm_rtm_entity_split(0, A, 0, 0, 1) == R);
	CHECK(rm_rtm_entity_split(0, A, 1, 0, 1) == (R | S));
	CHECK(rm_rtm_entity_split(RM_RF_GLOW, A, 0, 0, 1) == R);
	CHECK(rm_rtm_entity_split(RM_RF_MINLIGHT, A, 0, 0, 1) == R);
	CHECK(rm_rtm_entity_split(RM_RF_IR_VISIBLE, A, 0, 0, 1) == R);
	CHECK(rm_rtm_entity_split(0, B, 1, 0, 1) == R);

	/* forward and stock-lit: fullbright, IR under the goggles, the crosshair model (depth hack, not the weapon) */
	CHECK(rm_rtm_entity_split(RM_RF_FULLBRIGHT, A, 0, 0, 1) == F);
	CHECK(rm_rtm_entity_split(RM_RF_FULLBRIGHT, A, 1, 0, 1) == F);
	CHECK(rm_rtm_entity_split(RM_RF_IR_VISIBLE, A, 0, IR, 1) == F);
	CHECK(rm_rtm_entity_split(RM_RF_DEPTHHACK | RM_RF_FULLBRIGHT, A, 0, 0, 1) == F);
	CHECK(rm_rtm_entity_split(RM_RF_DEPTHHACK, A, 0, 0, 1) == F);

	/* the weapon goes forward in this slice; so do translucent models, shells and beams, as now */
	CHECK(rm_rtm_entity_split(gun, A, 0, 0, 1) == F);
	CHECK(rm_rtm_entity_split(RM_RF_WEAPONMODEL, A, 0, 0, 1) == F);
	CHECK(rm_rtm_entity_split(RM_RF_TRANSLUCENT, A, 0, 0, 1) == F);
	CHECK(rm_rtm_entity_split(RM_RF_SHELL_RED, A, 0, 0, 1) == F);
	CHECK(rm_rtm_entity_split(RM_RF_SHELL_HALF_DAM, A, 0, 0, 1) == F);
	CHECK(rm_rtm_entity_split(RM_RF_BEAM, RM_GB_KIND_NONE, 0, 0, 1) == F);
	CHECK(rm_rtm_entity_split(0, RM_GB_KIND_OTHER, 0, 0, 1) == F);

	/* a brush model is the raster's as without the mode */
	CHECK(rm_rtm_entity_split(RM_RF_FULLBRIGHT, B, 0, 0, 1) == rm_gb_entity_split(RM_RF_FULLBRIGHT, B, 0));
	CHECK(rm_rtm_entity_split(RM_RF_DEPTHHACK, B, 0, IR, 1) == rm_gb_entity_split(RM_RF_DEPTHHACK, B, 0));

	/* every combination: the mode's split is the stock split, or forward for a stock-lit or depth-hacked alias model */
	for (flags = 0; flags < (1 << 19); flags++)
	for (kind = 0; kind < KINDS; kind++)
	for (s = 0; s < 2; s++)
	for (rd = 0; rd < 2; rd++)
	{
		const int rdflags = rd ? IR : 0;
		const int got = rm_rtm_entity_split(flags, kind, (float)s, rdflags, 1);
		const int base = rm_gb_entity_split(flags, kind, (float)s);
		const int stock = (kind == A) && (rm_rtm_stock_light(flags, rdflags) || (flags & RM_RF_DEPTHHACK));

		bad += got != (stock ? F : base);
	}

	CHECK(bad == 0);

	/* traced: what the mode rasters, with its normals written */
	CHECK(rm_rtm_traced(0, 0, 1) == 1);
	CHECK(rm_rtm_traced(0, 0, 0) == 0);
	CHECK(rm_rtm_traced(RM_RF_GLOW, 0, 1) == 1);
	CHECK(rm_rtm_traced(RM_RF_GLOW, 0, 0) == 0);
	CHECK(rm_rtm_traced(RM_RF_MINLIGHT, 0, 1) == 1);
	CHECK(rm_rtm_traced(RM_RF_IR_VISIBLE, 0, 1) == 1);
	CHECK(rm_rtm_traced(RM_RF_IR_VISIBLE, IR, 1) == 0);
	CHECK(rm_rtm_traced(RM_RF_FULLBRIGHT, 0, 1) == 0);
	CHECK(rm_rtm_traced(gun, 0, 1) == 0);
	CHECK(rm_rtm_traced(RM_RF_DEPTHHACK, 0, 1) == 0);
	CHECK(rm_rtm_traced(RM_RF_TRANSLUCENT, 0, 1) == 0);
	CHECK(rm_rtm_traced(RM_RF_SHELL_GREEN, 0, 1) == 0);

	bad = 0;

	for (flags = 0; flags < (1 << 19); flags++)
	{
		bad += rm_rtm_traced(flags, 0, 0) != 0;
		bad += rm_rtm_traced(flags, 0, 1) != ((rm_rtm_entity_split(flags, A, 0, 0, 1) & R) != 0);
	}

	CHECK(bad == 0);
}

/* a stored channel: saturated, then rounded to bits of unorm and read back */
static float
unorm(float v, int bits)
{
	const float m = (float)((1 << bits) - 1);

	v = (v > 0.0f) ? ((v < 1.0f) ? v : 1.0f) : 0.0f;
	return (float)floor(v * m + 0.5f) / m;
}

static mtl_rgba_t
stored(mtl_rgba_t c, int bits)
{
	c.r = unorm(c.r, bits);
	c.g = unorm(c.g, bits);
	c.b = unorm(c.b, bits);
	c.a = unorm(c.a, bits);
	return c;
}

static void
test_tag(void)
{
	rm_avert_t v[3];
	unsigned f;
	int bits, i, bad = 0, j;

	for (f = 0; f <= MTL_RTM_FLAGS; f++)
	{
		const mtl_rgba_t c = mtl_rtm_tag(f);

		CHECK(mtl_rtm_is_tag(c) && mtl_rtm_tag_flags(c) == f);
		CHECK(c.a < 1.0f);      /* never the alpha of an opaque model's colour */

		for (bits = 8; bits <= 16; bits += 8)
		{
			const mtl_rgba_t s = stored(c, bits);

			CHECK(mtl_rtm_is_tag(s) && mtl_rtm_tag_flags(s) == f);
		}
	}

	/* a rastered model's own colour (opaque: alpha 1) is never a tag, whatever its rgb, stored or interpolated */
	for (i = 0; i < 256; i++)
	for (j = 0; j < 256; j += 5)
	{
		mtl_rgba_t c;

		c.r = i / 255.0f;
		c.g = j / 255.0f;
		c.b = i / 255.0f;
		c.a = 1.0f;
		bad += mtl_rtm_is_tag(c) || mtl_rtm_tag_flags(c);
		c.g = c.r;
		bad += mtl_rtm_is_tag(c) || mtl_rtm_is_tag(stored(c, 16)) || mtl_rtm_is_tag(stored(c, 8));
		c.r = c.g = c.b = i / 255.0f + 0.0013f;
		bad += mtl_rtm_is_tag(stored(c, 16));
	}

	CHECK(bad == 0);

	/* the grey with an alpha outside the tag's codes, a NaN, a channel past 1 */
	{
		mtl_rgba_t c = mtl_rtm_tag(0);

		c.a = (MTL_RTM_TAG_BASE - 1) / 255.0f;
		CHECK(!mtl_rtm_is_tag(c));
		c.a = (MTL_RTM_TAG_BASE + MTL_RTM_FLAGS + 1) / 255.0f;
		CHECK(!mtl_rtm_is_tag(c));
		c = mtl_rtm_tag(5);
		c.g = NAN;
		CHECK(!mtl_rtm_is_tag(c) && mtl_rtm_tag_flags(c) == 0);
		c = mtl_rtm_tag(5);
		c.r = (MTL_RTM_TAG_GREY + 1) / 255.0f;
		CHECK(!mtl_rtm_is_tag(c));
		c = mtl_rtm_tag(3);
		c.a = 1.5f;
		CHECK(!mtl_rtm_is_tag(c));
	}

	/* the tag writer: the colours alone */
	memset(v, 0, sizeof(v));

	for (i = 0; i < 3; i++)
	{
		v[i].pos[0] = (float)i;
		v[i].uv[1] = 0.5f;
	}

	rm_rtm_tag_verts(v, 2, MTL_RTM_GLOW | MTL_RTM_WEAPON);

	for (i = 0; i < 2; i++)
	{
		mtl_rgba_t c;

		c.r = v[i].color[0];
		c.g = v[i].color[1];
		c.b = v[i].color[2];
		c.a = v[i].color[3];
		CHECK(mtl_rtm_tag_flags(stored(c, 8)) == (MTL_RTM_GLOW | MTL_RTM_WEAPON));
		CHECK(v[i].pos[0] == (float)i && v[i].uv[1] == 0.5f);
	}

	CHECK(v[2].color[3] == 0 && v[2].color[0] == 0);
}

/* gl1's world matrix (R_SetupGL) of a view at org with angles a, on the shim's matrices */
static void
world_matrix(float *m, const float *org, const float *a)
{
	float r[16];

	mtl_mat_identity(m);
	mtl_mat_rotate(r, -90, 1, 0, 0);
	mtl_mat_mul(m, m, r);
	mtl_mat_rotate(r, 90, 0, 0, 1);
	mtl_mat_mul(m, m, r);
	mtl_mat_rotate(r, -a[2], 1, 0, 0);
	mtl_mat_mul(m, m, r);
	mtl_mat_rotate(r, -a[0], 0, 1, 0);
	mtl_mat_mul(m, m, r);
	mtl_mat_rotate(r, -a[1], 0, 0, 1);
	mtl_mat_mul(m, m, r);
	mtl_mat_translate(r, -org[0], -org[1], -org[2]);
	mtl_mat_mul(m, m, r);
}

static void
test_weapon(void)
{
	static const float pts[][3] = {{8, 0, 0}, {14, -5, -6}, {20, 6, 3}, {30, 1, -9}, {11, -3, 4}};
	const float org[3] = {100, -40, 24}, ang[3] = {0, 0, 0}, ang2[3] = {15, 120, 4};
	double zn, zf;
	int h, g, j, a, p;

	rm_alias_depth(RM_RF_DEPTHHACK, &zn, &zf);
	CHECK(MTL_RTM_WEAPON_ZFAR == (float)zf && zn == 0);
	CHECK(mtl_rtm_weapon_ndc(0) == -1.0f && mtl_rtm_weapon_ndc(MTL_RTM_WEAPON_ZFAR) == 1.0f);
	CHECK(mtl_rtm_weapon_depth(0.15f) == 0.15f / 0.3f);

	for (h = 0; h < 2; h++)
	for (g = 0; g < 2; g++)
	for (j = 0; j < 2; j++)
	for (a = 0; a < 2; a++)
	{
		rm_rtm_weapon_t w;
		rm_viewport_t vp;
		rm_rts_view_t v;
		float world[16], proj[16], pj[16], mvp[16];
		const float *an = a ? ang2 : ang;
		int ok;

		memset(&w, 0, sizeof(w));
		w.fovy = 73.74f;
		w.gunfov = g ? 90 : -1;
		w.hand = h ? 1 : 0;
		w.width = 1280;
		w.height = 720;
		w.znear = 4;
		w.jx = j ? 0.37f : 0;
		w.jy = j ? -0.21f : 0;
		vp.x = 16;
		vp.y = 8;
		vp.w = 1280;
		vp.h = 720;
		vp.znear = 0;
		vp.zfar = 1;

		world_matrix(world, org, an);
		ok = rm_rtm_weapon_view(&w, world, &vp, org, &v);
		CHECK(ok);

		/* the weapon's own projection, jittered, as R_DrawAliasModel and RM_GB_JitterProj draw it */
		CHECK(rm_weapon_projection(proj, w.fovy, w.gunfov, w.hand, w.width, w.height, w.znear, 0, 0, 0, 0));
		rm_gb_jitter(proj, w.jx, w.jy, vp.w, vp.h, pj);
		mtl_mat_mul(mvp, pj, world);

		for (p = 0; p < (int)(sizeof(pts) / sizeof(pts[0])); p++)
		{
			float wp[3], clip[4], back[3];
			double px, py, d;
			int i;

			/* a point in front of the eye, in the view's own axes: the rows of the world matrix's rotation */
			for (i = 0; i < 3; i++)
			{
				const float f = -world[i * 4 + 2], r = world[i * 4 + 0], u = world[i * 4 + 1];

				wp[i] = org[i] + f * pts[p][0] + r * pts[p][1] + u * pts[p][2];
			}

			for (i = 0; i < 4; i++)
			{
				clip[i] = mvp[i] * wp[0] + mvp[4 + i] * wp[1] + mvp[8 + i] * wp[2] + mvp[12 + i];
			}

			CHECK(clip[3] > 0);

			/* the pixel and the stored depth: GL's viewport mapping, the depth range 0 .. 0.3 */
			px = vp.x + (clip[0] / clip[3] + 1) * 0.5 * vp.w - 0.5;
			py = vp.y + (1 - clip[1] / clip[3]) * 0.5 * vp.h - 0.5;
			d = zn + (zf - zn) * (clip[2] / clip[3] + 1) * 0.5;

			CHECK(rm_rtm_weapon_unproject(&v, (float)px, (float)py, (float)d, back));
			CHECK(fabs(back[0] - wp[0]) < 1e-3 && fabs(back[1] - wp[1]) < 1e-3 && fabs(back[2] - wp[2]) < 1e-3);

			if (!(fabs(back[0] - wp[0]) < 1e-3 && fabs(back[1] - wp[1]) < 1e-3 && fabs(back[2] - wp[2]) < 1e-3))
			{
				printf("  hand %g gunfov %g jitter %d angles %d point %d: %g %g %g for %g %g %g\n", w.hand, w.gunfov,
						j, a, p, back[0], back[1], back[2], wp[0], wp[1], wp[2]);
			}
		}
	}

	/* a projection glFrustum refuses: no view */
	{
		rm_rtm_weapon_t w;
		rm_viewport_t vp = {0, 0, 640, 480, 0, 1};
		rm_rts_view_t v;
		float world[16];

		memset(&w, 0, sizeof(w));
		w.fovy = 90;
		w.gunfov = -1;
		w.width = 640;
		w.height = 0;
		w.znear = 4;
		mtl_mat_identity(world);
		CHECK(!rm_rtm_weapon_view(&w, world, &vp, org, &v));
	}
}

static void
test_pullback(void)
{
	const mtl_rtm_v3_t eye = mtl_rtm_v3(1, 2, 3), p = mtl_rtm_v3(11, 2, 3);
	mtl_rtm_v3_t q;

	/* no face before the point: the point */
	q = mtl_rtm_pullback(eye, p, INFINITY);
	CHECK(q.x == p.x && q.y == p.y && q.z == p.z);
	q = mtl_rtm_pullback(eye, p, NAN);
	CHECK(q.x == p.x);
	q = mtl_rtm_pullback(eye, p, 10);
	CHECK(q.x == p.x);
	q = mtl_rtm_pullback(eye, p, 12);
	CHECK(q.x == p.x);
	q = mtl_rtm_pullback(eye, p, -1);
	CHECK(q.x == p.x);

	/* a face at 4 units: MTL_RTM_PULLBACK before it, on the ray */
	q = mtl_rtm_pullback(eye, p, 4);
	CHECK(fabs(q.x - (1 + 4 - MTL_RTM_PULLBACK)) < 1e-6 && q.y == 2 && q.z == 3);
	CHECK(MTL_RTM_PULLBACK > 0 && MTL_RTM_PULLBACK < 1);

	/* a face nearer than the epsilon: the eye */
	q = mtl_rtm_pullback(eye, p, 0.1f);
	CHECK(q.x == eye.x && q.y == eye.y && q.z == eye.z);

	/* off an axis: the distance to the face is the epsilon */
	q = mtl_rtm_pullback(mtl_rtm_v3(0, 0, 0), mtl_rtm_v3(6, 8, 0), 5);
	CHECK(fabs(sqrt(q.x * q.x + q.y * q.y) - (5 - MTL_RTM_PULLBACK)) < 1e-5 && fabs(q.x * 8 - q.y * 6) < 1e-5);
}

int
main(void)
{
	rm_minlight_table(16, minlight16);
	gamma_table(1, gamma1);
	gamma_table(1.2f, gamma12);

	test_color_parity();
	test_color_rule();
	test_split_off();
	test_split_mode();
	test_tag();
	test_weapon();
	test_pullback();

	printf("rm_rtmodel_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
