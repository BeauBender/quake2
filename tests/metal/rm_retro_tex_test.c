/*
 * The host test of ref_metal's Retro texture upload and low-resolution 3D
 * view (TASK185, PM3): common/mtl_retro_tex.h, the pure part. Built and
 * run by tests/check-ref-metal.sh with the system compiler, ASAN and
 * UBSAN where they exist; no Metal, no SDL, no game data.
 *
 * It covers the RGB565 and ARGB4444 values against an independent
 * floating-point reference, the dither pattern, the alpha kept for the
 * 4444 class, the 256 cap's dimensions and its box average, the sampler's
 * filter name and the 480-line view's scale, one assertion a case.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/client/refresh/metal/common/mtl_retro_tex.h"

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

/* the quantization written out in floating point: floor(c / 255 * L + (r + 0.5) / 16), widened to the nearest byte */
static unsigned
ref_quant(unsigned c, unsigned bits, unsigned r)
{
	const double l = (double)((1u << bits) - 1u);
	const double k = floor((double)c / 255.0 * l + ((double)r + 0.5) / 16.0);

	return (unsigned)floor(k * 255.0 / l + 0.5);
}

/* every byte at every rank, every width the classes use, against the reference */
static void
test_values(void)
{
	unsigned bits[3] = { 4, 5, 6 }, b, c, r;
	int bad = 0;

	for (b = 0; b < 3; b++)
	{
		for (c = 0; c < 256; c++)
		{
			for (r = 0; r < 16; r++)
			{
				bad += (mtl_retro_quant(c, bits[b], r) != ref_quant(c, bits[b], r));
			}
		}
	}

	CHECK(bad == 0);

	/* 565: black and white stay, byte 128 is 123 or 132 in 5 bits and 125 or 130 in 6 bits */
	{
		unsigned char px[8] = { 0, 0, 0, 255, 255, 255, 255, 255 };

		mtl_retro_tex_quantize(px, 2, 1, 0);
		CHECK((px[0] == 0) && (px[1] == 0) && (px[2] == 0) && (px[3] == 255));
		CHECK((px[4] == 255) && (px[5] == 255) && (px[6] == 255) && (px[7] == 255));
	}

	CHECK(mtl_retro_quant(128, 5, 0) == 123);
	CHECK(mtl_retro_quant(128, 5, 15) == 132);
	CHECK(mtl_retro_quant(128, 6, 0) == 125);
	CHECK(mtl_retro_quant(128, 6, 15) == 130);
}

/* an opaque image: red and blue are one of 32 levels, green one of 64, alpha untouched */
static void
test_565(void)
{
	unsigned char px[256 * 4];
	unsigned lv5[256] = { 0 }, lv6[256] = { 0 }, k;
	int i, bad = 0, badalpha = 0;

	for (k = 0; k < 32; k++)
	{
		lv5[(k * 255 + 15) / 31] = 1;
	}

	for (k = 0; k < 64; k++)
	{
		lv6[(k * 255 + 31) / 63] = 1;
	}

	for (i = 0; i < 256; i++)
	{
		px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = (unsigned char)i;
		px[i * 4 + 3] = 255;
	}

	mtl_retro_tex_quantize(px, 16, 16, 0);

	for (i = 0; i < 256; i++)
	{
		bad += !lv5[px[i * 4]] || !lv6[px[i * 4 + 1]] || !lv5[px[i * 4 + 2]];
		badalpha += (px[i * 4 + 3] != 255);
	}

	CHECK(bad == 0);
	CHECK(badalpha == 0);
}

