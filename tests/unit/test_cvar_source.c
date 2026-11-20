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
#include "../../src/client/menu/header/vidrules.h"

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

	/* another value, now the player's (the same value would change no
	   owner) */
	player_line("set t_cs4 \"t_cs_probe 2\"\n");
	player_line("vstr t_cs4\n");
	player_line("$t_cs4\n");
	T_EQ_INT(probe_runs, 3);
	T_EQ_INT(probe_allowed, 2);

	/* the server writes the same value: still the player's */
	server_line("set t_cs4 \"t_cs_probe 2\"\n");
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

/* A player's write of the value a server's cvar already holds does not
   take it: the player takes a value back by writing another one. */
TEST(cvar_player_same_value_keeps_server)
{
	cs_setup();

	server_line("set t_cs8 \"t_cs_probe\"\n");
	player_line("set t_cs8 \"t_cs_probe\"\n");
	player_line("vstr t_cs8\n");
	T_EQ_INT(probe_runs, 1);
	T_EQ_INT(probe_allowed, 0);
	T_CHECK(Cvar_ServerWrote("t_cs8"));

	cs_teardown();
}

/* `set <x> <v> a` is a write too: the same value from the server leaves
   the player's cvar the player's, another takes it over. */
TEST(cvar_rv_flagged_same_value_keeps_player)
{
	cs_setup();
	player_line("set t_cs9 \"t_cs_probe\" a\n");
	server_line("set t_cs9 \"t_cs_probe\" a\n");
	player_line("vstr t_cs9\n");
	T_EQ_INT(probe_runs, 1);
	T_EQ_INT(probe_allowed, 1);
	server_line("set t_cs9 \"t_cs_probe 1\" a\n");
	player_line("vstr t_cs9\n");
	T_EQ_INT(probe_runs, 2);
	T_EQ_INT(probe_allowed, 1);
	cs_teardown();
}

/* A line run outside Cbuf_Execute (EXEC_NOW) that raises the source
   with `$` gives it back when it ends. */
TEST(cvar_rv_exec_now_raise_ends_with_line)
{
	cs_setup();
	server_line("set t_cs10 \"1\"\n");
	Cbuf_ExecuteText(EXEC_NOW, "t_cs_probe $t_cs10");
	T_EQ_INT(probe_runs, 1);
	T_EQ_INT(probe_allowed, 0);
	T_CHECK(!Cbuf_ExecutingServerText());
	Cbuf_ExecuteText(EXEC_NOW, "t_cs_probe");
	T_EQ_INT(probe_runs, 2);
	T_EQ_INT(probe_allowed, 1);
	Cbuf_ResetSource();
	cs_teardown();
}

static void
T_CsRaise_f(void)
{
	Com_Error(ERR_DROP, "t_cs_raise");
}

/* Com_Error ends the line it was raised in: an engine write after it is
   outside any line and keeps the mark. */
TEST(cvar_com_error_ends_line_count)
{
	cs_setup();
	if (!Cmd_Exists("t_cs_raise"))
	{
		Cmd_AddCommand("t_cs_raise", T_CsRaise_f);
	}

	server_line("set t_cs11 \"t_cs_probe\"\n");
	Cbuf_AddServerText("t_cs_raise\n");
	T_EXPECT_DROP(Cbuf_Execute());
	T_EQ_INT(Cmd_ExecutingLine(), 0);

	Cvar_Set("t_cs11", "t_cs_probe 1");
	T_CHECK(Cvar_ServerWrote("t_cs11"));
	player_line("vstr t_cs11\n");
	T_EQ_INT(probe_runs, 1);
	T_EQ_INT(probe_allowed, 0);
	cs_teardown();
}

/* The server's own config.cfg round trip: a game directory change
   writes config.cfg and runs it as the player's text. The replay
   writes the value the cvar holds, so it cannot take the cvar. */
