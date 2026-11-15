/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 * Copyright (C) 2015 Daniel Gibson
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * =======================================================================
 *
 * scale2x, R_FloodFillSkin and R_Convert8to32, defined once in
 * imgscale.c for the renderers and the client. Free of renderer state.
 *
 * =======================================================================
 */

#ifndef SRC_CLIENT_REFRESH_FILES_IMGSCALE_H_
#define SRC_CLIENT_REFRESH_FILES_IMGSCALE_H_

#include "../../../common/header/common.h"

extern void scale2x(const byte *src, byte *dst, int width, int height);
extern void R_FloodFillSkin(byte *skin, int skinwidth, int skinheight, const unsigned *table_8to24);
extern unsigned *R_Convert8to32(const byte *data, size_t width, size_t height, const unsigned *table_8to24);

#endif /* SRC_CLIENT_REFRESH_FILES_IMGSCALE_H_ */
