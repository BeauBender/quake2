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
 * The lightmap blocks of gl1_lightmap.c as the slices of one array
 * texture (docs/plans/ref-metal-native.md, section 5): gl1's block
 * allocator places each lit surface's rectangle in the current slice or
 * the next (rm_lm_place, the slice count bounded there), R_BuildLightMap
 * fills it in a CPU copy of the slice, and the whole array is uploaded
 * once the world's buffers exist. The CPU copies stay for the frames'
 * lightstyle updates (rm_surf.c), which rebuild a rectangle and upload
 * the slice's joined dirty region. gl1's slice numbers start at 1 (its
 * block 0 is the classic path's dynamic block); here they start at 0.
 *
 * =======================================================================
 */

#include "header/local.h"

static int lm_allocated[RM_LM_WIDTH];
static int lm_slice;                              /* the slice being filled */
static byte *lm_buffers[RM_LM_MAX_SLICES];        /* the CPU copy of each slice, rgba8 rows */
static int lm_building;

/* from gl1_lightmap.c */
void
LM_FreeLightmapBuffers(void)
{
	int i;

	for (i = 0; i < RM_LM_MAX_SLICES; i++)
	{
		free(lm_buffers[i]);
		lm_buffers[i] = NULL;
	}

	lm_slice = 0;
	lm_building = 0;
}

/* the CPU copy of a slice, cleared (gl1 leaves it as malloc gives it; no texel outside a rectangle is sampled) */
static qboolean
LM_AllocLightmapBuffer(int slice)
{
	if ((slice < 0) || (slice >= RM_LM_MAX_SLICES))
	{
		return false;
	}

	if (!lm_buffers[slice])
	{
		lm_buffers[slice] = calloc(1, RM_LM_WIDTH * RM_LM_HEIGHT * RM_LM_BYTES);
	}

	return lm_buffers[slice] != NULL;
}

byte *
LM_SliceBuffer(int slice)
{
	return ((slice >= 0) && (slice < RM_LM_MAX_SLICES)) ? lm_buffers[slice] : NULL;
}

/* from gl1_lightmap.c: the base lightstyles, so the lightmaps are not regenerated the first time they are seen */
qboolean
LM_BeginBuildingLightmaps(void)
{
	static lightstyle_t lightstyles[MAX_LIGHTSTYLES];
	int i;

	LM_FreeLightmapBuffers();
	memset(lm_allocated, 0, sizeof(lm_allocated));

	r_framecount = 1; /* no dlightcache */

	for (i = 0; i < MAX_LIGHTSTYLES; i++)
	{
		lightstyles[i].rgb[0] = 1;
		lightstyles[i].rgb[1] = 1;
		lightstyles[i].rgb[2] = 1;
		lightstyles[i].white = 3;
	}

	r_newrefdef.lightstyles = lightstyles;
	lm_building = 1;
	return LM_AllocLightmapBuffer(0);
}

/*
 * from gl1_lightmap.c: places the lightmap of a lit surface whose size
 * the loader checked (rm_lm_size) and builds it. false when the map
 * needs more slices than the array may have or memory runs out; the
 * caller refuses the map.
 */
qboolean
LM_CreateSurfaceLightmap(msurface_t *surf)
{
	int smax, tmax, placed;
	byte *base;

	if (surf->flags & (SURF_DRAWSKY | SURF_DRAWTURB))
	{
		return true;
	}

	if (!lm_building || !rm_lm_size(surf->extents[0], surf->extents[1], &smax, &tmax))
	{
		return false;
	}

	placed = rm_lm_place(lm_allocated, &lm_slice, smax, tmax, &surf->light_s, &surf->light_t);

	if (!placed || !LM_AllocLightmapBuffer(lm_slice) ||
		!rm_lm_region_ok(lm_slice, lm_slice + 1, surf->light_s, surf->light_t, smax, tmax))
	{
		return false;
	}

	surf->lightmaptexturenum = lm_slice;

	base = lm_buffers[lm_slice];
	base += (surf->light_t * RM_LM_WIDTH + surf->light_s) * RM_LM_BYTES;

	R_SetCacheState(surf);
	return R_BuildLightMap(surf, base, RM_LM_WIDTH * RM_LM_BYTES);
}

/* The slices the map filled, 1 to RM_LM_MAX_SLICES (gl1's LM_EndBuildingLightmaps uploads the last one). */
int
LM_EndBuildingLightmaps(void)
{
	lm_building = 0;
	return lm_slice + 1;
}

/*
 * The array texture of the world: slices slices, each written whole from
 * its CPU copy. 0 when the count is refused (rm_lm_slices_ok) or the
 * texture cannot be made or written; nothing is kept then.
 */
int
LM_UploadSlices(int slices)
{
	int tex, i;

	if (!rm_lm_slices_ok(slices) || !(tex = rmb_texture_array(slices)))
	{
		return 0;
	}

	for (i = 0; i < slices; i++)
	{
		if (!lm_buffers[i] ||
			!rmb_texture_region(tex, i, 0, 0, RM_LM_WIDTH, RM_LM_HEIGHT, lm_buffers[i], RM_LM_WIDTH * RM_LM_BYTES))
		{
			rmb_texture_free(tex);
			return 0;
		}
	}

	return tex;
}
