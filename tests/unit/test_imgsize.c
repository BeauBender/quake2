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
 * The size decisions of the shared image loaders: the pure functions of
 * src/client/refresh/files/imgsize.h, and the call sites in pcx.c and
 * wal.c, which this file includes whole (their statics are reachable)
 * over a one-file stand-in for the renderer's file system. Each case
 * states the verdict it expects: accepted, or refused (no picture, the
 * loader not called, the reported size left at 0).
 *
 * =======================================================================
 */

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "test.h"
#include "../../src/client/refresh/files/imgsize.h"

#include "../../src/client/refresh/ref_shared.h"

/* ---- stand-in for the renderer's file system and FixFileExt ---- */

refimport_t ri;

static byte *fake_data;
static int fake_len;

static int
Fake_LoadFile(const char *name, void **buf)
{
	byte *copy;

	(void)name;

	if (!fake_data)
	{
		*buf = NULL;
		return -1;
	}

	copy = malloc(fake_len ? fake_len : 1);
	memcpy(copy, fake_data, fake_len);
	*buf = copy;
	return fake_len;
}

static void
Fake_FreeFile(void *buf)
{
	free(buf);
}

void
FixFileExt(const char *origname, const char *ext, char *filename, size_t size)
{
	(void)ext;
	Q_strlcpy(filename, origname, size);
}

#include "../../src/client/refresh/files/pcx.c"
#include "../../src/client/refresh/files/wal.c"

static void
Fake_Set(byte *data, int len)
{
	ri.FS_LoadFile = Fake_LoadFile;
	ri.FS_FreeFile = Fake_FreeFile;
	fake_data = data;
	fake_len = len;
}

/* ---- builders ---- */

#define PCX_HEADER 128

/* a PCX header of the given sizes, little-endian as the file is */
static void
Pcx_Header(byte *buf, int xmin, int ymin, int xmax, int ymax,
	int bits, int planes, int bpl)
{
	pcx_t *p = (pcx_t *)buf;

	memset(buf, 0, PCX_HEADER);
	p->manufacturer = 0x0a;
	p->version = 5;
	p->encoding = 1;
	p->bits_per_pixel = bits;
	p->xmin = LittleShort(xmin);
	p->ymin = LittleShort(ymin);
	p->xmax = LittleShort(xmax);
	p->ymax = LittleShort(ymax);
	p->color_planes = planes;
	p->bytes_per_line = LittleShort(bpl);
}

/*
 * Decode a PCX of a given header with `body` bytes of literal (not run)
 * data after it, 0x01 each. Returns the picture or NULL.
 */
static byte *
Pcx_Run(int xmax, int ymax, int bits, int planes, int bpl, int body,
	int *w, int *h, int *bpp)
{
	byte *file = calloc(1, PCX_HEADER + body + 1);
	byte *pic = NULL, *pal = NULL;

	Pcx_Header(file, 0, 0, xmax, ymax, bits, planes, bpl);
	memset(file + PCX_HEADER, 0x01, body);
	*w = *h = 0;
	PCX_Decode("t.pcx", file, PCX_HEADER + body, &pic, &pal, w, h, bpp);
	free(pal);
	free(file);
	return pic;
}

/* ---- the pure functions ---- */

TEST(imgsize_pixels_cap)
{
	size_t n = 7;

	/* the cap itself: accepted */
	T_CHECK(IMGSIZE_Pixels(8192, 8192, &n));
	T_CHECK(n == ((size_t)1 << 26));
	/* one pixel past it: refused, the output untouched */
	n = 7;
	T_CHECK(!IMGSIZE_Pixels(8193, 8192, &n));
	T_CHECK(n == 7);
	T_CHECK(!IMGSIZE_Pixels((1 << 26) + 1, 1, &n));
	T_CHECK(IMGSIZE_Pixels(1 << 26, 1, &n));
	/* a product that wraps a 32-bit int (and a 32-bit size) */
	T_CHECK(!IMGSIZE_Pixels(65536, 65536, &n));
	T_CHECK(!IMGSIZE_Pixels(INT_MAX, INT_MAX, &n));
	T_CHECK(!IMGSIZE_Pixels(1 << 16, (1 << 16) + 1, &n));
}

