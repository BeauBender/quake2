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
 * A binding a server line wrote runs as the server's text, and a server
 * write never removes or replaces, in what config.cfg saves, a binding
 * the player's own text wrote (FIX156). The real src/client/keybind.c:
 * Key_SetBinding, the press queueing Key_AddBindingText that Key_Event
 * calls, unbindall (Key_UnbindAll) and the bind lines of config.cfg
 * (Key_WriteBindingLines, which Key_WriteBindings calls after its
 * unbindall line). The lines run through the real Cbuf_Execute and
 * exec; `bind` and `unbind` are stand-ins for cl_keyboard.c's
 * Key_Bind_f and Key_Unbind_f (not linked: SDL and the key name table),
 * which parse a single-character key the same way and call the same
 * Key_SetBinding.
 *
 * =======================================================================
 */

#include "test.h"
#include "../../src/client/header/client.h"

extern sizebuf_t cmd_text;      /* src/common/cmdparser.c */
extern char defer_text_buf[];   /* src/common/cmdparser.c */
void Z_Stats_f(void);           /* src/common/zone.c */

static int probe_runs;   /* the probe ran */
static int probe_server; /* as the server's text */

static void
T_Kb_Probe_f(void)
{
	probe_runs++;

	if (Cbuf_ExecutingServerText())
	{
		probe_server++;
	}
}

/* Key_Bind_f for a single-character key: the rest of the line is the
   body, its arguments joined by a space. */
static void
T_Kb_Bind_f(void)
{
	char cmd[1024];
	const char *k;
	int i, c = Cmd_Argc();

	if (c < 3)
	{
		return;
	}

	k = Cmd_Argv(1);
	cmd[0] = 0;

	for (i = 2; i < c; i++)
	{
		Q_strlcat(cmd, Cmd_Argv(i), sizeof(cmd));

		if (i != (c - 1))
		{
			Q_strlcat(cmd, " ", sizeof(cmd));
		}
	}

	Key_SetBinding(k[0] && !k[1] ? (unsigned char)k[0] : -1, cmd);
}

/* Key_Unbind_f for a single-character key. */
static void
T_Kb_Unbind_f(void)
{
	const char *k = Cmd_Argv(1);

	Key_SetBinding(k[0] && !k[1] ? (unsigned char)k[0] : -1, "");
}

/* Key_SetBinding with a raw key number, as `bind "<0x80>" x` reached it
   when Key_StringToKeynum returned a signed char (-128 on arm64). */
static void
T_Kb_BindNum_f(void)
{
	Key_SetBinding(atoi(Cmd_Argv(1)), Cmd_Argv(2));
}

static void
kb_setup(void)
{
	Cmd_AddCommand("bind", T_Kb_Bind_f);
	Cmd_AddCommand("unbind", T_Kb_Unbind_f);
	Cmd_AddCommand("unbindall", Key_UnbindAll);
	Cmd_AddCommand("t_kb_bindnum", T_Kb_BindNum_f);
	Cmd_AddCommand("t_kb_probe", T_Kb_Probe_f);

	SZ_Clear(&cmd_text);
	defer_text_buf[0] = 0;
	Key_FreeBindings();
	probe_runs = 0;
	probe_server = 0;
}

