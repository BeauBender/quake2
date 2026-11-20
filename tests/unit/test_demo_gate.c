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
 * The demo leg of the upscale gate (CL_UpscaleAllowed, FIX151): a demo
 * the local server plays counts as a demo whichever command started it
 * (`demomap`, or `map` with a .dm2 name, which leaves attractloop 0) and
 * whatever attractloop byte the demo's own serverdata carries. The
 * client takes cl.attractloop from that byte, so the gate also reads the
 * server's state, which the demo cannot set.
 *
 * Each case drives the real SV_Map with a game module that spawns
 * nothing, in a forked child: SV_Map rebuilds the server state.
 *
 * =======================================================================
 */

#include "test.h"
#include "../../src/server/header/server.h"
#include "../../src/client/header/upscale.h"

/* A game module that spawns nothing: SV_SpawnServer calls these. */
static void
fake_spawn(const char *mapname, char *entities, const char *spawnpoint)
{
}

static void
fake_frame(void)
{
}

typedef struct
{
	qboolean attractloop;  /* SV_Map's: 1 for demomap, 0 for map */
	const char *level;     /* the level SV_Map is given */
	int demo_byte;         /* cl.attractloop, as the demo's serverdata says */
	int want_state;        /* Com_ServerState() after the change */
	int want_allowed;      /* CL_UpscaleAllowed on the local server */
} t_gate_t;

static void
gate_child(void *arg)
{
	const t_gate_t *c = arg;
	static client_t clients[1];
	static edict_t edicts[1];
	static game_export_t fake;

	memset(clients, 0, sizeof(clients));
	memset(edicts, 0, sizeof(edicts));
	memset(&fake, 0, sizeof(fake));
	fake.SpawnEntities = fake_spawn;
	fake.RunFrame = fake_frame;
	fake.Shutdown = fake_frame;
	fake.edicts = edicts;
	fake.edict_size = sizeof(edict_t);
	fake.num_edicts = 1;
	fake.max_edicts = 1;
	ge = &fake;

	Cvar_FullSet("maxclients", "1", CVAR_SERVERINFO | CVAR_LATCH);
	svs.clients = clients;
	svs.initialized = true;
	sv.state = ss_game;
	sv.attractloop = false;

	/* SV_Map broadcasts "changing" before SV_SpawnServer sets this up. */
	SZ_Init(&sv.multicast, sv.multicast_buf, sizeof(sv.multicast_buf));

	SV_Map(c->attractloop, (char *)c->level, false, false);

	T_EQ_INT(Com_ServerState(), c->want_state);
	T_EQ_INT(CL_UpscaleAllowed(1, 1, c->demo_byte), c->want_allowed);
}

static void
gate(t_gate_t c)
{
	char out[8192];
	int status = t_expect_exit(gate_child, &c, out, sizeof(out));

	if (status != 0)
	{
		printf("%s", out);
	}

	T_EQ_INT(status, 0);
}

/* `map x.dm2`: SV_Map(false, ...), so sv.attractloop is 0. */
TEST(demo_gate_map_dm2_is_a_demo)
{
	gate((t_gate_t){ false, "t_gate.dm2", 0, ss_demo, 0 });
}

/* `demomap x.dm2` whose serverdata says attractloop 0. */
TEST(demo_gate_demo_byte_zero_is_a_demo)
{
	gate((t_gate_t){ true, "t_gate.dm2", 0, ss_demo, 0 });
}

/* A stock demo (CL_Record writes attractloop 1): refused, as before. */
TEST(demo_gate_stock_demo_refused)
{
	gate((t_gate_t){ true, "t_gate.dm2", 1, ss_demo, 0 });
	gate((t_gate_t){ false, "t_gate.dm2", 1, ss_demo, 0 });
}

/* A picture level is not a demo: allowed on the local server, as before. */
TEST(demo_gate_pic_level_allowed)
{
	gate((t_gate_t){ false, "t_gate.pcx", 0, ss_pic, 1 });
}
