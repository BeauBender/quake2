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
#include <stdlib.h>
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

/* one lookup: is_sky, the PNG's size, the stock face's size */
static void
expect(int line, skyorder_t o, int first, int later, int aw, int ah)
{
	checks++;
	if (o.png_first != first || o.try_png_later != later
			|| o.asset_w != aw || o.asset_h != ah)
	{
		failed++;
		printf("skyorder_test: line %d: got %d %d %d %d, want %d %d %d %d\n",
			line, o.png_first, o.try_png_later, o.asset_w, o.asset_h,
			first, later, aw, ah);
	}
}

#define EXPECT(o, first, later, aw, ah) expect(__LINE__, (o), (first), (later), (aw), (ah))

static void
test_order(void)
{
	/* not a sky face: the order for every image, whatever the sizes */
	EXPECT(SkyOrder_Choose(0, 0, 0, 0, 0), 0, 1, 0, 0);
	EXPECT(SkyOrder_Choose(0, 512, 512, 256, 256), 0, 1, 0, 0);
	EXPECT(SkyOrder_Choose(0, 512, 512, 0, 0), 0, 1, 0, 0);

	/* a sky face with no PNG (or one that did not decode): no second try */
	EXPECT(SkyOrder_Choose(1, 0, 0, 256, 256), 0, 0, 0, 0);
	EXPECT(SkyOrder_Choose(1, 0, 512, 256, 256), 0, 0, 0, 0);
	EXPECT(SkyOrder_Choose(1, 512, 0, 256, 256), 0, 0, 0, 0);
	EXPECT(SkyOrder_Choose(1, -1, -1, 0, 0), 0, 0, 0, 0);
	EXPECT(SkyOrder_Choose(1, -1, 512, 0, 0), 0, 0, 0, 0);
	EXPECT(SkyOrder_Choose(1, 512, -1, 0, 0), 0, 0, 0, 0);
	EXPECT(SkyOrder_Choose(1, 0, 0, 0, 0), 0, 0, 0, 0);

	/* a smaller PNG, in either side: the .tga first, the PNG after it */
	EXPECT(SkyOrder_Choose(1, 128, 128, 256, 256), 0, 1, 0, 0);
	EXPECT(SkyOrder_Choose(1, 255, 256, 256, 256), 0, 1, 0, 0);
	EXPECT(SkyOrder_Choose(1, 256, 255, 256, 256), 0, 1, 0, 0);
	EXPECT(SkyOrder_Choose(1, 512, 128, 256, 256), 0, 1, 0, 0);
	EXPECT(SkyOrder_Choose(1, 128, 512, 256, 256), 0, 1, 0, 0);

	/* an equal PNG: first, at the stock size */
	EXPECT(SkyOrder_Choose(1, 256, 256, 256, 256), 1, 0, 256, 256);

	/* a larger PNG with a known stock size: first, the asset is the stock face */
	EXPECT(SkyOrder_Choose(1, 512, 512, 256, 256), 1, 0, 256, 256);
	EXPECT(SkyOrder_Choose(1, 1024, 1024, 256, 256), 1, 0, 256, 256);
	EXPECT(SkyOrder_Choose(1, 257, 256, 256, 256), 1, 0, 256, 256);
	EXPECT(SkyOrder_Choose(1, 256, 257, 256, 256), 1, 0, 256, 256);
	/* a stock face that is not square: each side is its own */
	EXPECT(SkyOrder_Choose(1, 600, 400, 300, 200), 1, 0, 300, 200);

	/* a stock face of one texel a side is a size */
	EXPECT(SkyOrder_Choose(1, 8, 8, 1, 1), 1, 0, 1, 1);
	EXPECT(SkyOrder_Choose(1, 8, 8, 1, 4), 1, 0, 1, 4);
	EXPECT(SkyOrder_Choose(1, 8, 8, 4, 1), 1, 0, 4, 1);

	/* the stock size is not known: any PNG, the asset size is left alone */
	EXPECT(SkyOrder_Choose(1, 64, 64, 0, 0), 1, 0, 0, 0);
	EXPECT(SkyOrder_Choose(1, 512, 512, 0, 0), 1, 0, 0, 0);
	EXPECT(SkyOrder_Choose(1, 1, 1, 0, 0), 1, 0, 0, 0);
	/* half a size is no size */
	EXPECT(SkyOrder_Choose(1, 512, 512, 256, 0), 1, 0, 0, 0);
	EXPECT(SkyOrder_Choose(1, 512, 512, 0, 256), 1, 0, 0, 0);
	EXPECT(SkyOrder_Choose(1, 8, 8, 256, 0), 1, 0, 0, 0);
	EXPECT(SkyOrder_Choose(1, 8, 8, 0, 256), 1, 0, 0, 0);
	EXPECT(SkyOrder_Choose(1, 512, 512, -1, -1), 1, 0, 0, 0);
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

	/* too short, or no data: a heap buffer of exactly the size passed */
	tga_header(h, 256, 256);
	{
		unsigned char *exact = malloc(17);

		CHECK(exact != NULL);

		if (exact != NULL)
		{
			memcpy(exact, h, 17);
			w = hh = 5;
			CHECK(SkyOrder_TgaSize(exact, 17, &w, &hh) == 0 && w == 0 && hh == 0);
			free(exact);
		}
	}

	{
		unsigned char *exact = malloc(18);

		CHECK(exact != NULL);

		if (exact != NULL)
		{
			memcpy(exact, h, 18);
			CHECK(SkyOrder_TgaSize(exact, 18, &w, &hh) == 1 && w == 256);
			free(exact);
		}
	}

	CHECK(SkyOrder_TgaSize(h, 0, &w, &hh) == 0);
	CHECK(SkyOrder_TgaSize(NULL, 18, &w, &hh) == 0);
}

