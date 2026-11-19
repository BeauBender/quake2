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
	return Mod_DecompressVis(in, inlen, row);
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

/*
 * The write bound without a sanitizer: a full row of 0x01 is decoded
 * first, then a four-byte row whose zero run (count 255) would carry far
 * past it. The bytes of the static buffer after the four-byte row keep
 * the first row's 0x01.
 */
TEST(pvs_zero_run_stops_at_row_no_asan)
{
	static byte full[MAX_MAP_LEAFS / 8];
	static const byte lump[] = { 0x01, 0x00, 0xff };
	static const byte want[] = { 0x01, 0x00, 0x00, 0x00 };
	const byte *got;
	int k;

	memset(full, 0x01, sizeof(full));
	(void)t_decode(full, sizeof(full), sizeof(full));
	got = t_decode(lump, sizeof(lump), sizeof(want));
	T_EQ_MEM(got, want, sizeof(want));

	for (k = sizeof(want); k < MAX_MAP_LEAFS / 8; k++)
	{
		if (got[k] != 0x01)
		{
			t_fail(__FILE__, __LINE__, "byte %d after the row is %02x, not 01", k, got[k]);
			break;
		}
	}
}

TEST(pvs_empty_input)
{
	/* no byte of the lump left at the cluster's offset: all visible */
	static const byte lump[] = { 0x01 };
	static const byte want[] = { 0xff, 0xff, 0xff, 0xff };

	T_EQ_MEM(t_decode(lump, 0, sizeof(want)), want, sizeof(want));
}

TEST(pvs_row_past_buffer)
{
	/* a row longer than MAX_MAP_LEAFS / 8 bytes is decoded to the
	   buffer's end: the zero run stops there, nothing is written past it */
	static byte lump[MAX_MAP_LEAFS / 8 + 2];
	static byte want[MAX_MAP_LEAFS / 8];
	const int full = MAX_MAP_LEAFS / 8;

	memset(lump, 0x01, full - 1);
	lump[full - 1] = 0x00;
	lump[full] = 0xff;
	lump[full + 1] = 0x00;
	memset(want, 0x01, full - 1);
	want[full - 1] = 0x00;

	T_EQ_MEM(t_decode(lump, sizeof(lump), full + 100), want, full);
}

/*
 * The cluster index is the map's (a leaf's signed short):
 * Mod_ClusterPVSRow, which every renderer's ClusterPVS calls, gives the
 * no-vis row for any cluster outside [0, numclusters) and for a map
 * without visibility, and reads bitofs only inside the checked table.
 */
static const byte t_novis[4] = { 0xff, 0xff, 0xff, 0xff };

TEST(pvs_cluster_out_of_range)
{
	/* two clusters: the count and offset pairs, then one byte of each row */
	static int lump[1 + 2 * 2 + 1];
	byte *rows = (byte *)&lump[5];
	const int len = sizeof(lump);
	const dvis_t *vis = (const dvis_t *)lump;
	const byte *got;

	lump[0] = 2;
	lump[1] = 20;
	lump[2] = 21;
	lump[3] = 22;
	lump[4] = 23;
	rows[0] = 0x01;
	rows[1] = 0x03;
	rows[2] = 0x02;
	rows[3] = 0x03;

	got = Mod_ClusterPVSRow(vis, len, 0, t_novis);
	T_CHECK(got != t_novis);
	T_EQ_INT(got[0], 0x01);
	got = Mod_ClusterPVSRow(vis, len, 1, t_novis);
	T_CHECK(got != t_novis);
	T_EQ_INT(got[0], 0x02);

	T_CHECK(Mod_ClusterPVSRow(vis, len, -1, t_novis) == t_novis);
	T_CHECK(Mod_ClusterPVSRow(vis, len, 2, t_novis) == t_novis);
	T_CHECK(Mod_ClusterPVSRow(vis, len, -2, t_novis) == t_novis);
	T_CHECK(Mod_ClusterPVSRow(vis, len, 32767, t_novis) == t_novis);
	T_CHECK(Mod_ClusterPVSRow(vis, len, -32768, t_novis) == t_novis);
	T_CHECK(Mod_ClusterPVSRow(NULL, 0, 0, t_novis) == t_novis);
}