/* the 4444 class: every channel, alpha too, one of 16 levels (a multiple of 17); alpha 0 and 255 kept */
static void
test_4444(void)
{
	unsigned char px[256 * 4];
	int i, bad = 0, badends = 0;

	for (i = 0; i < 256; i++)
	{
		px[i * 4] = (unsigned char)i;
		px[i * 4 + 1] = (unsigned char)(255 - i);
		px[i * 4 + 2] = (unsigned char)((i * 7) & 255);
		px[i * 4 + 3] = (unsigned char)((i & 1) ? 255 : 0);
	}

	mtl_retro_tex_quantize(px, 16, 16, 1);

	for (i = 0; i < 256; i++)
	{
		bad += (px[i * 4] % 17) || (px[i * 4 + 1] % 17) || (px[i * 4 + 2] % 17) || (px[i * 4 + 3] % 17);
		badends += (px[i * 4 + 3] != ((i & 1) ? 255 : 0));
	}

	CHECK(bad == 0);
	CHECK(badends == 0);

	/* a middle alpha becomes one of its two neighbouring levels, both showing over the 16 ranks */
	{
		unsigned char m[16 * 4];
		int lo = 0, hi = 0;

		for (i = 0; i < 16; i++)
		{
			m[i * 4] = m[i * 4 + 1] = m[i * 4 + 2] = 255;
			m[i * 4 + 3] = 128;
		}

		mtl_retro_tex_quantize(m, 4, 4, 1);

		for (i = 0; i < 16; i++)
		{
			lo += (m[i * 4 + 3] == 119);
			hi += (m[i * 4 + 3] == 136);
		}

		CHECK(lo + hi == 16);
		CHECK((lo > 0) && (hi > 0));
	}
}

/* the dither: a flat image becomes the Bayer pattern, 4 texels in a period, the 16 averaging to the byte */
static void
test_dither(void)
{
	unsigned char px[8 * 8 * 4];
	int x, y, per = 1, differs = 0, sum = 0;

	for (x = 0; x < 64; x++)
	{
		px[x * 4] = px[x * 4 + 1] = px[x * 4 + 2] = 128;
		px[x * 4 + 3] = 255;
	}

	mtl_retro_tex_quantize(px, 8, 8, 0);

	for (y = 0; y < 8; y++)
	{
		for (x = 0; x < 8; x++)
		{
			const unsigned char *p = px + (y * 8 + x) * 4;
			const unsigned char *q = px + ((y & 3) * 8 + (x & 3)) * 4;

			per &= (p[0] == q[0]) && (p[1] == q[1]) && (p[2] == q[2]);
			/* the texel's own rank decides it */
			CHECK(p[0] == mtl_retro_quant(128, 5, mtl_retro_bayer((unsigned)x, (unsigned)y)));
			CHECK(p[1] == mtl_retro_quant(128, 6, mtl_retro_bayer((unsigned)x, (unsigned)y)));
		}
	}

	CHECK(per);

	for (y = 0; y < 4; y++)
	{
		for (x = 0; x < 4; x++)
		{
			differs += (px[(y * 8 + x) * 4] != px[0]);
			sum += px[(y * 8 + x) * 4];
		}
	}

	CHECK(differs > 0);
	/* the 16 ranks average to the byte within 1 */
	CHECK(abs(sum / 16 - 128) <= 1);
}

/* the cap's dimensions */
static void
test_cap_dims(void)
{
	int w, h;

	mtl_retro_cap_dims(512, 512, MTL_RETRO_TEX_CAP, &w, &h);
	CHECK((w == 256) && (h == 256));
	mtl_retro_cap_dims(256, 256, MTL_RETRO_TEX_CAP, &w, &h);
	CHECK((w == 256) && (h == 256));
	mtl_retro_cap_dims(64, 128, MTL_RETRO_TEX_CAP, &w, &h);
	CHECK((w == 64) && (h == 128));
	mtl_retro_cap_dims(1024, 256, MTL_RETRO_TEX_CAP, &w, &h);
	CHECK((w == 256) && (h == 64));
	mtl_retro_cap_dims(2048, 2048, MTL_RETRO_TEX_CAP, &w, &h);
	CHECK((w == 256) && (h == 256));
	mtl_retro_cap_dims(300, 200, MTL_RETRO_TEX_CAP, &w, &h);
	CHECK((w == 150) && (h == 100));
	mtl_retro_cap_dims(257, 1, MTL_RETRO_TEX_CAP, &w, &h);
	CHECK((w == 129) && (h == 1));
	/* a side of one never goes to zero */
	mtl_retro_cap_dims(4096, 1, MTL_RETRO_TEX_CAP, &w, &h);
	CHECK((w == 256) && (h == 1));
}

