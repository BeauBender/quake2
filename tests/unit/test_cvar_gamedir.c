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
 * The `game` cvar never holds a value FS_GameDirValid refuses (FIX173),
 * whoever writes it: Cvar_Set, Cvar_ForceSet, Cvar_FullSet, a console
 * line (`set game x`, `game x`) the player typed or a server sent, and
 * a latched write. The old value stays, and userGivenGame, which the
 * disconnect restores, is never set from a refused value. The strings
 * are the ones a server can send; the validator is the engine's own.
 *
 * =======================================================================
 */

#include "test.h"
#include "../../src/server/header/server.h"

extern char userGivenGame[MAX_QPATH]; /* src/common/frame.c */
extern cvar_t *fs_gamedirvar;         /* src/common/filesystem.c */
extern sizebuf_t cmd_text;            /* src/common/cmdparser.c */

#define T_LONG_NAME \
	"0123456789012345678901234567890123456789" \
	"012345678901234567890123"

static const char *gd_bad[] = {
	"../x", ".", "..", "a/b", "a\\b", "a:b", "a\x01" "b", "a\x7f" "b",
	"x\ny", T_LONG_NAME, NULL
};

/* The state a test found, put back at its end so later tests see it. */
static char gd_saved_game[MAX_QPATH];
static char gd_saved_given[MAX_QPATH];

static void
gd_save(void)
{
	Q_strlcpy(gd_saved_game, Cvar_VariableString("game"), sizeof(gd_saved_game));
	Q_strlcpy(gd_saved_given, userGivenGame, sizeof(gd_saved_given));
}

static void
gd_restore(void)
{
	Com_SetServerState(0);
	SZ_Clear(&cmd_text);
	Cvar_Get("game", "", CVAR_LATCH | CVAR_SERVERINFO)->latched_string = NULL;
	/* resets game and rebuilds the search paths for that directory */
	FS_BuildGameSpecificSearchPath(gd_saved_game);
	Q_strlcpy(userGivenGame, gd_saved_given, sizeof(userGivenGame));
}

static void
gd_check_unchanged(const char *what, const char *bad)
{
	cvar_t *g = Cvar_Get("game", "", CVAR_LATCH | CVAR_SERVERINFO);

	(void)bad;
	Com_Printf("check %s\n", what);
	T_CHECK(!strcmp(g->string, "ctf"));
	T_CHECK(!strcmp(userGivenGame, "ctf"));
	T_CHECK(g->latched_string == NULL);
}

static void
gd_setup(void)
{
	Com_SetServerState(0);
	SZ_Clear(&cmd_text);
	Cvar_Get("game", "", CVAR_LATCH | CVAR_SERVERINFO);
	Cvar_ForceSet("game", "ctf");
	Q_strlcpy(userGivenGame, "ctf", sizeof(userGivenGame));
	Cvar_Get("game", "", CVAR_LATCH | CVAR_SERVERINFO)->latched_string = NULL;
}

TEST(gamedir_api_refuses)
{
	int i;
	char line[256];

	(void)fs_gamedirvar;
	gd_save();

	for (i = 0; gd_bad[i]; i++)
	{
		gd_setup();
		Cvar_Set("game", gd_bad[i]);
		gd_check_unchanged("Cvar_Set", gd_bad[i]);
		Cvar_ForceSet("game", gd_bad[i]);
		gd_check_unchanged("Cvar_ForceSet", gd_bad[i]);
		Cvar_FullSet("game", gd_bad[i], CVAR_LATCH | CVAR_SERVERINFO);
		gd_check_unchanged("Cvar_FullSet", gd_bad[i]);

		/* a console line, as the player's text and as the server's. A
		   newline ends the line, so that name cannot be typed. */
		if (!strchr(gd_bad[i], '\n'))
		{
			Com_sprintf(line, sizeof(line), "set game \"%s\"\n", gd_bad[i]);
			Cbuf_AddText(line);
			Cbuf_Execute();
			gd_check_unchanged("player set", gd_bad[i]);
			Cbuf_AddServerText(line);
			Cbuf_Execute();
			gd_check_unchanged("server set", gd_bad[i]);
			Com_sprintf(line, sizeof(line), "game \"%s\"\n", gd_bad[i]);
			Cbuf_AddText(line);
			Cbuf_Execute();
			gd_check_unchanged("player game", gd_bad[i]);
			Cbuf_AddServerText(line);
			Cbuf_Execute();
			gd_check_unchanged("server game", gd_bad[i]);
		}

		/* a latched write: a server is running */
		Com_SetServerState(ss_game);
		Cvar_Set("game", gd_bad[i]);
		gd_check_unchanged("latched Cvar_Set", gd_bad[i]);
		if (!strchr(gd_bad[i], '\n'))
		{
			Com_sprintf(line, sizeof(line), "game \"%s\"\n", gd_bad[i]);
			Cbuf_AddServerText(line);
			Cbuf_Execute();
			gd_check_unchanged("latched server game", gd_bad[i]);
		}
		Cvar_GetLatchedVars();
		gd_check_unchanged("latched apply", gd_bad[i]);
		Com_SetServerState(0);
	}

	gd_restore();
}

TEST(gamedir_valid_accepted)
{
	cvar_t *g;

	gd_save();
	gd_setup();
	g = Cvar_Get("game", "", CVAR_LATCH | CVAR_SERVERINFO);

	/* back to baseq2, then a name, through the console */
	Cbuf_AddText("game \"\"\n");
	Cbuf_Execute();
	T_EQ_STR(g->string, "");
	T_EQ_STR(userGivenGame, "");
	Cbuf_AddServerText("set game ctf\n");
	Cbuf_Execute();
	T_EQ_STR(g->string, "ctf");
	Cvar_Set("game", "");
	T_EQ_STR(g->string, "");
	Cvar_ForceSet("game", "ctf");
	T_EQ_STR(g->string, "ctf");

	/* a latched valid write is kept for the next game */
	Com_SetServerState(ss_game);
	Cvar_Set("game", "xatrix");
	T_EQ_STR(g->latched_string, "xatrix");
	Com_SetServerState(0);
	gd_restore();
}

/* Qcommon_Init with `+set game ../x`, run by the runner before
   FS_InitFilesystem (tests/unit/main.c): the cvar does not exist yet, so
   the value goes through Cvar_GetNew. */
TEST(gamedir_startup_refused)
{
	T_EQ_STR(Cvar_VariableString("game"), "");
	T_CHECK(strcmp(userGivenGame, "../x") != 0);
	T_EQ_STR(userGivenGame, "");
	T_CHECK(strstr(t_startup_console(),
			"game: refused \"../x\", not a single directory name.") != NULL);
}

/* A default that is refused becomes "", whatever the cvar holds. */
TEST(gamedir_default_refused)
{
	t_console_reset();
	T_EQ_STR(Cvar_Get("game", "../x", 0)->default_string, "");
	T_CONSOLE_HAS("game: refused");
	Cvar_Get("game", "", 0);
}

/* The refusal line shows the value, control bytes and 0x7f or above as
   '?', and never uses it as a format. */
TEST(gamedir_refusal_line_sanitized)
{
	gd_save();
	t_console_reset();
	Cvar_Set("game", "a\x01" "b\x1b" "c\x7f" "d\xff" "e%s%n/");
	T_CONSOLE_HAS("game: refused \"a?b?c?d?e%s%n/\", not a single");
	gd_restore();
}
