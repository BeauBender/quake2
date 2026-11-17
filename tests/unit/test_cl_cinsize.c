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
#include <stdlib.h>
#include <string.h>

#include "test.h"
#include "../../src/client/header/client.h"
#include "../../src/client/header/cinsize.h"
#include "../../src/client/refresh/files/imgdecode.h"

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
	/* multiples of 8 would give bytes 1 and 0.5 without the guard */
	T_CHECK(SCR_StillBytes(16, 16, 12) == 0);
	T_CHECK(SCR_StillBytes(16, 16, 4) == 0);
	T_CHECK(SCR_StillBytes(16, 16, 24) == 16 * 16 * 3);
	T_CHECK(SCR_StillBytes(16, 16, 16) == 16 * 16 * 2);
}

TEST(cinsize_size_limit)
{
	size_t limit = (size_t)IMGDEC_MAX_AREA * 4;

	T_CHECK(!SCR_StillSizeOk(0));
	T_CHECK(SCR_StillSizeOk(1));
	T_CHECK(SCR_StillSizeOk(limit - 1));
	T_CHECK(SCR_StillSizeOk(limit));
	T_CHECK(!SCR_StillSizeOk(limit + 1));
	T_CHECK(!SCR_StillSizeOk((size_t)-1));
}

TEST(cinsize_check_verdicts)
{
	size_t bytes = 0;

	T_CHECK(SCR_StillCheck(8192, 8192, 32, &bytes) == SCR_STILL_OK);
	T_CHECK(bytes == (size_t)8192 * 8192 * 4);
	bytes = 7;
	T_CHECK(SCR_StillCheck(8193, 8192, 32, &bytes) == SCR_STILL_TOOLARGE);
	T_CHECK(SCR_StillCheck(65536, 65536, 32, &bytes) == SCR_STILL_TOOLARGE);
	T_CHECK(SCR_StillCheck(65536, 65536, 8, &bytes) == SCR_STILL_TOOLARGE);
	T_CHECK(SCR_StillCheck(0, 480, 8, &bytes) == SCR_STILL_BADFORMAT);
	T_CHECK(SCR_StillCheck(640, -1, 8, &bytes) == SCR_STILL_BADFORMAT);
	T_CHECK(SCR_StillCheck(16, 16, 12, &bytes) == SCR_STILL_BADFORMAT);
	T_CHECK(SCR_StillCheck(16, 16, 40, &bytes) == SCR_STILL_BADFORMAT);
	/* a refusal leaves *bytes alone */
	T_CHECK(bytes == 7);
}

/*
 * The still-image stem (FIX21): the name without its extension goes into
 * a caller-sized buffer. Each case lives in a larger array with guard
 * bytes either side of the buffer, because a write past a stack object
 * into its neighbour is invisible to the sanitizer.
 */
#define STEM_BUF 256
#define STEM_GUARD 32
#define STEM_FILL 0xA5

static char stem_arena[STEM_GUARD + STEM_BUF + STEM_GUARD];

static int
stem_guards_intact(void)
{
	size_t i;

	for (i = 0; i < STEM_GUARD; i++)
	{
		if (((unsigned char)stem_arena[i] != STEM_FILL) ||
			((unsigned char)stem_arena[STEM_GUARD + STEM_BUF + i] != STEM_FILL))
		{
			return 0;
		}
	}

	return 1;
}

/* Every byte of the arena, the buffer included, still holds the fill:
   a refusal writes nothing. */
static int
stem_arena_untouched(void)
{
	size_t i;

	for (i = 0; i < sizeof(stem_arena); i++)
	{
		if ((unsigned char)stem_arena[i] != STEM_FILL)
		{
			return 0;
		}
	}

	return 1;
}

/* A name of stemlen 'a' followed by ext, run into the STEM_BUF window. */
static int
stem_run(size_t stemlen, const char *ext, char *name)
{
	char *dot;

	memset(stem_arena, STEM_FILL, sizeof(stem_arena));
	memset(name, 'a', stemlen);
	strcpy(name + stemlen, ext);
	dot = name + stemlen;

	return SCR_StillStem(name, dot, stem_arena + STEM_GUARD, STEM_BUF);
}

TEST(cinsize_stem_normal)
{
	char *dot, name[] = "pics/title.pcx";

	memset(stem_arena, STEM_FILL, sizeof(stem_arena));
	dot = strstr(name, ".");
	T_CHECK(SCR_StillStem(name, dot, stem_arena + STEM_GUARD, STEM_BUF));
	T_CHECK(!strcmp(stem_arena + STEM_GUARD, "pics/title"));
	T_CHECK(stem_guards_intact());
}

TEST(cinsize_stem_fits_exactly)
{
	char *name = malloc(STEM_BUF + 16);

	T_CHECK(stem_run(STEM_BUF - 1, ".pcx", name));
	T_CHECK(strlen(stem_arena + STEM_GUARD) == STEM_BUF - 1);
	T_CHECK(stem_guards_intact());
	free(name);
}

TEST(cinsize_stem_equals_buffer_refused)
{
	char *name = malloc(STEM_BUF + 16);

	T_CHECK(!stem_run(STEM_BUF, ".pcx", name));
	T_CHECK(stem_arena_untouched());
	free(name);
}

TEST(cinsize_stem_one_over_refused)
{
	char *name = malloc(STEM_BUF + 16);

	T_CHECK(!stem_run(STEM_BUF + 1, ".pcx", name));
	T_CHECK(stem_arena_untouched());
	free(name);
}

TEST(cinsize_stem_ext_longer_than_name)
{
	char name[] = "ab";
	const char *dot = ".pcx.pcx";

	memset(stem_arena, STEM_FILL, sizeof(stem_arena));
	T_CHECK(!SCR_StillStem(name, dot, stem_arena + STEM_GUARD, STEM_BUF));
	T_CHECK(stem_arena_untouched());
}

TEST(cinsize_stem_empty)
{
	char *dot, name[] = "pics/.pcx";

	memset(stem_arena, STEM_FILL, sizeof(stem_arena));
	dot = strstr(name, ".");
	T_CHECK(SCR_StillStem(name, dot, stem_arena + STEM_GUARD, STEM_BUF));
	T_CHECK(!strcmp(stem_arena + STEM_GUARD, "pics/"));

	/* a name that is only the extension has an empty stem */
	memset(stem_arena, STEM_FILL, sizeof(stem_arena));
	T_CHECK(SCR_StillStem(".pcx", ".pcx", stem_arena + STEM_GUARD, STEM_BUF));
	T_CHECK(stem_arena[STEM_GUARD] == '\0');
	T_CHECK(stem_guards_intact());
}

/* A name as long as a server message allows, into the old 256-byte buffer.
   The caller's buffer is now MAX_OSPATH and takes this name whole. */
TEST(cinsize_stem_server_length_name_small_buffer)
{
	char *name = malloc(1500);

	T_CHECK(!stem_run(1380, ".pcx", name));
	T_CHECK(stem_arena_untouched());
	free(name);
}

TEST(cinsize_stem_no_room)
{
	char out[1];

	out[0] = 'x';
	T_CHECK(!SCR_StillStem("a.pcx", ".pcx", out, 0));
	T_CHECK(out[0] == 'x');
	T_CHECK(!SCR_StillStem("a.pcx", ".pcx", out, 1));
	T_CHECK(out[0] == 'x');
	T_CHECK(SCR_StillStem(".pcx", ".pcx", out, 1));
	T_CHECK(out[0] == '\0');
}
