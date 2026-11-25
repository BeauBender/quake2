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
 * The host test of r_rt_temporal through the tuning harness's A/B layer
 * (RT7, TASK59 slice 2b): r_rt_temporal is a tuning row, so r_rt_ab's B
 * side carries it, and the renderer must read it through RM_Effect at both
 * of its sites, the gate (RM_RTT_On) and the step of the view's history
 * (RM_RTT_ViewFor's alpha). Neither file builds for the host, so
 * tests/check-ref-metal.sh copies the real text of RM_Effect (rm_main.c),
 * of RM_RTT_On and of RM_RTT_ViewFor's alpha line (rm_rtaoscene.c) into
 * rttab_body.c, which this file includes; it links the real rm_ab_value
 * and rm_pres_value (rm_rtgate.c) and rm_rtt_alpha (rm_rttemporal.c). A
 * mutant that reads r_rt_temporal->value directly at either site fails it.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/client/refresh/metal/header/rm_rtgate.h"
#include "../../src/client/refresh/metal/header/rm_rttemporal.h"

/* the fields of cvar_t that the copied text reads */
typedef struct
{
	char *name;
	char *string;
	float value;
} cvar_t;

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

static int rm_presentation = RM_PRES_STOCK;
static cvar_t *r_rt_ab;
static cvar_t *r_rt_temporal;

float RM_Effect(const cvar_t *cv);
int RM_RTT_On(void);

#include "rttab_body.c"

static cvar_t ab = { "r_rt_ab", "", 0.0f };
static cvar_t temporal = { "r_rt_temporal", "0.9", 0.9f };

static void
set(const char *a, float v, char *spec)
{
	static char s[32];

	snprintf(s, sizeof(s), "%s", a);
	temporal.string = s;
	temporal.value = v;
	ab.string = spec;
}

int
main(void)
{
	r_rt_ab = &ab;

	/* no cvar yet: off at both sites */
	r_rt_temporal = NULL;
	CHECK(!RM_RTT_On());
	CHECK(rttab_view_alpha() == 0.0f);
	r_rt_temporal = &temporal;

	/* A alone: the user's value */
	set("0.9", 0.9f, "");
	CHECK(RM_RTT_On());
	CHECK(rttab_view_alpha() > 0.0f);
	set("0", 0.0f, "");
	CHECK(!RM_RTT_On());
	CHECK(rttab_view_alpha() == 0.0f);

	/* B holds 0 with A at 0.9: the accumulation is off at both sites */
	set("0.9", 0.9f, "B: off|r_rt_temporal=0;");
	CHECK(!RM_RTT_On());
	CHECK(rttab_view_alpha() == 0.0f);

	/* B holds 0.9 with A at 0: on at both sites, and the alpha is B's */
	set("0", 0.0f, "B: stock|r_rt_temporal=0.9;");
	CHECK(RM_RTT_On());
	CHECK(rttab_view_alpha() == rm_rtt_alpha(0.9f));

	/* a B side without the row leaves A's value */
	set("0", 0.0f, "B: other|r_rt_ao=1;");
	CHECK(!RM_RTT_On());
	CHECK(rttab_view_alpha() == 0.0f);

	printf("rm_rttab_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