TEST(imgsize_pixels_nonpositive)
{
	size_t n = 0;

	T_CHECK(!IMGSIZE_Pixels(0, 4, &n));
	T_CHECK(!IMGSIZE_Pixels(4, 0, &n));
	T_CHECK(!IMGSIZE_Pixels(-1, 4, &n));
	T_CHECK(!IMGSIZE_Pixels(4, -1, &n));
	T_CHECK(!IMGSIZE_Pixels(INT_MIN, INT_MIN, &n));
	T_CHECK(IMGSIZE_Pixels(1, 1, &n));
}

TEST(imgsize_bytes)
{
	size_t n = 0;

	T_CHECK(IMGSIZE_Bytes(8192, 8192, 4, &n));
	T_CHECK(n == ((size_t)1 << 28));
	T_CHECK(!IMGSIZE_Bytes(8193, 8192, 4, &n));
	T_CHECK(!IMGSIZE_Bytes(8, 8, 0, &n));
	T_CHECK(!IMGSIZE_Bytes(8, 8, 5, &n));
	T_CHECK(IMGSIZE_Bytes(320, 240, 1, &n));
	T_CHECK(n == 76800);
}

TEST(imgsize_dims_positive)
{
	T_CHECK(IMGSIZE_DimsPositive(1, 1));
	T_CHECK(!IMGSIZE_DimsPositive(0, 1));
	T_CHECK(!IMGSIZE_DimsPositive(1, 0));
	T_CHECK(!IMGSIZE_DimsPositive(-1, 1));
	T_CHECK(!IMGSIZE_DimsPositive(1, -1));
}

TEST(imgsize_pcx_row_fits)
{
	/* the exact minimum is accepted, one byte less refused */
	T_CHECK(IMGSIZE_PCXRowFits(1, 9, 2));
	T_CHECK(!IMGSIZE_PCXRowFits(1, 9, 1));
	T_CHECK(IMGSIZE_PCXRowFits(1, 8, 1));
	T_CHECK(!IMGSIZE_PCXRowFits(1, 8, 0));
	T_CHECK(IMGSIZE_PCXRowFits(2, 5, 2));
	T_CHECK(!IMGSIZE_PCXRowFits(2, 5, 1));
	T_CHECK(IMGSIZE_PCXRowFits(4, 10, 5));
	T_CHECK(!IMGSIZE_PCXRowFits(4, 10, 4));
	T_CHECK(IMGSIZE_PCXRowFits(4, 9, 5));
	T_CHECK(!IMGSIZE_PCXRowFits(4, 9, 4));
	T_CHECK(!IMGSIZE_PCXRowFits(8, 8, 8));
	T_CHECK(!IMGSIZE_PCXRowFits(4, 0, 5));
}

TEST(imgsize_body_fits)
{
	/* an exact body is accepted, one byte short refused */
	T_CHECK(IMGSIZE_BodyFits(100 + 16, 100, 2, 2, 4));
	T_CHECK(!IMGSIZE_BodyFits(100 + 15, 100, 2, 2, 4));
	T_CHECK(IMGSIZE_BodyFits(100 + 4, 100, 2, 2, 1));
	T_CHECK(!IMGSIZE_BodyFits(100 + 3, 100, 2, 2, 1));
	/* the offset outside the file, or not positive */
	T_CHECK(!IMGSIZE_BodyFits(100, 101, 1, 1, 1));
	T_CHECK(!IMGSIZE_BodyFits(100, 0, 1, 1, 1));
	T_CHECK(!IMGSIZE_BodyFits(100, -1, 1, 1, 1));
	/* a width whose times four wraps a 32-bit int to a small number */
	T_CHECK(!IMGSIZE_BodyFits(100 + 4, 100, 0x40000001, 1, 4));
	T_CHECK(!IMGSIZE_BodyFits(INT_MAX, 1, 0x40000000, 1, 4));
}

/* ---- PCX_Decode ---- */

TEST(imgsize_pcx_stock_8bit)
{
	int w, h, bpp;
	byte *pic;

	/* 320 by 240, one plane: the size of the stock full-screen pictures */
	pic = Pcx_Run(319, 239, 8, 1, 320, 320 * 240 + 769, &w, &h, &bpp);
	T_REQUIRE(pic != NULL);
	T_EQ_INT(w, 320);
	T_EQ_INT(h, 240);
	T_EQ_INT(bpp, 8);
	T_EQ_INT(pic[0], 1);
	free(pic);
}

