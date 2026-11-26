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
 * Host test of RT-primary's light on models (TASK191, PM9): the rules of
 * rm_rtmodel.c. Which models take a probe, the probe made from
 * R_LightPoint's trace (no face, values that are not finite, the normal
 * turned toward the model), the frame's list (full, the same key twice),
 * the match of a model to a result (the model, the kind, the distance,
 * the nearest, the tie), the light in R_LightPoint's units, and the slot
 * a frame may read. Built and run by tests/check-ref-metal.sh, which also
 * builds it against mutants of rm_rtmodel.c that it must fail.
 *
 * =======================================================================
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../../src/client/refresh/metal/header/rm_rtmodel.h"

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
test_on(void)
{
	CHECK(rm_rtm_on(1, 1.0f));
	CHECK(rm_rtm_on(1, 0.5f));
	CHECK(!rm_rtm_on(1, 0.0f));
	CHECK(!rm_rtm_on(1, -0.0f));
	CHECK(!rm_rtm_on(1, NAN));
	CHECK(!rm_rtm_on(0, 1.0f));
	CHECK(!rm_rtm_on(0, 0.0f));
}

static void
test_traced(void)
{
	int f;

	/* a plain model, a minlight or glow one and the view weapon take a probe */
	CHECK(rm_rtm_traced(0, 0));
	CHECK(rm_rtm_traced(RM_RF_MINLIGHT, 0));
	CHECK(rm_rtm_traced(RM_RF_GLOW, 0));
	CHECK(rm_rtm_traced(RM_RF_WEAPONMODEL | RM_RF_MINLIGHT | RM_RF_DEPTHHACK, 0));
	CHECK(rm_rtm_traced(RM_RF_WEAPONMODEL, RM_RDF_IRGOGGLES));
	CHECK(rm_rtm_traced(RM_RF_IR_VISIBLE, 0));

	/* the stock ones */
	CHECK(!rm_rtm_traced(RM_RF_FULLBRIGHT, 0));
	CHECK(!rm_rtm_traced(RM_RF_TRANSLUCENT, 0));
	CHECK(!rm_rtm_traced(RM_RF_TRANSLUCENT | RM_RF_WEAPONMODEL, 0));
	CHECK(!rm_rtm_traced(RM_RF_DEPTHHACK, 0));
	CHECK(!rm_rtm_traced(RM_RF_IR_VISIBLE, RM_RDF_IRGOGGLES));
	CHECK(!rm_rtm_traced(RM_RF_SHELL_RED, 0));
	CHECK(!rm_rtm_traced(RM_RF_SHELL_HALF_DAM | RM_RF_WEAPONMODEL, 0));

	/* every combination of the flags the rule reads, against the rule written out */
	for (f = 0; f < (1 << 18); f++)
	{
		const int flags = f;
		int want = !(flags & (RM_RF_SHELLS | RM_RF_FULLBRIGHT | RM_RF_TRANSLUCENT)) &&
			!((flags & RM_RF_DEPTHHACK) && !(flags & RM_RF_WEAPONMODEL));

		CHECK(rm_rtm_traced(flags, 0) == want);

		if (flags & RM_RF_IR_VISIBLE)
		{
			want = 0;
		}

		CHECK(rm_rtm_traced(flags, RM_RDF_IRGOGGLES) == want);
	}
}

