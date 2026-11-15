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

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/client/refresh/ref_shared.h"
#include "../../src/client/refresh/metal/header/rmb.h"
#include "../../src/client/refresh/metal/common/mtl_matrix.h"
#include "../../src/client/refresh/metal/common/mtl_ring.h"
#include "../../src/client/refresh/metal/common/mtl_shader_math.h"

/* tests/metal/rm_host_shim.c: the shim's mgl_compute_raster for glViewport(vx, vy, vw, vh) */
void rm_host_shim_raster(int vx, int vy, int vw, int vh, int tw, int th, double *out);
/* the shim's two texture units on gl1's multitexture world draw */
void rm_host_shim_world_lm(int env1, float scale, int opaque0, const float *d, const float *l, float *out);
/* the shim's point arithmetic (mgl_point_size, mgl_point_quads), the original functions */
void rm_host_shim_point_limits(float *out);
void rm_host_shim_point_set(float size, float min, float max, float *out);
float rm_host_shim_point_size(const rm_points_t *p, const float *eye);
long rm_host_shim_point_quads(const rm_points_t *p, const float *mv, const float *proj, const rm_avert_t *in, long n,
		float (*pos)[4], float (*pt)[4], unsigned short *idx);

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
		"texture 3/2, buffer 0/0, state 0/0), 0 unreleased, 0 unmatched";
	const char *want_clean =
		"ref_metal: teardown: 0 live objects (created/destroyed: device 1/1, queue 1/1, view 0/0, "
		"texture 3/3, buffer 0/0, state 0/0), 0 unreleased, 1 unmatched";
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
		"skipped surfaces: 13 sky, 14 warped, 15 translucent, 16 flowing; 17 dynamic lights left out; 24 particles refused; "
		"skipped entities: 18 sprites, 19 beams, 20 null models, 21 translucent brush models, 22 refused";
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
	s.dlight_skips = 17;
	s.particle_skips = 24;
	s.sprite_skips = 18;
	s.beam_skips = 19;
	s.null_skips = 20;
	s.bmodel_trans_skips = 21;
	s.ent_skips = 22;
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
 * One session of ref_metal as the ledger sees it, in the module's order
 * (N10): rmb_open's device and queue, InitContext's view and resources
 * (the library, 24 pipelines, 4 depth-stencil states, 10 samplers made
 * twice: R_InitImages and the first R_TextureMode, the staging ring),
 * the images, the first frame's two targets and three rings, a mode
 * change (both targets again, nothing else), a readback, then
 * RI_Shutdown: the images, the detach (rings, targets, readback buffer),
 * the resources, the view, the queue and the device. With drop set, the
 * last readback buffer is not destroyed.
 */
static void
ledger_session(rm_ledger_t *l, int drop)
{
	int i;

	rm_ledger_create(l, RM_OBJ_DEVICE);
	rm_ledger_create(l, RM_OBJ_QUEUE);
	rm_ledger_create(l, RM_OBJ_VIEW);

	for (i = 0; i < 1 + 24 + 4 + 10; i++)
	{
		rm_ledger_create(l, RM_OBJ_STATE);
	}

	for (i = 0; i < 10; i++)
	{
		rm_ledger_destroy(l, RM_OBJ_STATE);
		rm_ledger_create(l, RM_OBJ_STATE);
	}

	rm_ledger_create(l, RM_OBJ_BUFFER);

	for (i = 0; i < 40; i++)
	{
		rm_ledger_create(l, RM_OBJ_TEXTURE);
	}

	rm_ledger_create(l, RM_OBJ_TEXTURE);
	rm_ledger_create(l, RM_OBJ_TEXTURE);

	for (i = 0; i < RMB_FRAMES_IN_FLIGHT; i++)
	{
		rm_ledger_create(l, RM_OBJ_BUFFER);
	}

	/* the frame's size changed: both targets again */
	for (i = 0; i < 2; i++)
	{
		rm_ledger_destroy(l, RM_OBJ_TEXTURE);
		rm_ledger_create(l, RM_OBJ_TEXTURE);
	}

	rm_ledger_create(l, RM_OBJ_BUFFER);

	for (i = 0; i < 40; i++)
	{
		rm_ledger_destroy(l, RM_OBJ_TEXTURE);
	}

	for (i = 0; i < RMB_FRAMES_IN_FLIGHT; i++)
	{
		rm_ledger_destroy(l, RM_OBJ_BUFFER);
	}

	rm_ledger_destroy(l, RM_OBJ_TEXTURE);
	rm_ledger_destroy(l, RM_OBJ_TEXTURE);

	if (!drop)
	{
		rm_ledger_destroy(l, RM_OBJ_BUFFER);
	}

	rm_ledger_destroy(l, RM_OBJ_BUFFER);

	for (i = 0; i < 1 + 24 + 4 + 10; i++)
	{
		rm_ledger_destroy(l, RM_OBJ_STATE);
	}

	rm_ledger_destroy(l, RM_OBJ_VIEW);
	rm_ledger_destroy(l, RM_OBJ_QUEUE);
	rm_ledger_destroy(l, RM_OBJ_DEVICE);
}

/*
 * The ledger after two restarts (N10): every kind at zero, and clean;
 * with one destroy dropped, that kind and only that kind is live and the
 * teardown is not clean. Each operand of rm_ledger_clean on its own.
 */
static void
test_ledger_restarts(void)
{
	rm_ledger_t l;
	int k;

	memset(&l, 0, sizeof(l));
	CHECK(rm_ledger_clean(&l));
	ledger_session(&l, 0);
	ledger_session(&l, 0);

	for (k = 0; k < RM_OBJ_KINDS; k++)
	{
		CHECK(rm_ledger_kind_live(&l, (rm_obj_kind_t)k) == 0);
		CHECK(l.created[k] > 0 && l.created[k] == l.destroyed[k]);
	}

	CHECK(l.created[RM_OBJ_STATE] == 2 * (1 + 24 + 4 + 20));
	CHECK(rm_ledger_live(&l) == 0 && l.unmatched == 0 && rm_ledger_clean(&l));

	/* the second restart drops the readback buffer's destroy */
	memset(&l, 0, sizeof(l));
	ledger_session(&l, 0);
	ledger_session(&l, 1);

	for (k = 0; k < RM_OBJ_KINDS; k++)
	{
		CHECK(rm_ledger_kind_live(&l, (rm_obj_kind_t)k) == (k == RM_OBJ_BUFFER ? 1u : 0u));
	}

	CHECK(rm_ledger_live(&l) == 1 && l.unmatched == 0 && !rm_ledger_clean(&l));

	/* each kind on its own, then unreleased and unmatched on their own */
	for (k = 0; k < RM_OBJ_KINDS; k++)
	{
		memset(&l, 0, sizeof(l));
		rm_ledger_create(&l, (rm_obj_kind_t)k);
		CHECK(rm_ledger_kind_live(&l, (rm_obj_kind_t)k) == 1 && !rm_ledger_clean(&l));
		rm_ledger_destroy(&l, (rm_obj_kind_t)k);
		CHECK(rm_ledger_kind_live(&l, (rm_obj_kind_t)k) == 0 && rm_ledger_clean(&l));
	}

	memset(&l, 0, sizeof(l));
	l.unreleased = 1;
	CHECK(!rm_ledger_clean(&l));
	memset(&l, 0, sizeof(l));
	l.unmatched = 1;
	CHECK(!rm_ledger_clean(&l) && rm_ledger_live(&l) == 0);

	/* a kind out of range reads 0 and is never indexed */
	memset(&l, 0, sizeof(l));
	rm_ledger_create(&l, RM_OBJ_DEVICE);
	CHECK(rm_ledger_kind_live(&l, RM_OBJ_KINDS) == 0);
	CHECK(rm_ledger_kind_live(&l, (rm_obj_kind_t)-1) == 0);
	CHECK(rm_ledger_kind_live(&l, RM_OBJ_DEVICE) == 1);
}

/*
 * The frame's size is the mode's (FIX45): taken when both sides are 1 to
 * RMB_MAX_TEXTURE_SIDE, refused otherwise, each bound on each side.
 */
static void
test_frame_size(void)
{
	int w = -1, h = -1;

	CHECK(rm_frame_size(640, 480, &w, &h) && w == 640 && h == 480);
	CHECK(rm_frame_size(1, 1, &w, &h) && w == 1 && h == 1);
	CHECK(rm_frame_size(RMB_MAX_TEXTURE_SIDE, RMB_MAX_TEXTURE_SIDE, &w, &h)
			&& w == RMB_MAX_TEXTURE_SIDE && h == RMB_MAX_TEXTURE_SIDE);
	CHECK(!rm_frame_size(0, 480, &w, &h) && w == 0 && h == 0);
	CHECK(!rm_frame_size(640, 0, &w, &h) && w == 0 && h == 0);
	CHECK(!rm_frame_size(-640, 480, &w, &h) && w == 0 && h == 0);
	CHECK(!rm_frame_size(640, -480, &w, &h) && w == 0 && h == 0);
	CHECK(!rm_frame_size(RMB_MAX_TEXTURE_SIDE + 1, 480, &w, &h) && w == 0 && h == 0);
	CHECK(!rm_frame_size(640, RMB_MAX_TEXTURE_SIDE + 1, &w, &h) && w == 0 && h == 0);
	CHECK(!rm_frame_size(INT_MIN, INT_MAX, &w, &h) && w == 0 && h == 0);
}

/*
 * The resize decision (N10): a change of the frame's size makes both
 * targets again and nothing else; a change of the window's pixels alone
 * sets the drawable's size and makes no target (FIX45); a missing target
 * makes both.
 */
static void
test_frame_remake(void)
{
	const unsigned both = RM_REMAKE_COLOR | RM_REMAKE_DEPTH;
	rm_frame_held_t held = { 1, 1, 640, 480, 640, 480 };
	rm_frame_held_t none = { 0, 0, 0, 0, 0, 0 };

	CHECK(rm_frame_remake(&held, 640, 480, 640, 480) == 0);
	CHECK(rm_frame_remake(&none, 640, 480, 640, 480) == (both | RM_REMAKE_DRAWABLE));

	/* the frame's size, each side */
	CHECK(rm_frame_remake(&held, 800, 480, 640, 480) == both);
	CHECK(rm_frame_remake(&held, 640, 600, 640, 480) == both);
	CHECK(rm_frame_remake(&held, 800, 600, 800, 600) == (both | RM_REMAKE_DRAWABLE));

	/* the window's pixels alone, each side: the targets stay */
	CHECK(rm_frame_remake(&held, 640, 480, 960, 480) == RM_REMAKE_DRAWABLE);
	CHECK(rm_frame_remake(&held, 640, 480, 640, 540) == RM_REMAKE_DRAWABLE);

	/* a target missing */
	held.color = 0;
	CHECK(rm_frame_remake(&held, 640, 480, 640, 480) == both);
	held.color = 1;
	held.depth = 0;
	CHECK(rm_frame_remake(&held, 640, 480, 640, 480) == both);
	held.depth = 1;

	/* the layer's size on each side */
	held.lw = 641;
	CHECK(rm_frame_remake(&held, 640, 480, 640, 480) == RM_REMAKE_DRAWABLE);
	held.lw = 640;
	held.lh = 479;
	CHECK(rm_frame_remake(&held, 640, 480, 640, 480) == RM_REMAKE_DRAWABLE);
}

/*
 * The present blit at gl1's place (FIX45): equal sizes copy the whole
 * frame; a smaller frame lands in the drawable's bottom-left corner; a
 * larger one gives its bottom-left part; no overlap copies nothing.
 */
