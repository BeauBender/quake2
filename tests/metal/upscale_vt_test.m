/*
 * Host test of the vt backend of upscale_textures (TASK167),
 * src/client/cl_upscale_vt.m, built alone with UPSCALE_VT_TEST by
 * tests/check-upscale-vt.sh. It never fetches the model: the scaling
 * cases run only where the model is on the machine already.
 *
 * - nothing is scaled outside a run;
 * - the test switch makes isSupported read NO: the facts say so, the
 *   menu's check gives the reason, a run does not start;
 * - with the model there: a 24 x 24 and a 72 x 72 tile (16 and 64 plus
 *   the four-texel wrap border) at 2x and 4x, the same bytes twice with
 *   another size between, alpha 255 everywhere; a wrong output size is
 *   refused; nothing is scaled after the run;
 * - a refused run's reason is what the menu gives after.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "header/upscale_vt.h"

extern int upscale_vt_test_unsupported;

static int passed, failed;

static void
check(int ok, const char *what)
{
	printf("%s: %s\n", ok ? "ok" : "FAIL", what);
	ok ? passed++ : failed++;
}

static unsigned long long
fnv(const unsigned char *p, size_t n)
{
	unsigned long long h = 1469598103934665603ULL;

	while (n--)
	{
		h = (h ^ *p++) * 1099511628211ULL;
	}

	return h;
}

/* hard-edged blocks, as a palette tile has */
static void
tile(unsigned char *t, int w, int h)
{
	int x, y;

	for (y = 0; y < h; y++)
	{
		for (x = 0; x < w; x++)
		{
			unsigned char *p = t + ((size_t)y * w + x) * 4;

			p[0] = (unsigned char)(((x / 4) * 37 + (y / 4) * 11) & 255);
			p[1] = (unsigned char)(((x / 8) * 53) & 255);
			p[2] = (unsigned char)(((y / 3) * 29) & 255);
			p[3] = ((x * 7 + y) % 7) ? 255 : 0;
		}
	}
}

/* one scale of a side by side tile at factor; its hash, 0 on failure */
static unsigned long long
scale(int side, int factor, int *alpha)
{
	const int o = side * factor;
	unsigned char *in = malloc((size_t)side * side * 4);
	unsigned char *out = malloc((size_t)o * o * 4);
	unsigned long long h = 0;
	size_t i;

	*alpha = 1;
	tile(in, side, side);
	memset(out, 7, (size_t)o * o * 4);

	if (CL_UpscaleVTScale(NULL, in, side, side, out, o, o))
	{
		h = fnv(out, (size_t)o * o * 4);

		for (i = 0; i < (size_t)o * o; i++)
		{
			*alpha &= (out[i * 4 + 3] == 255);
		}
	}

	free(in);
	free(out);

	return h;
}

/*
 * A flat 72 x 72 tile of one colour scaled at the run's factor: 1 when
 * the mean of each colour channel out is within 12 levels of the colour
 * in (a scaler that keeps colours keeps a flat tile flat), the means in
 * mean; 0 when the scale fails or a channel moves.
 */
static int
flat(const unsigned char rgb[3], int factor, int mean[3])
{
	const int s = 72, o = s * factor;
	unsigned char *in = malloc((size_t)s * s * 4);
	unsigned char *out = malloc((size_t)o * o * 4);
	long long sum[3] = { 0, 0, 0 };
	size_t i;
	int c, ok;

	for (i = 0; i < (size_t)s * s; i++)
	{
		in[i * 4] = rgb[0];
		in[i * 4 + 1] = rgb[1];
		in[i * 4 + 2] = rgb[2];
		in[i * 4 + 3] = 255;
	}

	ok = CL_UpscaleVTScale(NULL, in, s, s, out, o, o);

	for (i = 0; ok && (i < (size_t)o * o); i++)
	{
		for (c = 0; c < 3; c++)
		{
			sum[c] += out[i * 4 + c];
		}
	}

	for (c = 0; c < 3; c++)
	{
		mean[c] = ok ? (int)(sum[c] / ((long long)o * o)) : -1;
		ok = ok && (abs(mean[c] - rgb[c]) <= 12);
	}

	free(in);
	free(out);

	return ok;
}

