/*
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
 * Host test of the image decoder (src/client/refresh/files/imgdecode.c),
 * run by tests/check-image-decoder.sh. Every file is generated here: PNG
 * and TGA by the writers below, JPEG, GIF, BMP and TIFF by ImageIO's
 * encoders. No game data, no engine.
 *
 *   imgdecode_test            all cases
 *   imgdecode_test --loop N   every decode case N times, for leaks(1)
 *   --bound SECONDS           (with either) exit 3 once SECONDS have passed
 *
 * =======================================================================
 */

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../src/client/refresh/files/imgdecode.h"

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#endif

static int checks, failures;

#define CHECK(cond, ...) \
	do \
	{ \
		checks++; \
		if (!(cond)) \
		{ \
			failures++; \
			printf("FAIL %s:%d: ", __FILE__, __LINE__); \
			printf(__VA_ARGS__); \
			printf("\n"); \
		} \
	} while (0)

/* ---------- a growing byte buffer ---------- */

typedef struct
{
	unsigned char *p;
	size_t n, cap;
} buf_t;

static void
put(buf_t *b, const void *d, size_t n)
{
	if (b->n + n > b->cap)
	{
		b->cap = (b->n + n) * 2 + 64;
		b->p = realloc(b->p, b->cap);
		if (!b->p)
		{
			printf("out of memory\n");
			exit(2);
		}
	}

	if (n)
	{
		memcpy(b->p + b->n, d, n);
	}
	b->n += n;
}

static void
put8(buf_t *b, unsigned v)
{
	unsigned char c = (unsigned char)v;

	put(b, &c, 1);
}

static void
put16le(buf_t *b, unsigned v)
{
	put8(b, v & 255);
	put8(b, (v >> 8) & 255);
}

static void
put32be(buf_t *b, uint32_t v)
{
	put8(b, v >> 24);
	put8(b, (v >> 16) & 255);
	put8(b, (v >> 8) & 255);
	put8(b, v & 255);
}

/* ---------- a PNG writer: stored deflate blocks, filter 0 ---------- */

static uint32_t
crc32_png(const unsigned char *d, size_t n)
{
	uint32_t c = 0xffffffffu;
	size_t i;
	int k;

	for (i = 0; i < n; i++)
	{
		c ^= d[i];
		for (k = 0; k < 8; k++)
		{
			c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
		}
	}

	return c ^ 0xffffffffu;
}

static void
png_chunk(buf_t *b, const char *type, const unsigned char *d, size_t n)
{
	size_t start;

	put32be(b, (uint32_t)n);
	start = b->n;
	put(b, type, 4);
	put(b, d, n);
	put32be(b, crc32_png(b->p + start, n + 4));
}

/* rows: h scanlines of rowbytes each, without the filter byte */
static buf_t
png_file(uint32_t w, uint32_t h, int depth, int ctype, const unsigned char *rows,
	size_t rowbytes, size_t nrows, const unsigned char *plte, size_t nplte,
	const unsigned char *trns, size_t ntrns)
{
	static const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
	buf_t b = {0}, z = {0}, raw = {0};
	unsigned char ihdr[13];
	uint32_t a = 1, s = 0;
	size_t off = 0, i, y;

	put(&b, sig, 8);
	ihdr[0] = w >> 24; ihdr[1] = w >> 16; ihdr[2] = w >> 8; ihdr[3] = w;
	ihdr[4] = h >> 24; ihdr[5] = h >> 16; ihdr[6] = h >> 8; ihdr[7] = h;
	ihdr[8] = depth; ihdr[9] = ctype; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
	png_chunk(&b, "IHDR", ihdr, 13);

	if (plte)
	{
		png_chunk(&b, "PLTE", plte, nplte);
	}

	if (trns)
	{
		png_chunk(&b, "tRNS", trns, ntrns);
	}

	for (y = 0; y < nrows; y++)
	{
		put8(&raw, 0);
		put(&raw, rows + y * rowbytes, rowbytes);
	}

	put8(&z, 0x78);
	put8(&z, 0x01);

	do
	{
		size_t n = raw.n - off;

		if (n > 65535)
		{
			n = 65535;
		}

		put8(&z, off + n == raw.n ? 1 : 0);
		put16le(&z, (unsigned)n);
		put16le(&z, (unsigned)(~n & 0xffff));
		put(&z, raw.p + off, n);
		off += n;
	} while (off < raw.n);

	for (i = 0; i < raw.n; i++)
	{
		a = (a + raw.p[i]) % 65521;
		s = (s + a) % 65521;
	}
	put32be(&z, (s << 16) | a);

	png_chunk(&b, "IDAT", z.p, z.n);
	png_chunk(&b, "IEND", NULL, 0);

	free(z.p);
	free(raw.p);
	return b;
}

/* ---------- a TGA writer: true-colour, raw or run-length ---------- */

/* rgba: the reference image, top row first; bpp 24 or 32 */
static buf_t
tga_file(unsigned w, unsigned h, int bpp, int rle, int top_origin,
	const unsigned char *rgba)
{
	buf_t b = {0};
	size_t px = (size_t)bpp / 8;
	unsigned yy;

	put8(&b, 0);
	put8(&b, 0);
	put8(&b, rle ? 10 : 2);
	put16le(&b, 0);
	put16le(&b, 0);
	put8(&b, 0);
	put16le(&b, 0);
	put16le(&b, 0);
	put16le(&b, w);
	put16le(&b, h);
	put8(&b, bpp);
	put8(&b, (bpp == 32 ? 8 : 0) | (top_origin ? 0x20 : 0));

	for (yy = 0; yy < h; yy++)
	{
		unsigned y = top_origin ? yy : h - 1 - yy;
		unsigned char row[64 * 4];
		unsigned x = 0, k;

		for (k = 0; k < w; k++)
		{
			const unsigned char *s = rgba + ((size_t)y * w + k) * 4;

			row[k * px + 0] = s[2];
			row[k * px + 1] = s[1];
			row[k * px + 2] = s[0];
			if (px == 4)
			{
				row[k * px + 3] = s[3];
			}
		}

		if (!rle)
		{
			put(&b, row, w * px);
			continue;
		}

		while (x < w)
		{
			unsigned n = 1;

			while (x + n < w && n < 128 && !memcmp(row + (x + n) * px, row + x * px, px))
			{
				n++;
			}

			if (n > 1)
			{
				put8(&b, 0x80 | (n - 1));
				put(&b, row + x * px, px);
			}
			else
			{
				put8(&b, 0);
				put(&b, row + x * px, px);
			}
			x += n;
		}
	}

	return b;
}

/* ---------- the reference image ---------- */

#define RW 7
#define RH 5

static unsigned char ref[RW * RH * 4];

static void
make_reference(void)
{
	int x, y;

	for (y = 0; y < RH; y++)
	{
		for (x = 0; x < RW; x++)
		{
			unsigned char *p = ref + (y * RW + x) * 4;
			int sx = (y == 2 && x >= 3) ? 3 : x;	/* a run for the RLE writer */

			p[0] = (unsigned char)(10 + sx * 30);
			p[1] = (unsigned char)(40 + y * 45);
			p[2] = (unsigned char)((sx * y * 53 + 7) & 255);
			/* alpha 0, 255 and partial values; colour kept under alpha 0 */
			p[3] = (unsigned char)((sx * 64 + y * 23) > 255 ? 255 : (sx * 64 + y * 23));
		}
	}
}

/* ---------- decode helpers ---------- */

static imgdec_result_t
decode(const buf_t *b, size_t size, const char *ext, unsigned char **pic, int *w, int *h)
{
	*pic = NULL;
	*w = -7;
	*h = -7;
	return IMGDEC_Decode(b ? b->p : NULL, size, ext, pic, w, h);
}

#ifdef __APPLE__
/* decodes b as ext and compares with want (RGBA8, w x h), each channel within tol */
static void
expect_image(const char *what, const buf_t *b, const char *ext,
	const unsigned char *want, int w, int h, int tol)
{
	unsigned char *pic;
	int pw, ph, i, worst = 0, at = -1;
	imgdec_result_t r;

	r = decode(b, b->n, ext, &pic, &pw, &ph);
	CHECK(r == IMGDEC_OK, "%s: decode returned %d (%s)", what, (int)r, IMGDEC_ResultString(r));
	if (r != IMGDEC_OK)
	{
		return;
	}

	CHECK(pic != NULL, "%s: no pixels", what);
	CHECK(pw == w && ph == h, "%s: size %dx%d, want %dx%d", what, pw, ph, w, h);
	if (pic && pw == w && ph == h)
	{
		for (i = 0; i < w * h * 4; i++)
		{
			int d = abs((int)pic[i] - (int)want[i]);

			if (d > worst)
			{
				worst = d;
				at = i;
			}
		}
		CHECK(worst <= tol, "%s: byte %d is %d, want %d (tolerance %d)", what, at,
			at >= 0 ? pic[at] : 0, at >= 0 ? want[at] : 0, tol);
	}

	free(pic);
}
#endif

static void
expect_refused(const char *what, const buf_t *b, size_t size, const char *ext,
	imgdec_result_t want)
{
	unsigned char *pic;
	int w, h;
	imgdec_result_t r;

	r = decode(b, size, ext, &pic, &w, &h);
	if (want == IMGDEC_OK)
	{
		/* any refusal */
		CHECK(r != IMGDEC_OK, "%s: decoded, want a refusal", what);
	}
	else
	{
		CHECK(r == want, "%s: returned %d (%s), want %d (%s)", what, (int)r,
			IMGDEC_ResultString(r), (int)want, IMGDEC_ResultString(want));
	}
	CHECK(pic == NULL, "%s: pixels set on a refusal", what);
	CHECK(w == -7 && h == -7, "%s: size written on a refusal", what);
	free(pic);
}

/* ---------- the pure functions ---------- */

