/*
 * Host test of the pure part of ref_metal's G-buffer (RT2, TASK54):
 * src/client/refresh/metal/rm_gbuf.c and common/mtl_gbuf_math.h (the
 * arithmetic rm_gbuf.metal shares), with rm_pure.c for the alias stream
 * and the entity matrix. No SDL, no Metal, no game data. Run by
 * tests/check-ref-metal.sh; prints "rm_gbuf_test: N checks, M failed".
 */

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../../src/client/refresh/metal/header/rm_gbuf.h"
#include "../../src/client/refresh/metal/header/rm_rt.h"
#include "../../src/client/refresh/metal/common/mtl_matrix.h"
#include "../../src/client/refresh/metal/common/mtl_shader_math.h"
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
near(float a, float b, float eps)
{
	return fabsf(a - b) <= eps;
}

/* forward unless asked: the G-buffer for an effect or r_rt_gbuffer, motion for r_rt_motion, never without a world */
static void
test_path(void)
{
	CHECK(rm_gb_path(0, 0, 0, 0) == RM_GB_PATH_FORWARD);
	CHECK(rm_gb_path(0, 1, 0, 0) == RM_GB_PATH_GBUFFER);
	CHECK(rm_gb_path(1, 0, 0, 0) == RM_GB_PATH_GBUFFER);
	CHECK(rm_gb_path(0, 0, 1, 0) == RM_GB_PATH_MOTION);
	CHECK(rm_gb_path(0, 1, 1, 0) == RM_GB_PATH_GBUFFER);
	CHECK(rm_gb_path(0, -1, 0, 0) == RM_GB_PATH_GBUFFER);
	CHECK(rm_gb_path(0, 0.5f, 0, 0) == RM_GB_PATH_GBUFFER);
	CHECK(rm_gb_path(1, 1, 1, 1) == RM_GB_PATH_FORWARD);
	CHECK(rm_gb_path(0, 0, 1, 1) == RM_GB_PATH_FORWARD);
	CHECK(rm_gb_path(0, NAN, 0, 0) == RM_GB_PATH_FORWARD);
	CHECK(rm_gb_path(0, 0, NAN, 0) == RM_GB_PATH_FORWARD);
	CHECK(rm_gb_path(0, INFINITY, 0, 0) == RM_GB_PATH_FORWARD);
}

static void
test_render_size(void)
{
	int w = -1, h = -1;

	CHECK(rm_gb_render_size(640, 480, 1, &w, &h) && w == 640 && h == 480);
	CHECK(rm_gb_render_size(640, 480, 0, &w, &h) && w == 640 && h == 480);
	CHECK(rm_gb_render_size(640, 480, 0.5f, &w, &h) && w == 320 && h == 240);
	CHECK(rm_gb_render_size(5120, 2880, 0.5f, &w, &h) && w == 2560 && h == 1440);
	CHECK(rm_gb_render_size(641, 481, 0.5f, &w, &h) && w == 321 && h == 241);   /* 320.5 rounds up */
	CHECK(rm_gb_render_size(1920, 1080, 0.75f, &w, &h) && w == 1440 && h == 810);
	CHECK(rm_gb_render_size(640, 480, 2, &w, &h) && w == 640 && h == 480);
	CHECK(rm_gb_render_size(640, 480, -0.5f, &w, &h) && w == 640 && h == 480);
	CHECK(rm_gb_render_size(640, 480, NAN, &w, &h) && w == 640 && h == 480);
	CHECK(rm_gb_render_size(640, 480, 1e-9f, &w, &h) && w == 1 && h == 1);
	CHECK(!rm_gb_render_size(0, 480, 1, &w, &h) && w == 0 && h == 0);
	CHECK(!rm_gb_render_size(640, -1, 1, &w, &h) && w == 0 && h == 0);
}