static void
test_present_region(void)
{
	rm_present_t p;

	CHECK(rm_present_region(640, 480, 640, 480, &p));
	CHECK(p.src_y == 0 && p.dst_y == 0 && p.w == 640 && p.h == 480);

	/* the fullscreen fallback of TASK98: 640 by 480 asked, 960 by 540 given */
	CHECK(rm_present_region(640, 480, 960, 540, &p));
	CHECK(p.src_y == 0 && p.dst_y == 60 && p.w == 640 && p.h == 480);

	/* a frame larger than the drawable */
	CHECK(rm_present_region(1024, 768, 800, 600, &p));
	CHECK(p.src_y == 168 && p.dst_y == 0 && p.w == 800 && p.h == 600);

	/* wider only, taller only */
	CHECK(rm_present_region(800, 480, 640, 480, &p));
	CHECK(p.src_y == 0 && p.dst_y == 0 && p.w == 640 && p.h == 480);
	CHECK(rm_present_region(640, 600, 640, 480, &p));
	CHECK(p.src_y == 120 && p.dst_y == 0 && p.w == 640 && p.h == 480);

	/* nothing to copy, each side on its own */
	memset(&p, 0x55, sizeof(p));
	CHECK(!rm_present_region(0, 480, 640, 480, &p) && p.w == 0 && p.h == 0 && p.src_y == 0 && p.dst_y == 0);
	CHECK(!rm_present_region(640, 0, 640, 480, &p) && p.w == 0 && p.h == 0);
	CHECK(!rm_present_region(640, 480, 0, 480, &p) && p.w == 0 && p.h == 0);
	CHECK(!rm_present_region(640, 480, 640, 0, &p) && p.w == 0 && p.h == 0);
	CHECK(!rm_present_region(-1, 480, 640, 480, &p) && p.w == 0);
	CHECK(!rm_present_region(640, 480, 640, -1, &p) && p.h == 0);
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

	/* an exact fit (the last surface of every map), and a base past 32 bits */
	CHECK(rm_world_fan(0, 5, idx, 9) == 9 && idx[8] == 4);
	CHECK(rm_world_fan(0x100000000ul, 3, idx, 16) == 0);
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

	/* the index and record products against the cap, each at its exact fit */
	memset(&s, 0, sizeof(s)); s.surfs = 1; s.verts = 3; s.indices = 1000;
	CHECK(!rm_world_bytes(&s, 1000 * 4 - 1, &vb, &ib, &sb) && rm_world_bytes(&s, 1000 * 4, &vb, &ib, &sb) && ib == 4000);
	memset(&s, 0, sizeof(s)); s.surfs = 100; s.verts = 3; s.indices = 3;
	CHECK(!rm_world_bytes(&s, 100 * 48 - 1, &vb, &ib, &sb) && rm_world_bytes(&s, 100 * 48, &vb, &ib, &sb) && sb == 4800);
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
	{
		/* a two-byte lump that ends inside the row: never read past its end */
		unsigned char lit[2] = { 1, 2 };

		memset(out, 0xee, sizeof(out));
		CHECK(!rm_decompress_vis(lit, sizeof(lit), 5, out, sizeof(out)) && out[0] == 0xff);
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

	/* only the last surface of the range is taken already */
	memset(owner, 0, sizeof(owner));
	CHECK(rm_mark_once(owner, 6, 4, 1) && !rm_mark_once(owner, 6, 3, 2) && !owner[3]);
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
	const int last_root[4] = { 1, -1, -1, 0 };                 /* the last slot names the root */
	const int last_again[4] = { 1, -1, -1, 1 };                /* the last slot names node 1 again */

	CHECK(rm_node_tree_ok(tree, 4, 4, seen) && rm_node_tree_ok(leaves, 1, 1, seen));
	CHECK(!rm_node_tree_ok(shared, 4, 4, seen));
	CHECK(!rm_node_tree_ok(twice, 2, 2, seen));
	CHECK(!rm_node_tree_ok(to_root, 2, 2, seen));
	CHECK(!rm_node_tree_ok(loop, 3, 3, seen) && !rm_node_tree_ok(self, 2, 2, seen));
	memset(seen, 0, sizeof(seen));   /* a child at the node count, not seen before */
	CHECK(!rm_node_tree_ok(far_node, 2, 4, seen) && !rm_node_tree_ok(far_leaf, 2, 4, seen));
	CHECK(rm_node_tree_ok(far_leaf, 2, 5, seen) && !rm_node_tree_ok(least, 2, 4, seen));
	CHECK(!rm_node_tree_ok(tree, 0, 4, seen) && !rm_node_tree_ok(tree, 4, 3, seen));
	CHECK(!rm_node_tree_ok(last_root, 2, 2, seen) && !rm_node_tree_ok(last_again, 2, 2, seen));
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

/* ---- the lightmaps (N5) ------------------------------------------------------- */

/*
 * gl1's Mod_CalcSurfaceExtents per axis: floor and ceil of the sixteenths,
 * texturemins and extents as shorts; a value that is not finite, a range
 * the wrong way round, or a result outside a short is refused.
 */
static void
test_surface_extents(void)
{
	short tm = 77, ex = 77;

	/* floor(-8.5 / 16) = -1, ceil(100 / 16) = 7: texturemins -16, extents 128 */
	CHECK(rm_surface_extents(-8.5f, 100.0f, &tm, &ex) && tm == -16 && ex == 128);
	/* on the grid: 0 .. 256 is 17 luxels' worth, extents 256 */
	CHECK(rm_surface_extents(0.0f, 256.0f, &tm, &ex) && tm == 0 && ex == 256);
	CHECK(rm_surface_extents(5.0f, 5.0f, &tm, &ex) && tm == 0 && ex == 16);

	/* the edges of a short: bmins -2048 and 2047, extents 2047 * 16 */
	CHECK(rm_surface_extents(-32768.0f, -32768.0f, &tm, &ex) && tm == -32768 && ex == 0);
	CHECK(rm_surface_extents(32752.0f, 32752.0f, &tm, &ex) && tm == 32752 && ex == 0);
	CHECK(rm_surface_extents(0.0f, 32752.0f, &tm, &ex) && ex == 32752);

	tm = ex = 77;
	CHECK(!rm_surface_extents(-32769.0f, 0.0f, &tm, &ex) && tm == 77 && ex == 77);
	CHECK(!rm_surface_extents(32768.0f, 32768.0f, &tm, &ex));
	CHECK(!rm_surface_extents(-32769.0f, -32769.0f, &tm, &ex));    /* bmins -2049, extents 16 */
	CHECK(!rm_surface_extents(-16.0f, 32752.0f, &tm, &ex));    /* extents 2048 * 16 */
	CHECK(!rm_surface_extents(1e30f, 1e30f, &tm, &ex) && !rm_surface_extents(-1e30f, 0, &tm, &ex));
	CHECK(!rm_surface_extents(NAN, 0.0f, &tm, &ex) && !rm_surface_extents(0.0f, INFINITY, &tm, &ex));
	tm = ex = 77;
	CHECK(!rm_surface_extents(10.0f, 9.0f, &tm, &ex) && tm == 77 && ex == 77);
}

/*
 * The size arithmetic of a lightmap (plan section 2, "Inputs and bounds"):
 * gl1's (extents >> 4) + 1 per axis, at most BLOCK_WIDTH - 1 wide (what
 * LM_AllocBlock places), BLOCK_HEIGHT high and 867 luxels (R_BuildLightMap's
 * s_blocklights check); every larger or negative one is refused.
 */
static void
test_lm_size(void)
{
	int s = 0, t = 0;

	CHECK(rm_lm_size(256, 256, &s, &t) && s == 17 && t == 17);    /* the stock compiler's largest */
	CHECK(rm_lm_size(0, 0, &s, &t) && s == 1 && t == 1);
	CHECK(rm_lm_size(15, 31, &s, &t) && s == 1 && t == 2);
	CHECK(rm_lm_size(50 * 16, 16 * 16, &s, &t) && s == 51 && t == 17);   /* 867: the exact fit */
	CHECK(rm_lm_size(126 * 16, 5 * 16, &s, &t) && s == 127 && t == 6);   /* the widest */
	CHECK(rm_lm_size(5 * 16, 127 * 16, &s, &t) && s == 6 && t == 128);   /* the highest */

	s = t = 9;
	CHECK(!rm_lm_size(30 * 16, 27 * 16, &s, &t) && s == 9 && t == 9);    /* 31 * 28 = 868 */
	CHECK(!rm_lm_size(127 * 16, 0, &s, &t));     /* 128 wide: LM_AllocBlock never places it */
	CHECK(!rm_lm_size(0, 128 * 16, &s, &t));     /* 129 high */
	CHECK(!rm_lm_size(-16, 0, &s, &t) && !rm_lm_size(0, -1, &s, &t));
	CHECK(!rm_lm_size(16384, 16384, &s, &t));    /* a warped surface's extents */
	CHECK(!rm_lm_size(32768, 0, &s, &t) && !rm_lm_size(1L << 40, 0, &s, &t));
}

/* The styles of a surface and the bound of its samples against the lighting lump. */
static void
test_lm_samples(void)
{
	const unsigned char none[4] = { 255, 0, 0, 0 }, two[4] = { 0, 5, 255, 7 }, four[4] = { 0, 1, 2, 3 };

	CHECK(rm_lm_nummaps(none) == 0 && rm_lm_nummaps(two) == 2 && rm_lm_nummaps(four) == 4);

	/* 3 by 2 luxels (extents 32, 16) of two styles: 36 bytes; exact end, one byte short */
	CHECK(rm_lm_samples_ok(100, 32, 16, 2, 136));
	CHECK(!rm_lm_samples_ok(100, 32, 16, 2, 135));
	CHECK(!rm_lm_samples_ok(101, 32, 16, 2, 136));
	CHECK(rm_lm_samples_ok(136, 32, 16, 0, 136));     /* no style reads nothing */
	CHECK(!rm_lm_samples_ok(137, 32, 16, 0, 136));    /* but the offset is still inside the lump */
	CHECK(!rm_lm_samples_ok(-1, 32, 16, 1, 136) && !rm_lm_samples_ok(0, -16, 16, 1, 136));
	CHECK(!rm_lm_samples_ok(0, 16, -16, 1, 136) && !rm_lm_samples_ok(0, 16, 16, 5, 1 << 20));
	CHECK(!rm_lm_samples_ok(0, 16, 16, -1, 1 << 20));
	/* the largest extents of a short, four styles: 50 MB, larger than any lump, and no overflow */
	CHECK(!rm_lm_samples_ok(0, 32767, 32767, 4, 0x200000L));
	CHECK(rm_lm_samples_ok(0, 32767, 32767, 4, 2048L * 2048 * 3 * 4));
	CHECK(!rm_lm_samples_ok(1, 32767, 32767, 4, 2048L * 2048 * 3 * 4));
}

/*
 * gl1's R_RecursiveLightPoint on one surface (gl1_light.c), its four
 * lines with s, t already converted to int: the reference rm_lp_coords
 * is compared with on inputs where that arithmetic is defined.
 */
static int
gl1_lp_coords(int s, int t, short smin, short tmin, short sext, short text, int *ds, int *dt)
{
	if ((s < smin) || (t < tmin))
	{
		return 0;
	}

	*ds = s - smin;
	*dt = t - tmin;

	return !((*ds > sext) || (*dt > text));
}

static unsigned lp_rand_state = 0x2545f491u;

static unsigned
lp_rand(void)
{
	/* xorshift32: deterministic, the same sequence on every run */
	lp_rand_state ^= lp_rand_state << 13;
	lp_rand_state ^= lp_rand_state >> 17;
	lp_rand_state ^= lp_rand_state << 5;
	return lp_rand_state;
}

/*
 * a coordinate within two units of lo or of hi in eighths (the edges and
 * the fractions next to them), or anywhere in lo - 4 .. hi + 4 in quarters
 */
static float
lp_rand_coord(int lo, int hi)
{
	unsigned mode = lp_rand() % 3;

	if (mode < 2)
	{
		return (float)(mode ? hi : lo) + (float)((int)(lp_rand() % 33) - 16) * 0.125f;
	}

	return (float)(lo - 4) + (float)(lp_rand() % (unsigned)(((hi - lo) + 8) * 4)) * 0.25f;
}

/*
 * The light point's texture coordinates on one surface (rm_lp_coords):
 * gl1's result inside, on each of the four edges, within one texture
 * unit outside each edge (truncation toward zero puts -32.5 on -32 but
 * 15.5 on 15) for negative and positive texturemins, and far outside;
 * NaN and the infinities in each coordinate, values near INT_MAX with a
 * negative texturemins (an int conversion first would wrap ds into the
 * extents), the edges of the int range, a negative extent; nothing
 * written when refused. Then a differential run against gl1's lines.
 */
static void
test_lp_coords(void)
{
	const float in = 2147483520.0f; /* the largest float below 2^31 */
	int ds, dt, i, gds, gdt, mismatch, accepted;

	/* inside: texturemins 16, -32, extents 64, 48 */
	CHECK(rm_lp_coords(40.5f, -20.25f, 16, -32, 64, 48, &ds, &dt) && ds == 24 && dt == 12);
	/* a negative coordinate truncates toward zero: -31.5 is -31, 1 past -32 */
	CHECK(rm_lp_coords(16, -31.5f, 16, -32, 64, 48, &ds, &dt) && ds == 0 && dt == 1);

	/* each edge is inside */
	CHECK(rm_lp_coords(16, 0, 16, -32, 64, 48, &ds, &dt) && ds == 0 && dt == 32);
	CHECK(rm_lp_coords(80, 0, 16, -32, 64, 48, &ds, &dt) && ds == 64 && dt == 32);
	CHECK(rm_lp_coords(20, -32, 16, -32, 64, 48, &ds, &dt) && ds == 4 && dt == 0);
	CHECK(rm_lp_coords(20, 16, 16, -32, 64, 48, &ds, &dt) && ds == 4 && dt == 48);
	CHECK(rm_lp_coords(-30000, 0, -30000, 0, 0, 0, &ds, &dt) && ds == 0 && dt == 0);

	/* lower edge, positive texturemins 16: 15.5 and 15.99 truncate to 15 < 16, refused as gl1 */
	ds = dt = 1234;
	CHECK(!rm_lp_coords(15.5f, 0, 16, -32, 64, 48, &ds, &dt));
	CHECK(!rm_lp_coords(nextafterf(16, 0), 0, 16, -32, 64, 48, &ds, &dt));
	CHECK(ds == 1234 && dt == 1234);
	/* lower edge, negative texturemins -32: -32.5 and -32.99 truncate to -32, accepted with dt 0 */
	CHECK(rm_lp_coords(20, -32.5f, 16, -32, 64, 48, &ds, &dt) && ds == 4 && dt == 0);
	CHECK(rm_lp_coords(20, nextafterf(-33, 0), 16, -32, 64, 48, &ds, &dt) && ds == 4 && dt == 0);
	CHECK(!rm_lp_coords(20, -33, 16, -32, 64, 48, &ds, &dt));
	/* lower edge, texturemins 0: -0.5 truncates to 0, accepted; -1 refused */
	CHECK(rm_lp_coords(-0.5f, 5, 0, 0, 16, 16, &ds, &dt) && ds == 0 && dt == 5);
	CHECK(rm_lp_coords(5, nextafterf(-1, 0), 0, 0, 16, 16, &ds, &dt) && ds == 5 && dt == 0);
	CHECK(!rm_lp_coords(-1, 5, 0, 0, 16, 16, &ds, &dt) && !rm_lp_coords(5, -1, 0, 0, 16, 16, &ds, &dt));
	/* upper edge, positive end 80: 80.5 and 80.99 truncate to 80, ds 64 accepted; 81 refused */
	CHECK(rm_lp_coords(80.5f, 0, 16, -32, 64, 48, &ds, &dt) && ds == 64 && dt == 32);
	CHECK(rm_lp_coords(nextafterf(81, 0), 0, 16, -32, 64, 48, &ds, &dt) && ds == 64 && dt == 32);
	CHECK(!rm_lp_coords(81, 0, 16, -32, 64, 48, &ds, &dt));
	/* upper edge, end 16 from texturemins -32: 16.5 truncates to 16, dt 48 accepted; 17 refused */
	CHECK(rm_lp_coords(20, 16.5f, 16, -32, 64, 48, &ds, &dt) && ds == 4 && dt == 48);
	CHECK(!rm_lp_coords(20, 17, 16, -32, 64, 48, &ds, &dt));
	/* upper edge, negative end -32 (texturemins -64, extent 32): -32.5 is -32, ds 32 accepted; -31.5 is -31, refused */
	CHECK(rm_lp_coords(-32.5f, 0, -64, 0, 32, 0, &ds, &dt) && ds == 32 && dt == 0);
	CHECK(!rm_lp_coords(-31.5f, 0, -64, 0, 32, 0, &ds, &dt));
	/* the smallest texturemins: -32768.5 truncates to -32768, accepted; -32769 refused */
	CHECK(rm_lp_coords(-32768.5f, 0, -32768, 0, 32767, 0, &ds, &dt) && ds == 0 && dt == 0);
	CHECK(!rm_lp_coords(-32769, 0, -32768, 0, 32767, 0, &ds, &dt));
	CHECK(rm_lp_coords(-1, 0, -32768, 0, 32767, 0, &ds, &dt) && ds == 32767 && dt == 0);

	/* far outside each side; ds and dt untouched */
	ds = dt = 1234;
	CHECK(!rm_lp_coords(-1e6f, 0, 16, -32, 64, 48, &ds, &dt) && !rm_lp_coords(1e6f, 0, 16, -32, 64, 48, &ds, &dt));
	CHECK(!rm_lp_coords(20, -1e6f, 16, -32, 64, 48, &ds, &dt) && !rm_lp_coords(20, 1e6f, 16, -32, 64, 48, &ds, &dt));
	CHECK(ds == 1234 && dt == 1234);

	/* not finite, in either coordinate */
	CHECK(!rm_lp_coords(NAN, 0, 16, -32, 64, 48, &ds, &dt) && !rm_lp_coords(20, NAN, 16, -32, 64, 48, &ds, &dt));
	CHECK(!rm_lp_coords(INFINITY, 0, 16, -32, 64, 48, &ds, &dt));
	CHECK(!rm_lp_coords(-INFINITY, 0, 16, -32, 64, 48, &ds, &dt));
	CHECK(!rm_lp_coords(20, INFINITY, 16, -32, 64, 48, &ds, &dt));
	CHECK(!rm_lp_coords(20, -INFINITY, 16, -32, 64, 48, &ds, &dt));

	/* near and past INT_MAX with texturemins -30000, extents 0 (the wrap the security review built) */
	ds = dt = 1234;
	CHECK(!rm_lp_coords(in, 0, -30000, 0, 0, 0, &ds, &dt) && !rm_lp_coords(0, in, 0, -30000, 0, 0, &ds, &dt));
	CHECK(!rm_lp_coords(5e9f, 0, -30000, 0, 0, 0, &ds, &dt) && !rm_lp_coords(-in, 0, -30000, 0, 0, 0, &ds, &dt));
	/* 2^31 does not fit an int, -2^31 does (and is below texturemins); either refused */
	CHECK(!rm_lp_coords(2147483648.0f, 0, -32768, 0, 32767, 0, &ds, &dt));
	CHECK(!rm_lp_coords(-2147483648.0f, 0, -32768, 0, 32767, 0, &ds, &dt));
	CHECK(!rm_lp_coords(0, 2147483648.0f, 0, -32768, 0, 32767, &ds, &dt));
	CHECK(!rm_lp_coords(0, -2147483648.0f, 0, -32768, 0, 32767, &ds, &dt));
	CHECK(!rm_lp_coords(-3e9f, 0, -32768, 0, 32767, 0, &ds, &dt));
	/* finite but past any integer type */
	CHECK(!rm_lp_coords(1e30f, 0, 16, -32, 64, 48, &ds, &dt) && !rm_lp_coords(-1e30f, 0, 16, -32, 64, 48, &ds, &dt));
	CHECK(!rm_lp_coords(20, 1e30f, 16, -32, 64, 48, &ds, &dt) && !rm_lp_coords(20, -1e30f, 16, -32, 64, 48, &ds, &dt));
	CHECK(ds == 1234 && dt == 1234);

	/* a negative extent holds no point */
	ds = dt = 1234;
	CHECK(!rm_lp_coords(16, 0, 16, 0, -1, 0, &ds, &dt) && !rm_lp_coords(16, 0, 16, 0, 0, -1, &ds, &dt));
	CHECK(ds == 1234 && dt == 1234);

	/*
	 * differential: 4000 deterministic points around surfaces of
	 * texturemins -2048 .. 2047 and extents -16 .. 1007, where gl1's
	 * conversion and differences are defined; the accept/reject and
	 * ds, dt agree with gl1's lines on every one
	 */
	mismatch = 0;
	accepted = 0;

	for (i = 0; i < 4000; i++)
	{
		short smin = (short)((int)(lp_rand() % 4096) - 2048);
		short tmin = (short)((int)(lp_rand() % 4096) - 2048);
		short sext = (short)((int)(lp_rand() % 1024) - 16);
		short text = (short)((int)(lp_rand() % 1024) - 16);
		float s = lp_rand_coord(smin, smin + (sext < 0 ? 0 : sext));
		float t = lp_rand_coord(tmin, tmin + (text < 0 ? 0 : text));
		int r, g;

		ds = dt = gds = gdt = -1;
		r = rm_lp_coords(s, t, smin, tmin, sext, text, &ds, &dt);
		g = gl1_lp_coords((int)s, (int)t, smin, tmin, sext, text, &gds, &gdt);

		if ((r != g) || (r && ((ds != gds) || (dt != gdt))))
		{
			mismatch++;
		}

		accepted += r;
	}

	CHECK(mismatch == 0);
	/* both outcomes come up often enough to mean something */
	CHECK(accepted > 1000 && accepted < 3000);
}

/*
 * The dynamic lights of a frame (rm_dlight_count): the count clamped to
 * 0 .. MAX_DLIGHTS (ref.h, held equal); the mark bit of each, unsigned,
 * every one of 32 distinct, 0 past them (rm_dlight_bit).
 */
static void
test_dlights(void)
{
	unsigned all = 0;
	int i, ok = 1;

	CHECK(RM_MAX_DLIGHTS == MAX_DLIGHTS);
	CHECK(rm_dlight_count(0) == 0 && rm_dlight_count(1) == 1 && rm_dlight_count(32) == 32);
	CHECK(rm_dlight_count(33) == 32 && rm_dlight_count(INT32_MAX) == 32);
	CHECK(rm_dlight_count(-1) == 0 && rm_dlight_count(INT32_MIN) == 0);

	for (i = 0; i < RM_MAX_DLIGHTS; i++)
	{
		ok &= (rm_dlight_bit(i) != 0) && !(all & rm_dlight_bit(i));
		all |= rm_dlight_bit(i);
	}

	CHECK(ok && all == 0xffffffffu && rm_dlight_bit(31) == 0x80000000u && rm_dlight_bit(0) == 1u);
	CHECK(rm_dlight_bit(32) == 0 && rm_dlight_bit(-1) == 0 && rm_dlight_bit(INT32_MAX) == 0);
}

/*
 * gl1's LM_AllocBlock positions for a fixed sequence of sizes on one
 * block (the first spot with the lowest top, never in the last w
 * columns), the exact fit, the full block, and the slice change of
 * LM_CreateSurfaceLightmap when a block is full.
 */
static void
test_lm_alloc(void)
{
	int col[RM_LM_WIDTH], x = -1, y = -1, slice = 0, i;

	memset(col, 0, sizeof(col));
	CHECK(rm_lm_alloc(col, 17, 17, &x, &y) && x == 0 && y == 0);
	CHECK(rm_lm_alloc(col, 10, 5, &x, &y) && x == 17 && y == 0);
	CHECK(rm_lm_alloc(col, 3, 3, &x, &y) && x == 27 && y == 0);
	CHECK(rm_lm_alloc(col, 20, 2, &x, &y) && x == 30 && y == 0);
	/* the lowest top is now at column 50; columns 17 to 26 stand at 5 */
	CHECK(rm_lm_alloc(col, 97, 1, &x, &y) && x == 30 && y == 2);
	CHECK(col[29] == 3 && col[30] == 3 && col[126] == 3 && col[127] == 0);
	CHECK(rm_lm_alloc(col, 127, 1, &x, &y) && x == 0 && y == 17);
	CHECK(col[0] == 18 && col[126] == 18 && col[127] == 0);

	/* 120 by 110 from row 18 ends at row 128 exactly; one more row is a full block */
	{
		int full[RM_LM_WIDTH];

		memcpy(full, col, sizeof(col));
		CHECK(!rm_lm_alloc(full, 120, 111, &x, &y) && memcmp(full, col, sizeof(col)) == 0);
		CHECK(rm_lm_alloc(full, 120, 110, &x, &y) && x == 0 && y == 18 && full[119] == 128);
		CHECK(rm_lm_alloc(full, 1, 1, &x, &y));   /* column 127 is never taken, 120 to 126 are free */
		CHECK(x == 120 && y == 18);
	}

	/* larger than a block, or empty */
	CHECK(!rm_lm_alloc(col, RM_LM_WIDTH, 1, &x, &y) && !rm_lm_alloc(col, 1, RM_LM_HEIGHT + 1, &x, &y));
	CHECK(!rm_lm_alloc(col, 0, 1, &x, &y) && !rm_lm_alloc(col, 1, 0, &x, &y));

	/* the placement: a fresh block takes 17 by 17 blocks 7 to a row, 7 rows of them (49) */
	memset(col, 0, sizeof(col));

	for (i = 0; i < 49; i++)
	{
		CHECK(rm_lm_place(col, &slice, 17, 17, &x, &y) == 1 && slice == 0);
	}

	CHECK(x == 102 && y == 102);
	/* the 50th starts slice 1 at its top left */
	CHECK(rm_lm_place(col, &slice, 17, 17, &x, &y) == 2 && slice == 1 && x == 0 && y == 0 && col[0] == 17 &&
		col[17] == 0);
	CHECK(rm_lm_place(col, &slice, 17, 17, &x, &y) == 1 && slice == 1 && x == 17 && y == 0);

	/* the last slice full: the map is refused, nothing changed */
	slice = RM_LM_MAX_SLICES - 1;

	for (i = 0; i < RM_LM_WIDTH; i++)
	{
		col[i] = RM_LM_HEIGHT;
	}

	x = y = -1;
	CHECK(rm_lm_place(col, &slice, 1, 1, &x, &y) == 0 && slice == RM_LM_MAX_SLICES - 1 && x == -1 && y == -1);
	slice = RM_LM_MAX_SLICES - 2;
	CHECK(rm_lm_place(col, &slice, 1, 1, &x, &y) == 2 && slice == RM_LM_MAX_SLICES - 1 && x == 0 && y == 0);
	/* a size no empty slice takes, and a slice outside the array */
	slice = 0;
	CHECK(rm_lm_place(col, &slice, RM_LM_WIDTH, 1, &x, &y) == 0 && slice == 0);
	slice = RM_LM_MAX_SLICES;
	CHECK(rm_lm_place(col, &slice, 1, 1, &x, &y) == 0);
	slice = -1;
	CHECK(rm_lm_place(col, &slice, 1, 1, &x, &y) == 0);
}

/*
 * The oversized counts: a map needing more slices than gl1 fills
 * (MAX_LIGHTMAPS less gl1's block 0 and its last-upload check), and a
 * region outside its slice, are refused before anything is sized or
 * written.
 */
static void
test_lm_regions(void)
{
	CHECK(rm_lm_slices_ok(1) && rm_lm_slices_ok(RM_LM_MAX_SLICES) && RM_LM_MAX_SLICES == 126);
	CHECK(!rm_lm_slices_ok(0) && !rm_lm_slices_ok(RM_LM_MAX_SLICES + 1) && !rm_lm_slices_ok(-1));

	CHECK(rm_lm_region_ok(0, 1, 0, 0, RM_LM_WIDTH, RM_LM_HEIGHT));
	CHECK(rm_lm_region_ok(125, 126, 111, 111, 17, 17));
	CHECK(!rm_lm_region_ok(126, 126, 0, 0, 1, 1) && !rm_lm_region_ok(-1, 126, 0, 0, 1, 1));
	CHECK(!rm_lm_region_ok(0, 127, 0, 0, 1, 1) && !rm_lm_region_ok(0, 0, 0, 0, 1, 1));
	CHECK(!rm_lm_region_ok(0, 1, 112, 0, 17, 1) && !rm_lm_region_ok(0, 1, 0, 112, 1, 17));
	CHECK(!rm_lm_region_ok(0, 1, -1, 0, 1, 1) && !rm_lm_region_ok(0, 1, 0, -1, 1, 1));
	CHECK(!rm_lm_region_ok(0, 1, 0, 0, 0, 1) && !rm_lm_region_ok(0, 1, 0, 0, 1, 0));
	CHECK(!rm_lm_region_ok(0, 1, 0, 0, RM_LM_WIDTH + 1, 1));
	CHECK(!rm_lm_region_ok(0, 1, 0x7fffffffffffffffL, 0, 2, 1));

	/* the back end's check of a texture of its own size */
	CHECK(rm_region_ok(64, 32, 3, 2, 63, 31, 1, 1) && !rm_region_ok(64, 32, 3, 3, 0, 0, 1, 1));
	CHECK(!rm_region_ok(64, 32, 3, 0, 63, 0, 2, 1) && !rm_region_ok(64, 32, 3, 0, 0, 31, 1, 2));
	CHECK(!rm_region_ok(0, 32, 3, 0, 0, 0, 1, 1) && !rm_region_ok(64, 0, 3, 0, 0, 0, 1, 1));
	CHECK(!rm_region_ok(64, 32, 3, 0, 0, 0, 65, 1) && !rm_region_ok(64, 32, 3, 0, 0, 0, 1, 33));
}

/* gl1's R_JoinAreas, from R_RegenAllLightmaps' empty start (top and left at the block's size). */
static void
test_lm_join(void)
{
	rm_lmrect_t best = { RM_LM_WIDTH, 0, RM_LM_HEIGHT, 0 };
	rm_lmrect_t a = { 10, 27, 5, 22 }, b = { 20, 30, 15, 40 }, c = { 100, 110, 90, 95 };

	rm_lm_join(&a, &best);
	CHECK(best.left == 10 && best.right == 27 && best.top == 5 && best.bottom == 22);
	rm_lm_join(&b, &best);     /* overlapping: right and bottom grow */
	CHECK(best.left == 10 && best.right == 30 && best.top == 5 && best.bottom == 40);
	rm_lm_join(&a, &best);     /* inside: nothing changes */
	CHECK(best.left == 10 && best.right == 30 && best.top == 5 && best.bottom == 40);
	rm_lm_join(&c, &best);     /* disjoint: the box around both */
	CHECK(best.left == 10 && best.right == 110 && best.top == 5 && best.bottom == 95);

	{
		rm_lmrect_t d = { 50, 60, 50, 60 }, e = { 0, 1, 70, 71 };

		rm_lm_join(&e, &d);    /* left and top of one, right and bottom of the other */
		CHECK(d.left == 0 && d.right == 60 && d.top == 50 && d.bottom == 71);
	}
}

/*
 * After a rebuilt lightmap (rm_lm_regen_mark), gl1's R_RegenAllLightmaps
 * keeps a surface dirty when a dynamic light touched it this frame, and
 * records its styles as built when one is 0 or 32 and above; here also
 * when its slice's upload was refused, it stays dirty and nothing is
 * recorded, so the next frame rebuilds and uploads it again. Every row.
 */
static void
test_lm_regen_mark(void)
{
	int up, dl, ca, dirty, ok = 1;

	for (up = 0; up < 2; up++)
	{
		for (dl = 0; dl < 2; dl++)
		{
			for (ca = 0; ca < 2; ca++)
			{
				int cache;

				dirty = 7;
				cache = rm_lm_regen_mark(up, dl, ca, &dirty);
				ok &= (dirty == (!up || dl)) && (cache == (up && !dl && ca));
			}
		}
	}

	CHECK(ok);
	/* any non-zero value is true */
	CHECK(rm_lm_regen_mark(2, 0, 3, &dirty) == 1 && dirty == 0);
	CHECK(rm_lm_regen_mark(0, 0, 1, &dirty) == 0 && dirty == 1);
	/* a dynamic-light mark is any non-zero value, and dirty is normalised to 1 */
	CHECK(rm_lm_regen_mark(1, 2, 1, &dirty) == 0 && dirty == 1);
}

/* gl1's minlight table: the identity at 0, (255 - m) * i / 255 + m otherwise, m clamped. */
static void
test_minlight(void)
{
	unsigned char t[256];
	int i, ok = 1;

	CHECK(rm_minlight_table(0, t) == 0);

	for (i = 0; i < 256; i++)
	{
		ok &= t[i] == i;
	}

	CHECK(ok);
	CHECK(rm_minlight_table(64, t) == 1);
	CHECK(t[0] == 64 && t[1] == 64 && t[15] == 75 && t[22] == 80 && t[29] == 85 && t[59] == 108);
	CHECK(t[80] == 123 && t[127] == 159 && t[255] == 255);
	CHECK(rm_minlight_table(300, t) == 1 && t[0] == 255 && t[100] == 255);
	CHECK(rm_minlight_table(-5, t) == 0 && t[7] == 7);
	CHECK(rm_minlight_table(NAN, t) == 0 && t[200] == 200);
}

/*
 * gl1's R_BuildLightMap on a synthetic 3 by 2 surface of two styles at
 * scales 0.5 and 2 (r_modulate 1), worked by hand from gl1_light.c:
 *
 *   luxel  style 0          style 1          sum (0.5 a + 2 b)   stored
 *   0      10  20  30       5   6   7        15   22   29        15 22 29, a 29
 *   1      200 100 0        100 10  0        300  70   0         max 300: t = 255/300 (0.85000002):
 *                                                                255 59 0, a 255
 *   2      2   4   6        1   1   1        3    4    5         3 4 5, a 5
 *   3      1   3   0        0   0   0        0.5  1.5  0         0 1 0 (truncated), a 1
 *   4      0   0   0        128 0   64       256  0    128       max 256: t = 0.99609375: 255 0 127, a 255
 *   5      40  80  120      30  20  10       80   80   80        80 80 80, a 80
 *
 * then through gl1_minlight 64 (t[15] = 75, ...) and a gamma table that
 * is not the identity (255 - i), each applied in gl1's order; the rows
 * stride bytes apart, the padding untouched.
 */
static void
test_lm_build(void)
{
	static const unsigned char samples[2 * 6 * 3] = {
		10, 20, 30, 200, 100, 0, 2, 4, 6, 1, 3, 0, 0, 0, 0, 40, 80, 120,
		5, 6, 7, 100, 10, 0, 1, 1, 1, 0, 0, 0, 128, 0, 64, 30, 20, 10,
	};
	static const unsigned char want[6][4] = {
		{ 15, 22, 29, 29 }, { 255, 59, 0, 255 }, { 3, 4, 5, 5 },
		{ 0, 1, 0, 1 }, { 255, 0, 127, 255 }, { 80, 80, 80, 80 },
	};
	const float rgb[2][3] = { { 0.5f, 0.5f, 0.5f }, { 2, 2, 2 } };
	unsigned char ident[256], inv[256], ml[256], out[2][20];
	float bl[RM_LM_MAX_LUXELS * 3];
	int i, j, ok = 1;

	for (i = 0; i < 256; i++)
	{
		ident[i] = (unsigned char)i;
		inv[i] = (unsigned char)(255 - i);
	}

	memset(out, 0xee, sizeof(out));
	CHECK(rm_lm_build(samples, 3, 2, 2, rgb, 1.0f, NULL, ident, &out[0][0], 20, bl));

	for (i = 0; i < 6; i++)
	{
		ok &= memcmp(&out[i / 3][(i % 3) * 4], want[i], 4) == 0;
	}

	CHECK(ok);
	CHECK(out[0][12] == 0xee && out[0][19] == 0xee && out[1][12] == 0xee && out[1][19] == 0xee);

	/* minlight, then gamma: luxel 0 is 75 80 85 before the inverse, luxel 3 64 64 64, alpha untouched */
	rm_minlight_table(64, ml);
	CHECK(rm_lm_build(samples, 3, 2, 2, rgb, 1.0f, ml, inv, &out[0][0], 20, bl));
	CHECK(out[0][0] == 255 - 75 && out[0][1] == 255 - 80 && out[0][2] == 255 - 85 && out[0][3] == 29);
	CHECK(out[0][4] == 0 && out[0][5] == 255 - 108 && out[0][6] == 255 - 64 && out[0][7] == 255);
	CHECK(out[1][0] == 255 - 64 && out[1][1] == 255 - 64 && out[1][3] == 1);
	CHECK(out[1][4] == 0 && out[1][6] == 255 - 159 && out[1][8] == 255 - 123 && out[1][11] == 80);

	/* r_modulate scales every style: 2 doubles the first luxel's 0.5 a + 2 b, alpha 58 */
	CHECK(rm_lm_build(samples, 3, 2, 2, rgb, 2.0f, NULL, ident, &out[0][0], 20, bl));
	CHECK(out[0][0] == 30 && out[0][1] == 44 && out[0][2] == 58 && out[0][3] == 58);

	/* two styles of different colours: each channel takes its own scale (luxel 0: 5 + 15, 20 + 6, 60 + 3.5) */
	{
		const float col[2][3] = { { 0.5f, 1, 2 }, { 3, 1, 0.5f } };

		CHECK(rm_lm_build(samples, 3, 2, 2, col, 1.0f, NULL, ident, &out[0][0], 20, bl));
		CHECK(out[0][0] == 20 && out[0][1] == 26 && out[0][2] == 63 && out[0][3] == 63);
	}

	/* one style: scale 1 copies the samples, 1.5 truncates; no light data is 255 everywhere */
	{
		const float one[1][3] = { { 1, 1, 1 } }, half[1][3] = { { 1.5f, 1.5f, 0.25f } };

		CHECK(rm_lm_build(samples, 3, 2, 1, one, 1.0f, NULL, ident, &out[0][0], 20, bl));
		CHECK(out[0][0] == 10 && out[0][1] == 20 && out[0][2] == 30 && out[1][10] == 120 && out[1][11] == 120);
		CHECK(rm_lm_build(samples, 3, 2, 1, half, 1.0f, NULL, ident, &out[0][0], 20, bl));
		CHECK(out[0][0] == 15 && out[0][1] == 30 && out[0][2] == 7 && out[0][3] == 30);
		/* 1.5 * 200 = 300 and 1.5 * 100 = 150, 0: max 300 scales them: 255, 127, 0 */
		CHECK(out[0][4] == 255 && out[0][5] == 127 && out[0][6] == 0);
		CHECK(rm_lm_build(NULL, 3, 2, 1, one, 1.0f, ml, inv, &out[0][0], 20, bl));
		CHECK(out[0][0] == 0 && out[1][10] == 0 && out[1][11] == 255);
	}

	/* negative lights become 0; a NaN or a huge r_modulate stays defined */
	{
		const float neg[1][3] = { { -1, 1, 1 } };

		CHECK(rm_lm_build(samples, 3, 2, 1, neg, 1.0f, NULL, ident, &out[0][0], 20, bl));
		CHECK(out[0][0] == 0 && out[0][1] == 20 && out[0][3] == 30);
		CHECK(rm_lm_build(samples, 3, 2, 2, rgb, NAN, NULL, ident, &out[0][0], 20, bl));
		CHECK(out[0][0] == 0 && out[0][3] == 0);
		CHECK(rm_lm_build(samples, 3, 2, 2, rgb, 1e30f, NULL, ident, &out[0][0], 20, bl));
		CHECK(out[0][0] == 255 && out[0][3] == 255);
	}

	/*
	 * one style with r_modulate 2 (the path most surfaces take); green the
	 * brightest channel of an over-bright luxel (170 255 0); every channel
	 * negative; the combine with gl1_overbrightbits 1
	 */
	{
		const float one1[1][3] = { { 1, 1, 1 } }, grn[1][3] = { { 1, 3, 1 } }, allneg[1][3] = { { -1, -1, -1 } };

		CHECK(rm_lm_build(samples, 3, 2, 1, one1, 2.0f, NULL, ident, &out[0][0], 20, bl));
		CHECK(out[0][0] == 20 && out[0][1] == 40 && out[0][2] == 60 && out[0][3] == 60);
		CHECK(rm_lm_build(samples, 3, 2, 1, grn, 1.0f, NULL, ident, &out[0][0], 20, bl));
		CHECK(out[0][4] == 170 && out[0][5] == 255 && out[0][6] == 0 && out[0][7] == 255);
		CHECK(rm_lm_build(samples, 3, 2, 1, allneg, 1.0f, NULL, ident, &out[0][0], 20, bl));
		CHECK(out[0][0] == 0 && out[0][1] == 0 && out[0][2] == 0 && out[0][3] == 0 && out[1][9] == 0 && out[1][10] == 0);
		CHECK(rm_lm_env(1, 1) == MTL_LM_COMBINE && rm_lm_env(1, 0) == MTL_LM_COMBINE);
	}

	/* refused: a stride below a row, sizes past rm_lm_size, styles past four; nothing written */
	memset(out, 0xee, sizeof(out));
	CHECK(!rm_lm_build(samples, 3, 2, 2, rgb, 1.0f, NULL, ident, &out[0][0], 11, bl));
	CHECK(!rm_lm_build(samples, 0, 2, 2, rgb, 1.0f, NULL, ident, &out[0][0], 20, bl));
	CHECK(!rm_lm_build(samples, 3, 0, 2, rgb, 1.0f, NULL, ident, &out[0][0], 20, bl));
	CHECK(!rm_lm_build(samples, 128, 1, 1, rgb, 1.0f, NULL, ident, &out[0][0], 512, bl));
	CHECK(!rm_lm_build(samples, 31, 28, 1, rgb, 1.0f, NULL, ident, &out[0][0], 512, bl));
	CHECK(!rm_lm_build(samples, 3, 2, 5, rgb, 1.0f, NULL, ident, &out[0][0], 20, bl));
	CHECK(!rm_lm_build(samples, 3, 2, -1, rgb, 1.0f, NULL, ident, &out[0][0], 20, bl));

	for (i = 0, ok = 1; i < 2; i++)
	{
		for (j = 0; j < 20; j++)
		{
			ok &= out[i][j] == 0xee;
		}
	}

	CHECK(ok);

	/* the largest surface fills exactly RM_LM_MAX_LUXELS of the scratch */
	{
		static unsigned char big[RM_LM_MAX_LUXELS * 3], dst[17 * 51 * 4];

		memset(big, 9, sizeof(big));
		CHECK(rm_lm_build(big, 51, 17, 1, rgb, 1.0f, NULL, ident, dst, 51 * 4, bl));
		CHECK(dst[0] == 4 && dst[sizeof(dst) - 2] == 4 && dst[sizeof(dst) - 1] == 4);
	}
}

/*
 * The world_lm fragment arithmetic (common/mtl_shader_math.h) against
 * the shim's two texture units for the same draw, bit for bit, over a
 * grid of texels in [0, 1] for each texture environment gl1 sets on the
 * lightmap unit (GL_MODULATE, GL_REPLACE with r_lightmap, GL_COMBINE
 * with gl1_overbrightbits 1, 2 and 4); and the combine by hand, a
 * texel that clamps and one that does not.
 */
static void
test_lm_combine(void)
{
	static const float v[] = { 0, 1.0f / 255, 0.25f, 0.5f, 100.0f / 255, 0.6f, 0.9f, 254.0f / 255, 1 };
	const int n = (int)(sizeof(v) / sizeof(v[0]));
	int i, j, k, e, ok = 1, cases = 0;
	mtl_rgba_t d, l, c;

	for (e = 0; e < 5; e++)
	{
		int env = e == 0 ? MTL_LM_MODULATE : (e == 1 ? MTL_LM_REPLACE : MTL_LM_COMBINE);
		float scale = e == 2 ? 1.0f : (e == 3 ? 2.0f : (e == 4 ? 4.0f : 1.0f));

		for (i = 0; i < n; i++)
		{
			for (j = 0; j < n; j++)
			{
				for (k = 0; k < n; k++)
				{
					float df[4] = { v[i], v[j], v[k], 1 }, lf[4] = { v[k], v[i], v[j], v[(i + j) % n] }, sh[4];

					d.r = df[0];
					d.g = df[1];
					d.b = df[2];
					d.a = df[3];
					l.r = lf[0];
					l.g = lf[1];
					l.b = lf[2];
					l.a = lf[3];
					c = mtl_world_lm(d, l, env, scale);
					rm_host_shim_world_lm(env, scale, 1, df, lf, sh);
					ok &= memcmp(&c.r, &sh[0], 4) == 0 && memcmp(&c.g, &sh[1], 4) == 0 &&
						memcmp(&c.b, &sh[2], 4) == 0 && memcmp(&c.a, &sh[3], 4) == 0;
					cases++;
				}
			}
		}
	}

	CHECK(ok && cases == 5 * 9 * 9 * 9);

	d.r = 0.6f;
	d.g = 0.25f;
	d.b = 0.5f;
	d.a = 1;
	l.r = 0.9f;
	l.g = 0.5f;
	l.b = 0.5f;
	l.a = 0.5f;
	/* overbright 2: 0.6 * 0.9 * 2 = 1.08 clamps to 1; 0.25 * 0.5 * 2 = 0.25 does not; alpha not scaled */
	c = mtl_world_lm(d, l, MTL_LM_COMBINE, 2);
	CHECK(c.r == 1.0f && c.g == 0.25f && c.b == 0.5f && c.a == 0.5f);
	/* overbright 4: 0.25 * 0.5 * 4 = 0.5, 0.5 * 0.5 * 4 = 1 (the edge), 0.6 * 0.9 * 4 clamps */
	c = mtl_world_lm(d, l, MTL_LM_COMBINE, 4);
	CHECK(c.r == 1.0f && c.g == 0.5f && c.b == 1.0f && c.a == 0.5f);
	/* overbright 1 is the product, as GL_MODULATE is */
	c = mtl_world_lm(d, l, MTL_LM_COMBINE, 1);
	CHECK(c.r == 0.6f * 0.9f && c.g == 0.125f && c.b == 0.25f);
	c = mtl_world_lm(d, l, MTL_LM_MODULATE, 4);    /* the scale is GL_COMBINE's only */
	CHECK(c.r == 0.6f * 0.9f && c.g == 0.125f && c.a == 0.5f);
	c = mtl_world_lm(d, l, MTL_LM_REPLACE, 4);
	CHECK(c.r == 0.9f && c.g == 0.5f && c.a == 0.5f);

	/* gl1's cvars to the environment and the scale */
	CHECK(rm_overbright(0) == 0 && rm_overbright(1) == 1 && rm_overbright(2) == 2 && rm_overbright(3) == 2);
	CHECK(rm_overbright(4) == 4 && rm_overbright(9) == 4 && rm_overbright(-1) == 0 && rm_overbright(2.7f) == 2);
	CHECK(rm_overbright(NAN) == 0);
	CHECK(rm_lm_env(0, 0) == MTL_LM_MODULATE && rm_lm_env(0, 1) == MTL_LM_REPLACE);
	CHECK(rm_lm_env(2, 0) == MTL_LM_COMBINE && rm_lm_env(4, 1) == MTL_LM_COMBINE);
}

/* The lightmap line of metal_stats and the pass a split reopens. */
static void
test_lm_line(void)
{
	char buf[RM_STATS_LINE];
	const char *want = "ref_metal: metal_stats: lightmaps maps/base2.bsp: 17 slices of 128 by 128 (1114112 bytes)";
	size_t n = strlen(want);

	CHECK(rm_lm_format("maps/base2.bsp", 17, buf, sizeof(buf)) && strcmp(buf, want) == 0);
	CHECK(rm_lm_format("maps/base2.bsp", 17, buf, n + 1) && !rm_lm_format("maps/base2.bsp", 17, buf, n));
	CHECK(buf[0] == '\0');
	CHECK(!rm_lm_format("maps/base2.bsp", 0, buf, sizeof(buf)) && buf[0] == '\0');
	CHECK(!rm_lm_format("maps/base2.bsp", RM_LM_MAX_SLICES + 1, buf, sizeof(buf)));

	{
		char name[MAX_QPATH];

		memset(name, 'x', sizeof(name) - 1);
		name[sizeof(name) - 1] = '\0';
		CHECK(rm_lm_format(name, RM_LM_MAX_SLICES, buf, sizeof(buf)) && strlen(buf) < sizeof(buf));
	}

	CHECK(rm_split_reopen(0, 0) == RM_REOPEN_NONE && rm_split_reopen(0, 1) == RM_REOPEN_NONE);
	CHECK(rm_split_reopen(1, 0) == RM_REOPEN_NONE && rm_split_reopen(1, 1) == RM_REOPEN_3D_KEEP);
}

/* ---- N6: sky, warped water, flowing and translucent surfaces ------------------------- */

/* gl1's table, from the file gl1_warp.c includes (its RI_Init halves it), to hold rm_turb_sin equal to it entry by entry */
static const float ref_turbsin[256] = {
#include "../../src/client/refresh/constants/warpsin.h"
};

#define REF_TURBSCALE (256.0 / (2 * M_PI))

/* a small deterministic generator for the differential cases */
static unsigned long long rng_state = 88172645463325252ull;

static unsigned
rng(void)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 7;
	rng_state ^= rng_state << 17;
	return (unsigned)(rng_state >> 16);
}

static float
rngf(float lo, float hi)
{
	return lo + (hi - lo) * ((rng() & 0xffffff) / 16777215.0f);
}

/*
 * The turbulence table, the index arithmetic of R_EmitWaterPolys worked
 * by hand for three vertices at one time, and the scrolls.
 */
static void
test_warp_arith(void)
{
	int i, ok = 1;
	float s, t;

	for (i = 0; i < 256; i++)
	{
		ok &= (rm_turb_sin(i) == ref_turbsin[i] * 0.5);
	}

	CHECK(ok);
	CHECK(rm_turb_sin(256) == ref_turbsin[0] * 0.5f && rm_turb_sin(-1) == ref_turbsin[255] * 0.5f);
	/* the peak of 8 * sin is 4 after the halving, at the quarter turn */
	CHECK(rm_turb_sin(64) > 3.99f && rm_turb_sin(64) <= 4.0f && rm_turb_sin(192) < -3.99f && rm_turb_sin(0) == 0);

	/* TURBSCALE = 256 / 2 pi = 40.7436654; hand-worked at time 1.5 */
	CHECK(rm_warp_index(0, 1.5f) == 61);       /* 1.5 * 40.74 = 61.1 */
	CHECK(rm_warp_index(32, 1.5f) == 224);     /* (4 + 1.5) * 40.74 = 224.09 */
	CHECK(rm_warp_index(64, 1.5f) == 131);     /* 9.5 * 40.74 = 387.06, 387 & 255 */
	CHECK(rm_warp_index(100, 1.5f) == 58);     /* 14 * 40.74 = 570.4, 570 & 255 */
	CHECK(rm_warp_index(-16, 1.5f) == 236);    /* -0.5 * 40.74 = -20.37, -20 & 255 */
	CHECK(rm_warp_index(8, 0) == 40 && rm_warp_index(-8, 0) == 216);
	/* an exact multiple: 256 / 40.74 steps */
	CHECK(rm_warp_index(0, 0) == 0);
	CHECK(rm_warp_index(0, 1.0f / 64) == 0);   /* 0.636 */
	CHECK(rm_warp_index(0, 0.025f) == 1);      /* 1.018 */
	/* not an int: NaN is 0, a huge value is taken modulo 256 and stays in the table */
	CHECK(rm_warp_index(NAN, 1) == 0);
	CHECK(rm_warp_index(1e30f, 0) >= 0 && rm_warp_index(1e30f, 0) < 256);
	CHECK(rm_warp_index(-1e30f, 0) >= 0 && rm_warp_index(-1e30f, 0) < 256);
	CHECK(rm_warp_index(INFINITY, 0) == 0 && rm_warp_index(-INFINITY, 0) == 0);
	/* past an int the value is taken modulo 256 as the integer would be: 1e9 * 0.125 * 40.74 = 5.09e9 */
	CHECK(rm_warp_index(1e9f, 0) == (int)(((long long)((1e9f * 0.125 + 0) * REF_TURBSCALE)) & 255));
	CHECK(rm_warp_index(-1e9f, 0) == (int)(((long long)((-1e9f * 0.125 + 0) * REF_TURBSCALE)) & 255));
	CHECK(rm_warp_index(1e9f, 7.5f) == (int)(((long long)((1e9f * 0.125 + 7.5f) * REF_TURBSCALE)) & 255));
	CHECK(rm_warp_index(53000000.0f, 0) == (int)(((long long)((53000000.0f * 0.125 + 0) * REF_TURBSCALE)) & 255));

	/* the three vertices: s = os + turb(ot) + scroll, t = ot + turb(os), each over 64 */
	rm_warp_st(0, 0, 1.5f, 0, &s, &t);
	CHECK(s == (float)((0 + (ref_turbsin[61] * 0.5f) + 0) * (1.0 / 64)) && t == (float)((0 + (ref_turbsin[61] * 0.5f)) * (1.0 / 64)));
	rm_warp_st(64, 32, 1.5f, -8, &s, &t);
	CHECK(s == (float)((64 + (ref_turbsin[224] * 0.5f) - 8) * (1.0 / 64)) && t == (float)((32 + (ref_turbsin[131] * 0.5f)) * (1.0 / 64)));
	rm_warp_st(-16, 100, 1.5f, 0, &s, &t);
	CHECK(s == (float)((-16 + (ref_turbsin[58] * 0.5f)) * (1.0 / 64)) && t == (float)((100 + (ref_turbsin[236] * 0.5f)) * (1.0 / 64)));

	/* the water's scroll: -64 * the fraction of time / 2, only on a flowing surface */
	CHECK(rm_warp_scroll(1.25f, 0) == 0);
	CHECK(rm_warp_scroll(1.25f, 1) == -40);    /* 0.625 * 64 */
	CHECK(rm_warp_scroll(3.0f, 1) == -32);     /* 1.5 -> 0.5 */
	CHECK(rm_warp_scroll(2.0f, 1) == 0);
	CHECK(rm_warp_scroll(0, 1) == 0);
	CHECK(rm_warp_scroll(-0.5f, 1) == 16);     /* (int) truncates toward 0: -0.25 - 0 = -0.25 */
	/* a time that is not a number or beyond an int is converted without the undefined behaviour of gl1's cast */
	CHECK(rm_warp_scroll(NAN, 1) != rm_warp_scroll(NAN, 1));
	CHECK(rm_warp_scroll(1e30f, 1) == (float)(-64 * ((1e30f * 0.5) - 2147483647.0)));
	CHECK(rm_warp_scroll(-1e30f, 1) == (float)(-64 * ((-1e30f * 0.5) + 2147483648.0)));
	CHECK(rm_warp_scroll(3e9f, 1) == (float)(-64 * ((3e9f * 0.5) - 1500000000.0)));
	/* just past an int the truncation is the largest and the smallest one */
	CHECK(rm_warp_scroll(5e9f, 1) == (float)(-64 * ((5e9f * 0.5) - 2147483647.0)));
	CHECK(rm_warp_scroll(-5e9f, 1) == (float)(-64 * ((-5e9f * 0.5) + 2147483648.0)));
	/* at the first value past the largest int the truncation saturates: one above it, and 0 above the smallest */
	CHECK(rm_warp_scroll(4294967296.0f, 1) == -64);
	CHECK(rm_warp_scroll(-4294967296.0f, 1) == 0);

	/* a flowing polygon's scroll: -64 * the fraction of time / 40, -64 where that is 0 */
	CHECK(rm_flow_scroll(10) == -16);
	CHECK(rm_flow_scroll(30) == -48);
	CHECK(rm_flow_scroll(0) == -64);
	CHECK(rm_flow_scroll(40) == -64);
	CHECK(rm_flow_scroll(80) == -64);
	CHECK(rm_flow_scroll(41) == -1.6f);
	CHECK(rm_flow_scroll(85899345920.0f) == -64);     /* 2^31 * 40: one above the largest int */
	CHECK(rm_flow_scroll(-85899345920.0f) == -64);    /* the smallest int exactly: the fraction is 0, which gl1 makes -64 */
	CHECK(rm_flow_scroll(1e12f) == (float)(-64 * ((1e12f / 40.0) - 2147483647.0)));
	CHECK(rm_flow_scroll(-1e12f) == (float)(-64 * ((-1e12f / 40.0) + 2147483648.0)));
	CHECK(rm_flow_scroll(1e30f) == (float)(-64 * ((1e30f / 40.0) - 2147483647.0)));
	CHECK(rm_flow_scroll(-1e30f) == (float)(-64 * ((-1e30f / 40.0) + 2147483648.0)));
	CHECK(rm_flow_scroll(NAN) != rm_flow_scroll(NAN));   /* the fraction of a NaN is a NaN: no conversion, no crash */

	/* differential: the same expressions as gl1's, over random values */
	ok = 1;

	for (i = 0; i < 20000; i++)
	{
		float os = rngf(-9000, 9000), ot = rngf(-9000, 9000), tm = rngf(0, 5000), sc = rm_warp_scroll(tm, rng() & 1);
		float rs, rt, es, et;
		int x, y;

		x = (int)((ot * 0.125 + tm) * REF_TURBSCALE) & 255;
		y = (int)((os * 0.125 + tm) * REF_TURBSCALE) & 255;
		es = os + (ref_turbsin[x] * 0.5f) + sc;
		et = ot + (ref_turbsin[y] * 0.5f);
		rm_warp_st(os, ot, tm, sc, &rs, &rt);
		ok &= (rs == (float)(es * (1.0 / 64))) && (rt == (float)(et * (1.0 / 64)));
		ok &= (rm_warp_index(ot, tm) == x) && (rm_warp_index(os, tm) == y);
	}

	CHECK(ok);
}

/* gl1's R_SubdividePolygon, unbounded, as the reference of the differential cases (pieces in emission order) */
#define REF_MAXP 4096

static float ref_out[REF_MAXP * 8][5];
static int ref_len[REF_MAXP], ref_np, ref_nv;

static void
ref_subdivide(int numverts, float *verts, const float *vec0, const float *vec1)
{
	int i, j, k;
	float mins[3], maxs[3];
	float m;
	float *v;
	float front[64][3], back[64][3];
	int f, b;
	float dist[64];
	float frac;
	float s, t;
	float total[3];
	float total_s, total_t;

	mins[0] = mins[1] = mins[2] = 9999;
	maxs[0] = maxs[1] = maxs[2] = -9999;
	v = verts;

	for (i = 0; i < numverts; i++)
	{
		for (j = 0; j < 3; j++, v++)
		{
			if (*v < mins[j])
			{
				mins[j] = *v;
			}

			if (*v > maxs[j])
			{
				maxs[j] = *v;
			}
		}
	}

	for (i = 0; i < 3; i++)
	{
		m = (mins[i] + maxs[i]) * 0.5;
		m = 64 * floor(m / 64 + 0.5);

		if (maxs[i] - m < 8)
		{
			continue;
		}

		if (m - mins[i] < 8)
		{
			continue;
		}

		v = verts + i;

		for (j = 0; j < numverts; j++, v += 3)
		{
			dist[j] = *v - m;
		}

		dist[j] = dist[0];
		v -= i;
		memcpy(v, verts, 3 * sizeof(float));

		f = b = 0;
		v = verts;

		for (j = 0; j < numverts; j++, v += 3)
		{
			if (dist[j] >= 0)
			{
				memcpy(front[f], v, 3 * sizeof(float));
				f++;
			}

			if (dist[j] <= 0)
			{
				memcpy(back[b], v, 3 * sizeof(float));
				b++;
			}

			if ((dist[j] == 0) || (dist[j + 1] == 0))
			{
				continue;
			}

			if ((dist[j] > 0) != (dist[j + 1] > 0))
			{
				frac = dist[j] / (dist[j] - dist[j + 1]);

				for (k = 0; k < 3; k++)
				{
					front[f][k] = back[b][k] = v[k] + frac * (v[3 + k] - v[k]);
				}

				f++;
				b++;
			}
		}

		ref_subdivide(f, front[0], vec0, vec1);
		ref_subdivide(b, back[0], vec0, vec1);
		return;
	}

	if (numverts < 1)
	{
		return;
	}

	total[0] = total[1] = total[2] = 0;
	total_s = 0;
	total_t = 0;

	for (i = 0; i < numverts; i++, verts += 3)
	{
		memcpy(ref_out[ref_nv + i + 1], verts, 3 * sizeof(float));
		s = verts[0] * vec0[0] + verts[1] * vec0[1] + verts[2] * vec0[2];
		t = verts[0] * vec1[0] + verts[1] * vec1[1] + verts[2] * vec1[2];

		total_s += s;
		total_t += t;
		total[0] = verts[0] + total[0];
		total[1] = verts[1] + total[1];
		total[2] = verts[2] + total[2];

		ref_out[ref_nv + i + 1][3] = s;
		ref_out[ref_nv + i + 1][4] = t;
	}

	VectorScale(total, (1.0 / numverts), ref_out[ref_nv]);
	ref_out[ref_nv][3] = total_s / numverts;
	ref_out[ref_nv][4] = total_t / numverts;
	memcpy(ref_out[ref_nv + i + 1], ref_out[ref_nv + 1], 5 * sizeof(float));

	ref_len[ref_np++] = numverts + 2;
	ref_nv += numverts + 2;
}

static float sub_out[RM_WARP_MAX_OUT * RM_WARP_FLOATS];
static unsigned sub_len[RM_WARP_MAX_POLYS];

/*
 * The subdivision of a 128 by 128 square, worked by hand (cut at x = 64,
 * then each half at y = 64: four pieces of six vertices, the first the
 * x >= 64, y >= 64 corner), the pieces that are not cut, the refusals
 * and each cap, and the differential against gl1's own function.
 */
static void
test_warp_subdivide(void)
{
	static const float v0[3] = { 1, 0, 0 }, v1[3] = { 0, 1, 0 };
	static const float sq[12] = { 0, 0, 0, 128, 0, 0, 128, 128, 0, 0, 128, 0 };
	unsigned np;
	unsigned long nv;
	int ok, i;

	CHECK(rm_warp_subdivide(sq, 4, v0, v1, sub_out, 1000, sub_len, 100, &np, &nv) == 1);
	CHECK(np == 4 && nv == 24);
	CHECK(sub_len[0] == 6 && sub_len[1] == 6 && sub_len[2] == 6 && sub_len[3] == 6);
	/* the first piece: centre (96, 96), then its four corners and the first again */
	CHECK(sub_out[0] == 96 && sub_out[1] == 96 && sub_out[2] == 0 && sub_out[3] == 96 && sub_out[4] == 96);
	CHECK(memcmp(sub_out + 5, sub_out + 5 * 5, 5 * sizeof(float)) == 0);
	{
		float xmin = 1e9f, ymin = 1e9f;

		for (i = 1; i <= 4; i++)
		{
			xmin = sub_out[i * 5] < xmin ? sub_out[i * 5] : xmin;
			ymin = sub_out[i * 5 + 1] < ymin ? sub_out[i * 5 + 1] : ymin;
			/* s and t are the dot products with the texture vectors */
			CHECK(sub_out[i * 5 + 3] == sub_out[i * 5] && sub_out[i * 5 + 4] == sub_out[i * 5 + 1]);
		}

		CHECK(xmin == 64 && ymin == 64);
	}
	/* the second piece is x >= 64, y < 64: centre (96, 32) */
	CHECK(sub_out[6 * 5] == 96 && sub_out[6 * 5 + 1] == 32);
	/* the third and fourth, x < 64: centres (32, 96) and (32, 32) */
	CHECK(sub_out[12 * 5] == 32 && sub_out[12 * 5 + 1] == 96 && sub_out[18 * 5] == 32 && sub_out[18 * 5 + 1] == 32);

	/* texture vectors scale s and t, and the centre's are the mean of the vertices' */
	{
		static const float w0[3] = { 2, 0, 0 }, w1[3] = { 0, -1, 0 };

		CHECK(rm_warp_subdivide(sq, 4, w0, w1, sub_out, 1000, sub_len, 100, &np, &nv) && np == 4);
		CHECK(sub_out[3] == 192 && sub_out[4] == -96);

		for (i = 1; i <= 4; i++)
		{
			CHECK(sub_out[i * 5 + 3] == 2 * sub_out[i * 5] && sub_out[i * 5 + 4] == -sub_out[i * 5 + 1]);
		}
	}

	/* a piece of 64 by 64 or less is not cut, nor one narrower than 8 on a side of a grid plane */
	{
		static const float small[12] = { 0, 0, 0, 64, 0, 0, 64, 64, 0, 0, 64, 0 };
		static const float edge[12] = { 56, 0, 0, 72, 0, 0, 72, 8, 0, 56, 8, 0 };
		static const float short_of[12] = { 57, 0, 0, 71, 0, 0, 71, 8, 0, 57, 8, 0 };
		static const float tri[9] = { 0, 0, 0, 16, 0, 0, 0, 16, 0 };

		CHECK(rm_warp_subdivide(small, 4, v0, v1, sub_out, 1000, sub_len, 100, &np, &nv) && np == 1 && nv == 6);
		CHECK(rm_warp_subdivide(edge, 4, v0, v1, sub_out, 1000, sub_len, 100, &np, &nv) && np == 2 && nv == 12);
		/* a side of the grid plane at 64 is cut when 8 or more away on both sides, not when 7 */
		CHECK(rm_warp_subdivide(short_of, 4, v0, v1, sub_out, 1000, sub_len, 100, &np, &nv) && np == 1 && nv == 6);
		CHECK(rm_warp_subdivide(tri, 3, v0, v1, sub_out, 1000, sub_len, 100, &np, &nv) && np == 1 && nv == 5);
	}

	/* refusals leave the counts 0: too few or too many vertices, a coordinate not a number or out of bounds */
	{
		static float big[3 * 61];
		float bad[12];

		memset(big, 0, sizeof(big));
		np = 7;
		nv = 7;
		CHECK(rm_warp_subdivide(sq, 2, v0, v1, sub_out, 1000, sub_len, 100, &np, &nv) == 0 && np == 0 && nv == 0);
		CHECK(rm_warp_subdivide(sq, 0, v0, v1, sub_out, 1000, sub_len, 100, &np, &nv) == 0);
		CHECK(rm_warp_subdivide(sq, -1, v0, v1, sub_out, 1000, sub_len, 100, &np, &nv) == 0);
		CHECK(rm_warp_subdivide(big, 61, v0, v1, sub_out, 1000, sub_len, 100, &np, &nv) == 0);
		CHECK(rm_warp_subdivide(big, 60, v0, v1, sub_out, 1000, sub_len, 100, &np, &nv) == 1);

		memcpy(bad, sq, sizeof(bad));
		bad[4] = NAN;
		CHECK(rm_warp_subdivide(bad, 4, v0, v1, sub_out, 1000, sub_len, 100, &np, &nv) == 0);
		bad[4] = INFINITY;
		CHECK(rm_warp_subdivide(bad, 4, v0, v1, sub_out, 1000, sub_len, 100, &np, &nv) == 0);
		/* exactly the bound is taken */
		bad[4] = 9999.0f;
		CHECK(rm_warp_subdivide(bad, 4, v0, v1, sub_out, RM_WARP_MAX_OUT, sub_len, RM_WARP_MAX_POLYS, &np, &nv) == 1 && np > 100);
		bad[4] = 10000.0f;
		CHECK(rm_warp_subdivide(bad, 4, v0, v1, sub_out, 1000, sub_len, 100, &np, &nv) == 0);
		bad[4] = -10000.0f;
		CHECK(rm_warp_subdivide(bad, 4, v0, v1, sub_out, 1000, sub_len, 100, &np, &nv) == 0);
	}

	/* the caps: the vertices (24 fit, 23 do not), the pieces (4 fit, 3 do not) */
	CHECK(rm_warp_subdivide(sq, 4, v0, v1, sub_out, 24, sub_len, 4, &np, &nv) == 1 && np == 4 && nv == 24);
	np = 7;
	nv = 7;
	CHECK(rm_warp_subdivide(sq, 4, v0, v1, sub_out, 23, sub_len, 4, &np, &nv) == 0 && np == 0 && nv == 0);
	CHECK(rm_warp_subdivide(sq, 4, v0, v1, sub_out, 24, sub_len, 3, &np, &nv) == 0 && np == 0 && nv == 0);
	CHECK(rm_warp_subdivide(sq, 4, v0, v1, sub_out, 0, sub_len, 4, &np, &nv) == 0);

	/* the largest map extent still subdivides inside the surface caps: 19998 square is 313 by 313 pieces */
	{
		static const float huge[12] = { -9999, -9999, 0, 9999, -9999, 0, 9999, 9999, 0, -9999, 9999, 0 };

		CHECK(rm_warp_subdivide(huge, 4, v0, v1, sub_out, RM_WARP_MAX_OUT, sub_len, RM_WARP_MAX_POLYS, &np, &nv) == 0);
		CHECK(np == 0 && nv == 0);
		/* 8192 by 8192, the extent of a map, is 128 by 128 pieces of six vertices */
		{
			static const float map[12] = { -4096, -4096, 0, 4096, -4096, 0, 4096, 4096, 0, -4096, 4096, 0 };

			CHECK(rm_warp_subdivide(map, 4, v0, v1, sub_out, RM_WARP_MAX_OUT, sub_len, RM_WARP_MAX_POLYS, &np, &nv) == 1);
			CHECK(np == 128 * 128 && nv == 128 * 128 * 6);
		}
	}

	/*
	 * a polygon whose pieces pass the vertex bound: 60 vertices zigzagging across the cut plane make
	 * a front piece of about 90 (the polygon is refused, as gl1 drops the map)
	 */
	{
		float zig[60 * 3];

		for (i = 0; i < 60; i++)
		{
			zig[i * 3] = (i & 1) ? 118.0f : 10.0f;
			zig[i * 3 + 1] = (float)i * 2;
			zig[i * 3 + 2] = 0;
		}

		np = 7;
		CHECK(rm_warp_subdivide(zig, 60, v0, v1, sub_out, RM_WARP_MAX_OUT, sub_len, RM_WARP_MAX_POLYS, &np, &nv) == 0 && np == 0);
	}

	/* a piece of exactly 61 vertices is refused: 41 vertices at x = +-16 alternating cut at no plane but the y bound */
	{
		float z61[41 * 3];
		static const float a0[3] = { 1, 0, 0 }, a1[3] = { 0, 1, 0 };

		for (i = 0; i < 41; i++)
		{
			z61[i * 3] = (i == 40 || !(i & 1)) ? 16.0f : -16.0f;
			z61[i * 3 + 1] = (float)i * 0.1f;
			z61[i * 3 + 2] = 0;
		}

		np = 7;
		CHECK(rm_warp_subdivide(z61, 41, a0, a1, sub_out, RM_WARP_MAX_OUT, sub_len, RM_WARP_MAX_POLYS, &np, &nv) == 0 && np == 0);
	}

	/* structured polygons against gl1's function: vertices on a cut plane, a cut through a vertex, 8 units apart */
	{
		static const float polys[][12] = {
			{ 64, 0, 0, 128, 64, 0, 64, 128, 0, 0, 64, 0 },       /* a diamond, two vertices on x = 64 */
			{ 0, 0, 0, 64, 0, 0, 64, 64, 0, 0, 64, 0 },           /* a side on the plane */
			{ 0, 0, 0, 128, 0, 0, 128, 64, 0, 0, 64, 0 },         /* a side on y = 64 */
			{ -20, -20, 0, 20, -20, 0, 20, 20, 0, -20, 20, 0 },   /* a vertex pair about the origin plane */
			{ 56, 56, 0, 72, 56, 0, 72, 72, 0, 56, 72, 0 },       /* 8 units from the planes on each side */
			{ 57, 56, 0, 71, 56, 0, 71, 72, 0, 57, 72, 0 },       /* 7 units in x */
			{ 0, 0, 0, 200, 0, 0, 200, 200, 0, 0, 200, 0 },
			{ 64, 64, 0, 192, 64, 0, 192, 192, 0, 64, 192, 0 },   /* on the grid: not cut */
		};
		static const float w0[3] = { 0.5f, 0.25f, -1 }, w1[3] = { -2, 1, 0.125f };
		size_t k;

		ok = 1;

		for (k = 0; k < sizeof(polys) / sizeof(polys[0]); k++)
		{
			float copy[12 + 3];
			unsigned long rnv = 0;
			unsigned rnp = 0;

			memcpy(copy, polys[k], sizeof(polys[k]));
			ref_np = ref_nv = 0;
			ref_subdivide(4, copy, w0, w1);
			ok &= rm_warp_subdivide(polys[k], 4, w0, w1, sub_out, RM_WARP_MAX_OUT, sub_len, RM_WARP_MAX_POLYS, &rnp, &rnv);
			ok &= ((int)rnp == ref_np) && ((int)rnv == ref_nv) && (memcmp(sub_out, ref_out, (size_t)ref_nv * 5 * sizeof(float)) == 0);
		}

		CHECK(ok);
		/* the diamond: four pieces after the x and the y cut, each a triangle of the cut (centre, 3 vertices, 1) */
		CHECK(rm_warp_subdivide(polys[0], 4, v0, v1, sub_out, 1000, sub_len, 100, &np, &nv) == 1 && np == 4 && nv == 4 * 5);
		CHECK(sub_len[0] == 5 && sub_len[1] == 5 && sub_len[2] == 5 && sub_len[3] == 5);
	}

	/* differential: random convex quads and triangles on random planes against gl1's R_SubdividePolygon */
	ok = 1;

	for (i = 0; i < 3000; i++)
	{
		float verts[4 * 3], copy[65 * 3];
		float cx = rngf(-3000, 3000), cy = rngf(-3000, 3000), cz = rngf(-3000, 3000);
		float a = rngf(8, 700), b = rngf(8, 700);
		int n = (rng() & 1) ? 4 : 3, j, axis = rng() % 3, a1 = (axis + 1) % 3, a2 = (axis + 2) % 3;
		float w0[3] = { rngf(-2, 2), rngf(-2, 2), rngf(-2, 2) }, w1[3] = { rngf(-2, 2), rngf(-2, 2), rngf(-2, 2) };
		static const float sx[4] = { -1, 1, 1, -1 }, sy[4] = { -1, -1, 1, 1 };
		unsigned long rnv;
		unsigned rnp;

		for (j = 0; j < n; j++)
		{
			verts[j * 3 + axis] = cz + rngf(-0.5f, 0.5f) * 0;
			verts[j * 3 + a1] = cx + sx[j] * a;
			verts[j * 3 + a2] = cy + sy[j] * b;
		}

		memcpy(copy, verts, sizeof(verts));
		ref_np = ref_nv = 0;
		ref_subdivide(n, copy, w0, w1);
		rnv = 0;
		rnp = 0;

		if (rm_warp_subdivide(verts, n, w0, w1, sub_out, RM_WARP_MAX_OUT, sub_len, RM_WARP_MAX_POLYS, &rnp, &rnv) == 0)
		{
			ok = 0;
			break;
		}

		ok &= ((int)rnp == ref_np) && ((int)rnv == ref_nv);

		if (ok)
		{
			int p, at = 0;

			for (p = 0; p < ref_np; p++)
			{
				ok &= ((int)sub_len[p] == ref_len[p]);
				ok &= (memcmp(sub_out + (size_t)at * 5, ref_out[at], (size_t)ref_len[p] * 5 * sizeof(float)) == 0);
				at += ref_len[p];
			}
		}

		if (!ok)
		{
			break;
		}
	}

	CHECK(ok);
}

/* gl1's sky clip and projection, unbounded, as the reference of the differential cases */
static float ref_skymins[2][6], ref_skymaxs[2][6];

static const int ref_vec_to_st[6][3] = {
	{-2, 3, 1}, {2, 3, -1}, {1, 3, 2}, {-1, 3, -2}, {-2, -1, 3}, {-2, 1, -3}
};

static const float ref_skyclip[6][3] = {
	{1, 1, 0}, {1, -1, 0}, {0, -1, 1}, {0, 1, 1}, {1, 0, 1}, {-1, 0, 1}
};

static void
ref_draw_sky_polygon(int nump, float *vecs)
{
	int i, j;
	float v[3], av[3];
	float s, t, dv;
	int axis;
	float *vp;

	v[0] = v[1] = v[2] = 0;

	for (i = 0, vp = vecs; i < nump; i++, vp += 3)
	{
		v[0] = vp[0] + v[0];
		v[1] = vp[1] + v[1];
		v[2] = vp[2] + v[2];
	}

	av[0] = fabs(v[0]);
	av[1] = fabs(v[1]);
	av[2] = fabs(v[2]);

	if ((av[0] > av[1]) && (av[0] > av[2]))
	{
		axis = v[0] < 0 ? 1 : 0;
	}
	else if ((av[1] > av[2]) && (av[1] > av[0]))
	{
		axis = v[1] < 0 ? 3 : 2;
	}
	else
	{
		axis = v[2] < 0 ? 5 : 4;
	}

	for (i = 0; i < nump; i++, vecs += 3)
	{
		j = ref_vec_to_st[axis][2];
		dv = j > 0 ? vecs[j - 1] : -vecs[-j - 1];

		if (dv < 0.001)
		{
			continue;
		}

		j = ref_vec_to_st[axis][0];
		s = j < 0 ? -vecs[-j - 1] / dv : vecs[j - 1] / dv;
		j = ref_vec_to_st[axis][1];
		t = j < 0 ? -vecs[-j - 1] / dv : vecs[j - 1] / dv;

		if (s < ref_skymins[0][axis])
		{
			ref_skymins[0][axis] = s;
		}

		if (t < ref_skymins[1][axis])
		{
			ref_skymins[1][axis] = t;
		}

		if (s > ref_skymaxs[0][axis])
		{
			ref_skymaxs[0][axis] = s;
		}

		if (t > ref_skymaxs[1][axis])
		{
			ref_skymaxs[1][axis] = t;
		}
	}
}

static void
ref_clip_sky(int nump, float *vecs, int stage)
{
	const float *norm;
	float *v;
	int front, back;
	float d, e;
	float dists[64];
	int sides[64];
	float newv[2][64][3];
	int newc[2];
	int i, j;

	if (stage == 6)
	{
		ref_draw_sky_polygon(nump, vecs);
		return;
	}

	front = back = 0;
	norm = ref_skyclip[stage];

	for (i = 0, v = vecs; i < nump; i++, v += 3)
	{
		d = v[0] * norm[0] + v[1] * norm[1] + v[2] * norm[2];

		if (d > 0.1)
		{
			front = 1;
			sides[i] = 0;
		}
		else if (d < -0.1)
		{
			back = 1;
			sides[i] = 1;
		}
		else
		{
			sides[i] = 2;
		}

		dists[i] = d;
	}

	if (!front || !back)
	{
		ref_clip_sky(nump, vecs, stage + 1);
		return;
	}

	sides[i] = sides[0];
	dists[i] = dists[0];
	memcpy(vecs + i * 3, vecs, 3 * sizeof(float));
	newc[0] = newc[1] = 0;

	for (i = 0, v = vecs; i < nump; i++, v += 3)
	{
		switch (sides[i])
		{
			case 0:
				memcpy(newv[0][newc[0]++], v, 12);
				break;
			case 1:
				memcpy(newv[1][newc[1]++], v, 12);
				break;
			case 2:
				memcpy(newv[0][newc[0]++], v, 12);
				memcpy(newv[1][newc[1]++], v, 12);
				break;
		}

		if ((sides[i] == 2) || (sides[i + 1] == 2) || (sides[i + 1] == sides[i]))
		{
			continue;
		}

		d = dists[i] / (dists[i] - dists[i + 1]);

		for (j = 0; j < 3; j++)
		{
			e = v[j] + d * (v[j + 3] - v[j]);
			newv[0][newc[0]][j] = e;
			newv[1][newc[1]][j] = e;
		}

		newc[0]++;
		newc[1]++;
	}

	ref_clip_sky(newc[0], newv[0][0], stage + 1);
	ref_clip_sky(newc[1], newv[1][0], stage + 1);
}

/*
 * The sky clip of a quad that crosses the planes x = y (the +x and +y
 * faces), worked by hand from gl1_warp.c, the face and bounds a polygon
 * on one face and one behind the view get, the bounds of the box, the
 * corner vectors and the coordinate clamp, the limits, and a
 * differential against gl1's R_ClipSkyPolygon.
 */
static void
test_sky(void)
{
	rm_skybounds_t b;
	int i, f, ok;
	float v[3], st[2];

	/* P0 (100, 50, 10), P1 (50, 100, 10), P2 (50, 100, -10), P3 (100, 50, -10): cut at x = y into two halves */
	{
		static const float quad[12] = { 100, 50, 10, 50, 100, 10, 50, 100, -10, 100, 50, -10 };

		rm_sky_clear(&b);

		for (f = 0; f < 6; f++)
		{
			CHECK(b.mins[0][f] == 9999 && b.mins[1][f] == 9999 && b.maxs[0][f] == -9999 && b.maxs[1][f] == -9999);
			CHECK(!rm_sky_face_visible(&b, f));
		}

		CHECK(rm_sky_clip(&b, 4, quad) == 1);
		/* the front half (75 75 10, 75 75 -10 and P0, P3) is on +x: s = -y / x in [-1, -0.5], t = z / x */
		CHECK(b.mins[0][0] == -1 && b.maxs[0][0] == -0.5f);
		CHECK(b.mins[1][0] == -10.0f / 75 && b.maxs[1][0] == 10.0f / 75);
		/* the back half is on +y: s = x / y in [0.5, 1], t = z / y */
		CHECK(b.mins[0][2] == 0.5f && b.maxs[0][2] == 1);
		CHECK(b.mins[1][2] == -10.0f / 75 && b.maxs[1][2] == 10.0f / 75);
		CHECK(rm_sky_face_visible(&b, 0) && rm_sky_face_visible(&b, 2));

		for (f = 0; f < 6; f++)
		{
			if (f != 0 && f != 2)
			{
				CHECK(b.mins[0][f] == 9999 && b.maxs[0][f] == -9999 && !rm_sky_face_visible(&b, f));
			}
		}
	}

	/* the same quad wholly on +x (x - y all positive): one face, not clipped */
	{
		static const float on_x[12] = { 100, 10, 10, 100, 20, 10, 100, 20, -10, 100, 10, -10 };

		rm_sky_clear(&b);
		CHECK(rm_sky_clip(&b, 4, on_x) == 1);
		CHECK(b.mins[0][0] == -0.2f && b.maxs[0][0] == -0.1f && b.mins[1][0] == -0.1f && b.maxs[1][0] == 0.1f);
		CHECK(!rm_sky_face_visible(&b, 1) && !rm_sky_face_visible(&b, 2));
	}

	/* a polygon behind the view (z <= 0.001 over the face's depth) adds nothing: a triangle on -x, dv < 0.001 skipped */
	{
		static const float flat[9] = { 0, 10, 10, 0, 20, 10, 0, 20, -10 };

		rm_sky_clear(&b);
		CHECK(rm_sky_clip(&b, 3, flat) == 1);

		for (f = 0; f < 6; f++)
		{
			CHECK(!rm_sky_face_visible(&b, f));
		}
	}

	/* the limits: 62 vertices are taken (a ring of them, not clipped away), 63 and a negative count are not */
	{
		float ring[64 * 3];

		for (i = 0; i < 64; i++)
		{
			ring[i * 3] = 1000;
			ring[i * 3 + 1] = (float)(i % 8) * 0.5f;
			ring[i * 3 + 2] = (float)(i / 8) * 0.5f;
		}

		rm_sky_clear(&b);
		CHECK(rm_sky_clip(&b, 62, ring) == 1);
		CHECK(rm_sky_clip(&b, 63, ring) == 0);
		CHECK(rm_sky_clip(&b, 64, ring) == 0);
		CHECK(rm_sky_clip(&b, 1000, ring) == 0);
		CHECK(rm_sky_clip(&b, -1, ring) == 0);
		CHECK(rm_sky_clip(&b, 0, ring) == 1);
	}

	/*
	 * a polygon that is not convex crosses a plane more often than a convex one: 62 vertices that alternate
	 * across x + y = 0 give 31 + 62 vertices to a side, more than the buffers hold, so the polygon is
	 * refused (the sanitizers watch the buffers); 20 of them fit and are clipped as gl1 clips them
	 */
	{
		float zig[62 * 3], copy[65 * 3];

		for (i = 0; i < 62; i++)
		{
			zig[i * 3] = (i & 1) ? -500.0f : 500.0f;
			zig[i * 3 + 1] = (i & 1) ? 400.0f : -400.0f;
			zig[i * 3 + 2] = 5.0f + (float)i;
		}

		rm_sky_clear(&b);
		CHECK(rm_sky_clip(&b, 62, zig) == 0);

		memcpy(copy, zig, sizeof(zig));
		rm_sky_clear(&b);
		CHECK(rm_sky_clip(&b, 20, zig) == 1);

		for (f = 0; f < 6; f++)
		{
			ref_skymins[0][f] = ref_skymins[1][f] = 9999;
			ref_skymaxs[0][f] = ref_skymaxs[1][f] = -9999;
		}

		ref_clip_sky(20, copy, 0);
		CHECK(memcmp(b.mins, ref_skymins, sizeof(ref_skymins)) == 0 && memcmp(b.maxs, ref_skymaxs, sizeof(ref_skymaxs)) == 0);
	}

	/*
	 * a polygon whose clip leaves a side of 64 vertices without the guard on the way there (62 vertices, two
	 * of them behind the first plane, the last edge crossing it): the next stage refuses it
	 */
	{
		float arc[62 * 3];

		for (i = 0; i < 62; i++)
		{
			arc[i * 3] = 500.0f + (float)i;
			arc[i * 3 + 1] = 10.0f;
			arc[i * 3 + 2] = 5.0f + (float)i;
		}

		arc[0] = arc[30 * 3] = -500.0f;
		arc[1] = arc[30 * 3 + 1] = -500.0f;
		rm_sky_clear(&b);
		CHECK(rm_sky_clip(&b, 62, arc) == 0);
	}

	/* structured polygons against gl1's clip: vertices on the planes, and 0.15 off them, a depth just over 0.001 */
	{
		static const float polys[][12] = {
			{ 100, 100, 5, 120, 100, -5, 150, 150, 0, 90, 90, 0 },       /* vertices on x = y */
			{ 100, 99.85f, 5, 100, 100.15f, 5, 80, 80, -5, 120, 120, -5 },   /* 0.15 off the plane, as 0 and as -0.15 */
			{ 100, 99.95f, 5, 100, 100.05f, 5, 80, 80, -5, 120, 120, -5 },   /* within 0.1 of it: on the plane */
			{ 0.002f, 10, 10, 0.002f, 20, 10, 0.002f, 20, -10, 0.002f, 10, -10 }, /* a depth just over 0.001 on the +x face */
			{ 0.0005f, 10, 10, 0.0005f, 20, 10, 0.0005f, 20, -10, 0.0005f, 10, -10 },
			{ 100, 100, 100, 100, -100, 100, -100, -100, 100, -100, 100, 100 },
			{ 50, 0, 10, 0, 50, 10, -50, 0, 10, 0, -50, 10 },
			{ 10, 10, -3, 10, 10, 3, 10, -10, 3, 10, -10, -3 },
		};
		size_t k;

		ok = 1;

		for (k = 0; k < sizeof(polys) / sizeof(polys[0]); k++)
		{
			float vecs[65 * 3], copy[65 * 3];

			memcpy(vecs, polys[k], sizeof(polys[k]));
			memcpy(copy, polys[k], sizeof(polys[k]));
			rm_sky_clear(&b);

			for (f = 0; f < 6; f++)
			{
				ref_skymins[0][f] = ref_skymins[1][f] = 9999;
				ref_skymaxs[0][f] = ref_skymaxs[1][f] = -9999;
			}

			ref_clip_sky(4, copy, 0);
			ok &= rm_sky_clip(&b, 4, vecs);
			ok &= (memcmp(b.mins, ref_skymins, sizeof(ref_skymins)) == 0) && (memcmp(b.maxs, ref_skymaxs, sizeof(ref_skymaxs)) == 0);
		}

		CHECK(ok);
	}

	/* a rotating sky is the whole box once any face is seen, and nothing when none is */
	rm_sky_clear(&b);
	CHECK(rm_sky_prepare(&b, 1) == 0);
	CHECK(rm_sky_prepare(&b, 0) == 1 && !rm_sky_face_visible(&b, 0));
	b.mins[0][3] = -0.5f;
	b.maxs[0][3] = 0.5f;
	b.mins[1][3] = 0.1f;
	b.maxs[1][3] = 0.2f;
	CHECK(rm_sky_prepare(&b, 0) == 1 && b.mins[0][3] == -0.5f && !rm_sky_face_visible(&b, 0));
	/* a face with one empty side is not seen: equal bounds on s with a t range, and a s range with equal t */
	rm_sky_clear(&b);
	b.mins[0][2] = 0.25f;
	b.maxs[0][2] = 0.25f;
	b.mins[1][2] = 0;
	b.maxs[1][2] = 1;
	CHECK(rm_sky_prepare(&b, 1) == 0);
	rm_sky_clear(&b);
	b.mins[0][2] = 0;
	b.maxs[0][2] = 1;
	b.mins[1][2] = 0.5f;
	b.maxs[1][2] = 0.5f;
	CHECK(rm_sky_prepare(&b, 1) == 0);
	b.maxs[1][2] = 0.75f;
	CHECK(rm_sky_prepare(&b, 1) == 1);
	CHECK(rm_sky_prepare(&b, 0.5f) == 1);

	for (f = 0; f < 6; f++)
	{
		CHECK(b.mins[0][f] == -1 && b.mins[1][f] == -1 && b.maxs[0][f] == 1 && b.maxs[1][f] == 1);
		CHECK(rm_sky_face_visible(&b, f));
	}

	/* a face with one empty side is not drawn: equal bounds, or min above max */
	rm_sky_clear(&b);
	b.mins[0][1] = 0;
	b.maxs[0][1] = 0;
	b.mins[1][1] = -1;
	b.maxs[1][1] = 1;
	CHECK(!rm_sky_face_visible(&b, 1));
	b.maxs[0][1] = 0.25f;
	CHECK(rm_sky_face_visible(&b, 1));
	b.mins[1][1] = 1;
	CHECK(!rm_sky_face_visible(&b, 1));
	CHECK(!rm_sky_face_visible(&b, -1) && !rm_sky_face_visible(&b, 6));

	/* the sky image of each face: gl1's skytexorder */
	CHECK(rm_sky_image_of_face(0) == 0 && rm_sky_image_of_face(1) == 2 && rm_sky_image_of_face(2) == 1);
	CHECK(rm_sky_image_of_face(3) == 3 && rm_sky_image_of_face(4) == 4 && rm_sky_image_of_face(5) == 5);
	CHECK(rm_sky_image_of_face(-1) == -1 && rm_sky_image_of_face(6) == -1);

	/* the corner of a face: positions on the box of 2300 or 4096, coordinates clamped to 1/512 .. 511/512, t flipped */
	CHECK(rm_sky_vec(-1, -1, 0, 0, v, st) == 1);
	CHECK(v[0] == 2300 && v[1] == 2300 && v[2] == -2300);
	CHECK(st[0] == 1.0f / 512 && st[1] == 1 - 1.0f / 512);
	CHECK(rm_sky_vec(1, 1, 0, 0, v, st) == 1);
	CHECK(v[0] == 2300 && v[1] == -2300 && v[2] == 2300);
	CHECK(st[0] == 511.0f / 512 && st[1] == 1 - 511.0f / 512);
	CHECK(rm_sky_vec(1, 1, 0, 1, v, st) == 1 && v[0] == 4096 && v[1] == -4096 && v[2] == 4096);
	CHECK(rm_sky_vec(0.5f, -0.5f, 4, 0, v, st) == 1);
	CHECK(v[0] == 1150 && v[1] == -1150 && v[2] == 2300 && st[0] == 0.75f && st[1] == 0.75f);
	/* the clamp: just inside and exactly at the ends, and outside the box */
	CHECK(rm_sky_vec(-0.99f, 0, 2, 0, v, st) == 1 && st[0] > 1.0f / 512 && st[0] < 0.01f);
	CHECK(rm_sky_vec(-2, 3, 2, 0, v, st) == 1 && st[0] == 1.0f / 512 && st[1] == 1 - 511.0f / 512);
	CHECK(rm_sky_vec(2, -3, 2, 0, v, st) == 1 && st[0] == 511.0f / 512 && st[1] == 1 - 1.0f / 512);
	CHECK(rm_sky_vec(0, 0, 3, 0, v, st) == 1 && st[0] == 0.5f && st[1] == 0.5f);
	/* each face's axes: s = 0.5, t = 0.25, so b = (1150, 575, 2300) */
	{
		static const float want[6][3] = {
			{ 2300, -1150, 575 }, { -2300, 1150, 575 }, { 1150, 2300, 575 },
			{ -1150, -2300, 575 }, { -575, -1150, 2300 }, { 575, -1150, -2300 }
		};

		ok = 1;

		for (f = 0; f < 6; f++)
		{
			ok &= rm_sky_vec(0.5f, 0.25f, f, 0, v, st);
			ok &= (v[0] == want[f][0]) && (v[1] == want[f][1]) && (v[2] == want[f][2]);
		}

		CHECK(ok);
	}
	CHECK(rm_sky_vec(0, 0, -1, 0, v, st) == 0 && rm_sky_vec(0, 0, 6, 0, v, st) == 0);

	/* the sky name: letters, digits, _ - . and not a path */
	CHECK(rm_sky_name_ok("unit1_") && rm_sky_name_ok("sky-2.x") && rm_sky_name_ok("a"));
	/* the ends of each class of characters */
	CHECK(rm_sky_name_ok("az") && rm_sky_name_ok("AZ") && rm_sky_name_ok("09") && rm_sky_name_ok("zZ9aA0"));
	CHECK(!rm_sky_name_ok("`") && !rm_sky_name_ok("{") && !rm_sky_name_ok("@") && !rm_sky_name_ok("[") &&
		!rm_sky_name_ok("/") && !rm_sky_name_ok(":"));
	CHECK(!rm_sky_name_ok(NULL) && !rm_sky_name_ok("") && !rm_sky_name_ok(".hidden") && !rm_sky_name_ok("a..b"));
	CHECK(!rm_sky_name_ok("../x") && !rm_sky_name_ok("a/b") && !rm_sky_name_ok("a\\b") && !rm_sky_name_ok("c:x"));
	CHECK(!rm_sky_name_ok("a b") && !rm_sky_name_ok("a\nb") && !rm_sky_name_ok("a%00") && !rm_sky_name_ok("\x7f"));
	CHECK(!rm_sky_name_ok("\xc3\xa9") && !rm_sky_name_ok("a*"));
	{
		char name[RM_SKY_NAME_MAX + 3];

		memset(name, 'a', sizeof(name));
		name[RM_SKY_NAME_MAX] = '\0';
		CHECK(rm_sky_name_ok(name));
		name[RM_SKY_NAME_MAX] = 'a';
		name[RM_SKY_NAME_MAX + 1] = '\0';
		CHECK(!rm_sky_name_ok(name));
		name[RM_SKY_NAME_MAX - 1] = '.';
		name[RM_SKY_NAME_MAX] = '\0';
		CHECK(rm_sky_name_ok(name));
	}

	/* the rotation and axis: finite */
	{
		static const float ax[3] = { 0, 0, 1 };
		float bad[3] = { 0, NAN, 1 };

		CHECK(rm_sky_params_ok(10, ax) && rm_sky_params_ok(0, ax));
		CHECK(!rm_sky_params_ok(NAN, ax) && !rm_sky_params_ok(INFINITY, ax) && !rm_sky_params_ok(-INFINITY, ax));
		CHECK(!rm_sky_params_ok(1, bad));
		bad[1] = 0;
		bad[0] = INFINITY;
		CHECK(!rm_sky_params_ok(1, bad));
		bad[0] = 0;
		bad[2] = -INFINITY;
		CHECK(!rm_sky_params_ok(1, bad));
	}

	/* the sky modelview: the world times the translation to the origin times the rotation */
	{
		float world[16], out[16], org[3] = { 1, 2, 3 }, axz[3] = { 0, 0, 1 }, zero[3] = { 0, 0, 0 };

		mtl_mat_identity(world);
		rm_sky_matrix(out, world, org, 5, 0, axz);
		CHECK(out[12] == 1 && out[13] == 2 && out[14] == 3 && out[0] == 1 && out[5] == 1 && out[10] == 1 && out[15] == 1);
		/* 5 * 18 = 90 degrees about z: the columns turn, the translation of the product (T * R) stays */
		rm_sky_matrix(out, world, org, 5, 18, axz);
		CHECK(out[12] == 1 && out[13] == 2 && out[14] == 3);
		CHECK(fabsf(out[0]) < 1e-6f && fabsf(out[1] - 1) < 1e-6f && fabsf(out[4] + 1) < 1e-6f && fabsf(out[5]) < 1e-6f);
		/* the time scales the angle: 2 * 45 = 90, the same matrix */
		{
			float out2[16];

			rm_sky_matrix(out2, world, org, 2, 45, axz);
			CHECK(memcmp(out, out2, sizeof(out)) == 0);
		}
		/* a zero axis leaves the translation alone (the shim's glRotatef about nothing) */
		rm_sky_matrix(out, world, org, 5, 18, zero);
		CHECK(out[0] == 1 && out[1] == 0 && out[12] == 1);
		/* the world matrix comes first: a world translation (10, 0, 0) is carried through */
		world[12] = 10;
		rm_sky_matrix(out, world, org, 0, 0, axz);
		CHECK(out[12] == 11 && out[13] == 2 && out[14] == 3);
		/* and it comes first as a product: a world turned 90 degrees about z carries the origin (1, 0, 0) to (0, 1, 0) */
		{
			float turned[16], ax_x[3] = { 1, 0, 0 }, o1[3] = { 1, 0, 0 };

			mtl_mat_identity(turned);
			CHECK(mtl_mat_rotate(turned, 90, 0, 0, 1) == 1);
			rm_sky_matrix(out, turned, o1, 0, 0, axz);
			CHECK(fabsf(out[12]) < 1e-6f && fabsf(out[13] - 1) < 1e-6f && out[14] == 0);
			/* the axis is (x, y, z) in that order: 90 degrees about x turns y into z */
			mtl_mat_identity(turned);
			rm_sky_matrix(out, turned, org, 1, 90, ax_x);
			CHECK(fabsf(out[5]) < 1e-6f && fabsf(out[6] - 1) < 1e-6f && fabsf(out[9] + 1) < 1e-6f && fabsf(out[10]) < 1e-6f);
			CHECK(out[12] == 1 && out[13] == 2 && out[14] == 3);
		}
	}

	/* differential: random polygons of 3 to 8 vertices around the origin against gl1's R_ClipSkyPolygon */
	ok = 1;

	for (i = 0; i < 20000; i++)
	{
		float vecs[65 * 3], copy[65 * 3];
		int n = 3 + rng() % 6, j;

		for (j = 0; j < n * 3; j++)
		{
			vecs[j] = rngf(-1500, 1500);
		}

		memcpy(copy, vecs, sizeof(vecs));
		rm_sky_clear(&b);

		for (j = 0; j < 6; j++)
		{
			ref_skymins[0][j] = ref_skymins[1][j] = 9999;
			ref_skymaxs[0][j] = ref_skymaxs[1][j] = -9999;
		}

		ref_clip_sky(n, copy, 0);
		ok &= rm_sky_clip(&b, n, vecs);
		ok &= (memcmp(b.mins, ref_skymins, sizeof(ref_skymins)) == 0) && (memcmp(b.maxs, ref_skymaxs, sizeof(ref_skymaxs)) == 0);
	}

	CHECK(ok);
}

/*
 * The translucent colours, the blend of one over a known destination
 * and the opaque water's environment: the shader's modulate is the
 * shim's, and the numbers are those of gl1 (glColor4f(inverse_intensity,
 * alpha), GL_MODULATE, GL_SRC_ALPHA / GL_ONE_MINUS_SRC_ALPHA).
 */
void rm_host_shim_modulate(const float *p, const float *t, float *out);

static void
test_trans(void)
{
	float rgba[4], ii, tex[4] = { 1, 0.5f, 0.25f, 1 }, c[4], dst[3] = { 0.2f, 0.4f, 0.6f }, got[3];
	mtl_rgba_t p, t, m;

	CHECK(rm_trans_alpha(RM_TEX_TRANS33) == 0.33f && rm_trans_alpha(RM_TEX_TRANS66) == 0.66f);
	CHECK(rm_trans_alpha(RM_TEX_TRANS33 | RM_TEX_TRANS66) == 0.33f);
	CHECK(rm_trans_alpha(0) == 1 && rm_trans_alpha(RM_TEX_WARP | RM_TEX_FLOWING | RM_TEX_SKY) == 1);
	CHECK(rm_trans_alpha(RM_TEX_TRANS66 | RM_TEX_WARP) == 0.66f && rm_trans_alpha(RM_TEX_TRANS33 | RM_TEX_FLOWING) == 0.33f);

	CHECK(rm_inverse_intensity(1, 2) == 0.5f && rm_inverse_intensity(1, 1) == 1 && rm_inverse_intensity(1.5f, 2) == 0.75f);
	ii = rm_inverse_intensity(1, 2);
	rm_trans_color(ii, 0.33f, rgba);
	CHECK(rgba[0] == 0.5f && rgba[1] == 0.5f && rgba[2] == 0.5f && rgba[3] == 0.33f);
	rm_trans_color(ii, 0.66f, rgba);
	CHECK(rgba[0] == 0.5f && rgba[3] == 0.66f);

	/* the shader's modulate is the shim's on every channel */
	p.r = rgba[0];
	p.g = rgba[1];
	p.b = rgba[2];
	p.a = rgba[3];
	t.r = tex[0];
	t.g = tex[1];
	t.b = tex[2];
	t.a = tex[3];
	m = mtl_world_color(p, t);
	rm_host_shim_modulate(rgba, tex, c);
	CHECK(m.r == c[0] && m.g == c[1] && m.b == c[2] && m.a == c[3]);
	CHECK(m.r == 0.5f && m.g == 0.25f && m.b == 0.125f && m.a == 0.66f);

	/* blended over the destination (0.2, 0.4, 0.6) at alpha 0.66: src * a + dst * (1 - a) */
	got[0] = m.r * m.a + dst[0] * (1 - m.a);
	got[1] = m.g * m.a + dst[1] * (1 - m.a);
	got[2] = m.b * m.a + dst[2] * (1 - m.a);
	CHECK(fabsf(got[0] - (0.5f * 0.66f + 0.2f * 0.34f)) < 1e-6f);
	CHECK(fabsf(got[1] - (0.25f * 0.66f + 0.4f * 0.34f)) < 1e-6f);
	CHECK(fabsf(got[2] - (0.125f * 0.66f + 0.6f * 0.34f)) < 1e-6f);
	CHECK(fabsf(got[0] - 0.398f) < 1e-6f && fabsf(got[1] - 0.301f) < 1e-6f && fabsf(got[2] - 0.2865f) < 1e-6f);

	/* a texel with alpha 0 (the transparent palette entry of a wall texture) leaves the destination */
	tex[3] = 0;
	rm_host_shim_modulate(rgba, tex, c);
	CHECK(c[3] == 0);
	t.a = 0;
	m = mtl_world_color(p, t);
	CHECK(m.a == c[3] && m.a == 0);

	/* the opaque water: GL_MODULATE with the inverse intensity, or the texel itself with overbright bits */
	CHECK(rm_warp_color(0, 0.5f, rgba) == 1 && rgba[0] == 0.5f && rgba[1] == 0.5f && rgba[2] == 0.5f && rgba[3] == 1);
	CHECK(rm_warp_color(1, 0.5f, rgba) == 0 && rgba[0] == 1 && rgba[1] == 1 && rgba[2] == 1 && rgba[3] == 1);
	CHECK(rm_warp_color(4, 0.25f, rgba) == 0);
	CHECK(rm_warp_color(2, 0.25f, rgba) == 0 && rm_warp_color(0, 0.25f, rgba) == 1 && rgba[0] == 0.25f);

	/* the batch of the alpha chain breaks where gl1's R_UpdateGLBuffer draws: another texture or another alpha */
	CHECK(!rm_alpha_break(0, 3, 0.33f, 4, 0.66f));       /* nothing batched yet */
	CHECK(!rm_alpha_break(5, 3, 0.33f, 3, 0.33f));
	CHECK(rm_alpha_break(5, 3, 0.33f, 4, 0.33f));
	CHECK(rm_alpha_break(5, 3, 0.33f, 3, 0.66f));
	CHECK(rm_alpha_break(1, 3, 0.33f, 4, 0.66f));
	CHECK(!rm_alpha_break(-1, 3, 0.33f, 4, 0.66f));
	CHECK(rm_alpha_break(1, 5, 0.66f, 5, 0.33f));   /* the alpha going down breaks the batch too */
	CHECK(rm_world_frame(3.4f) == 6 && rm_world_frame(0) == 0 && rm_world_frame(-1.2f) == -2);
	CHECK(rm_world_frame(6e9f) == 2147483647 && rm_world_frame(-6e9f) == (-2147483647 - 1) && rm_world_frame(NAN) == 0);
	CHECK(rm_world_frame(0.6f) == 1 && rm_world_frame(-0.6f) == -1);
}
/* gl1's SHADEDOT_QUANT (gl1_mesh.c) and BACKFACE_EPSILON (gl1/header/local.h) */
#define SHADEDOT_QUANT_REF 16
#define BACKFACE_EPSILON_REF 0.01

/* ---- N7: alias models, the view weapon and brush entities ----------------------------- */

/* gl1_mesh.c's tables, from the files it includes */
static float ref_avertexnormals[162][3] = {
#include "../../src/client/refresh/constants/anorms.h"
};

static float ref_avertexnormal_dots[16][256] = {
#include "../../src/client/refresh/constants/anormtab.h"
};

/*
 * The shared loader (files/models.c) is linked to hold rm_md2_check and
 * rm_sp2_check to it: a hunk in malloc'd memory with the bound of
 * src/backends/unix/shared/hunk.c, and the loader's other callees.
 */
static unsigned char *hunk_mem;
static size_t hunk_max, hunk_used;
static int hunk_overflow, find_calls;

void *
Hunk_Begin(int maxsize)
{
	free(hunk_mem);
	hunk_max = (size_t)maxsize + sizeof(size_t) + 32;
	hunk_used = 0;
	hunk_mem = calloc(1, hunk_max + sizeof(size_t));
	return hunk_mem ? hunk_mem + sizeof(size_t) : NULL;
}

void *
Hunk_Alloc(int size)
{
	unsigned char *buf;

	size = (size + 31) & ~31;

	if (hunk_used + size > hunk_max)
	{
		hunk_overflow++;
		return NULL;
	}

	buf = hunk_mem + sizeof(size_t) + hunk_used;
	hunk_used += size;
	return buf;
}

void
Com_Error(int code, const char *fmt, ...)
{
	/* the loader's paths taken here never call it */
	printf("FAIL: Com_Error(%d, %s)\n", code, fmt);
	abort();
}

struct image_s *
GetTexImage(const char *name, findimage_t find_image)
{
	(void)name;
	(void)find_image;
	return NULL;
}

/* the loader's find_image: any name, read to its end (ASAN sees a name past its record) */
static struct image_s *
host_find_image(const char *name, imagetype_t type)
{
	static int dummy;

	(void)type;
	find_calls++;
	return strlen(name) < 1000 ? (struct image_s *)&dummy : NULL;
}

/* gl1_mesh.c, R_LerpVerts, verbatim (the entity's flags passed in) */
static void
ref_LerpVerts(int flags, int nverts, dtrivertx_t *v, dtrivertx_t *ov,
		dtrivertx_t *verts, float *lerp, float move[3],
		float frontv[3], float backv[3])
{
	int i;

	if (flags &
		(RF_SHELL_RED | RF_SHELL_GREEN |
		 RF_SHELL_BLUE | RF_SHELL_DOUBLE |
		 RF_SHELL_HALF_DAM))
	{
		for (i = 0; i < nverts; i++, v++, ov++, lerp += 4)
		{
			float *normal = ref_avertexnormals[verts[i].lightnormalindex];

			lerp[0] = move[0] + ov->v[0] * backv[0] + v->v[0] * frontv[0] +
					  normal[0] * POWERSUIT_SCALE;
			lerp[1] = move[1] + ov->v[1] * backv[1] + v->v[1] * frontv[1] +
					  normal[1] * POWERSUIT_SCALE;
			lerp[2] = move[2] + ov->v[2] * backv[2] + v->v[2] * frontv[2] +
					  normal[2] * POWERSUIT_SCALE;
		}
	}
	else
	{
		for (i = 0; i < nverts; i++, v++, ov++, lerp += 4)
		{
			lerp[0] = move[0] + ov->v[0] * backv[0] + v->v[0] * frontv[0];
			lerp[1] = move[1] + ov->v[1] * backv[1] + v->v[1] * frontv[1];
			lerp[2] = move[2] + ov->v[2] * backv[2] + v->v[2] * frontv[2];
		}
	}
}

/* gl1_mesh.c, R_DrawAliasFrameLerp up to R_LerpVerts, verbatim */
static void
ref_lerp_setup(entity_t *currententity, daliasframe_t *frame, daliasframe_t *oldframe, float backlerp,
		vec3_t move, vec3_t frontv, vec3_t backv)
{
	int i;
	float frontlerp;
	vec3_t delta, vectors[3];

	frontlerp = 1.0 - backlerp;

	/* move should be the delta back to the previous frame * backlerp */
	VectorSubtract(currententity->oldorigin, currententity->origin, delta);
	AngleVectors(currententity->angles, vectors[0], vectors[1], vectors[2]);

	move[0] = DotProduct(delta, vectors[0]); /* forward */
	move[1] = -DotProduct(delta, vectors[1]); /* left */
	move[2] = DotProduct(delta, vectors[2]); /* up */

	VectorAdd(move, oldframe->translate, move);

	for (i = 0; i < 3; i++)
	{
		move[i] = backlerp * move[i] + frontlerp * frame->translate[i];
	}

	for (i = 0; i < 3; i++)
	{
		frontv[i] = frontlerp * frame->scale[i];
		backv[i] = backlerp * oldframe->scale[i];
	}
}

/* rm_alias_lerp_setup and rm_alias_lerp of one entity and two frames, as rm_mesh.c calls them */
static void
rm_lerp_of(entity_t *e, daliasframe_t *frame, daliasframe_t *oldframe, int n, float (*out)[4])
{
	vec3_t delta, vectors[3], move, frontv, backv;

	VectorSubtract(e->oldorigin, e->origin, delta);
	AngleVectors(e->angles, vectors[0], vectors[1], vectors[2]);
	rm_alias_lerp_setup(frame->scale, frame->translate, oldframe->scale, oldframe->translate, e->backlerp, delta,
			vectors[0], vectors[1], vectors[2], move, frontv, backv);
	rm_alias_lerp(n, (const unsigned char *)frame->verts, (const unsigned char *)oldframe->verts,
			(e->flags & RM_RF_SHELLS) != 0, move, frontv, backv, out);
}

static daliasframe_t *
frame_new(int n)
{
	return calloc(1, sizeof(daliasframe_t) + (size_t)n * sizeof(dtrivertx_t));
}

/* the lerped position of two vertices, by hand and as gl1 computes it, and random frames against gl1 */
static void
test_alias_lerp(void)
{
	static const float backlerps[3] = { 0, 0.25f, 1 };
	daliasframe_t *f = frame_new(2), *o = frame_new(2);
	float got[64][4], want[64][4];
	entity_t e;
	int i, k, n, same = 1;

	CHECK(sizeof(dmdl_t) == RM_MD2_HEADER && offsetof(daliasframe_t, verts) == RM_MD2_FRAME_HEADER);
	CHECK(sizeof(dtrivertx_t) == 4 && sizeof(rm_avert_t) == RM_AVERT_STRIDE);
	CHECK(RF_MINLIGHT == RM_RF_MINLIGHT && RF_WEAPONMODEL == RM_RF_WEAPONMODEL && RF_FULLBRIGHT == RM_RF_FULLBRIGHT);
	CHECK(RF_DEPTHHACK == RM_RF_DEPTHHACK && RF_TRANSLUCENT == RM_RF_TRANSLUCENT && RF_GLOW == RM_RF_GLOW);
	CHECK(RF_SHELL_RED == RM_RF_SHELL_RED && RF_SHELL_GREEN == RM_RF_SHELL_GREEN && RF_SHELL_BLUE == RM_RF_SHELL_BLUE);
	CHECK(RF_IR_VISIBLE == RM_RF_IR_VISIBLE && RF_SHELL_DOUBLE == RM_RF_SHELL_DOUBLE);
	CHECK(RF_SHELL_HALF_DAM == RM_RF_SHELL_HALF_DAM && RDF_IRGOGGLES == RM_RDF_IRGOGGLES);
	CHECK(POWERSUIT_SCALE == RM_POWERSUIT_SCALE && MAX_VERTS == RM_MD2_MAX_VERTS && MAX_MD2SKINS == RM_MD2_MAX_SKINS);
	CHECK(MAX_SKINNAME == RM_MD2_SKINNAME && MAX_LBM_HEIGHT == RM_MD2_MAX_SKINHEIGHT && ALIAS_VERSION == RM_MD2_VERSION);
	CHECK(IDALIASHEADER == RM_MD2_IDENT && IDSPRITEHEADER == RM_SP2_IDENT && SPRITE_VERSION == RM_SP2_VERSION);
	CHECK(offsetof(dsprite_t, frames) == RM_SP2_HEADER && sizeof(dsprframe_t) == RM_SP2_FRAME);

	/* the frame: scale 0.5, translate (10, 20, 30); the old one scale 1 at 0; vertex 0 (2, 4, 6), old (8, 8, 8) */
	memset(&e, 0, sizeof(e));

	for (k = 0; k < 3; k++)
	{
		f->scale[k] = 0.5f;
		f->translate[k] = 10.0f * (k + 1);
		o->scale[k] = 1;
		o->translate[k] = 0;
		f->verts[0].v[k] = (byte)(2 * (k + 1));
		o->verts[0].v[k] = 8;
		f->verts[1].v[k] = 255;
		o->verts[1].v[k] = 0;
	}

	f->verts[0].lightnormalindex = 5;
	f->verts[1].lightnormalindex = 161;

	for (i = 0; i < 3; i++)
	{
		vec3_t move, frontv, backv;

		e.backlerp = backlerps[i];
		rm_lerp_of(&e, f, o, 2, got);
		ref_lerp_setup(&e, f, o, e.backlerp, move, frontv, backv);
		ref_LerpVerts(0, 2, f->verts, o->verts, f->verts, want[0], move, frontv, backv);
		CHECK(memcmp(got[0], want[0], 3 * sizeof(float)) == 0 && memcmp(got[1], want[1], 3 * sizeof(float)) == 0);
	}

	/* by hand: backlerp 0 is the frame, 1 the old frame, 0.25 a quarter of the way back */
	e.backlerp = 0;
	rm_lerp_of(&e, f, o, 2, got);
	CHECK(got[0][0] == 11 && got[0][1] == 22 && got[0][2] == 33 && got[1][0] == 137.5f);
	e.backlerp = 1;
	rm_lerp_of(&e, f, o, 2, got);
	CHECK(got[0][0] == 8 && got[0][1] == 8 && got[0][2] == 8 && got[1][2] == 0);
	e.backlerp = 0.25f;
	rm_lerp_of(&e, f, o, 2, got);
	CHECK(got[0][0] == 10.25f && got[0][1] == 18.5f && got[0][2] == 26.75f);
	/* a shell: pushed out along normal 5 by 4 units */
	e.flags = RF_SHELL_GREEN;
	e.backlerp = 0;
	rm_lerp_of(&e, f, o, 2, got);
	CHECK(fabsf(got[0][0] - (11 + 4 * ref_avertexnormals[5][0])) < 1e-5f &&
		fabsf(got[0][2] - (33 + 4 * ref_avertexnormals[5][2])) < 1e-5f);
	/* the entity moved since the old frame: the delta is turned into the entity's axes */
	e.flags = 0;
	e.backlerp = 1;
	e.oldorigin[0] = 16;
	rm_lerp_of(&e, f, o, 2, got);
	CHECK(got[0][0] == 24 && got[0][1] == 8);
	free(f);
	free(o);

	/* random frames, entities and lerps against gl1, bit for bit */
	for (n = 0; n < 4000; n++)
	{
		const int nv = 1 + (int)(rng() % 64);
		vec3_t move, frontv, backv;

		f = frame_new(nv);
		o = frame_new(nv);
		memset(&e, 0, sizeof(e));

		for (k = 0; k < 3; k++)
		{
			f->scale[k] = rngf(0, 2);
			f->translate[k] = rngf(-300, 300);
			o->scale[k] = rngf(0, 2);
			o->translate[k] = rngf(-300, 300);
			e.origin[k] = rngf(-4000, 4000);
			e.oldorigin[k] = e.origin[k] + rngf(-40, 40);
			e.angles[k] = rngf(-360, 360);
		}

		for (i = 0; i < nv; i++)
		{
			for (k = 0; k < 3; k++)
			{
				f->verts[i].v[k] = (byte)rng();
				o->verts[i].v[k] = (byte)rng();
			}

			f->verts[i].lightnormalindex = (byte)(rng() % 162);
			o->verts[i].lightnormalindex = (byte)(rng() % 162);
		}

		e.backlerp = (n % 7 == 0) ? 0 : (n % 7 == 1) ? 1 : rngf(0, 1);
		e.flags = (n & 8) ? RF_SHELL_RED : (n & 16) ? RF_SHELL_HALF_DAM : 0;
		rm_lerp_of(&e, f, o, nv, got);
		ref_lerp_setup(&e, f, o, e.backlerp, move, frontv, backv);
		ref_LerpVerts(e.flags, nv, f->verts, o->verts, f->verts, want[0], move, frontv, backv);

		for (i = 0; i < nv; i++)
		{
			same = same && (memcmp(got[i], want[i], 3 * sizeof(float)) == 0);
		}

		free(f);
		free(o);
	}

	CHECK(same);
}

/* gl1_mesh.c, R_DrawAliasModel's lighting from "get lighting information" to the IR goggles, verbatim but the
   light point (R_LightPoint's colour) and the cvars passed in */
static void
ref_shadelight(entity_t *currententity, int rdflags, const float *point, float time, float overbright,
		float *shadelight)
{
	int i;

	if (currententity->flags &
		(RF_SHELL_HALF_DAM | RF_SHELL_GREEN | RF_SHELL_RED |
		 RF_SHELL_BLUE | RF_SHELL_DOUBLE))
	{
		VectorClear(shadelight);

		if (currententity->flags & RF_SHELL_HALF_DAM)
		{
			shadelight[0] = 0.56;
			shadelight[1] = 0.59;
			shadelight[2] = 0.45;
		}

		if (currententity->flags & RF_SHELL_DOUBLE)
		{
			shadelight[0] = 0.9;
			shadelight[1] = 0.7;
		}

		if (currententity->flags & RF_SHELL_RED)
		{
			shadelight[0] = 1.0;
		}

		if (currententity->flags & RF_SHELL_GREEN)
		{
			shadelight[1] = 1.0;
		}

		if (currententity->flags & RF_SHELL_BLUE)
		{
			shadelight[2] = 1.0;
		}
	}
	else if (currententity->flags & RF_FULLBRIGHT)
	{
		for (i = 0; i < 3; i++)
		{
			shadelight[i] = 1.0;
		}
	}
	else
	{
		VectorCopy(point, shadelight);
	}

	if (currententity->flags & RF_MINLIGHT)
	{
		for (i = 0; i < 3; i++)
		{
			if (shadelight[i] > 0.1)
			{
				break;
			}
		}

		if (i == 3)
		{
			shadelight[0] = 0.1;
			shadelight[1] = 0.1;
			shadelight[2] = 0.1;
		}
	}

	if (currententity->flags & RF_GLOW)
	{
		/* bonus items will pulse with time */
		float scale;
		float min;

		scale = 0.1 * sin(time * 7);

		for (i = 0; i < 3; i++)
		{
			min = shadelight[i] * 0.8;
			shadelight[i] += scale;

			if (shadelight[i] < min)
			{
				shadelight[i] = min;
			}
		}
	}

	if (overbright)
	{
		for (i = 0; i < 3; ++i)
		{
			shadelight[i] *= overbright;
		}
	}

	/* ir goggles color override */
	if (rdflags & RDF_IRGOGGLES && currententity->flags &
		RF_IR_VISIBLE)
	{
		shadelight[0] = 1.0;
		shadelight[1] = 0.0;
		shadelight[2] = 0.0;
	}
}

/* gl1_mesh.c, one vertex's colour in R_DrawAliasFrameLerp with GLBUFFER_COLOR (gammatable passed in), verbatim */
static void
ref_vertex_color(float l, const float *shadelight, int shell, int minlight_set, const byte *minlight,
		const byte *gammatable, float alpha, byte *clr)
{
	int idx[3], i;

	for (i = 0; i < 3; i++)
	{
		if (shell)
		{
			idx[i] = shadelight[i] * 255;
		}
		else
		{
			idx[i] = l * shadelight[i] * 255;
		}

		idx[i] = Q_clamp(idx[i], 0, 255);
	}

	if (minlight_set)
	{
		for (i = 0; i < 3; i++)
		{
			idx[i] = minlight[idx[i]];
		}
	}

	clr[0] = gammatable[idx[0]];
	clr[1] = gammatable[idx[1]];
	clr[2] = gammatable[idx[2]];
	clr[3] = alpha * 255;
}

/* gl1_main.c, R_SetLightLevel's choice, verbatim */
static float
ref_light_level(const float *shadelight)
{
	float v;

	if (shadelight[0] > shadelight[1])
	{
		if (shadelight[0] > shadelight[2])
		{
			v = 150 * shadelight[0];
		}
		else
		{
			v = 150 * shadelight[2];
		}
	}
	else
	{
		if (shadelight[1] > shadelight[2])
		{
			v = 150 * shadelight[1];
		}
		else
		{
			v = 150 * shadelight[2];
		}
	}

	return v;
}

/* the shade dots, the vertex colour of one normal and one light, the shells, the alpha and the light level */
static void
test_alias_shade(void)
{
	static const int flagsets[] = { 0, RF_MINLIGHT, RF_GLOW, RF_FULLBRIGHT, RF_SHELL_RED, RF_SHELL_GREEN | RF_SHELL_BLUE,
		RF_SHELL_DOUBLE, RF_SHELL_HALF_DAM, RF_SHELL_HALF_DAM | RF_SHELL_DOUBLE | RF_SHELL_RED, RF_MINLIGHT | RF_GLOW,
		RF_IR_VISIBLE, RF_IR_VISIBLE | RF_SHELL_BLUE, RF_FULLBRIGHT | RF_MINLIGHT | RF_GLOW, RF_TRANSLUCENT };
	byte gamma[256], minl[256], clr[4];
	float sl[3], want[3], rgba[4], point[3];
	int i, k, n, same = 1, row;

	for (i = 0; i < 256; i++)
	{
		gamma[i] = (byte)(255 - i / 2);
		minl[i] = (byte)(i < 40 ? 40 : i);
	}

	/* the dots: the table itself, by row and normal; the row of a yaw as gl1 takes it */
	CHECK(rm_shadedot(0, 0) == ref_avertexnormal_dots[0][0] && rm_shadedot(15, 255) == ref_avertexnormal_dots[15][255]);
	CHECK(rm_shadedot(7, 100) == ref_avertexnormal_dots[7][100] && rm_shadedot(16, 256) == ref_avertexnormal_dots[0][0]);
	CHECK(rm_shadedot_row(0) == 0 && rm_shadedot_row(22.5f) == 1 && rm_shadedot_row(22.4f) == 0);
	CHECK(rm_shadedot_row(359.9f) == 15 && rm_shadedot_row(360) == 0 && rm_shadedot_row(-22.5f) == 15);
	CHECK(rm_shadedot_row(-22.4f) == 0 && rm_shadedot_row(-45) == 14 && rm_shadedot_row(1e20f) == 15);
	CHECK(rm_shadedot_row(-1e20f) == 0 && rm_shadedot_row(NAN) == 0);

	for (n = 0; n < 100000; n++)
	{
		const float yaw = (n < 2881) ? (float)(n - 1440) * 0.5f : rngf(-1e6f, 1e6f);

		same = same && (rm_shadedot_row(yaw) == (((int)(yaw * (SHADEDOT_QUANT_REF / 360.0))) & (SHADEDOT_QUANT_REF - 1)));
	}

	CHECK(same);

	/* one normal and one light: l * light * 255, clamped, through minlight and gamma, then normalised */
	sl[0] = 0.5f;
	sl[1] = 1.0f;
	sl[2] = 2.0f;
	row = 3;
	rm_alias_color(rm_shadedot(row, 42), sl, 0, NULL, gamma, 255, rgba);
	ref_vertex_color(ref_avertexnormal_dots[row][42], sl, 0, 0, minl, gamma, 1.0f, clr);

	for (k = 0; k < 4; k++)
	{
		CHECK(rgba[k] == clr[k] / 255.0f);
	}

	/* by hand: dot 1.0 of row 0, normal 13 (a sample), the light 0.5 gives index 127, gamma 255 - 63 */
	for (i = 0; i < 256 && ref_avertexnormal_dots[0][i] != 1.0f; i++)
	{
	}

	CHECK(i < 256);
	rm_alias_color(rm_shadedot(0, i), sl, 0, NULL, gamma, 128, rgba);
	CHECK(rgba[0] == 192 / 255.0f && rgba[1] == 128 / 255.0f && rgba[2] == 128 / 255.0f && rgba[3] == 128 / 255.0f);
	/* with minlight, index 127 stays and 0 is lifted */
	sl[0] = 0;
	rm_alias_color(1.0f, sl, 0, minl, gamma, 255, rgba);
	CHECK(rgba[0] == (255 - 20) / 255.0f && rgba[1] == 128 / 255.0f);
	/* the shell: shadelight alone, no dot */
	sl[0] = 1.0f;
	sl[1] = 0.25f;
	sl[2] = 0;
	rm_alias_color(0.0f, sl, 1, NULL, gamma, 76, rgba);
	CHECK(rgba[0] == 128 / 255.0f && rgba[1] == (255 - 31) / 255.0f && rgba[2] == 1.0f && rgba[3] == 76 / 255.0f);
	/* a light gl1 cannot convert (a NaN, past an int) saturates: no undefined conversion */
	sl[0] = NAN;
	sl[1] = 1e30f;
	sl[2] = -1e30f;
	rm_alias_color(1.0f, sl, 0, NULL, gamma, 255, rgba);
	CHECK(rgba[0] == 1.0f && rgba[1] == 128 / 255.0f && rgba[2] == 1.0f);

	/* random lights and dots against gl1 */
	for (n = 0; n < 200000; n++)
	{
		const int shell = n & 1, mset = (n >> 1) & 1, nrm = (int)(rng() % 256);
		float a = rngf(0, 1);

		row = (int)(rng() % 16);

		for (k = 0; k < 3; k++)
		{
			sl[k] = (n % 11 == 0) ? (float)(rng() % 300) / 255.0f : rngf(-0.5f, 4);
		}

		rm_alias_color(rm_shadedot(row, nrm), sl, shell, mset ? minl : NULL, gamma, rm_alias_alpha(RF_TRANSLUCENT, a),
				rgba);
		ref_vertex_color(ref_avertexnormal_dots[row][nrm], sl, shell, mset, minl, gamma, a, clr);

		for (k = 0; k < 4; k++)
		{
			same = same && (rgba[k] == clr[k] / 255.0f);
		}
	}

	CHECK(same);

	/* the alpha byte */
	CHECK(rm_alias_alpha(0, 0.3f) == 255 && rm_alias_alpha(RF_TRANSLUCENT, 0.3f) == 76);
	CHECK(rm_alias_alpha(RF_TRANSLUCENT, 1) == 255 && rm_alias_alpha(RF_TRANSLUCENT, 0) == 0);
	CHECK(rm_alias_alpha(RF_TRANSLUCENT, 2) == 255 && rm_alias_alpha(RF_TRANSLUCENT, -1) == 0);
	CHECK(rm_alias_alpha(RF_TRANSLUCENT, NAN) == 0 && rm_alias_alpha(RF_TRANSLUCENT, -0.003f) == 0);
	CHECK(rm_alias_alpha(RF_TRANSLUCENT, 255.9f / 255) == 255 && rm_alias_alpha(RF_TRANSLUCENT, 254.9f / 255) == 254);

	/* the light of every flag set, against gl1 */
	CHECK(!rm_alias_lit(RF_SHELL_DOUBLE) && !rm_alias_lit(RF_FULLBRIGHT) && rm_alias_lit(RF_MINLIGHT | RF_GLOW));
	CHECK(!rm_alias_lit(RF_SHELL_HALF_DAM) && !rm_alias_lit(RF_SHELL_BLUE) && rm_alias_lit(RF_TRANSLUCENT));

	for (n = 0; n < 60000; n++)
	{
		entity_t e;
		const int rdflags = (n & 1) ? RDF_IRGOGGLES : 0;
		const float t = rngf(0, 2000), ob = (float)((n >> 1) % 5 == 3 ? 4 : (n >> 1) % 5);

		memset(&e, 0, sizeof(e));
		e.flags = flagsets[n % (sizeof(flagsets) / sizeof(flagsets[0]))];

		for (k = 0; k < 3; k++)
		{
			point[k] = (n % 3 == 0) ? 0.05f * (float)(rng() % 4) : rngf(0, 3);
		}

		rm_alias_shade(e.flags, rdflags, point, t, ob, sl);
		ref_shadelight(&e, rdflags, point, t, ob, want);
		same = same && (memcmp(sl, want, sizeof(sl)) == 0);
	}

	CHECK(same);

	/* by hand: minlight lifts a dark model to 0.1, the goggles paint it red */
	point[0] = point[1] = point[2] = 0.05f;
	rm_alias_shade(RF_MINLIGHT, 0, point, 0, 0, sl);
	CHECK(sl[0] == 0.1f && sl[1] == 0.1f && sl[2] == 0.1f);
	rm_alias_shade(RF_MINLIGHT | RF_IR_VISIBLE, RDF_IRGOGGLES, point, 0, 0, sl);
	CHECK(sl[0] == 1 && sl[1] == 0 && sl[2] == 0);
	rm_alias_shade(RF_SHELL_DOUBLE | RF_SHELL_BLUE, 0, point, 0, 2, sl);
	CHECK(sl[0] == 1.8f && sl[1] == 1.4f && sl[2] == 2);

	/* the light level */
	for (n = 0; n < 10000; n++)
	{
		for (k = 0; k < 3; k++)
		{
			sl[k] = (n % 5 == 0) ? (float)(rng() % 3) : rngf(0, 2);
		}

		same = same && (rm_light_level(sl) == ref_light_level(sl));
	}

	CHECK(same);
	sl[0] = sl[1] = sl[2] = 1;
	CHECK(rm_light_level(sl) == 150);
}

/* gl1_buffer.c, R_SetBufferIndices's two loops, verbatim (vtx_ptr and idx_ptr passed in; the count cast for -Wextra) */
static void
ref_SetBufferIndices(int fan, unsigned vertices_num, unsigned short *vtx_ptr, unsigned short *idx_ptr,
		unsigned short *idx)
{
	int i;

	if (fan)
	{
		for (i = 0; i < (int)vertices_num-2; i++)
		{
			idx[*idx_ptr]   = *vtx_ptr;
			idx[*idx_ptr+1] = *vtx_ptr+i+1;
			idx[*idx_ptr+2] = *vtx_ptr+i+2;
			*idx_ptr += 3;
		}
	}
	else
	{
		for (i = 0; i < (int)vertices_num-2; i++)
		{
			if (i % 2 == 0)
			{
				idx[*idx_ptr]   = *vtx_ptr+i;
				idx[*idx_ptr+1] = *vtx_ptr+i+1;
				idx[*idx_ptr+2] = *vtx_ptr+i+2;
			}
			else	// backwards order
			{
				idx[*idx_ptr]   = *vtx_ptr+i+2;
				idx[*idx_ptr+1] = *vtx_ptr+i+1;
				idx[*idx_ptr+2] = *vtx_ptr+i;
			}
			*idx_ptr += 3;
		}
	}

	*vtx_ptr += vertices_num;
}

/*
 * gl1_mesh.c, R_DrawAliasFrameLerp's walk of the command stream, verbatim
 * but GLBUFFER_* writing to the arrays, R_SetBufferIndices above, and the
 * colour through ref_vertex_color (GLBUFFER_COLOR); the number of vertices.
 */
static int
ref_emit(int flags, int *order, float (*s_lerped)[4], dtrivertx_t *verts, float *shadedots, const float *shadelight,
		int minlight_set, const byte *minlight, const byte *gammatable, float alpha, float *vtx, float *tx, byte *cl,
		unsigned short *idx, int *nidx)
{
	unsigned short vtx_ptr = 0, idx_ptr = 0;
	int count, index_xyz, vt = 0;
	float tex[2], l;

	while (1)
	{
		/* get the vertex count and primitive type */
		count = *order++;

		if (!count)
		{
			break; /* done */
		}

		if (count < 0)
		{
			count = -count;
			ref_SetBufferIndices(1, count, &vtx_ptr, &idx_ptr, idx);
		}
		else
		{
			ref_SetBufferIndices(0, count, &vtx_ptr, &idx_ptr, idx);
		}

		if (flags &
			(RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE))
		{
			do
			{
				index_xyz = order[2];
				order += 3;

				vtx[3 * vt] = s_lerped[index_xyz][0];
				vtx[3 * vt + 1] = s_lerped[index_xyz][1];
				vtx[3 * vt + 2] = s_lerped[index_xyz][2];
				tx[2 * vt] = tx[2 * vt + 1] = 0;
				ref_vertex_color(0, shadelight, 1, minlight_set, minlight, gammatable, alpha, cl + 4 * vt);
				vt++;
			}
			while (--count);
		}
		else
		{
			do
			{
				/* texture coordinates come from the draw list */
				tex[0] = ((float *)order)[0];
				tex[1] = ((float *)order)[1];

				index_xyz = order[2];
				order += 3;

				/* normals and vertexes come from the frame list */
				l = shadedots[verts[index_xyz].lightnormalindex];

				vtx[3 * vt] = s_lerped[index_xyz][0];
				vtx[3 * vt + 1] = s_lerped[index_xyz][1];
				vtx[3 * vt + 2] = s_lerped[index_xyz][2];
				tx[2 * vt] = tex[0];
				tx[2 * vt + 1] = tex[1];
				ref_vertex_color(l, shadelight, 0, minlight_set, minlight, gammatable, alpha, cl + 4 * vt);
				vt++;
			}
			while (--count);
		}
	}

	*nidx = idx_ptr;
	return vt;
}

/* a float's bits in an int of the command stream */
static int
fbits(float f)
{
	int i;

	memcpy(&i, &f, sizeof(i));
	return i;
}

/* the command stream walked into vertices and triangles as gl1 walks it, and every refusal */
static void
test_alias_emit(void)
{
	enum { NXYZ = 6, VCAP = 600, ICAP = 1800 };
	static float lerped[NXYZ][4];
	static rm_avert_t out[VCAP];
	static unsigned short idx[ICAP], ridx[ICAP];
	static float vtx[3 * VCAP], tx[2 * VCAP];
	static byte cl[4 * VCAP];
	dtrivertx_t verts[NXYZ];
	byte gamma[256];
	const float sl[3] = { 0.75f, 0.5f, 1.25f };
	/* a strip of 4 (two triangles, the second backwards) and a fan of 5, then the end */
	int order[1 + 12 + 1 + 15 + 1];
	int bad[sizeof(order) / sizeof(order[0])];
	long nv, ni;
	int i, k, n, rni, rnv, same = 1;

	for (i = 0; i < 256; i++)
	{
		gamma[i] = (byte)i;
	}

	for (i = 0; i < NXYZ; i++)
	{
		for (k = 0; k < 3; k++)
		{
			lerped[i][k] = (float)(10 * i + k);
		}

		verts[i].lightnormalindex = (byte)(17 * i);
	}

	n = 0;
	order[n++] = 4;

	for (i = 0; i < 4; i++)
	{
		order[n++] = fbits(0.25f * i);
		order[n++] = fbits(1 - 0.25f * i);
		order[n++] = i;
	}

	order[n++] = -5;

	for (i = 0; i < 5; i++)
	{
		order[n++] = fbits(0.5f);
		order[n++] = fbits(0.125f * i);
		order[n++] = NXYZ - 1 - i;
	}

	order[n++] = 0;

	nv = rm_alias_emit(order, n, (const float (*)[4])lerped, (const unsigned char *)verts, NXYZ, 2, sl, 0, NULL,
			gamma, 255, out, VCAP, idx, ICAP, &ni);
	CHECK(nv == 9 && ni == 15);
	/* the strip's triangles 0 1 2, 3 2 1; the fan's 4 5 6, 4 6 7, 4 7 8 */
	CHECK(idx[0] == 0 && idx[1] == 1 && idx[2] == 2 && idx[3] == 3 && idx[4] == 2 && idx[5] == 1);
	CHECK(idx[6] == 4 && idx[7] == 5 && idx[8] == 6 && idx[12] == 4 && idx[13] == 7 && idx[14] == 8);
	CHECK(out[1].pos[0] == 10 && out[1].pos[2] == 12 && out[1].pos[3] == 1 && out[1].uv[0] == 0.25f && out[1].uv[1] == 0.75f);
	CHECK(out[4].pos[0] == 50 && out[8].pos[1] == 11 && out[8].uv[1] == 0.5f);

	rnv = ref_emit(0, order, lerped, verts, ref_avertexnormal_dots[2], sl, 0, NULL, gamma, 1.0f, vtx, tx, cl,
			ridx, &rni);
	CHECK(rnv == nv && rni == ni && memcmp(idx, ridx, (size_t)ni * sizeof(idx[0])) == 0);

	for (i = 0; i < rnv; i++)
	{
		same = same && out[i].pos[0] == vtx[3 * i] && out[i].pos[1] == vtx[3 * i + 1] && out[i].pos[2] == vtx[3 * i + 2];
		same = same && out[i].uv[0] == tx[2 * i] && out[i].uv[1] == tx[2 * i + 1];

		for (k = 0; k < 4; k++)
		{
			same = same && out[i].color[k] == cl[4 * i + k] / 255.0f;
		}
	}

	CHECK(same);

	/* the shell: no texture coordinate, shadelight's colour on every vertex */
	nv = rm_alias_emit(order, n, (const float (*)[4])lerped, (const unsigned char *)verts, NXYZ, 2, sl, 1, NULL,
			gamma, 76, out, VCAP, idx, ICAP, &ni);
	rnv = ref_emit(RF_SHELL_RED, order, lerped, verts, ref_avertexnormal_dots[2], sl, 0, NULL, gamma, 0.3f, vtx, tx, cl,
			ridx, &rni);
	CHECK(nv == rnv && ni == rni && out[3].uv[0] == 0 && out[3].color[0] == 191 / 255.0f && out[3].color[2] == 1);
	CHECK(out[3].color[3] == cl[15] / 255.0f && out[5].pos[1] == vtx[16]);

	/* random streams against gl1 */
	for (k = 0; k < 2000; k++)
	{
		static int ro[3000];
		const int shell = k & 1;
		int strips = 1 + (int)(rng() % 20), s;

		int m = 0;

		for (s = 0; s < strips; s++)
		{
			int c = 3 + (int)(rng() % 20);

			ro[m++] = (rng() & 1) ? c : -c;

			for (i = 0; i < c; i++)
			{
				ro[m++] = fbits(rngf(-2, 2));
				ro[m++] = fbits(rngf(-2, 2));
				ro[m++] = (int)(rng() % NXYZ);
			}
		}

		ro[m++] = 0;

		for (i = 0; i < NXYZ; i++)
		{
			verts[i].lightnormalindex = (byte)rng();
		}

		nv = rm_alias_emit(ro, m, (const float (*)[4])lerped, (const unsigned char *)verts, NXYZ, k % 16, sl, shell,
				NULL, gamma, 255, out, VCAP, idx, ICAP, &ni);
		rnv = ref_emit(shell ? RF_SHELL_BLUE : 0, ro, lerped, verts, ref_avertexnormal_dots[k % 16], sl, 0, NULL, gamma, 1,
				vtx, tx, cl, ridx, &rni);
		same = same && (nv == rnv) && (ni == rni) && (memcmp(idx, ridx, (size_t)ni * sizeof(idx[0])) == 0);

		for (i = 0; i < rnv && same; i++)
		{
			same = out[i].pos[0] == vtx[3 * i] && out[i].pos[2] == vtx[3 * i + 2] && out[i].color[1] == cl[4 * i + 1] / 255.0f;
			same = same && (shell || (out[i].uv[0] == tx[2 * i] && out[i].uv[1] == tx[2 * i + 1]));
		}
	}

	CHECK(same);

	/* refused: a count of 2, -2, 1, 0 vertices left, a vertex past the stream, an index past num_xyz or negative,
	   no terminating 0, the vertex and the index capacity */
	memcpy(bad, order, sizeof(order));
	bad[0] = 2;
	CHECK(rm_alias_emit(bad, n, (const float (*)[4])lerped, (const unsigned char *)verts, NXYZ, 0, sl, 0, NULL, gamma, 255,
			out, VCAP, idx, ICAP, &ni) == -1 && ni == 0);
	bad[0] = -2;
	CHECK(rm_alias_emit(bad, n, (const float (*)[4])lerped, (const unsigned char *)verts, NXYZ, 0, sl, 0, NULL, gamma, 255,
			out, VCAP, idx, ICAP, &ni) == -1);
	bad[0] = INT_MIN;
	CHECK(rm_alias_emit(bad, n, (const float (*)[4])lerped, (const unsigned char *)verts, NXYZ, 0, sl, 0, NULL, gamma, 255,
			out, VCAP, idx, ICAP, &ni) == -1);
	bad[0] = 10;
	CHECK(rm_alias_emit(bad, n, (const float (*)[4])lerped, (const unsigned char *)verts, NXYZ, 0, sl, 0, NULL, gamma, 255,
			out, VCAP, idx, ICAP, &ni) == -1);
	memcpy(bad, order, sizeof(order));
	bad[3] = NXYZ;
	CHECK(rm_alias_emit(bad, n, (const float (*)[4])lerped, (const unsigned char *)verts, NXYZ, 0, sl, 0, NULL, gamma, 255,
			out, VCAP, idx, ICAP, &ni) == -1);
	bad[3] = -1;
	CHECK(rm_alias_emit(bad, n, (const float (*)[4])lerped, (const unsigned char *)verts, NXYZ, 0, sl, 1, NULL, gamma, 255,
			out, VCAP, idx, ICAP, &ni) == -1);
	CHECK(rm_alias_emit(order, n - 1, (const float (*)[4])lerped, (const unsigned char *)verts, NXYZ, 0, sl, 0, NULL, gamma,
			255, out, VCAP, idx, ICAP, &ni) == -1);
	CHECK(rm_alias_emit(order, n - 2, (const float (*)[4])lerped, (const unsigned char *)verts, NXYZ, 0, sl, 0, NULL, gamma,
			255, out, VCAP, idx, ICAP, &ni) == -1);
	CHECK(rm_alias_emit(order, n, (const float (*)[4])lerped, (const unsigned char *)verts, NXYZ, 0, sl, 0, NULL, gamma, 255,
			out, 8, idx, ICAP, &ni) == -1);
	CHECK(rm_alias_emit(order, n, (const float (*)[4])lerped, (const unsigned char *)verts, NXYZ, 0, sl, 0, NULL, gamma, 255,
			out, 9, idx, 14, &ni) == -1);
	CHECK(rm_alias_emit(order, n, (const float (*)[4])lerped, (const unsigned char *)verts, NXYZ, 0, sl, 0, NULL, gamma, 255,
			out, 9, idx, 15, &ni) == 9 && ni == 15);
	/* a strip of 2 after the strip of 4, then the end: refused (gl1 would add two vertices and no triangle) */
	memcpy(bad, order, sizeof(order));
	bad[13] = 2;
	bad[20] = 0;
	CHECK(rm_alias_emit(bad, 21, (const float (*)[4])lerped, (const unsigned char *)verts, NXYZ, 0, sl, 0, NULL, gamma, 255,
			out, VCAP, idx, ICAP, &ni) == -1);
	/* the strip of 4 with its last vertex cut: from a copy of exactly the ints given, nothing past them is read */
	{
		int *cut = malloc(12 * sizeof(int));

		memcpy(cut, order, 12 * sizeof(int));
		CHECK(rm_alias_emit(cut, 12, (const float (*)[4])lerped, (const unsigned char *)verts, NXYZ, 0, sl, 0, NULL, gamma,
				255, out, VCAP, idx, ICAP, &ni) == -1);
		free(cut);
	}
}

/* gl1_mesh.c, R_CullAliasModel from the frame pointers on, verbatim (frustum passed in) */
static qboolean
ref_cull(daliasframe_t *pframe, daliasframe_t *poldframe, entity_t *e, cplane_t *frustum)
{
	int i;
	vec3_t mins, maxs, bbox[8];
	vec3_t vectors[3];
	vec3_t thismins, oldmins, thismaxs, oldmaxs;
	vec3_t angles;

	/* compute axially aligned mins and maxs */
	if (pframe == poldframe)
	{
		for (i = 0; i < 3; i++)
		{
			mins[i] = pframe->translate[i];
			maxs[i] = mins[i] + pframe->scale[i] * 255;
		}
	}
	else
	{
		for (i = 0; i < 3; i++)
		{
			thismins[i] = pframe->translate[i];
			thismaxs[i] = thismins[i] + pframe->scale[i] * 255;

			oldmins[i] = poldframe->translate[i];
			oldmaxs[i] = oldmins[i] + poldframe->scale[i] * 255;

			if (thismins[i] < oldmins[i])
			{
				mins[i] = thismins[i];
			}
			else
			{
				mins[i] = oldmins[i];
			}

			if (thismaxs[i] > oldmaxs[i])
			{
				maxs[i] = thismaxs[i];
			}
			else
			{
				maxs[i] = oldmaxs[i];
			}
		}
	}

	/* compute a full bounding box */
	for (i = 0; i < 8; i++)
	{
		vec3_t tmp;

		if (i & 1)
		{
			tmp[0] = mins[0];
		}
		else
		{
			tmp[0] = maxs[0];
		}

		if (i & 2)
		{
			tmp[1] = mins[1];
		}
		else
		{
			tmp[1] = maxs[1];
		}

		if (i & 4)
		{
			tmp[2] = mins[2];
		}
		else
		{
			tmp[2] = maxs[2];
		}

		VectorCopy(tmp, bbox[i]);
	}

	/* rotate the bounding box */
	VectorCopy(e->angles, angles);
	angles[YAW] = -angles[YAW];
	AngleVectors(angles, vectors[0], vectors[1], vectors[2]);

	for (i = 0; i < 8; i++)
	{
		vec3_t tmp;

		VectorCopy(bbox[i], tmp);

		bbox[i][0] = DotProduct(vectors[0], tmp);
		bbox[i][1] = -DotProduct(vectors[1], tmp);
		bbox[i][2] = DotProduct(vectors[2], tmp);

		VectorAdd(e->origin, bbox[i], bbox[i]);
	}

	int p, f, aggregatemask = ~0;

	for (p = 0; p < 8; p++)
	{
		int mask = 0;

		for (f = 0; f < 4; f++)
		{
			float dp = DotProduct(frustum[f].normal, bbox[p]);

			if ((dp - frustum[f].dist) < 0)
			{
				mask |= (1 << f);
			}
		}

		aggregatemask &= mask;
	}

	if (aggregatemask)
	{
		return true;
	}

	return false;
}

/* rm_alias_culled as rm_mesh.c calls it */
static int
rm_cull_of(daliasframe_t *pframe, daliasframe_t *poldframe, entity_t *e, cplane_t *frustum)
{
	vec3_t angles, vectors[3];
	float normal[4][3], dist[4];
	int f;

	VectorCopy(e->angles, angles);
	angles[YAW] = -angles[YAW];
	AngleVectors(angles, vectors[0], vectors[1], vectors[2]);

	for (f = 0; f < 4; f++)
	{
		VectorCopy(frustum[f].normal, normal[f]);
		dist[f] = frustum[f].dist;
	}

	return rm_alias_culled(pframe->scale, pframe->translate, poldframe->scale, poldframe->translate, pframe == poldframe,
			e->origin, vectors[0], vectors[1], vectors[2], (const float (*)[3])normal, dist);
}

/* gl1_mesh.c's frame bounds: R_CullAliasModel's, then R_DrawAliasModel's (the weapon skips the first) */
static void
ref_frames(int *frame, int *oldframe, int num_frames, int weapon)
{
	if (!weapon)
	{
		if ((*frame >= num_frames) || (*frame < 0))
		{
			*frame = 0;
		}

		if ((*oldframe >= num_frames) || (*oldframe < 0))
		{
			*oldframe = 0;
		}
	}

	if ((*frame >= num_frames) ||
		(*frame < 0))
	{
		*frame = 0;
		*oldframe = 0;
	}

	if ((*oldframe >= num_frames) ||
		(*oldframe < 0))
	{
		*frame = 0;
		*oldframe = 0;
	}
}

/* culling, frames, skins, the entity's floats */
static void
test_alias_cull(void)
{
	static const int fr[] = { -2147483647 - 1, -5, -1, 0, 1, 2, 3, 4, 5, 2147483647 };
	daliasframe_t *f = frame_new(1), *o = frame_new(1);
	cplane_t frustum[4];
	vec3_t vup_, vpn_, vright_, org;
	entity_t e;
	const void *skins[MAX_MD2SKINS];
	int i, j, k, n, same = 1, culled = 0;
	float nan3[3] = { 0, NAN, 0 }, inf3[3] = { INFINITY, 0, 0 }, z3[3] = { 0, 0, 0 };

	for (n = 0; n < 40000; n++)
	{
		vec3_t ang;

		for (k = 0; k < 3; k++)
		{
			org[k] = rngf(-500, 500);
			ang[k] = rngf(-180, 180);
		}

		AngleVectors(ang, vpn_, vright_, vup_);
		R_SetFrustum(vup_, vpn_, vright_, org, rngf(60, 120), rngf(50, 100), frustum);
		memset(&e, 0, sizeof(e));

		for (k = 0; k < 3; k++)
		{
			f->scale[k] = rngf(0, 1);
			f->translate[k] = rngf(-150, 0);
			o->scale[k] = rngf(0, 1);
			o->translate[k] = rngf(-150, 0);
			e.origin[k] = org[k] + rngf(-1500, 1500);
			e.angles[k] = rngf(-360, 360);
		}

		k = rm_cull_of(f, (n & 1) ? f : o, &e, frustum);
		culled += k;
		same = same && (k == ref_cull(f, (n & 1) ? f : o, &e, frustum));
	}

	CHECK(same && culled > 1000 && culled < 39000);
	free(f);
	free(o);

	/* the frame bounds, every pair of the table against gl1 (cull pass, then draw pass; the weapon draw pass alone) */
	for (i = 0; i < (int)(sizeof(fr) / sizeof(fr[0])); i++)
	{
		for (j = 0; j < (int)(sizeof(fr) / sizeof(fr[0])); j++)
		{
			for (k = 0; k < 2; k++)
			{
				int a = fr[i], b = fr[j], ra = fr[i], rb = fr[j];

				if (!k)
				{
					rm_alias_frames(&a, &b, 4, 1);
				}

				rm_alias_frames(&a, &b, 4, 0);
				ref_frames(&ra, &rb, 4, k);
				same = same && (a == ra) && (b == rb) && (a >= 0) && (a < 4) && (b >= 0) && (b < 4);
			}
		}
	}

	CHECK(same);
	i = 5;
	j = 2;
	rm_alias_frames(&i, &j, 4, 1);
	CHECK(i == 0 && j == 2);
	i = 5;
	j = 2;
	rm_alias_frames(&i, &j, 4, 0);
	CHECK(i == 0 && j == 0);

	/* the skins: a set one, an unset one (0), past the array, negative */
	memset(skins, 0, sizeof(skins));
	skins[0] = skins;
	skins[3] = skins;
	CHECK(rm_alias_skin(3, skins) == 3 && rm_alias_skin(2, skins) == 0 && rm_alias_skin(31, skins) == 0);
	CHECK(rm_alias_skin(32, skins) == 0 && rm_alias_skin(INT_MAX, skins) == 0 && rm_alias_skin(-1, skins) == 0);
	CHECK(rm_alias_skin(INT_MIN, skins) == 0 && rm_alias_skin(0, skins) == 0);
	skins[31] = skins;
	CHECK(rm_alias_skin(31, skins) == 31);

	/* the entity's floats */
	CHECK(rm_entity_floats_ok(z3, z3, z3, 0.5f, 1));
	CHECK(!rm_entity_floats_ok(nan3, z3, z3, 0, 1) && !rm_entity_floats_ok(z3, inf3, z3, 0, 1));
	CHECK(!rm_entity_floats_ok(z3, z3, nan3, 0, 1) && !rm_entity_floats_ok(z3, z3, z3, NAN, 1));
	CHECK(!rm_entity_floats_ok(z3, z3, z3, 0, -INFINITY) && !rm_entity_floats_ok(inf3, z3, z3, 0, 1));
	CHECK(rm_entity_floats_ok(z3, z3, z3, -1e30f, 1e30f));

	/* the entity count the loops take */
	CHECK(RM_MAX_ENTITIES == MAX_ENTITIES && rm_entity_count(0) == 0 && rm_entity_count(-1) == 0);
	CHECK(rm_entity_count(LONG_MIN) == 0 && rm_entity_count(5) == 5 && rm_entity_count(MAX_ENTITIES) == MAX_ENTITIES);
	CHECK(rm_entity_count(MAX_ENTITIES + 1) == MAX_ENTITIES && rm_entity_count(LONG_MAX) == MAX_ENTITIES);
}

/* the weapon's projection and depth range, the entity matrix, the cull mode and the brush entity's arithmetic */
static void
test_weapon(void)
{
	float m[16], want[16], s[16], fr[16], w[16], got[16], c[4];
	const float world_angles[3] = { 10, 20, 5 }, world_org[3] = { 30, -40, 50 };
	double zn, zf;
	int i, k, n, same = 1;

	/* right-handed: the projection of gunfov as R_SetPerspective makes it; gunfov below 0 takes fov_y */
	CHECK(rm_weapon_projection(m, 73.74f, 80, 0, 640, 480, 4, 0, 0, 1, 1));
	CHECK(rm_projection(want, 80, 640, 480, 4, 0, 0, 1, 1) && memcmp(m, want, sizeof(m)) == 0);
	CHECK(rm_weapon_projection(m, 73.74f, -1, 0, 640, 480, 4, 1, 1, 2.5f, 1));
	CHECK(rm_projection(want, 73.74f, 640, 480, 4, 1, 1, 2.5f, 1) && memcmp(m, want, sizeof(m)) == 0);

	/* left-handed: P = S F as the shim multiplies them (glLoadIdentity, glScalef(-1, 1, 1), glFrustum) */
	CHECK(rm_weapon_projection(m, 73.74f, 80, 1, 640, 480, 4, 0, 1, 3.25f, 0.5f));
	CHECK(rm_projection(fr, 80, 640, 480, 4, 0, 1, 3.25f, 0.5f));
	mtl_mat_identity(want);
	mtl_mat_scale(s, -1, 1, 1);
	mtl_mat_mul(want, want, s);
	mtl_mat_mul(want, want, fr);
	CHECK(memcmp(m, want, sizeof(m)) == 0 && m[0] == -fr[0] && m[5] == fr[5] && m[14] == fr[14]);
	/* hand 2 is not mirrored (gl1 tests hand == 1; R_DrawAliasModel does not draw the weapon at 2) */
	CHECK(rm_weapon_projection(m, 73.74f, 80, 2, 640, 480, 4, 0, 1, 3.25f, 0.5f) && memcmp(m, fr, sizeof(m)) == 0);
	/* a frustum glFrustum refuses leaves the mirror or the identity, as the shim's matrix stays */
	CHECK(!rm_weapon_projection(m, 73.74f, 80, 1, 640, 0, 4, 0, 0, 0, 0) && m[0] == -1 && m[5] == 1 && m[15] == 1);
	CHECK(!rm_weapon_projection(m, 0, 0, 0, 640, 480, 4, 0, 0, 0, 0) && m[0] == 1 && m[1] == 0 && m[15] == 1);
	CHECK(!rm_weapon_projection(m, 73.74f, 0, 0, 640, 480, 4, 0, 0, 0, 0) && m[0] == 1 && m[5] == 1);

	/* the depth range: gl1's 0 .. 0.3 with RF_DEPTHHACK; the window depth of a point there as GL computes it */
	rm_alias_depth(0, &zn, &zf);
	CHECK(zn == 0 && zf == 1);
	rm_alias_depth(RF_DEPTHHACK | RF_WEAPONMODEL, &zn, &zf);
	CHECK(zn == 0 && zf == 0.0 + 0.3 * (1.0 - 0.0) && zf == 0.3);
	rm_alias_depth(RF_WEAPONMODEL, &zn, &zf);
	CHECK(zf == 1);
	/* a point 10 units ahead under the weapon's projection: GL's window depth zn + (zf - zn) * (ndc + 1) / 2 */
	CHECK(rm_weapon_projection(m, 73.74f, 80, 1, 640, 480, 4, 0, 0, 0, 0));
	rm_alias_depth(RF_DEPTHHACK, &zn, &zf);
	c[0] = m[8] * -10 + m[12];
	c[1] = m[9] * -10 + m[13];
	c[2] = m[10] * -10 + m[14];
	c[3] = m[11] * -10 + m[15];
	CHECK(c[3] == 10 && fabs(zn + (zf - zn) * mtl_gl_clip_z(c[2], c[3]) / c[3] -
		(zn + (zf - zn) * (c[2] / c[3] + 1) / 2)) < 1e-7);
	/* and its near plane lands on 0, its far plane on 0.3 */
	CHECK(mtl_gl_clip_z(-c[3], c[3]) == 0 && zn + (zf - zn) * mtl_gl_clip_z(c[3], c[3]) / c[3] == zf);

	/* the cull mode */
	CHECK(rm_alias_cull_mode(0, RF_WEAPONMODEL, 1) == RMB_CULL_NONE && rm_alias_cull_mode(1, 0, 1) == RMB_CULL_FRONT);
	CHECK(rm_alias_cull_mode(1, RF_WEAPONMODEL, 1) == RMB_CULL_BACK && rm_alias_cull_mode(1, RF_WEAPONMODEL, 0) == RMB_CULL_FRONT);
	CHECK(rm_alias_cull_mode(1, RF_WEAPONMODEL, 2) == RMB_CULL_FRONT && rm_alias_cull_mode(1, RF_DEPTHHACK, 1) == RMB_CULL_FRONT);
	CHECK(rm_alias_cull_mode(1, RF_WEAPONMODEL, 0.5f) == RMB_CULL_FRONT);
	CHECK(rm_alias_drawn(RF_WEAPONMODEL, 0) && rm_alias_drawn(RF_WEAPONMODEL, 1) && !rm_alias_drawn(RF_WEAPONMODEL, 2));
	CHECK(rm_alias_drawn(0, 2) && rm_alias_drawn(RF_DEPTHHACK, 2));

	/* the entity matrix: the shim's glTranslatef and glRotatef in R_RotateForEntity's order on the world's */
	rm_view_matrix(w, world_angles, world_org);

	for (n = 0; n < 2000; n++)
	{
		float a[3], o[3], r[16];

		for (k = 0; k < 3; k++)
		{
			a[k] = (n < 8) ? 90.0f * (float)((n >> k) & 1) : rngf(-360, 360);
			o[k] = rngf(-4000, 4000);
		}

		memcpy(want, w, sizeof(want));
		mtl_mat_translate(r, o[0], o[1], o[2]);
		mtl_mat_mul(want, want, r);
		mtl_mat_rotate(r, a[1], 0, 0, 1);
		mtl_mat_mul(want, want, r);
		mtl_mat_rotate(r, -a[0], 0, 1, 0);
		mtl_mat_mul(want, want, r);
		mtl_mat_rotate(r, -a[2], 1, 0, 0);
		mtl_mat_mul(want, want, r);
		rm_entity_matrix(got, w, o, a);
		same = same && (memcmp(got, want, sizeof(got)) == 0);
	}

	CHECK(same);

	/* an entity at (5, 0, 0) turned 90 degrees in yaw: its +x is the world's +y, so (1, 0, 0) is (5, 1, 0) */
	{
		const float a[3] = { 0, 90, 0 }, o[3] = { 5, 0, 0 };
		float eye1[3], eye2[3];

		mtl_mat_identity(w);
		rm_entity_matrix(got, w, o, a);

		for (i = 0; i < 3; i++)
		{
			eye1[i] = got[i] * 1 + got[12 + i];
			eye2[i] = (i == 0) ? 5 : (i == 1) ? 1 : 0;
		}

		CHECK(fabsf(eye1[0] - eye2[0]) < 1e-6f && fabsf(eye1[1] - eye2[1]) < 1e-6f && fabsf(eye1[2]) < 1e-6f);
	}

	/* the brush entity: its box, its modelorg and the facing of its surfaces, against gl1's lines */
	for (n = 0; n < 20000; n++)
	{
		float org[3], ang[3], mins[3], maxs[3], vo[3], bmins[3], bmaxs[3], rmins[3], rmaxs[3], mo[3], rmo[3];
		float fwd[3], rgt[3], up[3], nrm[3], dist, radius = rngf(0, 300);
		int rot, rrot, pb = n & 1;

		for (k = 0; k < 3; k++)
		{
			org[k] = rngf(-2000, 2000);
			ang[k] = (n % 3) ? 0 : rngf(-180, 180);
			ang[k] = ((n % 3 == 1) && (k == 2)) ? rngf(-180, 180) : ang[k];
			mins[k] = rngf(-100, 0);
			maxs[k] = rngf(0, 100);
			vo[k] = rngf(-2000, 2000);
			nrm[k] = rngf(-1, 1);
		}

		/* gl1_surf.c, R_DrawBrushModel */
		if (ang[0] || ang[1] || ang[2])
		{
			rrot = true;

			for (i = 0; i < 3; i++)
			{
				rmins[i] = org[i] - radius;
				rmaxs[i] = org[i] + radius;
			}
		}
		else
		{
			rrot = false;
			VectorAdd(org, mins, rmins);
			VectorAdd(org, maxs, rmaxs);
		}

		VectorSubtract(vo, org, rmo);

		if (rrot)
		{
			vec3_t temp;
			vec3_t forward, right, upv;

			VectorCopy(rmo, temp);
			AngleVectors(ang, forward, right, upv);
			rmo[0] = DotProduct(temp, forward);
			rmo[1] = -DotProduct(temp, right);
			rmo[2] = DotProduct(temp, upv);
		}

		dist = (n % 5 == 0) ? DotProduct(rmo, nrm) + ((n % 10 == 0) ? 0.01f : -0.01f) : rngf(-100, 100);
		rot = rm_brush_bounds(org, ang, mins, maxs, radius, bmins, bmaxs);
		AngleVectors(ang, fwd, rgt, up);
		rm_brush_modelorg(vo, org, rot, fwd, rgt, up, mo);
		same = same && (rot == rrot) && !memcmp(bmins, rmins, sizeof(bmins)) && !memcmp(bmaxs, rmaxs, sizeof(bmaxs));
		same = same && !memcmp(mo, rmo, sizeof(mo));

		/* gl1_surf.c, R_DrawInlineBModel's facing test */
		{
			float dot = DotProduct(rmo, nrm) - dist;
			int vis = ((pb && (dot < -BACKFACE_EPSILON_REF)) || (!pb && (dot > BACKFACE_EPSILON_REF)));

			same = same && (rm_brush_face_visible(mo, nrm, dist, pb) == vis);
		}
	}

	CHECK(same);

	{
		const float n0[3] = { 1, 0, 0 }, at[3] = { 0.02f, 0, 0 }, at2[3] = { 0.005f, 0, 0 };

		CHECK(rm_brush_face_visible(at, n0, 0, 0) && !rm_brush_face_visible(at, n0, 0, 1));
		CHECK(!rm_brush_face_visible(at2, n0, 0, 0) && !rm_brush_face_visible(at2, n0, 0, 1));
		CHECK(rm_brush_face_visible(at2, n0, 0.02f, 1) && !rm_brush_face_visible(at2, n0, 0.02f, 0));
	}
}

/* the per-entity regions of the frame ring: the layout of two models, and the oversized counts refused */
static void
test_alias_region(void)
{
	mtl_ring_t ring;
	rm_region_t a, b, c;

	ring.size = 8492 + 600;
	ring.used = 0;
	CHECK(rm_alias_region(&ring, 100, 300, &a));
	CHECK(a.voff == 0 && a.vbytes == 4800 && a.ioff == 4864 && a.ibytes == 600 && a.nverts == 100 && a.nindices == 300);
	CHECK(rm_alias_region(&ring, 50, 150, &b));
	CHECK(b.voff == 5632 && b.vbytes == 2400 && b.ioff == 8192 && b.ibytes == 300 && ring.used == 8492);
	/* each region's vertices start on the ring's alignment, so the stride of 48 bytes holds from voff */
	CHECK(a.voff % MTL_RING_ALIGN == 0 && b.voff % MTL_RING_ALIGN == 0 && b.ioff % MTL_RING_ALIGN == 0);
	CHECK(a.voff + (long)a.vbytes <= a.ioff && a.ioff + (long)a.ibytes <= b.voff);

	/* the vertices would fit in what is left, the indices not: refused, the ring as it was */
	CHECK(!rm_alias_region(&ring, 8, 600, &c) && ring.used == 8492 && c.nverts == 0 && c.voff == 0);
	/* oversized counts: never taken, nothing moved */
	CHECK(!rm_alias_region(&ring, RM_ALIAS_MAX_VERTS + 1, 300, &c) && ring.used == 8492);
	CHECK(!rm_alias_region(&ring, 100, RM_ALIAS_MAX_INDICES + 3, &c) && ring.used == 8492);
	CHECK(!rm_alias_region(&ring, 2, 3, &c) && !rm_alias_region(&ring, 3, 2, &c) && !rm_alias_region(&ring, 3, 4, &c));
	CHECK(!rm_alias_region(&ring, -1, 3, &c) && !rm_alias_region(&ring, LONG_MAX, 3, &c) && !rm_alias_region(NULL, 3, 3, &c));
	CHECK(ring.used == 8492);
	/* one past either count is refused even where the ring has the room */
	ring.size = RMB_RING_BYTES;
	ring.used = 0;
	CHECK(!rm_alias_region(&ring, RM_ALIAS_MAX_VERTS + 1, 3, &c) && ring.used == 0);
	CHECK(!rm_alias_region(&ring, 3, RM_ALIAS_MAX_INDICES + 3, &c) && ring.used == 0);
	CHECK(rm_alias_region(&ring, RM_ALIAS_MAX_VERTS, RM_ALIAS_MAX_INDICES, &c) && c.nverts == RM_ALIAS_MAX_VERTS);
	/* at the limit: the largest model in a ring of exactly its size */
	ring.size = 16384 * 48 + 256 * 0 + (size_t)RM_ALIAS_MAX_INDICES * 2;
	ring.used = 0;
	CHECK(rm_alias_region(&ring, RM_ALIAS_MAX_VERTS, RM_ALIAS_MAX_INDICES, &c) && ring.used == ring.size);
	ring.size--;
	ring.used = 0;
	CHECK(!rm_alias_region(&ring, RM_ALIAS_MAX_VERTS, RM_ALIAS_MAX_INDICES, &c) && ring.used == 0);
	/* the frame ring's size holds a model at the limit */
	CHECK((size_t)RM_ALIAS_MAX_VERTS * RM_AVERT_STRIDE + (size_t)RM_ALIAS_MAX_INDICES * 2 + 256 < RMB_RING_BYTES);
}

/* ---- the MD2 and SP2 checks against the shared loader -------------------------------------- */

static void
put32(unsigned char *p, long v)
{
	unsigned u = (unsigned)v;

	p[0] = (unsigned char)u;
	p[1] = (unsigned char)(u >> 8);
	p[2] = (unsigned char)(u >> 16);
	p[3] = (unsigned char)(u >> 24);
}

static void
putf(unsigned char *p, float f)
{
	unsigned u;

	memcpy(&u, &f, sizeof(u));
	put32(p, (long)u);
}

/*
 * A valid MD2 of 4 vertices, 2 frames, 1 skin and a stream of a strip of 3
 * and a fan of 4: header 68, skins 68, st 132, tris 136, frames 148 (56
 * each), commands 260 (24 ints), end 356.
 */
#define MD2_LEN 356
static void
md2_build(unsigned char *b)
{
	static const long h[17] = { IDALIASHEADER, ALIAS_VERSION, 64, 64, 56, 1, 4, 1, 1, 24, 2, 68, 132, 136, 148, 260, 356 };
	int i, k, n;

	memset(b, 0, MD2_LEN);

	for (i = 0; i < 17; i++)
	{
		put32(b + 4 * i, h[i]);
	}

	memcpy(b + 68, "models/test/skin.pcx", 20);

	for (i = 0; i < 2; i++)
	{
		unsigned char *f = b + 148 + 56 * i;

		for (k = 0; k < 3; k++)
		{
			putf(f + 4 * k, 0.5f);
			putf(f + 12 + 4 * k, -16.0f);
		}

		memcpy(f + 24, "frame", 5);

		for (k = 0; k < 4; k++)
		{
			f[40 + 4 * k] = (unsigned char)(10 * k);
			f[41 + 4 * k] = (unsigned char)(20 * k);
			f[42 + 4 * k] = (unsigned char)(30 * k);
			f[43 + 4 * k] = (unsigned char)(40 * k);
		}
	}

	n = 0;
	put32(b + 260 + 4 * n++, 3);

	for (i = 0; i < 3; i++)
	{
		putf(b + 260 + 4 * n++, 0.25f * i);
		putf(b + 260 + 4 * n++, 0.5f);
		put32(b + 260 + 4 * n++, i);
	}

	put32(b + 260 + 4 * n++, -4);

	for (i = 0; i < 4; i++)
	{
		putf(b + 260 + 4 * n++, 0.125f * i);
		putf(b + 260 + 4 * n++, 1);
		put32(b + 260 + 4 * n++, 3 - i);
	}

	put32(b + 260 + 4 * n++, 0);
}

/* the shared loader on a copy of exactly len bytes (ASAN sees a read past them): 1 when it returns a model */
static int
md2_loads(const unsigned char *b, long len)
{
	unsigned char *copy = malloc((size_t)len);
	struct image_s *skins[MAX_MD2SKINS];
	vec3_t mins, maxs;
	modtype_t type = mod_bad;
	void *x;

	memcpy(copy, b, (size_t)len);
	memset(skins, 0, sizeof(skins));
	x = Mod_LoadMD2("test", copy, (int)len, mins, maxs, skins, host_find_image, &type);
	free(copy);
	return x != NULL && type == mod_alias;
}

static int
sp2_loads(const unsigned char *b, long len)
{
	unsigned char *copy = malloc((size_t)len);
	struct image_s *skins[MAX_MD2SKINS];
	modtype_t type = mod_bad;
	void *x;

	memcpy(copy, b, (size_t)len);
	memset(skins, 0, sizeof(skins));
	x = Mod_LoadSP2("test", copy, (int)len, skins, host_find_image, &type);
	free(copy);
	return x != NULL && type == mod_sprite;
}

/* the model rm_md2_check accepted, walked by the draw's own functions over every frame pair: 1 when they agree */
static int
md2_draws(const unsigned char *b, const rm_md2_info_t *in)
{
	static float lerped[MAX_VERTS][4];
	static rm_avert_t out[RM_ALIAS_MAX_VERTS];
	static unsigned short idx[RM_ALIAS_MAX_INDICES];
	static const float sl[3] = { 1, 1, 1 };
	static byte gamma[256];
	int *order = malloc((size_t)in->num_glcmds * sizeof(int));
	long nv, ni;
	int f, ok = 1;

	memcpy(order, b + in->ofs_glcmds, (size_t)in->num_glcmds * sizeof(int));

	for (f = 0; f < in->num_frames && f < 4; f++)
	{
		const unsigned char *fr = b + in->ofs_frames + (size_t)f * in->framesize;
		const unsigned char *of = b + in->ofs_frames + (size_t)((f + 1) % in->num_frames) * in->framesize;
		float scale[3], trans[3], oscale[3], otrans[3], move[3], fv[3], bv[3];
		const float zero[3] = { 0, 0, 0 }, fw[3] = { 1, 0, 0 }, rt[3] = { 0, -1, 0 }, up[3] = { 0, 0, 1 };

		memcpy(scale, fr, sizeof(scale));
		memcpy(trans, fr + 12, sizeof(trans));
		memcpy(oscale, of, sizeof(oscale));
		memcpy(otrans, of + 12, sizeof(otrans));
		rm_alias_lerp_setup(scale, trans, oscale, otrans, 0.5f, zero, fw, rt, up, move, fv, bv);
		rm_alias_lerp(in->num_xyz, fr + RM_MD2_FRAME_HEADER, of + RM_MD2_FRAME_HEADER, f & 1, move, fv, bv, lerped);
		nv = rm_alias_emit(order, in->num_glcmds, (const float (*)[4])lerped, fr + RM_MD2_FRAME_HEADER, in->num_xyz,
				f, sl, f & 1, NULL, gamma, 255, out, RM_ALIAS_MAX_VERTS, idx, RM_ALIAS_MAX_INDICES, &ni);
		ok = ok && (nv == in->verts) && (ni == in->indices);
	}

	free(order);
	return ok;
}

static void
test_md2_check(void)
{
	static unsigned char b[MD2_LEN], m[MD2_LEN + 64];
	/* each field of the header set to a value the loader refuses, by byte offset */
	static const long loaderbad[][2] = {
		{ 4, 7 }, { 4, 9 }, { 64, -1 }, { 64, MD2_LEN + 1 }, { 12, 481 }, { 24, 0 }, { 24, -3 }, { 24, 2049 },
		{ 28, 0 }, { 32, 0 }, { 40, 0 }, { 40, -1 }, { 20, -1 }, { 36, 0 }, { 36, -1 }, { 16, 55 }, { 16, -56 },
		{ 44, -1 }, { 44, MD2_LEN - 63 }, { 48, -4 }, { 48, MD2_LEN - 3 }, { 52, -12 }, { 52, MD2_LEN - 11 },
		{ 56, -56 }, { 56, MD2_LEN - 111 }, { 60, -4 }, { 60, MD2_LEN - 95 }, { 40, 0x7fffffff }, { 16, 0x7ffffffc },
		{ 36, 0x7fffffff }
	};
	rm_md2_info_t in;
	long n, i, accepted = 0, refused = 0;
	int same = 1;

	Swap_Init();
	md2_build(b);
	CHECK(rm_md2_check(b, MD2_LEN, &in));
	CHECK(in.num_xyz == 4 && in.num_frames == 2 && in.num_skins == 1 && in.framesize == 56 && in.verts == 7 &&
		in.indices == 9 && in.ofs_glcmds == 260 && in.num_glcmds == 24 && in.ofs_frames == 148);
	CHECK(md2_loads(b, MD2_LEN) && md2_draws(b, &in));
	CHECK(!rm_md2_check(b, MD2_LEN - 1, &in) && in.verts == 0 && !rm_md2_check(b, RM_MD2_HEADER - 1, &in));
	CHECK(!rm_md2_check(NULL, MD2_LEN, &in));

	/* every refusal of the loader is a refusal here, so the loader never fails (and leaks its hunk) on a model passed */
	for (i = 0; i < (long)(sizeof(loaderbad) / sizeof(loaderbad[0])); i++)
	{
		memcpy(m, b, MD2_LEN);
		put32(m + loaderbad[i][0], loaderbad[i][1]);
		same = same && !md2_loads(m, MD2_LEN) && !rm_md2_check(m, MD2_LEN, &in);
	}

	CHECK(same);
	memcpy(m, b, MD2_LEN);
	put32(m, IDSPRITEHEADER);
	CHECK(!rm_md2_check(m, MD2_LEN, &in));

	/* what the loader passes and gl1's draw trusts: each refused here */
#define MD2_CASE(off, val, store) \
	do { memcpy(m, b, MD2_LEN); store(m + (off), (val)); CHECK(md2_loads(m, MD2_LEN) && !rm_md2_check(m, MD2_LEN, &in)); } while (0)
	MD2_CASE(260, 2, put32);            /* a strip of 2 */
	MD2_CASE(260, -2, put32);
	MD2_CASE(260, 1, put32);
	MD2_CASE(260, 7, put32);            /* past the stream */
	MD2_CASE(260, 0x7fffffff, put32);
	MD2_CASE(260, -0x7fffffff - 1, put32);
	MD2_CASE(260 + 12, 4, put32);       /* a vertex index past num_xyz */
	MD2_CASE(260 + 12, -1, put32);
	MD2_CASE(260 + 4 * 23, 5, put32);   /* no terminating 0 */
	MD2_CASE(260 + 4, NAN, putf);       /* a texture coordinate that is not finite */
	MD2_CASE(260 + 8, INFINITY, putf);
	MD2_CASE(148, NAN, putf);           /* a frame's scale or translate */
	MD2_CASE(148 + 56 + 20, -INFINITY, putf);
#undef MD2_CASE
	memcpy(m, b, MD2_LEN);
	m[148 + 56 + 43] = 162;             /* a light normal index past the table of 162 */
	CHECK(md2_loads(m, MD2_LEN) && !rm_md2_check(m, MD2_LEN, &in));
	m[148 + 56 + 43] = 161;
	CHECK(rm_md2_check(m, MD2_LEN, &in));
	memcpy(m, b, MD2_LEN);
	memset(m + 68, 'a', 64);            /* a skin name without its end */
	CHECK(md2_loads(m, MD2_LEN) && !rm_md2_check(m, MD2_LEN, &in));
	m[68 + 63] = 0;
	CHECK(rm_md2_check(m, MD2_LEN, &in));
	memcpy(m, b, MD2_LEN);
	put32(m + 4 * 15, 262);             /* the commands, the frames, the frame size, the st not aligned */
	put32(m + 4 * 16, 358);
	CHECK(!rm_md2_check(m, MD2_LEN + 2, &in));
	memcpy(m, b, MD2_LEN);
	put32(m + 4 * 12, 133);
	CHECK(!rm_md2_check(m, MD2_LEN, &in));
	memcpy(m, b, MD2_LEN);
	put32(m + 4 * 13, 137);
	CHECK(!rm_md2_check(m, MD2_LEN, &in));
	memcpy(m, b, MD2_LEN);
	put32(m + 4 * 14, 150);
	put32(m + 4 * 10, 1);
	CHECK(!rm_md2_check(m, MD2_LEN, &in));
	memcpy(m, b, MD2_LEN);
	put32(m + 4 * 4, 58);
	put32(m + 4 * 10, 1);
	CHECK(!rm_md2_check(m, MD2_LEN, &in));
	/* more than 32 skins are 32 to the loader and here; their names must end inside the file */
	memcpy(m, b, MD2_LEN);
	put32(m + 4 * 5, 40);
	CHECK(!md2_loads(m, MD2_LEN) && !rm_md2_check(m, MD2_LEN, &in));
	/* a strip of 2 between two good ones (gl1 draws nothing of it; a count below 3 is refused all the same) */
	memcpy(m, b, MD2_LEN);
	put32(m + 260 + 4 * 10, 2);
	put32(m + 260 + 4 * 17, 0);
	CHECK(md2_loads(m, MD2_LEN) && !rm_md2_check(m, MD2_LEN, &in) && in.verts == 0 && in.indices == 0);
	/* the stream moved 2 bytes off its alignment, whole (the loader would read it misaligned): refused */
	memcpy(m, b, MD2_LEN);
	memmove(m + 262, b + 260, 96);
	put32(m + 4 * 15, 262);
	put32(m + 4 * 16, 358);
	CHECK(!rm_md2_check(m, MD2_LEN + 2, &in));
	/* 40 skins declared, room for the 32 the loader keeps: accepted as the loader accepts them */
	{
		const long len = 68 + 32 * 64 + 4 + 12 + 112 + 96;
		unsigned char *sk = calloc(1, (size_t)len);
		long k;

		memcpy(sk, b, 68);
		put32(sk + 4 * 5, 40);
		put32(sk + 4 * 12, 68 + 32 * 64);
		put32(sk + 4 * 13, 68 + 32 * 64 + 4);
		put32(sk + 4 * 14, 68 + 32 * 64 + 16);
		put32(sk + 4 * 15, 68 + 32 * 64 + 128);
		put32(sk + 4 * 16, len);
		memcpy(sk + 68 + 32 * 64 + 16, b + 148, 112 + 96);

		for (k = 0; k < 32; k++)
		{
			memcpy(sk + 68 + 64 * k, "models/test/skin.pcx", 20);
		}

		CHECK(md2_loads(sk, len) && rm_md2_check(sk, len, &in) && in.num_skins == 32 && md2_draws(sk, &in));
		free(sk);
	}
	/* 2049 vertices with the frames to hold them: the loader refuses them, so the check does */
	{
		const long fs = 40 + 4 * 2049, len = 260 + 2 * fs + 96;
		unsigned char *big = calloc(1, (size_t)len);

		memcpy(big, b, 148);
		put32(big + 4 * 4, fs);
		put32(big + 4 * 6, 2049);
		put32(big + 4 * 14, 148);
		put32(big + 4 * 15, 148 + 2 * fs);
		put32(big + 4 * 16, 148 + 2 * fs + 96);
		memcpy(big + 148 + 2 * fs, b + 260, 96);
		CHECK(!md2_loads(big, len) && !rm_md2_check(big, len, &in));
		put32(big + 4 * 6, 2048);
		CHECK(md2_loads(big, len) && rm_md2_check(big, len, &in) && md2_draws(big, &in));
		free(big);
	}
	/* a stream whose last vertex runs one int past the file, read from a copy of exactly that size */
	{
		const long len = 260 + 4 * 9;
		unsigned char *cut = malloc((size_t)len);

		memcpy(cut, b, (size_t)len);
		put32(cut + 4 * 9, 9);
		put32(cut + 4 * 16, len);
		put32(cut + 260, 3);
		CHECK(!rm_md2_check(cut, len, &in) && in.verts == 0);
		free(cut);
	}
	/* a stream without its end leaves no count behind */
	memcpy(m, b, MD2_LEN);
	put32(m + 260 + 4 * 23, 3);
	CHECK(!rm_md2_check(m, MD2_LEN, &in) && in.verts == 0 && in.indices == 0);
	/* a stream of just its end: nothing to draw, refused */
	memcpy(m, b, MD2_LEN);
	put32(m + 260, 0);
	CHECK(md2_loads(m, MD2_LEN) && !rm_md2_check(m, MD2_LEN, &in));
	/* a frame size larger than its vertices need, on 4 bytes: accepted */
	memcpy(m, b, MD2_LEN);
	put32(m + 4 * 4, 60);
	put32(m + 4 * 10, 1);
	CHECK(rm_md2_check(m, MD2_LEN, &in) && in.framesize == 60 && md2_loads(m, MD2_LEN) && md2_draws(m, &in));

	/* the stream's vertex total: at the limit accepted, one past refused (a model of fans of 4 096 vertices) */
	{
		const long fans = 4, per = RM_ALIAS_MAX_VERTS / 4, ints = fans * (1 + 3 * per) + 1;
		const long len = 260 + 4 * (ints + 3);
		unsigned char *big = calloc(1, (size_t)len);
		long f, k, at;

		memcpy(big, b, 260);
		put32(big + 4 * 9, ints + 3);
		put32(big + 4 * 16, len);

		for (f = 0, at = 260; f < fans; f++)
		{
			put32(big + at, -per);
			at += 4;

			for (k = 0; k < per; k++, at += 12)
			{
				put32(big + at + 8, k % 4);
			}
		}

		put32(big + at, 0);
		CHECK(rm_md2_check(big, len, &in) && in.verts == RM_ALIAS_MAX_VERTS && in.indices == 3 * (RM_ALIAS_MAX_VERTS - 8));
		CHECK(md2_draws(big, &in));
		/* the last fan takes one vertex more (index 0, from the spare ints) and ends on the last: past the limit */
		put32(big + 260 + 4 * 3 * (1 + 3 * per), -(per + 1));
		CHECK(!rm_md2_check(big, len, &in));
		free(big);
	}

	/*
	 * Mutated models: every one rm_md2_check accepts is loaded by the
	 * shared loader (which never fails on it) and drawn by the draw's
	 * functions with the counts the check found; ASAN and UBSAN watch.
	 */
	for (n = 0; n < 60000; n++)
	{
		long k, len = MD2_LEN + (long)(rng() % 64) - 32;

		memcpy(m, b, MD2_LEN);
		memset(m + MD2_LEN, 0, 64);

		for (k = 0; k < 1 + (long)(rng() % 4); k++)
		{
			const long at = (long)(rng() % MD2_LEN);

			switch (rng() % 4)
			{
				case 0: m[at] = (unsigned char)rng(); break;
				case 1: put32(m + (at & ~3l), (long)(rng() % 600) - 100); break;
				case 2: put32(m + (at & ~3l), (long)(rng() % 9) - 4); break;
				default: m[at] ^= (unsigned char)(1u << (rng() % 8)); break;
			}
		}

		if (len < 1)
		{
			len = 1;
		}

		if (rm_md2_check(m, len, &in))
		{
			accepted++;
			same = same && md2_loads(m, len) && md2_draws(m, &in);
		}
		else
		{
			refused++;
		}
	}

	CHECK(same && accepted > 1000 && refused > 1000 && hunk_overflow == 0);
	free(hunk_mem);
	hunk_mem = NULL;
}

/*
 * The regions the shared loader copies into its hunk at their own offsets
 * (skins, st, tris, frames, commands) after it has swapped the 68-byte
 * header there and clamped its num_skins to 32: a region over the header
 * puts the file's words back, and a num_skins past 32 then makes the
 * loader's skin loop write past model_t.skins (TASK90's security review);
 * two regions over each other make the hunk differ from the file the
 * check read. Every region offset below the header and every two regions
 * that share a byte are refused.
 */

/* b's regions: where they are, how long they are */
static const long md2_from[5] = { 68, 132, 136, 148, 260 }, md2_size[5] = { 64, 4, 12, 112, 96 };

/* m: b's header and its five regions at ofs[0..4], ofs_end after the last; the header is written last */
static long
md2_relayout(unsigned char *m, long cap, const unsigned char *b, const long ofs[5])
{
	long end = RM_MD2_HEADER, i;

	memset(m, 0, (size_t)cap);

	for (i = 0; i < 5; i++)
	{
		memcpy(m + ofs[i], b + md2_from[i], (size_t)md2_size[i]);

		if (ofs[i] + md2_size[i] > end)
		{
			end = ofs[i] + md2_size[i];
		}
	}

	memcpy(m, b, RM_MD2_HEADER);

	for (i = 0; i < 5; i++)
	{
		put32(m + 4 * (11 + i), ofs[i]);
	}

	put32(m + 4 * 16, end);
	return end;
}

/* 40 skins declared and named, then b's st, tris, frames and commands: skins 68, st 2628, tris 2632, frames 2644, commands 2756 */
#define MD2_SK40_LEN (68 + 40 * 64 + 4 + 12 + 112 + 96)
static void
md2_build40(unsigned char *sk, const unsigned char *b)
{
	static const long ofs[5] = { 68, 68 + 40 * 64, 68 + 40 * 64 + 4, 68 + 40 * 64 + 16, 68 + 40 * 64 + 128 };
	long k;

	memset(sk, 0, MD2_SK40_LEN);
	memcpy(sk, b, RM_MD2_HEADER);
	put32(sk + 4 * 5, 40);

	for (k = 1; k < 5; k++)
	{
		memcpy(sk + ofs[k], b + md2_from[k], (size_t)md2_size[k]);
		put32(sk + 4 * (11 + k), ofs[k]);
	}

	put32(sk + 4 * 16, MD2_SK40_LEN);

	for (k = 0; k < 40; k++)
	{
		snprintf((char *)sk + 68 + 64 * k, 64, "models/test/skin%ld.pcx", k);
	}
}

static void
test_md2_regions(void)
{
	static unsigned char b[MD2_LEN], m[1024], sk[MD2_SK40_LEN], f[MD2_SK40_LEN];
	static const long seed_ofs[5] = { 68, 2628, 2632, 2644, 2756 }, seed_size[5] = { 2048, 4, 12, 112, 96 };
	rm_md2_info_t in;
	long r, i, n, ofs[5], len, accepted = 0, refused = 0;
	int same = 1;

	/* rm_regions_apart at and past each limit: the header's end, a byte shared, an empty region, a negative length */
	{
		static const long o1[] = { 68 }, l1[] = { 10 }, o0[] = { 67 }, z[] = { 0 }, neg[] = { -1 };
		static const long o2[] = { 68, 78 }, o2x[] = { 68, 77 }, o2r[] = { 78, 68 }, o2rx[] = { 77, 68 }, l2[] = { 10, 10 };
		static const long o2in[] = { 68, 70 }, l2in[] = { 10, 2 }, o2eq[] = { 70, 70 }, l2e[] = { 10, 0 }, o2e[] = { 68, 72 };
		static const long o5[] = { 68, 100, 200, 300, 400 }, l5[] = { 32, 100, 100, 100, 10 };
		static const long o5x[] = { 68, 100, 200, 300, 99 }, o5y[] = { 68, 100, 200, 300, 399 }, o5z[] = { 68, 100, 200, 300, 67 };
		static const long big[] = { 0x7fffff00l }, lbig[] = { 0x7fffff00l };

		CHECK(rm_regions_apart(o1, l1, 1, 68) && !rm_regions_apart(o0, l1, 1, 68) && !rm_regions_apart(z, l1, 1, 68));
		CHECK(!rm_regions_apart(z, z, 1, 68) && !rm_regions_apart(neg, l1, 1, 68) && !rm_regions_apart(o1, neg, 1, 68));
		CHECK(rm_regions_apart(z, l1, 1, 0) && rm_regions_apart(o1, l1, 0, 68) && rm_regions_apart(big, lbig, 1, 68));
		CHECK(rm_regions_apart(o2, l2, 2, 68) && !rm_regions_apart(o2x, l2, 2, 68));
		CHECK(rm_regions_apart(o2r, l2, 2, 68) && !rm_regions_apart(o2rx, l2, 2, 68));
		CHECK(!rm_regions_apart(o2in, l2in, 2, 68) && !rm_regions_apart(o2eq, l2, 2, 68) && !rm_regions_apart(o2, l2, 2, 69));
		CHECK(rm_regions_apart(o2e, l2e, 2, 68) && rm_regions_apart(o2eq, l2e, 2, 68));
		CHECK(rm_regions_apart(o5, l5, 5, 68) && !rm_regions_apart(o5x, l5, 5, 68) && !rm_regions_apart(o5y, l5, 5, 68));
		CHECK(!rm_regions_apart(o5z, l5, 5, 68) && rm_regions_apart(o5x, l5, 4, 68));
	}

	md2_build(b);

	/* each region first, at the header's end, the others after it: accepted, loaded and drawn */
	for (r = 0; r < 5; r++)
	{
		long at = RM_MD2_HEADER;

		ofs[r] = at;
		at += md2_size[r];

		for (i = 0; i < 5; i++)
		{
			if (i != r)
			{
				ofs[i] = at;
				at += md2_size[i];
			}
		}

		len = md2_relayout(m, sizeof(m), b, ofs);
		CHECK(rm_md2_check(m, len, &in) && md2_loads(m, len) && md2_draws(m, &in));

		/* the same region 4 bytes into the header (2 for st and tris, 1 for the skins), and at 0: refused */
		ofs[r] = RM_MD2_HEADER - 4;
		len = md2_relayout(m, sizeof(m), b, ofs);
		CHECK(!rm_md2_check(m, len, &in) && in.verts == 0);

		if ((r == 1) || (r == 2) || (r == 0))
		{
			ofs[r] = RM_MD2_HEADER - ((r == 0) ? 1 : 2);
			len = md2_relayout(m, sizeof(m), b, ofs);
			CHECK(!rm_md2_check(m, len, &in));
		}

		ofs[r] = 0;
		len = md2_relayout(m, sizeof(m), b, ofs);
		CHECK(!rm_md2_check(m, len, &in));
	}

	/* the review's case: 40 skins named, their offset moved over the header (the loader would put 40 back) */
	md2_build40(sk, b);
	CHECK(rm_md2_check(sk, MD2_SK40_LEN, &in) && in.num_skins == 32 && md2_loads(sk, MD2_SK40_LEN) && md2_draws(sk, &in));
	memcpy(f, sk, MD2_SK40_LEN);
	put32(f + 4 * 11, 0);
	CHECK(!rm_md2_check(f, MD2_SK40_LEN, &in) && in.verts == 0);
	put32(f + 4 * 11, 20);              /* num_skins's own word */
	CHECK(!rm_md2_check(f, MD2_SK40_LEN, &in));
	put32(f + 4 * 11, 24);              /* past it, still the header */
	CHECK(!rm_md2_check(f, MD2_SK40_LEN, &in));
	put32(f + 4 * 11, 67);
	CHECK(!rm_md2_check(f, MD2_SK40_LEN, &in));
	put32(f + 4 * 11, 68);
	CHECK(rm_md2_check(f, MD2_SK40_LEN, &in) && md2_loads(f, MD2_SK40_LEN));

	/* regions over each other that every other check passes and the loader loads: refused */
	memcpy(m, b, MD2_LEN);
	put32(m + 4 * 12, 144);             /* st over the tris' last 4 bytes */
	CHECK(md2_loads(m, MD2_LEN) && !rm_md2_check(m, MD2_LEN, &in));
	memcpy(m, b, MD2_LEN);
	put32(m + 4 * 13, 134);             /* tris over the st's last 2 bytes */
	CHECK(md2_loads(m, MD2_LEN) && !rm_md2_check(m, MD2_LEN, &in));
	memcpy(m, b, MD2_LEN);
	put32(m + 4 * 11, 260);             /* the skin name read from the commands */
	CHECK(md2_loads(m, MD2_LEN) && !rm_md2_check(m, MD2_LEN, &in));
	memcpy(m, b, MD2_LEN);
	memmove(m + 152, b + 148, 112);     /* the frames 4 bytes later, the last vertex over the first command */
	put32(m + 260, 3);
	put32(m + 4 * 14, 152);
	CHECK(md2_loads(m, MD2_LEN) && !rm_md2_check(m, MD2_LEN, &in));
	memcpy(m, b, MD2_LEN);
	put32(m + 4 * 15, 256);             /* ... and the commands 4 bytes earlier: they still parse, over the frames */
	memmove(m + 256, b + 260, 96);
	put32(m + 4 * 16, 352);
	CHECK(md2_loads(m, 352) && !rm_md2_check(m, 352, &in));
	/* touching is not overlapping: the base model's st ends where its tris begin, its frames end where the commands begin */
	CHECK(rm_md2_check(b, MD2_LEN, &in));
	/* no skins: an empty region shares no byte, wherever it is past the header; below it, refused */
	memcpy(m, b, MD2_LEN);
	put32(m + 4 * 5, 0);
	put32(m + 4 * 11, 132);
	CHECK(rm_md2_check(m, MD2_LEN, &in) && in.num_skins == 0 && md2_loads(m, MD2_LEN) && md2_draws(m, &in));
	put32(m + 4 * 11, 0);
	CHECK(!rm_md2_check(m, MD2_LEN, &in));
	/* an empty skins region strictly inside the st (132..136): no byte shared, accepted */
	put32(m + 4 * 11, 134);
	CHECK(rm_md2_check(m, MD2_LEN, &in) && in.num_skins == 0 && md2_loads(m, MD2_LEN));
	/* the tris over the commands tail (commands 260..356, tris 340..352): refused */
	memcpy(m, b, MD2_LEN);
	put32(m + 4 * 13, 340);
	CHECK(!rm_md2_check(m, MD2_LEN, &in));

	/*
	 * Mutated models of the 40 skins: region offsets moved into the header,
	 * onto, into and next to the other regions, num_skins changed, bytes
	 * changed; every one accepted is loaded (its skins array is MAX_MD2SKINS
	 * long, ASAN watches) and drawn.
	 */
	for (n = 0; n < 40000; n++)
	{
		long k;

		memcpy(f, sk, MD2_SK40_LEN);
		len = MD2_SK40_LEN;

		for (k = 0; k < 1 + (long)(rng() % 3); k++)
		{
			static const long hdr[] = { 0, 4, 20, 24, 64, 66, 67, 68 }, d[] = { -4, -2, 0, 2, 4 };
			static const long skins[] = { 0, 1, 31, 32, 33, 40, 64, 1000, 0x7fffffff };
			const long o = (long)(rng() % 5);

			switch (rng() % 4)
			{
				case 0:
					put32(f + 4 * (11 + (rng() % 5)), (rng() & 1) ? hdr[rng() % 8] :
						((rng() & 1) ? seed_ofs[o] : seed_ofs[o] + seed_size[o]) + d[rng() % 5]);
					break;
				case 1: put32(f + 4 * 5, skins[rng() % 9]); break;
				case 2: f[rng() % MD2_SK40_LEN] = (unsigned char)rng(); break;
				default: len = MD2_SK40_LEN - 4 * (long)(rng() % 3); put32(f + 4 * 16, len); break;
			}
		}

		if (rm_md2_check(f, len, &in))
		{
			accepted++;
			same = same && md2_loads(f, len) && md2_draws(f, &in);
		}
		else
		{
			refused++;
		}
	}

	CHECK(same && accepted > 1000 && refused > 1000 && hunk_overflow == 0);
	free(hunk_mem);
	hunk_mem = NULL;
}

static void
test_sp2_check(void)
{
	enum { LEN = 12 + 2 * 80 };
	unsigned char b[LEN], m[LEN];
	long n, accepted = 0;
	int same = 1;

	memset(b, 0, sizeof(b));
	put32(b, IDSPRITEHEADER);
	put32(b + 4, SPRITE_VERSION);
	put32(b + 8, 2);
	put32(b + 12, 32);
	memcpy(b + 12 + 16, "sprites/s_a.pcx", 15);
	memcpy(b + 92 + 16, "sprites/s_b.pcx", 15);
	CHECK(rm_sp2_check(b, LEN) && sp2_loads(b, LEN));
	CHECK(!rm_sp2_check(b, LEN - 1) && !rm_sp2_check(b, 11) && !rm_sp2_check(NULL, LEN));
	memcpy(m, b, LEN);
	put32(m + 4, 3);
	CHECK(!rm_sp2_check(m, LEN) && !sp2_loads(m, LEN));
	memcpy(m, b, LEN);
	put32(m + 8, 0);
	CHECK(!rm_sp2_check(m, LEN));
	put32(m + 8, -1);
	CHECK(!rm_sp2_check(m, LEN));
	put32(m + 8, 3);                    /* a frame past the file: the loader would read it */
	CHECK(!rm_sp2_check(m, LEN));
	put32(m + 8, 1000);                 /* kept as 32 by the loader, past the file */
	CHECK(!rm_sp2_check(m, LEN));
	put32(m + 8, 1);
	CHECK(rm_sp2_check(m, LEN) && sp2_loads(m, LEN));
	/* a first frame's name without its end, the next frame's width 0 after it: refused */
	memcpy(m, b, LEN);
	memset(m + 12 + 16, 'x', 64);
	put32(m + 92, 0);
	CHECK(!rm_sp2_check(m, LEN));
	/* 40 frames declared, the 32 the loader keeps in the file: accepted */
	{
		const long len = 12 + 32 * 80;
		unsigned char *sp = calloc(1, (size_t)len);

		memcpy(sp, b, 12);
		put32(sp + 8, 40);
		CHECK(rm_sp2_check(sp, len) && sp2_loads(sp, len));
		CHECK(!rm_sp2_check(sp, len - 1));
		free(sp);
	}
	memcpy(m, b, LEN);
	memset(m + 92 + 16, 'x', 64);       /* a frame name without its end */
	CHECK(!rm_sp2_check(m, LEN));
	m[LEN - 1] = 0;
	CHECK(rm_sp2_check(m, LEN));
	memcpy(m, b, LEN);
	put32(m, IDALIASHEADER);
	CHECK(!rm_sp2_check(m, LEN));

	for (n = 0; n < 20000; n++)
	{
		long len = LEN - (long)(rng() % 40);

		memcpy(m, b, LEN);

		switch (rng() % 4)
		{
			case 0: m[rng() % 12] ^= (unsigned char)(1u << (rng() % 8)); break;
			case 1: put32(m + 8, (long)(rng() % 8) - 3); break;
			case 2: m[12 + rng() % 160] = (unsigned char)rng(); break;
			default: break;
		}

		if (rng() & 1)
		{
			len = LEN;
		}

		if (rm_sp2_check(m, len))
		{
			accepted++;
			same = same && sp2_loads(m, len);
		}
	}

	CHECK(same && accepted > 100);
	free(hunk_mem);
	hunk_mem = NULL;
}

/* ---- N8: sprites, beams, null models, shadows, dynamic lights, flash blobs ---------- */

/* gl1_light.c, the body of R_AddDynamicLights' loop for one light dl, verbatim but for the surface's fields passed in */
static void
ref_add_dynamic_light(float *s_blocklights, int smax, int tmax, const float *normal, float dist,
		const float (*vecs)[4], const short *texturemins, const dlight_t *dl)
{
	int sd, td;
	float fdist, frad, fminlight;
	vec3_t impact, local;
	int s, t;
	int i;
	float *pfBL;
	float fsacc, ftacc;

	frad = dl->intensity;
	fdist = DotProduct(dl->origin, normal) -
			dist;
	frad -= fabs(fdist);

	/* rad is now the highest intensity on the plane */
	fminlight = DLIGHT_CUTOFF;

	if (frad < fminlight)
	{
		return;
	}

	fminlight = frad - fminlight;

	for (i = 0; i < 3; i++)
	{
		impact[i] = dl->origin[i] -
					normal[i] * fdist;
	}

	local[0] = DotProduct(impact,
			   vecs[0]) + vecs[0][3] - texturemins[0];
	local[1] = DotProduct(impact,
			   vecs[1]) + vecs[1][3] - texturemins[1];

	pfBL = s_blocklights;

	for (t = 0, ftacc = 0; t < tmax; t++, ftacc += 16)
	{
		td = local[1] - ftacc;

		if (td < 0)
		{
			td = -td;
		}

		for (s = 0, fsacc = 0; s < smax; s++, fsacc += 16, pfBL += 3)
		{
			sd = Q_ftol(local[0] - fsacc);

			if (sd < 0)
			{
				sd = -sd;
			}

			if (sd > td)
			{
				fdist = sd + (td >> 1);
			}
			else
			{
				fdist = td + (sd >> 1);
			}

			if (fdist < fminlight)
			{
				pfBL[0] += (frad - fdist) * dl->color[0];
				pfBL[1] += (frad - fdist) * dl->color[1];
				pfBL[2] += (frad - fdist) * dl->color[2];
			}
		}
	}
}

/* rm_lm_dlight with a dlight_t and a texinfo's vecs, as rm_light.c calls it */
static int
rm_dlight_of(float *bl, int smax, int tmax, const float *normal, float dist, const float (*vecs)[4],
		const short *tmins, const dlight_t *dl)
{
	return rm_lm_dlight(bl, smax, tmax, normal, dist, vecs[0], vecs[1], tmins[0], tmins[1], dl->origin,
			dl->intensity, dl->color);
}

/*
 * gl1's R_AddDynamicLights on a 3 by 2 surface in the z = 0 plane, worked
 * by hand: a light 100 above luxel (1, 0)'s corner of radius 200 lights
 * every luxel (frad 100, the cutoff 36 away: 80 and 92); 120 above, only
 * the luxels within 16 (72); 150 above, none (below DLIGHT_CUTOFF on the
 * plane). Then the bounds, the split of rm_lm_build against N5's, and
 * random surfaces and lights against gl1's lines.
 */
static void
test_lm_dlight(void)
{
	static const float normal[3] = { 0, 0, 1 };
	static const float vecs[2][4] = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 } };
	static const short tmins[2] = { 0, 0 };
	float bl[RM_LM_MAX_LUXELS * 3], ref[RM_LM_MAX_LUXELS * 3];
	dlight_t dl;
	int i, n, same = 1;

	CHECK(RM_DLIGHT_CUTOFF == DLIGHT_CUTOFF);

	memset(&dl, 0, sizeof(dl));
	dl.origin[0] = 16;
	dl.origin[1] = 8;
	dl.origin[2] = 100;
	dl.intensity = 200;
	dl.color[0] = 1;
	dl.color[1] = 0.5f;
	dl.color[2] = 0.25f;

	memset(bl, 0, sizeof(bl));
	CHECK(rm_dlight_of(bl, 3, 2, normal, 0, vecs, tmins, &dl));
	CHECK(bl[0] == 80 && bl[1] == 40 && bl[2] == 20 && bl[3] == 92 && bl[4] == 46 && bl[5] == 23);
	CHECK(bl[6] == 80 && bl[9] == 80 && bl[12] == 92 && bl[15] == 80 && bl[17] == 20 && bl[18] == 0);

	/* added to what the styles left, and stored as gl1 stores: 92 46 23, alpha the brightest */
	{
		static const unsigned char zero[3 * 6];
		const float one[1][3] = { { 1, 1, 1 } };
		unsigned char ident[256], out[2][12];

		for (i = 0; i < 256; i++)
		{
			ident[i] = (unsigned char)i;
		}

		CHECK(rm_lm_styles(zero, 3, 2, 1, one, 1.0f, bl) == 2);
		CHECK(rm_dlight_of(bl, 3, 2, normal, 0, vecs, tmins, &dl));
		CHECK(rm_lm_store(3, 2, NULL, ident, &out[0][0], 12, bl));
		CHECK(out[0][0] == 80 && out[0][1] == 40 && out[0][2] == 20 && out[0][3] == 80);
		CHECK(out[0][4] == 92 && out[0][5] == 46 && out[0][6] == 23 && out[0][7] == 92 && out[1][4] == 92);
		/* no light data: 255 everywhere and 1 (gl1 adds no dynamic light then) */
		CHECK(rm_lm_styles(NULL, 3, 2, 1, one, 1.0f, bl) == 1 && bl[0] == 255 && bl[17] == 255);
	}

	/* 120 above: frad 80, the luxels nearer than 16 only: (1, 0) and (1, 1) at 8, 72 each */
	dl.origin[2] = 120;
	memset(bl, 0, sizeof(bl));
	CHECK(rm_dlight_of(bl, 3, 2, normal, 0, vecs, tmins, &dl));
	CHECK(bl[0] == 0 && bl[3] == 72 && bl[4] == 36 && bl[5] == 18 && bl[6] == 0 && bl[12] == 72 && bl[15] == 0);

	/* 150 above (or below the plane: the distance is |fdist|): nothing, and taken */
	dl.origin[2] = 150;
	memset(bl, 0, sizeof(bl));
	CHECK(rm_dlight_of(bl, 3, 2, normal, 0, vecs, tmins, &dl) && bl[3] == 0 && bl[4] == 0);
	dl.origin[2] = -100;
	CHECK(rm_dlight_of(bl, 3, 2, normal, 0, vecs, tmins, &dl) && bl[3] == 92);

	/*
	 * a luxel exactly at the cutoff's distance gets nothing (gl1's <): the
	 * light 100 above (36, 0), frad 100, the cutoff 36 away; (0, 0) is 36
	 * away, (1, 0) 20 (80), (2, 0) 4 (96), (0, 1) 44, (1, 1) 28 (72), (2, 1) 18 (82)
	 */
	dl.origin[0] = 36;
	dl.origin[1] = 0;
	dl.origin[2] = 100;
	memset(bl, 0, sizeof(bl));
	CHECK(rm_dlight_of(bl, 3, 2, normal, 0, vecs, tmins, &dl));
	CHECK(bl[0] == 0 && bl[3] == 80 && bl[6] == 96 && bl[9] == 0 && bl[12] == 72 && bl[15] == 82);
	dl.origin[0] = 16;
	dl.origin[1] = 8;

	/* the sizes rm_lm_size allows, and nothing written outside them */
	dl.origin[2] = 100;
	memset(bl, 0, sizeof(bl));
	CHECK(!rm_dlight_of(bl, 0, 2, normal, 0, vecs, tmins, &dl) && !rm_dlight_of(bl, 3, 0, normal, 0, vecs, tmins, &dl));
	CHECK(!rm_dlight_of(bl, RM_LM_WIDTH, 1, normal, 0, vecs, tmins, &dl));
	CHECK(!rm_dlight_of(bl, 30, 30, normal, 0, vecs, tmins, &dl) && bl[0] == 0);
	CHECK(rm_dlight_of(bl, RM_LM_WIDTH - 1, 6, normal, 0, vecs, tmins, &dl));
	CHECK(rm_dlight_of(bl, 17, 51, normal, 0, vecs, tmins, &dl) && !rm_dlight_of(bl, 17, 52, normal, 0, vecs, tmins, &dl));

	/* what gl1 would carry into an undefined int conversion: refused, nothing added */
	{
		const float nan_normal[3] = { NAN, 0, 1 }, big_normal[3] = { 0, 0, 3e38f };
		float huge[2][4] = { { 1e30f, 0, 0, 0 }, { 0, 1, 0, 0 } }, edge[2][4] = { { 0, 0, 0, 0 }, { 0, 1, 0, 0 } };
		dlight_t odd = dl;

		memset(bl, 0, sizeof(bl));
		CHECK(!rm_dlight_of(bl, 3, 2, nan_normal, 0, vecs, tmins, &dl));
		CHECK(!rm_dlight_of(bl, 3, 2, normal, NAN, vecs, tmins, &dl));
		CHECK(!rm_dlight_of(bl, 3, 2, normal, INFINITY, vecs, tmins, &dl));
		CHECK(!rm_dlight_of(bl, 3, 2, big_normal, 0, vecs, tmins, &dl));
		CHECK(!rm_dlight_of(bl, 3, 2, normal, 0, (const float (*)[4])huge, tmins, &dl));
		odd.intensity = NAN;
		CHECK(!rm_dlight_of(bl, 3, 2, normal, 0, vecs, tmins, &odd));
		odd.intensity = INFINITY;
		CHECK(!rm_dlight_of(bl, 3, 2, normal, 0, vecs, tmins, &odd));
		CHECK(bl[0] == 0 && bl[3] == 0 && bl[17] == 0);

		/* the point 2^30 texture units out is taken (its arithmetic is defined, nothing near), past it refused */
		edge[0][3] = 1073741824.0f;
		CHECK(rm_dlight_of(bl, 3, 2, normal, 0, (const float (*)[4])edge, tmins, &dl) && bl[0] == 0);
		edge[0][3] = -1073741824.0f;
		CHECK(rm_dlight_of(bl, 3, 2, normal, 0, (const float (*)[4])edge, tmins, &dl) && bl[0] == 0);
		edge[0][3] = 1073741952.0f;
		CHECK(!rm_dlight_of(bl, 3, 2, normal, 0, (const float (*)[4])edge, tmins, &dl));
		edge[0][3] = 0;
		edge[1][3] = -1073741952.0f;
		CHECK(!rm_dlight_of(bl, 3, 2, normal, 0, (const float (*)[4])edge, tmins, &dl));
		/* a negative intensity never reaches the plane */
		odd.intensity = -500;
		CHECK(rm_dlight_of(bl, 3, 2, normal, 0, vecs, tmins, &odd) && bl[0] == 0);
	}

	/* the two halves are N5's rm_lm_build, byte for byte, when no light is added between them */
	for (n = 0; n < 3000; n++)
	{
		unsigned char samples[4 * RM_LM_MAX_LUXELS * 3], a[RM_LM_MAX_LUXELS * 4], b[RM_LM_MAX_LUXELS * 4];
		unsigned char gam[256], ml[256];
		float rgb[4][3];
		const int smax = 1 + (int)(rng() % 17), tmax = 1 + (int)(rng() % 17), maps = (int)(rng() % 5);
		const float mod = (n % 3) ? rngf(0, 3) : 1.0f;
		const int with_ml = n & 1, no_samples = (n % 11) == 0;

		for (i = 0; i < (int)sizeof(samples); i++)
		{
			samples[i] = (unsigned char)rng();
		}

		for (i = 0; i < 256; i++)
		{
			gam[i] = (unsigned char)rng();
		}

		rm_minlight_table(rngf(0, 128), ml);

		for (i = 0; i < 12; i++)
		{
			rgb[i / 3][i % 3] = (rng() & 1) ? 1.0f : rngf(0, 2);
		}

		memset(a, 0x5a, sizeof(a));
		memset(b, 0x5a, sizeof(b));
		rm_lm_build(no_samples ? NULL : samples, smax, tmax, maps, (const float (*)[3])rgb, mod, with_ml ? ml : NULL,
				gam, a, smax * 4, bl);
		same = same && (rm_lm_styles(no_samples ? NULL : samples, smax, tmax, maps, (const float (*)[3])rgb, mod, ref) ==
				(no_samples ? 1 : 2));
		same = same && rm_lm_store(smax, tmax, with_ml ? ml : NULL, gam, b, smax * 4, ref);
		same = same && (memcmp(a, b, sizeof(a)) == 0);
	}

	CHECK(same);

	/* each half refuses as rm_lm_build does, writing nothing */
	{
		unsigned char out[16], idt[256];
		const float one[1][3] = { { 1, 1, 1 } };

		for (i = 0; i < 256; i++)
		{
			idt[i] = (unsigned char)i;
		}

		memset(out, 0x5a, sizeof(out));
		bl[0] = 7;
		CHECK(!rm_lm_styles(NULL, 0, 1, 1, one, 1, bl) && !rm_lm_styles(NULL, 1, 1, 5, one, 1, bl));
		CHECK(!rm_lm_styles(NULL, 1, 1, -1, one, 1, bl) && !rm_lm_styles(NULL, RM_LM_WIDTH, 1, 1, one, 1, bl) && bl[0] == 7);
		CHECK(!rm_lm_store(2, 1, NULL, idt, out, 7, bl) && !rm_lm_store(0, 1, NULL, idt, out, 16, bl));
		CHECK(!rm_lm_store(1, RM_LM_HEIGHT + 1, NULL, idt, out, 16, bl) && out[0] == 0x5a && out[15] == 0x5a);
		CHECK(rm_lm_store(2, 1, NULL, idt, out, 8, bl) && out[8] == 0x5a);
	}

	/* random surfaces and lights against gl1's lines, float for float */
	for (n = 0; n < 20000; n++)
	{
		const int smax = 1 + (int)(rng() % 17), tmax = 1 + (int)(rng() % 17);
		float nrm[3], v[2][4], d;
		short tm[2];

		for (i = 0; i < 3; i++)
		{
			nrm[i] = (n % 4) ? rngf(-1, 1) : (float)(i == (n / 4) % 3);
			dl.origin[i] = rngf(-2000, 2000);
			dl.color[i] = (n % 7) ? rngf(-1, 2) : 1.0f;
		}

		for (i = 0; i < 8; i++)
		{
			v[i / 4][i % 4] = (i % 4 == 3) ? rngf(-4000, 4000) : rngf(-2, 2);
		}

		tm[0] = (short)(rng() % 8192) - 4096;
		tm[1] = (short)(rng() % 8192) - 4096;
		d = rngf(-3000, 3000);
		dl.intensity = rngf(-50, 600);

		/* a light near the plane, so most of them reach it */
		if (n & 1)
		{
			const float off = DotProduct(dl.origin, nrm) - d;

			for (i = 0; i < 3; i++)
			{
				dl.origin[i] -= nrm[i] * off * rngf(0.8f, 1.0f);
			}
		}

		for (i = 0; i < smax * tmax * 3; i++)
		{
			bl[i] = ref[i] = rngf(0, 300);
		}

		ref_add_dynamic_light(ref, smax, tmax, nrm, d, (const float (*)[4])v, tm, &dl);
		same = same && rm_dlight_of(bl, smax, tmax, nrm, d, (const float (*)[4])v, tm, &dl);
		same = same && (memcmp(bl, ref, (size_t)smax * tmax * 3 * sizeof(float)) == 0);
	}

	CHECK(same);

	/* how many of those lit a luxel at all (the comparison must not be one of untouched blocks) */
	{
		int lit = 0;

		for (n = 0; n < 2000; n++)
		{
			float nrm[3] = { 0, 0, 1 }, v[2][4] = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 } };
			short tm[2] = { 0, 0 };

			dl.origin[0] = rngf(-50, 300);
			dl.origin[1] = rngf(-50, 300);
			dl.origin[2] = rngf(-200, 200);
			dl.intensity = rngf(100, 300);
			memset(bl, 0, sizeof(bl));
			memset(ref, 0, sizeof(ref));
			ref_add_dynamic_light(ref, 17, 17, nrm, 0, (const float (*)[4])v, tm, &dl);
			same = same && rm_dlight_of(bl, 17, 17, nrm, 0, (const float (*)[4])v, tm, &dl);
			same = same && (memcmp(bl, ref, sizeof(bl)) == 0);

			for (i = 0; i < 17 * 17 * 3; i++)
			{
				if (bl[i] != 0)
				{
					lit++;
					break;
				}
			}
		}

		CHECK(same && lit > 500);
	}
}