static void
test_types(void)
{
	CHECK(IMGDEC_TypeOfExtension("png") == IMGDEC_TYPE_PNG, "png");
	CHECK(IMGDEC_TypeOfExtension("PNG") == IMGDEC_TYPE_PNG, "PNG");
	CHECK(IMGDEC_TypeOfExtension("jpg") == IMGDEC_TYPE_JPEG, "jpg");
	CHECK(IMGDEC_TypeOfExtension("JpG") == IMGDEC_TYPE_JPEG, "JpG");
	CHECK(IMGDEC_TypeOfExtension("tga") == IMGDEC_TYPE_TGA, "tga");
	CHECK(IMGDEC_TypeOfExtension("TGA") == IMGDEC_TYPE_TGA, "TGA");
	CHECK(IMGDEC_TypeOfExtension("gif") == IMGDEC_TYPE_NONE, "gif");
	CHECK(IMGDEC_TypeOfExtension("pcx") == IMGDEC_TYPE_NONE, "pcx");
	CHECK(IMGDEC_TypeOfExtension("jpeg") == IMGDEC_TYPE_NONE, "jpeg");
	CHECK(IMGDEC_TypeOfExtension("pn") == IMGDEC_TYPE_NONE, "pn");
	CHECK(IMGDEC_TypeOfExtension("pngx") == IMGDEC_TYPE_NONE, "pngx");
	CHECK(IMGDEC_TypeOfExtension("tg") == IMGDEC_TYPE_NONE, "tg");
	CHECK(IMGDEC_TypeOfExtension("tgaa") == IMGDEC_TYPE_NONE, "tgaa");
	CHECK(IMGDEC_TypeOfExtension("jp") == IMGDEC_TYPE_NONE, "jp");
	CHECK(IMGDEC_TypeOfExtension("") == IMGDEC_TYPE_NONE, "empty");
	CHECK(IMGDEC_TypeOfExtension(NULL) == IMGDEC_TYPE_NONE, "NULL");

	CHECK(IMGDEC_TypeOfIdentifier("public.png") == IMGDEC_TYPE_PNG, "public.png");
	CHECK(IMGDEC_TypeOfIdentifier("public.jpeg") == IMGDEC_TYPE_JPEG, "public.jpeg");
	CHECK(IMGDEC_TypeOfIdentifier("com.truevision.tga-image") == IMGDEC_TYPE_TGA, "tga uti");
	CHECK(IMGDEC_TypeOfIdentifier("com.compuserve.gif") == IMGDEC_TYPE_NONE, "gif uti");
	CHECK(IMGDEC_TypeOfIdentifier("public.tiff") == IMGDEC_TYPE_NONE, "tiff uti");
	CHECK(IMGDEC_TypeOfIdentifier("public.pn") == IMGDEC_TYPE_NONE, "public.pn");
	CHECK(IMGDEC_TypeOfIdentifier("public.pngx") == IMGDEC_TYPE_NONE, "public.pngx");
	CHECK(IMGDEC_TypeOfIdentifier("public.jpegx") == IMGDEC_TYPE_NONE, "public.jpegx");
	CHECK(IMGDEC_TypeOfIdentifier("com.truevision.tga") == IMGDEC_TYPE_NONE, "com.truevision.tga");
	CHECK(IMGDEC_TypeOfIdentifier("com.truevision.tga-imagex") == IMGDEC_TYPE_NONE, "tga uti, longer");
	CHECK(IMGDEC_TypeOfIdentifier("") == IMGDEC_TYPE_NONE, "empty uti");
	CHECK(IMGDEC_TypeOfIdentifier(NULL) == IMGDEC_TYPE_NONE, "NULL uti");

	/* the backstop behind ImageIO's own allowed type */
	CHECK(IMGDEC_TypeCheck("public.png", IMGDEC_TYPE_PNG) == IMGDEC_OK, "png as png");
	CHECK(IMGDEC_TypeCheck("public.jpeg", IMGDEC_TYPE_JPEG) == IMGDEC_OK, "jpeg as jpeg");
	CHECK(IMGDEC_TypeCheck("com.truevision.tga-image", IMGDEC_TYPE_TGA) == IMGDEC_OK, "tga as tga");
	CHECK(IMGDEC_TypeCheck("public.png", IMGDEC_TYPE_TGA) == IMGDEC_ERR_MISMATCH, "png as tga");
	CHECK(IMGDEC_TypeCheck("public.jpeg", IMGDEC_TYPE_PNG) == IMGDEC_ERR_MISMATCH, "jpeg as png");
	CHECK(IMGDEC_TypeCheck("com.truevision.tga-image", IMGDEC_TYPE_JPEG) == IMGDEC_ERR_MISMATCH, "tga as jpeg");
	CHECK(IMGDEC_TypeCheck("com.compuserve.gif", IMGDEC_TYPE_PNG) == IMGDEC_ERR_TYPE, "gif as png");
	CHECK(IMGDEC_TypeCheck("public.tiff", IMGDEC_TYPE_TGA) == IMGDEC_ERR_TYPE, "tiff as tga");
	CHECK(IMGDEC_TypeCheck(NULL, IMGDEC_TYPE_PNG) == IMGDEC_ERR_TYPE, "no uti");
}

static void
test_pixel_bytes(void)
{
	size_t bytes = 99;

	CHECK(IMGDEC_PixelBytes(1, 1, IMGDEC_MAX_SIDE, &bytes) == 1 && bytes == 4, "1x1");
	CHECK(IMGDEC_PixelBytes(7, 5, IMGDEC_MAX_SIDE, &bytes) == 1 && bytes == 140, "7x5");
	CHECK(IMGDEC_PixelBytes(16384, 16384, IMGDEC_MAX_SIDE, &bytes) == 1 &&
		bytes == (size_t)16384 * 16384 * 4, "16384x16384");
	CHECK(IMGDEC_PixelBytes(16384, 1, IMGDEC_MAX_SIDE, &bytes) == 1, "16384x1");
	CHECK(IMGDEC_PixelBytes(1, 16384, IMGDEC_MAX_SIDE, &bytes) == 1, "1x16384");

	bytes = 99;
	CHECK(IMGDEC_PixelBytes(16385, 1, IMGDEC_MAX_SIDE, &bytes) == 0 && bytes == 99, "16385x1");
	CHECK(IMGDEC_PixelBytes(1, 16385, IMGDEC_MAX_SIDE, &bytes) == 0, "1x16385");
	CHECK(IMGDEC_PixelBytes(0, 1, IMGDEC_MAX_SIDE, &bytes) == 0, "0x1");
	CHECK(IMGDEC_PixelBytes(1, 0, IMGDEC_MAX_SIDE, &bytes) == 0, "1x0");
	CHECK(IMGDEC_PixelBytes(0, 0, IMGDEC_MAX_SIDE, &bytes) == 0, "0x0");

	/* the overflow check, with the side bound out of the way */
	CHECK(IMGDEC_PixelBytes(SIZE_MAX / 4, 1, SIZE_MAX, &bytes) == 1 &&
		bytes == (SIZE_MAX / 4) * 4, "SIZE_MAX/4 x 1 fits");
	CHECK(IMGDEC_PixelBytes(SIZE_MAX / 4 + 1, 1, SIZE_MAX, &bytes) == 0, "SIZE_MAX/4+1 x 1 overflows");
	CHECK(IMGDEC_PixelBytes(SIZE_MAX / 8, 2, SIZE_MAX, &bytes) == 1, "SIZE_MAX/8 x 2 fits");
	CHECK(IMGDEC_PixelBytes(SIZE_MAX / 8 + 1, 2, SIZE_MAX, &bytes) == 0, "SIZE_MAX/8+1 x 2 overflows");
	CHECK(IMGDEC_PixelBytes(2, SIZE_MAX / 8 + 1, SIZE_MAX, &bytes) == 0, "2 x SIZE_MAX/8+1 overflows");
	CHECK(IMGDEC_PixelBytes((size_t)1 << 32, (size_t)1 << 32, SIZE_MAX, &bytes) == 0, "2^32 squared overflows");
}

static void
test_file_size(void)
{
	CHECK(IMGDEC_FileSizeOk(1, IMGDEC_MAX_FILE) == 1, "1 byte");
	CHECK(IMGDEC_FileSizeOk(IMGDEC_MAX_FILE, IMGDEC_MAX_FILE) == 1, "the cap");
	CHECK(IMGDEC_FileSizeOk(IMGDEC_MAX_FILE + 1, IMGDEC_MAX_FILE) == 0, "the cap + 1");
	CHECK(IMGDEC_FileSizeOk(0, IMGDEC_MAX_FILE) == 0, "0 bytes");
	CHECK(IMGDEC_FileSizeOk(10, 10) == 1 && IMGDEC_FileSizeOk(11, 10) == 0, "a cap of 10");
	CHECK(IMGDEC_FileSizeOk(SIZE_MAX, IMGDEC_MAX_FILE) == 0, "SIZE_MAX bytes");
}

static void
test_area(void)
{
	CHECK(IMGDEC_AreaOk(1, 1, IMGDEC_MAX_AREA) == 1, "1x1");
	CHECK(IMGDEC_AreaOk(16384, 4096, IMGDEC_MAX_AREA) == 1, "16384x4096, the bound");
	CHECK(IMGDEC_AreaOk(4096, 16384, IMGDEC_MAX_AREA) == 1, "4096x16384, the bound");
	CHECK(IMGDEC_AreaOk(8192, 8192, IMGDEC_MAX_AREA) == 1, "8192x8192, the bound");
	CHECK(IMGDEC_AreaOk(16384, 4097, IMGDEC_MAX_AREA) == 0, "16384x4097, past the bound");
	CHECK(IMGDEC_AreaOk(4097, 16384, IMGDEC_MAX_AREA) == 0, "4097x16384, past the bound");
	CHECK(IMGDEC_AreaOk(8193, 8192, IMGDEC_MAX_AREA) == 0, "8193x8192, past the bound");
	CHECK(IMGDEC_AreaOk(16384, 16384, IMGDEC_MAX_AREA) == 0, "16384x16384");
	CHECK(IMGDEC_AreaOk(10, 10, 100) == 1 && IMGDEC_AreaOk(10, 11, 100) == 0 &&
		IMGDEC_AreaOk(11, 10, 100) == 0, "a bound of 100");
	/* no overflow in the product */
	CHECK(IMGDEC_AreaOk(SIZE_MAX, 2, IMGDEC_MAX_AREA) == 0, "SIZE_MAX x 2");
	CHECK(IMGDEC_AreaOk((size_t)1 << 32, (size_t)1 << 32, IMGDEC_MAX_AREA) == 0, "2^32 squared");
	CHECK(IMGDEC_AreaOk(5, 0, 100) == 1 && IMGDEC_AreaOk(0, 5, 100) == 1, "a side of 0 has no area");
}