/* the cap's box average, in place: a checker of 0 and 200 becomes 100, a gradient the average of its four sources */
static void
test_cap_apply(void)
{
	const int n = 512;
	unsigned char *px = malloc((size_t)n * n * 4), *ref = malloc((size_t)n * n * 4);
	int w = n, h = n, x, y, bad = 0;

	CHECK(px && ref);

	if (!px || !ref)
	{
		free(px);
		free(ref);
		return;
	}

	for (y = 0; y < n; y++)
	{
		for (x = 0; x < n; x++)
		{
			unsigned char *p = px + ((size_t)y * n + x) * 4;

			p[0] = p[1] = p[2] = (unsigned char)(((x + y) & 1) ? 200 : 0);
			p[3] = 255;
		}
	}

	mtl_retro_cap_apply(px, &w, &h, MTL_RETRO_TEX_CAP);
	CHECK((w == 256) && (h == 256));

	for (y = 0; y < h; y++)
	{
		for (x = 0; x < w; x++)
		{
			const unsigned char *p = px + ((size_t)y * w + x) * 4;

			bad += (p[0] != 100) || (p[1] != 100) || (p[2] != 100) || (p[3] != 255);
		}
	}

	CHECK(bad == 0);

	for (y = 0; y < n; y++)
	{
		for (x = 0; x < n; x++)
		{
			unsigned char *p = ref + ((size_t)y * n + x) * 4;

			p[0] = (unsigned char)x;
			p[1] = (unsigned char)y;
			p[2] = (unsigned char)(x ^ y);
			p[3] = (unsigned char)(x + y);
		}
	}

	memcpy(px, ref, (size_t)n * n * 4);
	w = h = n;
	mtl_retro_cap_apply(px, &w, &h, MTL_RETRO_TEX_CAP);
	bad = 0;

	for (y = 0; y < h; y++)
	{
		for (x = 0; x < w; x++)
		{
			int c;

			for (c = 0; c < 4; c++)
			{
				const unsigned a = ref[((size_t)(2 * y) * n + 2 * x) * 4 + c];
				const unsigned b = ref[((size_t)(2 * y) * n + 2 * x + 1) * 4 + c];
				const unsigned d = ref[((size_t)(2 * y + 1) * n + 2 * x) * 4 + c];
				const unsigned e = ref[((size_t)(2 * y + 1) * n + 2 * x + 1) * 4 + c];

				bad += (px[((size_t)y * w + x) * 4 + c] != (a + b + d + e + 2) / 4);
			}
		}
	}

	CHECK(bad == 0);

	/* an image within the cap is untouched */
	w = 64;
	h = 64;
	mtl_retro_cap_apply(px, &w, &h, MTL_RETRO_TEX_CAP);
	CHECK((w == 64) && (h == 64));
	free(px);
	free(ref);
}

/* the sampler's filter and the 480-line view */
static void
test_view(void)
{
	CHECK(strcmp(MTL_RETRO_TEXMODE, "GL_LINEAR_MIPMAP_NEAREST") == 0);
	CHECK(MTL_RETRO_LINES == 480);
	CHECK(MTL_RETRO_TEX_CAP == 256);
	CHECK(mtl_retro_view_scale(480) == 1.0f);
	CHECK(mtl_retro_view_scale(300) == 1.0f);
	CHECK(fabsf(mtl_retro_view_scale(960) - 0.5f) < 1e-6f);
	CHECK(fabsf(mtl_retro_view_scale(2880) - 480.0f / 2880.0f) < 1e-6f);
	/* the lines it gives, rounded, are 480 at every height above */
	CHECK((int)floor(1080 * mtl_retro_view_scale(1080) + 0.5) == 480);
	CHECK((int)floor(2880 * mtl_retro_view_scale(2880) + 0.5) == 480);
	CHECK((int)floor(481 * mtl_retro_view_scale(481) + 0.5) == 480);
}

int
main(void)
{
	test_values();
	test_565();
	test_4444();
	test_dither();
	test_cap_dims();
	test_cap_apply();
	test_view();
	printf("rm_retro_tex_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
