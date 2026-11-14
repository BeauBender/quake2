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

#include "../../src/client/refresh/ref_shared.h"
#include "../../src/client/refresh/metal/header/rmb.h"
#include "../../src/client/refresh/metal/common/mtl_matrix.h"
#include "../../src/client/refresh/metal/common/mtl_ring.h"

/* tests/metal/rm_host_shim.c: the shim's mgl_compute_raster for glViewport(vx, vy, vw, vh) */
void rm_host_shim_raster(int vx, int vy, int vw, int vh, int tw, int th, double *out);

/* src/common/shared/shared.c, linked for files/surf.c, prints through the engine */
void
Com_Printf(const char *msg, ...)
{
	(void)msg;
}

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
		"10 pass splits, 12 pipelines, 0 created after init, 1 live objects, "
		"skipped surfaces: 13 sky, 14 warped, 15 translucent, 16 flowing";
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
	s.sky_skips = 13;
	s.warp_skips = 14;
	s.trans_skips = 15;
	s.flow_skips = 16;
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

	/* a textured pipeline needs a texture: REPLACE and MODULATE do, UNTEXTURED does not */
	for (e = 0; e < RM_TEXENVS; e++)
	{
		for (a = 0; a < 2; a++)
		{
			for (b = 0; b < 2; b++)
			{
				CHECK(rm_pipe_2d_textured(rm_pipe_2d((rm_texenv_t)e, a, b)) == (e != RM_TEXENV_UNTEXTURED));
			}
		}
	}

	CHECK(rm_pipe_2d_textured(-1) == 0 && rm_pipe_2d_textured(RM_PIPE_2D_COUNT) == 0);
	CHECK(rm_pipe_2d_textured(RM_PIPE_2D_COUNT + 1) == 0 && rm_pipe_2d_textured(1 << 20) == 0);
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

/*
 * gl1's per-texture filter: R_LoadPic gives an image on the no-lerp lists
 * GL_NEAREST, mipmapped or not; R_TextureMode then gives a mipmapped image
 * gl_filter_min and gl_filter_max whatever the lists say, and one without
 * mipmaps GL_NEAREST when listed, gl_filter_max otherwise.
 */
static void
test_image_sampler(void)
{
	CHECK(rm_image_sampler(1, 0, 0) == RMB_SAMPLER(RMB_SMP_MIP, 0));
	CHECK(rm_image_sampler(1, 1, 0) == RMB_SAMPLER(RMB_SMP_NEAREST, 0));
	CHECK(rm_image_sampler(1, 0, 1) == RMB_SAMPLER(RMB_SMP_MIP, 0));
	CHECK(rm_image_sampler(1, 1, 1) == RMB_SAMPLER(RMB_SMP_MIP, 0));
	CHECK(rm_image_sampler(0, 0, 0) == RMB_SAMPLER(RMB_SMP_FLAT, 0));
	CHECK(rm_image_sampler(0, 1, 0) == RMB_SAMPLER(RMB_SMP_NEAREST, 0));
	CHECK(rm_image_sampler(0, 0, 1) == RMB_SAMPLER(RMB_SMP_FLAT, 0));
	CHECK(rm_image_sampler(0, 1, 1) == RMB_SAMPLER(RMB_SMP_NEAREST, 0));
}

