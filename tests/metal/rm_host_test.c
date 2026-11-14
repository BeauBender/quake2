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
 * Host test of the pure-C parts of ref_metal (src/client/refresh/metal/
 * rm_pure.c), run by tests/check-ref-metal.sh with the system compiler:
 * no SDL, no Metal, no game data. It covers the readback conversion, the
 * apple5 floor and the family list, the object ledger and the two lines
 * it is printed in, each fixed-size buffer at its exact fit and one byte
 * short.
 *
 * =======================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/client/refresh/metal/header/rmb.h"

static int checks, failures;

#define CHECK(cond) \
	do { \
		checks++; \
		if (!(cond)) \
		{ \
			failures++; \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		} \
	} while (0)

/*
 * A 3 by 2 BGRA image whose bytes differ in every row, column and
 * channel, with 4 bytes of padding per row, converts to the RGB rows
 * top down gl1 hands over; the padding is not read, nothing past the
 * output is written.
 */
static void
test_readback(void)
{
	/* B G R A per pixel; pixel (x, y) is 0x10 * y + 4 * x + channel */
	static const unsigned char bgra[2][16] = {
		{ 0x01, 0x02, 0x03, 0xff, 0x05, 0x06, 0x07, 0xff, 0x09, 0x0a, 0x0b, 0xff, 0xee, 0xee, 0xee, 0xee },
		{ 0x11, 0x12, 0x13, 0xff, 0x15, 0x16, 0x17, 0xff, 0x19, 0x1a, 0x1b, 0xff, 0xee, 0xee, 0xee, 0xee },
	};
	static const unsigned char want[18] = {
		0x03, 0x02, 0x01, 0x07, 0x06, 0x05, 0x0b, 0x0a, 0x09,
		0x13, 0x12, 0x11, 0x17, 0x16, 0x15, 0x1b, 0x1a, 0x19,
	};
	unsigned char rgb[18 + 4];

	memset(rgb, 0x5a, sizeof(rgb));
	rm_bgra_to_rgb(&bgra[0][0], 16, 3, 2, rgb);
	CHECK(memcmp(rgb, want, sizeof(want)) == 0);
	CHECK(rgb[18] == 0x5a && rgb[19] == 0x5a && rgb[20] == 0x5a && rgb[21] == 0x5a);

	/* an empty image writes nothing */
	memset(rgb, 0x5a, sizeof(rgb));
	rm_bgra_to_rgb(&bgra[0][0], 16, 0, 2, rgb);
	rm_bgra_to_rgb(&bgra[0][0], 16, 3, 0, rgb);
	CHECK(rgb[0] == 0x5a);
}

static void
test_floor(void)
{
	int n;

	CHECK(!rm_caps_floor_ok(0));
	CHECK(!rm_caps_floor_ok(RMB_FAMILY_BIT(4)));
	CHECK(!rm_caps_floor_ok(RMB_FAMILY_BIT(1) | RMB_FAMILY_BIT(2) | RMB_FAMILY_BIT(3) | RMB_FAMILY_BIT(4)));
	CHECK(rm_caps_floor_ok(RMB_FAMILY_BIT(5)));
	CHECK(rm_caps_floor_ok(RMB_FAMILY_BIT(4) | RMB_FAMILY_BIT(5)));
	CHECK(rm_caps_floor_ok(RMB_FAMILY_BIT(9)));
	/* a bit past apple9 names no family this renderer knows */
	CHECK(!rm_caps_floor_ok(1u << RMB_FAMILY_LAST));

	for (n = RMB_FAMILY_FLOOR; n <= RMB_FAMILY_LAST; n++)
	{
		CHECK(rm_caps_floor_ok(RMB_FAMILY_BIT(n)));
	}
}

static void
test_family_list(void)
{
	static const char all[] = "apple5 apple6 apple7 apple8 apple9";
	char buf[64];

	CHECK(rm_caps_family_list(RMB_FAMILY_BIT(5) | RMB_FAMILY_BIT(7), buf, sizeof(buf)) && strcmp(buf, "apple5 apple7") == 0);
	CHECK(rm_caps_family_list(0, buf, sizeof(buf)) && strcmp(buf, "none") == 0);
	/* the report names 5 to 9 only, as the shim's does */
	CHECK(rm_caps_family_list(RMB_FAMILY_BIT(4), buf, sizeof(buf)) && strcmp(buf, "none") == 0);
	CHECK(rm_caps_family_list(0x1ff, buf, sizeof(buf)) && strcmp(buf, all) == 0);

	/* exact fit, then one byte short: truncated, still terminated */
	CHECK(rm_caps_family_list(0x1ff, buf, sizeof(all)) && strcmp(buf, all) == 0);
	memset(buf, 'x', sizeof(buf));
	CHECK(!rm_caps_family_list(0x1ff, buf, sizeof(all) - 1) && strlen(buf) == sizeof(all) - 2);
	CHECK(!rm_caps_family_list(0x1ff, buf, 0));
}

