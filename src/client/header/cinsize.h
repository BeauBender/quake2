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

size_t SCR_StillBytes(int width, int height, int bpp);

#endif
