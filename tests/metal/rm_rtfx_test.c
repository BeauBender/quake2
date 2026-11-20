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
 * The host test of ref_metal's effect table (TASK179, AR-17): the order
 * every walk takes, each row's name and switch, the switches against the
 * hardware gate's table (rm_rtgate.c: one source, so neither can change
 * alone), and the rule of the pipeline a 3D draw takes (rm_rtfx_pipe).
 * Built by tests/check-ref-metal.sh with rm_rtfx.c (RM_RTFX_HOST: the
 * rows' entry points NULL, so no backend is linked) and rm_rtgate.c.
 *
 * =======================================================================
 */

#include <stdio.h>
#include <string.h>

#include "../../src/client/refresh/metal/header/local.h"
#include "../../src/client/refresh/metal/header/rm_gbuf.h"
#include "../../src/client/refresh/metal/header/rm_rtfx.h"
#include "../../src/client/refresh/metal/header/rm_rtgate.h"

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

/* the order: the shadows, the AO, then the sun (it adds to the AO's light), the water last (it reads the resolve) */
static void
test_order(void)
{
	static const int want[RM_RTFX_COUNT] = { RM_RTFX_SHADOWS, RM_RTFX_AO, RM_RTFX_SUN, RM_RTFX_WATER };
	int seen[RM_RTFX_COUNT];
	int k;

	memset(seen, 0, sizeof(seen));

	for (k = 0; k < RM_RTFX_COUNT; k++)
	{
		CHECK(rm_rtfx_order[k] == want[k]);
		CHECK((rm_rtfx_order[k] >= 0) && (rm_rtfx_order[k] < RM_RTFX_COUNT));

		if ((rm_rtfx_order[k] >= 0) && (rm_rtfx_order[k] < RM_RTFX_COUNT))
		{
			seen[rm_rtfx_order[k]]++;
		}
	}

	for (k = 0; k < RM_RTFX_COUNT; k++)
	{
		CHECK(seen[k] == 1);
	}
}

/* each row's name and switch, and the ids out of range */
static void
test_rows(void)
{
	static const char *const name[RM_RTFX_COUNT] = { "shadows", "ao", "sun", "water" };
	static const char *const sw[RM_RTFX_COUNT] = { "r_rt_shadows", "r_rt_ao", "r_rt_sun", "r_rt_water" };
	int id;

	for (id = 0; id < RM_RTFX_COUNT; id++)
	{
		CHECK(rm_rtfx_name(id) && !strcmp(rm_rtfx_name(id), name[id]));
		CHECK(rm_rtfx_switch(id) && !strcmp(rm_rtfx_switch(id), sw[id]));
		CHECK(rm_rtfx_is_switch(sw[id]));
	}

	CHECK(rm_rtfx_name(-1) == NULL);
	CHECK(rm_rtfx_name(RM_RTFX_COUNT) == NULL);
	CHECK(rm_rtfx_switch(-1) == NULL);
	CHECK(rm_rtfx_switch(RM_RTFX_COUNT) == NULL);
	CHECK(!rm_rtfx_is_switch(NULL));
	CHECK(!rm_rtfx_is_switch(""));
	CHECK(!rm_rtfx_is_switch("r_rt_ao_show"));
	CHECK(!rm_rtfx_is_switch("r_rt_gbuffer"));
}

/* the gate's effect switches are the table's, row for row: one changed without the other fails here */
static void
test_gate(void)
{
	int id, i, effect, n = 0;
	const char *name;

	for (id = 0; id < RM_RTFX_COUNT; id++)
	{
		i = rm_rtg_find(rm_rtfx_switch(id));
		effect = 0;
		CHECK(i >= 0);
		CHECK(rm_rtg_cvar(i, &effect) != NULL);
		CHECK(effect == 1);
	}

	for (i = 0; (name = rm_rtg_cvar(i, &effect)) != NULL; i++)
	{
		CHECK(effect == rm_rtfx_is_switch(name));
		n += effect;
	}

	CHECK(n == RM_RTFX_COUNT);

	for (i = 0; (name = rm_rtg_effect(i)) != NULL; i++)
	{
		CHECK(rm_rtfx_is_switch(name));
	}

	CHECK(i == RM_RTFX_COUNT);
}

static int fw_calls, fw_have;

static int
fw(rmb_rtfx_io_t *io)
{
	(void)io;
	fw_calls++;
	return fw_have;
}

/* the rule of rmb_gbuf_pipe3d: forward asked only for a blended world face in the forward pass */
static void
test_pipe(void)
{
	int p, s;

	for (s = 0; s <= 1; s++)
	{
		for (fw_have = 0; fw_have <= 1; fw_have++)
		{
			for (p = 0; p < RM_PIPE_3D_COUNT; p++)
			{
				fw_calls = 0;
				CHECK(rm_rtfx_pipe(RMB_GB_PASS_NONE, p, s, fw, NULL) == RM_RTFX_PIPE_STOCK);
				CHECK(rm_rtfx_pipe(RMB_GB_PASS_RASTER, p, s, fw, NULL) ==
					(s ? RM_RTFX_PIPE_RASTER_SURF : RM_RTFX_PIPE_RASTER));
				CHECK(rm_rtfx_pipe(RMB_GB_PASS_OTHER, p, s, fw, NULL) == RM_RTFX_PIPE_NONE);
				CHECK(fw_calls == 0);

				CHECK(rm_rtfx_pipe(RMB_GB_PASS_MOTION, p, s, fw, NULL) ==
					(((p == RM_PIPE_WORLD_TEXC_BLEND) && fw_have) ? RM_RTFX_PIPE_FORWARD : RM_RTFX_PIPE_MOTION));
				CHECK(fw_calls == (p == RM_PIPE_WORLD_TEXC_BLEND));
			}

			/* a pipe out of range takes none, whatever the pass, and asks nothing */
			fw_calls = 0;
			CHECK(rm_rtfx_pipe(RMB_GB_PASS_NONE, -1, s, fw, NULL) == RM_RTFX_PIPE_NONE);
			CHECK(rm_rtfx_pipe(RMB_GB_PASS_MOTION, RM_PIPE_3D_COUNT, s, fw, NULL) == RM_RTFX_PIPE_NONE);
			CHECK(rm_rtfx_pipe(RMB_GB_PASS_RASTER, -1, s, fw, NULL) == RM_RTFX_PIPE_NONE);
			CHECK(fw_calls == 0);
		}
	}

	/* the blended world face without a forward function: the motion pass's own */
	CHECK(rm_rtfx_pipe(RMB_GB_PASS_MOTION, RM_PIPE_WORLD_TEXC_BLEND, 0, NULL, NULL) == RM_RTFX_PIPE_MOTION);
}

/* the walks over rows without entry points (RM_RTFX_HOST) do nothing and say no */
static void
test_walks(void)
{
	rm_rtfx_mark(NULL);
	rm_rtfx_trace(NULL);
	CHECK(rm_rtfx_resolve(NULL) == 0);
	rm_rtfx_show(NULL);
	rm_rtfx_post(NULL);
	CHECK(rm_rtfx_forward(NULL) == 0);
	rm_rtfx_bind(NULL);
	rm_rtfx_view_end();
	rm_rtfx_init(NULL);
	rm_rtfx_shutdown();
	CHECK(rm_rtfx_wanted() == 0);
	rm_rtfx_frame(NULL, 0);
	CHECK(rm_rtfx_active() == 0);
	CHECK(rm_rtfx_show_only() == 0);
	rm_rtfx_view(NULL);
}

int
main(void)
{
	test_order();
	test_rows();
	test_gate();
	test_pipe();
	test_walks();
	printf("rm_rtfx_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
