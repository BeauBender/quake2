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
#include <string.h>

#include "header/client.h"
#include "header/cinsize.h"
#include "refresh/files/imgdecode.h"

/*
 * The largest still the client allocates: the decoder's bound on the
 * pixels at 4 bytes each. A larger one is refused before the allocation.
 */
#define SCR_STILL_MAX_BYTES (IMGDEC_MAX_AREA * 4)

/* True when both sides are positive and the depth is 8, 16, 24 or 32. */
static int
SCR_StillFormatOk(int width, int height, int bpp)
{
	return !((width <= 0) || (height <= 0) || (bpp <= 0) || (bpp > 32) ||
		((bpp % 8) != 0));
}

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

	if (!SCR_StillFormatOk(width, height, bpp))
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

/*
 * True when a size from SCR_StillBytes may be allocated: not 0 (which
 * SCR_StillBytes returns for an unusable size) and not above
 * SCR_STILL_MAX_BYTES. A failure below that limit is the zone
 * allocator's fatal error, as for any other allocation.
 */
int
SCR_StillSizeOk(size_t bytes)
{
	return (bytes != 0) && (bytes <= SCR_STILL_MAX_BYTES);
}

/*
 * Sorts a still of width by height pixels at bpp bits into usable,
 * badly formed (a side or the depth) and too large, and stores its
 * size in *bytes when it is usable.
 */
int
SCR_StillCheck(int width, int height, int bpp, size_t *bytes)
{
	size_t size;

	if (!SCR_StillFormatOk(width, height, bpp))
	{
		return SCR_STILL_BADFORMAT;
	}

	size = SCR_StillBytes(width, height, bpp);

	if (!SCR_StillSizeOk(size))
	{
		return SCR_STILL_TOOLARGE;
	}

	*bytes = size;
	return SCR_STILL_OK;
}

/*
 * Writes the name without its extension (dot points at the extension
 * inside name, which runs to the end of the name) to out, a buffer of
 * outsize bytes. Refuses, and returns false with out untouched, when the
 * extension is longer than the name or the stem and its terminator do not
 * fit: a name that long is not a picture the client could have loaded, so
 * it is refused rather than cut to a different name.
 */
int
SCR_StillStem(const char *name, const char *dot, char *out, size_t outsize)
{
	size_t namelen = strlen(name);
	size_t extlen = strlen(dot);
	size_t stemlen;

	if ((extlen > namelen) || (outsize == 0))
	{
		return 0;
	}

	stemlen = namelen - extlen;

	if (stemlen >= outsize)
	{
		return 0;
	}

	memcpy(out, name, stemlen);
	out[stemlen] = '\0';
	return 1;
}
