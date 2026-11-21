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

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

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

#if defined(__APPLE__) && TARGET_OS_IPHONE
/*
 * The insets of the window's safe rectangle in pixels (TASK147), the
 * corners, the sensor housing and the home indicator of an iPhone or iPad.
 * All 0 when SDL has no rectangle or it leaves nothing. (Elsewhere
 * local.h's RI_GetLayer gives no inset, and the rest of the file is as it was.)
 */
static void
RI_GetSafeInsets(rm_inset_t *in)
{
	SDL_Rect r;
	int pw, ph;

	static int logged[6] = { -1, -1, -1, -1, -1, -1 };
	int now[6];

	memset(in, 0, sizeof(*in));
	pw = ph = 0;

	if (window && SDL_GetWindowSizeInPixels(window, &pw, &ph) && SDL_GetWindowSafeArea(window, &r))
	{
		rm_safe_insets((float)r.x, (float)r.y, (float)r.w, (float)r.h,
				SDL_GetWindowPixelDensity(window), pw, ph, in);
	}

	/* once per change, for the log: the window in pixels and the insets the frame is placed by */
	now[0] = pw;
	now[1] = ph;
	now[2] = in->left;
	now[3] = in->top;
	now[4] = in->right;
	now[5] = in->bottom;

	if (memcmp(now, logged, sizeof(now)) != 0)
	{
		memcpy(logged, now, sizeof(now));
		R_Printf(PRINT_ALL, "ref_metal: safe area: window %ix%i pixels, insets left %i top %i right %i bottom %i\n",
				pw, ph, in->left, in->top, in->right, in->bottom);
	}
}

/*
 * The view and the layer of the window (rm_window_sizes) with the insets
 * the present places the frame by: no inset when the sizes refuse them.
 */
static void
RI_GetWindowSizes(rm_sizes_t *s, rm_inset_t *in)
{
	int pw = 0, ph = 0;

	RI_GetSafeInsets(in);

	if (!window || !SDL_GetWindowSizeInPixels(window, &pw, &ph))
	{
		pw = ph = 0;
	}

	if (!rm_window_sizes(pw, ph, in, s))
	{
		memset(in, 0, sizeof(*in));
	}
}

/*
 * The layer's drawable, the window's whole pixels, and the insets of its
 * safe rectangle, where the present puts the frame (TASK147): viddef is
 * the rectangle (RI_GetDrawableSize), the drawable is not.
 */
void
RI_GetLayer(int *width, int *height, rm_inset_t *in)
{
	rm_sizes_t s;

	RI_GetWindowSizes(&s, in);
	*width = s.lw;
	*height = s.lh;
}
#endif

void
RI_GetDrawableSize(int *width, int *height)
{
	if (!window || !SDL_GetWindowSizeInPixels(window, width, height))
	{
		*width = 0;
		*height = 0;
	}
#if defined(__APPLE__) && TARGET_OS_IPHONE
	else
	{
		/* viddef is the safe rectangle: the HUD, the console, the menus and the 3D view lie inside it */
		rm_inset_t in;
		rm_sizes_t s;

		RI_GetWindowSizes(&s, &in);
		*width = s.vw;
		*height = s.vh;
	}
#endif
}

void
RI_ShutdownContext(void)
{
	RM_HdrProbeCancel();
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