/*
 * The stencil test and update of one fragment (GL 1.4, section 4.1.5) as
 * the depth-stencil state RMB_DS_STENCIL_SHADOW asks Metal for it
 * (rmb_resource.m: compare equal, keep, keep, increment clamped, the
 * masks and the reference of rmb.h): 1 when the fragment is written,
 * *stored updated. The shim's test_stencil, for ref_metal's state.
 */
static int
shadow_stencil(unsigned *stored, int depth_pass)
{
	const unsigned a = RM_SHADOW_STENCIL_REF & RM_SHADOW_STENCIL_READ_MASK, b = *stored & RM_SHADOW_STENCIL_READ_MASK;
	unsigned v = *stored;

	if (a != b)
	{
		return 0;   /* GL_KEEP */
	}

	if (depth_pass)
	{
		v = v < 255 ? v + 1 : 255;   /* GL_INCR */
	}

	*stored = (*stored & ~RM_SHADOW_STENCIL_WRITE_MASK) | (v & RM_SHADOW_STENCIL_WRITE_MASK);
	return depth_pass;
}

/* the frame's lights as taken, a submodel's head node, the stencil shadow's state */
static void
test_dlight_bounds(void)
{
	float o[3] = { 0, 0, 0 }, c[3] = { 1, 1, 1 };
	unsigned st;
	int i;

	CHECK(rm_dlight_ok(o, c, 200));
	CHECK(rm_dlight_ok(o, c, -200) && rm_dlight_ok(o, c, 0));
	CHECK(rm_dlight_ok(o, c, RM_DLIGHT_MAX_INTENSITY) && rm_dlight_ok(o, c, -RM_DLIGHT_MAX_INTENSITY));
	CHECK(!rm_dlight_ok(o, c, nextafterf(RM_DLIGHT_MAX_INTENSITY, INFINITY)));
	CHECK(!rm_dlight_ok(o, c, NAN) && !rm_dlight_ok(o, c, INFINITY) && !rm_dlight_ok(o, c, -INFINITY));

	for (i = 0; i < 3; i++)
	{
		float oo[3] = { 1, 2, 3 }, cc[3] = { -1, 0.5f, 2 };

		CHECK(rm_dlight_ok(oo, cc, 300));
		oo[i] = RM_DLIGHT_MAX_COORD;
		cc[i] = -RM_DLIGHT_MAX_COLOR;
		CHECK(rm_dlight_ok(oo, cc, 300));
		oo[i] = nextafterf(RM_DLIGHT_MAX_COORD, INFINITY);
		CHECK(!rm_dlight_ok(oo, cc, 300));
		oo[i] = -RM_DLIGHT_MAX_COORD;
		cc[i] = nextafterf(RM_DLIGHT_MAX_COLOR, INFINITY);
		CHECK(!rm_dlight_ok(oo, cc, 300));
		cc[i] = 1;
		oo[i] = NAN;
		CHECK(!rm_dlight_ok(oo, cc, 300));
		oo[i] = 0;
		cc[i] = NAN;
		CHECK(!rm_dlight_ok(oo, cc, 300));
		cc[i] = -INFINITY;
		CHECK(!rm_dlight_ok(oo, cc, 300));
	}

	/* the flash blob's colour times 51 and the light's int intensity are defined at the bounds */
	CHECK((double)RM_DLIGHT_MAX_COLOR * 51 < 2147483647.0 && (double)RM_DLIGHT_MAX_INTENSITY + 64 < 2147483647.0);

	/* a submodel's head node: a node of the world, else nothing marked (a leaf is -1 - leaf) */
	CHECK(rm_bmodel_headnode(0, 5) == 0 && rm_bmodel_headnode(4, 5) == 4);
	CHECK(rm_bmodel_headnode(5, 5) == -1 && rm_bmodel_headnode(-1, 5) == -1 && rm_bmodel_headnode(-100, 5) == -1);
	CHECK(rm_bmodel_headnode(0, 0) == -1 && rm_bmodel_headnode(LONG_MIN, 5) == -1 && rm_bmodel_headnode(LONG_MAX, LONG_MAX) == -1);

	/*
	 * The shadow's stencil, the shim's test_models case for ref_metal's
	 * state: the buffer cleared to 1, the first of two shadow triangles
	 * over a pixel passes (1 & 2 equals 1 & 2) and raises it to 2, the
	 * second fails (2 & 2 is not 0): one darkening; a fragment behind
	 * the depth keeps the 1; without the clear to 1 (0) both pass.
	 */
	CHECK(RM_SHADOW_STENCIL_REF == 1 && RM_SHADOW_STENCIL_READ_MASK == 2 && RM_SHADOW_STENCIL_WRITE_MASK == 0xff);
	st = 1;
	CHECK(!shadow_stencil(&st, 0) && st == 1);
	CHECK(shadow_stencil(&st, 1) && st == 2);
	CHECK(!shadow_stencil(&st, 1) && st == 2);
	CHECK(!shadow_stencil(&st, 1) && st == 2);
	st = 0;
	CHECK(shadow_stencil(&st, 1) && st == 1 && shadow_stencil(&st, 1) && st == 2 && !shadow_stencil(&st, 1));
	/* the read mask sees only bit 1: 253 passes and is raised, 255 fails and is kept */
	st = 255;
	CHECK(!shadow_stencil(&st, 1) && st == 255);
	st = 253;
	CHECK(shadow_stencil(&st, 1) && st == 254);
	st = 252;
	CHECK(shadow_stencil(&st, 1) && st == 253);
}

