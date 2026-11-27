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
 * TASK190 slice 3: RM_Effect's r_metalfx under RT-primary, over the real
 * rm_pres_fx and the effect table's rm_rtfx_show_asked (rm_rtfx.c with
 * the recording stubs of rm_rtfx_host.h): the temporal scaler where the
 * view is below its output, off for an asked development view and on a
 * device without the scaler, the user's at the output's own size and in
 * every other mode.
 * Round 2 (I1): RM_PresBelow's real text too (rm_main.c), with the real
 * rm_view_rect (rm_pure.c, copied the same way) and rm_fx_scale
 * (rm_metalfx.c): the view rectangle sized as R_SetupGL sizes it, with
 * viewsize below 100 shrinking it to the default rows or fewer, and the
 * two views of one frame where the menu's player model
 * (RDF_NOWORLDMODEL) is drawn over the world, which keep the world
 * view's value; a mutant that drops RM_PresBelow's early return for that
 * view fails it.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/client/refresh/metal/header/rm_rtgate.h"
#include "../../src/client/refresh/metal/header/rm_rttemporal.h"
#include "../../src/client/refresh/metal/header/rm_rtfx.h"
#include "../../src/client/refresh/metal/header/rm_metalfx.h"

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
static int rm_pres_temporal = 1;   /* the device has the temporal scaler */
static int rm_pres_below = 1;      /* the view's internal size is below its output */

extern const char *rm_rtfx_host_yes;   /* rm_rtfx_host.h: the effect entries that say yes */

/* the fields of refdef_t (shared.h) that RM_PresBelow reads, and the flag of the menu's player model */
typedef struct
{
	int x, y, width, height;
	int rdflags;
} refdef_t;

#define RDF_NOWORLDMODEL 2

/* the screen (vid) and the drawable's size (rmb_target_size) */
static struct
{
	int width, height;
} vid;

static int target_w, target_h;
static unsigned rm_pres_families = RMB_FAMILY_BIT(9);   /* apple9: 1080 default rows */
static cvar_t *r_metalfx_scale_ref;

void
rmb_target_size(int *w, int *h)
{
	*w = target_w;
	*h = target_h;
}

float
rmb_fx_max_ratio(int mode)
{
	(void)mode;
	return RM_FX_RATIO_MAX;
}

float RM_Effect(const cvar_t *cv);
int RM_RTT_On(void);

#include "rttab_body.c"

static cvar_t ab = { "r_rt_ab", "", 0.0f };
static cvar_t temporal = { "r_rt_temporal", "0.9", 0.9f };
static cvar_t metalfx = { "r_metalfx", "0", 0.0f };
static cvar_t fxscale = { "r_metalfx_scale", "0", 0.0f };

/* the screen and the drawable, w by h */
static void
screen(int w, int h)
{
	vid.width = target_w = w;
	vid.height = target_h = h;
}

/* SCR_CalcVrect's view at viewsize size on the screen, and the value r_metalfx reads for it */
static float
world_view(int size)
{
	refdef_t fd;

	fd.width = vid.width * size / 100;
	fd.height = vid.height * size / 100;
	fd.x = (vid.width - fd.width) / 2;
	fd.y = (vid.height - fd.height) / 2;
	fd.rdflags = 0;
	RM_PresBelow(&fd);
	return RM_Effect(&metalfx);
}

/* the menu's player model at menu scale s (menu.c: 168 * s + 4 * s lines high), and the value read for it */
static float
model_view(int s)
{
	refdef_t fd;

	fd.width = 160 * s;
	fd.height = 168 * s + 4 * s;
	fd.x = vid.width / 2;
	fd.y = vid.height / 2 - fd.height / 2;
	fd.rdflags = RDF_NOWORLDMODEL;
	RM_PresBelow(&fd);
	return RM_Effect(&metalfx);
}

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

	/* r_metalfx under RT-primary: the temporal scaler below the output, the user's 0 at its own size */
	set("0", 0.0f, "");
	rm_presentation = RM_PRES_RTPRIMARY;
	rm_rtfx_host_yes = "";
	CHECK(RM_Effect(&metalfx) == 2.0f);
	rm_pres_below = 0;
	CHECK(RM_Effect(&metalfx) == 0.0f);
	rm_pres_below = 1;

	/* a development view asked for (the table's walk, any row) is drawn unscaled */
	rm_rtfx_host_yes = "direct:show_asked";
	CHECK(RM_Effect(&metalfx) == 0.0f);
	rm_rtfx_host_yes = "water:show_asked";
	CHECK(RM_Effect(&metalfx) == 0.0f);
	rm_rtfx_host_yes = "";

	/* no temporal scaler on the device: off */
	rm_pres_temporal = 0;
	CHECK(RM_Effect(&metalfx) == 0.0f);
	rm_pres_temporal = 1;

	/* the stock presentation: the user's value, a development view asked or not */
	rm_presentation = RM_PRES_STOCK;
	metalfx.string = "2";
	metalfx.value = 2.0f;
	rm_rtfx_host_yes = "direct:show_asked";
	CHECK(RM_Effect(&metalfx) == 2.0f);
	rm_rtfx_host_yes = "";
	metalfx.string = "0";
	metalfx.value = 0.0f;

	/* RM_PresBelow: the view's rectangle against the default rows (apple9, 1080), the user's 0 under RT-primary */
	rm_presentation = RM_PRES_RTPRIMARY;
	r_metalfx_scale_ref = &fxscale;
	screen(2560, 1440);
	CHECK(world_view(100) == 2.0f);    /* 1440 lines: 1080 is below */
	CHECK(world_view(80) == 2.0f);     /* 1152 lines */
	CHECK(world_view(75) == 0.0f);     /* 1080 lines: the default rows, so no scaler */
	CHECK(world_view(70) == 0.0f);     /* 1008 lines */
	screen(1920, 1080);
	CHECK(world_view(100) == 0.0f);    /* a window no taller than the default rows */
	screen(2560, 1441);
	CHECK(world_view(100) == 2.0f);    /* an odd height */

	/* a scale of its own: 1 or more is the output's size, under 1 is below it whatever the view */
	screen(1920, 1080);
	fxscale.value = 0.5f;
	CHECK(world_view(70) == 2.0f);
	fxscale.value = 1.0f;
	screen(2560, 1440);
	CHECK(world_view(100) == 0.0f);
	fxscale.value = 0.0f;

	/*
	 * Two views in one frame (I1): the world, then the menu's player model
	 * over it (RDF_NOWORLDMODEL, 688 lines at menu scale 4). The model's
	 * view is drawn forward and never scaled, so it keeps the world's value:
	 * one that changed between the views would free and remake the scaler
	 * every frame.
	 */
	screen(2560, 1440);
	for (int frame = 0; frame < 3; frame++)
	{
		CHECK(world_view(100) == 2.0f);
		CHECK(model_view(4) == 2.0f);
	}

	/* and under a world view at its own size the model keeps that view's 0 */
	CHECK(world_view(70) == 0.0f);
	CHECK(model_view(4) == 0.0f);

	printf("rm_rttab_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
