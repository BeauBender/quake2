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
 * The screenshot command (gl1_misc.c's R_ScreenShot) and the metal_stats
 * line. The pin harness runs "screenshot tga" after the frame is drawn
 * and before it is presented (SCR_PinsCapture), so the frame read is the
 * one that will be presented.
 *
 * =======================================================================
 */

#include <dlfcn.h>

#include "header/local.h"

/* ref_metal.metallib lies next to the dylib this code is linked into (the shim's mgl_library_path) */
int
RM_ModuleDir(char *dir, size_t len)
{
	Dl_info info;

	if (!dladdr((const void *)&RM_ModuleDir, &info) || !info.dli_fname)
	{
		return 0;
	}

	return rm_dir_of(info.dli_fname, dir, len);
}

/*
 * Keeps this image mapped for the rest of the process (N10). The
 * completion handlers of ref_metal's command buffers are blocks whose
 * code, and the helpers that release them, are in this image; Metal runs
 * and releases each after its command buffer completes, which on a GPU
 * hung past the bounded waits of teardown can be after the engine's
 * dlclose. dyld already keeps an image with Objective-C metadata mapped
 * after dlclose (measured for TASK93: a plain C dylib is unmapped,
 * ref_metal is not); the RTLD_NODELETE reference makes that dlopen's
 * documented behaviour instead of dyld's. Taken once and never closed;
 * 0 when it could not be taken.
 */
int
RM_KeepMapped(void)
{
	static void *self;
	Dl_info info;

	if (self)
	{
		return 1;
	}

	if (!dladdr((const void *)&RM_KeepMapped, &info) || !info.dli_fname)
	{
		return 0;
	}

	self = dlopen(info.dli_fname, RTLD_LAZY | RTLD_NOLOAD | RTLD_NODELETE);
	return self != NULL;
}

/* from gl1_misc.c, R_InitParticleTexture: the particle texture (its tables are rm_particle_texture's) and r_notexture */
static byte notex[4][4] = {
	{0, 0, 0, 0},
	{0, 0, 1, 1},
	{0, 1, 1, 1},
	{0, 1, 1, 1}
};

/*
 * gl1 makes a new particle texture each time (gl1_particle_square
 * changed without point parameters) and leaves the old one to the next
 * R_FreeUnusedImages after it; here the old one is freed at once, so
 * there is one at any time, freed at shutdown by R_ShutdownImages
 */
void
R_InitParticleTexture(void)
{
	byte partData[16][16][4];

	rm_particle_texture(gl1_particle_square->value != 0, partData);

	if (r_particletexture)
	{
		rmb_texture_free(r_particletexture->texnum);
		memset(r_particletexture, 0, sizeof(*r_particletexture));
		r_particletexture = NULL;
	}

	r_particletexture = R_LoadPic("***particle***", (byte *)partData, 16, 0, 16, 0, 16 * 16, it_sprite, 32);
}

void
R_InitBuiltinTextures(void)
{
	byte notexData[8][8][4];
	int x, y;

	R_InitParticleTexture();

	/* also use this for bad textures, but without alpha */
	for (x = 0; x < 8; x++)
	{
		for (y = 0; y < 8; y++)
		{
			notexData[y][x][0] = notex[x & 3][y & 3] * 255;
			notexData[y][x][1] = 0;
			notexData[y][x][2] = 0;
			notexData[y][x][3] = 255;
		}
	}

	r_notexture = R_LoadPic("***r_notexture***", (byte *)notexData, 8, 0, 8, 0, 8 * 8, it_wall, 32);
}

void
RM_PrintStats(void)
{
	rmb_stats_t s;
	rm_ledger_t l;
	char line[RM_STATS_LINE];

	rmb_stats(&s, &l);
	s.noop_calls = rm_noop_calls;
	RM_WorldSkips(&s);
	RM_EntitySkips(&s);
	RM_DlightSkips(&s);
	RM_ParticleSkips(&s);

	if (rm_stats_format(&s, &l, line, sizeof(line)))
	{
		R_Printf(PRINT_ALL, "%s\n", line);
	}
}

/*
 * Reads the colour target and hands it to ri.Vid_WriteScreenshot as RGB
 * rows top down, which is what gl1's R_ScreenShot hands over after its
 * row swap (Metal's texture origin is the top left already).
 */
void
R_ScreenShot(void)
{
	unsigned char *bgra, *rgb;
	int w = 0, h = 0;
	size_t n;

	rmb_target_size(&w, &h);

	if ((w <= 0) || (h <= 0))
	{
		R_Printf(PRINT_ALL, "%s: no frame to read\n", __func__);
		return;
	}

	RM_Flush2D();   /* draw the batched 2D quads, or they are missing from the read */

	n = (size_t)w * (size_t)h;
	bgra = malloc(n * 4);
	rgb = malloc(n * 3);

	if (!bgra || !rgb)
	{
		R_Printf(PRINT_ALL, "%s: Couldn't malloc %zu bytes\n", __func__, n * 7);
	}
	else if (!rmb_read(bgra, n * 4))
	{
		R_Printf(PRINT_ALL, "%s: the frame could not be read back\n", __func__);
	}
	else
	{
		rm_bgra_to_rgb(bgra, (size_t)w * 4, w, h, rgb);
		ri.Vid_WriteScreenshot(w, h, 3, rgb);

		if (metal_stats->value)
		{
			RM_PrintStats();
		}
	}

	free(bgra);
	free(rgb);
}
