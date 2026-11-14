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

#include "header/local.h"

void
RM_PrintStats(void)
{
	rmb_stats_t s;
	rm_ledger_t l;
	char line[RM_STATS_LINE];

	rmb_stats(&s, &l);
	s.noop_calls = rm_noop_calls;

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
