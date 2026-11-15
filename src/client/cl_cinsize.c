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
 * The byte size of a cinematic still picture, pure arithmetic so that the
 * unit tests run it (cl_cin.c, which allocates the buffer, is not linked
 * there).
 *
 * =======================================================================
 */

#include <stddef.h>

#include "header/client.h"
#include "header/cinsize.h"

/*
 * The bytes of a still of width by height pixels at bpp bits per pixel,
 * computed in size_t, or 0 when the size is not usable: a side that is
 * not positive, a depth that is not a positive multiple of 8 up to 32,
 * or a product that does not fit in size_t.
 */
size_t
SCR_StillBytes(int width, int height, int bpp)
{
	size_t w, h, bytes;

	if ((width <= 0) || (height <= 0) || (bpp <= 0) || (bpp > 32) ||
		((bpp % 8) != 0))
	{
		return 0;
	}

	w = (size_t)width;
	h = (size_t)height;
	bytes = (size_t)(bpp / 8);

	if (w > SIZE_MAX / h)
	{
		return 0;
	}

	if ((w * h) > SIZE_MAX / bytes)
	{
		return 0;
	}

	return w * h * bytes;
}
