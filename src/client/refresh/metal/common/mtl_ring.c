/*
 * Copyright (C) 1997-2001 Id Software, Inc.
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
 * The ring allocator (mtl_ring.h), moved verbatim from the shim's
 * mgl_raster.c.
 *
 * =======================================================================
 */

#include "mtl_ring.h"

/* from mgl_raster.c, mgl_ring_alloc */
long
mtl_ring_alloc(mtl_ring_t *r, size_t n)
{
	size_t at = (r->used + MTL_RING_ALIGN - 1) / MTL_RING_ALIGN * MTL_RING_ALIGN;

	/* at > size is checked first, so size - at cannot wrap */
	if (n == 0 || at > r->size || n > r->size - at)
	{
		return -1;
	}

	r->used = at + n;
	return (long)at;
}
