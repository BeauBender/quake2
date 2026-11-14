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
 * The scrap, from gl1_scrap.c and gl1_draw.c (Scrap_Update): all the
 * little status bar pics in a few 256 by 256 textures, so that the
 * batched 2D quads share one texture. The allocator and the copy are
 * plain functions in rm_pure.c (host-tested); this file holds the texels,
 * the dirty flags and the textures. Each atlas is one back end texture,
 * made when the first block is placed in it and rewritten, through the
 * upload command buffer, when a draw finds it dirty.
 *
 * =======================================================================
 */

#include "header/local.h"

static int scrap_allocated[RM_MAX_SCRAPS][RM_SCRAP_WIDTH];
static unsigned scrap_texels[RM_MAX_SCRAPS][RM_SCRAP_WIDTH * RM_SCRAP_HEIGHT];
static qboolean scrap_dirty[RM_MAX_SCRAPS];
static int scrap_tex[RM_MAX_SCRAPS];

/* the allocation tables and the texels start empty (transparent); the textures are the images' to free */
void
Scrap_Init(void)
{
	memset(scrap_allocated, 0, sizeof(scrap_allocated));
	memset(scrap_dirty, 0, sizeof(scrap_dirty));
	memset(scrap_tex, 0, sizeof(scrap_tex));
	memset(scrap_texels, 0, sizeof(scrap_texels));
}

void
Scrap_Shutdown(void)
{
	int i;

	for (i = 0; i < RM_MAX_SCRAPS; i++)
	{
		rmb_texture_free(scrap_tex[i]);
		scrap_tex[i] = 0;
	}
}

/*
 * from gl1_scrap.c, Scrap_AllocBlock: returns the atlas index and the
 * position inside it, -1 when no atlas from scrap_offset on has room (or
 * the block is larger than an atlas, or the atlas texture cannot be made).
 * An atlas whose texture cannot be made takes no block.
 */
int
Scrap_AllocBlock(unsigned w, unsigned h, int *x, int *y, const unsigned *pic, int scrap_offset)
{
	int texnum;

	/* the empty border on all sides; the bounds first, so the additions cannot overflow */
	if ((w >= RM_SCRAP_WIDTH) || (h >= RM_SCRAP_HEIGHT) || !pic)
	{
		return -1;
	}

	w += 2;
	h += 2;

	for (texnum = Q_max(scrap_offset, 0); texnum < RM_MAX_SCRAPS; texnum++)
	{
		if (!scrap_tex[texnum])
		{
			/* transparent, as the texels are until the first update */
			scrap_tex[texnum] = rmb_texture_upload(0, (const unsigned char *)scrap_texels[texnum],
					RM_SCRAP_WIDTH, RM_SCRAP_HEIGHT, 0, 0);

			if (!scrap_tex[texnum])
			{
				return -1;
			}
		}

		if (rm_scrap_alloc(scrap_allocated[texnum], RM_SCRAP_WIDTH, RM_SCRAP_HEIGHT, (int)w, (int)h, x, y))
		{
			(*x)++;   /* jump the border */
			(*y)++;

			scrap_dirty[texnum] = true;

			if (!rm_scrap_copy(scrap_texels[texnum], RM_SCRAP_WIDTH, RM_SCRAP_HEIGHT,
					*x, *y, (int)w - 2, (int)h - 2, pic))
			{
				return -1;
			}

			return texnum;
		}
	}

	return -1;
}

/* the back end texture of an atlas, 0 for an index outside the table */
int
Scrap_Texture(int texnum)
{
	return ((texnum >= 0) && (texnum < RM_MAX_SCRAPS)) ? scrap_tex[texnum] : 0;
}

/*
 * The sampler of a scrap texture, by gl1's Scrap_Update: the first atlases
 * and every atlas with r_2D_unfiltered are nearest; the others are
 * filtered without mipmaps (gl_filter_max for both).
 */
int
Scrap_Sampler(int tex)
{
	int i;

	for (i = 0; i < RM_MAX_SCRAPS; i++)
	{
		if (tex && (tex == scrap_tex[i]))
		{
			return RMB_SAMPLER((i < RM_MAX_SCRAPS_NOLERP) || (r_2D_unfiltered->value != 0.0f)
					? RMB_SMP_NEAREST : RMB_SMP_FLAT, 0);
		}
	}

	return RMB_SAMPLER(RMB_SMP_FLAT, 0);
}

/*
 * from gl1_draw.c, Scrap_Update: the dirty atlases are written to their
 * textures, light-scaled as gl1's R_Upload32 does an image without mipmaps.
 * A copy goes through the scale (the texels stay as the pics were put).
 */
void
Scrap_Update(void)
{
	int texnum;

	for (texnum = 0; texnum < RM_MAX_SCRAPS; texnum++)
	{
		const size_t scrap_size = sizeof(scrap_texels[0]);
		unsigned *tmp;
		size_t i;
		qboolean has_alpha = false;

		if (!scrap_dirty[texnum] || !scrap_tex[texnum])
		{
			continue;
		}

		tmp = malloc(scrap_size);

		if (!tmp)
		{
			R_Printf(PRINT_ALL, "%s: Couldn't malloc %zu bytes\n", __func__, scrap_size);
			return;
		}

		memcpy(tmp, scrap_texels[texnum], scrap_size);
		R_LightScaleTexture(tmp, RM_SCRAP_WIDTH, RM_SCRAP_HEIGHT, true);

		for (i = 0; i < scrap_size / 4; i++)
		{
			if (((byte *)tmp)[i * 4 + 3] != 255)
			{
				has_alpha = true;
				break;
			}
		}

		/* 0 frees the id (an allocation failed): the pics on this atlas draw nothing until it is made again */
		scrap_tex[texnum] = rmb_texture_upload(scrap_tex[texnum], (const unsigned char *)tmp,
				RM_SCRAP_WIDTH, RM_SCRAP_HEIGHT, 0, !has_alpha);
		scrap_dirty[texnum] = scrap_tex[texnum] ? false : true;
		free(tmp);
	}
}
