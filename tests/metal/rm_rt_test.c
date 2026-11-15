/*
 * Host test of the pure part of ref_metal's ray tracing (RT1, TASK53):
 * src/client/refresh/metal/rm_rt.c, with rm_pure.c and common/ for the
 * entity matrix it reuses. No SDL, no Metal, no game data. Run by
 * tests/check-ref-metal.sh; prints "rm_rt_test: N checks, M failed".
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

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
near(float a, float b)
{
	return fabsf(a - b) < 1e-5f;
}

/* sky first, warped is water (a translucent one too), translucent is alpha, the rest world */
static void
test_material(void)
{
	CHECK(rm_rt_material(0) == RM_RT_MAT_WORLD);
	CHECK(rm_rt_material(RM_TEX_SKY) == RM_RT_MAT_SKY);
	CHECK(rm_rt_material(RM_TEX_SKY | RM_TEX_WARP | RM_TEX_TRANS33) == RM_RT_MAT_SKY);
	CHECK(rm_rt_material(RM_TEX_WARP) == RM_RT_MAT_WATER);
	CHECK(rm_rt_material(RM_TEX_WARP | RM_TEX_TRANS33) == RM_RT_MAT_WATER);
	CHECK(rm_rt_material(RM_TEX_WARP | RM_TEX_TRANS66) == RM_RT_MAT_WATER);
	CHECK(rm_rt_material(RM_TEX_TRANS33) == RM_RT_MAT_ALPHA);
	CHECK(rm_rt_material(RM_TEX_TRANS66) == RM_RT_MAT_ALPHA);
	CHECK(rm_rt_material(RM_TEX_FLOWING | RM_TEX_TRANS66) == RM_RT_MAT_ALPHA);
	CHECK(rm_rt_material(RM_TEX_FLOWING) == RM_RT_MAT_WORLD);
	/* SURF_LIGHT (1), SURF_SLICK (2), SURF_NODRAW (0x80) change nothing */
	CHECK(rm_rt_material(0x1 | 0x2 | 0x80) == RM_RT_MAT_WORLD);
	/* the ids are the G-buffer's of design section 3 */
	CHECK(RM_RT_MAT_NONE == 0 && RM_RT_MAT_WORLD == 1 && RM_RT_MAT_ALIAS == 2 && RM_RT_MAT_WATER == 3 &&
		RM_RT_MAT_SKY == 4 && RM_RT_MAT_ALPHA == 5 && RM_RT_MATS == 6);
}

