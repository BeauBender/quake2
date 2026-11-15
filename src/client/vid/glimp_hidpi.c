/*
 * Copyright (C) 2026 Yamagi Quake II contributors
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
 * USA.
 *
 * =======================================================================
 *
 * The high pixel density decisions of the SDL window code
 * (glimp_sdl3.c), free of SDL so that the unit tests link them. The
 * software renderer links this file too and sizes its buffer with
 * GLimp_DrawableInPixels(), so that the two cannot drift apart.
 *
 * =======================================================================
 */

#include "header/vid.h"

/*
 * True when the window is to be made with SDL_WINDOW_HIGH_PIXEL_DENSITY:
 * vid_highdpiaware is not 0 and the video driver is one whose windows
 * then have the display's pixels. Those are wayland, cocoa (macOS) and
 * uikit (iOS, iPadOS, tvOS and visionOS): without the flag SDL's Cocoa
 * and UIKit Metal views keep a drawable of the window's size in points.
 * A driver SDL could not name (NULL) is none of them.
 */
qboolean
GLimp_HighDPIWanted(float highdpiaware, const char *driver)
{
	if ((highdpiaware == 0) || !driver)
	{
		return false;
	}

	return (strcmp(driver, "wayland") == 0) || (strcmp(driver, "cocoa") == 0) ||
		(strcmp(driver, "uikit") == 0);
}

/*
 * True when the client takes the renderer's drawable size, the window's
 * pixels, for viddef; false when it takes the mode's size. A window made
 * without the flag has its size in points, and the drawable SDL reports
 * for it may be too small (window decorations), so the mode's size is
 * used. A fullscreen window (vid_fullscreen 2) fills the screen, so its
 * pixels are taken only when the native size was asked for (r_mode -2);
 * any other mode keeps its own size there. fullscreen is the value the
 * window was made with, the one passed to GLimp_InitGraphics() or 0 when
 * it fell back to a window; not the vid_fullscreen cvar, which can differ.
 *
 * soft calls this function itself for its buffer and its vid size
 * (SWimp_DrawableInPixels() in sw_main.c), with the same inputs. gl1, gl3
 * and metal keep their own copy of the rule in their SetMode code, which
 * reads the vid_fullscreen cvar instead, so they can differ from this
 * decision when the cvar is not the value passed (RE_SetMode()'s
 * invalid-mode fallback, a cvar of 2.5).
 */
qboolean
GLimp_DrawableInPixels(qboolean highdpi, int fullscreen, float r_mode)
{
	if (!highdpi)
	{
		return false;
	}

	return (fullscreen != 2) || (r_mode == -2);
}
