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
 * The host test of upscale_textures' metalfx backend (TASK124,
 * src/client/cl_upscale_metalfx.m), built alone with the test-only
 * switch UPSCALE_METALFX_TEST by tests/check-upscale-metalfx.sh: no
 * scaling outside a run; a forced-unsupported run refuses with its
 * reason and the menu's check then gives that reason; on a Metal device
 * with MetalFX, a synthetic 66 x 66 tile (64 x 64 and one texel of
 * wrap-padding, hard-edged blocks like a palette tile, no game data) is
 * scaled at 2x and 4x, twice each with other sizes between, to the same
 * bytes, with MetalFX's alpha 255 everywhere (why CL_UpscaleRGBA sets
 * the alpha from the indices); sizes it must refuse are refused. Prints
 * "skipped: no Metal device" and exits 0 where there is none.
 *
 * =======================================================================
 */

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "header/upscale_metalfx.h"

extern const char *upscale_metalfx_test_unsupported;

static int passed, failed;

#define CHECK(COND) \
	do \
	{ \
		if (COND) \
		{ \
			passed++; \
		} \
		else \
		{ \
			failed++; \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #COND); \
		} \
	} while (0)

enum { TW = 66, TH = 66 };

/* blocks of colour with hard edges, alpha 0 in about one texel in seven */
static void
make_tile(unsigned char *in)
{
	unsigned seed = 12345;
	int i;

	for (i = 0; i < TW * TH; i++)
	{
		int x = i % TW, y = i / TW;

		if (((x / 3) + (y / 5)) % 4 == 0)
		{
			seed = seed * 1103515245u + 12345u;
		}

		in[i * 4 + 0] = (unsigned char)(seed >> 16);
		in[i * 4 + 1] = (unsigned char)(seed >> 8);
		in[i * 4 + 2] = (unsigned char)(seed >> 24);
		in[i * 4 + 3] = ((x * y) % 7) ? 255 : 0;
	}
}

static unsigned long long
fnv1a(const unsigned char *p, size_t n)
{
	unsigned long long h = 1469598103934665603ull;
	size_t i;

	for (i = 0; i < n; i++)
	{
		h = (h ^ p[i]) * 1099511628211ull;
	}

	return h;
}

int
main(void)
{
	static unsigned char in[TW * TH * 4];
	unsigned char small[8 * 8 * 4], smallout[16 * 16 * 4];
	char reason[128];
	int f;

	make_tile(in);
	memset(small, 90, sizeof(small));

	@autoreleasepool
	{
		id<MTLDevice> probe = MTLCreateSystemDefaultDevice();

		if (!probe)
		{
			printf("upscale_metalfx_test: skipped: no Metal device\n");
			return 0;
		}
	}

	/* outside a run nothing is scaled */
	CHECK(CL_UpscaleMetalFXScale(NULL, small, 8, 8, smallout, 16, 16) == 0);

	/* forced unsupported: refused with its reason, and the menu's check says it */
	upscale_metalfx_test_unsupported = "test: forced unsupported";
	strcpy(reason, "x");
	CHECK(CL_UpscaleMetalFXBegin(reason, sizeof(reason)) == 0);
	CHECK(strcmp(reason, "test: forced unsupported") == 0);
	CHECK(CL_UpscaleMetalFXScale(NULL, small, 8, 8, smallout, 16, 16) == 0);
	strcpy(reason, "x");
	CHECK(CL_UpscaleMetalFXAvailable(reason, sizeof(reason)) == 0);
	CHECK(strcmp(reason, "test: forced unsupported") == 0);
	upscale_metalfx_test_unsupported = NULL;

	/* a reason too long for its buffer is cut, not overrun */
	upscale_metalfx_test_unsupported = "a reason longer than eight bytes";
	CHECK(CL_UpscaleMetalFXBegin(reason, 8) == 0);
	CHECK(strcmp(reason, "a reaso") == 0);
	upscale_metalfx_test_unsupported = NULL;

	if (!CL_UpscaleMetalFXBegin(reason, sizeof(reason)))
	{
		/* a device without MetalFX's spatial scaler: the menu's check agrees */
		printf("upscale_metalfx_test: unsupported here: %s\n", reason);
		CHECK(reason[0] != '\0');
		CHECK(CL_UpscaleMetalFXAvailable(reason, sizeof(reason)) == 0);
		printf("upscale_metalfx_test: %d passed, %d failed\n", passed, failed);
		return failed ? 1 : 0;
	}

	/* a supported run clears what the forced one left */
	CHECK(reason[0] == '\0');
	CHECK(CL_UpscaleMetalFXAvailable(reason, sizeof(reason)) == 1);
	CHECK(reason[0] == '\0');

	for (f = 2; f <= 4; f += 2)
	{
		const int ow = TW * f, oh = TH * f;
		const size_t n = (size_t)ow * oh * 4;
		unsigned char *a = malloc(n), *b = malloc(n);
		int i, opaque = 1;

		CHECK(a && b);

		if (!a || !b)
		{
			free(a);
			free(b);
			break;
		}

		memset(a, 1, n);
		memset(b, 2, n);
		CHECK(CL_UpscaleMetalFXScale(NULL, in, TW, TH, a, ow, oh) == 1);
		/* another size between: the scaler is made again */
		CHECK(CL_UpscaleMetalFXScale(NULL, small, 8, 8, smallout, 16, 16) == 1);
		CHECK(CL_UpscaleMetalFXScale(NULL, in, TW, TH, b, ow, oh) == 1);

		/* the same bytes every time */
		CHECK(memcmp(a, b, n) == 0);

		/* MetalFX drops the alpha: the caller sets it from the indices */
		for (i = 0; i < ow * oh; i++)
		{
			if (a[i * 4 + 3] != 255)
			{
				opaque = 0;
			}
		}

		/* an observation, not a check: CL_UpscaleRGBA sets the alpha anyway */
		printf("upscale_metalfx_test: %dx: fnv1a %016llx, alpha 255 everywhere: %s\n",
			f, fnv1a(a, n), opaque ? "yes" : "no");

		free(a);
		free(b);
	}

	/* sizes it refuses: smaller output, too big, no buffer */
	CHECK(CL_UpscaleMetalFXScale(NULL, small, 8, 8, smallout, 4, 4) == 0);
	CHECK(CL_UpscaleMetalFXScale(NULL, small, 8, 8, smallout, 16385, 16) == 0);
	CHECK(CL_UpscaleMetalFXScale(NULL, small, 0, 8, smallout, 16, 16) == 0);
	CHECK(CL_UpscaleMetalFXScale(NULL, NULL, 8, 8, smallout, 16, 16) == 0);
	CHECK(CL_UpscaleMetalFXScale(NULL, small, 8, 8, NULL, 16, 16) == 0);

	/* after the run nothing is scaled */
	CL_UpscaleMetalFXEnd();
	CHECK(CL_UpscaleMetalFXScale(NULL, small, 8, 8, smallout, 16, 16) == 0);
	CL_UpscaleMetalFXEnd();

	printf("upscale_metalfx_test: %d passed, %d failed\n", passed, failed);

	return failed ? 1 : 0;
}
