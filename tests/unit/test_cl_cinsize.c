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
 * The byte size of a cinematic still (FIX64, src/client/cl_cinsize.c):
 * the ImageIO decoder admits 8192x8192, whose 32 bit size, 2^28 bytes,
 * is 2^31 bits and wrapped the int the buffer size used to be computed in.
 *
 * =======================================================================
 */

#include <limits.h>
#include <stddef.h>

#include "test.h"
#include "../../src/client/header/client.h"
#include "../../src/client/header/cinsize.h"

/* the arithmetic cl_cin.c had: height * width * bpp / 8 in int */
static int
old_bytes(int w, int h, int bpp)
{
	return (int)((unsigned)h * (unsigned)w * (unsigned)bpp) / 8;
}

TEST(cinsize_cap_32bit)
{
	T_CHECK(SCR_StillBytes(8192, 8192, 32) == (size_t)8192 * 8192 * 4);
	/* the old arithmetic wraps to a negative, which Z_Malloc took as a huge size_t */
	T_CHECK(old_bytes(8192, 8192, 32) <= 0);
}

TEST(cinsize_cap_8bit)
{
	T_CHECK(SCR_StillBytes(8192, 8192, 8) == (size_t)8192 * 8192);
}

TEST(cinsize_ordinary)
{
	T_CHECK(SCR_StillBytes(640, 480, 8) == 307200);
	T_CHECK(SCR_StillBytes(640, 480, 32) == 1228800);
	T_CHECK(SCR_StillBytes(1, 1, 8) == 1);
	T_CHECK(SCR_StillBytes(1, 1, 32) == 4);
}

TEST(cinsize_past_cap)
{
	size_t one_past = SCR_StillBytes(8193, 8192, 32);

	T_CHECK(one_past == (size_t)8193 * 8192 * 4);
	T_CHECK(SCR_StillBytes(8192, 8193, 8) == (size_t)8192 * 8193);
	/* a side of INT_MAX is exact in size_t where it can be, never wrapped */
	T_CHECK(SCR_StillBytes(INT_MAX, 1, 8) == (size_t)INT_MAX);
	T_CHECK(SCR_StillBytes(INT_MAX, INT_MAX, 32) == 0 ||
		SCR_StillBytes(INT_MAX, INT_MAX, 32) == (size_t)INT_MAX * INT_MAX * 4);
}

TEST(cinsize_bad_sides)
{
	T_CHECK(SCR_StillBytes(0, 480, 8) == 0);
	T_CHECK(SCR_StillBytes(640, 0, 8) == 0);
	T_CHECK(SCR_StillBytes(-1, 480, 32) == 0);
	T_CHECK(SCR_StillBytes(640, -1, 32) == 0);
	T_CHECK(SCR_StillBytes(INT_MIN, INT_MIN, 32) == 0);
	T_CHECK(SCR_StillBytes(0, 0, 8) == 0);
}

TEST(cinsize_bad_depth)
{
	T_CHECK(SCR_StillBytes(16, 16, 0) == 0);
	T_CHECK(SCR_StillBytes(16, 16, -8) == 0);
	T_CHECK(SCR_StillBytes(16, 16, 7) == 0);
	T_CHECK(SCR_StillBytes(16, 16, 40) == 0);
	T_CHECK(SCR_StillBytes(16, 16, 24) == 16 * 16 * 3);
	T_CHECK(SCR_StillBytes(16, 16, 16) == 16 * 16 * 2);
}