TEST(imgsize_pcx_stock_24bit)
{
	int w, h, bpp;
	byte *pic;

	pic = Pcx_Run(7, 1, 8, 3, 8, 8 * 3 * 2, &w, &h, &bpp);
	T_REQUIRE(pic != NULL);
	T_EQ_INT(w, 8);
	T_EQ_INT(h, 2);
	T_EQ_INT(bpp, 32);
	free(pic);
}

TEST(imgsize_pcx_wrapping_size_refused)
{
	int w = 0, h = 0, bpp = 0;
	byte *file = calloc(1, PCX_HEADER + 8);
	byte *pic = NULL, *pal = NULL;

	/* xmin -32768, xmax 32767: 65536 columns; 65536 * 65536 wraps an int to 0 */
	Pcx_Header(file, -32768, -32768, 32767, 32767, 8, 1, 4);
	PCX_Decode("t.pcx", file, PCX_HEADER + 8, &pic, &pal, &w, &h, &bpp);
	T_CHECK(pic == NULL);
	free(pic);
	free(pal);
	free(file);
}

TEST(imgsize_pcx_cap_edge)
{
	int w, h, bpp;
	byte *pic;

	/* 8192 by 8192 is the cap: accepted (the short data only logs an issue) */
	pic = Pcx_Run(8191, 8191, 8, 1, 8192, 16, &w, &h, &bpp);
	T_CHECK(pic != NULL);
	T_EQ_INT(w, 8192);
	free(pic);

	/* one column more: refused */
	pic = Pcx_Run(8192, 8191, 8, 1, 8193, 16, &w, &h, &bpp);
	T_CHECK(pic == NULL);
	free(pic);
}

TEST(imgsize_pcx_packed_row)
{
	int w, h, bpp;
	byte *pic;

	/* 4 bits, 10 wide: the row needs 5 bytes. 4 refused, 5 accepted */
	pic = Pcx_Run(9, 1, 4, 1, 4, 64, &w, &h, &bpp);
	T_CHECK(pic == NULL);
	free(pic);
	pic = Pcx_Run(9, 1, 4, 1, 5, 64, &w, &h, &bpp);
	T_CHECK(pic != NULL);
	free(pic);

	/* 2 bits, 5 wide: 2 bytes. 1 refused, 2 accepted */
	pic = Pcx_Run(4, 1, 2, 1, 1, 64, &w, &h, &bpp);
	T_CHECK(pic == NULL);
	free(pic);
	pic = Pcx_Run(4, 1, 2, 1, 2, 64, &w, &h, &bpp);
	T_CHECK(pic != NULL);
	free(pic);

	/* 1 bit, two planes, 9 wide: 2 bytes a plane. 1 refused, 2 accepted */
	pic = Pcx_Run(8, 1, 1, 2, 1, 64, &w, &h, &bpp);
	T_CHECK(pic == NULL);
	free(pic);
	pic = Pcx_Run(8, 1, 1, 2, 2, 64, &w, &h, &bpp);
	T_CHECK(pic != NULL);
	free(pic);
}

/* ---- GetPCXInfo ---- */

TEST(imgsize_getpcxinfo_header)
{
	byte file[PCX_HEADER];
	int w = 0, h = 0;

	Pcx_Header(file, 0, 0, 319, 239, 8, 1, 320);

	/* one byte shorter than the header: sizes stay 0 */
	Fake_Set(file, PCX_HEADER - 1);
	GetPCXInfo("t", &w, &h);
	T_EQ_INT(w, 0);
	T_EQ_INT(h, 0);

	/* exactly the header: read */
	Fake_Set(file, PCX_HEADER);
	GetPCXInfo("t", &w, &h);
	T_EQ_INT(w, 320);
	T_EQ_INT(h, 240);
	Fake_Set(NULL, 0);
}

/* ---- WAL, M8, M32 ---- */

static int loader_calls;
static int loader_w, loader_h;

static struct image_s *
Fake_Loader(const char *name, byte *pic, int width, int realwidth,
	int height, int realheight, size_t data_size, imagetype_t type, int bits)
{
	(void)name; (void)pic; (void)realwidth; (void)realheight;
	(void)data_size; (void)type; (void)bits;
	loader_calls++;
	loader_w = width;
	loader_h = height;
	return (struct image_s *)&loader_calls;
}

