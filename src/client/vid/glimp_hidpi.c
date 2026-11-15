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
 * (glimp_sdl3.c), free of SDL so that the unit tests link them.
 *
 * =======================================================================
 */

#include "../header/client.h"

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
	(void)highdpiaware;
	(void)driver;
	return false; /* WIP: not written yet */
}

qboolean
GLimp_DrawableInPixels(qboolean highdpi, int fullscreen, float r_mode)
{
	(void)highdpi;
	(void)fullscreen;
	(void)r_mode;
	return false; /* WIP: not written yet */
}