static void
test_bytes(void)
{
	rm_gb_bytes_t b;
	const size_t px = (size_t)5120 * 2880;

	CHECK(rm_gb_bytes(RM_GB_PATH_FORWARD, 640, 480, 0, &b) && b.total == 0);
	CHECK(rm_gb_bytes(RM_GB_PATH_MOTION, 640, 480, 0, &b) && b.depth == 640 * 480 * 4 &&
		b.stencil == 640 * 480 && b.motion == 640 * 480 * 4 && b.normal == 0 && b.albedo == 0 &&
		b.color == 0 && b.total == 640 * 480 * 9);
	CHECK(rm_gb_bytes(RM_GB_PATH_GBUFFER, 5120, 2880, 0, &b) && b.depth == px * 4 && b.stencil == px &&
		b.normal == px * 4 && b.material == px && b.motion == px * 4 && b.lightmap == px * 4 &&
		b.albedo == px * 4 && b.color == 0 && b.total == px * 22);
	/* about 300 MB at 5120 x 2880, as the card says */
	CHECK(b.total > 300u * 1000 * 1000 && b.total < 330u * 1000 * 1000);
	CHECK(rm_gb_bytes(RM_GB_PATH_GBUFFER, 320, 240, 1, &b) && b.color == 320 * 240 * 4 &&
		b.total == 320 * 240 * 26);
	CHECK(rm_gb_bytes(RM_GB_PATH_FORWARD, 320, 240, 1, &b) && b.total == 0);
	CHECK(!rm_gb_bytes(RM_GB_PATH_GBUFFER, 0, 240, 0, &b) && b.total == 0 && b.depth == 0);
	CHECK(!rm_gb_bytes(RM_GB_PATH_GBUFFER, 16385, 240, 0, &b) && b.total == 0);
	CHECK(!rm_gb_bytes(7, 640, 480, 0, &b) && b.total == 0);
}

static void
test_render_rect(void)
{
	rm_viewport_t vp = { 64, 48, 512, 384, 0, 1 }, o;

	rm_gb_render_rect(&vp, 640, 480, 640, 480, &o);
	CHECK(memcmp(&vp, &o, sizeof(o)) == 0);

	rm_gb_render_rect(&vp, 640, 480, 320, 240, &o);
	CHECK(o.x == 32 && o.y == 24 && o.w == 256 && o.h == 192 && o.znear == 0 && o.zfar == 1);

	/* edges rounded down, so neighbouring rectangles share theirs */
	vp.x = 1;
	vp.y = 3;
	vp.w = 637;
	vp.h = 475;
	vp.znear = 0.25;
	rm_gb_render_rect(&vp, 640, 480, 320, 240, &o);
	CHECK(o.x == 0 && o.y == 1 && o.w == 319 && o.h == 238 && o.znear == 0.25 && o.zfar == 1);
}

/* the raster/forward split of R_DrawEntitiesSplit: which pass draws an entity, and its shadow */
static void
test_entity_split(void)
{
	static const int shells[] = { RM_RF_SHELL_RED, RM_RF_SHELL_GREEN, RM_RF_SHELL_BLUE, RM_RF_SHELL_DOUBLE,
		RM_RF_SHELL_HALF_DAM };
	const int R = RM_GB_ENT_RASTER, F = RM_GB_ENT_FORWARD, S = RM_GB_ENT_SHADOW;
	int i;

	/* the opaque alias and brush models: the raster's; an alias model's shadow the forward pass's, with r_shadows */
	CHECK(rm_gb_entity_split(0, RM_GB_KIND_ALIAS, 0) == R);
	CHECK(rm_gb_entity_split(0, RM_GB_KIND_BRUSH, 0) == R);
	CHECK(rm_gb_entity_split(0, RM_GB_KIND_ALIAS, 1) == (R | S));
	CHECK(rm_gb_entity_split(0, RM_GB_KIND_BRUSH, 1) == R);
	CHECK(rm_gb_entity_split(RM_RF_NOSHADOW, RM_GB_KIND_ALIAS, 1) == R);
	/* flags that change how a model is lit, not where it is drawn */
	CHECK(rm_gb_entity_split(RM_RF_MINLIGHT | RM_RF_FULLBRIGHT | RM_RF_GLOW | RM_RF_IR_VISIBLE | RM_RF_DEPTHHACK,
			RM_GB_KIND_ALIAS, 0) == R);
	CHECK(rm_gb_entity_split(RM_RF_FULLBRIGHT, RM_GB_KIND_BRUSH, 1) == R);

	/* the view weapon, a beam, a translucent entity and every shell: the forward pass's whole */
	CHECK(rm_gb_entity_split(RM_RF_WEAPONMODEL, RM_GB_KIND_ALIAS, 1) == F);
	CHECK(rm_gb_entity_split(RM_RF_WEAPONMODEL, RM_GB_KIND_BRUSH, 0) == F);
	CHECK(rm_gb_entity_split(RM_RF_BEAM, RM_GB_KIND_NONE, 1) == F);
	CHECK(rm_gb_entity_split(RM_RF_BEAM, RM_GB_KIND_ALIAS, 1) == F);
	CHECK(rm_gb_entity_split(RM_RF_BEAM, RM_GB_KIND_BRUSH, 0) == F);
	CHECK(rm_gb_entity_split(RM_RF_TRANSLUCENT, RM_GB_KIND_ALIAS, 1) == F);
	CHECK(rm_gb_entity_split(RM_RF_TRANSLUCENT, RM_GB_KIND_BRUSH, 0) == F);

	for (i = 0; i < (int)(sizeof(shells) / sizeof(shells[0])); i++)
	{
		CHECK(rm_gb_entity_split(shells[i], RM_GB_KIND_ALIAS, 1) == F);
		CHECK(rm_gb_entity_split(shells[i], RM_GB_KIND_BRUSH, 0) == F);
	}

	/* no model (the null model), a sprite, a kind not known: the forward pass's */
	CHECK(rm_gb_entity_split(0, RM_GB_KIND_NONE, 1) == F);
	CHECK(rm_gb_entity_split(0, RM_GB_KIND_OTHER, 1) == F);
	CHECK(rm_gb_entity_split(0, 7, 0) == F);
	CHECK(rm_gb_entity_split(0, -1, 0) == F);

	/* r_shadows as gl1 reads it: any value but 0, NaN included */
	CHECK(rm_gb_entity_split(0, RM_GB_KIND_ALIAS, -1) == (R | S));
	CHECK(rm_gb_entity_split(0, RM_GB_KIND_ALIAS, NAN) == (R | S));
}

