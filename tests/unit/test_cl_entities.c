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
