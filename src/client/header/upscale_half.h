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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * =======================================================================
 *
 * The byte to half-float conversion of the vt backend's copies
 * (cl_upscale_vt.m, TASK167): pure C, so the unit suite runs it without
 * VideoToolbox or the model (tests/unit/test_upscale_half.c).
 *
 * =======================================================================
 */

#ifndef CL_HEADER_UPSCALE_HALF_H
#define CL_HEADER_UPSCALE_HALF_H

#include <math.h>
#include <string.h>

/*
 * A byte (0 to 255) as the IEEE half-float bit pattern of v / 255 (the
 * scaler's colours run from 0 to 1), and back. The compiler's _Float16
 * does the rounding.
 */
static inline unsigned short
CL_UpscaleVTHalf(unsigned char v)
{
	const _Float16 f = (_Float16)(v / 255.0f);
	unsigned short h;

	memcpy(&h, &f, sizeof(h));

	return h;
}

static inline unsigned char
CL_UpscaleVTUnhalf(unsigned short h)
{
	int e = (h >> 10) & 0x1f;
	float f;

	if (h & 0x8000)
	{
		return 0;
	}

	if (e == 0x1f)
	{
		return (h & 0x3ff) ? 0 : 255;
	}

	f = (e ? (1.0f + (h & 0x3ff) / 1024.0f) * ldexpf(1.0f, e - 15) :
		(h & 0x3ff) / 1024.0f * ldexpf(1.0f, -14)) * 255.0f + 0.5f;

	return (f >= 255.0f) ? 255 : (unsigned char)f;
}

#endif
