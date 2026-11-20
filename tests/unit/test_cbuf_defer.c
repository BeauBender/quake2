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
 * The defer buffer a map change parks the command buffer in (FIX148,
 * after FIX113).
 *
 * - A demo's text (its svc_stufftext, tagged as the server's) ends with
 *   the demo: when SV_Map replaces a demo with a server that is not a
 *   demo, the demo's text does not reach the new game through
 *   Cbuf_CopyToDefer and SV_Begin_f's Cbuf_InsertFromDefer. A local
 *   game's own map change and a demo that follows a demo keep it, as
 *   before. The player's text is kept in all three. A demo started by
 *   `map x.dm2` (attractloop 0) counts as a demo, through gamemap and
 *   map alike, and its text also ends when killserver ends the demo
 *   (FIX151).
 * - Cbuf_CopyToDefer stays inside defer_text_buf when the command
 *   buffer is full to the last byte (Cbuf_InsertText can fill it
 *   exactly). The overrun shows under `make ASAN=1 test`.
 *
 * Each case runs in a forked child: SV_Map rebuilds the server state,
 * and the full buffer must not leak into later tests.
 *
 * =======================================================================
 */

#include "test.h"
#include "../../src/server/header/server.h"

extern sizebuf_t cmd_text;      /* src/common/cmdparser.c */
extern char defer_text_buf[];   /* src/common/cmdparser.c */

static int server_runs;
static int player_runs;

static void
T_DtServer_f(void)
{
	server_runs++;
}

static void
T_DtPlayer_f(void)
{
	player_runs++;
}

/* A game module that spawns nothing: SV_SpawnServer calls these. */
static void
fake_spawn(const char *mapname, char *entities, const char *spawnpoint)
{
}

static void
fake_frame(void)
{
}

/* How the map change is made. */
#define VIA_SV_MAP  0  /* SV_Map itself */
#define VIA_GAMEMAP 1  /* the gamemap command (SV_GameMap_f) */
#define VIA_MAP     2  /* what the map command (SV_Map_f) does */

typedef struct
{
	qboolean from_demo;  /* the running server is a demo (ss_demo) */
	qboolean to_demo;    /* the new one is */
	int want_server;     /* runs of the server's text in the new game */
	int via;             /* VIA_SV_MAP, VIA_GAMEMAP or VIA_MAP */
	qboolean map_dm2;    /* the demo was started by `map x.dm2`, so
	                        sv.attractloop is 0 (FIX151) */
} t_change_t;

static void
map_change_child(void *arg)
{
	const t_change_t *c = arg;
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
	sv.state = c->from_demo ? ss_demo : ss_game;
	sv.attractloop = c->from_demo && !c->map_dm2;
	Com_SetServerState(sv.state);

	/* SV_Map broadcasts "changing" before SV_SpawnServer sets this up. */
	SZ_Init(&sv.multicast, sv.multicast_buf, sizeof(sv.multicast_buf));

	Cmd_AddCommand("t_dt_server", T_DtServer_f);
	Cmd_AddCommand("t_dt_player", T_DtPlayer_f);

	SZ_Clear(&cmd_text);
	defer_text_buf[0] = 0;
	server_runs = 0;
	player_runs = 0;

	/* The rest of a server line's text, and the player's after it. */
	Cbuf_AddServerText("t_dt_server\n");
	Cbuf_AddText("t_dt_player\n");

	/* The map change, then what the ss_game branch of SV_Map and the
	   client's arrival in the new game (SV_Begin_f) do with the buffer.
	   The level is a picture so that SV_SpawnServer needs no BSP. */
	if (c->via == VIA_GAMEMAP)
	{
		char cmd[] = "gamemap t_demotext.pcx";

		Cmd_ExecuteString(cmd);
	}
	else if (c->via == VIA_MAP)
	{
		/* SV_Map_f sets sv.state, not Com_ServerState(), to ss_dead
		   before SV_GameMap_f. loadgame only keeps SV_Map from calling
		   SV_InitGame, which would load a real game module; the drop
		   of a demo's text does not read it. */
		sv.state = ss_dead;
		SV_Map(false, "t_demotext.pcx", true, false);
	}
	else
	{
		SV_Map(c->to_demo, "t_demotext.pcx", false, false);
	}

	T_EQ_INT(Com_ServerState(), ss_pic);  /* the change was made */
	Cbuf_CopyToDefer();
	Cbuf_InsertFromDefer();
	Cbuf_Execute();

	T_EQ_INT(server_runs, c->want_server);
	T_EQ_INT(player_runs, 1);
}

