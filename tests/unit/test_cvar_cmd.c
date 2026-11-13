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
 * Cvars (src/common/cvar.c) and the command buffer (src/common/
 * cmdparser.c): the console's configuration and scripting surface, and
 * the path a server's stufftext takes on a client.
 *
 * =======================================================================
 */

#include <unistd.h>

#include "test.h"

/* Runs a script through the command buffer and returns what it printed. */
static const char *
run(const char *script)
{
	t_console_reset();
	Cbuf_AddText(script);
	Cbuf_Execute();
	return t_console();
}

TEST(cvar_get_set)
{
	cvar_t *v;

	v = Cvar_Get("t_plain", "12.5", 0);
	T_REQUIRE(v != NULL);
	T_EQ_STR(v->string, "12.5");
	T_EQ_FLT(v->value, 12.5f);
	T_EQ_STR(v->default_string, "12.5");

	/* A second Cvar_Get keeps the value, ORs the flags in and replaces
	   the default. */
	v->modified = false;
	T_CHECK(Cvar_Get("t_plain", "99", CVAR_ARCHIVE) == v);
	T_EQ_STR(v->string, "12.5");
	T_EQ_STR(v->default_string, "99");
	T_CHECK(v->flags & CVAR_ARCHIVE);

	Cvar_Set("t_plain", "abc");
	T_EQ_STR(Cvar_VariableString("t_plain"), "abc");
	T_EQ_FLT(Cvar_VariableValue("t_plain"), 0.0f);
	T_CHECK(v->modified);

	Cvar_SetValue("t_plain", 3);
	T_EQ_STR(v->string, "3");
	Cvar_SetValue("t_plain", 0.25f);
	T_EQ_STR(v->string, "0.250000");

	/* Unknown variables read as empty / 0, and Cvar_Set creates them. */
	T_EQ_STR(Cvar_VariableString("t_nope"), "");
	T_EQ_FLT(Cvar_VariableValue("t_nope"), 0.0f);
	Cvar_Set("t_created", "1");
	v = Cvar_Get("t_created", "other", 0);
	T_REQUIRE(v != NULL);
	T_EQ_STR(v->string, "1");
	T_EQ_INT(v->flags, 0);

	/* A NULL default does not create. */
	T_CHECK(Cvar_Get("t_null", NULL, 0) == NULL);
	T_EQ_STR(Cvar_VariableString("t_null"), "");
}

TEST(cvar_info_validation)
{
	cvar_t *v;

	t_console_reset();
	T_CHECK(Cvar_Get("t_bad\\name", "x", CVAR_USERINFO) == NULL);
	T_CHECK(Cvar_Get("t_badval", "a;b", CVAR_SERVERINFO) == NULL);
	T_CHECK(Cvar_Get("t_badq", "a\"b", CVAR_USERINFO) == NULL);
	T_CONSOLE_HAS("invalid info cvar name");
	T_CONSOLE_HAS("invalid info cvar value");

	v = Cvar_Get("t_info", "ok", CVAR_USERINFO);
	T_REQUIRE(v != NULL);
	Cvar_Set("t_info", "no\\backslash");
	T_EQ_STR(v->string, "ok");
	Cvar_Set("t_info", "fine");
	T_EQ_STR(v->string, "fine");
	T_EQ_STR(Info_ValueForKey(Cvar_Userinfo(), "t_info"), "fine");
	T_EQ_STR(Info_ValueForKey(Cvar_Serverinfo(), "t_info"), "");
}

TEST(cvar_noset)
{
	cvar_t *v = Cvar_Get("t_noset", "locked", CVAR_NOSET);

	/* Cvar_Set and the console's three-argument "set" refuse... */
	t_console_reset();
	Cvar_Set("t_noset", "changed");
	T_EQ_STR(v->string, "locked");
	T_CONSOLE_HAS("t_noset is write protected.");
	run("set t_noset changed\n");
	T_EQ_STR(v->string, "locked");
	run("t_noset changed\n");
	T_EQ_STR(v->string, "locked");

	/* ...Cvar_ForceSet does not... */
	Cvar_ForceSet("t_noset", "forced");
	T_EQ_STR(v->string, "forced");

	/* ...and neither does the four-argument "set", which goes through
	   Cvar_FullSet and replaces the flags, dropping CVAR_NOSET. Pinned
	   as current behaviour. */
	run("set t_noset viaflags a\n");
	T_EQ_STR(v->string, "viaflags");
	T_EQ_INT(v->flags, CVAR_ARCHIVE);

	t_console_reset();
	run("set t_noset x q\n");
	T_CONSOLE_HAS("flags can only be 'a', 'u' or 's'");
	run("set t_noset\n");
	T_CONSOLE_HAS("usage: set <variable> <value> [u / s]");
}

