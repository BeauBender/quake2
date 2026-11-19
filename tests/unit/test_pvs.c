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
 * The renderers' PVS decompressor (src/client/refresh/files/pvs.c,
 * Mod_DecompressVis, compiled into this file whole because nothing else
 * in the suite links it), used by gl1, gl3 and soft (FIX134).
 *
 * - It reads no byte past the visibility lump: each row is decoded twice,
 *   once from a heap copy of exactly the lump's length, so a read past it
 *   is a heap overflow the address sanitizer reports, and once from a
 *   buffer whose bytes after the lump are a canary, so a read past it
 *   changes the row even without the sanitizer.
 * - A row whose input ends inside it (a zero run whose count byte is
 *   past the lump, or runs that end before the row is full) is all
 *   visible, as ref_metal's bounded decoder (rm_decompress_vis) makes it.
 * - It writes no byte past its row buffer: a zero run that carries past
 *   the row stops at the row's end.
 * - A stock-shaped row decodes exactly as before, also when its last
 *   zero run's count is the lump's last byte.
 *
 * =======================================================================
 */

#include "test.h"

#include "../../src/client/refresh/files/pvs.c"

#define T_CANARY 0xaa
#define T_CANARY_LEN 300

static const byte *
t_decode(const byte *in, int inlen, int row)
{
	(void)inlen; /* RED: the decoder takes no length yet */
	return Mod_DecompressVis(in, row);
}

/* Decodes lump (len bytes) into row bytes from both buffers, both must
   give want. */
static void
t_check_row(const byte *lump, int len, int row, const byte *want)
{
	static byte canaried[MAX_MAP_LEAFS / 8 + 1 + T_CANARY_LEN];
	byte *exact;
	const byte *got;

	T_REQUIRE(len + T_CANARY_LEN <= (int)sizeof(canaried));

	exact = malloc(len);
	T_REQUIRE(exact != NULL);
	memcpy(exact, lump, len);
	got = t_decode(exact, len, row);
	T_EQ_MEM(got, want, row);
	free(exact);

	memcpy(canaried, lump, len);
	memset(canaried + len, T_CANARY, T_CANARY_LEN);
	got = t_decode(canaried, len, row);
	T_EQ_MEM(got, want, row);
}

TEST(pvs_zero_run_count_past_lump)
{
	/* the zero byte is the lump's last, its count would be past it */
	static const byte lump[] = { 0x01, 0x00 };
	static const byte want[] = { 0xff, 0xff, 0xff, 0xff };

	t_check_row(lump, sizeof(lump), sizeof(want), want);
}

TEST(pvs_runs_end_before_row)
{
	/* four bytes of the eight-byte row, then the lump ends */
	static const byte lump[] = { 0x03, 0x00, 0x02, 0x05 };
	static const byte want[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	t_check_row(lump, sizeof(lump), sizeof(want), want);
}

TEST(pvs_stock_row)
{
	/* the row's runs, then the next cluster's bytes in the same lump */
	static const byte lump[] = { 0x81, 0x00, 0x03, 0x40, 0x00, 0x02, 0x7f, 0x11, 0x00, 0x09 };
	static const byte want[] = { 0x81, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x7f };

	t_check_row(lump, sizeof(lump), sizeof(want), want);
}

TEST(pvs_stock_row_ends_with_lump)
{
	/* the last zero run's count is the lump's last byte */
	static const byte lump[] = { 0x01, 0x00, 0x03 };
	static const byte want[] = { 0x01, 0x00, 0x00, 0x00 };

	t_check_row(lump, sizeof(lump), sizeof(want), want);
}

TEST(pvs_zero_run_past_row)
{
	/* a full-size row whose last zero run counts 255 from its last byte:
	   the run stops at the row's end, inside the row buffer */
	static byte lump[MAX_MAP_LEAFS / 8 + 1];
	static byte want[MAX_MAP_LEAFS / 8];
	const int row = MAX_MAP_LEAFS / 8;

	memset(lump, 0x01, row - 1);
	lump[row - 1] = 0x00;
	lump[row] = 0xff;
	memset(want, 0x01, row - 1);
	want[row - 1] = 0x00;

	t_check_row(lump, row + 1, row, want);
}

TEST(pvs_no_vis)
{
	static const byte want[] = { 0xff, 0xff, 0xff };

	T_EQ_MEM(t_decode(NULL, 0, sizeof(want)), want, sizeof(want));
}