/* the 18 header bytes of a TGA: image type, map entry size and pixel size */
static void
tga_header(unsigned char *hd, int type, int map_bits, int pixel_bits)
{
	memset(hd, 0, 18);
	hd[1] = map_bits ? 1 : 0;
	hd[2] = (unsigned char)type;
	hd[5] = map_bits ? 4 : 0;
	hd[7] = (unsigned char)map_bits;
	hd[12] = 2;
	hd[14] = 1;
	hd[16] = (unsigned char)pixel_bits;
	hd[17] = 0x20;
}

static void
expect_tga_layout(const char *what, int type, int map_bits, int pixel_bits, size_t size,
	int want)
{
	unsigned char hd[18];
	unsigned char *exact;

	tga_header(hd, type, map_bits, pixel_bits);
	/* exactly size bytes, so that a read past them is caught by ASAN */
	exact = malloc(size ? size : 1);
	if (!exact)
	{
		printf("out of memory\n");
		exit(2);
	}
	memcpy(exact, hd, size < sizeof(hd) ? size : sizeof(hd));
	CHECK(IMGDEC_TgaLayoutRefused(exact, size) == want, "tga layout %s: want %d", what, want);
	free(exact);
}

static void
test_tga_layout(void)
{
	/* refused: a colour map that is not 24-bit, 15-bit pixels */
	expect_tga_layout("type 1, 32-bit map", 1, 32, 8, 18, 1);
	expect_tga_layout("type 9, 32-bit map", 9, 32, 8, 18, 1);
	expect_tga_layout("type 1, 16-bit map", 1, 16, 8, 18, 1);
	expect_tga_layout("type 9, 15-bit map", 9, 15, 8, 18, 1);
	expect_tga_layout("type 1, no map size", 1, 0, 8, 18, 1);
	expect_tga_layout("type 2, 15-bit", 2, 0, 15, 18, 1);
	expect_tga_layout("type 10, 15-bit", 10, 0, 15, 18, 1);
	expect_tga_layout("type 1, 32-bit map, a longer file", 1, 32, 8, 64, 1);

	/* decoded */
	expect_tga_layout("type 1, 24-bit map", 1, 24, 8, 18, 0);
	expect_tga_layout("type 9, 24-bit map", 9, 24, 8, 18, 0);
	expect_tga_layout("type 2, 24-bit", 2, 0, 24, 18, 0);
	expect_tga_layout("type 2, 32-bit", 2, 0, 32, 18, 0);
	expect_tga_layout("type 2, 16-bit", 2, 0, 16, 18, 0);
	expect_tga_layout("type 10, 32-bit", 10, 0, 32, 18, 0);
	expect_tga_layout("type 3, 8-bit grey", 3, 0, 8, 18, 0);
	expect_tga_layout("type 11, 8-bit grey", 11, 0, 8, 18, 0);
	/* a truecolour image with a map is left to ImageIO, which refuses it */
	expect_tga_layout("type 2, 24-bit, an unused 32-bit map", 2, 32, 24, 18, 0);

	/* shorter than a header: not judged here, nothing past size is read */
	expect_tga_layout("type 1, 32-bit map, 17 bytes", 1, 32, 8, 17, 0);
	expect_tga_layout("type 2, 15-bit, 17 bytes", 2, 0, 15, 17, 0);
	expect_tga_layout("3 bytes", 1, 32, 8, 3, 0);
	expect_tga_layout("0 bytes", 1, 32, 8, 0, 0);
	CHECK(IMGDEC_TgaLayoutRefused(NULL, 0) == 0, "tga layout: NULL");
	CHECK(IMGDEC_TgaLayoutRefused(NULL, 18) == 0, "tga layout: NULL, 18 bytes");
}

#ifdef __APPLE__
static void
test_bitmap_layout(void)
{
	static const struct
	{
		uint32_t info, alpha;
		int ok;
		size_t swap;
		imgdec_alpha_t want;
	} c[] = {
		{kCGBitmapByteOrderDefault, kCGImageAlphaNone, 1, 0, IMGDEC_ALPHA_NONE},
		{kCGBitmapByteOrder16Big, kCGImageAlphaLast, 1, 0, IMGDEC_ALPHA_LAST},
		{kCGBitmapByteOrder32Big, kCGImageAlphaFirst, 1, 0, IMGDEC_ALPHA_FIRST},
		{kCGBitmapByteOrder16Little, kCGImageAlphaNoneSkipLast, 1, 2, IMGDEC_ALPHA_SKIP_LAST},
		{kCGBitmapByteOrder32Little, kCGImageAlphaNoneSkipFirst, 1, 4, IMGDEC_ALPHA_SKIP_FIRST},
		/* the alpha info arrives apart from the bitmap info's own alpha bits */
		{kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst, kCGImageAlphaFirst, 1, 4, IMGDEC_ALPHA_FIRST},
		{kCGBitmapByteOrderDefault, kCGImageAlphaPremultipliedLast, 0, 0, IMGDEC_ALPHA_NONE},
		{kCGBitmapByteOrderDefault, kCGImageAlphaPremultipliedFirst, 0, 0, IMGDEC_ALPHA_NONE},
		{kCGBitmapByteOrderDefault, kCGImageAlphaOnly, 0, 0, IMGDEC_ALPHA_NONE},
		{kCGBitmapByteOrderDefault, 99, 0, 0, IMGDEC_ALPHA_NONE},
		{kCGBitmapByteOrderDefault | kCGBitmapFloatComponents, kCGImageAlphaNone, 0, 0, IMGDEC_ALPHA_NONE},
		{kCGBitmapByteOrder16Little | kCGBitmapFloatComponents, kCGImageAlphaLast, 0, 0, IMGDEC_ALPHA_NONE},
		{5u << 12, kCGImageAlphaNone, 0, 0, IMGDEC_ALPHA_NONE},
	};
	size_t i;

	for (i = 0; i < sizeof(c) / sizeof(c[0]); i++)
	{
		size_t swap = 77;
		imgdec_alpha_t alpha = (imgdec_alpha_t)77;
		int ok = IMGDEC_BitmapLayout(c[i].info, c[i].alpha, &swap, &alpha);

		CHECK(ok == c[i].ok, "bitmap layout %zu: returned %d", i, ok);
		if (ok && c[i].ok)
		{
			CHECK(swap == c[i].swap && alpha == c[i].want,
				"bitmap layout %zu: swap %zu alpha %d", i, swap, (int)alpha);
		}
	}
}

/* complete only when both statuses are s[0]; 1 is past every CGImageSourceStatus */
static void
test_status(void)
{
	static const int32_t s[] = {kCGImageStatusComplete, kCGImageStatusUnexpectedEOF,
		kCGImageStatusInvalidData, kCGImageStatusUnknownType, kCGImageStatusReadingHeader,
		kCGImageStatusIncomplete, 1};
	size_t i, j;

	for (i = 0; i < sizeof(s) / sizeof(s[0]); i++)
	{
		for (j = 0; j < sizeof(s) / sizeof(s[0]); j++)
		{
			int want = i == 0 && j == 0;

			CHECK(IMGDEC_StatusComplete(s[i], s[j]) == want, "status %d and %d: want %d",
				(int)s[i], (int)s[j], want);
		}
	}
}
#endif

static void
test_result_strings(void)
{
	int i, j;

	for (i = IMGDEC_OK; i <= IMGDEC_ERR_ENCODE; i++)
	{
		const char *s = IMGDEC_ResultString((imgdec_result_t)i);

		CHECK(s && s[0], "result %d has no text", i);
		for (j = IMGDEC_OK; j < i; j++)
		{
			CHECK(strcmp(s, IMGDEC_ResultString((imgdec_result_t)j)) != 0,
				"results %d and %d share a text", i, j);
		}
	}
	CHECK(IMGDEC_ResultString((imgdec_result_t)99) != NULL, "an unknown result");
}

/*
 * IMGDEC_EncodePNG (upscale_textures): what it writes decodes back byte
 * for byte, alpha 0 and partial alpha included, so nothing is
 * premultiplied or colour-converted on the way.
 */
static void
test_encode_png(void)
{
	static const size_t sizes[][2] = { { 1, 1 }, { 3, 5 }, { 64, 64 }, { 256, 7 }, { 7, 256 } };
	size_t i, k;
	unsigned seed = 7;

	for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
	{
		int kind;

		/* 0: opaque, 1: alpha 0 or 255 (what upscale_textures writes), 2: any alpha */
		for (kind = 0; kind < 3; kind++)
		{
			size_t w = sizes[i][0], h = sizes[i][1], n = w * h * 4;
			unsigned char *rgba = malloc(n), *png = NULL, *back = NULL;
			size_t pnglen = 0;
			int bw = -1, bh = -1;
			imgdec_result_t r;

			CHECK(rgba != NULL, "encode: no memory");
			if (!rgba)
			{
				return;
			}

			for (k = 0; k < n; k++)
			{
				seed = seed * 1103515245u + 12345u;
				rgba[k] = (unsigned char)(seed >> 16);
			}

			for (k = 3; k < n; k += 4)
			{
				if (kind == 0)
				{
					rgba[k] = 255;
				}
				else if (kind == 1)
				{
					rgba[k] = (rgba[k] & 1) ? 255 : 0;
				}
			}

			r = IMGDEC_EncodePNG(rgba, w, h, &png, &pnglen);
#ifdef __APPLE__
			CHECK(r == IMGDEC_OK, "encode %zux%zu kind %d: %s", w, h, kind, IMGDEC_ResultString(r));
			CHECK(png != NULL && pnglen > 8 && !memcmp(png, "\x89PNG", 4),
				"encode %zux%zu kind %d: no PNG", w, h, kind);
			if (r == IMGDEC_OK && png)
			{
				r = IMGDEC_Decode(png, pnglen, "png", &back, &bw, &bh);
				CHECK(r == IMGDEC_OK, "encode %zux%zu kind %d: decodes as %s", w, h, kind,
					IMGDEC_ResultString(r));
				CHECK(bw == (int)w && bh == (int)h, "encode %zux%zu kind %d: back as %dx%d",
					w, h, kind, bw, bh);
				CHECK(back && bw == (int)w && bh == (int)h && !memcmp(back, rgba, n),
					"encode %zux%zu kind %d: not the same bytes back", w, h, kind);
			}
#else
			CHECK(r == IMGDEC_ERR_UNSUPPORTED, "encode: %s off Apple platforms", IMGDEC_ResultString(r));
			CHECK(png == NULL, "encode: a buffer off Apple platforms");
#endif
			free(back);
			free(png);
			free(rgba);
		}
	}

	/* the decoder's bounds, before anything is made */
	{
		unsigned char px[4] = { 1, 2, 3, 4 }, *png = (unsigned char *)px;
		size_t pnglen = 0;

		CHECK(IMGDEC_EncodePNG(px, 0, 1, &png, &pnglen) == IMGDEC_ERR_SIZE, "encode 0x1");
		CHECK(png == NULL, "encode 0x1: a buffer");
		CHECK(IMGDEC_EncodePNG(px, 1, 0, &png, &pnglen) == IMGDEC_ERR_SIZE, "encode 1x0");
		CHECK(IMGDEC_EncodePNG(px, IMGDEC_MAX_SIDE + 1, 1, &png, &pnglen) == IMGDEC_ERR_SIZE,
			"encode a side too long");
		CHECK(IMGDEC_EncodePNG(px, IMGDEC_MAX_SIDE, IMGDEC_MAX_SIDE, &png, &pnglen) == IMGDEC_ERR_SIZE,
			"encode too many pixels");
		CHECK(IMGDEC_EncodePNG(NULL, 1, 1, &png, &pnglen) == IMGDEC_ERR_ENCODE, "encode NULL");
		CHECK(IMGDEC_EncodePNG(px, 1, 1, NULL, &pnglen) == IMGDEC_ERR_ENCODE, "encode to NULL");
		CHECK(IMGDEC_EncodePNG(px, 1, 1, &png, NULL) == IMGDEC_ERR_ENCODE, "encode no length");
		CHECK(png == NULL, "encode refused: a buffer");
	}
}

