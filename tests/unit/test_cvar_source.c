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
 * A cvar value a server line wrote is the server's (FIX158): `vstr` of
 * it queues it as server text, and a line that expands it with `$`
 * runs as the server's, until the player's text writes the cvar again.
 * Without this, a server stufftexts `set x "<gated command>"` and the
 * player's own `vstr x`, or a line with `$x`, runs the value as the
 * player's, past every command that reads Cbuf_ExecutingServerText()
 * (FIX151). It is FIX156's rule for aliases and binds, for cvars.
 *
 * The probe is FIX151's, as in test_alias_source.c: the real upscale
 * gate, CL_UpscaleAllowed, on the local server with demo byte 0 and the
 * server showing a cinematic, where only the line's source can refuse
 * it. Every case drives the real Cvar_Set_f, Cvar_Command, Cmd_Vstr_f,
 * Cmd_MacroExpandString and Cbuf_Execute.
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
T_CsProbe_f(void)
{
	probe_runs++;

	if (CL_UpscaleAllowed(1, 1, 0))
	{
		probe_allowed++;
	}
}

static void
cs_setup(void)
{
	if (!Cmd_Exists("t_cs_probe"))
	{
		Cmd_AddCommand("t_cs_probe", T_CsProbe_f);
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
cs_teardown(void)
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

/* The server sets a cvar, the player's `vstr` runs it: the value is
   the server's. */
TEST(cvar_server_set_vstr_runs_as_server)
{
	cs_setup();

	server_line("set t_cs1 \"t_cs_probe\"\n");
	player_line("vstr t_cs1\n");

	T_EQ_INT(probe_runs, 1);    /* the value ran */
	T_EQ_INT(probe_allowed, 0); /* as the server's */

	cs_teardown();
}

/* The same through `$` expansion: the command itself, or only an
   argument of it, from the server's value makes the line the
   server's. */
TEST(cvar_server_set_macro_runs_as_server)
{
	cs_setup();

	server_line("set t_cs2 \"t_cs_probe\"\n");
	player_line("$t_cs2\n");

	T_EQ_INT(probe_runs, 1);
	T_EQ_INT(probe_allowed, 0);

	server_line("set t_cs2a \"1\"\n");
	player_line("t_cs_probe $t_cs2a\n");

	T_EQ_INT(probe_runs, 2);
	T_EQ_INT(probe_allowed, 0);

	cs_teardown();
}

/* A value the player set runs as the player's, through both. */
TEST(cvar_player_set_runs_as_player)
{
	cs_setup();

	player_line("set t_cs3 \"t_cs_probe\"\n");
	player_line("vstr t_cs3\n");
	player_line("$t_cs3\n");

	T_EQ_INT(probe_runs, 2);
	T_EQ_INT(probe_allowed, 2);

	cs_teardown();
}

/* The player writes the server's cvar again, with `set` or with the
   bare `name value` form: it is the player's. A server line that
   writes the value it already has leaves it the player's; one that
   writes another makes it the server's again. */
TEST(cvar_player_redefines_server_cvar)
{
	cs_setup();

	server_line("set t_cs4 \"t_cs_probe\"\n");
	player_line("vstr t_cs4\n");
	T_EQ_INT(probe_runs, 1);
	T_EQ_INT(probe_allowed, 0);

	/* the same value, now the player's */
	player_line("set t_cs4 \"t_cs_probe\"\n");
	player_line("vstr t_cs4\n");
	player_line("$t_cs4\n");
	T_EQ_INT(probe_runs, 3);
	T_EQ_INT(probe_allowed, 2);

	/* the server writes the same value: still the player's */
	server_line("set t_cs4 \"t_cs_probe\"\n");
	player_line("vstr t_cs4\n");
	T_EQ_INT(probe_runs, 4);
	T_EQ_INT(probe_allowed, 3);

	/* the server writes another value: the server's */
	server_line("set t_cs4 \"t_cs_probe 1\"\n");
	player_line("vstr t_cs4\n");
	T_EQ_INT(probe_runs, 5);
	T_EQ_INT(probe_allowed, 3);

	/* the bare form, from the player */
	player_line("t_cs4 t_cs_probe\n");
	player_line("vstr t_cs4\n");
	T_EQ_INT(probe_runs, 6);
	T_EQ_INT(probe_allowed, 4);

	cs_teardown();
}

/* A cvar the player's line sets from the server's value is the
   server's too: the line that wrote it was. */
TEST(cvar_copied_from_server_value)
{
	cs_setup();

	server_line("set t_cs5 \"t_cs_probe\"\n");
	player_line("set t_cs5b $t_cs5\n");
	player_line("vstr t_cs5b\n");

	T_EQ_INT(probe_runs, 1);
	T_EQ_INT(probe_allowed, 0);

	cs_teardown();
}

/* A write from the engine's own code, outside any line, keeps the
   mark: no text wrote the value, so nothing makes it the player's. */
TEST(cvar_engine_write_keeps_mark)
{
	cs_setup();

	server_line("set t_cs6 \"t_cs_probe\"\n");
	Cvar_Set("t_cs6", "t_cs_probe 1");
	player_line("vstr t_cs6\n");

	T_EQ_INT(probe_runs, 1);
	T_EQ_INT(probe_allowed, 0);

	/* and the player's write still takes it */
	player_line("set t_cs6 \"t_cs_probe\"\n");
	player_line("vstr t_cs6\n");

	T_EQ_INT(probe_runs, 2);
	T_EQ_INT(probe_allowed, 1);

	cs_teardown();
}

/* The server's `$` value makes only its own line the server's: the
   player's text after it, on the same buffer and the next, stays the
   player's. */
TEST(cvar_macro_leaves_player_text_after_it)
{
	cs_setup();

	server_line("set t_cs7 \"t_cs_probe\"\n");
	player_line("$t_cs7; t_cs_probe\nt_cs_probe\n");

	T_EQ_INT(probe_runs, 3);
	T_EQ_INT(probe_allowed, 2);

	/* and `vstr`: the text queued behind the value keeps its source */
	player_line("vstr t_cs7; t_cs_probe\n");

	T_EQ_INT(probe_runs, 5);
	T_EQ_INT(probe_allowed, 3);

	cs_teardown();
}
