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
 * The three pixel helpers of src/client/refresh/files/imgscale.c, which
 * the client's upscaler and all four renderers link as one copy: scale2x,
 * R_FloodFillSkin and R_Convert8to32. Each is fed a small fixed input and
 * its result is compared with an output worked out by hand below, so a
 * change in any one of them changes bytes that the renderers' textures
 * (and the upscaler's PNGs) are made of.
 *
 * =======================================================================
 */

#include <stdlib.h>
#include <string.h>

#include "test.h"
#include "../../src/client/refresh/files/imgscale.h"

/*
 * EPX on 1 2 / 2 1. Each pixel's output block is its own value with one
 * corner replaced where the rule for that corner fires:
 *   (0,0)=1: A=1 B=2 C=1 D=2: rule 1 gives 1, rule 4 gives D=2 -> 1 1 / 1 2
 *   (1,0)=2: A=2 B=2 C=1 D=1: rule 2 gives 2, rule 3 gives C=1 -> 2 2 / 1 2
 *   (0,1)=2: A=1 B=1 C=2 D=2: rule 2 gives B=1, rule 3 gives 2 -> 2 1 / 2 2
 *   (1,1)=1: A=2 B=1 C=2 D=1: rule 1 gives A=2, rule 4 gives 1 -> 2 1 / 1 1
 */
TEST(imgscale_scale2x_bytes)
{
	const byte src[4] = { 1, 2, 2, 1 };
	const byte want[16] = {
		1, 1, 2, 2,
		1, 2, 1, 2,
		2, 1, 2, 1,
		2, 2, 1, 1
	};
	byte got[16];

	memset(got, 0xEE, sizeof(got));
	scale2x(src, got, 2, 2);
	T_EQ_MEM(got, want, sizeof(want));
}

/* one pixel: all four outputs are the pixel, the edges standing in for the neighbours */
TEST(imgscale_scale2x_single)
{
	const byte src[1] = { 77 };
	const byte want[4] = { 77, 77, 77, 77 };
	byte got[4];

	memset(got, 0xEE, sizeof(got));
	scale2x(src, got, 1, 1);
	T_EQ_MEM(got, want, sizeof(want));
}

/*
 * 3 by 2, fill colour 5 (the first pixel), the opaque black is index 7.
 * The fill walks from (0,0), right before down:
 *   (0,0) sees 5 right and 5 below, none else: takes 7, the opaque black
 *   (1,0) sees 7 left, 9 right, 8 below, the last one seen wins: takes 8
 *   (0,1) sees 8 right, 7 above, the last one seen wins: takes 7
 */
TEST(imgscale_floodfill_bytes)
{
	byte skin[6] = {
		5, 5, 9,
		5, 8, 9
	};
	const byte want[6] = {
		7, 8, 9,
		7, 8, 9
	};
	unsigned table[256];

	memset(table, 0, sizeof(table));
	table[7] = 255;

	R_FloodFillSkin(skin, 3, 2, table);
	T_EQ_MEM(skin, want, sizeof(want));
}

/* a corner that is the opaque black, or transparent, is not filled */
TEST(imgscale_floodfill_refused)
{
	byte black[4] = { 7, 1, 2, 3 };
	const byte black_want[4] = { 7, 1, 2, 3 };
	byte clear[4] = { 255, 1, 2, 3 };
	const byte clear_want[4] = { 255, 1, 2, 3 };
	unsigned table[256];

	memset(table, 0, sizeof(table));
	table[7] = 255;

	R_FloodFillSkin(black, 2, 2, table);
	T_EQ_MEM(black, black_want, sizeof(black));
	R_FloodFillSkin(clear, 2, 2, table);
	T_EQ_MEM(clear, clear_want, sizeof(clear));
}

/* the colour of entry i, byte by byte: i, i ^ 0x55, 255 - i, alpha 200; entry 255 is 9 9 9 and alpha 0 */
static void
imgscale_table(unsigned *table)
{
	int i;

	for (i = 0; i < 256; i++)
	{
		byte *t = (byte *)&table[i];

		t[0] = (byte)i;
		t[1] = (byte)(i ^ 0x55);
		t[2] = (byte)(255 - i);
		t[3] = 200;
	}

	((byte *)&table[255])[0] = 9;
	((byte *)&table[255])[1] = 9;
	((byte *)&table[255])[2] = 9;
	((byte *)&table[255])[3] = 0;
}

/* the colour (rgb of entry p, alpha of entry a) of a texel, as bytes */
static void
imgscale_want(byte *out, int p, int a)
{
	unsigned table[256];

	imgscale_table(table);
	memcpy(out, &table[p], 3);
	out[3] = ((byte *)&table[a])[3];
}

/*
 * 3 by 3, 255 is transparent. A transparent texel takes the colour of the
 * first of above, below, left, right that is not 255, else index 0; its
 * alpha stays 0:
 *   i=1: above is outside, below (4) is 255, left (0) is 10 -> 10
 *   i=3: above is outside, below (6) is 40 -> 40
 *   i=4: above (1), below (7), left (3) are 255, right (5) is 30 -> 30
 *   i=7: above (4) is 255, below is outside, left (6) is 40 -> 40
 *   i=8: above (5) is 30 -> 30
 */
TEST(imgscale_convert8to32_bytes)
{
	const byte src[9] = {
		10, 255, 20,
		255, 255, 30,
		40, 255, 255
	};
	const int colour[9] = { 10, 10, 20, 40, 30, 30, 40, 40, 30 };
	unsigned table[256];
	unsigned *got;
	byte want[4];
	int i;

	imgscale_table(table);
	got = R_Convert8to32(src, 3, 3, table);
	T_REQUIRE(got != NULL);

	for (i = 0; i < 9; i++)
	{
		imgscale_want(want, colour[i], src[i]);
		T_EQ_MEM(&got[i], want, 4);
	}

	free(got);
}

/* nothing around is opaque: index 0's colour, alpha 0 */
TEST(imgscale_convert8to32_lone)
{
	const byte src[2] = { 255, 255 };
	unsigned table[256];
	unsigned *got;
	byte want[4];
	int i;

	imgscale_table(table);
	got = R_Convert8to32(src, 2, 1, table);
	T_REQUIRE(got != NULL);

	for (i = 0; i < 2; i++)
	{
		imgscale_want(want, 0, 255);
		T_EQ_MEM(&got[i], want, 4);
	}

	free(got);
}

/*
 * The last row's first texel (i = 6, the first one where "below" would
 * lie past the end) with a sentinel just after the image: above (3),
 * left (5) are 255, so the right neighbour (50) is taken, never the
 * sentinel. The others: 1 takes left (60), 3 takes left (60), 4 takes
 * below (50), 5 takes above (60).
 */
TEST(imgscale_convert8to32_last_row)
{
	const byte src[10] = {
		60, 255, 60,
		255, 255, 255,
		255, 50, 60,
		33
	};
	const int colour[9] = { 60, 60, 60, 60, 50, 60, 50, 50, 60 };
	unsigned table[256];
	unsigned *got;
	byte want[4];
	int i;

	imgscale_table(table);
	got = R_Convert8to32(src, 3, 3, table);
	T_REQUIRE(got != NULL);

	for (i = 0; i < 9; i++)
	{
		imgscale_want(want, colour[i], src[i]);
		T_EQ_MEM(&got[i], want, 4);
	}

	free(got);
}

/*
 * A fill that turns back to the left: 5 5 5 / 9 8 5 / 5 5 5 is walked
 * (0,0) (1,0) (2,0) (2,1) (2,2) (1,2) (0,2); a pixel takes the last of
 * left, right, above, below that is neither the fill colour nor already
 * visited. (0,2), the last one, sees 8 on its right and 9 above and takes
 * 9; every other pixel ends on 8, but the first, which sees only the 9
 * below.
 */
TEST(imgscale_floodfill_left_turn)
{
	byte skin[9] = {
		5, 5, 5,
		9, 8, 5,
		5, 5, 5
	};
	const byte want[9] = {
		9, 8, 8,
		9, 8, 8,
		9, 8, 8
	};
	unsigned table[256];

	memset(table, 0, sizeof(table));
	table[7] = 255;

	R_FloodFillSkin(skin, 3, 3, table);
	T_EQ_MEM(skin, want, sizeof(want));
}