/*
 * A view drawn below the frame's size: its pixels per frame pixel (the heights', as gl1 scales a
 * particle by the height), and the point sizes in frame pixels scaled into it
 */
static void
test_pixel_scale(void)
{
	rm_viewport_t f = { 0, 0, 640, 480, 0, 1 }, r;
	rm_points_t s, k;

	rm_gb_render_rect(&f, 640, 480, 320, 240, &r);
	CHECK(rm_gb_pixel_scale(&f, &r) == 0.5f);
	CHECK(rm_gb_pixel_scale(&f, &f) == 1.0f);
	rm_gb_render_rect(&f, 640, 480, 480, 360, &r);
	CHECK(rm_gb_pixel_scale(&f, &r) == 0.75f);
	/* the heights', not the widths': 641 x 481 at 0.5 is 321 x 241 */
	f.w = 641;
	f.h = 481;
	rm_gb_render_rect(&f, 641, 481, 321, 241, &r);
	CHECK(near(rm_gb_pixel_scale(&f, &r), 241.0f / 481.0f, 1e-6f));
	/* a sub-rectangle of the frame (viewsize) takes its own heights */
	f.x = 64;
	f.y = 48;
	f.w = 512;
	f.h = 384;
	rm_gb_render_rect(&f, 640, 480, 320, 240, &r);
	CHECK(rm_gb_pixel_scale(&f, &r) == 0.5f);
	/* nothing to scale by: 1 */
	r.h = 0;
	CHECK(rm_gb_pixel_scale(&f, &r) == 1.0f);
	r.h = NAN;
	CHECK(rm_gb_pixel_scale(&f, &r) == 1.0f);
	r.h = 192;
	f.h = 0;
	CHECK(rm_gb_pixel_scale(&f, &r) == 1.0f);
	f.h = INFINITY;
	CHECK(rm_gb_pixel_scale(&f, &r) == 1.0f);
	f.h = 384;
	r.h = 768;   /* never above the frame's: rm_gb_render_size */
	CHECK(rm_gb_pixel_scale(&f, &r) == 1.0f);
	CHECK(rm_gb_pixel_scale(NULL, &r) == 1.0f && rm_gb_pixel_scale(&f, NULL) == 1.0f);

	/* gl1's sizes: 4.5 pixels, at least 2, at most 40; at half the size they are half */
	rm_point_state_init(&s);
	rm_point_set_size(&s, 4.5f);
	rm_point_set_range(&s, 2, 40);
	s.att[1] = 0.25f;
	s.smooth = 1;
	k = s;
	rm_gb_point_scale(&k, 0.5f);
	CHECK(k.size == 2.25f && k.min == 1.0f && k.max == 20.0f);
	CHECK(k.att[0] == s.att[0] && k.att[1] == s.att[1] && k.att[2] == s.att[2] && k.smooth == s.smooth);
	/* 1, or no scale: unchanged, byte for byte */
	k = s;
	rm_gb_point_scale(&k, 1);
	CHECK(memcmp(&k, &s, sizeof(k)) == 0);
	rm_gb_point_scale(&k, 0);
	rm_gb_point_scale(&k, -0.5f);
	rm_gb_point_scale(&k, NAN);
	rm_gb_point_scale(&k, INFINITY);
	rm_gb_point_scale(&k, 2);   /* never above the frame's */
	CHECK(memcmp(&k, &s, sizeof(k)) == 0);
	rm_gb_point_scale(NULL, 0.5f);
}

