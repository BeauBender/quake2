/*
 * Host test of scripts/rt/preset-to-table.sh (TASK195, slice 2). It is built with rm_rtgate.c and
 * the rows the script printed for tests/fixtures/rt-preset/good.cfg, included as a header that
 * defines RM_PRES_ENHANCED_ROWS (tests/check-ref-metal.sh). Under the enhanced mode
 * rm_pres_value must return the preset's own values, and every other mode and name must stay the
 * user's. The preset's values are given on the command line as name=value pairs, so the test
 * compares the table with the fixture and not with itself.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/client/refresh/metal/header/rm_rtgate.h"

int
main(int argc, char **argv)
{
	int checks = 0, failures = 0;

	for (int i = 1; i < argc; i++)
	{
		char *eq = strchr(argv[i], '=');
		float want;

		if (!eq)
		{
			continue;
		}
		*eq = 0;
		want = strtof(eq + 1, NULL);

		/* a user value that differs from the preset's, so a pass-through shows */
		checks++;
		if (rm_pres_value(RM_PRES_ENHANCED, argv[i], want + 7.0f) != want)
		{
			printf("FAIL: enhanced %s: got %g, want %g\n", argv[i], rm_pres_value(RM_PRES_ENHANCED, argv[i], want + 7.0f), want);
			failures++;
		}

		/* the other modes decide nothing */
		checks++;
		if (rm_pres_value(RM_PRES_STOCK, argv[i], 99.0f) != 99.0f
			|| rm_pres_value(RM_PRES_RETRO, argv[i], 99.0f) != 99.0f)
		{
			printf("FAIL: %s is decided outside enhanced\n", argv[i]);
			failures++;
		}
	}

	/* a name outside the table stays the user's */
	checks++;
	if (rm_pres_value(RM_PRES_ENHANCED, "vid_gamma", 1.2f) != 1.2f)
	{
		printf("FAIL: vid_gamma is decided\n");
		failures++;
	}

	printf("rm_pres_table_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
