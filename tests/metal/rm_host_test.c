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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/client/refresh/metal/header/rmb.h"
#include "../../src/client/refresh/metal/common/mtl_matrix.h"
#include "../../src/client/refresh/metal/common/mtl_ring.h"

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
	char buf[RM_STATS_LINE];
	const char *want =
		"ref_metal: metal_stats: 23 frames, 1 without a drawable, 2 skipped, 3 failed command buffers, "
		"4 readbacks, 5 no-op calls, 6 draws, 7 draws skipped, 8 uploads, 9 uploads skipped, "
		"10 pass splits, 12 pipelines, 0 created after init, 1 live objects";
	size_t n;

	memset(&l, 0, sizeof(l));
	memset(&s, 0, sizeof(s));
	rm_ledger_create(&l, RM_OBJ_BUFFER);
	s.frames = 23;
	s.no_drawable = 1;
	s.skipped = 2;
	s.failed = 3;
	s.readbacks = 4;
	s.noop_calls = 5;
	s.draws = 6;
	s.draw_skips = 7;
	s.uploads = 8;
	s.upload_skips = 9;
	s.splits = 10;
	s.pipelines = 12;
	CHECK(rm_stats_format(&s, &l, buf, sizeof(buf)) && strcmp(buf, want) == 0);

	n = strlen(want);
	CHECK(rm_stats_format(&s, &l, buf, n + 1) && strcmp(buf, want) == 0);
	CHECK(!rm_stats_format(&s, &l, buf, n) && strlen(buf) == n - 1);

	/* the longest line (every counter at its largest) fits the callers' RM_STATS_LINE */
	memset(&s, 0xff, sizeof(s));
	memset(&l, 0, sizeof(l));
	l.unreleased = (unsigned long)-1;
	CHECK(rm_stats_format(&s, &l, buf, sizeof(buf)) && strlen(buf) < sizeof(buf));
}

/*
 * The size check of every upload (plan section 2, "Inputs and bounds"):
 * each side at most RMB_MAX_TEXTURE_SIDE and w * h * 4 bytes within the
 * staging ring, decided before the product is formed, so a product that
 * would overflow is refused too.
 */
static void
test_upload_bytes(void)
{
	size_t b = 1;

	CHECK(rm_upload_bytes(320, 240, RMB_STAGING_BYTES, &b) && b == 307200);
	CHECK(rm_upload_bytes(4096, 4096, RMB_STAGING_BYTES, &b) && b == RMB_STAGING_BYTES);
	CHECK(!rm_upload_bytes(4097, 4096, RMB_STAGING_BYTES, &b) && b == 0);
	CHECK(!rm_upload_bytes(4096, 4097, RMB_STAGING_BYTES, &b) && b == 0);
	CHECK(rm_upload_bytes(1, 1, 4, &b) && b == 4);
	CHECK(!rm_upload_bytes(1, 1, 3, &b));
	CHECK(!rm_upload_bytes(0, 240, RMB_STAGING_BYTES, &b));
	CHECK(!rm_upload_bytes(320, 0, RMB_STAGING_BYTES, &b));
	CHECK(!rm_upload_bytes(-320, -240, RMB_STAGING_BYTES, &b));
	/* the product of these wraps in 64 bits (2^32 * 2^32 * 4) and in 32 */
	CHECK(!rm_upload_bytes(1L << 32, 1L << 32, (size_t)-1, &b) && b == 0);
	CHECK(!rm_upload_bytes(65536, 65536, (size_t)-1 >> 31, &b));
	CHECK(!rm_upload_bytes(0x7fffffffL, 0x7fffffffL, RMB_STAGING_BYTES, &b));
	/* each side on its own: Metal refuses a texture side above its maximum by abort */
	CHECK(!rm_upload_bytes(20000, 1, RMB_STAGING_BYTES, &b) && b == 0);
	CHECK(!rm_upload_bytes(1, 20000, RMB_STAGING_BYTES, &b) && b == 0);
	CHECK(rm_upload_bytes(RMB_MAX_TEXTURE_SIDE, 1, RMB_STAGING_BYTES, &b) && b == RMB_MAX_TEXTURE_SIDE * 4);
	CHECK(rm_upload_bytes(1, RMB_MAX_TEXTURE_SIDE, RMB_STAGING_BYTES, &b) && b == RMB_MAX_TEXTURE_SIDE * 4);
	CHECK(rm_upload_bytes(RMB_MAX_TEXTURE_SIDE, 1024, RMB_STAGING_BYTES, &b) && b == RMB_STAGING_BYTES);
	CHECK(!rm_upload_bytes(RMB_MAX_TEXTURE_SIDE + 1, 1, RMB_STAGING_BYTES, &b) && b == 0);
	CHECK(!rm_upload_bytes(1, RMB_MAX_TEXTURE_SIDE + 1, RMB_STAGING_BYTES, &b) && b == 0);
	CHECK(!rm_upload_bytes(RMB_MAX_TEXTURE_SIDE + 1, 1, (size_t)-1, &b) && b == 0);
	CHECK(RMB_MAX_TEXTURE_SIDE <= 16384);
}

