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
 * The game module's IP filter (src/game/g_svcmds.c, FIX30 and FIX16):
 * SV_FilterPacket reads the "ip" value the engine formats (bare or in
 * brackets, with a port) and StringToFilter parses an operator's mask.
 * g_svcmds.c is included whole; gi and filterban are small fakes.
 *
 * =======================================================================
 */

#include "test.h"
#include "../../src/game/g_svcmds.c"

game_import_t gi;
cvar_t *filterban;
game_locals_t game;
static game_import_t t_gi_saved;
static cvar_t t_filterban_cvar;

static void
t_cprintf(const edict_t *ent, int printlevel, const char *fmt, ...)
{
	(void)ent;
	(void)printlevel;
	(void)fmt;
}

static void
ipf_begin(int ban)
{
	t_gi_saved = gi;
	gi.cprintf = t_cprintf;
	filterban = &t_filterban_cvar;
	t_filterban_cvar.value = (float)ban;
	numipfilters = 0;
}

static void
ipf_end(void)
{
	gi = t_gi_saved;
	numipfilters = 0;
}

/* Parses s as an operator's mask into a one-entry list. */
static qboolean
ipf_add(const char *s)
{
	char buf[1200];

	Q_strlcpy(buf, s, sizeof(buf));
	numipfilters = 0;

	if (!StringToFilter(buf, &ipfilters[0]))
	{
		return false;
	}

	numipfilters = 1;
	return true;
}

/* The verdict for value v, in a buffer of its own length. */
static int
ipf_verdict(const char *v)
{
	char buf[64];

	Q_strlcpy(buf, v, sizeof(buf));
	return SV_FilterPacket(buf);
}

TEST(ipfilter_engine_forms)
{
	ipf_begin(1);
	T_CHECK(ipf_add("10.0.0.1"));

	/* the engine's bracketed form with a port, and the bare forms */
	T_CHECK(ipf_verdict("[10.0.0.1]:27901"));
	T_CHECK(!ipf_verdict("[10.0.0.2]:27901"));
	T_CHECK(ipf_verdict("10.0.0.1:27901"));
	T_CHECK(ipf_verdict("10.0.0.1"));
	T_CHECK(!ipf_verdict("10.0.0.2:27901"));

	/* a short mask entry matches the prefix */
	T_CHECK(ipf_add("10.0"));
	T_CHECK(ipf_verdict("[10.0.5.5]:1"));
	T_CHECK(!ipf_verdict("[11.0.5.5]:1"));

	/* the same listed address under filterban 0 is the one admitted */
	T_CHECK(ipf_add("10.0.0.1"));
	t_filterban_cvar.value = 0;
	T_CHECK(!ipf_verdict("[10.0.0.1]:27901"));
	T_CHECK(ipf_verdict("[10.0.0.2]:27901"));
	ipf_end();
}

TEST(ipfilter_partial_values)
{
	const char *v[] = { "", "1", "1.2", "1.2.3", "1.2.3.4", "10.9.9.9:27901", "10" };
	int want[] = { 0, 0, 0, 0, 0, 1, 1 };
	size_t i;

	ipf_begin(1);
	numipfilters = 1;
	ipfilters[0].mask = 0x000000ff;
	ipfilters[0].compare = 0x0a;

	for (i = 0; i < sizeof(v) / sizeof(v[0]); i++)
	{
		T_EQ_INT(ipf_verdict(v[i]), want[i]);
	}

	/* every byte is compared: an unwritten byte reads as 0 */
	ipfilters[0].mask = 0xffffffffu;
	ipfilters[0].compare = 0;
	T_CHECK(ipf_verdict(""));
	T_CHECK(ipf_verdict("0.0"));
	T_CHECK(!ipf_verdict("1.2.3"));
	T_CHECK(!SV_FilterPacket(NULL));

	/* IPv6 in the engine's bracketed form: the first group reads as no
	   digits (0.0.0.0), and the parse stops at the closing bracket */
	T_CHECK(ipf_verdict("[::1]:27901"));

	/* the closing bracket ends the value, as a port does */
	ipfilters[0].compare = 0x0a;
	T_CHECK(ipf_verdict("[10]5.6"));
	T_CHECK(ipf_verdict("10:5.6"));
	ipf_end();
}

TEST(ipfilter_octet_digits)
{
	char big[1100];
	char ok[] = "1.2.3.4";
	ipfilter_t f;
	int lens[] = { 126, 127, 128, 129, 1000 };
	size_t i;

	ipf_begin(1);

	for (i = 0; i < sizeof(lens) / sizeof(lens[0]); i++)
	{
		memset(big, '1', (size_t)lens[i]);
		big[lens[i]] = '\0';
		T_EQ_INT(StringToFilter(big, &f) != 0, lens[i] <= 127);

		memcpy(big, "1.", 2);
		memset(big + 2, '1', (size_t)lens[i]);
		big[2 + lens[i]] = '\0';
		T_EQ_INT(StringToFilter(big, &f) != 0, lens[i] <= 127);
	}

	T_CHECK(StringToFilter(ok, &f));
	T_CHECK(f.mask == 0xffffffffu && f.compare == 0x04030201u);
	ipf_end();
}