/* the stock face's size: a known original, else the .tga's header, else none */
static void
stock(int line, int ow, int oh, int tw, int th, int ew, int eh)
{
	int w = 9, h = 9;

	SkyOrder_StockSize(ow, oh, tw, th, &w, &h);
	checks++;

	if (w != ew || h != eh)
	{
		failed++;
		printf("skyorder_test: line %d: stock size %dx%d, want %dx%d\n",
			line, w, h, ew, eh);
	}
}

#define STOCK(ow, oh, tw, th, ew, eh) stock(__LINE__, ow, oh, tw, th, ew, eh)

static void
test_stocksize(void)
{
	/* a known original decides, whatever the .tga is */
	STOCK(256, 256, 512, 512, 256, 256);
	STOCK(256, 256, 0, 0, 256, 256);
	STOCK(256, 128, 512, 512, 256, 128);
	STOCK(1, 1, 512, 512, 1, 1);
	/* no original: the .tga's header */
	STOCK(0, 0, 512, 512, 512, 512);
	STOCK(0, 0, 300, 200, 300, 200);
	/* half an original is none */
	STOCK(256, 0, 512, 512, 512, 512);
	STOCK(0, 256, 512, 512, 512, 512);
	STOCK(-1, -1, 512, 512, 512, 512);
	/* neither: not known, and half a .tga is none */
	STOCK(0, 0, 0, 0, 0, 0);
	STOCK(0, 0, 512, 0, 0, 0);
	STOCK(0, 0, 0, 512, 0, 0);
	STOCK(256, 0, 0, 512, 0, 0);

	CHECK(SkyOrder_SizeKnown(1, 1) == 1);
	CHECK(SkyOrder_SizeKnown(0, 1) == 0);
	CHECK(SkyOrder_SizeKnown(1, 0) == 0);
	CHECK(SkyOrder_SizeKnown(-1, 5) == 0);
	CHECK(SkyOrder_SizeKnown(5, -1) == 0);
}

int
main(void)
{
	test_order();
	test_stocksize();
	test_tgasize();
	printf("skyorder_test: %d checks, %d failed\n", checks, failed);
	return failed ? 1 : 0;
}
