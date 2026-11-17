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
 * The Wal image format
 *
 * =======================================================================
 */

#include "../ref_shared.h"
#include "imgsize.h"

struct image_s *
LoadWal(const char *origname, imagetype_t type, loadimage_t load_image)
{
	int	width, height, ofs, size;
	struct image_s	*image;
	char	name[256];
	miptex_t	*mt;

	FixFileExt(origname, "wal", name, sizeof(name));

	size = ri.FS_LoadFile(name, (void **)&mt);

	if (!mt)
	{
		return NULL;
	}

	if (size < sizeof(miptex_t))
	{
		Com_Printf("%s: can't load %s, small header\n", __func__, name);
		ri.FS_FreeFile((void *)mt);
		return NULL;
	}

	width = LittleLong(mt->width);
	height = LittleLong(mt->height);
	ofs = LittleLong(mt->offsets[0]);

	/* the body in 64 bits, and the size held to the cap */
	if (!IMGSIZE_BodyFits(size, ofs, width, height, 1))
	{
		Com_Printf("%s: can't load %s, small body\n", __func__, name);
		ri.FS_FreeFile((void *)mt);
		return NULL;
	}

	image = load_image(name, (byte *)mt + ofs,
		width, 0,
		height, 0,
		(size - ofs), type, 8);

	ri.FS_FreeFile((void *)mt);

	return image;
}

struct image_s *
LoadM8(const char *origname, imagetype_t type, loadimage_t load_image)
{
	m8tex_t *mt;
	int width, height, ofs, size;
	size_t i, pixels, buffer_size;
	struct image_s *image;
	char name[256];
	unsigned char *image_buffer = NULL;

	FixFileExt(origname, "m8", name, sizeof(name));

	size = ri.FS_LoadFile(name, (void **)&mt);

	if (!mt)
	{
		return NULL;
	}

	if (size < sizeof(m8tex_t))
	{
		Com_Printf("%s: can't load %s, small header\n", __func__, name);
		ri.FS_FreeFile((void *)mt);
		return NULL;
	}

	if (LittleLong (mt->version) != M8_VERSION)
	{
		Com_Printf("%s: can't load %s, wrong magic value.\n", __func__, name);
		ri.FS_FreeFile ((void *)mt);
		return NULL;
	}

	width = LittleLong(mt->width[0]);
	height = LittleLong(mt->height[0]);
	ofs = LittleLong(mt->offsets[0]);

	/* the body in 64 bits, and the size held to the cap */
	if (!IMGSIZE_BodyFits(size, ofs, width, height, 1))
	{
		Com_Printf("%s: can't load %s, small body\n", __func__, name);
		ri.FS_FreeFile((void *)mt);
		return NULL;
	}

	/* the expansion is sized from the capped pixel count, not the file */
	if (!IMGSIZE_Bytes(width, height, 4, &buffer_size))
	{
		ri.FS_FreeFile((void *)mt);
		return NULL;
	}

	pixels = buffer_size / 4;
	image_buffer = malloc(buffer_size);

	if (!image_buffer)
	{
		Com_Printf("%s: can't allocate for %s\n", __func__, name);
		ri.FS_FreeFile((void *)mt);
		return NULL;
	}

	for (i = 0; i < pixels; i++)
	{
		unsigned char value = *((byte *)mt + ofs + i);
		image_buffer[i * 4 + 0] = mt->palette[value].r;
		image_buffer[i * 4 + 1] = mt->palette[value].g;
		image_buffer[i * 4 + 2] = mt->palette[value].b;
		image_buffer[i * 4 + 3] = value == 255 ? 0 : 255;
	}

	image = load_image(name, image_buffer,
		width, 0,
		height, 0,
		pixels, type, 32);
	free(image_buffer);

	ri.FS_FreeFile((void *)mt);

	return image;
}

struct image_s *
LoadM32(const char *origname, imagetype_t type, loadimage_t load_image)
{
	m32tex_t	*mt;
	int		width, height, ofs, size;
	struct image_s	*image;
	char name[256];

	FixFileExt(origname, "m32", name, sizeof(name));

	size = ri.FS_LoadFile(name, (void **)&mt);

	if (!mt)
	{
		return NULL;
	}

	if (size < sizeof(m32tex_t))
	{
		Com_Printf("%s: can't load %s, small header\n", __func__, name);
		ri.FS_FreeFile((void *)mt);
		return NULL;
	}

	if (LittleLong (mt->version) != M32_VERSION)
	{
		Com_Printf("%s: can't load %s, wrong magic value.\n", __func__, name);
		ri.FS_FreeFile ((void *)mt);
		return NULL;
	}

	width = LittleLong (mt->width[0]);
	height = LittleLong (mt->height[0]);
	ofs = LittleLong (mt->offsets[0]);

	/* the body test in 64 bits: width * 4 wraps an int */
	if (!IMGSIZE_BodyFits(size, ofs, width, height, 4))
	{
		Com_Printf("%s: can't load %s, small body\n", __func__, name);
		ri.FS_FreeFile((void *)mt);
		return NULL;
	}

	image = load_image(name, (byte *)mt + ofs,
		width, 0,
		height, 0,
		(size - ofs) / 4, type, 32);
	ri.FS_FreeFile ((void *)mt);

	return image;
}

void
GetWalInfo(const char *origname, int *width, int *height)
{
	miptex_t *mt;
	int size;
	char filename[256];

	FixFileExt(origname, "wal", filename, sizeof(filename));

	size = ri.FS_LoadFile(filename, (void **)&mt);

	if (!mt)
	{
		return;
	}

	if (size < sizeof(miptex_t))
	{
		ri.FS_FreeFile((void *)mt);
		return;
	}

	if (IMGSIZE_DimsPositive(LittleLong(mt->width), LittleLong(mt->height)))
	{
		*width = LittleLong(mt->width);
		*height = LittleLong(mt->height);
	}

	ri.FS_FreeFile((void *)mt);

	return;
}

void
GetM8Info(const char *origname, int *width, int *height)
{
	m8tex_t *mt;
	int size;
	char filename[256];

	FixFileExt(origname, "m8", filename, sizeof(filename));

	size = ri.FS_LoadFile(filename, (void **)&mt);

	if (!mt)
	{
		return;
	}

	if (size < sizeof(m8tex_t) || LittleLong (mt->version) != M8_VERSION)
	{
		ri.FS_FreeFile((void *)mt);
		return;
	}

	if (IMGSIZE_DimsPositive(LittleLong(mt->width[0]), LittleLong(mt->height[0])))
	{
		*width = LittleLong(mt->width[0]);
		*height = LittleLong(mt->height[0]);
	}

	ri.FS_FreeFile((void *)mt);

	return;
}

void
GetM32Info(const char *origname, int *width, int *height)
{
	m32tex_t *mt;
	int size;
	char filename[256];

	FixFileExt(origname, "m32", filename, sizeof(filename));

	size = ri.FS_LoadFile(filename, (void **)&mt);

	if (!mt)
	{
		return;
	}

	if (size < sizeof(m32tex_t) || LittleLong (mt->version) != M32_VERSION)
	{
		ri.FS_FreeFile((void *)mt);
		return;
	}

	if (IMGSIZE_DimsPositive(LittleLong(mt->width[0]), LittleLong(mt->height[0])))
	{
		*width = LittleLong(mt->width[0]);
		*height = LittleLong(mt->height[0]);
	}

	ri.FS_FreeFile((void *)mt);

	return;
}