/* r_rt_gbuffer_show: 0 to 6, clamped before the conversion, 0 for a value that is not finite */
static void
test_show(void)
{
	CHECK(rm_gb_show(0) == 0);
	CHECK(rm_gb_show(1) == 1);
	CHECK(rm_gb_show(4.75f) == 4);
	CHECK(rm_gb_show(6) == 6);
	CHECK(rm_gb_show(7) == RM_GB_SHOW_MAX);
	CHECK(rm_gb_show(1e10f) == RM_GB_SHOW_MAX);
	CHECK(rm_gb_show(FLT_MAX) == RM_GB_SHOW_MAX);
	CHECK(rm_gb_show(-1) == 0);
	CHECK(rm_gb_show(-1e10f) == 0);
	CHECK(rm_gb_show(0.5f) == 0);
	CHECK(rm_gb_show(NAN) == 0);
	CHECK(rm_gb_show(INFINITY) == 0);
	CHECK(rm_gb_show(-INFINITY) == 0);
}

/* r_rt_flatlight (TASK173): 0 off, else the level 0 to 1 every lightmap sample is replaced by */
static void
test_flatlight(void)
{
	CHECK(rm_gb_flatlight(0) == 0.0f);
	CHECK(rm_gb_flatlight(0.4f) == 0.4f);
	CHECK(rm_gb_flatlight(1) == 1.0f);
	CHECK(rm_gb_flatlight(1.5f) == 1.0f);
	CHECK(rm_gb_flatlight(FLT_MAX) == 1.0f);
	CHECK(rm_gb_flatlight(-0.5f) == 0.0f);
	CHECK(rm_gb_flatlight(-FLT_MAX) == 0.0f);
	CHECK(rm_gb_flatlight(NAN) == 0.0f);
	CHECK(rm_gb_flatlight(INFINITY) == 0.0f);
	CHECK(rm_gb_flatlight(-INFINITY) == 0.0f);
}

static void
project(const float *m, const float *p, float *ndc)
{
	float c[4];
	int i;

	for (i = 0; i < 4; i++)
	{
		c[i] = m[i] * p[0] + m[4 + i] * p[1] + m[8 + i] * p[2] + m[12 + i] * p[3];
	}

	ndc[0] = c[0] / c[3];
	ndc[1] = c[1] / c[3];
	ndc[2] = c[2] / c[3];
	ndc[3] = c[3];
}

static void
test_jitter(void)
{
	float proj[16], j[16], a[4], b[4];
	const float p[4] = { 30, -20, -200, 1 };

	CHECK(rm_projection(proj, 73.74f, 640, 480, 4, 0, 0, 0, 0));

	rm_gb_jitter(proj, 0, 0, 640, 480, j);
	CHECK(memcmp(proj, j, sizeof(j)) == 0);

	/* half a pixel right and a quarter down: the point moves so far on screen, its depth and w stay */
	rm_gb_jitter(proj, 0.5f, 0.25f, 640, 480, j);
	project(proj, p, a);
	project(j, p, b);
	CHECK(near((b[0] - a[0]) * 320.0f, 0.5f, 1e-4f));
	CHECK(near((a[1] - b[1]) * 240.0f, 0.25f, 1e-4f));
	CHECK(b[2] == a[2] && b[3] == a[3]);
	CHECK(memcmp(proj, j, sizeof(j)) != 0);
}

static void
test_motion(void)
{
	mtl_mv_t m;

	/* the same clip point: no motion */
	m = mtl_gb_motion(0.25f, -0.5f, 2, 0.25f, -0.5f, 2, 320, 240);
	CHECK(m.x == 0 && m.y == 0);

	/* the point was 0.1 of the viewport's half-width to the right and 0.1 of its half-height higher (+y up in NDC) */
	m = mtl_gb_motion(0, 0, 1, 0.1f, 0.1f, 1, 320, 240);
	CHECK(near(m.x, 32, 1e-4f) && near(m.y, -24, 1e-4f));

	/* the divide by w is each frame's own */
	m = mtl_gb_motion(1, 1, 2, 1, 1, 4, 100, 100);
	CHECK(near(m.x, -25, 1e-4f) && near(m.y, 25, 1e-4f));

	/* behind either camera: none */
	m = mtl_gb_motion(0, 0, 1, 0.5f, 0, -1, 320, 240);
	CHECK(m.x == 0 && m.y == 0);
	m = mtl_gb_motion(0, 0, 0, 0.5f, 0, 1, 320, 240);
	CHECK(m.x == 0 && m.y == 0);
}