/* the shim's mgl_triangle_indices (mgl_draw.c) for glDrawArrays, verbatim but for its types; base added */
static long
ref_triangle_indices(int strip, int count, int base, unsigned short *out, size_t cap)
{
	size_t n, i, k = 0;
	size_t pos[3];

	n = count > 2 ? (size_t)count - 2 : 0;

	if (n * 3 > cap)
	{
		return -1;
	}

	for (i = 0; i < n; i++)
	{
		if (!strip)
		{
			pos[0] = 0;
			pos[1] = i + 1;
			pos[2] = i + 2;
		}
		else
		{
			/* GL 1.4, section 2.6.1: every odd triangle swaps its first two vertices */
			pos[0] = (i & 1) ? i + 1 : i;
			pos[1] = (i & 1) ? i : i + 1;
			pos[2] = i + 2;
		}

		out[k++] = (unsigned short)(base + pos[0]);
		out[k++] = (unsigned short)(base + pos[1]);
		out[k++] = (unsigned short)(base + pos[2]);
	}

	return (long)k;
}

/* gl1_buffer.c, R_SetBufferIndices for a GL_TRIANGLE_FAN at vtx_ptr, verbatim but for its buffer */
static int
ref_fan_buffer_indices(int vtx_ptr, int vertices_num, unsigned short *idx)
{
	int i, idx_ptr = 0;

	for (i = 0; i < vertices_num - 2; i++)
	{
		idx[idx_ptr] = vtx_ptr;
		idx[idx_ptr + 1] = vtx_ptr + i + 1;
		idx[idx_ptr + 2] = vtx_ptr + i + 2;
		idx_ptr += 3;
	}

	return idx_ptr;
}

