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
 * Text a server line puts into `nextserver` or `nextdemo` never runs as
 * the player's (FIX153). SV_Nextserver queues `nextserver` from
 * SV_Frame, outside any server line, so it is tagged as the player's;
 * SV_Map copies `nextdemo` into it. A line the server sent may not set
 * either cvar, by any route: `set` with three or four arguments, the
 * bare cvar name, or SV_Map's own write of a `+` suffix, also after a
 * drop earlier in the same line (a demo's map change, or CL_Drop on a
 * client that hosts no server), since a server line stays the server's
 * for its whole run. The stock
 * attract loop, whose `d1`, `d2` and `d3` aliases are the player's text
 * (default.cfg), still advances, also with a demo's refused line in
 * between.
 *
 * Each case drives the real SV_Map, SV_Nextserver and Cbuf_Execute with
 * a game module that spawns nothing, in a forked child.
 *
 * =======================================================================
 */

#include "test.h"
#include "../../src/server/header/server.h"
#include "../../src/client/header/upscale.h"
#include "stub_client.h"

extern sizebuf_t cmd_text;      /* src/common/cmdparser.c */
extern char defer_text_buf[];   /* src/common/cmdparser.c */

/* A game module that spawns nothing: SV_SpawnServer calls these. */
static void
fake_spawn(const char *mapname, char *entities, const char *spawnpoint)
{
}

static void
fake_frame(void)
{
}

static int gated_runs;
static int player_runs;

/* Stands in for upscale_textures, as in test_cbuf_defer.c: it counts the
   runs the real gate lets through on the local server over loopback,
   demo byte 0. On a cinematic only the line's source can refuse it. */
static void
T_NsGated_f(void)
{
	if (CL_UpscaleAllowed(1, 1, 0))
	{
		gated_runs++;
	}
}

static void
T_NsPlayer_f(void)
{
	player_runs++;
}

/* A local server in a game, with an empty command buffer. */
static void
ns_setup(void)
{
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

	/* SV_Shutdown (killserver) frees them. */
	Cvar_FullSet("maxclients", "1", CVAR_SERVERINFO | CVAR_LATCH);
	svs.clients = Z_Malloc(sizeof(client_t));
	svs.initialized = true;
	sv.state = ss_game;
	sv.attractloop = false;
	SZ_Init(&sv.multicast, sv.multicast_buf, sizeof(sv.multicast_buf));

	Cmd_AddCommand("t_ns_gated", T_NsGated_f);
	Cmd_AddCommand("t_ns_player", T_NsPlayer_f);
	SZ_Clear(&cmd_text);
	defer_text_buf[0] = 0;
	gated_runs = 0;
	player_runs = 0;
}

/* What happens when the cinematic ends (SV_DemoCompleted), then the
   player's same line in the state that leaves. */
static void
ns_end_and_check(void)
{
	SV_Nextserver();
	Cbuf_Execute();

	T_EQ_INT(gated_runs, 0);   /* the server's text did not run as ours */

	Cbuf_AddText("t_ns_gated\n");
	Cbuf_Execute();
	T_EQ_INT(gated_runs, 1);   /* the player's does */
}

static void
ns_run(void (*child)(void *), const char *line)
{
	char out[8192];
	int status = t_expect_exit(child, (void *)line, out, sizeof(out));

	if (status != 0)
	{
		printf("%s", out);
	}

	T_EQ_INT(status, 0);
}

/* A server line sets nextserver while a cinematic plays. */
static void
nextserver_child(void *arg)
{
	const char *line = arg;

	ns_setup();
	SV_Map(false, "t_ns.cin", false, false);
	T_EQ_INT(Com_ServerState(), ss_cinematic);

	Cbuf_AddServerText(line);
	Cbuf_Execute();
	T_EQ_STR(Cvar_VariableString("nextserver"), "");  /* never stored */

	ns_end_and_check();
}

TEST(nextserver_set_by_server_text_not_run_as_player)
{
	ns_run(nextserver_child, "set nextserver t_ns_gated\n");
}