/*
 * A known camera move through the matrices the renderer uses: the view
 * turned 1 degree of yaw to the left, a point straight ahead moves right
 * on the screen in this frame, so its motion (previous minus current)
 * points left by the projection's pixels for one degree.
 */
static void
test_motion_camera(void)
{
	float proj[16], v0[16], v1[16], m0[16], m1[16], c[4], p[4];
	const float org[3] = { 0, 0, 0 }, a0[3] = { 0, 0, 0 }, a1[3] = { 0, 1, 0 };
	const float pt[4] = { 100, 0, 0, 1 };
	mtl_mv_t m;
	int i;
	float expect;

	CHECK(rm_projection(proj, 73.74f, 640, 480, 4, 0, 0, 0, 0));
	rm_view_matrix(v0, a0, org);   /* the previous view: looking along +x */
	rm_view_matrix(v1, a1, org);   /* this view: turned 1 degree to the left (yaw +1) */
	mtl_mat_mul(m0, proj, v0);
	mtl_mat_mul(m1, proj, v1);

	for (i = 0; i < 4; i++)
	{
		c[i] = m1[i] * pt[0] + m1[4 + i] * pt[1] + m1[8 + i] * pt[2] + m1[12 + i] * pt[3];
		p[i] = m0[i] * pt[0] + m0[4 + i] * pt[1] + m0[8 + i] * pt[2] + m0[12 + i] * pt[3];
	}

	m = mtl_gb_motion(c[0], c[1], c[3], p[0], p[1], p[3], 320, 240);

	/* x_ndc = tan(1 degree) * proj[0] after the turn; the motion is minus that in pixels */
	expect = -(float)tan(1.0 * M_PI / 180.0) * proj[0] * 320.0f;
	CHECK(near(m.x, expect, 1e-3f));
	CHECK(near(m.y, 0, 1e-4f));
	CHECK(m.x < -5.0f && m.x > -7.0f);
}

static void
test_normal(void)
{
	mtl_rgba_t c = mtl_gb_normal(0, 0, 1);

	CHECK(c.r == 0.5f && c.g == 0.5f && c.b == 1.0f && c.a == 1.0f);
	c = mtl_gb_normal(-1, 1, 0);
	CHECK(c.r == 0.0f && c.g == 1.0f && c.b == 0.5f && c.a == 1.0f);
	c = mtl_gb_normal(0, 0, 0);
	CHECK(c.a == 0.0f);
	c = mtl_gb_normal(2, -2, 0);
	CHECK(c.r == 1.0f && c.g == 0.0f);
}

/* on values an rgba8 attachment holds exactly, the resolve is the stock fragment's product to the bit */
static void
test_resolve(void)
{
	int i, j, k, same = 1, mod = 1;
	const int envs[3] = { MTL_LM_MODULATE, MTL_LM_REPLACE, MTL_LM_COMBINE };
	const float scales[3] = { 1, 1, 2 };

	for (i = 0; i < 256; i += 5)
	{
		for (j = 0; j < 256; j += 3)
		{
			mtl_rgba_t a, l, r, s;

			a.r = i / 255.0f;
			a.g = (255 - i) / 255.0f;
			a.b = j / 255.0f;
			a.a = 1.0f;
			l.r = j / 255.0f;
			l.g = i / 255.0f;
			l.b = (255 - j) / 255.0f;
			l.a = 1.0f;

			for (k = 0; k < 3; k++)
			{
				r = mtl_gb_resolve(a, l, MTL_GB_MAT_WORLD, envs[k], scales[k]);
				s = mtl_world_lm(a, l, envs[k], scales[k]);
				same &= (memcmp(&r, &s, sizeof(r)) == 0);
			}

			r = mtl_gb_resolve(a, l, MTL_GB_MAT_ALIAS, MTL_LM_COMBINE, 2);
			s = mtl_world_color(l, a);
			mod &= (memcmp(&r, &s, sizeof(r)) == 0);
			r = mtl_gb_resolve(a, l, MTL_GB_MAT_WATER, MTL_LM_COMBINE, 2);
			mod &= (memcmp(&r, &s, sizeof(r)) == 0);
			r = mtl_gb_resolve(a, l, MTL_GB_MAT_SKY, MTL_LM_COMBINE, 2);
			mod &= (memcmp(&r, &s, sizeof(r)) == 0);
		}
	}

	CHECK(same);
	CHECK(mod);

	{
		mtl_rgba_t a = { 0.25f, 0.5f, 0.75f, 1 }, l = { 0.5f, 0.5f, 0.5f, 1 }, r;

		/* combine at scale 2 doubles the product, clamped */
		r = mtl_gb_resolve(a, l, MTL_GB_MAT_WORLD, MTL_LM_COMBINE, 2);
		CHECK(r.r == 0.25f && r.g == 0.5f && r.b == 0.75f);
		r = mtl_gb_resolve(a, l, MTL_GB_MAT_ALIAS, MTL_LM_COMBINE, 2);
		CHECK(r.r == 0.125f && r.g == 0.25f && r.b == 0.375f);
		r = mtl_gb_resolve(a, l, MTL_GB_MAT_NONE, MTL_LM_MODULATE, 1);
		CHECK(r.r == 0.25f && r.g == 0.5f && r.b == 0.75f);
		r = mtl_gb_resolve(a, l, MTL_GB_MAT_TRANS, MTL_LM_MODULATE, 1);
		CHECK(r.r == 0.25f);
	}

	/* the ids are rm_rt.h's */
	CHECK(MTL_GB_MAT_NONE == RM_RT_MAT_NONE && MTL_GB_MAT_WORLD == RM_RT_MAT_WORLD &&
		MTL_GB_MAT_ALIAS == RM_RT_MAT_ALIAS && MTL_GB_MAT_WATER == RM_RT_MAT_WATER &&
		MTL_GB_MAT_SKY == RM_RT_MAT_SKY && MTL_GB_MAT_TRANS == RM_RT_MAT_ALPHA);
}

