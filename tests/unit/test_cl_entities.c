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
 * The BFG ball's light ramp in CL_AddPacketEntities
 * (src/client/cl_entities.c): the animation frame the server sends picks
 * the light intensity from a six-entry table, and a frame outside the
 * table gets no ramp light instead of a read past the table (a global
 * the address sanitizer watches).
 *
 * =======================================================================
 */

#include <limits.h>

#include "test.h"
#include "../../src/client/header/client.h"
#include "stub_client.h"

/* Public in cl_entities.c; cl_main.c declares it locally the same way. */
void CL_AddPacketEntities(frame_t *frame);

#define ENT_NUM 5

/* Runs a frame with one BFG ball at the given animation frame. */
static void
add_bfg_ball(int animframe)
{
	frame_t frame;

	stub_client_cvars();
	memset(&cl, 0, sizeof(cl));
	memset(&frame, 0, sizeof(frame));
	CL_AllocEntity(ENT_NUM);

	memset(&cl_parse_entities[0], 0, sizeof(cl_parse_entities[0]));
	cl_parse_entities[0].number = ENT_NUM;
	cl_parse_entities[0].modelindex = 1;
	cl_parse_entities[0].effects = EF_BFG;
	cl_parse_entities[0].frame = animframe;

	frame.num_entities = 1;
	frame.parse_entities = 0;

	memset(&stub_calls, 0, sizeof(stub_calls));
	CL_AddPacketEntities(&frame);
}

/* Every frame of the table gets its own ramp light. */
TEST(cl_bfg_ramp_inside_table)
{
	static const int ramp[6] = {300, 400, 600, 300, 150, 75};
	int i;

	for (i = 0; i < 6; i++)
	{
		add_bfg_ball(i);
		T_EQ_INT(stub_calls.lights, 1);
		T_EQ_INT(stub_calls.last_light, ramp[i]);
	}
}

/* One past the table, and the far ends of the range: no light, no read. */
TEST(cl_bfg_ramp_outside_table)
{
	static const int frames[] = {6, 7, 255, 65535, INT_MAX, -1, INT_MIN};
	size_t i;

	for (i = 0; i < sizeof(frames) / sizeof(frames[0]); i++)
	{
		add_bfg_ball(frames[i]);
		T_EQ_INT(stub_calls.lights, 0);
	}
}

/* Sanitizer smoke test: one entity for every effect bit and every render
   effect bit, each with all four models set, drawn in one frame without a
   stray access (the sanitizers watch). It only asserts that some light was
   added; the exact light per effect bit is in the test below. */
TEST(cl_entities_every_effect_bit)
{
	frame_t frame;
	int i;

	stub_client_cvars();
	memset(&cl, 0, sizeof(cl));
	memset(&frame, 0, sizeof(frame));
	cl.playernum = 62;
	cl.time = 1000;
	cl.frame.serverframe = 10;
	CL_AllocEntity(64);

	for (i = 0; i < 64; i++)
	{
		entity_state_t *s = &cl_parse_entities[i];

		memset(s, 0, sizeof(*s));
		s->number = i + 1;
		s->modelindex = (i == 40) ? 255 : 1;
		s->modelindex2 = 2;
		s->modelindex3 = 3;
		s->modelindex4 = 4;
		s->frame = i % 6;

		if (i < 32)
		{
			s->effects = 1u << i;
		}
		else
		{
			s->renderfx = 1 << (i - 32);
		}
	}

	frame.num_entities = 64;
	memset(&stub_calls, 0, sizeof(stub_calls));
	CL_AddPacketEntities(&frame);
	T_CHECK(stub_calls.lights > 0);
}

/* Each effect bit alone, on its own entity: exactly the effects that
   carry a light add one, the rest add none. */
TEST(cl_entities_one_light_per_lit_effect_bit)
{
	static const unsigned int lit = EF_ROCKET | EF_BLASTER | EF_HYPERBLASTER |
		EF_BFG | EF_FLAG1 | EF_FLAG2 | EF_IONRIPPER | EF_BLUEHYPERBLASTER |
		EF_SPINNINGLIGHTS | EF_PLASMA | EF_TRAP | EF_TRACKER |
		EF_TAGTRAIL | EF_TRACKERTRAIL;
	int bit;

	for (bit = 0; bit < 32; bit++)
	{
		frame_t frame;
		unsigned int effect = 1u << bit;
		int want = (lit & effect) ? 1 : 0;

		stub_client_cvars();
		memset(&cl, 0, sizeof(cl));
		memset(&frame, 0, sizeof(frame));
		cl.playernum = 62;
		cl.time = 1000;
		CL_AllocEntity(ENT_NUM);

		memset(&cl_parse_entities[0], 0, sizeof(cl_parse_entities[0]));
		cl_parse_entities[0].number = ENT_NUM;
		cl_parse_entities[0].modelindex = 1;
		cl_parse_entities[0].effects = effect;

		frame.num_entities = 1;
		frame.parse_entities = 0;

		memset(&stub_calls, 0, sizeof(stub_calls));
		CL_AddPacketEntities(&frame);

		if (stub_calls.lights != want)
		{
			t_fail(__FILE__, __LINE__, "effect bit %d: %d lights, want %d",
				bit, stub_calls.lights, want);
		}
	}
}