TEST(cvar_zrv_game_dir_round_trip)
{
	char path[MAX_OSPATH];
	FILE *f;

	cs_setup();
	server_line("set t_cs12 \"t_cs_probe\" a\n");
	player_line("vstr t_cs12\n");
	T_EQ_INT(probe_runs, 1);
	T_EQ_INT(probe_allowed, 0);
	T_CHECK(Cvar_ServerWrote("t_cs12"));

	/* what CL_WriteConfiguration writes */
	Com_sprintf(path, sizeof(path), "%s/config.cfg", FS_Gamedir());
	f = Q_fopen(path, "w");
	T_REQUIRE(f != NULL);
	fprintf(f, "// generated by quake, do not modify\n");
	fclose(f);
	Cvar_WriteVariables(path);

	/* a remote client: no local server, `game` applies at once */
	Com_SetServerState(ss_dead);
	server_line("game t_cs_mod\n");
	Com_SetServerState(ss_cinematic);

	player_line("vstr t_cs12\n");
	T_EQ_INT(probe_runs, 2);
	T_CHECK(Cvar_ServerWrote("t_cs12"));
	T_EQ_INT(probe_allowed, 0);

	Com_SetServerState(ss_dead);
	player_line("game \"\"\n");
	cs_teardown();
}

/* The two-game-change route: the server's value reaches a config.cfg,
   the server changes it, and a later `game` replays the stale file.
   The writer never saves a marked cvar, so no file can carry it. */
TEST(cvar_zrv_two_game_change_route)
{
	char path[MAX_OSPATH];
	FILE *f;

	cs_setup();
	server_line("set t_cs13 \"t_cs_probe\" a\n");
	T_CHECK(Cvar_ServerWrote("t_cs13"));

	Com_sprintf(path, sizeof(path), "%s/config.cfg", FS_Gamedir());
	f = Q_fopen(path, "w");
	T_REQUIRE(f != NULL);
	fprintf(f, "// generated by quake, do not modify\n");
	fclose(f);
	Cvar_WriteVariables(path);

	Com_SetServerState(ss_dead);
	server_line("game t_cs_mod\n");
	server_line("set t_cs13 \"echo harmless\" a\n");
	server_line("game \"\"\n");
	Com_SetServerState(ss_cinematic);

	T_CHECK(Cvar_ServerWrote("t_cs13"));
	player_line("vstr t_cs13\n");
	T_EQ_INT(probe_runs, 0);
	T_EQ_INT(probe_allowed, 0);

	Com_SetServerState(ss_dead);
	player_line("game \"\"\n");
	cs_teardown();
}

static int
file_has(const char *path, const char *needle)
{
	char buf[8192];
	size_t n;
	FILE *f;

	f = Q_fopen(path, "r");
	T_REQUIRE(f != NULL);
	n = fread(buf, 1, sizeof(buf) - 1, f);
	buf[n] = 0;
	fclose(f);

	return strstr(buf, needle) != NULL;
}

/* The writer: no cvar a server wrote is in the file. */
TEST(cvar_zrv_writer_skips_marked)
{
	char path[MAX_OSPATH];
	FILE *f;

	cs_setup();
	server_line("set t_cs14 \"srv_val\" a\n");
	player_line("set t_cs15 \"plr_val\" a\n");
	T_CHECK(Cvar_ServerWrote("t_cs14"));
	T_CHECK(!Cvar_ServerWrote("t_cs15"));

	Com_sprintf(path, sizeof(path), "%s/config_w.cfg", FS_Gamedir());
	f = Q_fopen(path, "w");
	T_REQUIRE(f != NULL);
	fclose(f);
	Cvar_WriteVariables(path);
	T_CHECK(!file_has(path, "set t_cs14 "));
	T_CHECK(file_has(path, "set t_cs15 \"plr_val\""));

	/* the player writes a different value: it is theirs, and saved */
	player_line("set t_cs14 \"mine\" a\n");
	T_CHECK(!Cvar_ServerWrote("t_cs14"));
	f = Q_fopen(path, "w");
	T_REQUIRE(f != NULL);
	fclose(f);
	Cvar_WriteVariables(path);
	T_CHECK(file_has(path, "set t_cs14 \"mine\""));

	cs_teardown();
}

/* Writes the cvar file, empty first. */
static void
write_cfg(const char *path)
{
	FILE *f = Q_fopen(path, "w");

	T_REQUIRE(f != NULL);
	fclose(f);
	Cvar_WriteVariables(path);
}

/* FIX168 round 2: a server write marks the cvar and the file omits it,
   whatever the player held before (the skip is FIX158's). The player's
   own later write is saved. */
