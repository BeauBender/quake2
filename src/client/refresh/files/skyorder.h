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
 * the decision, its asset size and the size of the stock face, as pure functions that
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

/* 1 when both sides are above 0: a size is known only then. */
static inline int
SkyOrder_SizeKnown(int w, int h)
{
	return w >= 1 && h >= 1;
}

/*
 * The size of the stock face. orig_w, orig_h: the size of the original
 * the lookup was made for (the .pcx, .wal, .m8 or .m32), 0 and 0 for a
 * .tga-named lookup or an original whose size could not be read. tga_w,
 * tga_h: the size from the header of the .tga of that name, 0 and 0 when
 * there is none or it cannot be read. A known original decides, as it
 * did before the PNG came first: the software renderer with paletted
 * textures draws a sky face 256 texels a row, whatever a third-party
 * .tga is. Else the .tga's size, else 0 and 0 (not known).
 */
static inline void
SkyOrder_StockSize(int orig_w, int orig_h, int tga_w, int tga_h, int *w, int *h)
{
	if (SkyOrder_SizeKnown(orig_w, orig_h))
	{
		*w = orig_w;
		*h = orig_h;
	}
	else if (SkyOrder_SizeKnown(tga_w, tga_h))
	{
		*w = tga_w;
		*h = tga_h;
	}
	else
	{
		*w = 0;
		*h = 0;
	}
}

/*
 * What SkyOrder_Choose decides for one lookup.
 *
 * png_first: use the PNG that was decoded, before the .tga.
 * try_png_later: when the .tga, tried next, fails, the chain may look
 *   for the .png again. 0 when the PNG was already tried and failed (or
 *   is absent), so a decode error is printed at most once and the file
 *   is not decoded a second time in the lookup.
 * asset_w, asset_h: the size to hand to load_image as the asset's own
 *   size when png_first (the stock face's, so that the software renderer,
 *   which draws a sky face as 256 texels a row, resamples the face to it
 *   as it does a retextured wall; GL and Metal upload the PNG at its own
 *   size). 0 and 0: leave the size the caller already has.
 */
typedef struct
{
	int png_first;
	int try_png_later;
	int asset_w;
	int asset_h;
} skyorder_t;

/*
 * The order in which a sky face's replacements are tried. is_sky: the
 * image type is a sky face (retexturing is on whenever this is asked).
 * png_w, png_h: the size of the PNG, or 0 when there is none or it did
 * not decode. stock_w, stock_h: the size of the stock face, or 0 when
 * not known (no .tga, or one with no readable header); a size is known
 * only when both sides are above 0, and no PNG is smaller than an
 * unknown size.
 *
 * For a sky face with a PNG at least the stock face's size in both
 * sides, the PNG comes first. Every other case keeps the order for
 * every other image: .tga, .png, .jpg.
 */
static inline skyorder_t
SkyOrder_Choose(int is_sky, int png_w, int png_h, int stock_w, int stock_h)
{
	skyorder_t o = { 0, 1, 0, 0 };
	int have_png = png_w >= 1 && png_h >= 1;
	int known = stock_w >= 1 && stock_h >= 1;

	if (!is_sky)
	{
		return o;
	}

	/* the PNG is tried here: later only when it decoded and lost */
	o.try_png_later = have_png;

	if (!have_png)
	{
		return o;
	}

	if (known && (png_w < stock_w || png_h < stock_h))
	{
		return o;
	}

	o.png_first = 1;
	o.try_png_later = 0;

	if (known)
	{
		o.asset_w = stock_w;
		o.asset_h = stock_h;
	}

	return o;
}

#endif /* SRC_CLIENT_REFRESH_FILES_SKYORDER_H_ */
