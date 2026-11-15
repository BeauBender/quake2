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
 * The software renderer's view rectangle against its buffer (TASK118,
 * R_ClampViewToBuffer in src/client/refresh/soft/sw_view.c): a view
 * inside the buffer is left as it is, and a view that is not, from a
 * client that sized it for a larger window, ends up inside the buffer,
 * never past its end.
 *
 * =======================================================================
 */

#include "test.h"
#include "../../src/client/refresh/soft/header/local.h"

static refdef_t
view(int x, int y, int width, int height)
{
	refdef_t rd;

	memset(&rd, 0, sizeof(rd));
	rd.x = x;
	rd.y = y;
	rd.width = width;
	rd.height = height;

	return rd;
}

/* the rectangle lies inside a width x height buffer, at least 1x1 */
static qboolean
inside(const refdef_t *rd, int width, int height)
{
	return (rd->x >= 0) && (rd->y >= 0) && (rd->width >= 1) && (rd->height >= 1) &&
		(rd->x + rd->width <= width) && (rd->y + rd->height <= height);
}

TEST(sw_view_inside_the_buffer_is_unchanged)
{
	static const int cases[][4] = {
		{0, 0, 640, 480},	/* the whole buffer */
		{0, 0, 1280, 960},	/* the whole HighDPI buffer */
		{64, 48, 512, 384},	/* viewsize 80 at 640x480 */
		{639, 479, 1, 1},	/* the last pixel */
	};
	size_t i;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
	{
		refdef_t rd = view(cases[i][0], cases[i][1], cases[i][2], cases[i][3]);
		int w = (cases[i][2] > 640) ? 1280 : 640;
		int h = (cases[i][2] > 640) ? 960 : 480;

		R_ClampViewToBuffer(&rd, w, h);

		T_EQ_INT(rd.x, cases[i][0]);
		T_EQ_INT(rd.y, cases[i][1]);
		T_EQ_INT(rd.width, cases[i][2]);
		T_EQ_INT(rd.height, cases[i][3]);
	}
}

TEST(sw_view_larger_than_the_buffer_is_cut_to_it)
{
	/* viddef 1280x960 (the window's pixels), buffer 640x480 */
	refdef_t rd = view(0, 0, 1280, 960);

	R_ClampViewToBuffer(&rd, 640, 480);

	T_CHECK(inside(&rd, 640, 480));
	T_EQ_INT(rd.x, 0);
	T_EQ_INT(rd.y, 0);
	T_EQ_INT(rd.width, 640);
	T_EQ_INT(rd.height, 480);

	/* a smaller view centred in the larger window */
	rd = view(128, 96, 1024, 768);
	R_ClampViewToBuffer(&rd, 640, 480);

	T_CHECK(inside(&rd, 640, 480));
	T_EQ_INT(rd.x, 128);
	T_EQ_INT(rd.y, 96);
	T_EQ_INT(rd.width, 512);
	T_EQ_INT(rd.height, 384);
}

TEST(sw_view_outside_the_buffer_ends_inside)
{
	static const int cases[][4] = {
		{1000, 800, 64, 64},	/* starts past the buffer */
		{-10, -10, 100, 100},	/* starts before it */
		{-5000, 5000, 0, -3},	/* nonsense */
		{639, 479, 640, 480},	/* only the last pixel inside */
	};
	size_t i;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
	{
		refdef_t rd = view(cases[i][0], cases[i][1], cases[i][2], cases[i][3]);

		R_ClampViewToBuffer(&rd, 640, 480);

		T_CHECK(inside(&rd, 640, 480));
	}
}
