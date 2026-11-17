/*
 * Copyright (C) 2026 Yamagi Quake II contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * =======================================================================
 *
 * The layout arithmetic of a window of any aspect (TASK172, the iPhone
 * Duo): its outer display and its inner one, each in portrait and in
 * landscape, with insets that differ on every side. The view is the
 * window less its insets (the safe rectangle, viddef on iOS); for it:
 *
 * - the field of view, CL_AspectFov: Hor+ at 4:3 and wider, as before;
 *   with the vertical minimum (the iOS rule) the horizontal view is never
 *   narrower than cl_fov, so a portrait or near-square view keeps the
 *   whole 4:3 view and shows more above and below it;
 * - the HUD's rectangles: the 320x240 block the layout's xv and yv
 *   commands centre, at the HUD scale, the console's and the menu's
 *   scale, and the corners xl, xr, yt and yb anchor to, all inside the view.
 *
 * The window sizes are the device type's own (its profile.plist: outer
 * 1398x2034 pixels, inner 2007x2853, both at 3x) and the insets are of
 * the kind the simulator reports, unequal on purpose.
 *
 * =======================================================================
 */

#include <math.h>

#include "test.h"
#include "../../src/client/header/client.h"
#include "stub_client.h"

typedef struct
{
	const char *name;
	int w, h;                     /* the window in pixels */
	int left, top, right, bottom; /* its insets in pixels */
} duo_state_t;

static const duo_state_t duo_states[] = {
	/* the outer display, folded */
	{ "outer portrait", 1398, 2034, 0, 177, 0, 102 },
	{ "outer landscape", 2034, 1398, 177, 0, 141, 63 },
	/* the inner display, unfolded */
	{ "inner portrait", 2007, 2853, 0, 141, 0, 60 },
	{ "inner landscape", 2853, 2007, 141, 0, 96, 60 },
	/* the same, with insets that differ on every side */
	{ "inner portrait, odd", 2007, 2853, 13, 141, 57, 60 },
	{ "outer landscape, odd", 2034, 1398, 0, 9, 177, 63 },
};

#define DUO_STATES (sizeof(duo_states) / sizeof(duo_states[0]))

static void
duo_view(const duo_state_t *s, int *vw, int *vh)
{
	*vw = s->w - s->left - s->right;
	*vh = s->h - s->top - s->bottom;
}

/* the vertical view of a w x h view with a horizontal one of fov_x: cl_view.c's CalcFov */
static float
duo_fov_y(float fov_x, float w, float h)
{
	return atanf(h / (w / tanf(fov_x / 360.0f * (float)M_PI))) * 360.0f / (float)M_PI;
}

/* Hor+'s own formula (AdaptFov): the vertical view of 4:3 at fov, the horizontal one for w x h */
static float
duo_horplus(float fov, float w, float h)
{
	return atanf(tanf(fov / 360.0f * (float)M_PI) * (w / h * 0.75f)) / (float)M_PI * 360.0f;
}

/* At 4:3 and wider the rule is Hor+, with or without the vertical minimum: nothing wide changes. */
TEST(duo_fov_wide_is_horplus)
{
	static const int wide[][2] = { { 640, 480 }, { 1920, 1080 }, { 2250, 1146 }, { 2420, 1628 }, { 2616, 1947 } };
	size_t i;

	for (i = 0; i < sizeof(wide) / sizeof(wide[0]); i++)
	{
		float w = wide[i][0], h = wide[i][1];

		T_CHECK(fabsf(CL_AspectFov(90, w, h, true) - duo_horplus(90, w, h)) < 0.001f);
		T_CHECK(fabsf(CL_AspectFov(90, w, h, false) - duo_horplus(90, w, h)) < 0.001f);
		T_CHECK(CL_AspectFov(90, w, h, true) >= 90 - 0.001f);
	}
}