static byte *
Wal_File(unsigned w, unsigned h, int body, int *len)
{
	byte *f = calloc(1, sizeof(miptex_t) + body + 1);
	miptex_t *m = (miptex_t *)f;

	m->width = LittleLong(w);
	m->height = LittleLong(h);
	m->offsets[0] = LittleLong(sizeof(miptex_t));
	*len = sizeof(miptex_t) + body;
	return f;
}

static byte *
M8_File(unsigned w, unsigned h, int body, int *len)
{
	byte *f = calloc(1, sizeof(m8tex_t) + body + 1);
	m8tex_t *m = (m8tex_t *)f;

	m->version = LittleLong(M8_VERSION);
	m->width[0] = LittleLong(w);
	m->height[0] = LittleLong(h);
	m->offsets[0] = LittleLong(sizeof(m8tex_t));
	*len = sizeof(m8tex_t) + body;
	return f;
}

static byte *
M32_File(unsigned w, unsigned h, int body, int *len)
{
	byte *f = calloc(1, sizeof(m32tex_t) + body + 1);
	m32tex_t *m = (m32tex_t *)f;

	m->version = LittleLong(M32_VERSION);
	m->width[0] = LittleLong(w);
	m->height[0] = LittleLong(h);
	m->offsets[0] = LittleLong(sizeof(m32tex_t));
	*len = sizeof(m32tex_t) + body;
	return f;
}

TEST(imgsize_wal_m8_m32_stock)
{
	int len;
	byte *f;
	struct image_s *img;

	f = Wal_File(4, 4, 16, &len);
	Fake_Set(f, len);
	loader_calls = 0;
	img = LoadWal("t", it_wall, Fake_Loader);
	T_CHECK(img != NULL);
	T_EQ_INT(loader_w, 4);
	T_EQ_INT(loader_h, 4);
	free(f);

	f = M8_File(4, 4, 16, &len);
	Fake_Set(f, len);
	img = LoadM8("t", it_wall, Fake_Loader);
	T_CHECK(img != NULL);
	T_EQ_INT(loader_w, 4);
	free(f);

	f = M32_File(4, 4, 64, &len);
	Fake_Set(f, len);
	img = LoadM32("t", it_wall, Fake_Loader);
	T_CHECK(img != NULL);
	T_EQ_INT(loader_w, 4);
	T_EQ_INT(loader_h, 4);
	free(f);
	Fake_Set(NULL, 0);
}

TEST(imgsize_m32_wrapping_width_refused)
{
	int len;
	byte *f = M32_File(0x40000001u, 1, 4, &len);

	Fake_Set(f, len);
	loader_calls = 0;
	T_CHECK(LoadM32("t", it_wall, Fake_Loader) == NULL);
	T_EQ_INT(loader_calls, 0);
	free(f);

	f = M32_File(0x80000000u, 1, 4, &len);
	Fake_Set(f, len);
	T_CHECK(LoadM32("t", it_wall, Fake_Loader) == NULL);
	T_EQ_INT(loader_calls, 0);
	free(f);
	Fake_Set(NULL, 0);
}

TEST(imgsize_m32_body)
{
	int len;
	byte *f;

	/* 2 by 2 at four bytes: 16. One short is refused, exact accepted */
	f = M32_File(2, 2, 15, &len);
	Fake_Set(f, len);
	loader_calls = 0;
	T_CHECK(LoadM32("t", it_wall, Fake_Loader) == NULL);
	T_EQ_INT(loader_calls, 0);
	free(f);

	f = M32_File(2, 2, 16, &len);
	Fake_Set(f, len);
	T_CHECK(LoadM32("t", it_wall, Fake_Loader) != NULL);
	T_EQ_INT(loader_calls, 1);
	free(f);
	Fake_Set(NULL, 0);
}

static void
Info_Case(byte *f, int len, void (*get)(const char *, int *, int *),
	int want_w, int want_h)
{
	int w = 0, h = 0;

	Fake_Set(f, len);
	get("t", &w, &h);
	T_EQ_INT(w, want_w);
	T_EQ_INT(h, want_h);
	free(f);
	Fake_Set(NULL, 0);
}