static void
test_ledger(void)
{
	rm_ledger_t l;
	char buf[256];
	const char *want_live =
		"ref_metal: teardown: 3 live objects (created/destroyed: device 1/0, queue 1/0, view 0/0, "
		"texture 3/2, buffer 0/0), 0 unreleased, 0 unmatched";
	const char *want_clean =
		"ref_metal: teardown: 0 live objects (created/destroyed: device 1/1, queue 1/1, view 0/0, "
		"texture 3/3, buffer 0/0), 0 unreleased, 1 unmatched";
	size_t n;

	memset(&l, 0, sizeof(l));
	CHECK(rm_ledger_live(&l) == 0);

	rm_ledger_create(&l, RM_OBJ_DEVICE);
	rm_ledger_create(&l, RM_OBJ_QUEUE);
	rm_ledger_create(&l, RM_OBJ_TEXTURE);
	rm_ledger_create(&l, RM_OBJ_TEXTURE);
	rm_ledger_create(&l, RM_OBJ_TEXTURE);
	rm_ledger_destroy(&l, RM_OBJ_TEXTURE);
	rm_ledger_destroy(&l, RM_OBJ_TEXTURE);
	CHECK(rm_ledger_live(&l) == 3);
	CHECK(rm_ledger_format(&l, buf, sizeof(buf)) && strcmp(buf, want_live) == 0);

	rm_ledger_destroy(&l, RM_OBJ_TEXTURE);
	rm_ledger_destroy(&l, RM_OBJ_QUEUE);
	rm_ledger_destroy(&l, RM_OBJ_DEVICE);
	/* a destroy without its create is counted, never taken below zero */
	rm_ledger_destroy(&l, RM_OBJ_VIEW);
	CHECK(l.destroyed[RM_OBJ_VIEW] == 0 && l.unmatched == 1);
	CHECK(rm_ledger_live(&l) == 0);
	CHECK(rm_ledger_format(&l, buf, sizeof(buf)) && strcmp(buf, want_clean) == 0);

	/* objects released but still alive count as live */
	l.unreleased = 2;
	CHECK(rm_ledger_live(&l) == 2);
	l.unreleased = 0;

	/* an out-of-range kind is refused, not written past the arrays */
	rm_ledger_create(&l, RM_OBJ_KINDS);
	rm_ledger_destroy(&l, RM_OBJ_KINDS);
	CHECK(rm_ledger_live(&l) == 0 && l.unmatched == 3);

	/* exact fit, then one byte short */
	l.unmatched = 1;
	n = strlen(want_clean);
	CHECK(rm_ledger_format(&l, buf, n + 1) && strcmp(buf, want_clean) == 0);
	CHECK(!rm_ledger_format(&l, buf, n) && strlen(buf) == n - 1);
	CHECK(!rm_ledger_format(&l, buf, 0));
}

static void
test_stats(void)
{
	rmb_stats_t s;
	rm_ledger_t l;
	char buf[256];
	const char *want =
		"ref_metal: metal_stats: 23 frames, 1 without a drawable, 2 skipped, 3 failed command buffers, "
		"4 readbacks, 5 no-op calls, 1 live objects";
	size_t n;

	memset(&l, 0, sizeof(l));
	rm_ledger_create(&l, RM_OBJ_BUFFER);
	s.frames = 23;
	s.no_drawable = 1;
	s.skipped = 2;
	s.failed = 3;
	s.readbacks = 4;
	s.noop_calls = 5;
	CHECK(rm_stats_format(&s, &l, buf, sizeof(buf)) && strcmp(buf, want) == 0);

	n = strlen(want);
	CHECK(rm_stats_format(&s, &l, buf, n + 1) && strcmp(buf, want) == 0);
	CHECK(!rm_stats_format(&s, &l, buf, n) && strlen(buf) == n - 1);
}

/*
 * The colour load of a pass (gl1 clears colour only in R_Clear at the
 * start of the frame): the r_clear colour when the pass clears,
 * otherwise what the target holds, or black when no pass has written
 * the target since it was made.
 */
static void
test_color_load(void)
{
	CHECK(rm_color_load(1, 0) == RM_LOAD_CLEAR);
	CHECK(rm_color_load(1, 1) == RM_LOAD_CLEAR);
	CHECK(rm_color_load(0, 1) == RM_LOAD_KEEP);
	CHECK(rm_color_load(0, 0) == RM_LOAD_BLACK);
}

/*
 * With len 0 the formatters write nothing: neither the byte of a one
 * byte heap block (which must stay as it was) nor the byte just past
 * its end (which ASAN reports).
 */
static void
test_len_zero(void)
{
	rmb_stats_t s;
	rm_ledger_t l;
	char *block = malloc(1);

	CHECK(block != NULL);

	if (!block)
	{
		return;
	}

	memset(&s, 0, sizeof(s));
	memset(&l, 0, sizeof(l));

	block[0] = 'x';
	CHECK(!rm_caps_family_list(0x1ff, block, 0) && block[0] == 'x');
	CHECK(!rm_ledger_format(&l, block, 0) && block[0] == 'x');
	CHECK(!rm_stats_format(&s, &l, block, 0) && block[0] == 'x');

	CHECK(!rm_caps_family_list(0x1ff, block + 1, 0));
	CHECK(!rm_ledger_format(&l, block + 1, 0));
	CHECK(!rm_stats_format(&s, &l, block + 1, 0));

	free(block);
}

int
main(void)
{
	test_readback();
	test_floor();
	test_family_list();
	test_ledger();
	test_stats();
	test_color_load();
	test_len_zero();

	printf("rm_host_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
