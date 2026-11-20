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
 * An alias a server line defined runs as the server's text whenever it
 * is expanded, until the player defines it again (FIX156). Without
 * this, a server stufftexts `alias x "<gated command>"` and the player's
 * own `x`, typed, bound or named by another alias, runs the server's
 * body as the player's, past every command that reads
 * Cbuf_ExecutingServerText() (FIX151).
 *
 * The probe is FIX151's: the real upscale gate, CL_UpscaleAllowed, on
 * the local server over loopback with demo byte 0, the server showing
 * a cinematic, where only the line's source can refuse it. Every case
 * drives the real Cmd_Alias_f, Cmd_ExecuteString and Cbuf_Execute.
 *
 * =======================================================================
 */

#include "test.h"
#include "../../src/server/header/server.h"
#include "../../src/client/header/upscale.h"

extern sizebuf_t cmd_text;      /* src/common/cmdparser.c */
extern char defer_text_buf[];   /* src/common/cmdparser.c */

static int probe_runs;    /* the probe ran */
static int probe_allowed; /* and the gate let it through */
static int saved_state;

/* Stands in for upscale_textures. */
static void
T_AsProbe_f(void)
{
	probe_runs++;

	if (CL_UpscaleAllowed(1, 1, 0))
	{
		probe_allowed++;
	}
}

static void
as_setup(void)
{
	if (!Cmd_Exists("t_as_probe"))
	{
		Cmd_AddCommand("t_as_probe", T_AsProbe_f);
	}

	SZ_Clear(&cmd_text);
	defer_text_buf[0] = 0;
	probe_runs = 0;
	probe_allowed = 0;

	/* a cinematic: the server state alone refuses nothing */
	saved_state = Com_ServerState();
	Com_SetServerState(ss_cinematic);
}

static void
as_teardown(void)
{
	Com_SetServerState(saved_state);
	SZ_Clear(&cmd_text);
}

/* A line the server sent, run to the end. */
static void
server_line(const char *text)
{
	Cbuf_AddServerText(text);
	Cbuf_Execute();
}

/* A line the player typed, run to the end. */
static void
player_line(const char *text)
{
	Cbuf_AddText(text);
	Cbuf_Execute();
}

/* The server defines an alias, the player runs it: the body is the
   server's. */
TEST(alias_server_defined_runs_as_server)
{
	as_setup();

	server_line("alias t_as1 \"t_as_probe\"\n");
	player_line("t_as1\n");

	T_EQ_INT(probe_runs, 1);    /* the body ran */
	T_EQ_INT(probe_allowed, 0); /* as the server's */

	as_teardown();
}

/* The player's alias names the server's: the server's body is still
   the server's. */
TEST(alias_player_alias_naming_server_alias)
{
	as_setup();

	server_line("alias t_as2 \"t_as_probe\"\n");
	player_line("alias t_as2p \"t_as2\"\n");
	player_line("t_as2p\n");

	T_EQ_INT(probe_runs, 1);
	T_EQ_INT(probe_allowed, 0);

	as_teardown();
}

/* The player's own alias, with the same body, runs as the player's. */
TEST(alias_player_defined_runs_as_player)
{
	as_setup();

	player_line("alias t_as3 \"t_as_probe\"\n");
	player_line("t_as3\n");

	T_EQ_INT(probe_runs, 1);
	T_EQ_INT(probe_allowed, 1);

	as_teardown();
}

/* Once the player defines it again, it is the player's. */
TEST(alias_player_redefines_server_alias)
{
	as_setup();

	server_line("alias t_as4 \"t_as_probe\"\n");
	player_line("t_as4\n");
	T_EQ_INT(probe_runs, 1);
	T_EQ_INT(probe_allowed, 0);

	player_line("alias t_as4 \"t_as_probe\"\n");
	player_line("t_as4\n");
	T_EQ_INT(probe_runs, 2);
	T_EQ_INT(probe_allowed, 1);

	/* and the server's next definition is the server's again */
	server_line("alias t_as4 \"t_as_probe;t_as_probe\"\n");
	player_line("t_as4\n");
	T_EQ_INT(probe_runs, 4);
	T_EQ_INT(probe_allowed, 1);

	as_teardown();
}

/* An alias that a server-defined body defines is the server's. */
TEST(alias_defined_by_server_body)
{
	as_setup();

	server_line("alias t_as5 \"alias t_as5y t_as_probe\"\n");
	player_line("t_as5\n");   /* the server's body defines t_as5y */
	player_line("t_as5y\n");

	T_EQ_INT(probe_runs, 1);
	T_EQ_INT(probe_allowed, 0);

	as_teardown();
}

/* A server line that writes the body the player's alias already has
   does not take it over: running the player's own configuration again
   (`exec autoexec.cfg`) leaves it the player's. */
TEST(alias_server_same_body_stays_player)
{
	as_setup();

	player_line("alias t_as6 \"t_as_probe\"\n");
	server_line("alias t_as6 \"t_as_probe\"\n");
	player_line("t_as6\n");

	T_EQ_INT(probe_runs, 1);
	T_EQ_INT(probe_allowed, 1);

	as_teardown();
}

/* The server's body does not change the source of the player's text
   queued behind it, on the same line or after it. */
TEST(alias_server_body_leaves_player_text_after_it)
{
	as_setup();

	server_line("alias t_as7 \"t_as_probe\"\n");
	player_line("t_as7;t_as_probe\nt_as_probe\n");

	T_EQ_INT(probe_runs, 3);
	T_EQ_INT(probe_allowed, 2); /* the player's two */

	as_teardown();
}

/* A server alias expanded outside Cbuf_Execute, through
   Cmd_ExecuteString as EXEC_NOW does, still queues its body as the
   server's. */
TEST(alias_server_defined_exec_now)
{
	as_setup();

	server_line("alias t_as8 \"t_as_probe\"\n");
	Cbuf_ExecuteText(EXEC_NOW, "t_as8");
	Cbuf_Execute();

	T_EQ_INT(probe_runs, 1);
	T_EQ_INT(probe_allowed, 0);

	as_teardown();
}
