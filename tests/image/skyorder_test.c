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
 * Host test of the sky face order (src/client/refresh/files/skyorder.h),
 * run by tests/check-image-decoder.sh. No engine, no game data.
 *
 * =======================================================================
 */

#include <stdio.h>
#include <string.h>

#include "../../src/client/refresh/files/skyorder.h"

static int checks, failed;

#define CHECK(cond) \
	do { \
		checks++; \
		if (!(cond)) \
		{ \
			failed++; \
			printf("skyorder_test: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		} \
	} while (0)

static void
test_order(void)
{
	/* retexturing off, a sky, a larger PNG: the old order */
	CHECK(SkyOrder_PngFirst(0, 1, 512, 512, 256, 256) == 0);
	/* not a sky face */
	CHECK(SkyOrder_PngFirst(1, 0, 512, 512, 256, 256) == 0);
	/* no PNG decoded */
	CHECK(SkyOrder_PngFirst(1, 1, 0, 0, 256, 256) == 0);
	CHECK(SkyOrder_PngFirst(1, 1, 0, 512, 256, 256) == 0);
	CHECK(SkyOrder_PngFirst(1, 1, 512, 0, 256, 256) == 0);
	CHECK(SkyOrder_PngFirst(1, 1, -1, -1, 0, 0) == 0);
	CHECK(SkyOrder_PngFirst(1, 1, -1, 512, 0, 0) == 0);
	CHECK(SkyOrder_PngFirst(1, 1, 512, -1, 0, 0) == 0);
	/* no PNG decoded, and no stock size either: still no PNG first */
	CHECK(SkyOrder_PngFirst(1, 1, 0, 0, 0, 0) == 0);
	CHECK(SkyOrder_PngFirst(1, 1, 0, 512, 0, 0) == 0);
	CHECK(SkyOrder_PngFirst(1, 1, 512, 0, 0, 0) == 0);
	CHECK(SkyOrder_PngFirst(1, 1, 1, 1, 0, 0) == 1);
	/* smaller than the face, in either side */
	CHECK(SkyOrder_PngFirst(1, 1, 128, 128, 256, 256) == 0);
	CHECK(SkyOrder_PngFirst(1, 1, 255, 256, 256, 256) == 0);
	CHECK(SkyOrder_PngFirst(1, 1, 256, 255, 256, 256) == 0);
	CHECK(SkyOrder_PngFirst(1, 1, 512, 128, 256, 256) == 0);
	CHECK(SkyOrder_PngFirst(1, 1, 128, 512, 256, 256) == 0);
	/* equal to the face */
	CHECK(SkyOrder_PngFirst(1, 1, 256, 256, 256, 256) == 1);
	/* larger */
	CHECK(SkyOrder_PngFirst(1, 1, 512, 512, 256, 256) == 1);
	CHECK(SkyOrder_PngFirst(1, 1, 1024, 1024, 256, 256) == 1);
	CHECK(SkyOrder_PngFirst(1, 1, 257, 256, 256, 256) == 1);
	CHECK(SkyOrder_PngFirst(1, 1, 256, 257, 256, 256) == 1);
	/* the .tga is missing: its size is not known, any PNG is used */
	CHECK(SkyOrder_PngFirst(1, 1, 64, 64, 0, 0) == 1);
	CHECK(SkyOrder_PngFirst(1, 1, 512, 512, 0, 0) == 1);
}

static void
tga_header(unsigned char *h, int w, int hh)
{
	memset(h, 0, 18);
	h[2] = 2;
	h[12] = (unsigned char)(w & 255);
	h[13] = (unsigned char)(w >> 8);
	h[14] = (unsigned char)(hh & 255);
	h[15] = (unsigned char)(hh >> 8);
}

static void
test_tgasize(void)
{
	unsigned char h[18];
	int w = 7, hh = 7;

	tga_header(h, 256, 256);
	CHECK(SkyOrder_TgaSize(h, 18, &w, &hh) == 1 && w == 256 && hh == 256);

	/* the two bytes of each side, and the two sides apart */
	tga_header(h, 300, 513);
	CHECK(SkyOrder_TgaSize(h, 18, &w, &hh) == 1 && w == 300 && hh == 513);

	/* the bound: 16384 is kept, 16385 and a 65535 side are not */
	tga_header(h, SKYORDER_MAX_SIDE, SKYORDER_MAX_SIDE);
	CHECK(SkyOrder_TgaSize(h, 18, &w, &hh) == 1 && w == 16384 && hh == 16384);
	tga_header(h, SKYORDER_MAX_SIDE + 1, 256);
	CHECK(SkyOrder_TgaSize(h, 18, &w, &hh) == 0 && w == 0 && hh == 0);
	tga_header(h, 256, SKYORDER_MAX_SIDE + 1);
	CHECK(SkyOrder_TgaSize(h, 18, &w, &hh) == 0 && w == 0 && hh == 0);
	tga_header(h, 65535, 256);
	CHECK(SkyOrder_TgaSize(h, 18, &w, &hh) == 0);

	/* a zero side */
	tga_header(h, 0, 256);
	CHECK(SkyOrder_TgaSize(h, 18, &w, &hh) == 0 && w == 0 && hh == 0);
	tga_header(h, 256, 0);
	CHECK(SkyOrder_TgaSize(h, 18, &w, &hh) == 0);
	tga_header(h, 1, 1);
	CHECK(SkyOrder_TgaSize(h, 18, &w, &hh) == 1 && w == 1 && hh == 1);

	/* too short, or no data */
	tga_header(h, 256, 256);
	w = hh = 5;
	CHECK(SkyOrder_TgaSize(h, 17, &w, &hh) == 0 && w == 0 && hh == 0);
	CHECK(SkyOrder_TgaSize(h, 0, &w, &hh) == 0);
	CHECK(SkyOrder_TgaSize(NULL, 18, &w, &hh) == 0);
}

int
main(void)
{
	test_order();
	test_tgasize();
	printf("skyorder_test: %d checks, %d failed\n", checks, failed);
	return failed ? 1 : 0;
}
