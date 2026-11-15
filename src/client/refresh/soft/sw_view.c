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
 * The view rectangle against the buffer, free of SDL and of the
 * renderer's state so that the unit tests link it.
 *
 * =======================================================================
 */

#include "header/local.h"

/*
 * Keeps the view rectangle inside a width x height buffer. The client
 * sizes the view from viddef, this renderer draws it into the
 * vid_buffer_width x vid_buffer_height buffer. The two are the same size
 * when both sides agree on the window (SWimp_DrawableInPixels() in
 * sw_main.c), and then nothing changes here. Should they ever disagree,
 * the view is drawn into the part of the buffer it covers, a wrong
 * picture, instead of past the end of the buffer. width and height are
 * at least 1.
 */
void
R_ClampViewToBuffer(refdef_t *rd, int width, int height)
{
	rd->x = Q_clamp(rd->x, 0, width - 1);
	rd->y = Q_clamp(rd->y, 0, height - 1);
	rd->width = Q_clamp(rd->width, 1, width - rd->x);
	rd->height = Q_clamp(rd->height, 1, height - rd->y);
}
