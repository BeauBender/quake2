/*
 * TASK237: the presentation overlay as the menu shows it against the
 * overlay the renderer runs, linked into one host binary by
 * tests/check-ref-metal.sh: src/client/menu/vidrules.c
 * (VidRules_RTOverridden, VidRules_RTEffective, VIDPRES_*) and
 * src/client/refresh/metal/rm_rtgate.c (rm_pres_decide, rm_pres_value,
 * rm_effect_overlaid, RM_PRES_*).
 *
 * For every r_presentation value (the modes, fractions, values that are
 * no mode, not finite ones), every ray-tracing path of the gate (the menu
 * sees only whether there is one, r_rt_available), every control of the
 * ray-tracing screen and every user value, the menu's "decided by the
 * presentation" is the renderer's (rm_effect_overlaid of the mode it runs
 * for the request on that device) and the menu's effective value is the
 * renderer's (rm_pres_value, not 0). The two enums agree: each VIDPRES_
 * value is its RM_PRES_ value, and VIDPRES_COUNT is the number of modes the
 * renderer names. Off ref_metal the menu decides nothing.
 *
 * The test includes its headers as "header/..." found through -I, so the
 * check can build a scratch copy of it (the enum mutants).
 */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "header/rm_rtgate.h"
#include "header/vidrules.h"

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

int
main(void)
{
	const float pres[] = { 0.0f, 1.0f, 2.0f, 3.0f, 0.5f, 1.5f, 2.9f, 3.5f, 3.999f, -0.5f, -1.0f, 4.0f, 7.0f,
		1e9f, NAN, INFINITY, -INFINITY };
	const int paths[] = { RM_RTG_NONE, RM_RTG_COMPUTE, RM_RTG_RENDER };
	const float users[] = { 0.0f, 1.0f, 0.5f, 2.0f, -1.0f };
	const char *const others[] = { "gl1", "gl3", "soft", "", NULL };
	int decided = 0;

	/* the enums: the same value for each mode, and as many modes */
	CHECK(VIDPRES_STOCK == RM_PRES_STOCK, "VIDPRES_STOCK %d, RM_PRES_STOCK %d", VIDPRES_STOCK, RM_PRES_STOCK);
	CHECK(VIDPRES_ENHANCED == RM_PRES_ENHANCED, "VIDPRES_ENHANCED %d, RM_PRES_ENHANCED %d", VIDPRES_ENHANCED,
		RM_PRES_ENHANCED);
	CHECK(VIDPRES_RETRO == RM_PRES_RETRO, "VIDPRES_RETRO %d, RM_PRES_RETRO %d", VIDPRES_RETRO, RM_PRES_RETRO);
	CHECK(VIDPRES_RTPRIMARY == RM_PRES_RTPRIMARY, "VIDPRES_RTPRIMARY %d, RM_PRES_RTPRIMARY %d", VIDPRES_RTPRIMARY,
		RM_PRES_RTPRIMARY);
	for (int m = 0; m < VIDPRES_COUNT; m++)
	{
		CHECK(rm_pres_name(m)[0] != '\0', "mode %d of the menu's %d has no name in the renderer", m, VIDPRES_COUNT);
	}
	CHECK(rm_pres_name(VIDPRES_COUNT)[0] == '\0', "the renderer names a mode %d past the menu's %d", VIDPRES_COUNT,
		VIDPRES_COUNT);

	for (size_t p = 0; p < sizeof(pres) / sizeof(pres[0]); p++)
	{
		for (size_t g = 0; g < sizeof(paths) / sizeof(paths[0]); g++)
		{
			const int mode = rm_pres_decide(pres[p], paths[g], NULL);
			const int available = paths[g] != RM_RTG_NONE;

			for (int i = 0; i < VidRules_RTNum(); i++)
			{
				const char *cvar = VidRules_RTControl(i)->cvar;
				const int mo = VidRules_RTOverridden(i, "metal", available, pres[p]);
				const int ro = rm_effect_overlaid(mode, "", cvar);

				CHECK(mo == ro, "r_presentation %g, path %d (mode %d), %s: the menu says %s, the renderer %s",
					pres[p], paths[g], mode, cvar, mo ? "decided" : "the user's",
					ro ? "decided" : "the user's");
				decided += ro;

				for (size_t u = 0; u < sizeof(users) / sizeof(users[0]); u++)
				{
					const int me = VidRules_RTEffective(i, "metal", available, pres[p], users[u]);
					const int re = rm_pres_value(mode, cvar, users[u]) != 0.0f;

					CHECK(me == re, "r_presentation %g, path %d (mode %d), %s %g: the menu runs %d, the renderer %d",
						pres[p], paths[g], mode, cvar, users[u], me, re);
				}
			}

			/* off ref_metal no presentation runs: the menu decides nothing and shows the user's values */
			for (size_t r = 0; r < sizeof(others) / sizeof(others[0]); r++)
			{
				for (int i = 0; i < VidRules_RTNum(); i++)
				{
					CHECK(!VidRules_RTOverridden(i, others[r], available, pres[p]) &&
						VidRules_RTEffective(i, others[r], available, pres[p], 1.0f) &&
						!VidRules_RTEffective(i, others[r], available, pres[p], 0.0f),
						"renderer %s, r_presentation %g: %s decided", others[r] ? others[r] : "(none)", pres[p],
						VidRules_RTControl(i)->cvar);
				}
			}
		}
	}

	/* the walk reached a decided switch, so a check that compares nothing cannot pass */
	CHECK(decided > 0, "no control is decided under any mode");

	printf("rm_pres_parity_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