/* Cvar_FullSet, which the archive flag also reaches. */
TEST(nextserver_set_archive_by_server_text_not_run_as_player)
{
	ns_run(nextserver_child, "set nextserver t_ns_gated a\n");
}

/* Cvar_Command: SV_Map created the cvar, so its bare name sets it. */
TEST(nextserver_bare_name_by_server_text_not_run_as_player)
{
	ns_run(nextserver_child, "nextserver t_ns_gated\n");
}

/* SV_Map's own write of the `+` suffix, inside the server's line. The
   value would be `gamemap "t_ns_gated"`, so the check is the store. */
TEST(nextserver_map_suffix_by_server_text_not_stored)
{
	ns_run(nextserver_child, "demomap t_ns2.cin+t_ns_gated\n");
}

/* A server line sets nextdemo; the next SV_Map, from the player's side,
   copies it into nextserver. */
static void
nextdemo_child(void *arg)
{
	const char *line = arg;

	ns_setup();
	Cbuf_AddServerText(line);
	Cbuf_Execute();
	T_EQ_STR(Cvar_VariableString("nextdemo"), "");  /* never stored */

	SV_Map(false, "t_ns.cin", false, false);
	T_EQ_INT(Com_ServerState(), ss_cinematic);
	T_EQ_STR(Cvar_VariableString("nextserver"), "");

	ns_end_and_check();
}

TEST(nextdemo_set_by_server_text_not_run_as_player)
{
	ns_run(nextdemo_child, "set nextdemo t_ns_gated\n");
}

/* The archived nextdemo that FIX151 left out of the threat model. */
TEST(nextdemo_set_archive_by_server_text_not_run_as_player)
{
	ns_run(nextdemo_child, "set nextdemo t_ns_gated a\n");
}

/* The stock attract loop: default.cfg's aliases, the player's text. */
static void
attract_child(void *arg)
{
	const char *line = arg;

	ns_setup();
	Cbuf_AddText("alias d1 \"demomap t_d1.cin ; set nextserver d2\"\n");
	Cbuf_AddText("alias d2 \"demomap t_d2.cin ; set nextserver d1 ; t_ns_player\"\n");
	Cbuf_AddText("d1\n");
	Cbuf_Execute();

	T_EQ_INT(Com_ServerState(), ss_cinematic);
	T_EQ_STR(Cvar_VariableString("nextserver"), "d2");

	/* the demo's own line, if any, is refused and changes nothing */
	if (line)
	{
		Cbuf_AddServerText(line);
		Cbuf_Execute();
		T_EQ_STR(Cvar_VariableString("nextserver"), "d2");
	}

	SV_Nextserver();
	Cbuf_Execute();

	T_EQ_INT(player_runs, 1);  /* d2 ran */
	T_EQ_STR(Cvar_VariableString("nextserver"), "d1");
}

TEST(nextserver_stock_attract_loop_advances)
{
	ns_run(attract_child, NULL);
}

TEST(nextserver_stock_attract_loop_advances_past_server_text)
{
	ns_run(attract_child, "set nextserver t_ns_gated\n");
}

/*
 * A server line that drops stays the server's for the rest of its run
 * (FIX153 round 2). SV_Map drops server text before it writes the `+`
 * suffix in two cases: a demo's own map change (FIX148, FIX151) and,
 * on a client that hosts no server, SV_InitGame's CL_Drop. The suffix
 * here would queue `gamemap "t_ns.cin"`, then t_ns_gated as the
 * player's line, on a cinematic.
 */
#define NS_SUFFIX "t_ns2.cin+t_ns.cin\";t_ns_gated;\""

/* The attract loop's demomap plays a cinematic with attractloop set;
   the demo's own gamemap drops in SV_Map. */
static void
demo_gamemap_child(void *arg)
{
	ns_setup();
	SV_Map(true, "t_ns.cin", false, false);
	T_EQ_INT(Com_ServerState(), ss_cinematic);

	Cbuf_AddServerText("gamemap " NS_SUFFIX "\n");
	Cbuf_Execute();
	T_EQ_INT(Com_ServerState(), ss_cinematic);
	T_EQ_STR(Cvar_VariableString("nextserver"), "");  /* never stored */

	ns_end_and_check();
}

