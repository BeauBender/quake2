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
 * The order in which a sky face's replacements are tried (files/image.c):
 * the decision and the size of the stock face, as pure functions that
 * need no engine header, so tests/image/skyorder_test.c tests them alone.
 *
 * =======================================================================
 */

#ifndef SRC_CLIENT_REFRESH_FILES_SKYORDER_H_
#define SRC_CLIENT_REFRESH_FILES_SKYORDER_H_

#include <stddef.h>

/* The largest side of a stock face that is believed (imgdecode.h's bound). */
#define SKYORDER_MAX_SIDE 16384

/*
 * The side of a face's TGA from its 18 header bytes: 1 and *width and
 * *height set when size holds the header and both sides are in
 * 1..SKYORDER_MAX_SIDE, else 0 and both 0. Reads only the header.
 */
static inline int
SkyOrder_TgaSize(const unsigned char *data, size_t size, int *width, int *height)
{
	int w, h;

	*width = 0;
	*height = 0;

	if (data == NULL || size < 18)
	{
		return 0;
	}

	w = data[12] | (data[13] << 8);
	h = data[14] | (data[15] << 8);

	if (w < 1 || w > SKYORDER_MAX_SIDE || h < 1 || h > SKYORDER_MAX_SIDE)
	{
		return 0;
	}

	*width = w;
	*height = h;
	return 1;
}

/*
 * 1 when a sky face's decoded PNG is tried before the stock .tga: only
 * with retexturing, for a sky face, when a PNG decoded (png_w and png_h
 * above 0) and it is at least the stock face's size. stock_w and stock_h
 * are 0 when the stock face's size is not known (no .tga, or one with
 * no readable header), which no PNG is smaller than. Else 0, and the
 * order is the one for every other image: .tga, .png, .jpg.
 */
static inline int
SkyOrder_PngFirst(int retexturing, int is_sky, int png_w, int png_h,
		int stock_w, int stock_h)
{
	if (!retexturing || !is_sky)
	{
		return 0;
	}

	if (png_w < 1 || png_h < 1)
	{
		return 0;
	}

	return png_w >= stock_w && png_h >= stock_h;
}

#endif /* SRC_CLIENT_REFRESH_FILES_SKYORDER_H_ */
