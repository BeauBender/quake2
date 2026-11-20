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

	/* ...and the four-argument "set" refuses too, one assertion per
	   flag form: value and flags stay, the console says why. */
	Cvar_ForceSet("t_noset", "locked");
	t_console_reset();
	run("set t_noset viaflags\n");
	T_EQ_STR(v->string, "locked");
	T_CONSOLE_HAS("t_noset is write protected.");

	run("set t_noset viaflags u\n");
	T_EQ_STR(v->string, "locked");
	T_EQ_INT(v->flags, CVAR_NOSET);
	T_CONSOLE_HAS("t_noset is write protected.");

	run("set t_noset viaflags s\n");
	T_EQ_STR(v->string, "locked");
	T_EQ_INT(v->flags, CVAR_NOSET);
	T_CONSOLE_HAS("t_noset is write protected.");

	run("set t_noset viaflags a\n");
	T_EQ_STR(v->string, "locked");
	T_EQ_INT(v->flags, CVAR_NOSET);
	T_CONSOLE_HAS("t_noset is write protected.");

	/* Cvar_FullSet itself is the engine's own path (mapname, gamedir)
	   and is not guarded. */
	Cvar_FullSet("t_noset", "engine", CVAR_NOSET);
	T_EQ_STR(v->string, "engine");

	t_console_reset();
	run("set t_noset x q\n");
	T_CONSOLE_HAS("flags can only be 'a', 'u' or 's'");
	run("set t_noset\n");
	T_CONSOLE_HAS("usage: set <variable> <value> [u / s]");
}

TEST(cvar_noset_registration_order)
{
	cvar_t *v;

	/* A value set before the cvar is registered, as +set does on the
	   command line, survives the later NOSET registration. */
	run("set t_pre1 fromcmdline\n");
	v = Cvar_Get("t_pre1", "default", CVAR_NOSET);
	T_EQ_STR(v->string, "fromcmdline");
	T_CHECK(v->flags & CVAR_NOSET);

	run("set t_pre2 fromcmdline a\n");
	v = Cvar_Get("t_pre2", "default", CVAR_NOSET);
	T_EQ_STR(v->string, "fromcmdline");
	T_CHECK(v->flags & CVAR_NOSET);

	/* Once registered it is protected. */
	run("set t_pre2 later\n");
	T_EQ_STR(v->string, "fromcmdline");
}

