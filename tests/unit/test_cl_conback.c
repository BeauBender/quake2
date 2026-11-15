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
 * The console background's rectangle (TASK119, src/client/cl_conback.c).
 * cl_console.c, which draws it, is not linked into the test binary, so
 * the arithmetic it calls is: Con_BackRect, with the stretched default
 * and the 4:3 switch con_backaspect.
 *
 * =======================================================================
 */

#include <limits.h>

#include "test.h"
#include "../../src/client/header/client.h"

static void
rect(int vw, int vh, int lines, int keep43, int *x, int *y, int *w, int *h)
{
	*x = *y = *w = *h = -12345;
	Con_BackRect(vw, vh, lines, keep43, x, y, w, h);
}

/* the switch off: the whole window, its bottom edge at the console's, as Con_DrawConsole drew it before */
TEST(conback_stretched_by_default)
{
	int x, y, w, h;

	rect(1920, 1080, 540, 0, &x, &y, &w, &h);
	T_EQ_INT(x, 0);
	T_EQ_INT(y, 540 - 1080);
	T_EQ_INT(w, 1920);
	T_EQ_INT(h, 1080);

	rect(640, 480, 480, 0, &x, &y, &w, &h);
	T_EQ_INT(y, 0);
	T_EQ_INT(w, 640);
	T_EQ_INT(h, 480);
}

/* a window of 4:3: the same rectangle as stretched, whatever the console's depth */
TEST(conback_43_window_equals_stretched)
{
	static const int sizes[][2] = { { 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1600, 1200 }, { 2048, 1536 }, { 4, 3 } };
	size_t i;
	int lines;

	for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
	{
		for (lines = 1; lines <= sizes[i][1]; lines += (sizes[i][1] / 7) + 1)
		{
			int x, y, w, h, x0, y0, w0, h0;

			rect(sizes[i][0], sizes[i][1], lines, 1, &x, &y, &w, &h);
			rect(sizes[i][0], sizes[i][1], lines, 0, &x0, &y0, &w0, &h0);
			T_EQ_INT(x, x0);
			T_EQ_INT(y, y0);
			T_EQ_INT(w, w0);
			T_EQ_INT(h, h0);
		}
	}
}

/* wider than 4:3: as wide as the window, taller than it, centred nothing sideways, bottom edge at the console's */
TEST(conback_wide_window_crops_the_top)
{
	int x, y, w, h;

	rect(1920, 1080, 1080, 1, &x, &y, &w, &h);
	T_EQ_INT(x, 0);
	T_EQ_INT(w, 1920);
	T_EQ_INT(h, 1440);
	T_EQ_INT(y, 1080 - 1440);

	/* half the screen down: the bottom edge follows the console */
	rect(1920, 1080, 540, 1, &x, &y, &w, &h);
	T_EQ_INT(y, 540 - 1440);
	T_EQ_INT(h, 1440);

	/* 2560 by 1440: 1920 high, rounded up when the quarter is not whole */
	rect(2560, 1440, 1440, 1, &x, &y, &w, &h);
	T_EQ_INT(w, 2560);
	T_EQ_INT(h, 1920);
	rect(1001, 500, 500, 1, &x, &y, &w, &h);
	T_EQ_INT(w, 1001);
	T_EQ_INT(h, 751);   /* 1001 * 3 / 4 = 750.75 */
	T_CHECK(h >= 500);
}

/* narrower than 4:3: as high as the window, wider than it, centred, the extra pixel on the right */
TEST(conback_narrow_window_crops_the_sides)
{
	int x, y, w, h;

	rect(1000, 1000, 1000, 1, &x, &y, &w, &h);
	T_EQ_INT(h, 1000);
	T_EQ_INT(w, 1334);   /* 4000 / 3 = 1333.33, rounded up */
	T_EQ_INT(x, -167);   /* 334 / 2 */
	T_EQ_INT(y, 0);
	T_CHECK(x + w >= 1000);

	rect(828, 1792, 1792, 1, &x, &y, &w, &h);   /* an iPhone held upright */
	T_EQ_INT(h, 1792);
	T_EQ_INT(w, 2390);   /* 7168 / 3 = 2389.33 */
	T_CHECK(x <= 0 && x + w >= 828);
}

/* a covering rectangle in every window: it spans the window both ways and keeps 4:3 to a pixel */
TEST(conback_covers_every_window)
{
	int vw, vh;

	for (vh = 1; vh <= 300; vh += 7)
	{
		for (vw = 1; vw <= 400; vw += 5)
		{
			int x, y, w, h;

			rect(vw, vh, vh, 1, &x, &y, &w, &h);
			T_CHECK(x <= 0);
			T_CHECK(x + w >= vw);
			T_CHECK(y <= 0);
			T_CHECK(y + h == vh);
			T_CHECK(w >= vw && h >= vh);
			/* the shape is 4:3 to the rounding of one side */
			T_CHECK(w * 3 >= h * 4 - 3 && w * 3 <= h * 4 + 3);
		}
	}
}

/* sizes that are not positive, and sizes whose rectangle does not fit an int, are drawn stretched */
TEST(conback_degenerate_sizes_are_stretched)
{
	int x, y, w, h;

	rect(0, 480, 100, 1, &x, &y, &w, &h);
	T_EQ_INT(w, 0);
	T_EQ_INT(h, 480);
	T_EQ_INT(x, 0);
	T_EQ_INT(y, 100 - 480);

	rect(640, 0, 0, 1, &x, &y, &w, &h);
	T_EQ_INT(w, 640);
	T_EQ_INT(h, 0);

	rect(-5, -7, 3, 1, &x, &y, &w, &h);
	T_EQ_INT(w, -5);
	T_EQ_INT(h, -7);

	rect(INT_MAX, 1, 1, 1, &x, &y, &w, &h);
	T_EQ_INT(w, INT_MAX);
	T_EQ_INT(h, 1610612736);   /* (3 * INT_MAX + 3) / 4 */

	rect(1, INT_MAX, INT_MAX, 1, &x, &y, &w, &h);
	T_EQ_INT(h, INT_MAX);
	T_EQ_INT(w, 1);   /* 4 * INT_MAX / 3 does not fit */
	T_EQ_INT(x, 0);
}