TEST(cvar_fix168_file_omits_marked_until_player_writes)
{
	char path[MAX_OSPATH];

	cs_setup();
	player_line("set t_cs20 \"P\" a\n");
	server_line("set t_cs20 \"V\"\n");
	server_line("set t_cs21 \"V2\" a\n");
	T_CHECK(Cvar_ServerWrote("t_cs20"));
	T_EQ_STR(Cvar_VariableString("t_cs20"), "V");

	Com_sprintf(path, sizeof(path), "%s/config_k.cfg", FS_Gamedir());
	write_cfg(path);
	T_CHECK(!file_has(path, "set t_cs20 "));
	T_CHECK(!file_has(path, "set t_cs21 "));
	T_CHECK(!file_has(path, "\"P\""));

	player_line("set t_cs20 \"Q\"\n");
	write_cfg(path);
	T_CHECK(file_has(path, "set t_cs20 \"Q\""));

	cs_teardown();
}

/* Security round 1, B1: a value the engine copied from the network may
   hold a newline. A server write after it must not get that value into
   config.cfg, where it would split into the player's lines. */
TEST(cvar_rvsec_engine_then_server_newline_not_written)
{
	char path[MAX_OSPATH];

	cs_setup();
	Cvar_Get("t_rvs_g", "", CVAR_LATCH | CVAR_SERVERINFO);
	Com_SetServerState(ss_dead);
	Cvar_Set("t_rvs_g", "x\nt_cs_probe\nx"); /* outside a line, as CL_ParseServerData does */
	Com_SetServerState(ss_cinematic);
	T_CHECK(!Cvar_ServerWrote("t_rvs_g"));
	server_line("set t_rvs_g y a\n");
	T_CHECK(Cvar_ServerWrote("t_rvs_g"));

	Com_sprintf(path, sizeof(path), "%s/config_rvs.cfg", FS_Gamedir());
	write_cfg(path);
	T_CHECK(!file_has(path, "\nt_cs_probe\n"));
	probe_runs = 0;
	probe_allowed = 0;
	player_line("exec config_rvs.cfg\n");
	T_EQ_INT(probe_allowed, 0);
	T_EQ_INT(probe_runs, 0);
	cs_teardown();
}

/* Security round 1, I1: a latched cvar the server wrote while no local
   server ran, then the player's latched write, then the server again:
   the live value is the server's and is not in the file. */
TEST(cvar_rvsec_latched_server_value_not_written)
{
	char path[MAX_OSPATH];

	cs_setup();
	Cvar_Get("t_rvs_l", "A", CVAR_LATCH | CVAR_ARCHIVE);
	Com_SetServerState(ss_dead);
	server_line("set t_rvs_l \"S1\"\n");
	Com_SetServerState(ss_cinematic);
	player_line("set t_rvs_l \"P\"\n");
	server_line("set t_rvs_l \"S2\"\n");

	Com_sprintf(path, sizeof(path), "%s/config_rvl.cfg", FS_Gamedir());
	write_cfg(path);
	T_CHECK(!file_has(path, "set t_rvs_l \"S1\""));
	T_CHECK(!file_has(path, "set t_rvs_l \"S2\""));
	cs_teardown();
}

/* Security round 1, I2: a server's archive flag does not make the
   client save a value the player set on a cvar that was not archived. */
TEST(cvar_rvsec_server_archive_flag_not_written)
{
	char path[MAX_OSPATH];

	cs_setup();
	Cvar_Get("t_rvs_pw", "", 0);
	player_line("set t_rvs_pw secretpw\n");
	server_line("set t_rvs_pw x a\n");

	Com_sprintf(path, sizeof(path), "%s/config_rvp.cfg", FS_Gamedir());
	write_cfg(path);
	T_CHECK(!file_has(path, "secretpw"));
	cs_teardown();
}

/* Puts the suite's cvar table, the preset directory and the scratch
   cvar back, and removes the file the test wrote. The player's write
   first clears the mark the table's cvar may still hold. */
static void
rtsave_restore(cvar_t *saved, const char *path)
{
	cvar_t *v;

	player_line("set r_rt_ao \"9\"\n");
	v = cvar_vars;
	RTPreset_SetDir(NULL);
	cvar_vars = saved;
	remove(path);

	while (v)
	{
		cvar_t *next = v->next;

		Z_Free(v->name);
		Z_Free(v->string);
		Z_Free(v->default_string);
		Z_Free(v->latched_string);
		Z_Free(v);
		v = next;
	}

	cs_teardown();
}