/*
 * The 8-bit cinematic frame (gl1's RDraw_StretchRaw on the non-power-of-
 * two path): each index becomes its r_rawpalette entry at the frame's
 * own size; a buffer of 320 by 240 texels (gl1's image32) holds the
 * largest frame gl1 converts without allocating, and one texel more is
 * refused with nothing written.
 */
static void
test_raw8(void)
{
	static unsigned char big[320 * 240 * 4], frame[320 * 240];
	unsigned palette[256];
	unsigned char *p = (unsigned char *)palette, out[3 * 2 * 4 + 4];
	const unsigned char data[6] = { 0, 1, 255, 2, 1, 0 };
	const unsigned char want[24] = {
		10, 20, 30, 255,  11, 21, 31, 255,  12, 22, 32, 255,
		13, 23, 33, 255,  11, 21, 31, 255,  10, 20, 30, 255
	};
	int i;

	for (i = 0; i < 256; i++)
	{
		p[i * 4 + 0] = (unsigned char)(10 + (i == 255 ? 2 : i));
		p[i * 4 + 1] = (unsigned char)(20 + (i == 255 ? 2 : i));
		p[i * 4 + 2] = (unsigned char)(30 + (i == 255 ? 2 : i));
		p[i * 4 + 3] = 0xff;
	}

	p[2 * 4 + 0] = 13;
	p[2 * 4 + 1] = 23;
	p[2 * 4 + 2] = 33;
	memset(out, 0x5a, sizeof(out));
	CHECK(rm_raw8_to_rgba(data, 3, 2, palette, out, 24));
	CHECK(memcmp(out, want, 24) == 0 && out[24] == 0x5a && out[27] == 0x5a);

	/* one row of three is not the same as three rows of one: the stride is cols */
	CHECK(rm_raw8_to_rgba(data, 1, 3, palette, out, 12) && memcmp(out, want, 4) == 0 &&
			out[4] == 11 && out[8] == 12);

	memset(out, 0x5a, sizeof(out));
	CHECK(!rm_raw8_to_rgba(data, 3, 2, palette, out, 23) && out[0] == 0x5a);
	CHECK(!rm_raw8_to_rgba(data, 0, 2, palette, out, sizeof(out)) && out[0] == 0x5a);

	memset(frame, 1, sizeof(frame));
	CHECK(rm_raw8_to_rgba(frame, 320, 240, palette, big, sizeof(big)) && big[sizeof(big) - 4] == 11);
	big[0] = 0x5a;
	CHECK(!rm_raw8_to_rgba(frame, 321, 240, palette, big, sizeof(big)) && big[0] == 0x5a);
	CHECK(!rm_raw8_to_rgba(frame, 320, 241, palette, big, sizeof(big)) && big[0] == 0x5a);

	/* a cinematic frame with a side over the texture maximum is refused though the buffer holds it */
	CHECK(!rm_raw8_to_rgba(frame, 20000, 1, palette, big, sizeof(big)) && big[0] == 0x5a);
	CHECK(!rm_raw8_to_rgba(frame, 1, 20000, palette, big, sizeof(big)) && big[0] == 0x5a);
	CHECK(!rm_raw8_to_rgba(frame, RMB_MAX_TEXTURE_SIDE + 1, 1, palette, big, sizeof(big)) && big[0] == 0x5a);
	CHECK(rm_raw8_to_rgba(frame, RMB_MAX_TEXTURE_SIDE, 1, palette, big, sizeof(big)) && big[0] == 11);
}

/* gl1's 2D quads are fans; the shim draws them as 0, i, i + 1 */
static void
test_fan(void)
{
	unsigned short idx[12];
	const unsigned short quad[6] = { 0, 1, 2, 0, 2, 3 };
	const unsigned short five[9] = { 0, 1, 2, 0, 2, 3, 0, 3, 4 };

	memset(idx, 0xee, sizeof(idx));
	CHECK(rm_fan_indices(4, idx, 12) == 6 && memcmp(idx, quad, sizeof(quad)) == 0 && idx[6] == 0xeeee);
	CHECK(rm_fan_indices(5, idx, 9) == 9 && memcmp(idx, five, sizeof(five)) == 0);
	CHECK(rm_fan_indices(3, idx, 3) == 3 && idx[0] == 0 && idx[1] == 1 && idx[2] == 2);
	memset(idx, 0xee, sizeof(idx));
	CHECK(rm_fan_indices(5, idx, 8) == 0 && idx[0] == 0xeeee);
	CHECK(rm_fan_indices(2, idx, 12) == 0 && rm_fan_indices(70000, idx, 12) == 0);
}

