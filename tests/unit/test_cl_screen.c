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
 * The HUD layout a server sends and the client draws (FIX13,
 * src/client/cl_screen.c):
 *
 * - the "num" and "pic" layout commands take a stat index, and "pic"
 *   takes the stat's value as an image number; both are checked before
 *   they index anything. The layout interpreter is static; the tests
 *   run it through SCR_UpdateScreen with the layout a server sets, the
 *   renderer stubs counting what would be drawn.
 * - DrawHUDStringScaled keeps one line of text in a buffer of its own;
 *   a longer line is drawn in pieces, never past the buffer. DrawHUDString
 *   is its public caller. The address sanitizer reports the overflow.
 *
 * =======================================================================
 */

#include <limits.h>

#include "test.h"
#include "../../src/client/header/client.h"
#include "stub_client.h"

#define STAT_LAYOUTS 13
#define PIC_STAT 20
#define PIC_VALUE 7

void DrawHUDString(char *string, int x, int y, int centerwidth, int xor);

/* A client that is in the game with a 640x480 screen and a layout. */
static void
show_layout(const char *layout, int statindex, int statvalue)
{
	stub_client_cvars();
	memset(&cl, 0, sizeof(cl));
	memset(&cls, 0, sizeof(cls));
	memset(&viddef, 0, sizeof(viddef));
	viddef.width = 640;
	viddef.height = 480;
	con.initialized = true;
	cls.state = ca_active;
	cl.refresh_prepped = true;

	cl.frame.playerstate.stats[STAT_LAYOUTS] = 1;
	Q_strlcpy(cl.layout, layout, sizeof(cl.layout));
	strcpy(cl.configstrings[CS_IMAGES + PIC_VALUE], "w_blaster");

	/* The names past both ends of the image table, so that a read
	   there would draw something. */
	strcpy(cl.configstrings[CS_IMAGES + MAX_IMAGES], "past_the_end");
	strcpy(cl.configstrings[CS_IMAGES - 1], "before_the_start");

	cl.frame.playerstate.stats[1] = 100;       /* health */
	cl.frame.playerstate.stats[3] = 25;        /* ammo */
	cl.frame.playerstate.stats[5] = 50;        /* armor */
	cl.frame.playerstate.stats[11] = 7;        /* flashes */
	cl.frame.playerstate.stats[21] = 40;       /* a configstring number */
	cl.frame.playerstate.stats[22] = SHRT_MAX; /* far past the configstrings */

	if (statindex >= 0 && statindex < MAX_STATS)
	{
		cl.frame.playerstate.stats[statindex] = statvalue;
	}

	memset(&stub_calls, 0, sizeof(stub_calls));
	SCR_UpdateScreen();
}

/* "num" draws a stat; the first and last stat are both fine. */
TEST(hud_num_stat_index_inside_range)
{
	show_layout("num 3 0", 0, 42);
	T_EQ_INT(stub_calls.pics, 2); /* "42" */

	show_layout("num 3 31", MAX_STATS - 1, 7);
	T_EQ_INT(stub_calls.pics, 1);
}

/* One past the last stat, and indexes far outside: nothing is read or drawn. */
TEST(hud_num_stat_index_outside_range)
{
	static const char *layouts[] = {
		"num 3 32",
		"num 3 -1",
		"num 3 100000",
		"num 3 2147483647",
		"num 3 -2147483648",
	};
	size_t i;

	for (i = 0; i < sizeof(layouts) / sizeof(layouts[0]); i++)
	{
		show_layout(layouts[i], -1, 0);
		T_EQ_INT(stub_calls.pics, 0);
	}
}

/* "pic" draws the image a stat names; the stat index and value are checked. */
TEST(hud_pic_inside_range)
{
	show_layout("pic 20", PIC_STAT, PIC_VALUE);
	T_EQ_INT(stub_calls.pics, 1);

	/* The last stat. */
	show_layout("pic 31", MAX_STATS - 1, PIC_VALUE);
	T_EQ_INT(stub_calls.pics, 1);
}

TEST(hud_pic_stat_index_outside_range)
{
	static const char *layouts[] = {
		"pic 32", "pic -1", "pic 100000", "pic 2147483647", "pic -2147483648",
	};
	size_t i;

	for (i = 0; i < sizeof(layouts) / sizeof(layouts[0]); i++)
	{
		show_layout(layouts[i], -1, 0);
		T_EQ_INT(stub_calls.pics, 0);
	}
}

/* An image number just past either end of the table is not looked up. */
TEST(hud_pic_value_outside_range)
{
	static const int values[] = {MAX_IMAGES, -1, MAX_IMAGES + 1, SHRT_MAX, SHRT_MIN};
	size_t i;

	for (i = 0; i < sizeof(values) / sizeof(values[0]); i++)
	{
		show_layout("pic 20", PIC_STAT, values[i]);
		T_EQ_INT(stub_calls.pics, 0);
	}

	/* The last image of the table itself is inside it. */
	show_layout("pic 20", PIC_STAT, MAX_IMAGES - 1);
	T_EQ_INT(stub_calls.pics, 0); /* its name is empty: nothing to draw */
}

/* A string of the given length on one line: every character is drawn. */
static int
chars_drawn_for(size_t len)
{
	char *s = malloc(len + 1);
	int n;

	T_REQUIRE(s != NULL);
	memset(s, 'a', len);
	s[len] = '\0';
	memset(&stub_calls, 0, sizeof(stub_calls));
	DrawHUDString(s, 0, 0, 0, 0);
	n = stub_calls.chars;
	free(s);
	return n;
}

/* Lines up to the buffer size minus its terminator are drawn whole. */
TEST(hud_string_line_inside_buffer)
{
	T_EQ_INT(chars_drawn_for(0), 0);
	T_EQ_INT(chars_drawn_for(1), 1);
	T_EQ_INT(chars_drawn_for(1022), 1022);
	T_EQ_INT(chars_drawn_for(1023), 1023);
}

/* A line one character longer, and much longer, is drawn in pieces. The
   drawing code skips one character at each break (it takes it for the
   newline), which is how it always behaved and not what is tested here:
   the line is cut at the buffer, most of it is drawn, never more than
   there is. */
TEST(hud_string_line_past_buffer)
{
	static const size_t lens[] = {1024, 1025, 2046, 2047, 100000};
	size_t i;

	for (i = 0; i < sizeof(lens) / sizeof(lens[0]); i++)
	{
		int n = chars_drawn_for(lens[i]);

		T_CHECK(n >= 1023);
		T_CHECK((size_t)n <= lens[i]);
		T_CHECK((size_t)n >= lens[i] - lens[i] / 1023 - 1);
	}
}

/* Every layout command with well-formed arguments, and some with arguments
   past the end of a table: nothing is drawn past anything (the sanitizers
   watch) and the well-formed ones draw what they ask for. */
TEST(hud_layout_every_command)
{
	show_layout("xl 4 xr -4 xv 8 yt 4 yb -4 yv 8 "
		"picn net pic 20 num 3 1 hnum anum rnum "
		"client 0 0 0 12 34 56 client 0 0 300 1 2 3 "
		"ctf 0 0 0 5 1500 ctf 0 0 -1 5 7 "
		"stat_string 21 stat_string 32 stat_string 22 "
		"string hello string2 world cstring mid cstring2 mid2 "
		"if 20 string shown endif if 23 string hidden endif if 99 num 3 2 endif "
		"bogus", PIC_STAT, PIC_VALUE);

	/* A pic from the stat, one from its name, digits, a client block. */
	T_CHECK(stub_calls.pics >= 5);
	T_CHECK(stub_calls.chars >= 6);
}