/* T_REQUIRE leaves the test: the suite's state is put back first */
#define RTSAVE_REQUIRE(COND) \
	do { if (!(COND)) { rtsave_restore(saved, path); T_REQUIRE(COND); } } while (0)

/* FIX168 step 4: the RT preset save writes no value a server wrote,
   and writes the player's own. */
TEST(cvar_fix168_rt_preset_save_skips_marked)
{
	char path[MAX_OSPATH], text[4096];
	cvar_t *saved = cvar_vars;
	size_t got;
	FILE *f;

	cs_setup();
	snprintf(path, sizeof(path), "%s/presets/rt-srv.cfg", t_tmpdir());
	cvar_vars = NULL;
	Cvar_Get("r_rt_ao", "1", CVAR_ARCHIVE);
	RTPreset_SetDir(t_tmpdir());
	server_line("set r_rt_ao \"2\"\n");
	T_CHECK(Cvar_ServerWrote("r_rt_ao"));

	T_EQ_INT(RTPreset_Save("srv"), 1);
	f = fopen(path, "r");
	RTSAVE_REQUIRE(f != NULL);
	got = fread(text, 1, sizeof(text) - 1, f);
	fclose(f);
	text[got] = '\0';
	T_CHECK(strstr(text, "r_rt_ao") == NULL);

	/* the player's own write is saved */
	player_line("set r_rt_ao \"3\"\n");
	T_EQ_INT(RTPreset_Save("srv"), 1);
	f = fopen(path, "r");
	RTSAVE_REQUIRE(f != NULL);
	got = fread(text, 1, sizeof(text) - 1, f);
	fclose(f);
	text[got] = '\0';
	T_CHECK(strstr(text, "set r_rt_ao \"3\"") != NULL);

	rtsave_restore(saved, path);
}

/* FIX168 step 2: a reset to a default a server line created leaves
   the cvar the server's. `reset` on the named cvar, not `resetall`: a
   resetall would reset every cvar of the shared test process. */
TEST(cvar_fix168_reset_to_server_default_stays_marked)
{
	char path[MAX_OSPATH];

	cs_setup();
	server_line("set t_cs22 \"t_cs_probe\" a\n");
	player_line("set t_cs22 \"W\"\n");
	T_CHECK(!Cvar_ServerWrote("t_cs22"));
	player_line("reset t_cs22\n");
	T_EQ_STR(Cvar_VariableString("t_cs22"), "t_cs_probe");
	T_CHECK(Cvar_ServerWrote("t_cs22"));
	player_line("vstr t_cs22\n");
	T_EQ_INT(probe_runs, 1);
	T_EQ_INT(probe_allowed, 0);

	Com_sprintf(path, sizeof(path), "%s/config_r.cfg", FS_Gamedir());
	write_cfg(path);
	T_CHECK(!file_has(path, "t_cs_probe"));

	/* the player writes a different value, then resets again */
	player_line("set t_cs22 \"W\"\n");
	T_CHECK(!Cvar_ServerWrote("t_cs22"));
	player_line("reset t_cs22\n");
	T_CHECK(Cvar_ServerWrote("t_cs22"));

	cs_teardown();
}

/* FIX168 round 2, correctness I1: a server line created the cvar before
   the engine registered it. Cvar_Get replaced the default, so a reset
   to it is the player's own default and not the server's. */
TEST(cvar_rv168_stale_server_default)
{
	char path[MAX_OSPATH];

	cs_setup();
	server_line("set t_cs30 \"S\"\n");
	Cvar_Get("t_cs30", "E", CVAR_ARCHIVE);
	player_line("set t_cs30 \"W\"\n");
	T_CHECK(!Cvar_ServerWrote("t_cs30"));
	player_line("reset t_cs30\n");
	T_EQ_STR(Cvar_VariableString("t_cs30"), "E");
	T_CHECK(!Cvar_ServerWrote("t_cs30"));

	Com_sprintf(path, sizeof(path), "%s/config_z.cfg", FS_Gamedir());
	write_cfg(path);
	T_CHECK(file_has(path, "set t_cs30 \"E\""));
	cs_teardown();
}