TEST(imgsize_info_dimensions)
{
	int len;

	/* valid: reported */
	Info_Case(Wal_File(8, 4, 0, &len), len, GetWalInfo, 8, 4);
	Info_Case(M8_File(8, 4, 0, &len), len, GetM8Info, 8, 4);
	Info_Case(M32_File(8, 4, 0, &len), len, GetM32Info, 8, 4);

	/* zero width, zero height, negative width, negative height: sizes stay 0 */
	Info_Case(Wal_File(0, 4, 0, &len), len, GetWalInfo, 0, 0);
	Info_Case(Wal_File(8, 0, 0, &len), len, GetWalInfo, 0, 0);
	Info_Case(Wal_File((unsigned)-8, 4, 0, &len), len, GetWalInfo, 0, 0);
	Info_Case(Wal_File(8, (unsigned)-4, 0, &len), len, GetWalInfo, 0, 0);

	Info_Case(M8_File(0, 4, 0, &len), len, GetM8Info, 0, 0);
	Info_Case(M8_File(8, 0, 0, &len), len, GetM8Info, 0, 0);
	Info_Case(M8_File((unsigned)-8, 4, 0, &len), len, GetM8Info, 0, 0);
	Info_Case(M8_File(8, (unsigned)-4, 0, &len), len, GetM8Info, 0, 0);

	Info_Case(M32_File(0, 4, 0, &len), len, GetM32Info, 0, 0);
	Info_Case(M32_File(8, 0, 0, &len), len, GetM32Info, 0, 0);
	Info_Case(M32_File((unsigned)-8, 4, 0, &len), len, GetM32Info, 0, 0);
	Info_Case(M32_File(8, (unsigned)-4, 0, &len), len, GetM32Info, 0, 0);
}

/* ---- WAL and M8 held to the cap ---- */

TEST(imgsize_wal_m8_cap)
{
	int len;
	byte *f;
	size_t cap_body = (size_t)1 << 26;

	/* WAL at the cap (8192 by 8192, one byte a pixel): accepted */
	f = Wal_File(8192, 8192, (int)cap_body, &len);
	Fake_Set(f, len);
	loader_calls = 0;
	T_CHECK(LoadWal("t", it_wall, Fake_Loader) != NULL);
	T_EQ_INT(loader_calls, 1);
	free(f);

	/* one column past it, with a body that holds it: refused */
	f = Wal_File(8193, 8192, (int)cap_body + 8192, &len);
	Fake_Set(f, len);
	loader_calls = 0;
	T_CHECK(LoadWal("t", it_wall, Fake_Loader) == NULL);
	T_EQ_INT(loader_calls, 0);
	free(f);

	/* the same for M8 */
	f = M8_File(8192, 8192, (int)cap_body, &len);
	Fake_Set(f, len);
	loader_calls = 0;
	T_CHECK(LoadM8("t", it_wall, Fake_Loader) != NULL);
	T_EQ_INT(loader_calls, 1);
	free(f);

	f = M8_File(8193, 8192, (int)cap_body + 8192, &len);
	Fake_Set(f, len);
	loader_calls = 0;
	T_CHECK(LoadM8("t", it_wall, Fake_Loader) == NULL);
	T_EQ_INT(loader_calls, 0);
	free(f);
	Fake_Set(NULL, 0);
}

TEST(imgsize_wal_m8_body)
{
	int len;
	byte *f;

	/* 3 by 3: 9 bytes exact accepted, 8 refused */
	f = Wal_File(3, 3, 9, &len);
	Fake_Set(f, len);
	loader_calls = 0;
	T_CHECK(LoadWal("t", it_wall, Fake_Loader) != NULL);
	free(f);
	f = Wal_File(3, 3, 8, &len);
	Fake_Set(f, len);
	T_CHECK(LoadWal("t", it_wall, Fake_Loader) == NULL);
	free(f);

	f = M8_File(3, 3, 9, &len);
	Fake_Set(f, len);
	T_CHECK(LoadM8("t", it_wall, Fake_Loader) != NULL);
	free(f);
	f = M8_File(3, 3, 8, &len);
	Fake_Set(f, len);
	T_CHECK(LoadM8("t", it_wall, Fake_Loader) == NULL);
	free(f);

	/* an offset past the end */
	f = Wal_File(1, 1, 1, &len);
	((miptex_t *)f)->offsets[0] = LittleLong(len + 1);
	Fake_Set(f, len);
	T_CHECK(LoadWal("t", it_wall, Fake_Loader) == NULL);
	free(f);
	Fake_Set(NULL, 0);
}