TEST(cvar_latch)
{
	cvar_t *v = Cvar_Get("t_latch", "1", CVAR_LATCH);
	int state = Com_ServerState();

	/* With no server running a latched cvar changes at once. */
	Com_SetServerState(0);
	Cvar_Set("t_latch", "2");
	T_EQ_STR(v->string, "2");
	T_CHECK(v->latched_string == NULL);

	/* With a server running it latches until Cvar_GetLatchedVars. */
	Com_SetServerState(2); /* ss_game: any nonzero state latches */
	t_console_reset();
	Cvar_Set("t_latch", "3");
	T_EQ_STR(v->string, "2");
	T_REQUIRE(v->latched_string != NULL);
	T_EQ_STR(v->latched_string, "3");
	T_CONSOLE_HAS("t_latch will be changed for next game.");

	/* Setting it back to the latched value is a no-op, a third value
	   replaces the latch. */
	Cvar_Set("t_latch", "4");
	T_EQ_STR(v->latched_string, "4");

	Cvar_GetLatchedVars();
	T_EQ_STR(v->string, "4");
	T_EQ_FLT(v->value, 4.0f);
	T_CHECK(v->latched_string == NULL);

	/* ForceSet drops a pending latch and sets immediately. */
	Cvar_Set("t_latch", "5");
	T_REQUIRE(v->latched_string != NULL);
	Cvar_ForceSet("t_latch", "6");
	T_CHECK(v->latched_string == NULL);
	T_EQ_STR(v->string, "6");

	Com_SetServerState(state);
}

TEST(cvar_console_commands)
{
	cvar_t *v = Cvar_Get("t_cmd", "5", 0);

	T_EQ_STR(run("t_cmd\n"), "\"t_cmd\" is \"5\"\n");
	run("t_cmd 7\n");
	T_EQ_STR(v->string, "7");
	/* inc / dec write the "%f" form. */
	run("inc t_cmd\n");
	T_EQ_STR(v->string, "8.000000");
	run("dec t_cmd 10.5\n");
	T_EQ_STR(v->string, "-2.500000");

	/* toggle flips 0 and 1 only, or cycles through the given values. */
	run("toggle t_cmd\n");
	T_CONSOLE_HAS("\"t_cmd\" is \"-2.500000\", can't toggle");
	run("t_cmd 0\n");
	run("toggle t_cmd\n");
	T_EQ_STR(v->string, "1");
	run("toggle t_cmd\n");
	T_EQ_STR(v->string, "0");
	run("toggle t_cmd a b c\n");
	T_CONSOLE_HAS("can't cycle");
	run("t_cmd B\n");
	run("toggle t_cmd a b c\n");
	T_EQ_STR(v->string, "c");
	run("toggle t_cmd a b c\n");
	T_EQ_STR(v->string, "a");

	run("t_cmd text\n");
	run("inc t_cmd\n");
	T_CONSOLE_HAS("can't inc");
	T_EQ_STR(v->string, "text");

	run("reset t_cmd\n");
	T_EQ_STR(v->string, "5");
}

static t_text_t trace;

static void
t_record_f(void)
{
	t_text_printf(&trace, "argc=%d args=[%s]", Cmd_Argc(), Cmd_Args());

	for (int i = 0; i < Cmd_Argc(); i++)
	{
		t_text_printf(&trace, " <%s>", Cmd_Argv(i));
	}

	t_text_printf(&trace, "\n");
}