static void
test_probe(void)
{
	const float origin[3] = { 10, 20, 100 }, spot[3] = { 10, 20, 24 }, up[3] = { 0, 0, 1 }, down[3] = { 0, 0, -1 };
	const float slope[3] = { 0, 3, 4 }, zero[3] = { 0, 0, 0 };
	float bad[3] = { 10, 20, 24 };
	rm_rtm_probe_t p;

	memset(&p, 0, sizeof(p));
	CHECK(rm_rtm_probe(origin, spot, up, 1, &p));
	CHECK((p.spot[0] == 10) && (p.spot[1] == 20) && (p.spot[2] == 24));
	CHECK((p.normal[0] == 0) && (p.normal[1] == 0) && (p.normal[2] == 1));

	/* a face without samples was still met: its point is the probe's */
	CHECK(rm_rtm_probe(origin, spot, up, 0, &p));

	/* the plane's normal pointing away from the model is turned toward it */
	CHECK(rm_rtm_probe(origin, spot, down, 1, &p));
	CHECK((p.normal[2] == 1) && (p.normal[0] == 0) && (p.normal[1] == 0));

	/* a model below a ceiling it met (the normal toward it stays) */
	{
		const float under[3] = { 10, 20, 0 };

		CHECK(rm_rtm_probe(under, spot, down, 1, &p));
		CHECK(p.normal[2] == -1);
		CHECK(rm_rtm_probe(under, spot, up, 1, &p));
		CHECK(p.normal[2] == -1);
	}

	/* the normal is made unit */
	CHECK(rm_rtm_probe(origin, spot, slope, 1, &p));
	CHECK(near(p.normal[1], 0.6, 1e-6) && near(p.normal[2], 0.8, 1e-6));

	/* refused: no face, a zero or missing normal, a value that is not finite */
	CHECK(!rm_rtm_probe(origin, spot, up, -1, &p));
	CHECK(!rm_rtm_probe(origin, spot, zero, 1, &p));
	CHECK(!rm_rtm_probe(origin, spot, NULL, 1, &p));
	CHECK(!rm_rtm_probe(origin, NULL, up, 1, &p));
	bad[2] = NAN;
	CHECK(!rm_rtm_probe(origin, bad, up, 1, &p));
	bad[2] = INFINITY;
	CHECK(!rm_rtm_probe(origin, bad, up, 1, &p));
	bad[2] = 24;
	bad[0] = NAN;
	CHECK(!rm_rtm_probe(bad, spot, up, 1, &p));
	bad[0] = 0;
	bad[1] = NAN;
	CHECK(!rm_rtm_probe(origin, spot, bad, 1, &p));
}

static rm_rtm_key_t
key(const void *model, int weapon, float x, float y, float z)
{
	rm_rtm_key_t k;

	memset(&k, 0, sizeof(k));
	k.model = model;
	k.weapon = weapon;
	k.origin[0] = x;
	k.origin[1] = y;
	k.origin[2] = z;
	return k;
}

static void
test_add(void)
{
	static rm_rtm_frame_t f;
	static const int m1 = 1, m2 = 2;
	rm_rtm_probe_t p;
	rm_rtm_key_t k;
	int i;

	memset(&f, 0, sizeof(f));
	memset(&p, 0, sizeof(p));
	p.normal[2] = 1;

	k = key(&m1, 0, 1, 2, 3);
	CHECK(rm_rtm_add(&f, &k, &p) == 0);
	CHECK(f.n == 1);

	/* the same model drawn again (the shadow pass) is one probe */
	p.spot[0] = 7;
	CHECK(rm_rtm_add(&f, &k, &p) == 0);
	CHECK((f.n == 1) && (f.probe[0].spot[0] == 0));

	/* another model, another kind or another origin is another probe */
	k = key(&m2, 0, 1, 2, 3);
	CHECK(rm_rtm_add(&f, &k, &p) == 1);
	k = key(&m1, 1, 1, 2, 3);
	CHECK(rm_rtm_add(&f, &k, &p) == 2);
	k = key(&m1, 0, 1, 2, 4);
	CHECK(rm_rtm_add(&f, &k, &p) == 3);
	CHECK((f.n == 4) && (f.probe[3].spot[0] == 7) && (f.key[3].origin[2] == 4));

	/* up to RM_RTM_MAX, then refused */
	for (i = 4; i < RM_RTM_MAX; i++)
	{
		k = key(&m1, 0, (float)i, 0, 0);
		CHECK(rm_rtm_add(&f, &k, &p) == i);
	}

	k = key(&m2, 1, 99, 99, 99);
	CHECK(rm_rtm_add(&f, &k, &p) == -1);
	CHECK(f.n == RM_RTM_MAX);

	/* a list whose count is out of range takes nothing */
	f.n = -1;
	CHECK(rm_rtm_add(&f, &k, &p) == -1);
	f.n = RM_RTM_MAX + 1;
	CHECK(rm_rtm_add(&f, &k, &p) == -1);
}