static rm_gb_ent_t
ent(const void *model, float x, float y, int flags)
{
	rm_gb_ent_t e;

	memset(&e, 0, sizeof(e));
	e.model = model;
	e.origin[0] = x;
	e.origin[1] = y;
	e.flags = flags;
	return e;
}

static void
test_match(void)
{
	static const int ma = 1, mb = 2;
	rm_gb_ent_t prev[4], cur[5];
	int match[5];

	prev[0] = ent(&ma, 0, 0, 0);
	prev[1] = ent(&ma, 100, 0, 0);
	prev[2] = ent(&mb, 0, 0, 0);
	prev[3] = ent(&ma, 5, 5, 4);   /* the view weapon */

	/* reordered this frame, each moved a little; a new model; a teleport far off */
	cur[0] = ent(&ma, 98, 2, 0);
	cur[1] = ent(&ma, 3, 1, 0);
	cur[2] = ent(&mb, 1, 0, 0);
	cur[3] = ent(&mb, 2000, 0, 0);
	cur[4] = ent(&ma, 6, 5, 4);

	rm_gb_match(prev, 4, cur, 5, 64, 4, match);
	CHECK(match[0] == 1);
	CHECK(match[1] == 0);
	CHECK(match[2] == 2);
	CHECK(match[3] == -1);   /* mb's only one is taken, and 2000 is beyond 64 */
	CHECK(match[4] == 3);    /* the weapon only matches the weapon */

	/* each previous entity matches once: the second of two at the same place gets none */
	cur[0] = ent(&mb, 0, 0, 0);
	cur[1] = ent(&mb, 0, 0, 0);
	rm_gb_match(prev, 4, cur, 2, 64, 4, match);
	CHECK(match[0] == 2 && match[1] == -1);

	/* the nearest wins, not the first */
	prev[0] = ent(&ma, 50, 0, 0);
	prev[1] = ent(&ma, 10, 0, 0);
	cur[0] = ent(&ma, 0, 0, 0);
	rm_gb_match(prev, 2, cur, 1, 64, 4, match);
	CHECK(match[0] == 1);

	/* the bound is inclusive; just past it, none */
	prev[0] = ent(&ma, 64, 0, 0);
	rm_gb_match(prev, 1, cur, 1, 64, 4, match);
	CHECK(match[0] == 0);
	prev[0] = ent(&ma, 64.01f, 0, 0);
	rm_gb_match(prev, 1, cur, 1, 64, 4, match);
	CHECK(match[0] == -1);

	/* a weapon among non-weapons of its model: not matched to them */
	prev[0] = ent(&ma, 0, 0, 0);
	cur[0] = ent(&ma, 0, 0, 4);
	rm_gb_match(prev, 1, cur, 1, 64, 4, match);
	CHECK(match[0] == -1);

	/* a position that is not finite matches nothing */
	cur[0] = ent(&ma, NAN, 0, 0);
	rm_gb_match(prev, 1, cur, 1, 64, 4, match);
	CHECK(match[0] == -1);
	cur[0] = ent(&ma, 0, 0, 0);
	prev[0] = ent(&ma, INFINITY, 0, 0);
	rm_gb_match(prev, 1, cur, 1, 64, 4, match);
	CHECK(match[0] == -1);

	/* none of a count outside the arrays */
	match[0] = 7;
	rm_gb_match(prev, 1, cur, -1, 64, 4, match);
	rm_gb_match(prev, -1, cur, 1, 64, 4, match);
	CHECK(match[0] == -1);
}