/* converts one layout and compares with want */
static void
expect_rgba(const char *what, const imgdec_layout_t *l, const unsigned char *src,
	size_t srclen, const unsigned char *want)
{
	unsigned char out[4 * 16];
	size_t n = l->width * l->height * 4;
	imgdec_result_t r;

	memset(out, 0xcd, sizeof(out));
	r = IMGDEC_ToRGBA(l, src, srclen, out, n);
	CHECK(r == IMGDEC_OK, "%s: returned %d (%s)", what, (int)r, IMGDEC_ResultString(r));
	CHECK(r != IMGDEC_OK || !memcmp(out, want, n), "%s: wrong pixels", what);
	CHECK(out[n] == 0xcd, "%s: wrote past the image", what);
}

static void
expect_layout_refused(const char *what, const imgdec_layout_t *l, const unsigned char *src,
	size_t srclen, imgdec_result_t want)
{
	unsigned char out[4 * 16];
	imgdec_result_t r;

	r = IMGDEC_ToRGBA(l, src, srclen, out, sizeof(out));
	CHECK(r == want, "%s: returned %d (%s), want %d", what, (int)r, IMGDEC_ResultString(r), (int)want);
}

static void
test_to_rgba(void)
{
	/* two pixels: (1,2,3,a=4) and (5,6,7,a=8) */
	static const unsigned char want2[8] = {1, 2, 3, 4, 5, 6, 7, 8};
	static const unsigned char want2_opaque[8] = {1, 2, 3, 255, 5, 6, 7, 255};
	static const unsigned char rgba[8] = {1, 2, 3, 4, 5, 6, 7, 8};
	static const unsigned char argb[8] = {4, 1, 2, 3, 8, 5, 6, 7};
	static const unsigned char bgra[8] = {3, 2, 1, 4, 7, 6, 5, 8};	/* alpha first, 32-bit little */
	static const unsigned char abgr[8] = {4, 3, 2, 1, 8, 7, 6, 5};	/* alpha last, 32-bit little */
	static const unsigned char rgb[6] = {1, 2, 3, 5, 6, 7};
	static const unsigned char rgb16be[12] = {1, 0xaa, 2, 0xbb, 3, 0xcc, 5, 0xdd, 6, 0xee, 7, 0xff};
	static const unsigned char rgb16le[12] = {0xaa, 1, 0xbb, 2, 0xcc, 3, 0xdd, 5, 0xee, 6, 0xff, 7};
	static const unsigned char rgba16le[16] = {0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 7, 0, 8};
	static const unsigned char grey[2] = {9, 200};
	static const unsigned char want_grey[8] = {9, 9, 9, 255, 200, 200, 200, 255};
	static const unsigned char grey_alpha[4] = {9, 50, 200, 0};
	static const unsigned char want_grey_alpha[8] = {9, 9, 9, 50, 200, 200, 200, 0};
	static const unsigned char grey16le[4] = {0x11, 9, 0x22, 200};
	static const unsigned char palette[9] = {10, 20, 30, 40, 50, 60, 70, 80, 90};
	static const unsigned char indexes[2] = {2, 0};
	static const unsigned char want_indexed[8] = {70, 80, 90, 255, 10, 20, 30, 255};
	static const unsigned char bad_index[2] = {0, 3};
	/* 2x2 RGBX with 3 bytes of padding per row */
	static const unsigned char padded[22] = {1, 2, 3, 0, 5, 6, 7, 0, 0xee, 0xee, 0xee,
		9, 10, 11, 0, 13, 14, 15, 0, 0xee, 0xee, 0xee};
	static const unsigned char want_padded[16] = {1, 2, 3, 255, 5, 6, 7, 255,
		9, 10, 11, 255, 13, 14, 15, 255};
	static const unsigned char unpadded[16] = {1, 2, 3, 0, 5, 6, 7, 0, 9, 10, 11, 0, 13, 14, 15, 0};
	imgdec_layout_t l, base;

	memset(&base, 0, sizeof(base));
	base.width = 2;
	base.height = 1;
	base.bits_per_component = 8;
	base.bits_per_pixel = 32;
	base.bytes_per_row = 8;
	base.colors = 3;
	base.alpha = IMGDEC_ALPHA_LAST;

	l = base;
	expect_rgba("rgba", &l, rgba, sizeof(rgba), want2);
	{
		unsigned char out[8];

		CHECK(IMGDEC_ToRGBA(&l, rgba, sizeof(rgba), out, 7) == IMGDEC_ERR_SIZE,
			"a destination one byte short");
	}
	l.alpha = IMGDEC_ALPHA_FIRST;
	expect_rgba("argb", &l, argb, sizeof(argb), want2);
	l.swap = 4;
	expect_rgba("bgra (argb, 32-bit little)", &l, bgra, sizeof(bgra), want2);
	l.alpha = IMGDEC_ALPHA_LAST;
	expect_rgba("abgr (rgba, 32-bit little)", &l, abgr, sizeof(abgr), want2);
	l = base;
	l.alpha = IMGDEC_ALPHA_SKIP_LAST;
	expect_rgba("rgbx", &l, rgba, sizeof(rgba), want2_opaque);
	l.alpha = IMGDEC_ALPHA_SKIP_FIRST;
	expect_rgba("xrgb", &l, argb, sizeof(argb), want2_opaque);
	l.swap = 4;
	expect_rgba("bgrx (xrgb, 32-bit little)", &l, bgra, sizeof(bgra), want2_opaque);

	l = base;
	l.alpha = IMGDEC_ALPHA_NONE;
	l.bits_per_pixel = 24;
	l.bytes_per_row = 6;
	expect_rgba("rgb", &l, rgb, sizeof(rgb), want2_opaque);

	l.bits_per_component = 16;
	l.bits_per_pixel = 48;
	l.bytes_per_row = 12;
	expect_rgba("rgb16 big", &l, rgb16be, sizeof(rgb16be), want2_opaque);
	l.swap = 2;
	expect_rgba("rgb16 little", &l, rgb16le, sizeof(rgb16le), want2_opaque);
	l.alpha = IMGDEC_ALPHA_LAST;
	l.bits_per_pixel = 64;
	l.bytes_per_row = 16;
	expect_rgba("rgba16 little", &l, rgba16le, sizeof(rgba16le), want2);

	l = base;
	l.colors = 1;
	l.alpha = IMGDEC_ALPHA_NONE;
	l.bits_per_pixel = 8;
	l.bytes_per_row = 2;
	expect_rgba("grey", &l, grey, sizeof(grey), want_grey);
	l.alpha = IMGDEC_ALPHA_LAST;
	l.bits_per_pixel = 16;
	l.bytes_per_row = 4;
	expect_rgba("grey alpha", &l, grey_alpha, sizeof(grey_alpha), want_grey_alpha);
	l.alpha = IMGDEC_ALPHA_NONE;
	l.bits_per_component = 16;
	l.swap = 2;
	expect_rgba("grey16 little", &l, grey16le, sizeof(grey16le), want_grey);

	l = base;
	l.colors = 1;
	l.alpha = IMGDEC_ALPHA_NONE;
	l.bits_per_pixel = 8;
	l.bytes_per_row = 2;
	l.palette = palette;
	l.palette_count = 3;
	expect_rgba("indexed", &l, indexes, sizeof(indexes), want_indexed);
	expect_layout_refused("an index past the palette", &l, bad_index, sizeof(bad_index), IMGDEC_ERR_DECODE);
	l.palette_count = 0;
	expect_layout_refused("an empty palette", &l, indexes, sizeof(indexes), IMGDEC_ERR_DECODE);
	{
		static unsigned char full[256 * 3];
		static const unsigned char last[2] = {255, 0};
		unsigned char want_full[8] = {0, 0, 0, 255, 0, 0, 0, 255};
		int k;

		for (k = 0; k < 256 * 3; k++)
		{
			full[k] = (unsigned char)(k / 3);
		}
		want_full[0] = want_full[1] = want_full[2] = 255;
		l.palette = full;
		l.palette_count = 256;
		expect_rgba("a palette of 256 entries", &l, last, sizeof(last), want_full);
	}

	l = base;
	l.height = 2;
	l.alpha = IMGDEC_ALPHA_SKIP_LAST;
	l.bytes_per_row = 11;
	expect_rgba("padded rows", &l, padded, sizeof(padded), want_padded);
	/* the last row needs only its pixels, not its padding */
	expect_rgba("padded rows, the last without padding", &l, padded, sizeof(padded) - 3, want_padded);
	expect_layout_refused("one byte short", &l, padded, sizeof(padded) - 4, IMGDEC_ERR_DECODE);
	l.bytes_per_row = 7;
	expect_layout_refused("rows shorter than their pixels", &l, padded, sizeof(padded), IMGDEC_ERR_LAYOUT);
	l.bytes_per_row = 0;
	expect_layout_refused("rows of 0 bytes", &l, padded, sizeof(padded), IMGDEC_ERR_LAYOUT);
	l.bytes_per_row = 8;
	expect_rgba("rows without padding", &l, unpadded, sizeof(unpadded), want_padded);
	l.height = 1;
	expect_layout_refused("no bytes", &l, padded, 0, IMGDEC_ERR_DECODE);
	expect_layout_refused("one pixel short", &l, padded, 7, IMGDEC_ERR_DECODE);

	l = base;
	l.width = 0;
	expect_layout_refused("width 0", &l, rgba, sizeof(rgba), IMGDEC_ERR_SIZE);
	l = base;
	l.height = 0;
	expect_layout_refused("height 0", &l, rgba, sizeof(rgba), IMGDEC_ERR_SIZE);
	l = base;
	l.width = IMGDEC_MAX_SIDE + 1;
	expect_layout_refused("width above the bound", &l, rgba, sizeof(rgba), IMGDEC_ERR_SIZE);
	{
		/* with room for every byte, only the side bound refuses it */
		size_t n = (size_t)(IMGDEC_MAX_SIDE + 1) * 4;
		unsigned char *big_src = calloc(n, 1), *big_dst = malloc(n);

		l.bytes_per_row = n;
		CHECK(big_src && big_dst && IMGDEC_ToRGBA(&l, big_src, n, big_dst, n) == IMGDEC_ERR_SIZE,
			"width above the bound, with room for it");
		l.width = 1;
		l.height = IMGDEC_MAX_SIDE + 1;
		l.bytes_per_row = 4;
		CHECK(big_src && big_dst && IMGDEC_ToRGBA(&l, big_src, n, big_dst, n) == IMGDEC_ERR_SIZE,
			"height above the bound, with room for it");
		l.height = IMGDEC_MAX_SIDE;
		CHECK(big_src && big_dst && IMGDEC_ToRGBA(&l, big_src, n, big_dst, n) == IMGDEC_OK,
			"height at the bound");
		free(big_src);
		free(big_dst);
	}
	l = base;
	l.bits_per_component = 4;
	l.bits_per_pixel = 16;
	expect_layout_refused("4-bit components", &l, rgba, sizeof(rgba), IMGDEC_ERR_LAYOUT);
	l = base;
	l.bits_per_component = 32;
	l.bits_per_pixel = 128;
	expect_layout_refused("32-bit components", &l, rgba, sizeof(rgba), IMGDEC_ERR_LAYOUT);
	l = base;
	l.bits_per_pixel = 24;
	expect_layout_refused("bits per pixel not the channels'", &l, rgba, sizeof(rgba), IMGDEC_ERR_LAYOUT);
	l = base;
	l.colors = 2;
	l.bits_per_pixel = 24;
	expect_layout_refused("two colours", &l, rgba, sizeof(rgba), IMGDEC_ERR_LAYOUT);
	l = base;
	l.colors = 4;
	l.alpha = IMGDEC_ALPHA_NONE;
	expect_layout_refused("four colours", &l, rgba, sizeof(rgba), IMGDEC_ERR_LAYOUT);
	l = base;
	l.alpha = (imgdec_alpha_t)9;
	expect_layout_refused("an unknown alpha", &l, rgba, sizeof(rgba), IMGDEC_ERR_LAYOUT);
	l.bits_per_pixel = 24;
	l.bytes_per_row = 6;
	expect_layout_refused("an unknown alpha, three channels", &l, rgb, sizeof(rgb), IMGDEC_ERR_LAYOUT);
	l = base;
	l.swap = 3;
	expect_layout_refused("3-byte words", &l, rgba, sizeof(rgba), IMGDEC_ERR_LAYOUT);
	l = base;
	l.swap = 1;
	expect_layout_refused("1-byte words", &l, rgba, sizeof(rgba), IMGDEC_ERR_LAYOUT);
	l = base;
	l.alpha = IMGDEC_ALPHA_NONE;
	l.bits_per_pixel = 24;
	l.bytes_per_row = 6;
	l.swap = 4;
	expect_layout_refused("4-byte words in 3-byte pixels", &l, rgb, sizeof(rgb), IMGDEC_ERR_LAYOUT);
	l.swap = 2;
	expect_layout_refused("2-byte words in 3-byte pixels", &l, rgb, sizeof(rgb), IMGDEC_ERR_LAYOUT);
	l = base;
	l.palette = palette;
	l.palette_count = 3;
	expect_layout_refused("a palette with RGB", &l, rgba, sizeof(rgba), IMGDEC_ERR_LAYOUT);
	l.colors = 1;
	l.bits_per_pixel = 16;
	expect_layout_refused("a palette with alpha", &l, rgba, sizeof(rgba), IMGDEC_ERR_LAYOUT);
	l.alpha = IMGDEC_ALPHA_NONE;
	l.bits_per_component = 16;
	expect_layout_refused("a palette with 16-bit indexes", &l, rgba, sizeof(rgba), IMGDEC_ERR_LAYOUT);
	l.bits_per_component = 8;
	l.bits_per_pixel = 8;
	l.palette_count = 257;
	expect_layout_refused("a palette above 256 entries", &l, indexes, sizeof(indexes), IMGDEC_ERR_LAYOUT);
	l = base;
	l.alpha = IMGDEC_ALPHA_NONE;
	l.bits_per_pixel = 24;
	l.bytes_per_row = 6;
	l.palette = palette;
	l.palette_count = 3;
	expect_layout_refused("a palette with RGB and no alpha", &l, rgb, sizeof(rgb), IMGDEC_ERR_LAYOUT);
}