TEST(cvar_set_flag_forms)
{
	cvar_t *v = Cvar_Get("t_forms", "0", 0);

	run("set t_forms one\n");
	T_EQ_STR(v->string, "one");
	T_EQ_INT(v->flags, 0);

	run("set t_forms two u\n");
	T_EQ_STR(v->string, "two");
	T_EQ_INT(v->flags, CVAR_USERINFO);

	run("set t_forms three s\n");
	T_EQ_STR(v->string, "three");
	T_EQ_INT(v->flags, CVAR_SERVERINFO);

	run("set t_forms four a\n");
	T_EQ_STR(v->string, "four");
	T_EQ_INT(v->flags, CVAR_ARCHIVE);
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

/* Tokenizes one line with macro expansion from an exactly sized heap copy
   and prints argv, or nothing for a discarded line, into out. */
static void
expand_line(const char *line, char *out, size_t outsize)
{
	size_t n = strlen(line);
	char *copy = malloc(n + 1);
	int i;

	memcpy(copy, line, n + 1);
	out[0] = 0;
	Cmd_TokenizeString(copy, true);

	for (i = 0; i < Cmd_Argc(); i++)
	{
		Q_strlcat(out, "[", outsize);
		Q_strlcat(out, Cmd_Argv(i), outsize);
		Q_strlcat(out, "]", outsize);
	}

	free(copy);
}

#define LONGNAME "t_mx_very_long_macro_name_x"

TEST(cmd_macro_expand_current_line)
{
	char out[2 * MAX_STRING_CHARS + 64];
	char line[2 * MAX_STRING_CHARS + 64];
	char msg[96];
	size_t i;

	Cvar_Set("t_mx_quote", "xx\"");
	Cvar_Set(LONGNAME, "");
	Cvar_Set("t_mx_short", "s");
	Cvar_Set("t_mx_long", "LLLLLLLLLLLLLLLLLLLL");
	Cvar_Set("t_mx_one", "1");

	/* A long expansion leaves a quote in the static buffer, in the range
	   that a shorter line's expansion no longer covers. The next line
	   must not see it. */
	t_console_reset();
	expand_line("m $t_mx_quote", out, sizeof(out));
	T_CONSOLE_HAS("unmatched quote");
	t_console_reset();
	expand_line("m $" LONGNAME, out, sizeof(out));
	T_EQ_STR(out, "[m]");
	T_EQ_STR(t_console(), "");

	/* A macro that expands to a shorter value. */
	expand_line("a $t_mx_short b", out, sizeof(out));
	T_EQ_STR(out, "[a][s][b]");
	expand_line("a $" LONGNAME " b", out, sizeof(out));
	T_EQ_STR(out, "[a][b]");

	/* A longer value. */
	expand_line("a $t_mx_long b", out, sizeof(out));
	T_EQ_STR(out, "[a][LLLLLLLLLLLLLLLLLLLL][b]");

	/* Two macros on one line, shorter then longer and the reverse. */
	expand_line("$" LONGNAME " $t_mx_long $t_mx_short", out, sizeof(out));
	T_EQ_STR(out, "[LLLLLLLLLLLLLLLLLLLL][s]");
	expand_line("$t_mx_long $" LONGNAME " $t_mx_short", out, sizeof(out));
	T_EQ_STR(out, "[LLLLLLLLLLLLLLLLLLLL][s]");

	/* A macro at the end of the line, and a bare dollar sign. */
	expand_line("a b $" LONGNAME, out, sizeof(out));
	T_EQ_STR(out, "[a][b]");
	expand_line("a $", out, sizeof(out));
	T_EQ_STR(out, "[a][$]");

	/* A quote opened before a macro: no expansion inside it. An
	   unclosed one discards the line. */
	expand_line("a \"$t_mx_long\" $" LONGNAME " b", out, sizeof(out));
	T_EQ_STR(out, "[a][$t_mx_long][b]");
	t_console_reset();
	expand_line("a $" LONGNAME " \"$t_mx_long", out, sizeof(out));
	T_EQ_STR(out, "");
	T_CONSOLE_HAS("unmatched quote");

	/* A macro name that is an unterminated quoted token reads only up
	   to the end of the line; the next line is unaffected. */
	expand_line("a $\"abc", out, sizeof(out));
	expand_line("a b", out, sizeof(out));
	T_EQ_STR(out, "[a][b]");

	/* The expansion count limit: 99 expansions pass, the 100th is
	   refused. */
	line[0] = 0;

	for (i = 0; i < 99; i++)
	{
		Q_strlcat(line, "$t_mx_one ", sizeof(line));
	}

	t_console_reset();
	expand_line(line, out, sizeof(out));
	T_CHECK(strncmp(out, "[1][1]", 6) == 0);
	T_EQ_STR(t_console(), "");
	Q_strlcat(line, "$t_mx_one ", sizeof(line));
	expand_line(line, out, sizeof(out));
	T_EQ_STR(out, "");
	T_CONSOLE_HAS("Macro expansion loop, discarded.");

	/* The length limits: a line of MAX_STRING_CHARS - 1 passes, one of
	   MAX_STRING_CHARS is refused; an expansion that reaches
	   MAX_STRING_CHARS is refused, one byte less is not. */
	memset(line, 'x', MAX_STRING_CHARS - 1);
	line[MAX_STRING_CHARS - 1] = 0;
	t_console_reset();
	expand_line(line, out, sizeof(out));
	T_CHECK(Cmd_Argc() == 1);
	T_EQ_STR(t_console(), "");
	line[MAX_STRING_CHARS - 1] = 'x';
	line[MAX_STRING_CHARS] = 0;
	expand_line(line, out, sizeof(out));
	T_EQ_STR(out, "");
	Com_sprintf(msg, sizeof(msg), "Line exceeded %i chars, discarded.",
			MAX_STRING_CHARS);
	T_CONSOLE_HAS(msg);

	/* "$t_mx_long" is 10 characters and adds 20: a line of
	   MAX_STRING_CHARS - 21 grows to MAX_STRING_CHARS - 1, one byte
	   longer to MAX_STRING_CHARS. */
	memset(line, 'x', MAX_STRING_CHARS - 21 - 11);
	strcpy(line + MAX_STRING_CHARS - 21 - 11, " $t_mx_long");
	T_EQ_INT(strlen(line), MAX_STRING_CHARS - 21);
	t_console_reset();
	expand_line(line, out, sizeof(out));
	T_CHECK(Cmd_Argc() == 2);
	T_EQ_STR(t_console(), "");
	memmove(line + 1, line, strlen(line) + 1);
	line[0] = 'x';
	expand_line(line, out, sizeof(out));
	T_EQ_STR(out, "");
	Com_sprintf(msg, sizeof(msg), "Expanded line exceeded %i chars, discarded.",
			MAX_STRING_CHARS);
	T_CONSOLE_HAS(msg);
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
	int start;

	/* "wait" stops the buffer until more than 16 ms of wall-clock time
	   (Sys_Milliseconds) have passed; Cbuf_Execute compares against the
	   time "wait" ran. No public function sets that clock or the wait
	   mark, so this test depends on real time, and is written so that it
	   cannot flake: the "still waiting" check is made only when the
	   second Cbuf_Execute provably came within 16 ms of the "wait" (the
	   wait ran at or after start), and the release follows a sleep
	   longer than the interval (usleep sleeps at least that long). */
	Sys_Milliseconds();
	usleep(2000);

	t_console_reset();
	start = Sys_Milliseconds();
	Cbuf_AddText("echo before; wait; echo after\n");
	Cbuf_Execute();
	T_EQ_STR(t_console(), "before \n");
	Cbuf_Execute();

	if (Sys_Milliseconds() - start <= 16)
	{
		T_EQ_STR(t_console(), "before \n");
	}

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

/* FIX165: Cvar_WriteVariables writes "set name "value"" and the tokenizer
   has no escape. A value that holds a quote would end the quoted string
   early and a ';' after it would start a second command at the next
   start. The word form of "set" reaches such a value, since COM_Parse
   keeps a quote in the middle of an unquoted word. */
TEST(cvar_write_refuses_unsafe_values)
{
	char path[] = "/tmp/q2ut_cvarwrite_XXXXXX";
	char buf[4096];
	FILE *f;
	size_t n;
	int fd;

	/* The hostile value is reachable through the command buffer. */
	run("set t_w_quote a\"b;t_w_inj\"c a\n");
	T_EQ_STR(Cvar_VariableString("t_w_quote"), "a\"b;t_w_inj\"c");

	/* A lone quote is not: the buffer discards the line. */
	run("set t_w_lone a\"b;t_w_inj a\n");
	T_CONSOLE_HAS("unmatched quote");
	T_EQ_STR(Cvar_VariableString("t_w_lone"), "");

	/* A raw newline from engine code. */
	Cvar_FullSet("t_w_nl", "x\nquit", CVAR_ARCHIVE);
	Cvar_FullSet("t_w_cr", "x\rquit", CVAR_ARCHIVE);

	/* Safe values, a ';' inside the quotes included, are written. */
	Cvar_FullSet("t_w_semi", "a;b", CVAR_ARCHIVE);
	Cvar_FullSet("t_w_ok", "fine", CVAR_ARCHIVE);

	fd = mkstemp(path);
	T_REQUIRE(fd >= 0);
	close(fd);

	t_console_reset();
	Cvar_WriteVariables(path);
	T_CONSOLE_HAS("t_w_quote");
	T_CONSOLE_HAS("t_w_nl");
	T_CONSOLE_HAS("t_w_cr");

	f = fopen(path, "r");
	T_REQUIRE(f != NULL);
	n = fread(buf, 1, sizeof(buf) - 1, f);
	buf[n] = '\0';
	fclose(f);
	unlink(path);

	T_CHECK(strstr(buf, "t_w_quote") == NULL);
	T_CHECK(strstr(buf, "t_w_nl") == NULL);
	T_CHECK(strstr(buf, "t_w_cr") == NULL);
	T_CHECK(strstr(buf, "set t_w_semi \"a;b\"\n") != NULL);
	T_CHECK(strstr(buf, "set t_w_ok \"fine\"\n") != NULL);
}