/* Each Duo state: the horizontal view is at least cl_fov and the vertical one at least 4:3's. */
TEST(duo_fov_any_aspect_keeps_the_4_3_view)
{
	const float fov43_y = duo_fov_y(90, 4, 3);
	size_t i;

	for (i = 0; i < DUO_STATES; i++)
	{
		int vw, vh;
		float x, y;

		duo_view(&duo_states[i], &vw, &vh);
		x = CL_AspectFov(90, vw, vh, true);
		y = duo_fov_y(x, vw, vh);

		if (x < 90 - 0.001f || y < fov43_y - 0.001f)
		{
			printf("    %s: view %dx%d, fov %.2f x %.2f, under 90 x %.2f\n", duo_states[i].name, vw, vh, x, y, fov43_y);
		}

		T_CHECK(x >= 90 - 0.001f);
		T_CHECK(y >= fov43_y - 0.001f);
	}
}

/* Portrait: exactly cl_fov across, whatever cl_fov is; without the minimum, Hor+ as before (narrower). */
TEST(duo_fov_portrait_is_cl_fov_across)
{
	static const float fovs[] = { 60, 90, 110 };
	size_t i, j;

	for (i = 0; i < DUO_STATES; i++)
	{
		int vw, vh;

		duo_view(&duo_states[i], &vw, &vh);

		if (vw * 3 >= vh * 4)
		{
			continue;
		}

		for (j = 0; j < sizeof(fovs) / sizeof(fovs[0]); j++)
		{
			T_CHECK(fabsf(CL_AspectFov(fovs[j], vw, vh, true) - fovs[j]) < 0.001f);
			T_CHECK(fabsf(CL_AspectFov(fovs[j], vw, vh, false) - duo_horplus(fovs[j], vw, vh)) < 0.001f);
			T_CHECK(CL_AspectFov(fovs[j], vw, vh, false) < fovs[j]);
		}
	}
}

/* A view with no size keeps the fov it was given. */
TEST(duo_fov_empty_view)
{
	T_CHECK(CL_AspectFov(90, 0, 1179, true) == 90);
	T_CHECK(CL_AspectFov(90, 2556, 0, true) == 90);
	T_CHECK(CL_AspectFov(90, -1, -1, false) == 90);
}

/* The HUD's 320x240 block, its corners, the console and the menu lie inside each Duo view, at every scale asked. */
TEST(duo_hud_inside_the_view)
{
	static const char *scales[] = { "-1", "1", "2", "4", "8", "20" };
	size_t i, j;

	stub_client_cvars();

	for (i = 0; i < DUO_STATES; i++)
	{
		int vw, vh;

		duo_view(&duo_states[i], &vw, &vh);
		viddef.width = vw;
		viddef.height = vh;

		for (j = 0; j < sizeof(scales) / sizeof(scales[0]); j++)
		{
			float hud, con_s, menu;
			int x0, y0;

			Cvar_Set("r_hudscale", (char *)scales[j]);
			Cvar_Set("r_consolescale", (char *)scales[j]);
			Cvar_Set("r_menuscale", (char *)scales[j]);
			hud = SCR_GetHUDScale();
			con_s = SCR_GetConsoleScale();
			menu = SCR_GetMenuScale();

			/* xv 0 .. xv 320 and yv 0 .. yv 240: cl_screen.c's layout puts width / 2 - 160 * scale + scale * v into an int */
			x0 = vw / 2 - 160 * hud;
			y0 = vh / 2 - 120 * hud;
			T_CHECK(x0 >= 0 && x0 + 320 * hud <= vw);
			T_CHECK(y0 >= 0 && y0 + 240 * hud <= vh);

			/* the status bar's corners: xr -24 .. the edge, yb -24 .. the edge (a 24 pixel pic at scale) */
			T_CHECK(vw - 24 * hud >= 0);
			T_CHECK(vh - 24 * hud >= 0);

			/* the console's 8x8 characters and the menu's 320x240 frame */
			T_CHECK(vw / (8 * con_s) >= 40);
			T_CHECK(320 * menu <= vw && 240 * menu <= vh);
		}
	}

	Cvar_Set("r_hudscale", "-1");
	Cvar_Set("r_consolescale", "-1");
	Cvar_Set("r_menuscale", "-1");
	memset(&viddef, 0, sizeof(viddef));
}