static void
kb_teardown(void)
{
	Key_FreeBindings();
	SZ_Clear(&cmd_text);
	Cmd_RemoveCommand("bind");
	Cmd_RemoveCommand("unbind");
	Cmd_RemoveCommand("unbindall");
	Cmd_RemoveCommand("t_kb_bindnum");
	Cmd_RemoveCommand("t_kb_probe");
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

/* Key_Event's press of a key whose binding is not a +command. */
static void
kb_press(int key)
{
	if (keybindings[key] && keybindings[key][0])
	{
		Key_AddBindingText(key, keybindings[key]);
		Key_AddBindingText(key, "\n");
	}

	Cbuf_Execute();
}

/* How many keys a press would queue as the server's text. */
static int
kb_server_keys(void)
{
	int i, n = 0;

	for (i = 0; i < K_LAST; i++)
	{
		if (Key_BindingIsServer(i))
		{
			n++;
		}
	}

	return n;
}

/* The bind lines config.cfg would get now. */
static void
kb_written(char *out, size_t len)
{
	char path[MAX_OSPATH];
	size_t n;
	FILE *f;

	out[0] = 0;
	snprintf(path, sizeof(path), "%s/t_kb_written.cfg", t_tmpdir());
	f = fopen(path, "w+");
	T_REQUIRE(f != NULL);

	Key_WriteBindingLines(f);
	rewind(f);
	n = fread(out, 1, len - 1, f);
	out[n] = 0;
	fclose(f);
}

/* The player's own keys: typed, as default.cfg and config.cfg run them. */
static const char player_binds[] =
	"bind w \"t_kb_probe\"\n"
	"bind a \"+moveleft\"\n"
	"bind m \"say hello world\"\n"
	"unbind x\n";

/*
 * A server line runs the player's own config.cfg, as `exec config.cfg`
 * does, alone or after default.cfg (the order Qcommon_ExecConfigs
 * uses; a server line's `game <dir>` runs them as the player's text
 * since round 3, keybind_server_game_dir_keeps_dir_config). unbindall
 * first, with cfg_unbindall 1 (the default) or none with 0; then every
 * bind line. Every key stays the player's and what config.cfg saves is
 * what it saved before.
 */
static void
kb_server_runs_config(qboolean unbindall, qboolean defaults_first)
{
	static const char defaults[] =
		"unbindall\n"
		"bind w \"+forward\"\n"
		"bind x \"+back\"\n"
		"bind k \"kill\"\n";
	char before[4096], after[4096], config[4200];

	kb_setup();

	player_line(defaults);
	player_line(player_binds);
	kb_written(before, sizeof(before));
	T_CHECK(strstr(before, "bind w \"t_kb_probe\"\n") != NULL);

	snprintf(config, sizeof(config), "%s%s", unbindall ? "unbindall\n" : "", before);
	t_write_file("data/baseq2/t_kb_config.cfg", config, strlen(config));
	t_write_file("data/baseq2/t_kb_default.cfg", defaults, strlen(defaults));

	if (defaults_first)
	{
		server_line("exec t_kb_default.cfg\nexec t_kb_config.cfg\n");
	}
	else
	{
		server_line("exec t_kb_config.cfg\n");
	}

	if (defaults_first && !unbindall)
	{
		/* x: the player unbound it, default.cfg binds it, and a
		   config.cfg written with cfg_unbindall 0 cannot unbind it
		   again; it keeps default.cfg's binding as the server's, and
		   config.cfg still saves the player's (none) */
		T_EQ_INT(kb_server_keys(), 1);
		T_CHECK(Key_BindingIsServer('x'));
		T_EQ_STR(keybindings['x'], "+back");
	}
	else
	{
		T_EQ_INT(kb_server_keys(), 0);
	}

	kb_written(after, sizeof(after));
	T_EQ_STR(after, before);

	kb_press('w');
	T_EQ_INT(probe_runs, 1);
	T_EQ_INT(probe_server, 0);

	kb_teardown();
}

TEST(keybind_server_exec_config_unbindall_keeps_player_keys)
{
	kb_server_runs_config(true, false);
}

TEST(keybind_server_exec_config_keeps_player_keys)
{
	kb_server_runs_config(false, false);
}

TEST(keybind_server_exec_default_then_config_keeps_player_keys)
{
	kb_server_runs_config(true, true);
	kb_server_runs_config(false, true);
}

/*
 * A server line `game <dir>` on a remote client, where <dir> has a
 * config.cfg of its own that the player saved there earlier
 * (correctness round 2, B1). The directory change runs that file as the
 * player's text (Qcommon_ExecConfigs), so its binds are the player's and
 * the next write leaves it as it was: not the old directory's binds.
 */
TEST(keybind_server_game_dir_keeps_dir_config)
{
	static const char modcfg[] = "unbindall\nbind w \"+forward\"\n";
	char before[4096], after[4096];

	kb_setup();
	player_line(player_binds);
	kb_written(before, sizeof(before));
	T_CHECK(strstr(before, "bind w \"t_kb_probe\"\n") != NULL);

	t_write_file("data/t_kbmod/config.cfg", modcfg, strlen(modcfg));
	server_line("game t_kbmod\n");

	T_EQ_INT(kb_server_keys(), 0);
	T_EQ_STR(keybindings['w'], "+forward");
	kb_written(after, sizeof(after));
	T_EQ_STR(after, "bind w \"+forward\"\n");

	player_line("game \"\"\n");
	kb_teardown();
}

/* A server binds over a player's key: the press runs as the server's
   text, and config.cfg still saves the player's binding. */
TEST(keybind_server_bind_over_player_key)
{
	char before[4096], after[4096];

	kb_setup();

	player_line(player_binds);
	kb_written(before, sizeof(before));

	server_line("bind w \"t_kb_probe;t_kb_probe\"\n");
	T_EQ_STR(keybindings['w'], "t_kb_probe;t_kb_probe");
	T_CHECK(Key_BindingIsServer('w'));
	T_EQ_INT(kb_server_keys(), 1);

	kb_press('w');
	T_EQ_INT(probe_runs, 2);
	T_EQ_INT(probe_server, 2);

	kb_written(after, sizeof(after));
	T_EQ_STR(after, before);

	/* a server's unbind, or unbindall, removes nothing config.cfg saves */
	server_line("unbind a\nunbindall\n");
	kb_written(after, sizeof(after));
	T_EQ_STR(after, before);

	kb_teardown();
}

/* A server that writes the player's own binding back gives the key back
   to the player. */
TEST(keybind_server_writes_player_body_back)
{
	kb_setup();

	player_line(player_binds);
	server_line("bind w \"kill\"\n");
	T_CHECK(Key_BindingIsServer('w'));

	server_line("bind w \"t_kb_probe\"\n");
	T_CHECK(!Key_BindingIsServer('w'));

	kb_press('w');
	T_EQ_INT(probe_runs, 1);
	T_EQ_INT(probe_server, 0);

	kb_teardown();
}

/* The player rebinds a key after a server bound it: the key is the
   player's, and config.cfg saves the new binding. */
TEST(keybind_player_rebind_after_server_bind)
{
	char out[4096];

	kb_setup();

	player_line("bind w \"+forward\"\n");
	server_line("bind w \"kill\"\n");
	player_line("bind w \"t_kb_probe\"\n");

	T_CHECK(!Key_BindingIsServer('w'));
	T_EQ_INT(kb_server_keys(), 0);

	kb_press('w');
	T_EQ_INT(probe_runs, 1);
	T_EQ_INT(probe_server, 0);

	kb_written(out, sizeof(out));
	T_EQ_STR(out, "bind w \"t_kb_probe\"\n");

	kb_teardown();
}

/* The player's unbind and unbindall clear what config.cfg saves too, so
   a server's bind afterwards stays the server's and is not saved. */
TEST(keybind_player_unbind_clears_saved)
{
	char out[4096];

	kb_setup();

	player_line("bind w \"t_kb_probe\"\nbind a \"+moveleft\"\n");
	player_line("unbind w\n");
	kb_written(out, sizeof(out));
	T_EQ_STR(out, "bind a \"+moveleft\"\n");

	player_line("unbindall\n");
	kb_written(out, sizeof(out));
	T_EQ_STR(out, "");

	server_line("bind w \"t_kb_probe\"\n");
	T_CHECK(Key_BindingIsServer('w'));
	kb_written(out, sizeof(out));
	T_EQ_STR(out, "");

	kb_teardown();
}

/*
 * Every line the bind writer emits, run back through Cbuf_Execute,
 * runs exactly one bind with the same key and body (FIX166). A body
 * with a quote or a line break would end its own quoted string and
 * run the rest as a second command at the next start: the writer
 * skips it and says so.
 */
TEST(keybind_written_lines_round_trip)
{
	char out[4096];

	kb_setup();

	Key_SetBinding('h', "a\"b;t_kb_probe\"c");
	Key_SetBinding('n', "x\nt_kb_probe");
	Key_SetBinding('r', "x\rt_kb_probe");
	Key_SetBinding('f', "+forward");
	Key_SetBinding('s', "say a;b");
	Key_SetBinding('p', "t_kb_probe");

	t_console_reset();
	kb_written(out, sizeof(out));

	Key_FreeBindings();
	probe_runs = 0;
	player_line(out);

	/* only the plain probe bind exists; pressing it is the one run */
	T_EQ_INT(probe_runs, 0);
	T_EQ_STR(keybindings['f'], "+forward");
	T_EQ_STR(keybindings['s'], "say a;b");
	T_EQ_STR(keybindings['p'], "t_kb_probe");
	T_CHECK(keybindings['h'] == NULL || !keybindings['h'][0]);
	T_CHECK(keybindings['n'] == NULL || !keybindings['n'][0]);
	T_CHECK(keybindings['r'] == NULL || !keybindings['r'][0]);

	T_CONSOLE_HAS("Not saving bind for h: its body holds a quote or a line break.");
	T_CONSOLE_HAS("Not saving bind for n: its body holds a quote or a line break.");
	T_CONSOLE_HAS("Not saving bind for r: its body holds a quote or a line break.");

	kb_teardown();
}

/* Key numbers outside keybindings[], from a server's or the player's
   line: refused, nothing allocated, nothing queued. In a child, since
   the unchecked index wrote outside the arrays. */
static void
kb_zone_totals(void)
{
	char same[] = "t_kb_zone same";

	/* the tokenizer's own allocations, the same both times */
	Cmd_TokenizeString(same, false);
	Z_Stats_f();
}

static void
kb_bad_keynums_child(void *arg)
{
	char line[64];

	(void)arg;

	kb_setup();
	player_line(player_binds);

	kb_zone_totals();
	server_line("t_kb_bindnum -128 x\n");
	server_line("t_kb_bindnum -1 x\n");
	snprintf(line, sizeof(line), "t_kb_bindnum %d x\n", K_LAST);
	server_line(line);
	player_line("t_kb_bindnum -2 x\n");
	snprintf(line, sizeof(line), "t_kb_bindnum %d x\n", K_LAST + 100);
	player_line(line);

	Key_AddBindingText(-128, "t_kb_probe\n");
	Key_AddBindingText(K_LAST, "t_kb_probe\n");
	Cbuf_Execute();
	kb_zone_totals();

	T_CHECK(probe_runs == 0);
	T_CHECK(kb_server_keys() == 0);

	kb_teardown();
}

TEST(keybind_bad_keynum_refused)
{
	char out[8192];
	const char *first, *second;
	int status;

	status = t_expect_exit(kb_bad_keynums_child, NULL, out, sizeof(out));
	T_EQ_INT(status, 0);

	/* the zone's totals before and after are the same line */
	first = strstr(out, " blocks\n");
	second = first ? strstr(first + 1, " blocks\n") : NULL;
	T_REQUIRE(first != NULL && second != NULL);

	while (first > out && first[-1] != '\n')
	{
		first--;
	}

	{
		const char *s = second;
		size_t n = strcspn(first, "\n");

		while (s > out && s[-1] != '\n')
		{
			s--;
		}

		if (strncmp(first, s, n) != 0 || s[n] != '\n')
		{
			t_fail(__FILE__, __LINE__, "a refused key number allocated:\n%s", out);
		}
	}
}
