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
 * The rectangle of the console background, pure arithmetic so that the
 * unit tests run it (cl_console.c, which draws it, is not linked there).
 *
 * =======================================================================
 */

#include "header/client.h"

/*
 * The rectangle Con_DrawConsole hands Draw_StretchPic for the console
 * background in a window of vw by vh pixels with the console lines
 * pixels deep. Without keep43 it is the whole window stretched, its
 * bottom edge at lines (x 0, y lines - vh, vw by vh). With keep43 the
 * picture keeps its 4:3 shape and covers the window: as wide as the
 * window when the window is wider than 4:3, as high when it is narrower,
 * rounded up, centred across (the extra half on the right when it is
 * odd) and with its bottom edge at lines, so the cropping is of the top
 * of the picture and of its sides. A window of 4:3 gives the stretched
 * rectangle exactly. A size that is not positive gives the stretched
 * rectangle.
 */
void
Con_BackRect(int vw, int vh, int lines, int keep43, int *x, int *y, int *w, int *h)
{
	long long rw = vw, rh = vh;

	if (keep43 && (vw > 0) && (vh > 0))
	{
		if ((long long)vw * 3 >= (long long)vh * 4)
		{
			rw = vw;
			rh = ((long long)vw * 3 + 3) / 4;
		}
		else
		{
			rh = vh;
			rw = ((long long)vh * 4 + 2) / 3;
		}
	}

	/* a window so narrow that the covering width does not fit an int is not cropped (the height is the window's there) */
	if (rw > INT_MAX)
	{
		rw = vw;
	}

	*w = (int)rw;
	*h = (int)rh;
	*x = (int)-((rw - vw) / 2);
	*y = lines - (int)rh;
}