static void
test_hist(void)
{
	static const int ma = 1;
	static rm_gb_hist_t h;

	rm_gb_hist_reset(&h);
	CHECK(!h.prev.valid && !h.cur.valid);

	/* a first frame has no previous view: nothing matches */
	h.cur.valid = 1;
	h.cur.nents = 1;
	h.cur.ents[0] = ent(&ma, 0, 0, 0);
	rm_gb_hist_match(&h, 64, 4);
	CHECK(h.match[0] == -1 && rm_gb_hist_prev(&h, 0) == NULL);
	rm_gb_hist_end(&h);
	CHECK(h.prev.valid && h.prev.nents == 1 && !h.cur.valid && h.cur.nents == 0);

	/* the next one does */
	h.cur.valid = 1;
	h.cur.nents = 1;
	h.cur.ents[0] = ent(&ma, 4, 0, 0);
	rm_gb_hist_match(&h, 64, 4);
	CHECK(h.match[0] == 0 && rm_gb_hist_prev(&h, 0) == &h.prev.ents[0]);
	CHECK(rm_gb_hist_prev(&h, -1) == NULL && rm_gb_hist_prev(&h, 1) == NULL);
	CHECK(rm_gb_hist_prev(&h, RM_GB_MAX_ENTS) == NULL);

	/* a reset forgets it */
	rm_gb_hist_reset(&h);
	h.cur.valid = 1;
	h.cur.nents = 1;
	h.cur.ents[0] = ent(&ma, 4, 0, 0);
	rm_gb_hist_match(&h, 64, 4);
	CHECK(h.match[0] == -1);

	/* entities kept from a view that is not valid match nothing */
	h.prev.nents = 1;
	h.prev.ents[0] = ent(&ma, 4, 0, 0);
	h.prev.valid = 0;
	rm_gb_hist_match(&h, 64, 4);
	CHECK(h.match[0] == -1);

	/* a count past the arrays is bounded */
	h.cur.nents = RM_GB_MAX_ENTS + 5;
	h.prev.valid = 1;
	h.prev.nents = RM_GB_MAX_ENTS + 5;
	rm_gb_hist_match(&h, 64, 4);
	CHECK(h.match[RM_GB_MAX_ENTS - 1] >= -1);
}

/* the map walks the stream as rm_alias_emit does: each vertex's position is the lerped one of its index */
static void
test_alias_map(void)
{
	/* a strip of 4 (indices 3, 1, 0, 2) and a fan of 3 (2, 1, 3), then the end */
	int order[1 + 12 + 1 + 9 + 1];
	float lerped[4][4];
	unsigned char verts[4 * 4];
	rm_avert_t out[16];
	unsigned short idx[64], map[16];
	const float shadelight[3] = { 1, 1, 1 };
	unsigned char minlight[256], gamma[256];
	long n, ni, k;
	int i, ok = 1, pos = 0;
	const int strip[4] = { 3, 1, 0, 2 }, fan[3] = { 2, 1, 3 };

	for (i = 0; i < 256; i++)
	{
		minlight[i] = gamma[i] = (unsigned char)i;
	}

	for (i = 0; i < 4; i++)
	{
		lerped[i][0] = 10.0f * i;
		lerped[i][1] = 1.0f + i;
		lerped[i][2] = -2.0f * i;
		lerped[i][3] = 1;
		verts[4 * i] = verts[4 * i + 1] = verts[4 * i + 2] = 0;
		verts[4 * i + 3] = (unsigned char)i;
	}

	order[pos++] = 4;

	for (i = 0; i < 4; i++)
	{
		float st = 0.1f * i;

		memcpy(&order[pos], &st, sizeof(st));
		memcpy(&order[pos + 1], &st, sizeof(st));
		order[pos + 2] = strip[i];
		pos += 3;
	}

	order[pos++] = -3;

	for (i = 0; i < 3; i++)
	{
		float st = 0.2f * i;

		memcpy(&order[pos], &st, sizeof(st));
		memcpy(&order[pos + 1], &st, sizeof(st));
		order[pos + 2] = fan[i];
		pos += 3;
	}

	order[pos++] = 0;

	n = rm_alias_emit(order, pos, (const float (*)[4])lerped, verts, 4, 0, shadelight, 0, minlight, gamma, 255,
			out, 16, idx, 64, &ni);
	CHECK(n == 7);
	CHECK(rm_gb_alias_map(order, pos, 4, map, 16) == n);

	for (k = 0; k < n; k++)
	{
		ok &= (map[k] < 4) && (memcmp(out[k].pos, lerped[map[k]], 3 * sizeof(float)) == 0);
	}

	CHECK(ok);
	CHECK(map[0] == 3 && map[3] == 2 && map[4] == 2 && map[6] == 3);

	/* refused as rm_alias_emit refuses: an index outside, no room, a short stream, a count under 3 */
	order[3] = 4;
	CHECK(rm_gb_alias_map(order, pos, 4, map, 16) == -1);
	order[3] = 3;
	CHECK(rm_gb_alias_map(order, pos, 4, map, 6) == -1);
	CHECK(rm_gb_alias_map(order, pos - 1, 4, map, 16) == -1);
	CHECK(rm_gb_alias_map(order, 5, 4, map, 16) == -1);
	order[0] = 2;
	CHECK(rm_gb_alias_map(order, pos, 4, map, 16) == -1);
	order[0] = 4;
	CHECK(rm_gb_alias_map(order, pos, 4, map, 16) == 7);
}