static void
test_prim_indices(void)
{
	unsigned short got[3 * 64 + 4], want[3 * 64 + 4];
	int n, strip, base, same = 1;

	for (strip = 0; strip < 2; strip++)
	{
		for (n = 3; n <= 64; n++)
		{
			for (base = 0; base < 40; base += 13)
			{
				long k = ref_triangle_indices(strip, n, base, want, 3 * 64);

				memset(got, 0xee, sizeof(got));
				same = same && (rm_prim_indices(strip, n, base, got, 3 * 64) == k);
				same = same && (memcmp(got, want, (size_t)k * sizeof(got[0])) == 0) && (got[k] == 0xeeee);

				if (!strip)
				{
					same = same && (ref_fan_buffer_indices(base, n, want) == k) &&
						(memcmp(got, want, (size_t)k * sizeof(got[0])) == 0);
				}
			}
		}
	}

	CHECK(same);

	/* the beam's strip: its first two triangles, the second swapped */
	CHECK(rm_prim_indices(1, RM_BEAM_VERTS, 0, got, 3 * 64) == 66);
	CHECK(got[0] == 0 && got[1] == 1 && got[2] == 2 && got[3] == 2 && got[4] == 1 && got[5] == 3 && got[65] == 23);

	/* the bounds: fewer than 3, a negative base, past 65536, one index short of the room; nothing written */
	memset(got, 0xee, sizeof(got));
	CHECK(!rm_prim_indices(0, 2, 0, got, 64) && !rm_prim_indices(1, 0, 0, got, 64) && !rm_prim_indices(0, -5, 0, got, 64));
	CHECK(!rm_prim_indices(0, 4, -1, got, 64) && !rm_prim_indices(0, 4, 65533, got, 64));
	CHECK(!rm_prim_indices(0, 18, 0, got, 47) && !rm_prim_indices(0, 18, 0, got, -1) && got[0] == 0xeeee);
	CHECK(rm_prim_indices(0, 18, 0, got, 48) == 48 && rm_prim_indices(0, 4, 65532, got, 64) == 6 && got[5] == 65535);
	CHECK(rm_prim_indices(0, 3, 0, got, 3) == 3 && !rm_prim_indices(0, 3, 0, got, 2));
}