/* The 2D pipeline table: every combination one index, all inside the table. */
static void
test_pipe_2d(void)
{
	int seen[RM_PIPE_2D_COUNT], e, a, b, ok = 1;

	memset(seen, 0, sizeof(seen));

	for (e = 0; e < RM_TEXENVS; e++)
	{
		for (a = 0; a < 2; a++)
		{
			for (b = 0; b < 2; b++)
			{
				int i = rm_pipe_2d((rm_texenv_t)e, a, b);

				ok = ok && i >= 0 && i < RM_PIPE_2D_COUNT && !seen[i];

				if (i >= 0 && i < RM_PIPE_2D_COUNT)
				{
					seen[i] = 1;
				}
			}
		}
	}

	CHECK(ok);
	CHECK(rm_pipe_2d(RM_TEXENVS, 0, 0) == -1 && rm_pipe_2d((rm_texenv_t)-1, 0, 0) == -1);
}

/* The directory ref_metal.metallib is looked for in: the dylib's, bounded by the buffer */
static void
test_dir_of(void)
{
	char d[16];

	CHECK(rm_dir_of("/a/b/ref_metal.dylib", d, sizeof(d)) && strcmp(d, "/a/b") == 0);
	CHECK(rm_dir_of("ref_metal.dylib", d, sizeof(d)) && strcmp(d, ".") == 0);
	CHECK(rm_dir_of("/ref_metal.dylib", d, sizeof(d)) && strcmp(d, "/") == 0);
	/* "/abcdefghijklmn" is 15 bytes and its NUL the 16th: an exact fit, then one byte short */
	CHECK(rm_dir_of("/abcdefghijklmn/x", d, 16) && strcmp(d, "/abcdefghijklmn") == 0);
	memset(d, 'z', sizeof(d));
	CHECK(!rm_dir_of("/abcdefghijklmn/x", d, 15) && d[0] == '\0' && d[1] == 'z');
	d[0] = 'z';
	CHECK(!rm_dir_of("/a/x", d, 0) && d[0] == 'z');
}

/* gl_texturemode by gl1's names, case-insensitive as Q_stricmp */
static void
test_texture_mode(void)
{
	int min = -1, mag = -1;

	CHECK(rm_texture_mode("GL_LINEAR_MIPMAP_NEAREST", &min, &mag) && min == 3 && mag == 1);
	CHECK(rm_texture_mode("gl_nearest", &min, &mag) && min == 0 && mag == 0);
	CHECK(rm_texture_mode("GL_NEAREST_MIPMAP_LINEAR", &min, &mag) && min == 4 && mag == 0);
	CHECK(rm_texture_mode("GL_LINEAR_MIPMAP_LINEAR", &min, &mag) && min == 5 && mag == 1);
	min = mag = 7;
	CHECK(!rm_texture_mode("GL_LINEAR_MIPMAP", &min, &mag) && min == 7 && mag == 7);
	CHECK(!rm_texture_mode(NULL, &min, &mag));
}

/*
 * gl1's 2D projection, glOrtho(0, 640, 480, 0, -99999, 99999), through
 * common/mtl_matrix.c, as raw float bits: the values the shim's host
 * test expects (m[10] is (float)(-2.0 / 199998.0), m[14] the -0 of
 * -(99999 + -99999) / 199998), and the 2D product with the identity
 * modelview, which the shim hands its vertex function (0 + -0 is +0).
 */
static void
test_ortho(void)
{
	float m[16], ident[16], mvp[16];
	uint32_t bits[16], want[16];
	float f;
	int i;

	memset(want, 0, sizeof(want));
	f = (float)(2.0 / 640.0); memcpy(&want[0], &f, 4);
	f = (float)(2.0 / -480.0); memcpy(&want[5], &f, 4);
	f = (float)(-2.0 / 199998.0); memcpy(&want[10], &f, 4);
	f = -1.0f; memcpy(&want[12], &f, 4);
	f = 1.0f; memcpy(&want[13], &f, 4);
	want[14] = 0x80000000u;
	f = 1.0f; memcpy(&want[15], &f, 4);

	CHECK(mtl_mat_ortho(m, 0, 640, 480, 0, -99999, 99999));
	memcpy(bits, m, sizeof(bits));
	CHECK(memcmp(bits, want, sizeof(want)) == 0);
	CHECK(want[0] == 0x3b4ccccdu && want[5] == 0xbb888889u);

	mtl_mat_identity(ident);
	mtl_mat_mul(mvp, m, ident);
	want[14] = 0;
	memcpy(bits, mvp, sizeof(bits));
	CHECK(memcmp(bits, want, sizeof(want)) == 0);

	/* GL refuses an empty volume; m stays as it was */
	for (i = 0; i < 16; i++)
	{
		m[i] = 7.0f;
	}

	CHECK(!mtl_mat_ortho(m, 0, 0, 480, 0, -1, 1) && m[0] == 7.0f && m[15] == 7.0f);
}