/* ---------- decoding generated files ---------- */

typedef struct
{
	const char *what;
	buf_t file;
	size_t size;		/* bytes passed, file.n unless cut */
	const char *ext;
	const unsigned char *want;	/* RGBA8, NULL for a refusal */
	int w, h, tol;
	imgdec_result_t refusal;	/* IMGDEC_OK: any refusal */
	int gap;			/* a known gap: decodes, though it should not */
	int gap_from, gap_to;		/* the rows [from, to) the file does not hold */
	int gap_fill;			/* the byte the missing rows must hold */
	int gap_alpha;			/* the fill check covers the alpha byte too */
} dcase_t;

#define MAX_CASES 96
static dcase_t cases[MAX_CASES];
static int ncases;

static dcase_t *
add_case(const char *what, buf_t file, const char *ext)
{
	dcase_t *c;

	if (ncases >= MAX_CASES)
	{
		printf("too many cases\n");
		exit(2);
	}

	c = &cases[ncases++];
	memset(c, 0, sizeof(*c));
	c->what = what;
	c->file = file;
	c->size = file.n;
	c->ext = ext;
	return c;
}

static void
add_good(const char *what, buf_t file, const char *ext, const unsigned char *want,
	int w, int h, int tol)
{
	dcase_t *c = add_case(what, file, ext);

	c->want = want;
	c->w = w;
	c->h = h;
	c->tol = tol;
}

static void
add_bad(const char *what, buf_t file, size_t size, const char *ext, imgdec_result_t refusal)
{
	dcase_t *c = add_case(what, file, ext);

	c->size = size;
	c->refusal = refusal;
}

#ifdef __APPLE__
static void
add_gap(const char *what, buf_t file, const char *ext, int from, int to, int fill, int alpha)
{
	dcase_t *c = add_case(what, file, ext);

	c->gap = 1;
	c->gap_from = from;
	c->gap_to = to;
	c->gap_fill = fill;
	c->gap_alpha = alpha;
}
#endif

static buf_t
copy_buf(const buf_t *b)
{
	buf_t c = {0};

	put(&c, b->p, b->n);
	return c;
}

static buf_t
bytes_buf(const unsigned char *p, size_t n)
{
	buf_t c = {0};

	put(&c, p, n);
	return c;
}

#ifdef __APPLE__
/* an RGBA8 (or grey) image encoded by ImageIO as uti; a progressive JPEG when asked */
static buf_t
encode_as(CFStringRef uti, unsigned w, unsigned h, const unsigned char *pix, int grey,
	int progressive)
{
	CGColorSpaceRef cs = grey ? CGColorSpaceCreateDeviceGray() : CGColorSpaceCreateDeviceRGB();
	CGDataProviderRef dp = CGDataProviderCreateWithData(NULL, pix, (size_t)w * h * (grey ? 1 : 4), NULL);
	CGImageRef img = CGImageCreate(w, h, 8, grey ? 8 : 32, w * (grey ? 1 : 4), cs,
		grey ? kCGImageAlphaNone : kCGImageAlphaNoneSkipLast, dp, NULL, false,
		kCGRenderingIntentDefault);
	CFMutableDataRef out = CFDataCreateMutable(NULL, 0);
	CGImageDestinationRef dst = CGImageDestinationCreateWithData(out, uti, 1, NULL);
	float q = 1.0f;
	CFNumberRef qn = CFNumberCreate(NULL, kCFNumberFloatType, &q);
	CFMutableDictionaryRef p = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
		&kCFTypeDictionaryValueCallBacks);
	buf_t b = {0};

	CFDictionarySetValue(p, kCGImageDestinationLossyCompressionQuality, qn);
	if (progressive)
	{
		const void *k = kCGImagePropertyJFIFIsProgressive;
		const void *v = kCFBooleanTrue;
		CFDictionaryRef jfif = CFDictionaryCreate(NULL, &k, &v, 1, &kCFTypeDictionaryKeyCallBacks,
			&kCFTypeDictionaryValueCallBacks);

		CFDictionarySetValue(p, kCGImagePropertyJFIFDictionary, jfif);
		CFRelease(jfif);
	}
	CGImageDestinationAddImage(dst, img, p);
	if (!CGImageDestinationFinalize(dst))
	{
		printf("ImageIO could not encode a test file\n");
		exit(2);
	}

	put(&b, CFDataGetBytePtr(out), (size_t)CFDataGetLength(out));
	CFRelease(p);
	CFRelease(qn);
	CFRelease(dst);
	CFRelease(out);
	CGImageRelease(img);
	CGDataProviderRelease(dp);
	CGColorSpaceRelease(cs);
	return b;
}

static buf_t
encode(CFStringRef uti, unsigned w, unsigned h, const unsigned char *pix, int grey)
{
	return encode_as(uti, w, h, pix, grey, 0);
}

/*
 * The offset of the scan data after the JPEG's n-th (0-based) start of
 * scan (SOS) segment, or 0 when there is no such scan.
 */