/* gl1_main.c, R_DrawSpriteModel's frame, verbatim but on a copy */
static int
ref_sprite_frame(int frame, int numframes)
{
	/* A negative frame would leave a negative remainder and index
	   before frames[] and skins[]; use the first frame. */
	if (frame < 0)
	{
		frame = 0;
	}

	frame %= numframes;
	return frame;
}

/* gl1_main.c, R_DrawSpriteModel's corners, verbatim (VectorMA is shared.c's) */
static void
ref_sprite_quad(const float *origin, const dsprframe_t *frame, const float *up, const float *right, vec3_t *point)
{
	VectorMA( origin, -frame->origin_y, up, point[0] );
	VectorMA( point[0], -frame->origin_x, right, point[0] );

	VectorMA( origin, frame->height - frame->origin_y, up, point[1] );
	VectorMA( point[1], -frame->origin_x, right, point[1] );

	VectorMA( origin, frame->height - frame->origin_y, up, point[2] );
	VectorMA( point[2], frame->width - frame->origin_x, right, point[2] );

	VectorMA( origin, -frame->origin_y, up, point[3] );
	VectorMA( point[3], frame->width - frame->origin_x, right, point[3] );
}

static void
test_sprite(void)
{
	int f, nf, n, i, same = 1, blend, atest;
	float a;

	for (nf = 1; nf <= RM_MD2_MAX_SKINS; nf++)
	{
		for (f = -5; f < 100; f++)
		{
			same = same && (rm_sprite_frame(f, nf) == ref_sprite_frame(f, nf));
		}

		same = same && (rm_sprite_frame(INT_MAX, nf) == ref_sprite_frame(INT_MAX, nf));
		same = same && (rm_sprite_frame(INT_MIN, nf) == 0);
	}

	CHECK(same);
	CHECK(rm_sprite_frame(3, 0) == -1 && rm_sprite_frame(0, RM_MD2_MAX_SKINS + 1) == -1 && rm_sprite_frame(0, -1) == -1);
	CHECK(rm_sprite_frame(7, 4) == 3 && rm_sprite_frame(-7, 4) == 0);

	/* the alpha: 1 without RF_TRANSLUCENT, alpha-tested; the entity's with it, blended unless it is 1 */
	a = rm_sprite_alpha(0, 0.3f, &blend, &atest);
	CHECK(a == 1 && !blend && atest);
	a = rm_sprite_alpha(RF_TRANSLUCENT, 0.3f, &blend, &atest);
	CHECK(a == 0.3f && blend && !atest);
	a = rm_sprite_alpha(RF_TRANSLUCENT, 1, &blend, &atest);
	CHECK(a == 1 && !blend && atest);
	a = rm_sprite_alpha(RF_TRANSLUCENT | RF_FULLBRIGHT, 0, &blend, &atest);
	CHECK(a == 0 && blend && !atest);
	a = rm_sprite_alpha(RF_TRANSLUCENT, 1.5f, &blend, &atest);
	CHECK(a == 1.5f && blend && !atest);
	a = rm_sprite_alpha(RF_TRANSLUCENT, NAN, &blend, &atest);
	CHECK(a != a && blend && !atest);

	/* a 32 by 16 frame with its origin at (16, 8), at (100, 0, 0) facing -x: up z, right y */
	{
		const float origin[3] = { 100, 0, 0 }, up[3] = { 0, 0, 1 }, right[3] = { 0, 1, 0 };
		float p[4][3];

		CHECK(rm_sprite_quad(origin, 32, 16, 16, 8, up, right, p));
		CHECK(p[0][0] == 100 && p[0][1] == -16 && p[0][2] == -8 && p[1][1] == -16 && p[1][2] == 8);
		CHECK(p[2][1] == 16 && p[2][2] == 8 && p[3][1] == 16 && p[3][2] == -8);
	}

	/* random frames and views against gl1's lines */
	for (n = 0; n < 20000; n++)
	{
		float origin[3], up[3], right[3], p[4][3];
		vec3_t want[4];
		dsprframe_t fr;

		for (i = 0; i < 3; i++)
		{
			origin[i] = rngf(-4096, 4096);
			up[i] = rngf(-1, 1);
			right[i] = rngf(-1, 1);
		}

		memset(&fr, 0, sizeof(fr));
		fr.width = (int)(rng() % 512);
		fr.height = (int)(rng() % 512);
		fr.origin_x = (int)(rng() % 600) - 50;
		fr.origin_y = (int)(rng() % 600) - 50;
		ref_sprite_quad(origin, &fr, up, right, want);
		same = same && rm_sprite_quad(origin, fr.width, fr.height, fr.origin_x, fr.origin_y, up, right, p);
		same = same && (memcmp(p, want, sizeof(p)) == 0);
	}

	CHECK(same);

	/* a file's extreme ints: no overflow (gl1's int differences would), finite corners */
	{
		const float origin[3] = { 0, 0, 0 }, up[3] = { 0, 0, 1 }, right[3] = { 0, 1, 0 };
		float p[4][3];

		CHECK(rm_sprite_quad(origin, INT_MAX, INT_MAX, INT_MIN, INT_MIN, up, right, p));
		CHECK(p[2][1] == 4294967295.0f && p[2][2] == 4294967295.0f && p[0][1] == 2147483648.0f);
		CHECK(rm_sprite_quad(origin, INT_MIN, INT_MIN, INT_MAX, INT_MAX, up, right, p));
		CHECK(p[2][1] == -4294967295.0f && p[0][2] == -2147483647.0f);
	}

	/* corners that are not finite are refused */
	{
		const float origin[3] = { 0, 0, 0 }, up[3] = { 3e38f, 0, 0 }, right[3] = { 0, 1, 0 };
		float p[4][3];

		const float upz[3] = { 0, 0, 3e38f }, upy[3] = { 0, 3e38f, 0 };

		CHECK(!rm_sprite_quad(origin, 0, 2, 0, 0, up, right, p));
		CHECK(rm_sprite_quad(origin, 0, 1, 0, 0, up, right, p) && p[1][0] == 3e38f);
		CHECK(!rm_sprite_quad(origin, 0, 2, 0, 0, upz, right, p) && !rm_sprite_quad(origin, 0, 2, 0, 0, upy, right, p));
		CHECK(!rm_sprite_quad(origin, 0, 0, 0, -2, upz, right, p));
	}
}