/* the off state: every setting at its default builds nothing, nor a device without ray tracing */
static void
test_wanted(void)
{
	CHECK(rm_rt_wanted(0, 1) == 0);
	CHECK(rm_rt_wanted(0, 0) == 0);
	CHECK(rm_rt_wanted(1, 1) == 1);
	CHECK(rm_rt_wanted(2, 1) == 1);
	CHECK(rm_rt_wanted(-1, 1) == 1);
	CHECK(rm_rt_wanted(1, 0) == 0);
	CHECK(rm_rt_wanted(2, 0) == 0);
	CHECK(rm_rt_wanted(NAN, 1) == 0);
	CHECK(rm_rt_wanted(INFINITY, 1) == 0);
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
test_materials(void)
{
	rm_surfrec_t r[4];
	unsigned char mat[16];
	static const unsigned char want[] = { 1, 1, 4, 3, 3, 3, 5 };

	rec(&r[0], 0, 0, 6);
	rec(&r[1], RM_TEX_SKY, 6, 3);
	rec(&r[2], RM_TEX_WARP | RM_TEX_TRANS33, 9, 9);
	rec(&r[3], RM_TEX_TRANS66, 18, 3);
	memset(mat, 0xee, sizeof(mat));
	CHECK(rm_rt_materials(r, 4, 21, mat, sizeof(mat)) == 7);
	CHECK(memcmp(mat, want, sizeof(want)) == 0);
	CHECK(mat[7] == 0xee);   /* nothing past the triangles */

	/* exactly the room it needs */
	CHECK(rm_rt_materials(r, 4, 21, mat, 7) == 7);
	CHECK(rm_rt_materials(r, 4, 21, mat, 6) == -1);
	/* the records must cover the buffer, no more and no less */
	CHECK(rm_rt_materials(r, 4, 24, mat, sizeof(mat)) == -1);
	CHECK(rm_rt_materials(r, 4, 18, mat, sizeof(mat)) == -1);
	CHECK(rm_rt_materials(r, 3, 18, mat, sizeof(mat)) == 6);
	/* a gap, an overlap, a count that is not whole triangles */
	r[1].first_index = 7;
	CHECK(rm_rt_materials(r, 4, 21, mat, sizeof(mat)) == -1);
	r[1].first_index = 5;
	CHECK(rm_rt_materials(r, 4, 21, mat, sizeof(mat)) == -1);
	r[1].first_index = 6;
	r[1].num_indices = 4;
	r[2].first_index = 10;
	r[2].num_indices = 8;
	r[3].first_index = 18;
	CHECK(rm_rt_materials(r, 4, 21, mat, sizeof(mat)) == -1);
	/* nothing at all */
	CHECK(rm_rt_materials(r, 0, 0, mat, 0) == 0);
	CHECK(rm_rt_materials(NULL, 0, 0, NULL, 0) == 0);
	CHECK(rm_rt_materials(r, -1, 0, mat, sizeof(mat)) == -1);
	/* a record past the end of a 32-bit buffer does not wrap */
	rec(&r[0], 0, 0, 3);
	rec(&r[1], 0, 3, 0xfffffffdu);
	CHECK(rm_rt_materials(r, 2, 6, mat, sizeof(mat)) == -1);
}

static void
test_range(void)
{
	unsigned long first = 99, n = 99;

	CHECK(rm_rt_range(0, 6, 18, &first, &n) == 1 && first == 0 && n == 2);
	CHECK(rm_rt_range(15, 3, 18, &first, &n) == 1 && first == 5 && n == 1);
	CHECK(rm_rt_range(0, 18, 18, &first, &n) == 1 && first == 0 && n == 6);
	CHECK(rm_rt_range(3, 0, 18, &first, &n) == 0);
	CHECK(rm_rt_range(4, 6, 18, &first, &n) == 0);
	CHECK(rm_rt_range(3, 4, 18, &first, &n) == 0);
	CHECK(rm_rt_range(12, 9, 18, &first, &n) == 0);
	CHECK(rm_rt_range(18, 3, 18, &first, &n) == 0);
	CHECK(rm_rt_range(21, 3, 18, &first, &n) == 0);
	CHECK(rm_rt_range(~0ul - 2, 6, ~0ul, &first, &n) == 0);
}

static void
test_count(void)
{
	static const unsigned char mat[] = { 1, 1, 4, 3, 3, 3, 5, 9 };
	rm_rt_tris_t t;

	memset(&t, 0x55, sizeof(t));
	rm_rt_count(mat, 0, 7, &t);
	CHECK(t.total == 7);
	CHECK(t.tris[RM_RT_MAT_WORLD] == 2 && t.tris[RM_RT_MAT_SKY] == 1 && t.tris[RM_RT_MAT_WATER] == 3 &&
		t.tris[RM_RT_MAT_ALPHA] == 1 && t.tris[RM_RT_MAT_ALIAS] == 0 && t.tris[RM_RT_MAT_NONE] == 0);
	rm_rt_count(mat, 2, 2, &t);
	CHECK(t.total == 2 && t.tris[RM_RT_MAT_SKY] == 1 && t.tris[RM_RT_MAT_WATER] == 1 && t.tris[RM_RT_MAT_WORLD] == 0);
	/* an id past the table counts as none, not past the array */
	rm_rt_count(mat, 7, 1, &t);
	CHECK(t.total == 1 && t.tris[RM_RT_MAT_NONE] == 1);
	rm_rt_count(mat, 0, 0, &t);
	CHECK(t.total == 0 && t.tris[RM_RT_MAT_WORLD] == 0);
}

static void
test_frames(void)
{
	size_t b = 7;

	CHECK(rm_rt_frames_bytes(10, 40 + 4 * 100, 40, 100, 1 << 20, &b) == 1 && b == 4400);
	CHECK(rm_rt_frames_bytes(1, 44, 40, 1, 44, &b) == 1 && b == 44);
	CHECK(rm_rt_frames_bytes(512, 40 + 4 * 2048, 40, 2048, 64u << 20, &b) == 1 && b == 512u * 8232u);
	CHECK(rm_rt_frames_bytes(1, 44, 40, 1, 43, &b) == 0 && b == 0);
	CHECK(rm_rt_frames_bytes(0, 44, 40, 1, 1 << 20, &b) == 0);
	CHECK(rm_rt_frames_bytes(513, 44, 40, 1, 1 << 20, &b) == 0);
	CHECK(rm_rt_frames_bytes(1, 40 + 4 * 2049, 40, 2049, 1 << 20, &b) == 0);
	CHECK(rm_rt_frames_bytes(1, 44, 40, 0, 1 << 20, &b) == 0);
	CHECK(rm_rt_frames_bytes(1, 46, 40, 1, 1 << 20, &b) == 0);    /* frame size not a multiple of 4 */
	CHECK(rm_rt_frames_bytes(1, 46, 42, 1, 1 << 20, &b) == 0);    /* vertex offset not a multiple of 4 */
	CHECK(rm_rt_frames_bytes(2, 40 + 4 * 99, 40, 100, 1 << 20, &b) == 0);   /* the vertices do not fit a frame */
	CHECK(rm_rt_frames_bytes(1, 44, -4, 1, 1 << 20, &b) == 0);
	CHECK(rm_rt_frames_bytes(1, -44, 40, 1, 1 << 20, &b) == 0);

	CHECK(rm_rt_frame_vertex(0, 10, 440, 40) == 10);
	CHECK(rm_rt_frame_vertex(2, 10, 440, 40) == 230);
	CHECK(rm_rt_frame_vertex(9, 10, 440, 40) == (9 * 440 + 40) / 4);
	CHECK(rm_rt_frame_vertex(10, 10, 440, 40) == -1);
	CHECK(rm_rt_frame_vertex(-1, 10, 440, 40) == -1);
}

static void
test_alias_indices(void)
{
	/* dtriangle_t: index_xyz[3], index_st[3] */
	short tris[3][6] = { { 0, 1, 2, 7, 7, 7 }, { 2, 3, 0, 7, 7, 7 }, { 3, 1, 2, 7, 7, 7 } };
	unsigned short out[12];
	static const unsigned short want[] = { 0, 1, 2, 2, 3, 0, 3, 1, 2 };

	memset(out, 0xee, sizeof(out));
	CHECK(rm_rt_alias_indices(&tris[0][0], 6, 3, 4, out, 12) == 3);
	CHECK(memcmp(out, want, sizeof(want)) == 0);
	CHECK(out[9] == 0xeeee);
	CHECK(rm_rt_alias_indices(&tris[0][0], 6, 3, 4, out, 9) == 3);
	CHECK(rm_rt_alias_indices(&tris[0][0], 6, 3, 4, out, 8) == 0);
	/* an index at num_xyz, a negative one */
	CHECK(rm_rt_alias_indices(&tris[0][0], 6, 3, 3, out, 12) == 0);
	tris[1][1] = -1;
	CHECK(rm_rt_alias_indices(&tris[0][0], 6, 3, 4, out, 12) == 0);
	tris[1][1] = 3;
	/* the st indices are not read */
	tris[2][4] = -5;
	CHECK(rm_rt_alias_indices(&tris[0][0], 6, 3, 4, out, 12) == 3);
	/* counts outside MD2's limits, a stride shorter than a triangle */
	CHECK(rm_rt_alias_indices(&tris[0][0], 6, 0, 4, out, 12) == 0);
	CHECK(rm_rt_alias_indices(&tris[0][0], 6, RM_RT_MD2_MAX_TRIS + 1, 4, out, 12) == 0);
	CHECK(rm_rt_alias_indices(&tris[0][0], 6, 3, 0, out, 12) == 0);
	CHECK(rm_rt_alias_indices(&tris[0][0], 6, 3, RM_MD2_MAX_VERTS + 1, out, 12) == 0);
	CHECK(rm_rt_alias_indices(&tris[0][0], 2, 3, 4, out, 12) == 0);
	CHECK(rm_rt_alias_indices(NULL, 6, 3, 4, out, 12) == 0);
}

/* xform is column-major 4 by 3: column c is xform[3c .. 3c + 2] */
static void
apply(const float *x, const float *p, float *out)
{
	int r;

	for (r = 0; r < 3; r++)
	{
		out[r] = x[r] * p[0] + x[3 + r] * p[1] + x[6 + r] * p[2] + x[9 + r];
	}
}

static void
test_transform(void)
{
	static const float zero[3] = { 0, 0, 0 }, org[3] = { 10, -20, 30 };
	static const float id[12] = { 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 };
	float x[12], a[3], p[3], q[3], m[16], w[16];
	int i;

	rm_rt_transform(zero, zero, 0, x);
	for (i = 0; i < 12; i++)
	{
		CHECK(near(x[i], id[i]));
	}

	rm_rt_transform(org, zero, 1, x);
	CHECK(near(x[9], 10) && near(x[10], -20) && near(x[11], 30) && near(x[0], 1) && near(x[4], 1) && near(x[8], 1));

	/* yaw 90: x goes to y, y to -x */
	a[0] = 0; a[1] = 90; a[2] = 0;
	rm_rt_transform(zero, a, 0, x);
	p[0] = 1; p[1] = 0; p[2] = 0;
	apply(x, p, q);
	CHECK(near(q[0], 0) && near(q[1], 1) && near(q[2], 0));
	p[0] = 0; p[1] = 1;
	apply(x, p, q);
	CHECK(near(q[0], -1) && near(q[1], 0) && near(q[2], 0));

	/* pitch 90 (both kinds turn the pitch): a model's forward x points down, -z, as Quake's pitch does */
	a[0] = 90; a[1] = 0; a[2] = 0;
	for (i = 0; i < 2; i++)
	{
		rm_rt_transform(zero, a, i, x);
		p[0] = 1; p[1] = 0; p[2] = 0;
		apply(x, p, q);
		CHECK(near(q[0], 0) && near(q[1], 0) && near(q[2], -1));
	}

	/* roll 90: a brush model turns its roll, an alias model does not, so they roll opposite ways */
	a[0] = 0; a[1] = 0; a[2] = 90;
	p[0] = 0; p[1] = 1; p[2] = 0;
	rm_rt_transform(zero, a, 0, x);
	apply(x, p, q);
	CHECK(near(q[0], 0) && near(q[1], 0) && near(q[2], -1));
	rm_rt_transform(zero, a, 1, x);
	apply(x, p, q);
	CHECK(near(q[0], 0) && near(q[1], 0) && near(q[2], 1));

	/* the same as the raster's R_RotateForEntity (rm_entity_matrix) on the identity, with its turned angles */
	a[0] = 33; a[1] = -71; a[2] = 12;
	mtl_mat_identity(w);
	{
		float t[3] = { -a[0], a[1], a[2] };

		rm_entity_matrix(m, w, org, t);
	}
	rm_rt_transform(org, a, 0, x);
	for (i = 0; i < 4; i++)
	{
		CHECK(near(x[3 * i], m[4 * i]) && near(x[3 * i + 1], m[4 * i + 1]) && near(x[3 * i + 2], m[4 * i + 2]));
	}
	{
		float t[3] = { -a[0], a[1], -a[2] };

		rm_entity_matrix(m, w, org, t);
	}
	rm_rt_transform(org, a, 1, x);
	for (i = 0; i < 4; i++)
	{
		CHECK(near(x[3 * i], m[4 * i]) && near(x[3 * i + 1], m[4 * i + 1]) && near(x[3 * i + 2], m[4 * i + 2]));
	}
}

static void
ent(rm_rt_ent_t *e, int kind, int flags, int model)
{
	memset(e, 0, sizeof(*e));
	e->kind = kind;
	e->flags = flags;
	e->model = model;
	e->origin[0] = 1;
	e->origin[1] = 2;
	e->origin[2] = 3;
	e->angles[1] = 45;
	e->alpha = 1;
}

static void
test_instances(void)
{
	rm_rt_ent_t e[12];
	rm_rt_inst_t out[RM_RT_MAX_INSTANCES], big_out[RM_RT_MAX_INSTANCES + 8];
	static rm_rt_ent_t many[RM_MAX_ENTITIES + 10];
	rm_rt_excl_t x;
	float want[12];
	int n, i;

	ent(&e[0], RM_RT_ENT_ALIAS, RM_RF_WEAPONMODEL | RM_RF_DEPTHHACK | RM_RF_MINLIGHT, 7);
	ent(&e[1], RM_RT_ENT_ALIAS, 0, 3);
	ent(&e[2], RM_RT_ENT_BRUSH, 0, 4);
	ent(&e[3], RM_RT_ENT_BRUSH, 0, 0);         /* an inline model without a structure */
	ent(&e[4], RM_RT_ENT_SPRITE, 0, 0);
	ent(&e[5], RM_RT_ENT_NULL, 0, 0);
	ent(&e[6], RM_RT_ENT_ALIAS, RM_RF_BEAM, 3);  /* a beam is a beam, whatever its model */
	ent(&e[7], RM_RT_ENT_ALIAS, RM_RF_SHELL_RED | RM_RF_TRANSLUCENT, 3);
	ent(&e[8], RM_RT_ENT_ALIAS, 0, 3);
	e[8].origin[1] = NAN;
	ent(&e[9], RM_RT_ENT_ALIAS, 0, 0);         /* an alias model without a structure (refused) */
	ent(&e[10], RM_RT_ENT_ALIAS, RM_RF_TRANSLUCENT, 5);
	ent(&e[11], RM_RT_ENT_BRUSH, RM_RF_SHELL_HALF_DAM, 6);

	n = rm_rt_instances(e, 12, 1, 0, 0, out, RM_RT_MAX_INSTANCES, &x);
	CHECK(n == 4);
	CHECK(out[0].kind == RM_RT_INST_WORLD && out[0].model == 0 && out[0].ent == -1);
	CHECK(near(out[0].xform[0], 1) && near(out[0].xform[4], 1) && near(out[0].xform[8], 1) && near(out[0].xform[9], 0));
	CHECK(out[1].kind == RM_RT_INST_ALIAS && out[1].model == 3 && out[1].ent == 1);
	CHECK(out[2].kind == RM_RT_INST_INLINE && out[2].model == 4 && out[2].ent == 2);
	CHECK(out[3].kind == RM_RT_INST_ALIAS && out[3].model == 5 && out[3].ent == 10);
	CHECK(x.weapon == 1 && x.nomodel == 2 && x.sprite == 1 && x.null == 1 && x.beam == 1 && x.shell == 2 && x.bad == 1);

	rm_rt_transform(e[2].origin, e[2].angles, 1, want);
	CHECK(memcmp(out[2].xform, want, sizeof(want)) == 0);
	rm_rt_transform(e[1].origin, e[1].angles, 0, want);
	CHECK(memcmp(out[1].xform, want, sizeof(want)) == 0);

	/* r_rt_gunshadow 1 keeps the view weapon, unless the raster hides it (hand 2) */
	n = rm_rt_instances(e, 12, 1, 1, 0, out, RM_RT_MAX_INSTANCES, &x);
	CHECK(n == 5 && out[1].kind == RM_RT_INST_ALIAS && out[1].model == 7 && out[1].ent == 0 && x.weapon == 0);
	n = rm_rt_instances(e, 12, 1, 1, 1, out, RM_RT_MAX_INSTANCES, &x);
	CHECK(n == 5 && x.weapon == 0);
	n = rm_rt_instances(e, 12, 1, 1, 2, out, RM_RT_MAX_INSTANCES, &x);
	CHECK(n == 4 && x.weapon == 1);

	/* no world, no world instance */
	n = rm_rt_instances(e, 12, 0, 0, 0, out, RM_RT_MAX_INSTANCES, &x);
	CHECK(n == 3 && out[0].kind == RM_RT_INST_ALIAS && out[0].ent == 1);

	/* the cap holds, the world comes first */
	n = rm_rt_instances(e, 12, 1, 0, 0, out, 2, &x);
	CHECK(n == 2 && out[0].kind == RM_RT_INST_WORLD && out[1].ent == 1);
	n = rm_rt_instances(e, 12, 1, 0, 0, out, 0, &x);
	CHECK(n == 0);
	n = rm_rt_instances(e, 0, 1, 0, 0, out, RM_RT_MAX_INSTANCES, &x);
	CHECK(n == 1 && x.beam == 0 && x.weapon == 0 && x.nomodel == 0);
	n = rm_rt_instances(e, -3, 1, 0, 0, out, RM_RT_MAX_INSTANCES, &x);
	CHECK(n == 1);

	/* at most RM_MAX_ENTITIES entities are read, as the raster's loops */
	for (i = 0; i < RM_MAX_ENTITIES + 10; i++)
	{
		ent(&many[i], RM_RT_ENT_ALIAS, 0, 1);
	}
	n = rm_rt_instances(many, RM_MAX_ENTITIES + 10, 1, 0, 0, big_out, RM_RT_MAX_INSTANCES + 8, &x);
	CHECK(n == RM_RT_MAX_INSTANCES && big_out[n - 1].ent == RM_MAX_ENTITIES - 1);

	/* every float is checked: oldorigin, backlerp, alpha, angles */
	ent(&e[0], RM_RT_ENT_ALIAS, 0, 3);
	e[0].oldorigin[2] = INFINITY;
	ent(&e[1], RM_RT_ENT_ALIAS, 0, 3);
	e[1].backlerp = NAN;
	ent(&e[2], RM_RT_ENT_BRUSH, 0, 3);
	e[2].alpha = NAN;
	ent(&e[3], RM_RT_ENT_BRUSH, 0, 3);
	e[3].angles[0] = -INFINITY;
	n = rm_rt_instances(e, 4, 0, 0, 0, out, RM_RT_MAX_INSTANCES, &x);
	CHECK(n == 0 && x.bad == 4);
}

static void
test_bytes(void)
{
	CHECK(rm_rt_instance_bytes(RM_RT_MAX_INSTANCES) == (size_t)RM_RT_MAX_INSTANCES * 64);
	CHECK(rm_rt_instance_bytes(1) == 64);
	CHECK(rm_rt_instance_bytes(0) == 0);
	CHECK(rm_rt_instance_bytes(RM_RT_MAX_INSTANCES + 1) == 0);
	CHECK(rm_rt_instance_bytes(-1) == 0);
	CHECK(rm_rt_avert_bytes(RM_MD2_MAX_VERTS) == (size_t)RM_MD2_MAX_VERTS * 16);
	CHECK(rm_rt_avert_bytes(1) == 16);
	CHECK(rm_rt_avert_bytes(0) == 0);
	CHECK(rm_rt_avert_bytes(RM_MD2_MAX_VERTS + 1) == 0);
	CHECK(rm_rt_aindex_bytes(RM_RT_MD2_MAX_TRIS) == (size_t)RM_RT_MD2_MAX_TRIS * 6);
	CHECK(rm_rt_aindex_bytes(1) == 6);
	CHECK(rm_rt_aindex_bytes(0) == 0);
	CHECK(rm_rt_aindex_bytes(RM_RT_MD2_MAX_TRIS + 1) == 0);
}

static void
test_spread(void)
{
	double a[] = { 3, 1, 2 }, b[] = { 4, 1, 3, 2 }, c[100], d[1] = { 0.25 };
	rm_rt_spread_t s;
	int i;

	CHECK(rm_rt_spread(a, 3, &s) == 1);
	CHECK(s.n == 3 && s.min == 1 && s.median == 2 && s.p95 == 3 && s.max == 3);
	CHECK(a[0] == 1 && a[1] == 2 && a[2] == 3);
	CHECK(rm_rt_spread(b, 4, &s) == 1 && s.median == 2.5 && s.min == 1 && s.max == 4 && s.p95 == 4);

	for (i = 0; i < 100; i++)
	{
		c[i] = 100 - i;   /* 100 down to 1 */
	}
	CHECK(rm_rt_spread(c, 100, &s) == 1 && s.min == 1 && s.max == 100 && s.median == 50.5 && s.p95 == 95);
	CHECK(rm_rt_spread(d, 1, &s) == 1 && s.min == 0.25 && s.median == 0.25 && s.p95 == 0.25 && s.max == 0.25);

	s.n = 9;
	s.max = 9;
	CHECK(rm_rt_spread(a, 0, &s) == 0 && s.n == 0 && s.max == 0 && s.min == 0);
}

static void
test_format(void)
{
	rm_rt_map_stats_t m;
	rm_rt_frame_stats_t f;
	rm_rt_spread_t sa, si;
	char buf[RM_STATS_LINE], small[40];

	memset(&m, 0, sizeof(m));
	m.world.tris[RM_RT_MAT_WORLD] = 9000;
	m.world.tris[RM_RT_MAT_SKY] = 500;
	m.world.tris[RM_RT_MAT_WATER] = 300;
	m.world.tris[RM_RT_MAT_ALPHA] = 20;
	m.world.total = 9820;
	m.inline_models = 12;
	m.inlines.tris[RM_RT_MAT_WORLD] = 700;
	m.inlines.tris[RM_RT_MAT_ALPHA] = 4;
	m.inlines.total = 704;
	m.world_bytes = 1000000;
	m.inline_bytes = 70000;
	m.scratch_bytes = 300000;
	m.material_bytes = 10524;
	m.build_ms = 0.7126;

	CHECK(rm_rt_map_format("maps/base1.bsp", &m, buf, sizeof(buf)) == 1);
	CHECK(strcmp(buf, "ref_metal: rt: maps/base1.bsp: world 9820 triangles (world 9000, sky 500, water 300, "
		"alpha 20), 1000000 bytes; 12 inline models, 704 triangles (world 700, sky 0, water 0, alpha 4), "
		"70000 bytes; materials 10524 bytes, build scratch 300000 bytes; built in 0.713 ms GPU") == 0);
	m.build_ms = -1;
	CHECK(rm_rt_map_format("maps/base1.bsp", &m, buf, sizeof(buf)) == 1);
	CHECK(strstr(buf, "; build time not measured") != NULL && strstr(buf, "built in") == NULL);
	CHECK(rm_rt_map_format("maps/base1.bsp", &m, small, sizeof(small)) == 0 && small[0] == '\0');
	CHECK(rm_rt_map_format("maps/base1.bsp", &m, small, 0) == 0);

	memset(&f, 0, sizeof(f));
	f.frame = 42;
	f.instances = 20;
	f.inlines = 6;
	f.aliases = 13;
	f.alias_tris = 6500;
	f.built = 2;
	f.refit = 11;
	f.excl.beam = 1;
	f.excl.sprite = 2;
	f.excl.weapon = 1;
	f.excl.shell = 3;
	f.alias_bytes = 640000;
	f.vertex_bytes = 52000;
	f.model_bytes = 900000;
	f.ias_bytes = 12000;
	f.scratch_bytes = 4000;
	f.alias_ms = 0.2034;
	f.ias_ms = 0.1749;
	CHECK(rm_rt_frame_format(&f, buf, sizeof(buf)) == 1);
	CHECK(strcmp(buf, "ref_metal: rt: frame 42: 20 instances (6 inline, 13 alias), alias 6500 triangles, "
		"2 built, 11 refit, 0 skipped; left out: 1 beams, 0 null, 2 sprites, 1 weapon, 3 shells, 0 without a "
		"structure, 0 bad; bytes: alias structures 640000, vertices 52000, models 900000, instance structure "
		"12000, scratch 4000; gpu ms: alias 0.203, instances 0.175") == 0);
	f.alias_ms = -1;
	f.ias_ms = -1;
	CHECK(rm_rt_frame_format(&f, buf, sizeof(buf)) == 1);
	CHECK(strstr(buf, "; gpu ms: not measured") != NULL);
	CHECK(rm_rt_frame_format(&f, small, sizeof(small)) == 0 && small[0] == '\0');

	memset(&sa, 0, sizeof(sa));
	memset(&si, 0, sizeof(si));
	sa.n = 120;
	sa.min = 0.1;
	sa.median = 0.2;
	sa.p95 = 0.3;
	sa.max = 0.4;
	si.n = 120;
	si.min = 0.15;
	si.median = 0.17;
	si.p95 = 0.19;
	si.max = 0.6;
	CHECK(rm_rt_summary_format("maps/base1.bsp", 130, &sa, &si, 14, 7000, buf, sizeof(buf)) == 1);
	CHECK(strcmp(buf, "ref_metal: rt: maps/base1.bsp: 130 frames; gpu ms over 120 frames: alias lerp and refit "
		"min 0.100, median 0.200, p95 0.300, max 0.400; instance rebuild min 0.150, median 0.170, p95 0.190, "
		"max 0.600; busiest frame 14 alias instances, 7000 alias triangles") == 0);
	sa.n = si.n = 0;
	CHECK(rm_rt_summary_format("maps/base1.bsp", 3, &sa, &si, 0, 0, buf, sizeof(buf)) == 1);
	CHECK(strcmp(buf, "ref_metal: rt: maps/base1.bsp: 3 frames; gpu ms not measured; busiest frame 0 alias "
		"instances, 0 alias triangles") == 0);
	CHECK(rm_rt_summary_format("maps/base1.bsp", 3, &sa, &si, 0, 0, small, sizeof(small)) == 0 && small[0] == '\0');
}

int
main(void)
{
	test_material();
	test_wanted();
	test_materials();
	test_range();
	test_count();
	test_frames();
	test_alias_indices();
	test_transform();
	test_instances();
	test_bytes();
	test_spread();
	test_format();

	printf("rm_rt_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