/*
 * The ring allocator moved from the shim (common/mtl_ring.c): aligned
 * regions, and an allocation of more than the rest fails without moving
 * the head, including a count near SIZE_MAX that would wrap.
 */
static void
test_ring(void)
{
	mtl_ring_t r;

	r.size = 4 * MTL_RING_ALIGN;
	r.used = 0;
	CHECK(mtl_ring_alloc(&r, 10) == 0 && r.used == 10);
	CHECK(mtl_ring_alloc(&r, 1) == MTL_RING_ALIGN && r.used == MTL_RING_ALIGN + 1);
	CHECK(mtl_ring_alloc(&r, 3 * MTL_RING_ALIGN) == -1 && r.used == MTL_RING_ALIGN + 1);
	CHECK(mtl_ring_alloc(&r, (size_t)-1) == -1 && r.used == MTL_RING_ALIGN + 1);
	CHECK(mtl_ring_alloc(&r, 0) == -1 && r.used == MTL_RING_ALIGN + 1);
	CHECK(mtl_ring_alloc(&r, 2 * MTL_RING_ALIGN) == 2 * MTL_RING_ALIGN && r.used == r.size);
	CHECK(mtl_ring_alloc(&r, 1) == -1 && r.used == r.size);

	/* an image larger than the staging ring is refused by the ring as well */
	r.size = RMB_STAGING_BYTES;
	r.used = 0;
	CHECK(mtl_ring_alloc(&r, (size_t)RMB_STAGING_BYTES + 4) == -1 && r.used == 0);
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
 * The targets are made again when either is missing (a depth target
 * whose allocation failed is retried on the next frame, TASK84 review)
 * or the window changed size, and kept otherwise.
 */
static void
test_targets_rebuild(void)
{
	CHECK(!rm_targets_rebuild(1, 1, 640, 480, 640, 480));
	CHECK(rm_targets_rebuild(0, 0, 0, 0, 640, 480));
	CHECK(rm_targets_rebuild(1, 0, 640, 480, 640, 480));
	CHECK(rm_targets_rebuild(0, 1, 0, 0, 640, 480));
	CHECK(rm_targets_rebuild(1, 1, 640, 480, 641, 480));
	CHECK(rm_targets_rebuild(1, 1, 640, 480, 640, 479));
}

/*
 * With len 0 the formatters write nothing: not the byte of a one byte
 * heap block (which must stay as it was), and, given the middle byte of
 * a three byte block, neither that byte nor the one before it (where a
 * terminator at buf[len - 1] would land) nor the one after. The second
 * half judges by the bytes' values, so it holds without a sanitizer.
 */
static void
test_len_zero(void)
{
	rmb_stats_t s;
	rm_ledger_t l;
	char *block = malloc(1), *three = malloc(3);

	CHECK(block != NULL && three != NULL);

	if (!block || !three)
	{
		free(block);
		free(three);
		return;
	}

	memset(&s, 0, sizeof(s));
	memset(&l, 0, sizeof(l));

	block[0] = 'x';
	CHECK(!rm_caps_family_list(0x1ff, block, 0) && block[0] == 'x');
	CHECK(!rm_ledger_format(&l, block, 0) && block[0] == 'x');
	CHECK(!rm_stats_format(&s, &l, block, 0) && block[0] == 'x');

	memcpy(three, "xyz", 3);
	CHECK(!rm_caps_family_list(0x1ff, three + 1, 0) && memcmp(three, "xyz", 3) == 0);
	CHECK(!rm_ledger_format(&l, three + 1, 0) && memcmp(three, "xyz", 3) == 0);
	CHECK(!rm_stats_format(&s, &l, three + 1, 0) && memcmp(three, "xyz", 3) == 0);

	free(block);
	free(three);
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
	test_targets_rebuild();
	test_upload_bytes();
	test_raw8();
	test_fan();
	test_pipe_2d();
	test_texture_mode();
	test_dir_of();
	test_ortho();
	test_ring();
	test_len_zero();

	printf("rm_host_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