TEST(cmd_tokenize_and_buffer)
{
	trace.len = 0;
	Cmd_AddCommand("t_record", t_record_f);
	T_CHECK(Cmd_Exists("t_record"));

	Cvar_Set("t_macro", "expanded");
	run("t_record one two;t_record \"quoted ; semicolon\" three\n"
		"T_RECORD  spaced   out  \n"
		"t_record $t_macro \"$t_macro\" pre$t_macro\n"
		"t_record // comment\n"
		"t_record \"unmatched\n");
	T_CONSOLE_HAS("Line has unmatched quote, discarded.");
	/* Not pinned: a macro that expands to something shorter than its
	   name ("$t_nosuchvar" -> ""). Cmd_MacroExpandString keeps scanning
	   up to the pre-expansion length and reads stale bytes of its static
	   buffer, so the outcome depends on earlier lines. Reported with
	   this harness (TASK29); extend this test when it is fixed. */
	t_text_printf(&trace, "--- insert vs. add\n");
	Cbuf_AddText("t_record added\n");
	Cbuf_InsertText("t_record inserted\n");
	Cbuf_Execute();
	t_text_printf(&trace, "--- exec now\n");
	Cbuf_ExecuteText(EXEC_NOW, "t_record now");
	Cbuf_ExecuteText(EXEC_APPEND, "t_record appended\n");
	Cbuf_Execute();

	Cmd_RemoveCommand("t_record");
	T_CHECK(!Cmd_Exists("t_record"));

	t_pin("cmd-tokenize", trace.data);
	t_text_free(&trace);
}

TEST(cmd_alias)
{
	T_EQ_STR(run("alias t_greet \"echo hi; echo there\"\nt_greet\n"), "hi \nthere \n");

	/* Redefining replaces; aliases may call aliases. */
	T_EQ_STR(run("alias t_greet echo again\nalias t_outer t_greet\nt_outer\n"), "again \n");

	/* A self-recursive alias stops at ALIAS_LOOP_COUNT. */
	run("alias t_loop t_loop\nt_loop\n");
	T_CONSOLE_HAS("ALIAS_LOOP_COUNT");

	/* The exact limit. Cbuf_Execute resets alias_count; Cmd_ExecuteString
	   refuses the expansion that makes ++alias_count reach
	   ALIAS_LOOP_COUNT (16). So one Cbuf_Execute may expand 15 aliases
	   and the 16th is refused. t_c1 .. t_c16 is a chain in which each
	   alias runs the next and t_c16 echoes: starting at t_c2 takes 15
	   expansions and reaches the echo, starting at t_c1 takes 16 and
	   stops one short of it. */
	{
		char def[64];
		int k;

		for (k = 1; k <= 16; k++)
		{
			if (k < 16)
			{
				Com_sprintf(def, sizeof(def), "alias t_c%d t_c%d\n", k, k + 1);
			}
			else
			{
				Com_sprintf(def, sizeof(def), "alias t_c%d echo reached\n", k);
			}

			run(def);
		}

		T_EQ_STR(run("t_c2\n"), "reached \n");
		T_EQ_STR(run("t_c1\n"), "ALIAS_LOOP_COUNT\n");
	}

	/* vstr runs a cvar's value as a command. */
	Cvar_Set("t_script", "echo from vstr");
	T_EQ_STR(run("vstr t_script\n"), "from vstr \n");
}

TEST(cmd_wait)
{
	/* "wait" stops the buffer until about 16 ms have passed. */
	Sys_Milliseconds();
	usleep(2000);

	t_console_reset();
	Cbuf_AddText("echo before; wait; echo after\n");
	Cbuf_Execute();
	T_EQ_STR(t_console(), "before \n");
	Cbuf_Execute();
	T_EQ_STR(t_console(), "before \n");

	usleep(40 * 1000);
	Cbuf_Execute();
	T_EQ_STR(t_console(), "before \nafter \n");
}

TEST(cbuf_overflow_and_long_lines)
{
	static char big[40000];

	/* Text that does not fit the 32 KiB buffer is dropped whole. */
	memset(big, 'x', sizeof(big) - 1);
	big[sizeof(big) - 1] = '\0';
	t_console_reset();
	Cbuf_AddText(big);
	T_CONSOLE_HAS("Cbuf_AddText: overflow");
	T_EQ_STR(run(""), "");

	/* A macro expansion that grows past MAX_STRING_CHARS is discarded. */
	memset(big, 'y', 1100);
	big[1100] = '\0';
	Cvar_Set("t_long", big);
	run("echo $t_long $t_long\n");
	T_CONSOLE_HAS("Expanded line exceeded");
}
