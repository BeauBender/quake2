/*
 * Copyright (C) 2026 Yamagi Quake II contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * =======================================================================
 *
 * The vt backend's byte to half-float conversion
 * (src/client/header/upscale_half.h, TASK167), without VideoToolbox or
 * its model: every byte goes out as v / 255 and comes back as itself.
 * TASK167 round 1's Blocker stored a byte as its own value (128 as
 * 128.0), which these cases refuse.
 *
 * =======================================================================
 */

#include "test.h"
#include "../../src/client/header/upscale_half.h"

/* all 256 bytes come back exactly, and in order */
TEST(upscale_half_round_trip)
{
	int v, bad = 0, first = -1;

	for (v = 0; v < 256; v++)
	{
		if (CL_UpscaleVTUnhalf(CL_UpscaleVTHalf((unsigned char)v)) != v)
		{
			bad++;

			if (first < 0)
			{
				first = v;
			}
		}
	}

	T_EQ_INT(bad, 0);
	T_EQ_INT(first, -1);

	for (v = 1; v < 256; v++)
	{
		T_CHECK(CL_UpscaleVTHalf((unsigned char)v) >
			CL_UpscaleVTHalf((unsigned char)(v - 1)));
	}
}

/* the encode is v / 255: 0 is 0.0, 255 is 1.0, 51 is 0.2 rounded */
TEST(upscale_half_bits)
{
	T_EQ_INT(CL_UpscaleVTHalf(0), 0x0000);
	T_EQ_INT(CL_UpscaleVTHalf(255), 0x3c00);
	T_EQ_INT(CL_UpscaleVTHalf(51), 0x3266);
	T_EQ_INT(CL_UpscaleVTHalf(128), 0x3804);
}

/* what the scaler can give back: negatives, above 1, infinity, NaN */
TEST(upscale_half_decode_edges)
{
	T_EQ_INT(CL_UpscaleVTUnhalf(0x8000), 0);
	T_EQ_INT(CL_UpscaleVTUnhalf(0xbc00), 0);
	T_EQ_INT(CL_UpscaleVTUnhalf(0x4000), 255);
	T_EQ_INT(CL_UpscaleVTUnhalf(0x7c00), 255);
	T_EQ_INT(CL_UpscaleVTUnhalf(0x7e00), 0);
	T_EQ_INT(CL_UpscaleVTUnhalf(0x0001), 0);
}