/* gl1_main.c */
#define NUM_BEAM_SEGS 6

/* gl1_main.c, R_DrawBeam's vertices, verbatim but for the entity's fields passed in */
static int
ref_beam(const float *eorigin, const float *eoldorigin, int eframe, float *vtx)
{
	int i;
	vec3_t perpvec;
	vec3_t direction, normalized_direction;
	vec3_t start_points[NUM_BEAM_SEGS], end_points[NUM_BEAM_SEGS];
	vec3_t oldorigin, origin;
	unsigned int index_vtx = 0;
	unsigned int pointb;

	oldorigin[0] = eoldorigin[0];
	oldorigin[1] = eoldorigin[1];
	oldorigin[2] = eoldorigin[2];

	origin[0] = eorigin[0];
	origin[1] = eorigin[1];
	origin[2] = eorigin[2];

	normalized_direction[0] = direction[0] = oldorigin[0] - origin[0];
	normalized_direction[1] = direction[1] = oldorigin[1] - origin[1];
	normalized_direction[2] = direction[2] = oldorigin[2] - origin[2];

	if (VectorNormalize(normalized_direction) == 0)
	{
		return 0;
	}

	PerpendicularVector(perpvec, normalized_direction);
	VectorScale(perpvec, eframe / 2, perpvec);

	for (i = 0; i < 6; i++)
	{
		RotatePointAroundVector(start_points[i], normalized_direction, perpvec,
				(360.0 / NUM_BEAM_SEGS) * i);
		VectorAdd(start_points[i], origin, start_points[i]);
		VectorAdd(start_points[i], direction, end_points[i]);
	}

	for ( i = 0; i < NUM_BEAM_SEGS; i++ )
	{
		vtx[index_vtx++] = start_points [ i ][ 0 ];
		vtx[index_vtx++] = start_points [ i ][ 1 ];
		vtx[index_vtx++] = start_points [ i ][ 2 ];

		vtx[index_vtx++] = end_points [ i ][ 0 ];
		vtx[index_vtx++] = end_points [ i ][ 1 ];
		vtx[index_vtx++] = end_points [ i ][ 2 ];

		pointb = ( i + 1 ) % NUM_BEAM_SEGS;
		vtx[index_vtx++] = start_points [ pointb ][ 0 ];
		vtx[index_vtx++] = start_points [ pointb ][ 1 ];
		vtx[index_vtx++] = start_points [ pointb ][ 2 ];

		vtx[index_vtx++] = end_points [ pointb ][ 0 ];
		vtx[index_vtx++] = end_points [ pointb ][ 1 ];
		vtx[index_vtx++] = end_points [ pointb ][ 2 ];
	}

	return 1;
}

/* gl1_main.c, R_DrawBeam's colour, verbatim, and the shim's glColor4ub (b / 255.0f) */
static void
ref_beam_color(const unsigned *d_8to24table, const unsigned char *gammatable, int skinnum, float alpha, float *out)
{
	int clr[4];
	unsigned char ub[4];

	clr[0] = (LittleLong(d_8to24table[skinnum & 0xFF])) & 0xFF;
	clr[1] = (LittleLong(d_8to24table[skinnum & 0xFF]) >> 8) & 0xFF;
	clr[2] = (LittleLong(d_8to24table[skinnum & 0xFF]) >> 16) & 0xFF;
	clr[3] = alpha * 255;

	/* glColor4ub(gammatable[clr[0]], gammatable[clr[1]], gammatable[clr[2]], clr[3]) */
	ub[0] = gammatable[clr[0]];
	ub[1] = gammatable[clr[1]];
	ub[2] = gammatable[clr[2]];
	ub[3] = clr[3];

	out[0] = ub[0] / 255.0f;
	out[1] = ub[1] / 255.0f;
	out[2] = ub[2] / 255.0f;
	out[3] = ub[3] / 255.0f;
}

static void
test_beam(void)
{
	unsigned pal[256];
	unsigned char gam[256];
	float got[RM_BEAM_VERTS][3], want[RM_BEAM_VERTS * 3], c[4], w[4];
	int n, i, same = 1, drawn = 0;

	CHECK(RM_BEAM_SEGS == NUM_BEAM_SEGS);
	Swap_Init();

	for (i = 0; i < 256; i++)
	{
		pal[i] = (unsigned)rng();
		gam[i] = (unsigned char)rng();
	}

	/* a beam along x of width 8: its first start point is 4 off the axis, its end 100 further */
	{
		const float o[3] = { 0, 0, 0 }, oo[3] = { 100, 0, 0 };

		CHECK(rm_beam_points(o, oo, 8, got) == 1);
		CHECK(fabsf(sqrtf(got[0][1] * got[0][1] + got[0][2] * got[0][2]) - 4) < 1e-5f && got[0][0] == 0);
		CHECK(got[1][0] == 100 && got[1][1] == got[0][1] && got[1][2] == got[0][2]);
		CHECK(memcmp(got[2], got[4], sizeof(got[2])) == 0 && memcmp(got[22], got[0], sizeof(got[0])) == 0);
		/* no length: gl1 draws nothing */
		CHECK(rm_beam_points(o, o, 8, got) == 0);
	}

	for (n = 0; n < 20000; n++)
	{
		float o[3], oo[3];
		int frame = (n % 50 == 0) ? -(int)(rng() % 9) : (int)(rng() % 41);

		for (i = 0; i < 3; i++)
		{
			o[i] = rngf(-4096, 4096);
			oo[i] = (n % 5 == 0 && i != (n / 5) % 3) ? o[i] : rngf(-4096, 4096);
		}

		if (ref_beam(o, oo, frame, want))
		{
			drawn++;
			same = same && (rm_beam_points(o, oo, frame, got) == 1) && (memcmp(got, want, sizeof(got)) == 0);
		}
		else
		{
			same = same && (rm_beam_points(o, oo, frame, got) == 0);
		}
	}

	CHECK(same && drawn > 19000);

	/* a direction gl1's normalization turns into NaNs: not drawn */
	{
		const float o[3] = { 3e38f, 0, 0 }, oo[3] = { -3e38f, 0, 0 };

		CHECK(rm_beam_points(o, oo, 8, got) == -1);
	}

	/* the colour: the palette through the gamma table, the alpha byte as glColor4ub keeps it */
	for (n = 0; n < 4000; n++)
	{
		const int skin = (n % 3) ? (int)(rng() % 256) : (int)(rng() % 65536) - 32768;
		const float alpha = (n % 4) ? rngf(0, 1) : rngf(-3, 3);

		rm_beam_color(pal, gam, skin, alpha, c);
		ref_beam_color(pal, gam, skin, alpha, w);
		same = same && (memcmp(c, w, sizeof(c)) == 0);
	}

	CHECK(same);
	rm_beam_color(pal, gam, 0, 0.30f, c);
	CHECK(c[3] == 76 / 255.0f && c[0] == gam[pal[0] & 0xff] / 255.0f && c[2] == gam[(pal[0] >> 16) & 0xff] / 255.0f);
	rm_beam_color(pal, gam, 0, 1.5f, c);
	CHECK(c[3] == 126 / 255.0f);
	/* where gl1's conversion is undefined: the int saturated, its low byte kept */
	rm_beam_color(pal, gam, 0, INFINITY, c);
	CHECK(c[3] == 1.0f);
	rm_beam_color(pal, gam, 0, -INFINITY, c);
	CHECK(c[3] == 0.0f);
	rm_beam_color(pal, gam, 0, 1e30f, c);
	CHECK(c[3] == 1.0f);
	rm_beam_color(pal, gam, 0, 1e7f, c);
	CHECK(c[3] == 1.0f);
	rm_beam_color(pal, gam, 0, -1e7f, c);
	CHECK(c[3] == 0.0f);
}

static void
test_null_model(void)
{
	/* gl1_main.c, R_DrawNullModel's two arrays, verbatim (GLfloat is float) */
	float vtxA[] = {
		0, 0, -16,
		16 * cos( 0 * M_PI / 2 ), 16 * sin( 0 * M_PI / 2 ), 0,
		16 * cos( 1 * M_PI / 2 ), 16 * sin( 1 * M_PI / 2 ), 0,
		16 * cos( 2 * M_PI / 2 ), 16 * sin( 2 * M_PI / 2 ), 0,
		16 * cos( 3 * M_PI / 2 ), 16 * sin( 3 * M_PI / 2 ), 0,
		16 * cos( 4 * M_PI / 2 ), 16 * sin( 4 * M_PI / 2 ), 0
	};
	float vtxB[] = {
		0, 0, 16,
		16 * cos( 4 * M_PI / 2 ), 16 * sin( 4 * M_PI / 2 ), 0,
		16 * cos( 3 * M_PI / 2 ), 16 * sin( 3 * M_PI / 2 ), 0,
		16 * cos( 2 * M_PI / 2 ), 16 * sin( 2 * M_PI / 2 ), 0,
		16 * cos( 1 * M_PI / 2 ), 16 * sin( 1 * M_PI / 2 ), 0,
		16 * cos( 0 * M_PI / 2 ), 16 * sin( 0 * M_PI / 2 ), 0
	};
	float got[RM_NULL_VERTS][3];

	CHECK(sizeof(vtxA) + sizeof(vtxB) == sizeof(got));
	memset(got, 0, sizeof(got));
	rm_null_model(got);
	CHECK(memcmp(got[0], vtxA, sizeof(vtxA)) == 0 && memcmp(got[6], vtxB, sizeof(vtxB)) == 0);
	CHECK(got[0][2] == -16 && got[1][0] == 16 && got[2][1] == 16 && got[6][2] == 16 && got[11][0] == 16);
}

/* gl1_mesh.c, R_DrawAliasModel's shadevector, verbatim */
static void
ref_shadevector(float yaw, vec3_t shadevector)
{
	float an;

	an = yaw / 180 * M_PI;
	shadevector[0] = cos(-an);
	shadevector[1] = sin(-an);
	shadevector[2] = 1;
	VectorNormalize(shadevector);
}

/* gl1_mesh.c, R_DrawAliasShadow for one vertex of s_lerped, verbatim */
static void
ref_shadow_point(const float *lerped, const vec3_t shadevector, const float *origin, const float *lightspot,
		vec3_t out)
{
	vec3_t point;
	float height = 0, lheight;

	lheight = origin[2] - lightspot[2];
	height = -lheight + 0.1f;

	memcpy(point, lerped, sizeof(point));

	point[0] -= shadevector[0] * (point[2] + lheight);
	point[1] -= shadevector[1] * (point[2] + lheight);
	point[2] = height;

	VectorCopy(point, out);
}

static void
test_shadow(void)
{
	float sv[3], want_sv[3], w[16], got[16], want[16], r[16];
	int n, i, same = 1;
	const float world_angles[3] = { 10, 20, 0 }, world_org[3] = { 1, 2, 3 };

	/* gl1's flags: r_shadows, never for translucent, the view weapon or RF_NOSHADOW */
	CHECK(RM_RF_NOSHADOW == RF_NOSHADOW && RM_RF_BEAM == RF_BEAM);
	CHECK(rm_shadow_drawn(0, 1) && rm_shadow_drawn(RF_FULLBRIGHT, 1) && rm_shadow_drawn(RF_GLOW | RF_SHELL_RED, 2));
	CHECK(!rm_shadow_drawn(0, 0) && rm_shadow_drawn(0, NAN) && !rm_shadow_drawn(RF_TRANSLUCENT, 1));
	CHECK(!rm_shadow_drawn(RF_WEAPONMODEL, 1) && !rm_shadow_drawn(RF_NOSHADOW, 1) && rm_shadow_drawn(RF_DEPTHHACK, 1));

	/* a height where gl1's float 0.1f and a double 0.1 put the plane on two floats */
	{
		const float p[3] = { 0, 0, 0 }, v[3] = { 0, 0, 1 }, l = 0x1.ffc006p-3f;
		float out[3];

		rm_shadow_point(p, v, l, out);
		CHECK(out[2] == -l + 0.1f && out[2] != (float)(-(double)l + 0.1));
	}

	/* yaw 0: the shadow leans along +x, normalized (1, 0, 1) */
	rm_shadevector(0, sv);
	CHECK(fabsf(sv[0] - 0.70710678f) < 1e-6f && sv[1] == 0 && fabsf(sv[2] - 0.70710678f) < 1e-6f);

	/* a point 10 above the entity's origin, 30 above the spot: pushed 40 along the vector, put on the plane */
	{
		const float p[3] = { 5, 6, 10 }, v[3] = { 0.5f, -0.25f, 1 };
		float out[3];

		rm_shadow_point(p, v, 30, out);
		CHECK(out[0] == 5 - 0.5f * 40 && out[1] == 6 + 0.25f * 40 && out[2] == -30 + 0.1f);
	}

	for (n = 0; n < 20000; n++)
	{
		const float yaw = (n < 8) ? 45.0f * n : rngf(-720, 720);
		float p[3], o[3], spot[3], out[3];
		vec3_t want_p;

		rm_shadevector(yaw, sv);
		ref_shadevector(yaw, want_sv);
		same = same && (memcmp(sv, want_sv, sizeof(sv)) == 0);

		for (i = 0; i < 3; i++)
		{
			p[i] = rngf(-300, 300);
			o[i] = rngf(-4096, 4096);
			spot[i] = rngf(-4096, 4096);
		}

		ref_shadow_point(p, want_sv, o, spot, want_p);
		rm_shadow_point(p, sv, o[2] - spot[2], out);
		same = same && (memcmp(out, want_p, sizeof(out)) == 0);
	}

	CHECK(same);

	/* the matrix: the shim's glTranslatef(origin), then glRotatef(yaw, 0, 0, 1), on the world's */
	rm_view_matrix(w, world_angles, world_org);

	for (n = 0; n < 2000; n++)
	{
		float o[3];
		const float yaw = (n < 4) ? 90.0f * n : rngf(-360, 360);

		for (i = 0; i < 3; i++)
		{
			o[i] = rngf(-4000, 4000);
		}

		memcpy(want, w, sizeof(want));
		mtl_mat_translate(r, o[0], o[1], o[2]);
		mtl_mat_mul(want, want, r);
		mtl_mat_rotate(r, yaw, 0, 0, 1);
		mtl_mat_mul(want, want, r);
		rm_shadow_matrix(got, w, o, yaw);
		same = same && (memcmp(got, want, sizeof(got)) == 0);
	}

	CHECK(same);
}

/* gl1_light.c, R_RenderDlight, verbatim but for GLBUFFER_VERTEX and GLBUFFER_COLOR writing into arrays */
static void
ref_render_dlight(const dlight_t *light, const vec3_t vpn, const vec3_t vright, const vec3_t vup,
		const unsigned char *gammatable, float (*vout)[3], unsigned char (*cout)[4])
{
	const float rad = light->intensity * 0.35;
	int i, j, clr[3], nv = 0;
	float vtx[3], a;

	for ( i = 0; i < 3; i++ )
	{
		vtx [ i ] = light->origin [ i ] - vpn [ i ] * rad;
		clr [ i ] = light->color [ i ] * 51;	// 255 * 0.2 = 51
	}

	/* GLBUFFER_VERTEX( vtx[0], vtx[1], vtx[2] ), GLBUFFER_COLOR( clr[0], clr[1], clr[2], 255 ) */
	memcpy(vout[nv], vtx, sizeof(vtx));
	cout[nv][0] = gammatable[clr[0]];
	cout[nv][1] = gammatable[clr[1]];
	cout[nv][2] = gammatable[clr[2]];
	cout[nv][3] = 255;
	nv++;

	for ( i = 16; i >= 0; i-- )
	{
		a = i / 16.0 * M_PI * 2;

		for ( j = 0; j < 3; j++ )
		{
			vtx[ j ] = light->origin [ j ] + vright [ j ] * cos( a ) * rad
				+ vup [ j ] * sin( a ) * rad;
		}

		/* GLBUFFER_VERTEX( vtx[0], vtx[1], vtx[2] ), GLBUFFER_COLOR( 0, 0, 0, 255 ) */
		memcpy(vout[nv], vtx, sizeof(vtx));
		cout[nv][0] = gammatable[0];
		cout[nv][1] = gammatable[0];
		cout[nv][2] = gammatable[0];
		cout[nv][3] = 255;
		nv++;
	}
}