static size_t
jpeg_scan_data(const buf_t *b, int n)
{
	size_t i;

	for (i = 2; i + 4 < b->n; i++)
	{
		if (b->p[i] == 0xff && b->p[i + 1] == 0xda && n-- == 0)
		{
			return i + 2 + ((size_t)b->p[i + 2] << 8 | b->p[i + 3]);
		}
	}

	return 0;
}

/*
 * The refusal of a PNG, JPEG or TGA named as another of the three. From
 * the 27.0 SDK and system on, ImageIO is told the one type it may read
 * and refuses the file itself (IMGDEC_ERR_TYPE); before, the type check
 * behind it does (IMGDEC_ERR_MISMATCH).
 */
static imgdec_result_t
other_type_refusal(void)
{
#if defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 270000
	if (__builtin_available(macOS 27.0, *))
	{
		return IMGDEC_ERR_TYPE;
	}
#endif
	return IMGDEC_ERR_MISMATCH;
}

/* a CMYK JPEG of w x h, encoded by ImageIO */
static buf_t
encode_cmyk(unsigned w, unsigned h)
{
	size_t n = (size_t)w * h * 4, i;
	unsigned char *pix = malloc(n);
	CGColorSpaceRef cs = CGColorSpaceCreateDeviceCMYK();
	CGDataProviderRef dp;
	CGImageRef img;
	CFMutableDataRef out = CFDataCreateMutable(NULL, 0);
	CGImageDestinationRef dst = CGImageDestinationCreateWithData(out, CFSTR("public.jpeg"), 1, NULL);
	buf_t b = {0};

	if (!pix)
	{
		printf("out of memory\n");
		exit(2);
	}

	for (i = 0; i < n; i++)
	{
		pix[i] = (unsigned char)(i * 7);
	}

	dp = CGDataProviderCreateWithData(NULL, pix, n, NULL);
	img = CGImageCreate(w, h, 8, 32, w * 4, cs, kCGImageAlphaNone, dp, NULL, false,
		kCGRenderingIntentDefault);
	CGImageDestinationAddImage(dst, img, NULL);
	if (!CGImageDestinationFinalize(dst))
	{
		printf("ImageIO could not encode a CMYK test file\n");
		exit(2);
	}

	put(&b, CFDataGetBytePtr(out), (size_t)CFDataGetLength(out));
	CFRelease(dst);
	CFRelease(out);
	CGImageRelease(img);
	CGDataProviderRelease(dp);
	CGColorSpaceRelease(cs);
	free(pix);
	return b;
}

/* the offset of the height in the JPEG's baseline frame header (SOF0) */
static size_t
jpeg_frame_size(const buf_t *b)
{
	size_t i;

	for (i = 2; i + 9 < b->n; i++)
	{
		if (b->p[i] == 0xff && b->p[i + 1] == 0xc0)
		{
			return i + 5;
		}
	}

	printf("no frame header in a JPEG test file\n");
	exit(2);
}

/* the offset of the colour transform in the JPEG's Adobe (APP14) segment */
static size_t
jpeg_adobe_transform(const buf_t *b)
{
	size_t i;

	for (i = 2; i + 16 < b->n; i++)
	{
		if (b->p[i] == 0xff && b->p[i + 1] == 0xee && !memcmp(b->p + i + 4, "Adobe", 5))
		{
			return i + 15;
		}
	}

	printf("no Adobe segment in a JPEG test file\n");
	exit(2);
}
#endif

static unsigned char want_rgb[RW * RH * 4], want_grey[RW * RH * 4], want_grey_alpha[RW * RH * 4];
static unsigned char want_indexed[RW * RH * 4], want_indexed_trns[RW * RH * 4];
static unsigned char want_grey1[16 * 4], want_jpeg[16 * 16 * 4], want_jpeg_grey[16 * 16 * 4];
static unsigned char want_wide[16384 * 4];

static void
build_cases(void)
{
	static unsigned char rgb[RW * RH * 3], grey[RW * RH], ga[RW * RH * 2], idx[RW * RH];
	static unsigned char rgb16[RW * RH * 6], wide[16385 * 3];
	static const unsigned char plte[9] = {255, 0, 0, 0, 255, 0, 20, 40, 250};
	static const unsigned char trns[3] = {255, 128, 0};
	static const unsigned char g1[4] = {0xa0, 0x50, 0xf0, 0x00};
	unsigned char jpeg_src[16 * 16 * 4], jpeg_grey_src[16 * 16];
	buf_t b;
	int i;

	make_reference();

	for (i = 0; i < RW * RH; i++)
	{
		const unsigned char *r = ref + i * 4;
		int p = i % 3;

		rgb[i * 3 + 0] = r[0];
		rgb[i * 3 + 1] = r[1];
		rgb[i * 3 + 2] = r[2];
		memcpy(want_rgb + i * 4, r, 3);
		want_rgb[i * 4 + 3] = 255;

		grey[i] = r[1];
		memset(want_grey + i * 4, r[1], 3);
		want_grey[i * 4 + 3] = 255;

		ga[i * 2] = r[1];
		ga[i * 2 + 1] = r[3];
		memset(want_grey_alpha + i * 4, r[1], 3);
		want_grey_alpha[i * 4 + 3] = r[3];

		idx[i] = (unsigned char)p;
		memcpy(want_indexed + i * 4, plte + p * 3, 3);
		want_indexed[i * 4 + 3] = 255;
		memcpy(want_indexed_trns + i * 4, plte + p * 3, 3);
		want_indexed_trns[i * 4 + 3] = trns[p];

		/* 16-bit: the high byte is the reference, the low byte noise */
		rgb16[i * 6 + 0] = r[0];
		rgb16[i * 6 + 1] = 0x5a;
		rgb16[i * 6 + 2] = r[1];
		rgb16[i * 6 + 3] = 0xa5;
		rgb16[i * 6 + 4] = r[2];
		rgb16[i * 6 + 5] = 0xff;
	}

	for (i = 0; i < 16; i++)
	{
		int v = (g1[i / 4] >> (7 - i % 4)) & 1;

		memset(want_grey1 + i * 4, v ? 255 : 0, 3);
		want_grey1[i * 4 + 3] = 255;
	}

	for (i = 0; i < 16 * 16; i++)
	{
		jpeg_src[i * 4 + 0] = 200;
		jpeg_src[i * 4 + 1] = 100;
		jpeg_src[i * 4 + 2] = 50;
		jpeg_src[i * 4 + 3] = 255;
		memcpy(want_jpeg + i * 4, jpeg_src + i * 4, 4);
		jpeg_grey_src[i] = 77;
		memset(want_jpeg_grey + i * 4, 77, 3);
		want_jpeg_grey[i * 4 + 3] = 255;
	}

	for (i = 0; i < 16385; i++)
	{
		wide[i * 3 + 0] = (unsigned char)i;
		wide[i * 3 + 1] = (unsigned char)(i >> 8);
		wide[i * 3 + 2] = 7;
		if (i < 16384)
		{
			memcpy(want_wide + i * 4, wide + i * 3, 3);
			want_wide[i * 4 + 3] = 255;
		}
	}

	/* PNG */
	add_good("png rgb", png_file(RW, RH, 8, 2, rgb, RW * 3, RH, NULL, 0, NULL, 0), "png", want_rgb, RW, RH, 0);
	add_good("png rgba, partial alpha", png_file(RW, RH, 8, 6, ref, RW * 4, RH, NULL, 0, NULL, 0), "png", ref, RW, RH, 0);
	add_good("png grey", png_file(RW, RH, 8, 0, grey, RW, RH, NULL, 0, NULL, 0), "png", want_grey, RW, RH, 0);
	add_good("png grey alpha", png_file(RW, RH, 8, 4, ga, RW * 2, RH, NULL, 0, NULL, 0), "png", want_grey_alpha, RW, RH, 0);
	add_good("png paletted", png_file(RW, RH, 8, 3, idx, RW, RH, plte, 9, NULL, 0), "png", want_indexed, RW, RH, 0);
	add_good("png paletted, tRNS", png_file(RW, RH, 8, 3, idx, RW, RH, plte, 9, trns, 3), "PNG", want_indexed_trns, RW, RH, 0);
	{
		/* the index 7 past a palette of 3, which ImageIO hands on as it is */
		unsigned char bad_idx[RW * RH];

		memcpy(bad_idx, idx, sizeof(bad_idx));
		bad_idx[RW + 2] = 7;
		add_bad("png paletted, an index past the palette",
			png_file(RW, RH, 8, 3, bad_idx, RW, RH, plte, 9, NULL, 0), (size_t)-1, "png", IMGDEC_ERR_DECODE);
	}
	add_good("png grey 1-bit", png_file(4, 4, 1, 0, g1, 1, 4, NULL, 0, NULL, 0), "png", want_grey1, 4, 4, 0);
	add_good("png rgb 16-bit", png_file(RW, RH, 16, 2, rgb16, RW * 6, RH, NULL, 0, NULL, 0), "png", want_rgb, RW, RH, 0);
	add_good("png 16384x1", png_file(16384, 1, 8, 2, wide, 16384 * 3, 1, NULL, 0, NULL, 0), "png", want_wide, 16384, 1, 0);

	/* TGA */
	add_good("tga 24-bit", tga_file(RW, RH, 24, 0, 0, ref), "tga", want_rgb, RW, RH, 0);
	add_good("tga 32-bit", tga_file(RW, RH, 32, 0, 0, ref), "tga", ref, RW, RH, 0);
	add_good("tga 32-bit, top origin", tga_file(RW, RH, 32, 0, 1, ref), "TGA", ref, RW, RH, 0);
	add_good("tga 24-bit rle", tga_file(RW, RH, 24, 1, 0, ref), "tga", want_rgb, RW, RH, 0);
	add_good("tga 32-bit rle", tga_file(RW, RH, 32, 1, 0, ref), "tga", ref, RW, RH, 0);
	{
		/* 2x1, map entries red, green, blue, (10,20,30); indices 1 and 2 */
		static const unsigned char cmap24[] = {0, 1, 1, 0, 0, 4, 0, 24, 0, 0, 0, 0, 2, 0, 1, 0, 8, 0x20,
			0, 0, 255, 0, 255, 0, 255, 0, 0, 30, 20, 10, 1, 2};
		static const unsigned char cmap24_rle[] = {0, 1, 9, 0, 0, 4, 0, 24, 0, 0, 0, 0, 2, 0, 1, 0, 8, 0x20,
			0, 0, 255, 0, 255, 0, 255, 0, 0, 30, 20, 10, 0x01, 1, 2};
		static const unsigned char want_cmap24[8] = {0, 255, 0, 255, 0, 0, 255, 255};
		/* the same with a 32-bit map: alpha 0, 100, 200, 255 */
		static const unsigned char cmap32[] = {0, 1, 1, 0, 0, 4, 0, 32, 0, 0, 0, 0, 2, 0, 1, 0, 8, 0x28,
			0, 0, 255, 0, 0, 255, 0, 100, 255, 0, 0, 200, 30, 20, 10, 255, 1, 2};
		static const unsigned char cmap32_rle[] = {0, 1, 9, 0, 0, 4, 0, 32, 0, 0, 0, 0, 2, 0, 1, 0, 8, 0x28,
			0, 0, 255, 0, 0, 255, 0, 100, 255, 0, 0, 200, 30, 20, 10, 255, 0x01, 1, 2};
		/* 2x1 truecolour, 15-bit pixels */
		static const unsigned char tc15[] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 1, 0, 15, 0x20,
			0x34, 0x12, 0xff, 0x7f};
		/* 2x1 truecolour, 24-bit, after an unused 32-bit map: ImageIO refuses it */
		static const unsigned char unused_map[] = {0, 1, 2, 0, 0, 4, 0, 32, 0, 0, 0, 0, 2, 0, 1, 0, 24, 0x20,
			0, 0, 255, 0, 0, 255, 0, 100, 255, 0, 0, 200, 30, 20, 10, 255, 10, 20, 30, 50, 100, 200};

		add_good("tga colour-mapped, 24-bit map", bytes_buf(cmap24, sizeof(cmap24)), "tga",
			want_cmap24, 2, 1, 0);
		add_good("tga colour-mapped rle, 24-bit map", bytes_buf(cmap24_rle, sizeof(cmap24_rle)), "tga",
			want_cmap24, 2, 1, 0);
		add_bad("tga 24-bit, an unused 32-bit map", bytes_buf(unused_map, sizeof(unused_map)), (size_t)-1,
			"tga", IMGDEC_ERR_DECODE);
		add_bad("tga colour-mapped, 32-bit map", bytes_buf(cmap32, sizeof(cmap32)), (size_t)-1, "tga",
			IMGDEC_ERR_LAYOUT);
		add_bad("tga colour-mapped rle, 32-bit map", bytes_buf(cmap32_rle, sizeof(cmap32_rle)), (size_t)-1,
			"tga", IMGDEC_ERR_LAYOUT);
		/* 2x1 colour-mapped, 24-bit map whose first entry index is 1 */
		static const unsigned char cmap_first[] = {0, 1, 1, 1, 0, 4, 0, 24, 0, 0, 0, 0, 2, 0, 1, 0, 8, 0x20,
			0, 0, 255, 0, 255, 0, 255, 0, 0, 30, 20, 10, 1, 2};
		static const unsigned char cmap_first_rle[] = {0, 1, 9, 1, 0, 4, 0, 24, 0, 0, 0, 0, 2, 0, 1, 0, 8, 0x20,
			0, 0, 255, 0, 255, 0, 255, 0, 0, 30, 20, 10, 0x01, 1, 2};
		/* 2x1 colour-mapped, 16-bit indices */
		static const unsigned char cmap_idx16[] = {0, 1, 1, 0, 0, 4, 0, 24, 0, 0, 0, 0, 2, 0, 1, 0, 16, 0x20,
			0, 0, 255, 0, 255, 0, 255, 0, 0, 30, 20, 10, 1, 0, 2, 0};
		/* 2x1 truecolour type 2 with 8-bit pixels */
		static const unsigned char tc8[] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 1, 0, 8, 0x20,
			10, 20};

		add_bad("tga colour-mapped, first entry index 1", bytes_buf(cmap_first, sizeof(cmap_first)),
			(size_t)-1, "tga", IMGDEC_ERR_DECODE);
		add_bad("tga colour-mapped rle, first entry index 1", bytes_buf(cmap_first_rle, sizeof(cmap_first_rle)),
			(size_t)-1, "tga", IMGDEC_ERR_DECODE);
		add_bad("tga colour-mapped, 16-bit indices", bytes_buf(cmap_idx16, sizeof(cmap_idx16)), (size_t)-1,
			"tga", IMGDEC_ERR_DECODE);
		add_bad("tga type 2, 8-bit pixels", bytes_buf(tc8, sizeof(tc8)), (size_t)-1, "tga",
			IMGDEC_ERR_DECODE);
		add_bad("tga 15-bit", bytes_buf(tc15, sizeof(tc15)), (size_t)-1, "tga", IMGDEC_ERR_LAYOUT);
	}

	/* refusals that need no decoder: the extension and the file size */
	b = png_file(RW, RH, 8, 2, rgb, RW * 3, RH, NULL, 0, NULL, 0);
	add_bad("png named .pcx", copy_buf(&b), b.n, "pcx", IMGDEC_ERR_EXTENSION);
	add_bad("png named .jpeg", copy_buf(&b), b.n, "jpeg", IMGDEC_ERR_EXTENSION);
	add_bad("png, no extension", copy_buf(&b), b.n, "", IMGDEC_ERR_EXTENSION);
	add_bad("png, NULL extension", copy_buf(&b), b.n, NULL, IMGDEC_ERR_EXTENSION);
	add_bad("png, 0 bytes", copy_buf(&b), 0, "png", IMGDEC_ERR_FILESIZE);
	/* the size is refused before a byte is read: the buffer is far smaller */
	add_bad("png, above the file cap", copy_buf(&b), IMGDEC_MAX_FILE + 1, "png", IMGDEC_ERR_FILESIZE);
	add_bad("png, NULL data", (buf_t){0}, 10, "png", IMGDEC_ERR_FILESIZE);

