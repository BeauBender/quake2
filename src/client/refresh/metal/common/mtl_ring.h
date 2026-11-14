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
 * The bump allocator of a per-frame ring, shared by ref_metal (its frame
 * ring and its staging ring) and the shim (its vertex ring), moved here
 * from the shim's mgl_raster.c (mgl_ring_alloc) by TASK85
 * (docs/plans/ref-metal-native.md, section 8). Regions start at
 * MTL_RING_ALIGN. mtl_ring_alloc answers the offset of n bytes, or -1
 * when the rest of the ring cannot hold them; on -1 the ring is as it
 * was (used does not move).
 *
 * =======================================================================
 */

#ifndef MTL_RING_H
#define MTL_RING_H

#include <stddef.h>

#define MTL_RING_ALIGN 256

typedef struct
{
	size_t size, used;
} mtl_ring_t;

long mtl_ring_alloc(mtl_ring_t *r, size_t n);

#endif /* MTL_RING_H */
