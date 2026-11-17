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
 * The byte size of a cinematic still picture (cl_cinsize.c).
 *
 * =======================================================================
 */

#ifndef CL_CINSIZE_H
#define CL_CINSIZE_H

#include <stddef.h>

/* the verdicts of SCR_StillCheck */
enum
{
	SCR_STILL_OK = 0,
	SCR_STILL_BADFORMAT,	/* a side or the depth is not usable */
	SCR_STILL_TOOLARGE		/* the size is 0 or above SCR_STILL_MAX_BYTES */
};

size_t SCR_StillBytes(int width, int height, int bpp);
int SCR_StillCheck(int width, int height, int bpp, size_t *bytes);
int SCR_StillSizeOk(size_t bytes);
int SCR_StillStem(const char *name, const char *dot, char *out, size_t outsize);

#endif