#ifdef __APPLE__
	/* JPEG, lossy: a flat colour within a small tolerance */
	add_good("jpeg", encode(CFSTR("public.jpeg"), 16, 16, jpeg_src, 0), "jpg", want_jpeg, 16, 16, 3);
	add_good("jpeg grey", encode(CFSTR("public.jpeg"), 16, 16, jpeg_grey_src, 1), "jpg", want_jpeg_grey, 16, 16, 1);

	/* the type check and the extension match: inside ImageIO where it can be told */
	add_bad("png named .tga", copy_buf(&b), b.n, "tga", other_type_refusal());
	add_bad("png named .jpg", copy_buf(&b), b.n, "jpg", other_type_refusal());
	add_bad("tga named .png", tga_file(RW, RH, 24, 0, 0, ref), (size_t)-1, "png", other_type_refusal());
	add_bad("jpeg named .png", encode(CFSTR("public.jpeg"), 16, 16, jpeg_src, 0), (size_t)-1, "png", other_type_refusal());
	add_bad("jpeg named .tga", encode(CFSTR("public.jpeg"), 16, 16, jpeg_src, 0), (size_t)-1, "tga", other_type_refusal());
	add_bad("gif named .gif", encode(CFSTR("com.compuserve.gif"), 16, 16, jpeg_src, 0), (size_t)-1, "gif", IMGDEC_ERR_EXTENSION);
	add_bad("gif named .png", encode(CFSTR("com.compuserve.gif"), 16, 16, jpeg_src, 0), (size_t)-1, "png", IMGDEC_ERR_TYPE);
	add_bad("gif named .tga", encode(CFSTR("com.compuserve.gif"), 16, 16, jpeg_src, 0), (size_t)-1, "tga", IMGDEC_ERR_TYPE);
	add_bad("bmp named .tga", encode(CFSTR("com.microsoft.bmp"), 16, 16, jpeg_src, 0), (size_t)-1, "tga", IMGDEC_ERR_TYPE);
	add_bad("tiff named .jpg", encode(CFSTR("public.tiff"), 16, 16, jpeg_src, 0), (size_t)-1, "jpg", IMGDEC_ERR_TYPE);
	{
		buf_t z = {0};
		unsigned char zero[64] = {0};

		put(&z, zero, sizeof(zero));
		add_bad("64 zero bytes named .tga", z, z.n, "tga", IMGDEC_ERR_TYPE);
	}

	/* the side bound, checked on the declared size before decoding */
	add_bad("png 16385x1", png_file(16385, 1, 8, 2, wide, 16385 * 3, 1, NULL, 0, NULL, 0), (size_t)-1, "png", IMGDEC_ERR_SIZE);
	add_bad("png 1x16385", png_file(1, 16385, 8, 2, wide, 3, 16385, NULL, 0, NULL, 0), (size_t)-1, "png", IMGDEC_ERR_SIZE);
	/* cut in the pixels, so only the declared size can refuse them as too large */
	{
		buf_t c = png_file(16385, 1, 8, 2, wide, 16385 * 3, 1, NULL, 0, NULL, 0);

		add_bad("png 16385x1 cut in its pixels", c, c.n / 2, "png", IMGDEC_ERR_SIZE);
		c = png_file(1, 16385, 8, 2, wide, 3, 16385, NULL, 0, NULL, 0);
		add_bad("png 1x16385 cut in its pixels", c, c.n / 2, "png", IMGDEC_ERR_SIZE);
	}
	/*
	 * The area bound, 2^26 pixels, on the declared size. The PNG has one
	 * row written. The CMYK JPEG's frame header is patched to declare the
	 * size: at the bound it passes and its layout is refused, past it the
	 * size is.
	 */
	add_bad("png 16384x4097, past the area bound", png_file(16384, 4097, 8, 0, wide, 16384, 1, NULL, 0, NULL, 0),
		(size_t)-1, "png", IMGDEC_ERR_SIZE);
	{
		static const struct
		{
			const char *what;
			unsigned w, h;
			imgdec_result_t want;
		} a[] = {
			{"jpeg cmyk", 16, 16, IMGDEC_ERR_LAYOUT},
			{"jpeg cmyk declaring 8192x8192, at the area bound", 8192, 8192, IMGDEC_ERR_LAYOUT},
			{"jpeg cmyk declaring 8193x8192, past the area bound", 8193, 8192, IMGDEC_ERR_SIZE},
			{"jpeg cmyk declaring 8192x8193, past the area bound", 8192, 8193, IMGDEC_ERR_SIZE},
		};
		size_t k;

		for (k = 0; k < sizeof(a) / sizeof(a[0]); k++)
		{
			buf_t j = encode_cmyk(16, 16);
			size_t at = jpeg_frame_size(&j);

			j.p[at + 0] = (unsigned char)(a[k].h >> 8);
			j.p[at + 1] = (unsigned char)(a[k].h & 255);
			j.p[at + 2] = (unsigned char)(a[k].w >> 8);
			j.p[at + 3] = (unsigned char)(a[k].w & 255);
			add_bad(a[k].what, j, j.n, "jpg", a[k].want);
		}
	}
	{
		/* ImageIO writes CMYK (Adobe transform 0); the same file marked YCCK */
		buf_t j = encode_cmyk(16, 16);
		size_t at = jpeg_adobe_transform(&j);

		CHECK(j.p[at] == 0, "the CMYK JPEG's Adobe transform is %d", j.p[at]);
		j.p[at] = 2;
		add_bad("jpeg ycck", j, j.n, "jpg", IMGDEC_ERR_LAYOUT);
	}
	/* byte 16 of a PNG (its width's high byte) is 15: the TGA layout check is not applied */
	add_bad("png declaring 251658241x1", png_file(0x0f000001u, 1, 8, 0, wide, 16, 1, NULL, 0, NULL, 0),
		(size_t)-1, "png", IMGDEC_ERR_DECODE);
	add_bad("png declaring 100000x100000", png_file(100000, 100000, 8, 2, rgb, 3, 1, NULL, 0, NULL, 0), (size_t)-1, "png", IMGDEC_OK);
	{
		buf_t t = tga_file(1, 1, 24, 0, 0, ref);

		t.p[12] = 0x20;	/* 20000 x 20000 */
		t.p[13] = 0x4e;
		t.p[14] = 0x20;
		t.p[15] = 0x4e;
		add_bad("tga declaring 20000x20000", t, t.n, "tga", IMGDEC_OK);
	}

	/* truncated files */
	add_bad("png cut to its signature", copy_buf(&b), 8, "png", IMGDEC_OK);
	add_bad("png cut in IHDR", copy_buf(&b), 20, "png", IMGDEC_OK);
	add_bad("png cut after IHDR", copy_buf(&b), 33, "png", IMGDEC_OK);
	add_bad("png cut in IDAT", copy_buf(&b), b.n / 2, "png", IMGDEC_OK);
	add_bad("png cut in its last row", copy_buf(&b), b.n - 30, "png", IMGDEC_OK);
	{
		buf_t t = tga_file(RW, RH, 24, 0, 0, ref);
		buf_t r = tga_file(RW, RH, 32, 1, 0, ref);
		buf_t j = encode(CFSTR("public.jpeg"), 16, 16, jpeg_src, 0);

		add_bad("tga cut to its header", copy_buf(&t), 18, "tga", IMGDEC_OK);
		add_bad("tga cut in the pixels", copy_buf(&t), t.n / 2, "tga", IMGDEC_OK);
		add_bad("tga one byte short", t, t.n - 1, "tga", IMGDEC_OK);
		add_bad("tga rle cut in the pixels", copy_buf(&r), r.n / 2, "tga", IMGDEC_OK);
		add_bad("tga rle one byte short", r, r.n - 1, "tga", IMGDEC_OK);
		add_bad("jpeg cut to 10 bytes", copy_buf(&j), 10, "jpg", IMGDEC_OK);
		add_bad("jpeg cut in the scan", j, j.n / 2, "jpg", IMGDEC_OK);
	}
	{
		/*
		 * Cut inside the pixel data, the container itself well formed: a
		 * PNG whose complete zlib stream holds fewer rows than its IHDR
		 * declares, and TGAs that declare more rows than they hold. Only
		 * the plain TGA is refused. For the others ImageIO's status stays
		 * complete, before and after decoding and whatever the decode
		 * options, and they decode with the missing rows filled in: known
		 * gaps, kept here so that a change in ImageIO shows.
		 */
		static const char *png_rows[4] = {"png holding 1 of its 5 rows", "png holding 2 of its 5 rows",
			"png holding 3 of its 5 rows", "png holding 4 of its 5 rows"};
		buf_t t;
		int k;

		for (k = 1; k < RH; k++)
		{
			add_gap(png_rows[k - 1], png_file(RW, RH, 8, 2, rgb, RW * 3, (size_t)k, NULL, 0, NULL, 0), "png",
				k, RH, 0x00, 0);
		}
		add_gap("png rgba holding 4 of its 5 rows", png_file(RW, RH, 8, 6, ref, RW * 4, RH - 1, NULL, 0, NULL, 0),
			"png", RH - 1, RH, 0x00, 0);

		t = tga_file(RW, 3, 24, 0, 0, ref);
		t.p[14] = RH;
		add_bad("tga holding 3 of its 5 rows", t, (size_t)-1, "tga", IMGDEC_ERR_DECODE);
		t = tga_file(RW, 3, 32, 1, 0, ref);
		t.p[14] = RH;
		add_gap("tga rle holding 3 of its 5 rows", t, "tga", 0, RH - 3, 0xFF, 1);
	}
	{
		/* JPEGs cut inside their scan data, baseline and progressive */
		static unsigned char noise[32 * 32 * 4];
		buf_t j, p;
		size_t at;

		for (i = 0; i < 32 * 32 * 4; i++)
		{
			noise[i] = (unsigned char)(((uint32_t)i * 2654435761u) >> 13);
		}

		j = encode_as(CFSTR("public.jpeg"), 32, 32, noise, 0, 0);
		at = jpeg_scan_data(&j, 0);
		CHECK(at > 0 && at + 64 < j.n && jpeg_scan_data(&j, 1) == 0, "the baseline JPEG has one scan");
		add_bad("jpeg cut 16 bytes into its scan", copy_buf(&j), at + 16, "jpg", IMGDEC_ERR_DECODE);
		add_bad("jpeg cut halfway through its scan", j, at + (j.n - at) / 2, "jpg", IMGDEC_ERR_DECODE);

		p = encode_as(CFSTR("public.jpeg"), 32, 32, noise, 0, 1);
		at = jpeg_scan_data(&p, 1);
		CHECK(at > 0 && at < p.n, "the progressive JPEG has more than one scan");
		add_bad("progressive jpeg cut 16 bytes into its first scan", copy_buf(&p),
			jpeg_scan_data(&p, 0) + 16, "jpg", IMGDEC_ERR_DECODE);
		add_bad("progressive jpeg cut 16 bytes into its second scan", p, at + 16, "jpg",
			IMGDEC_ERR_DECODE);
	}
