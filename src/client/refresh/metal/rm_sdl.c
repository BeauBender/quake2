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
 * The window seat of ref_metal (from gl1_sdl.c): the SDL window flag,
 * the SDL Metal view on the window the client made, its CAMetalLayer
 * handed to the back end, the drawable size and vsync. The only file of
 * ref_metal that includes SDL.
 *
 * =======================================================================
 */

#include "header/local.h"

#include <SDL3/SDL.h>

static SDL_Window *window;
static SDL_MetalView view;
qboolean IsHighDPIaware = false;

int
RI_PrepareForWindow(void)
{
	return SDL_WINDOW_METAL;
}

int
RI_InitContext(void *win)
{
	char err[256], title[80], dir[1024] = "";

	if (win == NULL)
	{
		R_Printf(PRINT_ALL, "%s: no window\n", __func__);
		return false;
	}

	RI_ShutdownContext();

	window = (SDL_Window *)win;
	view = SDL_Metal_CreateView(window);

	if (!view)
	{
		R_Printf(PRINT_ALL, "%s: SDL_Metal_CreateView failed: %s\n", __func__, SDL_GetError());
		window = NULL;
		return false;
	}

	rmb_ledger_note(RM_OBJ_VIEW, 1);

	if (!rmb_attach(SDL_Metal_GetLayer(view), r_vsync->value != 0, err, sizeof(err)))
	{
		R_Printf(PRINT_ALL, "%s: %s\n", __func__, err);
		RI_ShutdownContext();
		return false;
	}

	r_vsync->modified = false;

	/* the shader library, the pipeline table and the samplers (section 4) */
	if (!RM_ModuleDir(dir, sizeof(dir)) || !rmb_resources_open(dir, err, sizeof(err)))
	{
		R_Printf(PRINT_ALL, "%s: %s\n", __func__, dir[0] ? err : "cannot find the directory of ref_metal");
		RI_ShutdownContext();
		return false;
	}

	snprintf(title, sizeof(title), DISPLAYNAME " %s - Metal", YQ2VERSION);
	SDL_SetWindowTitle(window, title);

	IsHighDPIaware = (SDL_GetWindowFlags(window) & SDL_WINDOW_HIGH_PIXEL_DENSITY) ? true : false;

	return true;
}

void
RI_GetDrawableSize(int *width, int *height)
{
	if (!window || !SDL_GetWindowSizeInPixels(window, width, height))
	{
		*width = 0;
		*height = 0;
	}
}

void
RI_ShutdownContext(void)
{
	rmb_detach();

	if (view)
	{
		SDL_Metal_DestroyView(view);
		rmb_ledger_note(RM_OBJ_VIEW, 0);
		view = NULL;
	}

	window = NULL;
}

qboolean
RI_IsVSyncActive(void)
{
	return rmb_vsync() ? true : false;
}

int
RI_GetSDLVersion(void)
{
	int version = SDL_GetVersion();

	return SDL_VERSIONNUM_MAJOR(version);
}