TEST(nextserver_gamemap_suffix_after_demo_drop_not_stored)
{
	ns_run(demo_gamemap_child, NULL);
}

/* A demo started by `map x.dm2`: ss_demo, attractloop 0. */
static void
demo_state_child(void *arg)
{
	ns_setup();
	sv.state = ss_demo;
	Com_SetServerState(ss_demo);

	Cbuf_AddServerText("gamemap " NS_SUFFIX "\n");
	Cbuf_Execute();
	T_EQ_INT(Com_ServerState(), ss_cinematic);
	T_EQ_STR(Cvar_VariableString("nextserver"), "");  /* never stored */

	ns_end_and_check();
}

TEST(nextserver_map_suffix_from_demo_text_not_stored)
{
	ns_run(demo_state_child, NULL);
}

/*
 * Stands in for `map` on a client that hosts no server, which the suite
 * cannot run whole: it builds the server objects DEDICATED_ONLY, which
 * compiles SV_InitGame's CL_Drop out, and SV_InitGame loads a real game
 * module (SV_InitGameProgs). In the client, SV_Map calls SV_InitGame,
 * whose CL_Drop runs CL_Disconnect and so Cbuf_DropServerText, before
 * it writes the `+` suffix. This runs that real CL_Drop on a connected
 * client, then the real SV_Map, inside the same line. The local server
 * of ns_setup only keeps SV_Map from calling SV_InitGame.
 */
static void
T_NsRemoteMap_f(void)
{
	CL_Drop();
	SV_Map(false, Cmd_Argv(1), false, false);
}

static void
remote_map_child(void *arg)
{
	ns_setup();
	stub_client_connect();
	Cmd_AddCommand("t_ns_remote_map", T_NsRemoteMap_f);

	/* the line, then one more of the server's that the drop discards */
	Cbuf_AddServerText("t_ns_remote_map " NS_SUFFIX "\nt_ns_player\n");
	Cbuf_Execute();
	T_EQ_INT(stub_client_connected(), 0);  /* CL_Drop disconnected */
	T_EQ_INT(player_runs, 0);              /* and dropped the server's text */
	T_EQ_INT(Com_ServerState(), ss_cinematic);
	T_EQ_STR(Cvar_VariableString("nextserver"), "");  /* never stored */

	ns_end_and_check();
}

TEST(nextserver_map_suffix_after_cl_drop_not_stored)
{
	ns_run(remote_map_child, NULL);
}

/* The primitive: dropping server text removes the server's queued bytes
   and the player's stay, but the line already running stays the
   server's. The next line has its own source. */
static int drop_seen;
static int srv_runs;

static void
T_NsDrop_f(void)
{
	Cbuf_DropServerText();
	drop_seen = Cbuf_ExecutingServerText();
}

static void
T_NsSrv_f(void)
{
	srv_runs++;
}

TEST(cbuf_server_line_stays_server_after_drop)
{
	SZ_Clear(&cmd_text);
	defer_text_buf[0] = 0;
	drop_seen = -1;
	srv_runs = 0;
	player_runs = 0;

	if (!Cmd_Exists("t_ns_drop"))
	{
		Cmd_AddCommand("t_ns_drop", T_NsDrop_f);
		Cmd_AddCommand("t_ns_srv", T_NsSrv_f);
		Cmd_AddCommand("t_ns_player", T_NsPlayer_f);
	}

	Cbuf_AddServerText("t_ns_drop\nt_ns_srv\n");
	Cbuf_AddText("t_ns_player\n");
	Cbuf_Execute();

	T_EQ_INT(drop_seen, 1);    /* still the server's line after its drop */
	T_EQ_INT(srv_runs, 0);     /* the server's queued line was dropped */
	T_EQ_INT(player_runs, 1);  /* the player's was kept */
	T_EQ_INT(Cbuf_ExecutingServerText(), 0);

	/* the player's line that drops stays the player's */
	Cbuf_AddText("t_ns_drop\n");
	Cbuf_Execute();
	T_EQ_INT(drop_seen, 0);
}