/* gl_texturemode by gl1's names, case-insensitive as Q_stricmp */
static void
test_texture_mode(void)
{
	int min = -1, mag = -1;

	CHECK(rm_texture_mode("GL_LINEAR_MIPMAP_NEAREST", &min, &mag) && min == 3 && mag == 1);
	CHECK(rm_texture_mode("GL_LINEAR", &min, &mag) && min == 1 && mag == 1);
	CHECK(rm_texture_mode("GL_NEAREST_MIPMAP_NEAREST", &min, &mag) && min == 2 && mag == 0);
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
 * How many of quads 2D quads fit in a frame ring of bytes, queued in
 * batches of RM_BATCH_QUADS and allocated as rmb_draw_2d_idx does: the
 * vertices, then the indices, of each draw. The quads after the first
 * draw that does not fit are skipped.
 */
static long
ring_quads(size_t bytes, long quads)
{
	mtl_ring_t r;
	long placed = 0;

	r.size = bytes;
	r.used = 0;

	while (placed < quads)
	{
		long n = quads - placed < RM_BATCH_QUADS ? quads - placed : RM_BATCH_QUADS;
		size_t used = r.used;

		if ((mtl_ring_alloc(&r, (size_t)n * 4 * sizeof(rmb_vert2d_t)) < 0)
				|| (mtl_ring_alloc(&r, (size_t)n * 6 * sizeof(unsigned short)) < 0))
		{
			/* the draw is skipped: whatever its vertices took is not given back, as in rmb_draw_2d_idx */
			CHECK(r.used >= used && r.used <= r.size);
			break;
		}

		placed += n;
	}

	return placed;
}

/*
 * The frame ring against the most characters one frame can queue, a full
 * console (cl_console.c): its text is at most CON_TEXTSIZE (32768)
 * characters at any resolution and console scale (con.totallines is
 * CON_TEXTSIZE / con.linewidth), plus the input line, the download bar and
 * the backscroll arrows (at most a line each, con.linewidth at most
 * MAXCMDLINE - 2 = 1022) and the 28 characters of the version and the
 * date. All of them are characters of one sheet, so they batch. At 4K
 * with r_consolescale 1 the line is 478 characters and the text 68 lines:
 * 32504 + 3 * 478 + 28 = 33966 quads. The old 4 MiB ring held 20 batches, 20480.
 */
static void
test_ring_console(void)
{
	const long console = 32768 + 3 * 1022 + 28;
	const long console_4k = 68 * 478 + 3 * 478 + 28;

	CHECK(ring_quads(RMB_RING_BYTES, console) == console);
	CHECK(ring_quads(RMB_RING_BYTES, console_4k) == console_4k);
	CHECK(ring_quads(4u << 20, console_4k) == 20480);
	/* a frame that exhausts the ring places what fits, then skips: 8 MiB holds 40 batches */
	CHECK(ring_quads(RMB_RING_BYTES, 100000) == 40 * RM_BATCH_QUADS);
	CHECK(ring_quads(RMB_RING_BYTES, 1) == 1);
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
	CHECK(rm_color_load(1, 0, 0) == RM_LOAD_CLEAR);
	CHECK(rm_color_load(1, 1, 1) == RM_LOAD_CLEAR);
	CHECK(rm_color_load(0, 1, 0) == RM_LOAD_KEEP);
	CHECK(rm_color_load(0, 0, 0) == RM_LOAD_BLACK);
	/* a frame gl1 does not clear starts black, as the shim's first pass does, not on the last frame */
	CHECK(rm_color_load(0, 1, 1) == RM_LOAD_BLACK);
	CHECK(rm_color_load(0, 0, 1) == RM_LOAD_BLACK);
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

/*
 * The scrap allocator on gl1's sequence (worked by hand from
 * CommonAllocBlock): blocks of 10 by 10, 20 by 30 and 8 by 40 go to (0, 0),
 * (10, 0) and (30, 0); a 250 by 5 block (0, 40), another (0, 45); 250 by 206
 * fills the atlas down to 256, and then nothing fits, not even 6 by 1,
 * because gl1's loop never starts a block in the last w columns (a 1 by 1
 * block still goes to (250, 0)). Blocks
 * that do not fit or are larger than the atlas write nothing.
 */
static void
test_scrap_alloc(void)
{
	static const int blocks[][5] = {
		/* w, h, fits, x, y */
		{ 10, 10, 1, 0, 0 },
		{ 20, 30, 1, 10, 0 },
		{ 8, 40, 1, 30, 0 },
		{ 250, 5, 1, 0, 40 },
		{ 250, 5, 1, 0, 45 },
		{ 250, 211, 0, -7, -7 },
		{ 250, 206, 1, 0, 50 },
		{ 6, 1, 0, -7, -7 },
		{ 1, 1, 1, 250, 0 }
	};
	int allocated[RM_SCRAP_WIDTH], saved[RM_SCRAP_WIDTH];
	size_t i;
	int x, y, ok = 1;

	memset(allocated, 0, sizeof(allocated));

	for (i = 0; i < sizeof(blocks) / sizeof(blocks[0]); i++)
	{
		x = y = -7;
		memcpy(saved, allocated, sizeof(saved));

		if (rm_scrap_alloc(allocated, RM_SCRAP_WIDTH, RM_SCRAP_HEIGHT, blocks[i][0], blocks[i][1], &x, &y)
				!= blocks[i][2] || x != blocks[i][3] || y != blocks[i][4])
		{
			ok = 0;
		}

		if (!blocks[i][2] && memcmp(saved, allocated, sizeof(saved)) != 0)
		{
			ok = 0;
		}
	}

	CHECK(ok);
	/* the 250 wide block raised its columns to 256, the 1 by 1 one column to 1 */
	CHECK(allocated[0] == 256 && allocated[249] == 256 && allocated[250] == 1 && allocated[251] == 0);

	/* a fresh atlas: the first block is at the origin and raises exactly its columns */
	memset(allocated, 0, sizeof(allocated));
	CHECK(rm_scrap_alloc(allocated, RM_SCRAP_WIDTH, RM_SCRAP_HEIGHT, 130, 130, &x, &y) && x == 0 && y == 0);
	CHECK(allocated[0] == 130 && allocated[129] == 130 && allocated[130] == 0);
	/* a second of 130 by 130 does not fit in one atlas of 256 (130 + 130 > 256): it goes to the next */
	CHECK(!rm_scrap_alloc(allocated, RM_SCRAP_WIDTH, RM_SCRAP_HEIGHT, 130, 130, &x, &y));

	/* oversized, empty and negative blocks: refused, nothing written */
	memset(allocated, 0, sizeof(allocated));
	x = y = -7;
	CHECK(!rm_scrap_alloc(allocated, RM_SCRAP_WIDTH, RM_SCRAP_HEIGHT, 257, 1, &x, &y));
	CHECK(!rm_scrap_alloc(allocated, RM_SCRAP_WIDTH, RM_SCRAP_HEIGHT, 1000000, 1, &x, &y));
	CHECK(!rm_scrap_alloc(allocated, RM_SCRAP_WIDTH, RM_SCRAP_HEIGHT, 256, 1, &x, &y));
	CHECK(!rm_scrap_alloc(allocated, RM_SCRAP_WIDTH, RM_SCRAP_HEIGHT, 10, 257, &x, &y));
	CHECK(!rm_scrap_alloc(allocated, RM_SCRAP_WIDTH, RM_SCRAP_HEIGHT, 0, 5, &x, &y));
	CHECK(!rm_scrap_alloc(allocated, RM_SCRAP_WIDTH, RM_SCRAP_HEIGHT, -4, 5, &x, &y));
	CHECK(!rm_scrap_alloc(allocated, RM_SCRAP_WIDTH, RM_SCRAP_HEIGHT, 5, -4, &x, &y));
	CHECK(!rm_scrap_alloc(NULL, RM_SCRAP_WIDTH, RM_SCRAP_HEIGHT, 5, 5, &x, &y));
	CHECK(x == -7 && y == -7);

	for (i = 0; i < RM_SCRAP_WIDTH; i++)
	{
		ok = ok && allocated[i] == 0;
	}

	CHECK(ok);
}

/*
 * The atlas state read at draw time: an atlas is written whenever it is
 * dirty, a missing texture included (a failed upload freed it, the next
 * update makes it again with id 0); its texture and sampler are looked up
 * by the atlas index, never by an id an image kept.
 */
static void
test_scrap_state(void)
{
	/* a guard entry before the table: an index of -1 that is not refused reads 99 */
	const int guarded[RM_MAX_SCRAPS + 1] = { 99, 7, 0, 9, 12 };
	const int *tex = guarded + 1;

	CHECK(rm_scrap_needs_upload(1, 5));
	CHECK(rm_scrap_needs_upload(1, 0));
	CHECK(!rm_scrap_needs_upload(0, 5));
	CHECK(!rm_scrap_needs_upload(0, 0));

	CHECK(rm_scrap_texture(tex, RM_MAX_SCRAPS, 0) == 7);
	CHECK(rm_scrap_texture(tex, RM_MAX_SCRAPS, 1) == 0);
	CHECK(rm_scrap_texture(tex, RM_MAX_SCRAPS, 3) == 12);
	CHECK(rm_scrap_texture(tex, RM_MAX_SCRAPS, RM_MAX_SCRAPS) == 0);
	CHECK(rm_scrap_texture(tex, RM_MAX_SCRAPS, -1) == 0);
	CHECK(rm_scrap_texture(tex, 2, 2) == 0);
	CHECK(rm_scrap_texture(NULL, RM_MAX_SCRAPS, 0) == 0);

	/* gl1's Scrap_Update: the first two atlases nearest, the others filtered unless r_2D_unfiltered */
	CHECK(rm_scrap_sampler(0, 0) == RMB_SAMPLER(RMB_SMP_NEAREST, 0));
	CHECK(rm_scrap_sampler(RM_MAX_SCRAPS_NOLERP - 1, 0) == RMB_SAMPLER(RMB_SMP_NEAREST, 0));
	CHECK(rm_scrap_sampler(RM_MAX_SCRAPS_NOLERP, 0) == RMB_SAMPLER(RMB_SMP_FLAT, 0));
	CHECK(rm_scrap_sampler(RM_MAX_SCRAPS - 1, 0) == RMB_SAMPLER(RMB_SMP_FLAT, 0));
	CHECK(rm_scrap_sampler(RM_MAX_SCRAPS_NOLERP, 1) == RMB_SAMPLER(RMB_SMP_NEAREST, 0));
	CHECK(rm_scrap_sampler(0, 1) == RMB_SAMPLER(RMB_SMP_NEAREST, 0));
	/* an index outside the table: filtered, as a texture that is no atlas's */
	CHECK(rm_scrap_sampler(RM_MAX_SCRAPS - 1, 1) == RMB_SAMPLER(RMB_SMP_NEAREST, 0));
	CHECK(rm_scrap_sampler(RM_MAX_SCRAPS, 1) == RMB_SAMPLER(RMB_SMP_FLAT, 0));
	CHECK(rm_scrap_sampler(-1, 0) == RMB_SAMPLER(RMB_SMP_FLAT, 0));
}

/* The copy of a block into the atlas: placed by the row pitch, refused outside it. */
static void
test_scrap_copy(void)
{
	unsigned texels[8 * 8], pic[3 * 2] = { 1, 2, 3, 4, 5, 6 };
	unsigned zero[8 * 8];
	int i, ok = 1;

	memset(texels, 0, sizeof(texels));
	memset(zero, 0, sizeof(zero));
	CHECK(rm_scrap_copy(texels, 8, 8, 4, 5, 3, 2, pic));

	for (i = 0; i < 64; i++)
	{
		unsigned want = 0;

		if (i >= 5 * 8 + 4 && i <= 5 * 8 + 6)
		{
			want = (unsigned)(i - (5 * 8 + 4)) + 1;
		}

		if (i >= 6 * 8 + 4 && i <= 6 * 8 + 6)
		{
			want = (unsigned)(i - (6 * 8 + 4)) + 4;
		}

		ok = ok && texels[i] == want;
	}

	CHECK(ok);

	/* the last row and column fit exactly */
	CHECK(rm_scrap_copy(texels, 8, 8, 5, 6, 3, 2, pic) && texels[7 * 8 + 7] == 6);

	/* outside: one column, one row, negative, overflowing: nothing written */
	memset(texels, 0, sizeof(texels));
	CHECK(!rm_scrap_copy(texels, 8, 8, 6, 5, 3, 2, pic));
	CHECK(!rm_scrap_copy(texels, 8, 8, 4, 7, 3, 2, pic));
	CHECK(!rm_scrap_copy(texels, 8, 8, -1, 5, 3, 2, pic));
	CHECK(!rm_scrap_copy(texels, 8, 8, 4, -1, 3, 2, pic));
	CHECK(!rm_scrap_copy(texels, 8, 8, 2147483647, 0, 3, 2, pic));
	CHECK(!rm_scrap_copy(texels, 8, 8, 0, 0, 0, 2, pic));
	CHECK(!rm_scrap_copy(texels, 8, 8, 0, 0, 3, 0, pic));
	CHECK(!rm_scrap_copy(texels, 8, 8, 0, 0, 3, 2, NULL));
	CHECK(memcmp(texels, zero, sizeof(texels)) == 0);
}

#define QEQ(q, a, b, c, d, e, f, g, h) \
	((q).x0 == (a) && (q).y0 == (b) && (q).x1 == (c) && (q).y1 == (d) \
		&& (q).s0 == (e) && (q).t0 == (f) && (q).s1 == (g) && (q).t1 == (h))

/*
 * A character's cell, by gl1's formula, worked by hand: 'A' (65) is row 4,
 * column 1 of the 16 by 16 sheet. On a whole image the cell is
 * (0.0625, 0.25) to (0.125, 0.3125); on a scrap block (0.25, 0.25) to
 * (0.75, 0.75) the coordinates are cut into it. Spaces and characters above the
 * screen are not drawn.
 */
static void
test_char_quad(void)
{
	static const float whole[4] = { 0, 0, 1, 1 };
	static const float scrap[4] = { 0.25f, 0.25f, 0.75f, 0.75f };
	rm_quad_t q;

	CHECK(rm_char_quad(&q, 100, 20, 65, 2, whole) && QEQ(q, 100, 20, 116, 36, 0.0625f, 0.25f, 0.125f, 0.3125f));
	CHECK(rm_char_quad(&q, 100, 20, 65, 2, scrap) && QEQ(q, 100, 20, 116, 36, 0.28125f, 0.375f, 0.3125f, 0.40625f));
	/* a block that is not square (0.5 wide, 0.25 high): the rows are cut by the height */
	{
		static const float wide[4] = { 0.25f, 0.25f, 0.75f, 0.5f };

		CHECK(rm_char_quad(&q, 100, 20, 65, 2, wide) && QEQ(q, 100, 20, 116, 36, 0.28125f, 0.3125f, 0.3125f, 0.328125f));
	}
	/* a character is its low 8 bits, as gl1 masks it */
	CHECK(rm_char_quad(&q, 100, 20, 256 + 65, 2, whole) && QEQ(q, 100, 20, 116, 36, 0.0625f, 0.25f, 0.125f, 0.3125f));
	/* character 0: the first cell; 255: the last */
	CHECK(rm_char_quad(&q, 0, 0, 0, 1, whole) && QEQ(q, 0, 0, 8, 8, 0.0f, 0.0f, 0.0625f, 0.0625f));
	CHECK(rm_char_quad(&q, 0, 0, 255, 1, whole) && QEQ(q, 0, 0, 8, 8, 0.9375f, 0.9375f, 1.0f, 1.0f));
	/* spaces, in both halves of the sheet */
	CHECK(!rm_char_quad(&q, 0, 0, 32, 1, whole));
	CHECK(!rm_char_quad(&q, 0, 0, 160, 1, whole));
	/* clipped to the top of the screen: -7 still shows, -8 does not */
	CHECK(rm_char_quad(&q, 4, -7, 65, 1, whole) && q.y0 == -7 && q.y1 == 1);
	CHECK(!rm_char_quad(&q, 4, -8, 65, 1, whole));
	CHECK(!rm_char_quad(&q, 4, -100, 65, 1, whole));
}

/* A scaled pic and a stretched one: the corners are x + w * factor, the coordinates the image's. */
static void
test_pic_quad(void)
{
	static const float scrap[4] = { 0.25f, 0.5f, 0.5f, 0.75f };
	rm_quad_t q;

	rm_pic_quad(&q, 10, 20, 32, 16, 1.5f, scrap);
	CHECK(QEQ(q, 10, 20, 58, 44, 0.25f, 0.5f, 0.5f, 0.75f));
	rm_pic_quad(&q, -5, -6, 10, 10, 1.0f, scrap);
	CHECK(QEQ(q, -5, -6, 5, 4, 0.25f, 0.5f, 0.5f, 0.75f));
}

/*
 * Tile clear: a 64 by 64 pic over the box (10, 20, 100, 70): the second
 * column is clipped to 36 pixels (36/64 = 0.5625 of the width), the second
 * row to 6 (6/64 = 0.09375), the coordinates cut in proportion.
 */
static void
test_tile_quad(void)
{
	static const float whole[4] = { 0, 0, 1, 1 };
	static const float scrap[4] = { 0.25f, 0.5f, 0.5f, 0.75f };
	rm_quad_t q;

	CHECK(rm_tile_quad(&q, 10, 20, 100, 70, 64, 64, 0, 0, whole) && QEQ(q, 10, 20, 74, 84, 0.0f, 0.0f, 1.0f, 1.0f));
	CHECK(rm_tile_quad(&q, 10, 20, 100, 70, 64, 64, 64, 0, whole) && QEQ(q, 74, 20, 110, 84, 0.0f, 0.0f, 0.5625f, 1.0f));
	CHECK(rm_tile_quad(&q, 10, 20, 100, 70, 64, 64, 0, 64, whole) && QEQ(q, 10, 84, 74, 90, 0.0f, 0.0f, 1.0f, 0.09375f));
	CHECK(rm_tile_quad(&q, 10, 20, 100, 70, 64, 64, 64, 64, scrap)
			&& QEQ(q, 74, 84, 110, 90, 0.25f, 0.5f, 0.390625f, 0.5234375f));
	/* offsets outside the box, a pic without a size: no quad */
	CHECK(!rm_tile_quad(&q, 10, 20, 100, 70, 64, 64, 100, 0, whole));
	CHECK(!rm_tile_quad(&q, 10, 20, 100, 70, 64, 64, 0, 70, whole));
	CHECK(!rm_tile_quad(&q, 10, 20, 100, 70, 64, 64, -1, 0, whole));
	CHECK(!rm_tile_quad(&q, 10, 20, 100, 70, 0, 64, 0, 0, whole));
	CHECK(!rm_tile_quad(&q, 10, 20, 100, 70, 64, -3, 0, 0, whole));
	/* a pic that is not square (32 by 16) over (0, 0, 48, 28): the last column is 16 of 32, the last row 12 of 16 */
	CHECK(rm_tile_quad(&q, 0, 0, 48, 28, 32, 16, 32, 16, whole) && QEQ(q, 32, 16, 48, 28, 0.0f, 0.0f, 0.5f, 0.75f));
}

/* The batch's capacity check, with an oversized count and the overflow edges. */
static void
test_batch_fits(void)
{
	const long big = 0x7fffffffffffffffL;

	CHECK(rm_batch_fits(0, 1, RM_BATCH_QUADS));
	CHECK(rm_batch_fits(RM_BATCH_QUADS - 1, 1, RM_BATCH_QUADS));
	CHECK(!rm_batch_fits(RM_BATCH_QUADS, 1, RM_BATCH_QUADS));
	CHECK(rm_batch_fits(5, RM_BATCH_QUADS - 5, RM_BATCH_QUADS));
	CHECK(!rm_batch_fits(5, RM_BATCH_QUADS - 4, RM_BATCH_QUADS));
	/* oversized: more quads than the batch holds, even when empty */
	CHECK(!rm_batch_fits(0, RM_BATCH_QUADS + 1, RM_BATCH_QUADS));
	CHECK(!rm_batch_fits(0, big, RM_BATCH_QUADS));
	CHECK(!rm_batch_fits(big, 1, RM_BATCH_QUADS));
	CHECK(!rm_batch_fits(big, big, big));
	CHECK(!rm_batch_fits(0, 0, RM_BATCH_QUADS));
	CHECK(!rm_batch_fits(0, -1, RM_BATCH_QUADS));
	CHECK(!rm_batch_fits(-1, 1, RM_BATCH_QUADS));
}

/* The indices of a batch of quads: gl1's 0 1 2, 0 2 3 per quad, bounded by cap and by 16 bits. */
static void
test_quad_indices(void)
{
	static const unsigned short two[12] = { 0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7 };
	unsigned short idx[16], *big;
	int n;

	memset(idx, 0xee, sizeof(idx));
	CHECK(rm_quad_indices(2, idx, 12) == 12 && memcmp(idx, two, sizeof(two)) == 0 && idx[12] == 0xeeee);
	memset(idx, 0xee, sizeof(idx));
	CHECK(rm_quad_indices(2, idx, 11) == 0 && idx[0] == 0xeeee);
	CHECK(rm_quad_indices(0, idx, 16) == 0 && rm_quad_indices(-1, idx, 16) == 0);
	CHECK(rm_quad_indices(1, idx, -1) == 0);
	CHECK(rm_quad_indices(1, NULL, 6) == 0);
	CHECK(idx[0] == 0xeeee);

	/* 16384 quads use vertices 0 to 65535, the last the 16-bit index can name; one more would wrap */
	big = malloc(16384 * 6 * sizeof(*big));
	CHECK(big != NULL);

	if (big)
	{
		n = rm_quad_indices(16384, big, 16384 * 6);
		CHECK(n == 16384 * 6 && big[n - 1] == 65535 && big[n - 2] == 65534 && big[n - 3] == 65532);
		CHECK(rm_quad_indices(16385, big, 1 << 30) == 0);
		free(big);
	}
}

/* The tint of a coloured pic: the colour as given (unclamped, as the shim keeps it) with alpha 1. */
static void
test_tint(void)
{
	const float rgb[3] = { 0.25f, 0.5f, 1.5f };
	float out[4] = { 9, 9, 9, 9 };

	rm_tint_color(out, rgb);
	CHECK(out[0] == 0.25f && out[1] == 0.5f && out[2] == 1.5f && out[3] == 1.0f);
}

/* ---- the world (N4) ------------------------------------------------------------- */

static int
near(double a, double b, double eps)
{
	return a - b < eps && b - a < eps;
}

/* The mirrored flags and limits are q_shared.h's and files.h's; the material class of each flag. */
static void
test_material(void)
{
	CHECK(RM_TEX_SKY == SURF_SKY && RM_TEX_WARP == SURF_WARP && RM_TEX_TRANS33 == SURF_TRANS33
			&& RM_TEX_TRANS66 == SURF_TRANS66 && RM_TEX_FLOWING == SURF_FLOWING);
	CHECK(RM_MAX_MAP_FACES == MAX_MAP_FACES && RM_MAX_MAP_SURFEDGES == MAX_MAP_SURFEDGES
			&& RM_MAX_MAP_LEAFS == MAX_MAP_LEAFS && RM_MAX_MAP_AREAS == MAX_MAP_AREAS);

	CHECK(rm_material(0) == RM_MAT_WORLD);
	CHECK(rm_material(SURF_LIGHT | SURF_SLICK | SURF_NODRAW) == RM_MAT_WORLD);
	CHECK(rm_material(SURF_SKY) == RM_MAT_SKY);
	CHECK(rm_material(SURF_WARP) == RM_MAT_WATER);
	CHECK(rm_material(SURF_TRANS33) == RM_MAT_TRANSLUCENT);
	CHECK(rm_material(SURF_TRANS66) == RM_MAT_TRANSLUCENT);
	CHECK(rm_material(SURF_FLOWING) == RM_MAT_FLOWING);
	/* gl1's order: sky first, then the alpha chain, then the warped and the flowing draws */
	CHECK(rm_material(SURF_SKY | SURF_TRANS33 | SURF_WARP) == RM_MAT_SKY);
	CHECK(rm_material(SURF_TRANS66 | SURF_WARP | SURF_FLOWING) == RM_MAT_TRANSLUCENT);
	CHECK(rm_material(SURF_WARP | SURF_FLOWING) == RM_MAT_WATER);
}

/* The retained vertex: position at offset 0, w after it, one stride of 32 bytes; the record's normal. */
static void
test_wvert_record(void)
{
	const float pn[3] = { 0.0f, -0.6f, 0.8f };
	float n[3];

	CHECK(offsetof(rm_wvert_t, pos) == 0 && offsetof(rm_wvert_t, uv) == 16 && offsetof(rm_wvert_t, lmuv) == 24);
	CHECK(sizeof(rm_wvert_t) == RM_WVERT_STRIDE && RM_WVERT_STRIDE % 4 == 0);

	rm_surf_normal(pn, 0, n);
	CHECK(n[0] == 0.0f && n[1] == -0.6f && n[2] == 0.8f);
	rm_surf_normal(pn, SURF_PLANEBACK, n);
	CHECK(n[0] == 0.0f && n[1] == 0.6f && n[2] == -0.8f);
}

/*
 * The fan of gl1's R_SetBufferIndices into the static index buffer: a
 * 5-vertex polygon at vertex 10 and a 3-vertex one after it; too small a
 * capacity, a polygon of 2 and a base that would pass 32 bits write nothing.
 */
static void
test_world_fan(void)
{
	unsigned idx[16];
	const unsigned five[9] = { 10, 11, 12, 10, 12, 13, 10, 13, 14 };
	const unsigned three[3] = { 15, 16, 17 };

	memset(idx, 0xee, sizeof(idx));
	CHECK(rm_world_fan(10, 5, idx, 16) == 9 && memcmp(idx, five, sizeof(five)) == 0 && idx[9] == 0xeeeeeeee);
	CHECK(rm_world_fan(15, 3, idx + 9, 7) == 3 && memcmp(idx + 9, three, sizeof(three)) == 0 && idx[12] == 0xeeeeeeee);

	memset(idx, 0xee, sizeof(idx));
	CHECK(rm_world_fan(0, 5, idx, 8) == 0 && idx[0] == 0xeeeeeeee);
	CHECK(rm_world_fan(0, 2, idx, 16) == 0 && rm_world_fan(0, -1, idx, 16) == 0);
	CHECK(rm_world_fan(0xfffffffeul, 3, idx, 16) == 0 && idx[0] == 0xeeeeeeee);
	CHECK(rm_world_fan(0xfffffffcul, 3, idx, 16) == 3 && idx[2] == 0xfffffffeu);
}

/*
 * The retained-buffer arithmetic and its bounds (plan section 2, "Inputs and
 * bounds"): counts are added per polygon up to the loader's limits, and the
 * bytes are formed only when they fit; oversized counts and products that
 * would overflow are refused without changing anything.
 */
static void
test_world_sizes(void)
{
	rm_world_size_t s, big;
	size_t vb = 1, ib = 1, sb = 1;
	long i;

	memset(&s, 0, sizeof(s));
	CHECK(rm_world_add(&s, 5) && rm_world_add(&s, 3) && rm_world_add(&s, 4));
	CHECK(s.surfs == 3 && s.verts == 12 && s.indices == 3 * (3 + 1 + 2));
	CHECK(rm_world_bytes(&s, RMB_STATIC_MAX_BYTES, &vb, &ib, &sb));
	CHECK(vb == 12 * 32 && ib == 18 * 4 && sb == 3 * sizeof(rm_surfrec_t));

	/* a polygon of fewer than 3 vertices, or one that is negative, adds nothing */
	CHECK(!rm_world_add(&s, 2) && !rm_world_add(&s, -5) && !rm_world_add(&s, 0));
	CHECK(s.surfs == 3 && s.verts == 12);

	/* the vertex limit: exactly full, then one more refused */
	memset(&s, 0, sizeof(s));
	CHECK(rm_world_add(&s, RM_WORLD_MAX_VERTS - 3) && rm_world_add(&s, 3));
	CHECK(s.verts == RM_WORLD_MAX_VERTS && !rm_world_add(&s, 3) && s.verts == RM_WORLD_MAX_VERTS);
	CHECK(rm_world_bytes(&s, RMB_STATIC_MAX_BYTES, &vb, &ib, &sb) && vb == (size_t)RM_WORLD_MAX_VERTS * 32);

	/* a polygon one vertex longer than what is left is refused */
	memset(&s, 0, sizeof(s));
	CHECK(rm_world_add(&s, RM_WORLD_MAX_VERTS - 3) && !rm_world_add(&s, 4) && s.verts == RM_WORLD_MAX_VERTS - 3);

	/* the index limit on its own (a total near it with few vertices; rm_world_add alone cannot reach it) */
	memset(&s, 0, sizeof(s));
	s.indices = RM_WORLD_MAX_INDICES - 5;
	CHECK(!rm_world_add(&s, 4) && s.indices == RM_WORLD_MAX_INDICES - 5 && s.surfs == 0);
	CHECK(rm_world_add(&s, 3) && s.indices == RM_WORLD_MAX_INDICES - 2);

	/* a count near LONG_MAX is refused, not wrapped */
	memset(&s, 0, sizeof(s));
	CHECK(!rm_world_add(&s, 0x7fffffffffffffffL) && s.verts == 0 && s.indices == 0);

	/* the surface limit */
	memset(&s, 0, sizeof(s));

	for (i = 0; i < RM_MAX_MAP_FACES; i++)
	{
		rm_world_add(&s, 3);
	}

	CHECK(s.surfs == RM_MAX_MAP_FACES && !rm_world_add(&s, 3) && s.surfs == RM_MAX_MAP_FACES);

	/* counts past the limits, and products that overflow size_t, are refused with 0 bytes */
	memset(&big, 0, sizeof(big));
	big.verts = (unsigned long)-1 / 9;
	CHECK(!rm_world_bytes(&big, (size_t)-1, &vb, &ib, &sb) && vb == 0 && ib == 0 && sb == 0);
	memset(&big, 0, sizeof(big));
	big.indices = (unsigned long)-1 / 2;
	CHECK(!rm_world_bytes(&big, (size_t)-1, &vb, &ib, &sb) && ib == 0);
	memset(&big, 0, sizeof(big));
	big.surfs = RM_MAX_MAP_FACES + 1;
	CHECK(!rm_world_bytes(&big, (size_t)-1, &vb, &ib, &sb));
	/* inside the limits but past the buffer cap */
	memset(&big, 0, sizeof(big));
	big.verts = 1000;
	CHECK(!rm_world_bytes(&big, 1000 * 32 - 1, &vb, &ib, &sb) && vb == 0);
	CHECK(rm_world_bytes(&big, 1000 * 32, &vb, &ib, &sb) && vb == 32000);
}

/* The BSP index checks: ranges, lumps, a face's edges, a surfedge, a leaf. */
static void
test_bsp_bounds(void)
{
	CHECK(rm_range_ok(0, 0, 0) && rm_range_ok(2, 3, 5) && !rm_range_ok(2, 4, 5));
	CHECK(!rm_range_ok(-1, 1, 5) && !rm_range_ok(1, -1, 5) && !rm_range_ok(0x7fffffffffffffffL, 1, 5));
	CHECK(!rm_range_ok(1, 0x7fffffffffffffffL, 0x7fffffffffffffffL));

	CHECK(rm_lump_ok(160, 40, 200) && !rm_lump_ok(160, 41, 200) && !rm_lump_ok(-4, 4, 200));
	CHECK(!rm_lump_ok(4, -4, 200) && !rm_lump_ok(0x7fffffff, 0x7fffffff, 200));
	/* a lump's records are read as int and float fields: its offset is a multiple of 4 */
	CHECK(!rm_lump_ok(161, 4, 200) && !rm_lump_ok(162, 0, 200) && !rm_lump_ok(3, 1, 200) && rm_lump_ok(196, 4, 200));

	CHECK(rm_face_edges_ok(0, 3, 3) && rm_face_edges_ok(7, 5, 12) && !rm_face_edges_ok(8, 5, 12));
	CHECK(!rm_face_edges_ok(0, 2, 12) && !rm_face_edges_ok(-1, 3, 12) && !rm_face_edges_ok(0, -3, 12));
	CHECK(!rm_face_edges_ok(0x7fffffff, 0x7fff, 12));

	CHECK(rm_surfedge_ok(0, 1) && rm_surfedge_ok(9, 10) && rm_surfedge_ok(-9, 10));
	CHECK(!rm_surfedge_ok(10, 10) && !rm_surfedge_ok(-10, 10) && !rm_surfedge_ok(-2147483647L - 1, 10));

	CHECK(rm_leaf_ok(-1, 0, 4, 1) && rm_leaf_ok(3, 255, 4, 1) && !rm_leaf_ok(4, 0, 4, 1));
	CHECK(!rm_leaf_ok(-2, 0, 4, 1) && !rm_leaf_ok(0, 256, 4, 1) && !rm_leaf_ok(0, -1, 4, 1));
	CHECK(rm_leaf_ok(70000, 0, 0, 0));   /* without visibility the cluster indexes nothing */

	/* a leaf's contents may be anything but CONTENTS_NODE, the mark of a node */
	CHECK(rm_leaf_contents_ok(0) && rm_leaf_contents_ok(1) && rm_leaf_contents_ok(-2) && rm_leaf_contents_ok(0x7fffffff));
	CHECK(!rm_leaf_contents_ok(-1));

	/* the world has a node, and model 0 starts at one of them */
	CHECK(rm_world_head_ok(1, 1, 0) && rm_world_head_ok(5, 3, 4));
	CHECK(!rm_world_head_ok(0, 1, 0) && !rm_world_head_ok(0, 1, -1) && !rm_world_head_ok(2, 0, 0));
	CHECK(!rm_world_head_ok(2, 1, -1) && !rm_world_head_ok(2, 1, 2) && !rm_world_head_ok(-1, 1, 0));
}

/* nexttexinfo chains: in range, and each one ends or returns to its start (Mod_LoadTexinfo's count loop). */
static void
test_texinfo_chains(void)
{
	const int plain[3] = { 0, 0, -1 };
	const int ring[4] = { 1, 2, 0, 0 };     /* 0 -> 1 -> 2 -> end (0 is the end) */
	const int cycle[4] = { 0, 2, 3, 1 };    /* 1 -> 2 -> 3 -> 1 */
	const int lasso[4] = { 1, 2, 3, 2 };    /* 0 -> 1 -> 2 -> 3 -> 2: never back to 0, never ends */
	const int out[3] = { 0, 3, 0 };
	const int neg[3] = { -7, 0, 0 };

	CHECK(rm_texinfo_chains_ok(plain, 3) && rm_texinfo_chains_ok(ring, 4) && rm_texinfo_chains_ok(cycle, 4));
	CHECK(rm_texinfo_chains_ok(neg, 3) && rm_texinfo_chains_ok(plain, 0));
	CHECK(!rm_texinfo_chains_ok(lasso, 4));
	CHECK(!rm_texinfo_chains_ok(out, 3));
	CHECK(!rm_texinfo_chains_ok(plain, -1));
}

/* The visibility header and the bounded decompression of a PVS row. */
static void
test_vis(void)
{
	const unsigned char row[] = { 0x81, 0x00, 0x03, 0x7e };   /* 0x81, three zeros, 0x7e */
	const unsigned char want[5] = { 0x81, 0, 0, 0, 0x7e };
	const unsigned char longrun[] = { 0x01, 0x00, 0xff };     /* a run of 255 zeros in a row of 4 */
	unsigned char out[8];

	CHECK(rm_vis_header_ok(0, 4) && rm_vis_header_ok(2, 20) && !rm_vis_header_ok(2, 19));
	CHECK(!rm_vis_header_ok(-1, 100) && !rm_vis_header_ok(RM_MAX_MAP_LEAFS + 1, 0x7fffffff));
	CHECK(!rm_vis_header_ok(0x7fffffffffffffffL / 4, 0x7fffffffffffffffL) && !rm_vis_header_ok(0, 3));

	memset(out, 0xee, sizeof(out));
	CHECK(rm_decompress_vis(row, sizeof(row), 5, out, sizeof(out)) && memcmp(out, want, 5) == 0 && out[5] == 0xee);

	/* the zero run is cut at the end of the row: nothing past it is written */
	memset(out, 0xee, sizeof(out));
	CHECK(rm_decompress_vis(longrun, sizeof(longrun), 4, out, sizeof(out)) && out[0] == 1 && out[3] == 0 && out[4] == 0xee);

	/* input that ends inside the row, or a run without its count: all visible, never read past */
	memset(out, 0xee, sizeof(out));
	CHECK(!rm_decompress_vis(row, 2, 5, out, sizeof(out)) && out[0] == 0xff && out[4] == 0xff && out[5] == 0xee);
	CHECK(!rm_decompress_vis(row, 1, 5, out, sizeof(out)) && out[1] == 0xff);
	{
		/* a zero as the last byte, its count past the end: not read (ASAN sees a read of tail[2]) */
		const unsigned char tail[2] = { 0x81, 0x00 };

		CHECK(!rm_decompress_vis(tail, sizeof(tail), 5, out, sizeof(out)) && out[0] == 0xff);
	}
	/* a row longer than the output writes nothing */
	memset(out, 0xee, sizeof(out));
	CHECK(!rm_decompress_vis(row, sizeof(row), 9, out, sizeof(out)) && out[0] == 0xee && out[7] == 0xee);
}

/* Each surface in at most one node: a second claim, or a range outside the table, marks nothing. */
static void
test_mark_once(void)
{
	unsigned char owner[6];

	memset(owner, 0, sizeof(owner));
	CHECK(rm_mark_once(owner, 6, 0, 2) && rm_mark_once(owner, 6, 4, 2) && rm_mark_once(owner, 6, 2, 0));
	CHECK(owner[0] && owner[1] && !owner[2] && !owner[3] && owner[4] && owner[5]);
	CHECK(!rm_mark_once(owner, 6, 1, 2) && !owner[2]);
	CHECK(!rm_mark_once(owner, 6, 3, 4) && !owner[3]);
	CHECK(!rm_mark_once(owner, 6, -1, 1) && rm_mark_once(owner, 6, 2, 2));
}

/*
 * The node lump is a tree from node 0 before it is linked: a node named by
 * two parents, twice by one, a cycle back to node 0 or through another
 * node, and a child outside the nodes or the leafs are refused.
 */
static void
test_node_tree(void)
{
	unsigned char seen[4];
	const int tree[8] = { 1, 2, -1, -2, 3, -3, -4, -1 };      /* 0 -> 1, 2; 2 -> 3; leaves 0 to 3 */
	const int leaves[2] = { -1, -1 };                          /* one node, a leaf twice */
	const int shared[8] = { 1, 2, 3, -1, 3, -2, -3, -4 };      /* 3 is a child of 1 and of 2 */
	const int twice[4] = { 1, 1, -1, -2 };                     /* 1 is both children of 0 */
	const int to_root[4] = { 1, -1, 0, -2 };                   /* 0 -> 1 -> 0 */
	const int loop[6] = { 1, -1, 2, -2, 1, -3 };               /* 0 -> 1 -> 2 -> 1 */
	const int self[4] = { 1, -1, 1, -2 };                      /* 1 -> 1 */
	const int far_node[4] = { 2, -1, -1, -1 };
	const int far_leaf[4] = { 1, -5, -1, -1 };
	const int least[4] = { 1, -2147483647 - 1, -1, -1 };

	CHECK(rm_node_tree_ok(tree, 4, 4, seen) && rm_node_tree_ok(leaves, 1, 1, seen));
	CHECK(!rm_node_tree_ok(shared, 4, 4, seen));
	CHECK(!rm_node_tree_ok(twice, 2, 2, seen));
	CHECK(!rm_node_tree_ok(to_root, 2, 2, seen));
	CHECK(!rm_node_tree_ok(loop, 3, 3, seen) && !rm_node_tree_ok(self, 2, 2, seen));
	CHECK(!rm_node_tree_ok(far_node, 2, 4, seen) && !rm_node_tree_ok(far_leaf, 2, 4, seen));
	CHECK(rm_node_tree_ok(far_leaf, 2, 5, seen) && !rm_node_tree_ok(least, 2, 4, seen));
	CHECK(!rm_node_tree_ok(tree, 0, 4, seen) && !rm_node_tree_ok(tree, 4, 3, seen));
	/* the scratch is cleared on each call: the same valid tree passes twice */
	CHECK(rm_node_tree_ok(tree, 4, 4, seen) && rm_node_tree_ok(tree, 4, 4, seen));
}

/*
 * A model slot is handed out only when its load finished: the name alone
 * (a loader dropped past Mod_ForName) or memory without the flag is an
 * unfinished load, to be freed before the next one.
 */
static void
test_slot_state(void)
{
	CHECK(rm_slot_state(0, 0, 0) == RM_SLOT_EMPTY);
	CHECK(rm_slot_state(1, 1, 1) == RM_SLOT_LOADED && rm_slot_state(1, 1, 0) == RM_SLOT_LOADED);
	CHECK(rm_slot_state(1, 0, 0) == RM_SLOT_UNFINISHED && rm_slot_state(1, 0, 1) == RM_SLOT_UNFINISHED);
	CHECK(rm_slot_state(0, 0, 1) == RM_SLOT_UNFINISHED);
	CHECK(rm_slot_state(0, 1, 0) == RM_SLOT_UNFINISHED && rm_slot_state(0, 1, 1) == RM_SLOT_UNFINISHED);
}

/*
 * The 3D viewport of gl1's R_SetupGL on the target equals the shim's
 * mgl_compute_raster of the same glViewport, for the full view and for a
 * smaller centred one (viewsize below 100).
 */
static void
test_view_rect(void)
{
	static const int rects[3][4] = { { 0, 0, 640, 480 }, { 64, 48, 512, 384 }, { 33, 7, 101, 55 } };
	rm_viewport_t vp;
	double shim[6];
	int i;

	for (i = 0; i < 3; i++)
	{
		const int *r = rects[i];
		int x = floor(r[0] * 640 / (float)640), x2 = ceil((r[0] + r[2]) * 640 / (float)640);
		int y = floor(480 - r[1] * 480 / (float)480), y2 = ceil(480 - (r[1] + r[3]) * 480 / (float)480);

		rm_view_rect(r[0], r[1], r[2], r[3], 640, 480, 480, &vp);
		rm_host_shim_raster(x, y2, x2 - x, y - y2, 640, 480, shim);
		CHECK(vp.x == shim[0] && vp.y == shim[1] && vp.w == shim[2] && vp.h == shim[3]);
		CHECK(vp.znear == shim[4] && vp.zfar == shim[5]);
	}

	/* hand-worked: the 64, 48 view of 512 by 384 starts 48 rows from the top in Metal */
	rm_view_rect(64, 48, 512, 384, 640, 480, 480, &vp);
	CHECK(vp.x == 64 && vp.y == 48 && vp.w == 512 && vp.h == 384 && vp.znear == 0 && vp.zfar == 1);
	rm_view_rect(10, 20, 100, 50, 640, 480, 480, &vp);
	CHECK(vp.x == 10 && vp.y == 20 && vp.w == 100 && vp.h == 50);
}

/* gl1's R_SetupGL modelview: as raw float bits through mtl_matrix.c, and what it does to a point. */
static void
test_view_matrix(void)
{
	const float angles[3] = { 10.0f, 30.0f, -5.0f }, org[3] = { 100.0f, -20.0f, 48.0f };
	const float zero[3] = { 0, 0, 0 }, o2[3] = { 1, 2, 3 };
	float m[16], want[16], r[16];
	float eye[3];
	int i;

	mtl_mat_identity(want);
	mtl_mat_rotate(r, -90, 1, 0, 0); mtl_mat_mul(want, want, r);
	mtl_mat_rotate(r, 90, 0, 0, 1); mtl_mat_mul(want, want, r);
	mtl_mat_rotate(r, -angles[2], 1, 0, 0); mtl_mat_mul(want, want, r);
	mtl_mat_rotate(r, -angles[0], 0, 1, 0); mtl_mat_mul(want, want, r);
	mtl_mat_rotate(r, -angles[1], 0, 0, 1); mtl_mat_mul(want, want, r);
	mtl_mat_translate(r, -org[0], -org[1], -org[2]); mtl_mat_mul(want, want, r);

	memset(m, 0x5a, sizeof(m));
	rm_view_matrix(m, angles, org);
	CHECK(memcmp(m, want, sizeof(m)) == 0);

	/* with no angles a point 10 units ahead (+x) is 10 down GL's -z, up (+z) is +y, left (+y) is -x */
	rm_view_matrix(m, zero, o2);

	for (i = 0; i < 3; i++)
	{
		eye[i] = m[i] * 11 + m[4 + i] * 2 + m[8 + i] * 3 + m[12 + i];
	}

	CHECK(near(eye[0], 0, 1e-5) && near(eye[1], 0, 1e-5) && near(eye[2], -10, 1e-5));

	for (i = 0; i < 3; i++)
	{
		eye[i] = m[i] * 1 + m[4 + i] * 7 + m[8 + i] * 8 + m[12 + i];
	}

	CHECK(near(eye[0], -5, 1e-5) && near(eye[1], 5, 1e-5) && near(eye[2], 0, 1e-5));

	/* the screen blend's modelview: the first two rotations alone */
	mtl_mat_identity(want);
	mtl_mat_rotate(r, -90, 1, 0, 0); mtl_mat_mul(want, want, r);
	mtl_mat_rotate(r, 90, 0, 0, 1); mtl_mat_mul(want, want, r);
	rm_blend_matrix(m);
	CHECK(memcmp(m, want, sizeof(m)) == 0);
}

/* gl1's R_SetPerspective, with and without r_farsee and the under-water wobble of gl1_waterwarp. */
static void
test_projection(void)
{
	float m[16], want[16], id[16];
	double ymax, xmax, warp;

	/* 90 degrees on 640 by 480, gl_znear 4 */
	ymax = 4.0 * tan(90.0f * M_PI / 360.0);
	xmax = ymax * ((double)640 / 480);
	mtl_mat_identity(id);
	mtl_mat_frustum(want, -xmax, xmax, -ymax, ymax, 4.0, 4096.0);
	mtl_mat_mul(want, id, want);
	CHECK(rm_projection(m, 90.0f, 640, 480, 4.0f, 0, 0, 1.0f, 1.0f) && memcmp(m, want, sizeof(m)) == 0);
	CHECK(near(m[0], 0.75, 1e-6) && near(m[5], 1, 1e-6) && m[11] == -1.0f);
	CHECK(near(m[10], -4100.0 / 4092.0, 1e-6) && near(m[14], -2.0 * 4096 * 4 / 4092.0, 1e-4));

	/* r_farsee: zFar 8192; gl_znear below 0.1 is 0.1 */
	CHECK(rm_projection(m, 90.0f, 640, 480, 4.0f, 1, 0, 1.0f, 1.0f) && near(m[10], -8196.0 / 8188.0, 1e-6));
	CHECK(rm_projection(m, 90.0f, 640, 480, 0.0f, 0, 0, 1.0f, 1.0f) && near(m[14], -2.0 * 4096 * 0.1 / (4096 - 0.1), 1e-5));

	/* under water at time 2: ymax shrinks and xmax grows by sin(3) * 0.03 */
	warp = sin(2.0f * 1.5) * 0.03 * 1.0f;
	ymax = 4.0 * tan(90.0f * M_PI / 360.0);
	xmax = ymax * ((double)640 / 480);
	ymax *= 1.0 - warp;
	xmax *= 1.0 + warp;
	mtl_mat_frustum(want, -xmax, xmax, -ymax, ymax, 4.0, 4096.0);
	mtl_mat_mul(want, id, want);
	CHECK(rm_projection(m, 90.0f, 640, 480, 4.0f, 0, 1, 2.0f, 1.0f) && memcmp(m, want, sizeof(m)) == 0);
	CHECK(!near(m[5], 1, 1e-4));
	/* gl1_waterwarp 0: no wobble under water */
	CHECK(rm_projection(m, 90.0f, 640, 480, 4.0f, 0, 1, 2.0f, 0.0f) && near(m[5], 1, 1e-6));

	/* glFrustum refuses a zero height: m is left as it was */
	memset(m, 0x5a, sizeof(m));
	CHECK(!rm_projection(m, 90.0f, 640, 0, 4.0f, 0, 0, 1.0f, 1.0f) && ((unsigned char *)m)[0] == 0x5a);
}

/* R_PolyBlend: the colour times sw_gamma, the alpha as given; no quad for gl_polyblend 0 or alpha 0. */
static void
test_polyblend(void)
{
	const float blend[4] = { 0.5f, 0.25f, 1.0f, 0.3f }, clear[4] = { 0.5f, 0.25f, 1.0f, 0.0f };
	float rgba[4] = { 9, 9, 9, 9 };

	CHECK(rm_polyblend(blend, 1.2f, 1.0f, rgba));
	CHECK(rgba[0] == 0.5f * 1.2f && rgba[1] == 0.25f * 1.2f && rgba[2] == 1.0f * 1.2f && rgba[3] == 0.3f);
	CHECK(!rm_polyblend(blend, 1.2f, 0.0f, rgba));
	CHECK(!rm_polyblend(clear, 1.2f, 1.0f, rgba));
}

/*
 * What ref_metal takes from files/surf.c unchanged: the frustum of
 * R_SetFrustum against hand-worked planes, R_CullBox on boxes in front,
 * behind and beside, and R_TextureAnimation's frame of a chain.
 */
static void
test_frustum_cull(void)
{
	vec3_t vpn, vright, vup, org = { 0, 0, 0 }, org2 = { 100, 0, 0 }, ang = { 0, 0, 0 };
	vec3_t a0 = { 10, -1, -1 }, a1 = { 20, 1, 1 };     /* ahead */
	vec3_t b0 = { -20, -1, -1 }, b1 = { -10, 1, 1 };   /* behind */
	vec3_t c0 = { 10, 50, 0 }, c1 = { 11, 60, 1 };     /* left, outside 45 degrees */
	vec3_t d0 = { -5, -5, -5 }, d1 = { 5, 5, 5 };      /* around the eye */
	vec3_t e0 = { 10, -1, 50 }, e1 = { 11, 1, 60 };    /* above, outside the top plane only */
	cplane_t f[4];
	const double h = sqrt(0.5);

	AngleVectors(ang, vpn, vright, vup);
	R_SetFrustum(vup, vpn, vright, org, 90, 90, f);

	CHECK(near(f[0].normal[0], h, 1e-6) && near(f[0].normal[1], -h, 1e-6) && near(f[0].normal[2], 0, 1e-6));
	CHECK(near(f[1].normal[0], h, 1e-6) && near(f[1].normal[1], h, 1e-6) && near(f[1].normal[2], 0, 1e-6));
	CHECK(near(f[2].normal[0], h, 1e-6) && near(f[2].normal[1], 0, 1e-6) && near(f[2].normal[2], h, 1e-6));
	CHECK(near(f[3].normal[0], h, 1e-6) && near(f[3].normal[1], 0, 1e-6) && near(f[3].normal[2], -h, 1e-6));
	CHECK(f[0].dist == 0 && f[0].type == PLANE_ANYZ && f[0].signbits == 2 && f[3].signbits == 4);

	CHECK(!R_CullBox(a0, a1, f) && R_CullBox(b0, b1, f) && R_CullBox(c0, c1, f) && !R_CullBox(d0, d1, f));
	CHECK(R_CullBox(e0, e1, f));

	/* from 100 units further on, the plane distances move and the box ahead is now behind */
	R_SetFrustum(vup, vpn, vright, org2, 90, 90, f);
	CHECK(near(f[1].dist, 100 * h, 1e-4) && R_CullBox(a0, a1, f));
}

static void
test_texture_animation(void)
{
	struct image_s *imgs[3] = { (struct image_s *)&imgs[0], (struct image_s *)&imgs[1], (struct image_s *)&imgs[2] };
	mtexinfo_t t[3];
	entity_t e;
	int i;

	memset(t, 0, sizeof(t));
	memset(&e, 0, sizeof(e));

	for (i = 0; i < 3; i++)
	{
		t[i].image = imgs[i];
		t[i].next = &t[(i + 1) % 3];
		t[i].numframes = 3;
	}

	e.frame = 4;    /* 4 % 3: one step on */
	CHECK(R_TextureAnimation(&e, &t[0]) == imgs[1] && R_TextureAnimation(&e, &t[2]) == imgs[0]);
	e.frame = 6;
	CHECK(R_TextureAnimation(&e, &t[1]) == imgs[1]);
	e.frame = -5;   /* a negative frame is the first */
	CHECK(R_TextureAnimation(&e, &t[0]) == imgs[0]);
	CHECK(R_TextureAnimation(NULL, &t[0]) == imgs[0]);
	t[0].next = NULL;
	e.frame = 1;
	CHECK(R_TextureAnimation(&e, &t[0]) == imgs[0]);
}

/* The per-map line of metal_stats, and the skipped-surface counts of the per-frame line. */
static void
test_world_line(void)
{
	rm_world_size_t s;
	char buf[RM_STATS_LINE];
	const char *want = "ref_metal: metal_stats: world maps/base1.bsp: 12 vertices (384 bytes), "
		"18 indices (72 bytes), 3 surface records (144 bytes)";
	size_t n = strlen(want);

	s.surfs = 3;
	s.verts = 12;
	s.indices = 18;
	CHECK(rm_world_format("maps/base1.bsp", &s, buf, sizeof(buf)) && strcmp(buf, want) == 0);
	CHECK(rm_world_format("maps/base1.bsp", &s, buf, n + 1) && !rm_world_format("maps/base1.bsp", &s, buf, n));

	/* the longest map name (MAX_QPATH) and counts fit RM_STATS_LINE; counts past the limits print nothing */
	{
		char name[MAX_QPATH];

		memset(name, 'x', sizeof(name) - 1);
		name[sizeof(name) - 1] = '\0';
		s.surfs = RM_MAX_MAP_FACES;
		s.verts = RM_WORLD_MAX_VERTS;
		s.indices = RM_WORLD_MAX_INDICES;
		CHECK(rm_world_format(name, &s, buf, sizeof(buf)) && strlen(buf) < sizeof(buf));
		s.verts++;
		CHECK(!rm_world_format(name, &s, buf, sizeof(buf)) && buf[0] == '\0');
	}
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
	test_image_sampler();
	test_dir_of();
	test_ortho();
	test_ring();
	test_ring_console();
	test_len_zero();
	test_scrap_alloc();
	test_scrap_copy();
	test_scrap_state();
	test_char_quad();
	test_pic_quad();
	test_tile_quad();
	test_batch_fits();
	test_quad_indices();
	test_tint();
	test_material();
	test_wvert_record();
	test_world_fan();
	test_world_sizes();
	test_bsp_bounds();
	test_texinfo_chains();
	test_vis();
	test_mark_once();
	test_slot_state();
	test_node_tree();
	test_view_rect();
	test_view_matrix();
	test_projection();
	test_polyblend();
	test_frustum_cull();
	test_texture_animation();
	test_world_line();

	printf("rm_host_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