static void
map_change(t_change_t c)
{
	char out[8192];
	int status = t_expect_exit(map_change_child, &c, out, sizeof(out));

	if (status != 0)
	{
		printf("%s", out);
	}

	T_EQ_INT(status, 0);
}

TEST(cbuf_demo_text_dropped_by_local_game)
{
	map_change((t_change_t){ true, false, 0 });
}

TEST(cbuf_demo_text_kept_by_next_demo)
{
	map_change((t_change_t){ true, true, 1 });
}

TEST(cbuf_local_text_kept_by_map_change)
{
	map_change((t_change_t){ false, false, 1 });
}

/* A `map x.dm2` demo (ss_demo, attractloop 0) whose text changes the
   level with gamemap: the text ends with the demo (FIX151). */
TEST(cbuf_map_dm2_text_dropped_by_gamemap)
{
	map_change((t_change_t){ true, false, 0, VIA_GAMEMAP, true });
}

/* The same through map, which sets sv.state to ss_dead first. */
TEST(cbuf_map_dm2_text_dropped_by_map)
{
	map_change((t_change_t){ true, false, 0, VIA_MAP, true });
}

/* A local game's own map keeps its text, as before. (Its gamemap would
   save the level first, which needs a real game module.) */
TEST(cbuf_local_text_kept_by_map)
{
	map_change((t_change_t){ false, false, 1, VIA_MAP, false });
}

static int probe_runs;

static void
T_DtProbe_f(void)
{
	probe_runs++;
}

/* killserver in a server's own text: SV_Shutdown. */
static void
killserver_child(void *arg)
{
	const int *demo = arg;
	static edict_t edicts[1];
	static game_export_t fake;

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

	/* SV_Shutdown frees them. */
	Cvar_FullSet("maxclients", "1", CVAR_SERVERINFO | CVAR_LATCH);
	svs.clients = Z_Malloc(sizeof(client_t));
	svs.initialized = true;
	sv.state = ss_game;
	sv.attractloop = false;
	SZ_Init(&sv.multicast, sv.multicast_buf, sizeof(sv.multicast_buf));

	/* `map x.dm2`: a demo with attractloop 0, which the client takes
	   from the demo's serverdata. A local game otherwise. */
	if (*demo)
	{
		SV_Map(false, "t_kill.dm2", false, false);
		T_EQ_INT(Com_ServerState(), ss_demo);
	}
	else
	{
		Com_SetServerState(ss_game);
	}

	Cmd_AddCommand("t_probe", T_DtProbe_f);
	SZ_Clear(&cmd_text);
	defer_text_buf[0] = 0;
	probe_runs = 0;

	Cbuf_AddServerText("killserver\nt_probe\n");
	Cbuf_Execute();

	T_EQ_INT(Com_ServerState(), ss_dead);  /* killserver ran */
	T_EQ_INT(probe_runs, *demo ? 0 : 1);
}

static void
killserver(int demo)
{
	char out[8192];
	int status = t_expect_exit(killserver_child, &demo, out, sizeof(out));

	if (status != 0)
	{
		printf("%s", out);
	}

	T_EQ_INT(status, 0);
}

/* A demo's text ends with the demo however it ends (FIX151). */
TEST(cbuf_demo_text_dropped_by_killserver)
{
	killserver(1);
}

/* A local game's own killserver is not a demo's: its text is kept. */
TEST(cbuf_local_text_kept_by_killserver)
{
	killserver(0);
}

static void
full_buffer_child(void *arg)
{
	static char text[32768];
	int half = (int)sizeof(text) / 2;

	SZ_Clear(&cmd_text);
	memset(text, 'a', sizeof(text));

	/* Cbuf_AddText refuses a buffer that would be full; Cbuf_InsertText
	   writes the old text back through SZ_Write, which allows it. */
	text[half] = 0;
	Cbuf_AddText(text);
	text[half] = 'a';
	text[cmd_text.maxsize - half] = 0;
	Cbuf_InsertText(text);
	T_EQ_INT(cmd_text.cursize, cmd_text.maxsize);

	Cbuf_CopyToDefer();
	T_EQ_INT(cmd_text.cursize, 0);
	T_EQ_INT(defer_text_buf[cmd_text.maxsize - 1], 'a');
}

TEST(cbuf_copy_to_defer_full_buffer)
{
	char out[8192];
	int status = t_expect_exit(full_buffer_child, NULL, out, sizeof(out));

	if (status != 0)
	{
		printf("%s", out);
	}

	T_EQ_INT(status, 0);
}