int
main(void)
{
	upscale_vt_facts_t f;
	unsigned char in[64 * 4], out[16 * 64 * 4];
	char reason[160];
	int require = getenv("UPSCALE_VT_REQUIRE") && !strcmp(getenv("UPSCALE_VT_REQUIRE"), "1");

	@autoreleasepool
	{
		memset(in, 1, sizeof(in));
		check(!CL_UpscaleVTScale(NULL, in, 8, 8, out, 16, 16),
			"nothing is scaled outside a run");

		upscale_vt_test_unsupported = 1;
		CL_UpscaleVTFacts(2, &f);
		check(f.built && !f.supported && !f.config,
			"the switch: the facts say isSupported is NO");
		check(!CL_UpscaleVTAvailable(4, reason, sizeof(reason)) && strlen(reason) > 0,
			"the switch: the menu's check gives a reason");
		printf("reason: %s\n", reason);
		check(!CL_UpscaleVTBegin(2, reason, sizeof(reason)),
			"the switch: a run does not start");
		check(!CL_UpscaleVTFetchStart(4, reason, sizeof(reason)) && strlen(reason) > 0,
			"the switch: no download starts");
		upscale_vt_test_unsupported = 0;

		CL_UpscaleVTFacts(2, &f);
		printf("facts at 2x: os %d, isSupported %d, factor %d, config %d, model %d\n",
			f.os, f.supported, f.factor, f.config, f.model);
		CL_UpscaleVTFacts(4, &f);
		/* the scaling cases need the model, judged at 4x */
		printf("facts at 4x: os %d, isSupported %d, factor %d, config %d, model %d\n",
			f.os, f.supported, f.factor, f.config, f.model);

		if (!f.supported || (f.model != UPSCALE_VT_MODEL_READY))
		{
			printf("skipped: %s\n", !f.supported ? "unsupported here" :
				"the model is not on this machine (the test never fetches it)");
			check(!require, "UPSCALE_VT_REQUIRE is not 1");
		}
		else
		{
			static const int sides[] = { 24, 72 };
			static const unsigned char levels[][3] = {
				{ 0, 0, 0 }, { 32, 32, 32 }, { 128, 128, 128 }, { 200, 200, 200 },
				{ 255, 255, 255 }, { 200, 64, 32 }, { 16, 128, 240 }
			};
			int factor, k, a1, a2;

			for (factor = 2; factor <= 4; factor += 2)
			{
				/* the M6 has 4x only: a factor VideoToolbox lacks is not run */
				CL_UpscaleVTFacts(factor, &f);

				if (!f.factor)
				{
					printf("skipped: VideoToolbox has no %dx here\n", factor);
					check(!CL_UpscaleVTBegin(factor, reason, sizeof(reason)),
						"a run at a factor it lacks does not start");
					check(!CL_UpscaleVTAvailable(factor, reason, sizeof(reason)) &&
						strstr(reason, "has no"), "the menu greys a factor it lacks");
					continue;
				}

				check(CL_UpscaleVTBegin(factor, reason, sizeof(reason)),
					"a run starts");

				for (k = 0; k < 2; k++)
				{
					unsigned long long h1 = scale(sides[k], factor, &a1);
					unsigned long long h2;
					char what[96];

					scale(sides[1 - k], factor, &a2);
					h2 = scale(sides[k], factor, &a2);
					snprintf(what, sizeof(what), "%d x %d at %dx: %016llx, the same twice",
						sides[k], sides[k], factor, h1);
					check(h1 && (h1 == h2), what);
					check(a1 && a2, "alpha 255 everywhere");
				}

				/* greys from black to white, and colours: each channel survives */
				for (k = 0; k < (int)(sizeof(levels) / sizeof(levels[0])); k++)
				{
					int m[3];
					char what[128];
					const int ok = flat(levels[k], factor, m);

					snprintf(what, sizeof(what), "a flat (%d, %d, %d) tile at %dx comes "
						"back near itself: (%d, %d, %d)", levels[k][0], levels[k][1],
						levels[k][2], factor, m[0], m[1], m[2]);
					check(ok, what);
				}

				check(!CL_UpscaleVTScale(NULL, in, 8, 8, out, 8 * factor + 1, 8 * factor),
					"a wrong output size is refused");
				CL_UpscaleVTEnd();
				check(!CL_UpscaleVTScale(NULL, in, 8, 8, out, 8 * factor, 8 * factor),
					"nothing is scaled after the run");
			}
		}

		/*
		 * nothing a run found is remembered: after the runs above (the
		 * M6 refuses 2x), the menu's check at each factor is the class's
		 * list, so a refusal at one factor greys nothing at another
		 */
		{
			int factor;

			for (factor = 2; factor <= 4; factor += 2)
			{
				char what[96];

				CL_UpscaleVTFacts(factor, &f);
				snprintf(what, sizeof(what), "after the runs, the menu's check "
					"at %dx is the class's list (%d)", factor, f.factor);
				check(CL_UpscaleVTAvailable(factor, reason, sizeof(reason)) ==
					(f.supported && f.factor), what);
			}
		}

		check(!CL_UpscaleVTAvailable(3, reason, 12) && (strlen(reason) == 11),
			"a reason cut to the menu's buffer");
		check((CL_UpscaleVTFetchState(reason, sizeof(reason)) == UPSCALE_VT_FETCH_IDLE) &&
			!reason[0], "nothing was fetched: the test never starts a download");
	}

	printf("%d passed, %d failed\n", passed, failed);

	return failed ? 1 : 0;
}
