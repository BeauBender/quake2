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
 * The high pixel density decisions of the window code (TASK118,
 * src/client/vid/glimp_hidpi.c), and the 2D scale that follows the size
 * the client takes (src/client/cl_screen.c):
 *
 * - GLimp_HighDPIWanted asks for SDL_WINDOW_HIGH_PIXEL_DENSITY for the
 *   wayland, cocoa and uikit drivers when vid_highdpiaware is not 0, and
 *   never for another driver, a near miss of a name or no name.
 * - GLimp_DrawableInPixels takes the window's pixels for viddef on a
 *   HighDPI window that is not a fullscreen window, or a fullscreen
 *   window at r_mode -2; the mode's size otherwise.
 * - With the two together, a 640x480 window on a display of pixel density
 *   2 gives viddef 1280x960 and the default 2D scale 2 at
 *   vid_highdpiaware 1, and 640x480 and scale 1 at 0.
 *
 * Not proven here: what SDL makes of the flag (the window's pixels);
 * that is measured with the engine, see docs/runbook/TASK118.md.
 * Nor the mouse: the engine draws no menu pointer and counts motion only
 * in game (src/client/input/sdl3.c), so the card's menu cursor item is
 * closed by that code path in the runbook, not by a test here.
 *
 * =======================================================================
 */

#include "test.h"
#include "../../src/client/header/client.h"
#include "stub_client.h"

TEST(hidpi_wanted_on_the_apple_drivers_and_wayland)
{
	T_CHECK(GLimp_HighDPIWanted(1, "cocoa"));
	T_CHECK(GLimp_HighDPIWanted(1, "uikit"));
	T_CHECK(GLimp_HighDPIWanted(1, "wayland"));

	/* any value but 0, as the window code has always read it */
	T_CHECK(GLimp_HighDPIWanted(2, "cocoa"));
	T_CHECK(GLimp_HighDPIWanted(-1, "uikit"));
	T_CHECK(GLimp_HighDPIWanted(0.5f, "wayland"));
}

TEST(hidpi_not_wanted_at_0)
{
	T_CHECK(!GLimp_HighDPIWanted(0, "cocoa"));
	T_CHECK(!GLimp_HighDPIWanted(0, "uikit"));
	T_CHECK(!GLimp_HighDPIWanted(0, "wayland"));
	T_CHECK(!GLimp_HighDPIWanted(-0.0f, "cocoa"));
}

TEST(hidpi_not_wanted_on_other_drivers)
{
	static const char *const others[] = {
		"x11", "windows", "offscreen", "dummy", "kmsdrm", "emscripten", "",
		"Cocoa", "UIKIT", "coco", "cocoa2", "uikit ", " uikit", "waylandx", "wayland\n"
	};
	size_t i;

	for (i = 0; i < sizeof(others) / sizeof(others[0]); i++)
	{
		if (GLimp_HighDPIWanted(1, others[i]))
		{
			t_fail(__FILE__, __LINE__, "the driver \"%s\" must not get the flag", others[i]);
		}
	}

	T_CHECK(!GLimp_HighDPIWanted(1, NULL));
	T_CHECK(!GLimp_HighDPIWanted(0, NULL));
}

TEST(hidpi_drawable_in_pixels_on_a_highdpi_window)
{
	/* windowed and exclusive fullscreen: always the window's pixels */
	T_CHECK(GLimp_DrawableInPixels(true, 0, 4));
	T_CHECK(GLimp_DrawableInPixels(true, 0, -1));
	T_CHECK(GLimp_DrawableInPixels(true, 0, -2));
	T_CHECK(GLimp_DrawableInPixels(true, 1, 4));
	T_CHECK(GLimp_DrawableInPixels(true, 1, -2));

	/* the fullscreen window: its pixels only for the native size */
	T_CHECK(GLimp_DrawableInPixels(true, 2, -2));
	T_CHECK(!GLimp_DrawableInPixels(true, 2, 4));
	T_CHECK(!GLimp_DrawableInPixels(true, 2, -1));
	T_CHECK(!GLimp_DrawableInPixels(true, 2, -3));
	T_CHECK(!GLimp_DrawableInPixels(true, 2, -2.5f));
	T_CHECK(!GLimp_DrawableInPixels(true, 2, 2));
}

TEST(hidpi_drawable_never_in_pixels_without_the_flag)
{
	int fullscreen;

	for (fullscreen = 0; fullscreen <= 2; fullscreen++)
	{
		T_CHECK(!GLimp_DrawableInPixels(false, fullscreen, 4));
		T_CHECK(!GLimp_DrawableInPixels(false, fullscreen, -1));
		T_CHECK(!GLimp_DrawableInPixels(false, fullscreen, -2));
	}
}

/*
 * The client's choice for a 640x480 window on a display of the given
 * density, as GLimp_InitGraphics makes it: SDL gives a HighDPI window the
 * display's pixels and any other window its size in points.
 */
static void
t_hidpi_viddef(float highdpiaware, int density, int fullscreen, float r_mode)
{
	const int mode_w = 640, mode_h = 480;
	qboolean highdpi = GLimp_HighDPIWanted(highdpiaware, "cocoa");
	int pix_w = highdpi ? mode_w * density : mode_w;
	int pix_h = highdpi ? mode_h * density : mode_h;

	if (GLimp_DrawableInPixels(highdpi, fullscreen, r_mode))
	{
		viddef.width = pix_w;
		viddef.height = pix_h;
	}
	else
	{
		viddef.width = mode_w;
		viddef.height = mode_h;
	}
}

TEST(hidpi_2d_scale_follows_the_size_taken)
{
	stub_client_cvars();
	Cvar_Set("r_hudscale", "-1");
	Cvar_Set("r_consolescale", "-1");
	Cvar_Set("r_menuscale", "-1");

	t_hidpi_viddef(1, 2, 0, -1);
	T_EQ_INT(viddef.width, 1280);
	T_EQ_INT(viddef.height, 960);
	T_EQ_FLT(SCR_GetHUDScale(), 2);
	T_EQ_FLT(SCR_GetConsoleScale(), 2);
	T_EQ_FLT(SCR_GetMenuScale(), 2);

	t_hidpi_viddef(0, 2, 0, -1);
	T_EQ_INT(viddef.width, 640);
	T_EQ_INT(viddef.height, 480);
	T_EQ_FLT(SCR_GetHUDScale(), 1);
	T_EQ_FLT(SCR_GetMenuScale(), 1);

	/* density 1: both values give the same size */
	t_hidpi_viddef(1, 1, 0, -1);
	T_EQ_INT(viddef.width, 640);
	T_EQ_INT(viddef.height, 480);
	T_EQ_FLT(SCR_GetMenuScale(), 1);

	/* a fullscreen window at r_mode 4 keeps the mode's size */
	t_hidpi_viddef(1, 2, 2, 4);
	T_EQ_INT(viddef.width, 640);
	T_EQ_INT(viddef.height, 480);
	T_EQ_FLT(SCR_GetMenuScale(), 1);

	viddef.width = 640;
	viddef.height = 480;
}

/*
 * TASK147: the screen an iOS window was made for. A rotation moves the
 * safe rectangle (and may swap the pixel size); any one of the six
 * numbers differing is a change, and an unmeasured screen is one.
 */
TEST(hidpi_screen_changed_on_any_number)
{
	/* an iPhone 17 landscape: 2556x1179 pixels, 852x393 points, 59 points of corner on each side, 21 below */
	const int made[GLIMP_SCREEN_INTS] = { 2556, 1179, 59, 0, 734, 372 };
	int now[GLIMP_SCREEN_INTS];
	int i;

	memcpy(now, made, sizeof(now));
	T_CHECK(!GLimp_ScreenChanged(made, now));

	for (i = 0; i < GLIMP_SCREEN_INTS; i++)
	{
		memcpy(now, made, sizeof(now));
		now[i] += 1;
		T_CHECK(GLimp_ScreenChanged(made, now));
	}

	/* the other landscape: the housing on the other side, the safe rectangle's x moved */
	memcpy(now, made, sizeof(now));
	now[2] = 0;
	now[4] = 793;
	T_CHECK(GLimp_ScreenChanged(made, now));
}

TEST(hidpi_screen_unmeasured_is_a_change)
{
	const int made[GLIMP_SCREEN_INTS] = { 2556, 1179, 59, 0, 734, 372 };
	const int empty_w[GLIMP_SCREEN_INTS] = { 0, 1179, 59, 0, 734, 372 };
	const int empty_h[GLIMP_SCREEN_INTS] = { 2556, 0, 59, 0, 734, 372 };
	const int zero[GLIMP_SCREEN_INTS] = { 0, 0, 0, 0, 0, 0 };

	T_CHECK(GLimp_ScreenChanged(NULL, made));
	T_CHECK(GLimp_ScreenChanged(made, NULL));
	T_CHECK(GLimp_ScreenChanged(NULL, NULL));
	/* an empty drawable is a change even against itself */
	T_CHECK(GLimp_ScreenChanged(empty_w, empty_w));
	T_CHECK(GLimp_ScreenChanged(empty_h, empty_h));
	T_CHECK(GLimp_ScreenChanged(zero, zero));
}

/*
 * TASK172 round 2: what the window code does when the screen's pixel size
 * or safe rectangle moved (GLimp_ScreenAction): nothing for an empty
 * window or a screen already noted, a vid_restart when the renderer gives
 * no drawable, and the live resize otherwise.
 */
TEST(hidpi_orientation_names)
{
	T_CHECK(GLimp_OrientationBits("portrait") == GLIMP_ORIENT_PORTRAIT);
	T_CHECK(GLimp_OrientationBits("PortraitUpsideDown") == GLIMP_ORIENT_UPSIDEDOWN);
	T_CHECK(GLimp_OrientationBits("landscapeleft") == GLIMP_ORIENT_LANDSCAPELEFT);
	T_CHECK(GLimp_OrientationBits("landscaperight") == GLIMP_ORIENT_LANDSCAPERIGHT);
	T_CHECK(GLimp_OrientationBits("landscape") == (GLIMP_ORIENT_LANDSCAPELEFT | GLIMP_ORIENT_LANDSCAPERIGHT));
	T_CHECK(GLimp_OrientationBits("all") == 15);
	T_CHECK(GLimp_OrientationBits("") == 0);
	T_CHECK(GLimp_OrientationBits("land") == 0);
	T_CHECK(GLimp_OrientationBits(NULL) == 0);
}

TEST(hidpi_screen_action_ignore)
{
	const int made[GLIMP_SCREEN_INTS] = { 2622, 1206, 62, 0, 750, 382 };
	const int empty_w[GLIMP_SCREEN_INTS] = { 0, 1206, 62, 0, 750, 382 };
	const int empty_h[GLIMP_SCREEN_INTS] = { 2622, 0, 62, 0, 750, 382 };

	/* the same screen again: the second event of one change, with or without a drawable */
	T_CHECK(GLimp_ScreenAction(made, made, 2250, 1146) == GLIMP_SCREEN_IGNORE);
	T_CHECK(GLimp_ScreenAction(made, made, 0, 0) == GLIMP_SCREEN_IGNORE);
	/* a window with no pixels is left alone, even against a record that differs */
	T_CHECK(GLimp_ScreenAction(made, empty_w, 2250, 1146) == GLIMP_SCREEN_IGNORE);
	T_CHECK(GLimp_ScreenAction(made, empty_h, 2250, 1146) == GLIMP_SCREEN_IGNORE);
	T_CHECK(GLimp_ScreenAction(empty_w, empty_w, 2250, 1146) == GLIMP_SCREEN_IGNORE);
	T_CHECK(GLimp_ScreenAction(made, NULL, 2250, 1146) == GLIMP_SCREEN_IGNORE);
}

TEST(hidpi_screen_action_restart)
{
	const int made[GLIMP_SCREEN_INTS] = { 2622, 1206, 62, 0, 750, 382 };
	/* the other landscape: the same size, the insets swapped */
	const int flipped[GLIMP_SCREEN_INTS] = { 2622, 1206, 0, 0, 812, 382 };

	/* changed, and the renderer gives no drawable: vid_restart */
	T_CHECK(GLimp_ScreenAction(made, flipped, 0, 0) == GLIMP_SCREEN_RESTART);
	T_CHECK(GLimp_ScreenAction(made, flipped, 2250, 0) == GLIMP_SCREEN_RESTART);
	T_CHECK(GLimp_ScreenAction(made, flipped, 0, 1146) == GLIMP_SCREEN_RESTART);
	T_CHECK(GLimp_ScreenAction(made, flipped, -1, -1) == GLIMP_SCREEN_RESTART);
	/* nothing noted yet: a change too */
	T_CHECK(GLimp_ScreenAction(NULL, flipped, 0, 0) == GLIMP_SCREEN_RESTART);
}

TEST(hidpi_screen_action_live)
{
	const int made[GLIMP_SCREEN_INTS] = { 2622, 1206, 62, 0, 750, 382 };
	const int flipped[GLIMP_SCREEN_INTS] = { 2622, 1206, 0, 0, 812, 382 };
	const int bigger[GLIMP_SCREEN_INTS] = { 2420, 1668, 0, 0, 1210, 814 };

	T_CHECK(GLimp_ScreenAction(made, flipped, 2436, 1146) == GLIMP_SCREEN_LIVE);
	T_CHECK(GLimp_ScreenAction(made, bigger, 2420, 1628) == GLIMP_SCREEN_LIVE);
	T_CHECK(GLimp_ScreenAction(NULL, bigger, 2420, 1628) == GLIMP_SCREEN_LIVE);
}

/*
 * TASK147: with the real game data default.cfg sets r_mode 4 and
 * vid_fullscreen 1. On the uikit driver the window is still the whole
 * screen at its own size (fullscreen 2, r_mode -2, so the client takes
 * the drawable's pixels); no other driver is touched.
 */
TEST(hidpi_ios_is_the_screen_whatever_default_cfg_says)
{
	int fs = 1;
	float mode = 4;

	T_CHECK(GLimp_OwnScreenOnly("uikit", &fs, &mode));
	T_EQ_INT(fs, 2);
	T_EQ_FLT(mode, -2);
	T_CHECK(GLimp_DrawableInPixels(GLimp_HighDPIWanted(1, "uikit"), fs, mode));

	/* any starting values give the same */
	fs = 0;
	mode = -1;
	T_CHECK(GLimp_OwnScreenOnly("uikit", &fs, &mode));
	T_EQ_INT(fs, 2);
	T_EQ_FLT(mode, -2);
}

TEST(hidpi_other_drivers_keep_their_mode)
{
	static const char *const others[] = { "cocoa", "wayland", "x11", "UIKIT", "uikitx", "" };
	int i;

	for (i = 0; i < 6; i++)
	{
		int fs = 1;
		float mode = 4;

		T_CHECK(!GLimp_OwnScreenOnly(others[i], &fs, &mode));
		T_EQ_INT(fs, 1);
		T_EQ_FLT(mode, 4);
	}

	{
		int fs = 1;
		float mode = 4;

		T_CHECK(!GLimp_OwnScreenOnly(NULL, &fs, &mode));
		T_CHECK(!GLimp_OwnScreenOnly("uikit", NULL, &mode));
		T_CHECK(!GLimp_OwnScreenOnly("uikit", &fs, NULL));
		T_EQ_INT(fs, 1);
		T_EQ_FLT(mode, 4);
	}
}

/*
 * FIX76: on uikit (iOS) the engine does not ask for relative mouse mode
 * to be turned on while no mouse is connected; turning it off is always
 * asked, and every other driver is asked as before.
 */
TEST(hidpi_ios_no_relative_mouse_without_a_mouse)
{
	T_CHECK(!GLimp_RelativeMouseWanted("uikit", false, true));
	T_CHECK(GLimp_RelativeMouseWanted("uikit", true, true));
	T_CHECK(GLimp_RelativeMouseWanted("uikit", false, false));
	T_CHECK(GLimp_RelativeMouseWanted("uikit", true, false));
	T_CHECK(GLimp_RelativeMouseWanted("cocoa", false, true));
	T_CHECK(GLimp_RelativeMouseWanted("cocoa", true, true));
	T_CHECK(GLimp_RelativeMouseWanted("x11", false, true));
	T_CHECK(GLimp_RelativeMouseWanted(NULL, false, true));
}

/*
 * FIX76: a failed request is reported once per run of failures, not once
 * per frame; a success lets the next failure be reported again.
 */
TEST(hidpi_relative_mouse_failure_reported_once)
{
	qboolean reported = false;
	int printed = 0;
	int i;

	for (i = 0; i < 1000; i++)
	{
		if (GLimp_RelativeMouseReport(false, &reported))
		{
			printed++;
		}
	}

	T_EQ_INT(printed, 1);

	/* a success prints nothing and re-arms the report */
	T_CHECK(!GLimp_RelativeMouseReport(true, &reported));
	T_CHECK(GLimp_RelativeMouseReport(false, &reported));
	T_CHECK(!GLimp_RelativeMouseReport(false, &reported));

	/* successes alone never print */
	reported = false;
	for (i = 0; i < 10; i++)
	{
		T_CHECK(!GLimp_RelativeMouseReport(true, &reported));
	}

	T_CHECK(!GLimp_RelativeMouseReport(false, NULL));
}
