/*
 * Copyright (C) 2026 Yamagi Quake II contributors
 *
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
 * =======================================================================
 *
 * The size decisions of the image loaders (pcx.c, wal.c, image.c and the
 * software renderer's sw_image.c), as pure functions with no state, so
 * that the unit suite reaches each one. Every product is computed in 64
 * bits and compared with a cap before any `int` or allocation sees it
 * (the pattern R_Convert8to32 uses).
 *
 * The cap, IMGSIZE_MAX_PIXELS, is 1 << 26 pixels: the same area the
 * ImageIO decoder (IMGDEC_MAX_AREA) accepts, so a file of any type is
 * held to one limit. At four bytes a pixel that is 256 MB, far above any
 * stock picture (the largest is 640 by 480) and any texture pack, and
 * far below the point where a product reaches 2^31.
 *
 * =======================================================================
 */

#ifndef SRC_CLIENT_REFRESH_FILES_IMGSIZE_H_
#define SRC_CLIENT_REFRESH_FILES_IMGSIZE_H_

#include <stddef.h>
#include <stdint.h>

#define IMGSIZE_MAX_PIXELS ((int64_t)1 << 26)

/*
 * Both sides at least 1 and width * height at most IMGSIZE_MAX_PIXELS:
 * returns 1 and the pixel count, or 0 and leaves *pixels alone.
 */
static inline int
IMGSIZE_Pixels(int width, int height, size_t *pixels)
{
	int64_t area;

	if (width <= 0 || height <= 0)
	{
		return 0;
	}

	area = (int64_t)width * (int64_t)height;

	if (area > IMGSIZE_MAX_PIXELS)
	{
		return 0;
	}

	*pixels = (size_t)area;
	return 1;
}

/* The same, in bytes at `bytes_per_pixel` each (1 to 4). */
static inline int
IMGSIZE_Bytes(int width, int height, int bytes_per_pixel, size_t *bytes)
{
	size_t pixels;

	if (bytes_per_pixel < 1 || bytes_per_pixel > 4
		|| !IMGSIZE_Pixels(width, height, &pixels))
	{
		return 0;
	}

	*bytes = pixels * (size_t)bytes_per_pixel;
	return 1;
}

/* A dimension pair a Get*Info function may report: both sides positive. */
static inline int
IMGSIZE_DimsPositive(int width, int height)
{
	return width > 0 && height > 0;
}

/*
 * A packed PCX row (1, 2 or 4 bits a pixel) of `width` pixels is read up
 * to byte (width * bits + 7) / 8 - 1 of a `bytes_per_line` row: it fits
 * when bytes_per_line is at least (width * bits + 7) / 8.
 */
static inline int
IMGSIZE_PCXRowFits(int bits_per_pixel, int width, int bytes_per_line)
{
	int64_t need;

	if (bits_per_pixel < 1 || bits_per_pixel > 4 || width <= 0
		|| bytes_per_line <= 0)
	{
		return 0;
	}

	need = ((int64_t)width * bits_per_pixel + 7) / 8;
	return bytes_per_line >= need;
}

/*
 * The body of a mip-mapped texture file: `size` bytes loaded, the first
 * mip at `ofs`, `width` by `height` at `bytes_per_pixel` each. The body
 * (size - ofs) must hold width * height * bytes_per_pixel bytes. The
 * dimensions are held to the cap. An offset past the end gives a
 * negative body, which no size fits.
 */
static inline int
IMGSIZE_BodyFits(int64_t size, int64_t ofs, int width, int height,
	int bytes_per_pixel)
{
	size_t bytes;

	if (ofs <= 0 || !IMGSIZE_Bytes(width, height, bytes_per_pixel, &bytes))
	{
		return 0;
	}

	return (size - ofs) >= (int64_t)bytes;
}

#endif /* SRC_CLIENT_REFRESH_FILES_IMGSIZE_H_ */