#else
	(void)want_jpeg;
	(void)want_jpeg_grey;
	(void)jpeg_src;
	(void)jpeg_grey_src;
#endif

	free(b.p);

	for (i = 0; i < ncases; i++)
	{
		if (cases[i].size == (size_t)-1)
		{
			cases[i].size = cases[i].file.n;
		}
	}
}

static void
run_cases(void)
{
	int i;

	for (i = 0; i < ncases; i++)
	{
		const dcase_t *c = &cases[i];

#ifdef __APPLE__
		if (c->want)
		{
			expect_image(c->what, &c->file, c->ext, c->want, c->w, c->h, c->tol);
			continue;
		}

		if (c->gap)
		{
			unsigned char *pic;
			int w, h;
			imgdec_result_t r = decode(&c->file, c->size, c->ext, &pic, &w, &h);

			CHECK(r == IMGDEC_OK, "%s: now refused (%s): move it to the refusals", c->what,
				IMGDEC_ResultString(r));
			if (r == IMGDEC_OK)
			{
				/* the rows the file does not hold (the top ones in a bottom-origin TGA) are filled in, never left uninitialised */
				int y, x, b, nb = c->gap_alpha ? 4 : 3, n = 0;

				for (y = c->gap_from; y < c->gap_to && y < h; y++)
				{
					for (x = 0; x < w; x++)
					{
						for (b = 0; b < nb; b++)
						{
							CHECK(pic[((size_t)y * w + x) * 4 + b] == c->gap_fill,
								"%s: row %d byte %d is not the fill 0x%02x", c->what, y, b,
								c->gap_fill);
							n++;
						}
					}
				}
				CHECK(n > 0, "%s: no fill byte was checked", c->what);
			}
			free(pic);
			continue;
		}
#else
		if (c->want ||
			(c->refusal != IMGDEC_ERR_EXTENSION && c->refusal != IMGDEC_ERR_FILESIZE))
		{
			/* past the extension and the file size there is no decoder */
			expect_refused(c->what, c->file.p ? &c->file : NULL, c->size, c->ext, IMGDEC_ERR_UNSUPPORTED);
			continue;
		}
#endif
		expect_refused(c->what, c->file.p ? &c->file : NULL, c->size, c->ext, c->refusal);
	}
}

static void
free_cases(void)
{
	int i;

	for (i = 0; i < ncases; i++)
	{
		free(cases[i].file.p);
	}
}

static void
on_alarm(int sig)
{
	static const char msg[] = "imgdecode_test: outlived its wall bound\n";

	(void)sig;
	(void)!write(STDOUT_FILENO, msg, sizeof(msg) - 1);
	_exit(3);
}

int
main(int argc, char **argv)
{
	int loops = 0, i;

	for (i = 1; i < argc; i += 2)
	{
		if (i + 1 < argc && !strcmp(argv[i], "--loop"))
		{
			loops = atoi(argv[i + 1]);
		}
		else if (i + 1 < argc && !strcmp(argv[i], "--bound"))
		{
			/* ends the run at the bound, also under leaks(1) */
			signal(SIGALRM, on_alarm);
			alarm((unsigned)atoi(argv[i + 1]));
		}
		else
		{
			printf("usage: %s [--bound SECONDS] [--loop N]\n", argv[0]);
			return 2;
		}
	}

	build_cases();

	if (loops > 0)
	{
		/* every case, good and refused, decoded and freed: for leaks(1) */
		for (i = 0; i < loops; i++)
		{
			run_cases();
			test_encode_png();
		}
		free_cases();
		printf("imgdecode_test: %d loops of %d cases, %d checks, %d failed\n",
			loops, ncases, checks, failures);
		return failures ? 1 : 0;
	}

	test_types();
	test_file_size();
	test_area();
	test_tga_layout();
#ifdef __APPLE__
	test_bitmap_layout();
	test_status();
#endif
	test_pixel_bytes();
	test_result_strings();
	test_to_rgba();
	test_encode_png();
	run_cases();
	free_cases();

	printf("imgdecode_test: %d cases, %d checks, %d failed\n", ncases, checks, failures);
	return failures ? 1 : 0;
}