static void
test_match(void)
{
	static const int m1 = 1, m2 = 2;
	const float o[3] = { 0, 0, 0 };
	rm_rtm_key_t keys[6];

	keys[0] = key(&m2, 0, 1, 0, 0);       /* another model, nearest */
	keys[1] = key(&m1, 1, 2, 0, 0);       /* another kind */
	keys[2] = key(&m1, 0, 30, 0, 0);
	keys[3] = key(&m1, 0, 0, -20, 0);     /* the nearest of the model */
	keys[4] = key(&m1, 0, 0, 0, 20);      /* a tie with 3: the first wins */
	keys[5] = key(&m1, 0, 64.5f, 0, 0);

	CHECK(rm_rtm_match(keys, 6, &m1, 0, o) == 3);
	CHECK(rm_rtm_match(keys, 6, &m2, 0, o) == 0);
	CHECK(rm_rtm_match(keys, 6, &m1, 1, o) == 1);
	CHECK(rm_rtm_match(keys, 6, &m2, 1, o) == -1);
	CHECK(rm_rtm_match(keys, 3, &m1, 0, o) == 2);
	CHECK(rm_rtm_match(keys, 0, &m1, 0, o) == -1);
	CHECK(rm_rtm_match(NULL, 6, &m1, 0, o) == -1);
	CHECK(rm_rtm_match(keys, -1, &m1, 0, o) == -1);

	/* the distance bound: 64 in, 64.5 out */
	{
		const float far[3] = { 128.5f, 0, 0 }, edge[3] = { 0.5f, 0, 0 };
		rm_rtm_key_t k[2];

		k[0] = key(&m1, 0, 64.5f, 0, 0);
		CHECK(rm_rtm_match(k, 1, &m1, 0, edge) == 0);
		CHECK(rm_rtm_match(k, 1, &m1, 0, far) == 0);
		CHECK(rm_rtm_match(k, 1, &m1, 0, o) == -1);
		k[1] = key(&m1, 0, 200, 0, 0);
		CHECK(rm_rtm_match(k, 2, &m1, 0, far) == 0);
	}

	/* an origin that is not finite matches nothing */
	{
		const float n[3] = { NAN, 0, 0 };

		CHECK(rm_rtm_match(keys, 6, &m1, 0, n) == -1);
	}
}

static void
test_point(void)
{
	const float l[3] = { 0.25f, 0.5f, 1.0f }, over[3] = { 2.0f, -1.0f, NAN };
	float p[3];

	rm_rtm_point(l, 1.0f, p);
	CHECK((p[0] == 0.25f) && (p[1] == 0.5f) && (p[2] == 1.0f));
	rm_rtm_point(l, 2.0f, p);
	CHECK((p[0] == 0.5f) && (p[1] == 1.0f) && (p[2] == 2.0f));
	rm_rtm_point(over, 1.5f, p);
	CHECK((p[0] == 1.5f) && (p[1] == 0.0f) && (p[2] == 0.0f));

	/* a modulate that is not finite or below 0 gives no light */
	rm_rtm_point(l, NAN, p);
	CHECK((p[0] == 0.0f) && (p[1] == 0.0f) && (p[2] == 0.0f));
	rm_rtm_point(l, -1.0f, p);
	CHECK((p[0] == 0.0f) && (p[1] == 0.0f) && (p[2] == 0.0f));
}

static void
test_slot(void)
{
	CHECK(rm_rtm_slot_ready(10, 10 + RMB_FRAMES_IN_FLIGHT));
	CHECK(!rm_rtm_slot_ready(10, 10 + RMB_FRAMES_IN_FLIGHT - 1));
	CHECK(!rm_rtm_slot_ready(10, 10 + RMB_FRAMES_IN_FLIGHT + 1));
	CHECK(!rm_rtm_slot_ready(10, 10));
	CHECK(!rm_rtm_slot_ready(0, RMB_FRAMES_IN_FLIGHT));
	CHECK(!rm_rtm_slot_ready(0, 0));
	CHECK(rm_rtm_slot_ready(1, 1 + RMB_FRAMES_IN_FLIGHT));
}

static void
test_stats(void)
{
	char b[256];

	CHECK(rm_rtm_stats_format(12, 5, 4, 1, b, sizeof(b)));
	CHECK(!strcmp(b, "ref_metal: rt models: frame 12: 5 probes traced, 4 models lit by a probe, 1 by the lightmap"));
	CHECK(!rm_rtm_stats_format(12, 5, 4, 1, b, 10));
	CHECK(b[0] == '\0');
	CHECK(!rm_rtm_stats_format(12, 5, 4, 1, b, 0));
}

int
main(void)
{
	test_on();
	test_traced();
	test_probe();
	test_add();
	test_match();
	test_point();
	test_slot();
	test_stats();
	printf("rm_rtmodel_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