static void
test_alias_normal(void)
{
	float n[3], len;

	/* entry 5 of anorms.h is (0, 0, 1), entry 0 (-0.525731, 0, 0.850651) */
	rm_gb_alias_normal(5, 5, 0.3f, n);
	CHECK(near(n[0], 0, 1e-6f) && near(n[1], 0, 1e-6f) && near(n[2], 1, 1e-6f));
	rm_gb_alias_normal(0, 0, 0, n);
	CHECK(near(n[0], -0.525731f, 1e-5f) && near(n[2], 0.850651f, 1e-5f));

	/* half way between: the normalized mean */
	rm_gb_alias_normal(5, 0, 0.5f, n);
	len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
	CHECK(near(len, 1, 1e-5f));
	CHECK(near(n[0] / n[2], -0.525731f / (1.0f + 0.850651f), 1e-5f));

	/* backlerp 1 is the old frame's */
	rm_gb_alias_normal(5, 0, 1, n);
	CHECK(near(n[0], -0.525731f, 1e-5f));

	/* an index outside the table is the first entry, as rm_alias_lerp bounds it */
	rm_gb_alias_normal(200, 200, 0, n);
	CHECK(near(n[0], -0.525731f, 1e-5f));
}

static void
test_alias_extra(void)
{
	const unsigned short map[3] = { 2, 0, 2 };
	const float prev[3][4] = { { 1, 2, 3, 1 }, { 4, 5, 6, 1 }, { 7, 8, 9, 1 } };
	const unsigned char verts[12] = { 0, 0, 0, 5, 0, 0, 0, 5, 0, 0, 0, 0 };
	const unsigned char overts[12] = { 0, 0, 0, 5, 0, 0, 0, 5, 0, 0, 0, 5 };
	rm_gb_avx_t out[3];
	float n[3];

	CHECK(sizeof(rm_gb_avx_t) == 32);
	CHECK(sizeof(rm_gb_uniforms_t) == 208);
	CHECK(sizeof(rm_gb_resolve_t) == 16);

	CHECK(rm_gb_alias_extra(map, 3, 3, prev, verts, overts, 0.25f, out));
	CHECK(memcmp(out[0].prev, prev[2], sizeof(prev[2])) == 0 && memcmp(out[1].prev, prev[0], sizeof(prev[0])) == 0);
	CHECK(out[0].prev[3] == 1 && out[0].normal[3] == 0);
	rm_gb_alias_normal(0, 5, 0.25f, n);
	CHECK(out[0].normal[0] == n[0] && out[0].normal[1] == n[1] && out[0].normal[2] == n[2]);
	CHECK(near(out[1].normal[2], 1, 1e-6f));

	{
		const unsigned short bad[1] = { 3 };

		CHECK(!rm_gb_alias_extra(bad, 1, 3, prev, verts, overts, 0, out));
	}
}

int
main(void)
{
	test_path();
	test_render_size();
	test_bytes();
	test_render_rect();
	test_jitter();
	test_show();
	test_flatlight();
	test_entity_split();
	test_pixel_scale();
	test_motion();
	test_motion_camera();
	test_normal();
	test_resolve();
	test_match();
	test_hist();
	test_alias_map();
	test_alias_normal();
	test_alias_extra();

	printf("rm_gbuf_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