static void
test_flash_blob(void)
{
	rm_avert_t got[RM_FLASH_VERTS];
	float wv[RM_FLASH_VERTS][3];
	unsigned char wc[RM_FLASH_VERTS][4], gam[256];
	int n, i, k, same = 1;

	for (i = 0; i < 256; i++)
	{
		gam[i] = (unsigned char)(255 - i);
	}

	for (n = 0; n < 5000; n++)
	{
		dlight_t l;
		vec3_t pn, rt, up;

		for (i = 0; i < 3; i++)
		{
			l.origin[i] = rngf(-4096, 4096);
			l.color[i] = (n % 3) ? rngf(0, 1) : rngf(0, 5);
			pn[i] = rngf(-1, 1);
			rt[i] = rngf(-1, 1);
			up[i] = rngf(-1, 1);
		}

		l.intensity = rngf(0, 600);
		ref_render_dlight(&l, pn, rt, up, gam, wv, wc);
		rm_flash_blob(l.origin, l.intensity, l.color, pn, rt, up, gam, got);

		for (k = 0; k < RM_FLASH_VERTS; k++)
		{
			same = same && (memcmp(got[k].pos, wv[k], sizeof(wv[k])) == 0) && (got[k].pos[3] == 1);

			for (i = 0; i < 4; i++)
			{
				same = same && (got[k].color[i] == wc[k][i] / 255.0f);
			}

			same = same && (got[k].uv[0] == 0) && (got[k].uv[1] == 0);
		}
	}

	CHECK(same);

	/* a light of colour (1, 0.5, 0) at the origin of radius 100: the centre 35 towards the viewer, 51 25 0 through the table */
	{
		const float o[3] = { 0, 0, 0 }, c[3] = { 1, 0.5f, 0 }, pn[3] = { 1, 0, 0 }, rt[3] = { 0, 1, 0 }, up[3] = { 0, 0, 1 };

		rm_flash_blob(o, 100, c, pn, rt, up, gam, got);
		CHECK(got[0].pos[0] == -35 && got[0].pos[1] == 0 && got[0].color[0] == 204 / 255.0f && got[0].color[1] == 230 / 255.0f);
		CHECK(got[0].color[2] == 1.0f && got[0].color[3] == 1.0f && got[1].color[0] == 1.0f);
		/* the rim from 2 pi down to 0: the first and the last rim vertex are the same point on +y */
		CHECK(fabsf(got[1].pos[1] - 35) < 1e-4f && fabsf(got[17].pos[1] - 35) < 1e-4f && fabsf(got[9].pos[1] + 35) < 1e-4f);
	}

	/* a dark light (-1) and a colour past 5: the gamma index held to the table where gl1 reads outside it */
	{
		const float o[3] = { 0, 0, 0 }, c[3] = { -1, 6, 1e6f }, pn[3] = { 1, 0, 0 }, rt[3] = { 0, 1, 0 }, up[3] = { 0, 0, 1 };

		rm_flash_blob(o, 100, c, pn, rt, up, gam, got);
		CHECK(got[0].color[0] == gam[0] / 255.0f && got[0].color[1] == gam[255] / 255.0f && got[0].color[2] == gam[255] / 255.0f);
	}
}


/* ---- particles (N9) ---- */

/*
 * The counts and bounds of a view's particles: the ring is the client's
 * (MAX_PARTICLES), what lies past it is counted and not drawn, a negative
 * count is none; a particle is drawn only with a finite origin within its
 * bound, a colour that is a palette index and an alpha of 0 to 1.
 */
static void
test_particle_bounds(void)
{
	const float o[3] = { 10, -20, 30 };
	float b[3], big[3];
	long refused = -1;
	int i;

	CHECK(RM_MAX_PARTICLES == MAX_PARTICLES);
	CHECK(rm_particle_count(0, &refused) == 0 && refused == 0);
	CHECK(rm_particle_count(1, &refused) == 1 && refused == 0);
	CHECK(rm_particle_count(RM_MAX_PARTICLES, &refused) == RM_MAX_PARTICLES && refused == 0);
	CHECK(rm_particle_count(RM_MAX_PARTICLES + 1, &refused) == RM_MAX_PARTICLES && refused == 1);
	CHECK(rm_particle_count(100000, &refused) == RM_MAX_PARTICLES && refused == 100000 - RM_MAX_PARTICLES);
	CHECK(rm_particle_count(-1, &refused) == 0 && refused == 0);
	CHECK(rm_particle_count(LONG_MIN, &refused) == 0 && refused == 0);
	CHECK(rm_particle_count(LONG_MAX, &refused) == RM_MAX_PARTICLES && refused == LONG_MAX - RM_MAX_PARTICLES);

	CHECK(rm_particle_ok(o, 0, 0.0f) && rm_particle_ok(o, 255, 1.0f) && rm_particle_ok(o, 7, 0.5f));
	CHECK(!rm_particle_ok(o, -1, 0.5f) && !rm_particle_ok(o, 256, 0.5f) && !rm_particle_ok(o, INT_MIN, 0.5f));
	CHECK(!rm_particle_ok(o, INT_MAX, 0.5f));
	CHECK(!rm_particle_ok(o, 7, -0.001f) && !rm_particle_ok(o, 7, 1.001f) && !rm_particle_ok(o, 7, NAN));
	CHECK(!rm_particle_ok(o, 7, INFINITY) && !rm_particle_ok(o, 7, -INFINITY));

	/* each axis: the bound itself is inside, past it, NaN and infinity are not */
	for (i = 0; i < 3; i++)
	{
		memcpy(b, o, sizeof(b));
		b[i] = RM_PARTICLE_BOUND;
		CHECK(rm_particle_ok(b, 1, 1));
		b[i] = -RM_PARTICLE_BOUND;
		CHECK(rm_particle_ok(b, 1, 1));
		b[i] = nextafterf(RM_PARTICLE_BOUND, INFINITY);
		CHECK(!rm_particle_ok(b, 1, 1));
		b[i] = -nextafterf(RM_PARTICLE_BOUND, INFINITY);
		CHECK(!rm_particle_ok(b, 1, 1));
		b[i] = NAN;
		CHECK(!rm_particle_ok(b, 1, 1));
		b[i] = INFINITY;
		CHECK(!rm_particle_ok(b, 1, 1));
		b[i] = -INFINITY;
		CHECK(!rm_particle_ok(b, 1, 1));
		b[i] = FLT_MAX;
		CHECK(!rm_particle_ok(b, 1, 1));
	}

	big[0] = big[1] = big[2] = 0;
	CHECK(rm_particle_ok(big, 0, 0));
}

/* gl1's colour bytes of R_DrawParticles: the palette entry's bytes through the gamma table, alpha * 255 as a byte */
static void
test_particle_color(void)
{
	unsigned pal[256];
	unsigned char gam[256];
	float c[4];
	int i, same = 1;

	for (i = 0; i < 256; i++)
	{
		pal[i] = (unsigned)(i * 3 + 1) | ((unsigned)(255 - i) << 8) | ((unsigned)((i * 7) & 0xff) << 16) | 0xff000000u;
		gam[i] = (unsigned char)((i * 5 + 11) & 0xff);
	}

	rm_particle_color(pal, gam, 0, 1.0f, c);
	CHECK(c[0] == gam[1] / 255.0f && c[1] == gam[255] / 255.0f && c[2] == gam[0] / 255.0f && c[3] == 1.0f);
	rm_particle_color(pal, gam, 200, 0.5f, c);
	/* 0.5 * 255 = 127.5: the byte is 127 */
	CHECK(c[0] == gam[601 & 0xff] / 255.0f && c[1] == gam[55] / 255.0f && c[2] == gam[(1400) & 0xff] / 255.0f);
	CHECK(c[3] == 127 / 255.0f);
	rm_particle_color(pal, gam, 3, 0.0f, c);
	CHECK(c[3] == 0.0f);

	for (i = 0; i < 4000; i++)
	{
		const int col = (int)(rng() & 0xff);
		const float a = rngf(0, 1);
		const unsigned p = pal[col];
		const unsigned char want[4] = { gam[p & 0xff], gam[(p >> 8) & 0xff], gam[(p >> 16) & 0xff], (unsigned char)(a * 255) };

		rm_particle_color(pal, gam, col, a, c);
		same = same && c[0] == want[0] / 255.0f && c[1] == want[1] / 255.0f && c[2] == want[2] / 255.0f &&
			c[3] == want[3] / 255.0f;
	}

	CHECK(same);
}

static rm_points_t
test_points(float size, float min, float max, float a, float b, float c, int smooth)
{
	rm_points_t p;

	rm_point_state_init(&p);
	p.size = size;
	p.min = min;
	p.max = max;
	p.att[0] = a;
	p.att[1] = b;
	p.att[2] = c;
	p.smooth = smooth;
	p.vx = 0;
	p.vy = 0;
	p.vw = 640;
	p.vh = 480;
	return p;
}

/*
 * The width of a point (gl1_particle_size scaled by the height over 480,
 * gl1's attenuation 0.01, 0, 0.01, the range 2 to 40): at two eye
 * distances, clamped to the minimum and to the maximum, square (a whole
 * width) and smooth; each case against the shim's mgl_point_size.
 */
static void
test_point_size(void)
{
	float lim[3], eye[4] = { 0, 0, 0, 1 };
	/* gl1's defaults: size 40 at a height of 480, min 2, max 40, 0.01 0 0.01 */
	rm_points_t sq = test_points(40, 2, 40, 0.01f, 0.0f, 0.01f, 0);
	rm_points_t sm = test_points(40, 2, 40, 0.01f, 0.0f, 0.01f, 1);
	int i, same = 1;

	rm_host_shim_point_limits(lim);
	CHECK(lim[0] == RM_POINT_ALIASED_MAX && lim[1] == RM_POINT_SMOOTH_MIN && lim[2] == RM_POINT_SMOOTH_MAX);

	/* distance 100: 40 / sqrt(0.01 + 0.01 * 10000) = 40 / 10.0005 = 3.9998: a whole 4, smooth 3.9998 */
	eye[2] = -100;
	CHECK(rm_point_size(&sq, eye) == 4.0f);
	CHECK(fabsf(rm_point_size(&sm, eye) - 3.9998f) < 1e-3f);
	CHECK(rm_point_size(&sq, eye) == rm_host_shim_point_size(&sq, eye));
	CHECK(rm_point_size(&sm, eye) == rm_host_shim_point_size(&sm, eye));

	/* distance 10: 40 / sqrt(0.01 + 1) = 39.80: 40 whole; smooth 39.80 */
	eye[2] = -10;
	CHECK(rm_point_size(&sq, eye) == 40.0f);
	CHECK(fabsf(rm_point_size(&sm, eye) - 39.8f) < 0.01f);

	/* distance 1000: 40 / sqrt(0.01 + 10^4) = 0.4: clamped up to the minimum 2 */
	eye[2] = -1000;
	CHECK(rm_point_size(&sq, eye) == 2.0f && rm_point_size(&sm, eye) == 2.0f);

	/* distance 0: 40 / sqrt(0.01) = 400: clamped down to the maximum 40 */
	eye[2] = 0;
	CHECK(rm_point_size(&sq, eye) == 40.0f && rm_point_size(&sm, eye) == 40.0f);

	/* the distance, not the depth: a point 60 right and 80 up is at 100 */
	eye[0] = 60;
	eye[1] = 80;
	CHECK(rm_point_size(&sq, eye) == 4.0f);

	/* GL's own limits after the range: a smooth point at least 0.1 and at most 256; an aliased one at least 1, at most 8191 */
	{
		rm_points_t a = test_points(1000, 0, 100000, 1, 0, 0, 1), b = test_points(0.01f, 0, 1, 1, 0, 0, 1);
		rm_points_t c = test_points(100000, 0, 100000, 1, 0, 0, 0), d = test_points(0.2f, 0, 1, 1, 0, 0, 0);

		eye[0] = eye[1] = eye[2] = 0;
		CHECK(rm_point_size(&a, eye) == 256.0f && rm_point_size(&b, eye) == 0.1f);
		CHECK(rm_point_size(&c, eye) == 8191.0f && rm_point_size(&d, eye) == 1.0f);
	}

	/* a denominator that is not above 0 leaves the size; NaN and infinity are 1 */
	{
		rm_points_t z = test_points(7.4f, 0, 100, 0, 0, 0, 0), n = test_points(NAN, 0, 100, 1, 0, 0, 0);
		rm_points_t i1 = test_points(5, 0, INFINITY, 0, 0, 0, 1);
		rm_points_t i2 = test_points(INFINITY, 0, INFINITY, 1, 0, 0, 0), m = test_points(5, NAN, NAN, 1, 0, 0, 1);

		CHECK(rm_point_size(&z, eye) == 7.0f);
		CHECK(rm_point_size(&n, eye) == 1.0f);
		CHECK(rm_point_size(&i1, eye) == 5.0f);
		CHECK(rm_point_size(&i2, eye) == 1.0f);
		CHECK(rm_point_size(&m, eye) == rm_host_shim_point_size(&m, eye));

		/* a smooth point too: 1 (inside 0.1 to 256), not GL's smooth minimum */
		n.smooth = 1;
		CHECK(rm_point_size(&n, eye) == 1.0f && rm_host_shim_point_size(&n, eye) == 1.0f);
	}

	/* random states and eyes against the shim's function */
	for (i = 0; i < 20000; i++)
	{
		rm_points_t p = test_points(rngf(0.01f, 500), rngf(0, 20), rngf(0, 300), rngf(0, 2), rngf(-0.01f, 0.1f),
				rngf(-0.001f, 0.1f), (int)(rng() & 1));
		float e[4];

		e[0] = rngf(-3000, 3000);
		e[1] = rngf(-3000, 3000);
		e[2] = rngf(-3000, 3000);
		e[3] = 1;

		if ((i % 7) == 0)
		{
			p.min = rngf(0, 600);
			p.max = rngf(0, 600);
		}

		same = same && rm_point_size(&p, e) == rm_host_shim_point_size(&p, e);
	}

	CHECK(same);
}

/* glPointSize and glPointParameterf: what they refuse, as the shim does */
static void
test_point_state(void)
{
	rm_points_t s;
	float sh[3];
	const float vals[] = { 1, 0, -1, 0.5f, 40, NAN, INFINITY, -INFINITY, 1e30f, -0.0f };
	int i, j, k, same = 1;

	rm_point_state_init(&s);
	CHECK(s.size == 1.0f && s.min == 0.0f && s.max == RM_POINT_ALIASED_MAX && s.att[0] == 1.0f && s.att[1] == 0.0f && s.att[2] == 0.0f);
	CHECK(s.smooth == 0);

	for (i = 0; i < (int)(sizeof(vals) / sizeof(vals[0])); i++)
	{
		for (j = 0; j < (int)(sizeof(vals) / sizeof(vals[0])); j++)
		{
			for (k = 0; k < (int)(sizeof(vals) / sizeof(vals[0])); k++)
			{
				rm_point_state_init(&s);
				rm_point_set_size(&s, vals[i]);
				rm_point_set_range(&s, vals[j], vals[k]);
				rm_host_shim_point_set(vals[i], vals[j], vals[k], sh);
				same = same && memcmp(&s.size, &sh[0], sizeof(float)) == 0 &&
					memcmp(&s.min, &sh[1], sizeof(float)) == 0 && memcmp(&s.max, &sh[2], sizeof(float)) == 0;
			}
		}
	}

	CHECK(same);

	/* a refused size keeps the one before it */
	rm_point_state_init(&s);
	rm_point_set_size(&s, 12);
	rm_point_set_size(&s, 0);
	rm_point_set_size(&s, -3);
	rm_point_set_size(&s, NAN);
	CHECK(s.size == 12.0f);
	rm_point_set_range(&s, -1, -1);
	CHECK(s.min == 0.0f && s.max == RM_POINT_ALIASED_MAX);
}

/*
 * The quads of GL's points: the snapped square of an odd and an even
 * width at a fractional centre (GL 1.4, section 3.3.1), the disc of a
 * smooth point, a centre outside the clip volume dropped, and random
 * points, states and viewports against the shim's mgl_point_quads, bit
 * for bit.
 */
static void
test_point_quads(void)
{
	static rm_avert_t in[64], got[4 * 64];
	static unsigned short idx[6 * 64], widx[6 * 64];
	static float wpos[4 * 64][4], wpt[4 * 64][4];
	float id[16], mv[16], proj[16];
	long n, k, i, j;
	int same = 1, c;

	mtl_mat_identity(id);

	/* an orthographic-like view: identity matrices, clip = position, a 640 by 480 window at (0, 0) */
	memset(in, 0, sizeof(in));
	in[0].pos[3] = 1;
	in[0].color[0] = 0.25f;
	in[0].color[3] = 0.5f;

	/* centre (0.3, 0.1): window x = 320 + 0.3 * 320 = 416, y = 240 + 0.1 * 240 = 264 exactly */
	in[0].pos[0] = 0.3f;
	in[0].pos[1] = 0.1f;
	in[0].pos[2] = 0.5f;
	{
		rm_points_t p = test_points(3, 0, 100, 1, 0, 0, 0);

		/* an odd width 3 is centred on a pixel centre: floor(416.0000x) + 0.5; the window x of 0.3f is 416.0000089 */
		k = rm_point_quads(&p, id, id, in, 1, got, 4, idx, 6);
		CHECK(k == 1);
		CHECK(idx[0] == 0 && idx[1] == 1 && idx[2] == 2 && idx[3] == 0 && idx[4] == 2 && idx[5] == 3);

		for (c = 0; c < 4; c++)
		{
			const double sx = (c == 0 || c == 3) ? -1 : 1, sy = (c < 2) ? -1 : 1;
			const double wx = 416.5 + sx * 1.5, wy = 264.5 + sy * 1.5;

			CHECK(fabs(got[c].pos[0] - ((wx - 0) * 2 / 640 - 1)) < 1e-6 && fabs(got[c].pos[1] - ((wy - 0) * 2 / 480 - 1)) < 1e-6);
			CHECK(got[c].pos[2] == 0.5f && got[c].pos[3] == 1.0f);
			CHECK(got[c].uv[0] == (float)(sx * 1.5) && got[c].uv[1] == (float)(sy * 1.5) && got[c].pad[0] == 1.5f && got[c].pad[1] == 0.0f);
			CHECK(got[c].color[0] == 0.25f && got[c].color[3] == 0.5f);
		}

		/* an even width 4 is centred on a corner: floor(416.0000089 + 0.5) = 416, floor(264.0000x + 0.5) = 264 */
		p.size = 4;
		k = rm_point_quads(&p, id, id, in, 1, got, 4, idx, 6);
		CHECK(k == 1);

		for (c = 0; c < 4; c++)
		{
			const double sx = (c == 0 || c == 3) ? -1 : 1, sy = (c < 2) ? -1 : 1;
			const double wx = 416 + sx * 2, wy = 264 + sy * 2;

			CHECK(fabs(got[c].pos[0] - ((wx - 0) * 2 / 640 - 1)) < 1e-6 && fabs(got[c].pos[1] - ((wy - 0) * 2 / 480 - 1)) < 1e-6);
		}

		/* a fractional centre: x = 0.31 gives window 419.2, y = 0.12 gives 268.8: odd width 5 centres on 419.5, 268.5 */
		in[0].pos[0] = 0.31f;
		in[0].pos[1] = 0.12f;
		p.size = 5;
		k = rm_point_quads(&p, id, id, in, 1, got, 4, idx, 6);
		CHECK(k == 1 && fabs(got[0].pos[0] - (((419.5 - 2.5) * 2 / 640) - 1)) < 1e-5 && fabs(got[0].pos[1] - (((268.5 - 2.5) * 2 / 480) - 1)) < 1e-5);
		/* even width 6: floor(419.2 + 0.5) = 419, floor(268.8 + 0.5) = 269 */
		p.size = 6;
		k = rm_point_quads(&p, id, id, in, 1, got, 4, idx, 6);
		CHECK(k == 1 && fabs(got[0].pos[0] - (((419.0 - 3) * 2 / 640) - 1)) < 1e-5 && fabs(got[0].pos[1] - (((269.0 - 3) * 2 / 480) - 1)) < 1e-5);

		/* a smooth point is a disc about the exact centre: half the quad is the radius plus 1/2 */
		p.smooth = 1;
		p.size = 6;
		k = rm_point_quads(&p, id, id, in, 1, got, 4, idx, 6);
		CHECK(k == 1 && got[0].pad[0] == 3.0f && got[0].pad[1] == 1.0f && got[0].uv[0] == -3.5f && got[0].uv[1] == -3.5f);
		CHECK(fabs(got[0].pos[0] - (((419.2 - 3.5) * 2 / 640) - 1)) < 1e-5);
	}

	/* a centre outside the clip volume drops the point: x, y past w, z past w, w not above 0, NaN */
	{
		rm_points_t p = test_points(3, 0, 100, 1, 0, 0, 0);
		static const float out[][4] = {
			{ 1.001f, 0, 0, 1 }, { -1.001f, 0, 0, 1 }, { 0, 1.001f, 0, 1 }, { 0, -1.001f, 0, 1 },
			{ 0, 0, 1.001f, 1 }, { 0, 0, -1.001f, 1 }, { 0, 0, 0, 0 }, { 0, 0, 0, -1 }, { NAN, 0, 0, 1 },
			{ 0, NAN, 0, 1 }, { 0, 0, NAN, 1 }
		};	/* the last is dropped by the x test (0 * NaN is NaN in every clip row), not the z test */
		static const float edge[][4] = { { 1, 0, 0, 1 }, { -1, 0, 0, 1 }, { 0, 1, 0, 1 }, { 0, -1, 0, 1 }, { 0, 0, 1, 1 }, { 0, 0, -1, 1 } };

		for (i = 0; i < (long)(sizeof(out) / sizeof(out[0])); i++)
		{
			memset(in, 0, sizeof(in[0]));
			memcpy(in[0].pos, out[i], sizeof(out[i]));
			k = rm_point_quads(&p, id, id, in, 1, got, 4, idx, 6);
			CHECK(k == 0);
		}

		/* on the volume's edge it stays (GL: inside is |x| <= w) */
		for (i = 0; i < (long)(sizeof(edge) / sizeof(edge[0])); i++)
		{
			memset(in, 0, sizeof(in[0]));
			memcpy(in[0].pos, edge[i], sizeof(edge[i]));
			k = rm_point_quads(&p, id, id, in, 1, got, 4, idx, 6);
			CHECK(k == 1);
		}

		/* the dropped ones leave the rest in order: quad 0 is the second point's */
		memset(in, 0, 3 * sizeof(in[0]));
		in[0].pos[3] = 1;
		in[0].pos[0] = 2;
		in[1].pos[3] = 1;
		in[1].pos[0] = 0.5f;
		in[2].pos[3] = 1;
		in[2].pos[0] = -3;
		k = rm_point_quads(&p, id, id, in, 3, got, 12, idx, 18);
		/* x = 0.5 is window 480: a width of 3 on the pixel centre 480.5, its first corner at 479 */
		CHECK(k == 1 && fabs(got[0].pos[0] - (479.0 * 2 / 640 - 1)) < 1e-6);
		CHECK(idx[6 * 0] == 0 && idx[5] == 3);

		/* a viewport with no area draws nothing */
		p.vw = 0;
		k = rm_point_quads(&p, id, id, in, 3, got, 12, idx, 18);
		CHECK(k == 0);

		/* nor does one with a width but no height */
		p.vw = 640;
		p.vh = 0;
		k = rm_point_quads(&p, id, id, in, 3, got, 12, idx, 18);
		CHECK(k == 0);
	}

	/* a NaN depth with x, y and w finite is refused like the others: m[10] NaN makes clip z NaN only */
	{
		rm_points_t p = test_points(3, 0, 100, 1, 0, 0, 0);
		float nz[16];

		memcpy(nz, id, sizeof(nz));
		nz[10] = NAN;
		memset(in, 0, sizeof(in[0]));
		in[0].pos[2] = 1;
		in[0].pos[3] = 1;
		CHECK(rm_point_quads(&p, id, id, in, 1, got, 4, idx, 6) == 1);
		CHECK(rm_point_quads(&p, id, nz, in, 1, got, 4, idx, 6) == 0);
	}

	/* the capacity: -1 and nothing written when n or the buffers are out of bounds */
	{
		rm_points_t p = test_points(3, 0, 100, 1, 0, 0, 0);

		memset(in, 0, sizeof(in));
		memset(got, 0x5a, sizeof(got[0]) * 4);
		idx[0] = 0x5a5a;
		in[0].pos[3] = in[1].pos[3] = 1;
		CHECK(rm_point_quads(&p, id, id, in, 2, got, 7, idx, 12) == -1);
		CHECK(rm_point_quads(&p, id, id, in, 2, got, 8, idx, 11) == -1);
		CHECK(rm_point_quads(&p, id, id, in, -1, got, 8, idx, 12) == -1);
		CHECK(rm_point_quads(&p, id, id, in, RM_MAX_PARTICLES + 1, got, 4 * (RM_MAX_PARTICLES + 1), idx, 6 * (RM_MAX_PARTICLES + 1)) == -1);
		CHECK(rm_point_quads(&p, id, id, NULL, 2, got, 8, idx, 12) == -1 && rm_point_quads(&p, id, id, in, 2, NULL, 8, idx, 12) == -1);
		CHECK(rm_point_quads(&p, id, id, in, 2, got, 8, NULL, 12) == -1 && rm_point_quads(NULL, id, id, in, 2, got, 8, idx, 12) == -1);
		CHECK(rm_point_quads(&p, NULL, id, in, 2, got, 8, idx, 12) == -1 && rm_point_quads(&p, id, NULL, in, 2, got, 8, idx, 12) == -1);
		CHECK(((unsigned char *)got)[0] == 0x5a && idx[0] == 0x5a5a);
		CHECK(rm_point_quads(&p, id, id, in, 2, got, 8, idx, 12) == 2);
		CHECK(rm_point_quads(&p, id, id, in, 0, got, 0, idx, 0) == 0);
	}

	/* random views, states and points against the shim's mgl_point_quads, in 640 by 480 and other windows */
	for (n = 0; n < 400; n++)
	{
		const float ang[3] = { rngf(-80, 80), rngf(0, 360), rngf(-30, 30) }, org[3] = { rngf(-500, 500), rngf(-500, 500), rngf(-100, 300) };
		const int vw = 64 + (int)(rng() % 1500), vh = 48 + (int)(rng() % 900);
		rm_points_t p = test_points(rngf(1, 80) * vh / 480.0f, rngf(0, 6), rngf(6, 60), 0.01f, 0, 0.01f, (int)(rng() & 1));
		float mvp[16];
		long m = 1 + (long)(rng() % 64), kq, wk;

		if ((n % 5) == 0)
		{
			p.att[0] = rngf(0, 1);
			p.att[1] = rngf(0, 0.01f);
			p.att[2] = rngf(0, 0.001f);
			p.max = rngf(1, 300);
		}

		p.vx = (double)(int)(rng() % 40);
		p.vy = (double)(int)(rng() % 40);
		p.vw = vw;
		p.vh = vh;

		rm_view_matrix(mv, ang, org);
		CHECK(rm_projection(proj, rngf(30, 110), vw, vh, 4, (int)(rng() & 1), 0, 0, 0));
		mtl_mat_mul(mvp, proj, mv);

		memset(in, 0, sizeof(in));

		for (i = 0; i < m; i++)
		{
			/* mostly in front of the view, some behind and far out */
			float sp = (i % 5) == 0 ? 8000 : 700;

			in[i].pos[0] = org[0] + rngf(-sp, sp);
			in[i].pos[1] = org[1] + rngf(-sp, sp);
			in[i].pos[2] = org[2] + rngf(-sp, sp);
			in[i].pos[3] = 1;

			for (j = 0; j < 4; j++)
			{
				in[i].color[j] = rngf(0, 1);
			}
		}

		kq = rm_point_quads(&p, mv, mvp, in, m, got, 4 * m, idx, 6 * m);
		wk = rm_host_shim_point_quads(&p, mv, proj, in, m, wpos, wpt, widx);
		same = same && kq == wk;

		for (i = 0; same && i < 4 * (wk < kq ? wk : kq); i++)
		{
			same = same && memcmp(got[i].pos, wpos[i], sizeof(wpos[i])) == 0;
			same = same && got[i].uv[0] == wpt[i][0] && got[i].uv[1] == wpt[i][1] && got[i].pad[0] == wpt[i][2] && got[i].pad[1] == wpt[i][3];
		}

		for (i = 0; same && i < 6 * (wk < kq ? wk : kq); i++)
		{
			same = same && idx[i] == widx[i];
		}

		if (!same)
		{
			printf("point quads differ: view %ld, %ld points, %ld against %ld quads\n", n, m, kq, wk);
			break;
		}
	}

	CHECK(same);
}

/*
 * gl1's textured particle (gl1_pointparameters 0): the triangle at the
 * point, plus 1.5 vup and 1.5 vright times the scale (1 within 20 units
 * of the eye along vpn, else 1 + distance * 0.004), worked by hand and in
 * double on random views; the texture coordinates; a vertex that is not finite refuses it.
 */
static void
test_particle_tri(void)
{
	const float eye[3] = { 100, 200, 300 }, vpn[3] = { 1, 0, 0 }, vup[3] = { 0, 0, 1 }, vright[3] = { 0, -1, 0 };
	float v[3][3], tex[3][2];
	float o[3];
	int i, j, ok = 1;

	/* 10 units ahead: scale 1 */
	o[0] = 110; o[1] = 205; o[2] = 290;
	CHECK(rm_particle_tri(o, eye, vpn, vup, vright, v));
	CHECK(v[0][0] == 110 && v[0][1] == 205 && v[0][2] == 290);
	CHECK(v[1][0] == 110 && v[1][1] == 205 && v[1][2] == 291.5f);
	CHECK(v[2][0] == 110 && v[2][1] == 203.5f && v[2][2] == 290);

	/* exactly 20 ahead is not "below 20" and gets 1 + 20 * 0.004 = 1.08 */
	o[0] = 120;
	CHECK(rm_particle_tri(o, eye, vpn, vup, vright, v));
	CHECK(fabsf(v[1][2] - (290 + 1.5f * 1.08f)) < 1e-4f && fabsf(v[2][1] - (205 - 1.5f * 1.08f)) < 1e-4f);

	/* 19.999 ahead is 1 */
	o[0] = 119.999f;
	CHECK(rm_particle_tri(o, eye, vpn, vup, vright, v) && v[1][2] == 291.5f);

	/* behind the eye: scale 1 */
	o[0] = 0;
	CHECK(rm_particle_tri(o, eye, vpn, vup, vright, v) && v[1][2] == 291.5f);

	/* 1000 ahead: 1 + 1000 * 0.004 = 5 */
	o[0] = 1100;
	CHECK(rm_particle_tri(o, eye, vpn, vup, vright, v) && fabsf(v[1][2] - (290 + 1.5f * 5)) < 1e-3f);

	/*
	 * 662 ahead: gl1's 0.004 is a double, so the scale is 1 + 662 * 0.004
	 * in double, 3.648 as a float, and the vertex 290 + 1.5 * 3.648 =
	 * 295.471985 (with a float 0.004 it would be 295.472015)
	 */
	o[0] = 762;
	CHECK(rm_particle_tri(o, eye, vpn, vup, vright, v) && v[1][2] == 295.471985f && v[2][1] == 205 - 1.5f * 3.648f);

	rm_particle_tex(tex);
	CHECK(tex[0][0] == 0.0625f && tex[0][1] == 0.0625f && tex[1][0] == 1.0625f && tex[1][1] == 0.0625f);
	CHECK(tex[2][0] == 0.0625f && tex[2][1] == 1.0625f);

	/* random views against the same worked in double (the float arithmetic differs in the last bits only) */
	for (i = 0; i < 5000; i++)
	{
		float e[3], n[3], u[3], r[3], p[3], t[3][3];
		double d = 0, sc;

		for (j = 0; j < 3; j++)
		{
			e[j] = rngf(-4000, 4000);
			n[j] = rngf(-1, 1);
			u[j] = rngf(-1, 1);
			r[j] = rngf(-1, 1);
			p[j] = e[j] + rngf(-3000, 3000);
			d += (double)(p[j] - e[j]) * n[j];
		}

		sc = d < 20 ? 1.0 : 1 + d * 0.004;
		ok = ok && rm_particle_tri(p, e, n, u, r, t);

		for (j = 0; j < 3; j++)
		{
			ok = ok && t[0][j] == p[j] && fabs(t[1][j] - (p[j] + u[j] * 1.5 * sc)) < 1e-2 &&
				fabs(t[2][j] - (p[j] + r[j] * 1.5 * sc)) < 1e-2;
		}
	}

	CHECK(ok);

	/* an origin far out makes a scale past the floats: refused, not drawn */
	{
		const float huge[3] = { 3e38f, 3e38f, 3e38f }, big_up[3] = { 3e38f, 3e38f, 3e38f };

		CHECK(!rm_particle_tri(huge, eye, big_up, vup, vright, v));
		o[0] = 1100;
		CHECK(!rm_particle_tri(o, eye, vpn, big_up, vright, v));
		o[0] = NAN;
		CHECK(!rm_particle_tri(o, eye, vpn, vup, vright, v));
	}

	/* only the z of vertex 1, then only the x of vertex 2, not finite (1.5 * 3e38 is inf as a float) */
	{
		const float zup[3] = { 0, 0, 3e38f }, xr[3] = { 3e38f, 0, 0 };

		o[0] = 110; o[1] = 205; o[2] = 290;
		CHECK(!rm_particle_tri(o, eye, vpn, zup, vright, v));
		CHECK(!rm_particle_tri(o, eye, vpn, vup, xr, v));
	}
}

/* gl1's R_InitParticleTexture: white, alpha 85 times the table, the dot's and the square's corners and rows */
static void
test_particle_texture(void)
{
	unsigned char dot[16][16][4], sq[16][16][4];
	int x, y, white = 1;

	memset(dot, 0x5a, sizeof(dot));
	memset(sq, 0x5a, sizeof(sq));
	rm_particle_texture(0, dot);
	rm_particle_texture(1, sq);

	for (y = 0; y < 16; y++)
	{
		for (x = 0; x < 16; x++)
		{
			white = white && dot[y][x][0] == 255 && dot[y][x][1] == 255 && dot[y][x][2] == 255 &&
				sq[y][x][0] == 255 && sq[y][x][1] == 255 && sq[y][x][2] == 255;
			white = white && (dot[y][x][3] % 85) == 0 && (sq[y][x][3] % 85) == 0;
		}
	}

	CHECK(white);

	/* partData[y][x] = table[x][y] * 85: the dot's table row 1 is 0 0 1 2 3 3 2 1, so column 1 of the texture reads it down */
	CHECK(dot[2][1][3] == 85 && dot[3][1][3] == 170 && dot[4][1][3] == 255 && dot[5][1][3] == 255 && dot[7][1][3] == 85 && dot[8][1][3] == 0);
	CHECK(dot[0][0][3] == 0 && dot[1][1][3] == 0 && dot[15][15][3] == 0 && dot[4][4][3] == 255);
	CHECK(dot[4][9][3] == 0 && dot[9][4][3] == 0);

	/*
	 * the dot is a disc in the 8 by 8 block from (1, 1): its alpha is the
	 * same under a transpose and under the mirror x to 9 - x, nothing lies
	 * outside the block, and the table sums to 156 (alpha 13,260)
	 */
	{
		int sym = 1, sum = 0;

		for (y = 0; y < 16; y++)
		{
			for (x = 0; x < 16; x++)
			{
				const int in_block = (x >= 1 && x <= 8 && y >= 1 && y <= 8);

				sum += dot[y][x][3];
				sym = sym && dot[y][x][3] == dot[x][y][3] && (in_block || dot[y][x][3] == 0);
				sym = sym && (!in_block || dot[y][x][3] == dot[y][9 - x][3]);
			}
		}

		CHECK(sym && sum == 156 * 85);
	}

	/* the square: a 8 by 8 block of 255 from (1, 1), nothing else */
	for (y = 0; y < 16; y++)
	{
		for (x = 0; x < 16; x++)
		{
			const int in_sq = (x >= 1 && x <= 8 && y >= 1 && y <= 8);

			CHECK(sq[y][x][3] == (in_sq ? 255 : 0));
		}
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
	test_ledger_restarts();
	test_frame_size();
	test_frame_remake();
	test_present_region();
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
	test_surface_extents();
	test_lm_size();
	test_lm_samples();
	test_lp_coords();
	test_dlights();
	test_lm_alloc();
	test_lm_regions();
	test_lm_join();
	test_lm_regen_mark();
	test_minlight();
	test_lm_build();
	test_lm_combine();
	test_lm_line();
	test_warp_arith();
	test_warp_subdivide();
	test_sky();
	test_trans();
	test_alias_lerp();
	test_alias_shade();
	test_alias_emit();
	test_alias_cull();
	test_weapon();
	test_alias_region();
	test_md2_check();
	test_md2_regions();
	test_sp2_check();
	test_lm_dlight();
	test_dlight_bounds();
	test_prim_indices();
	test_sprite();
	test_beam();
	test_null_model();
	test_shadow();
	test_flash_blob();
	test_particle_bounds();
	test_particle_color();
	test_point_size();
	test_point_state();
	test_point_quads();
	test_particle_tri();
	test_particle_texture();

	printf("rm_host_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
