/*
 * TASK190 slice 3: the menu's copy of the scaler's presentation rule
 * (src/client/menu/vidrules.c, VidRules_MetalFXDecided) against the
 * renderer's (rm_rtgate.c: rm_pres_decide, then rm_pres_fx, with the
 * view sized by rm_metalfx.c's rm_fx_scale), linked into one host binary
 * by tests/check-ref-metal.sh. Until TASK237 makes the two one table,
 * this test is what holds them in step for r_metalfx: for every
 * presentation value, with and without ray tracing, every mode mask
 * r_metalfx_modes can hold, every r_metalfx_scale and every user value,
 * the scaler the menu shows is the one the renderer reads, with no
 * development view shown (the menu does not see one) and an output above
 * the family's default rows (the menu does not see the window's height,
 * and assumes so). The menu's reason line is checked too: a decided value
 * has one, and RT-primary's names the missing temporal scaler where the
 * row reads off.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "../../src/client/refresh/metal/header/rm_rtgate.h"
#include "../../src/client/refresh/metal/header/rm_metalfx.h"
#include "../../src/client/menu/header/vidrules.h"

/* vidrules.c's one engine symbol */
int
Q_stricmp(const char *s1, const char *s2)
{
	return strcasecmp(s1, s2);
}

static int checks, failures;

#define CHECK(cond, ...) \
	do { \
		checks++; \
		if (!(cond)) \
		{ \
			failures++; \
			printf("FAIL %s:%d: ", __FILE__, __LINE__); \
			printf(__VA_ARGS__); \
			printf("\n"); \
		} \
	} while (0)

/* an output of 2160 lines, above apple9's default 1080 rows */
#define OUT_H 2160

/* the scaler the renderer reads: the mode it runs for the request on this device, then the overlay */
static float
renderer_fx(float presentation, int available, int modemask, float scale, float user)
{
	const int mode = rm_pres_decide(presentation, available ? RM_RTG_COMPUTE : RM_RTG_NONE, NULL);
	const int below = rm_fx_scale(scale, RM_FX_ROWS_APPLE9, OUT_H, RM_FX_RATIO_MAX) < 1.0f;

	/* r_metalfx_modes holds RM_FX_HAS(m) for each mode m the device has: bit 2 is the temporal scaler */
	return rm_pres_fx(mode, user, (modemask & VIDMFX_TEMPORAL) != 0, 0, below);
}

static float
menu_fx(float presentation, int available, int modemask, float scale, float user, const char **why)
{
	const int d = VidRules_MetalFXDecided("metal", available, presentation, modemask, scale, why);

	return (d >= 0) ? (float)d : user;
}

int
main(void)
{
	const float pres[] = { 0.0f, 1.0f, 2.0f, 3.0f, 3.5f, 2.9f, -0.5f, 4.0f, 7.0f, NAN, INFINITY };
	const float users[] = { 0.0f, 1.0f, 2.0f, 3.0f };
	const float scales[] = { 0.0f, 0.5f, 0.75f, 0.99f, 1.0f, 1.5f, -1.0f, NAN, INFINITY, -INFINITY };

	for (size_t p = 0; p < sizeof(pres) / sizeof(pres[0]); p++)
	{
		for (int available = 0; available <= 1; available++)
		{
			for (int mask = 0; mask <= VIDMFX_ALL; mask++)
			{
				for (size_t s = 0; s < sizeof(scales) / sizeof(scales[0]); s++)
				{
					for (size_t u = 0; u < sizeof(users) / sizeof(users[0]); u++)
					{
						const char *why = "unset";
						const float r = renderer_fx(pres[p], available, mask, scales[s], users[u]);
						const float m = menu_fx(pres[p], available, mask, scales[s], users[u], &why);
						const int d = VidRules_MetalFXDecided("metal", available, pres[p], mask, scales[s], NULL);

						CHECK(r == m, "r_presentation %g, ray tracing %d, modes %d, scale %g, r_metalfx %g: the renderer "
							"reads %g, the menu shows %g", pres[p], available, mask, scales[s], users[u], r, m);
						CHECK((d >= 0) == (why != NULL), "r_presentation %g, ray tracing %d, modes %d, scale %g: decided "
							"%d, reason %s", pres[p], available, mask, scales[s], d, why ? why : "(none)");
					}
				}
			}
		}
	}

	/* RT-primary's row: the temporal scaler where it is and the view is scaled down, else off with the reason */
	{
		const char *why = NULL;

		CHECK(VidRules_MetalFXDecided("metal", 1, 3.0f, 7, 0.5f, &why) == 2 && why &&
			!strcmp(why, "set by the presentation: temporal"), "RT-primary with the temporal scaler: %s",
			why ? why : "(none)");
		CHECK(VidRules_MetalFXDecided("metal", 1, 3.0f, 7, 0.0f, &why) == 2 && why, "RT-primary at the default scale");
		CHECK(VidRules_MetalFXDecided("metal", 1, 3.0f, 7, 1.0f, &why) == -1 && !why, "RT-primary at scale 1");
		CHECK(VidRules_MetalFXDecided("metal", 1, 3.0f, 3, 0.5f, &why) == 0 && why &&
			!strcmp(why, "off: no temporal scaler on this GPU"), "RT-primary without the temporal scaler: %s",
			why ? why : "(none)");
		CHECK(VidRules_MetalFXDecided("metal", 1, 3.0f, 3, 1.0f, &why) == 0 && why, "RT-primary without it at scale 1");
		CHECK(VidRules_MetalFXDecided("metal", 0, 3.0f, 7, 0.5f, &why) == -1 && !why, "RT-primary without ray tracing");
		CHECK(VidRules_MetalFXDecided("gl1", 1, 3.0f, 7, 0.5f, &why) == -1 && !why, "RT-primary under gl1");
		CHECK(VidRules_MetalFXDecided("metal", 0, 2.0f, 7, 0.5f, &why) == 0 && why &&
			!strcmp(why, "set by the presentation"), "Retro: %s", why ? why : "(none)");
		CHECK(VidRules_MetalFXDecided("metal", 1, 1.0f, 7, 0.5f, &why) == -1 && !why, "Enhanced");
	}

	printf("rm_pres_menu_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
