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
 * How many entities a frame may carry (FIX11), and what the server message
 * parser does with a bad configstring index or text:
 *
 * - CL_DeltaEntity in src/client/cl_parse.c refuses an entity once the
 *   frame already holds MAX_EDICTS of them. It is static; the tests
 *   reach it through CL_ParseServerMessage with a real svc_frame
 *   message, as a server sends it.
 * - S_BuildSoundList in src/client/sound/sound.c never writes more than
 *   maxsounds entries, however many entities the frame has. The array in
 *   the test is exactly maxsounds long, so a write past it is an
 *   out-of-bounds write the address sanitizer reports.
 *
 * =======================================================================
 */

#include <limits.h>

#include "test.h"
#include "../../src/client/header/client.h"
#include "../../src/client/sound/header/local.h"
#include "stub_client.h"

static byte msgbuf[65536];

/* The message a server sends for one frame carrying the entities 1..count,
   each as an unchanged delta from the baseline. */
static void
build_frame(int count)
{
	int n;

	SZ_Init(&net_message, msgbuf, sizeof(msgbuf));
	MSG_WriteByte(&net_message, svc_frame);
	MSG_WriteLong(&net_message, 1);      /* server frame */
	MSG_WriteLong(&net_message, -1);     /* no delta frame: from scratch */
	MSG_WriteByte(&net_message, 0);      /* suppress count */
	MSG_WriteByte(&net_message, 0);      /* no area bits */
	MSG_WriteByte(&net_message, svc_playerinfo);
	MSG_WriteShort(&net_message, 0);     /* no player state fields */
	MSG_WriteLong(&net_message, 0);      /* no stats */
	MSG_WriteByte(&net_message, svc_packetentities);

	for (n = 1; n <= count; n++)
	{
		if (n < 256)
		{
			MSG_WriteByte(&net_message, 0);
			MSG_WriteByte(&net_message, n);
		}
		else
		{
			MSG_WriteByte(&net_message, U_MOREBITS1);
			MSG_WriteByte(&net_message, U_NUMBER16 >> 8);
			MSG_WriteShort(&net_message, n);
		}
	}

	MSG_WriteByte(&net_message, 0);      /* entity 0 ends the list */
	MSG_WriteByte(&net_message, 0);
}

static void
reset_client(void)
{
	stub_client_cvars();
	memset(&cls, 0, sizeof(cls));
	memset(&cl, 0, sizeof(cl));
	SZ_Init(&cls.netchan.message, msgbuf, sizeof(msgbuf));
	cls.state = ca_connected;
	cls.serverProtocol = PROTOCOL_VERSION;
}

/* A frame with exactly MAX_EDICTS entities is a full frame, and is taken. */
TEST(cl_frame_with_max_entities_accepted)
{
	reset_client();
	build_frame(MAX_EDICTS);
	MSG_BeginReading(&net_message);

	CL_ParseServerMessage();

	T_EQ_INT(cl.frame.num_entities, MAX_EDICTS);
	T_CHECK(cl.frame.valid);
	T_EQ_INT(cls.state, ca_active);
	T_EQ_STR(t_console(), "");
}

/* One entity more than that is refused as a protocol error. */
TEST(cl_frame_one_past_max_entities_refused)
{
	reset_client();
	build_frame(MAX_EDICTS + 1);
	MSG_BeginReading(&net_message);

	T_EXPECT_DROP(CL_ParseServerMessage());
	T_CONSOLE_HAS("too many entities in frame");
	T_CHECK(cl.frame.num_entities <= MAX_EDICTS);
	T_CHECK(cls.state != ca_active);
}

/* A frame far past the limit is refused at the same point. */
TEST(cl_frame_far_past_max_entities_refused)
{
	reset_client();
	build_frame(4000);
	MSG_BeginReading(&net_message);

	T_EXPECT_DROP(CL_ParseServerMessage());
	T_CONSOLE_HAS("too many entities in frame");
	T_CHECK(cl.frame.num_entities <= MAX_EDICTS);
}

/* The sound list for a frame of the given size, in an array exactly
   maxsounds long. Returns how many entries were written (non-zero). */
static int
build_sound_list(int num_entities, int maxsounds)
{
	int *sounds = malloc(maxsounds * sizeof(*sounds));
	int i, written = 0;

	T_REQUIRE(sounds != NULL);
	memset(&cl, 0, sizeof(cl));
	s_ambient = Cvar_Get("s_ambient", "1", 0);
	Cvar_Set("s_ambient", "1");

	for (i = 0; i < MAX_PARSE_ENTITIES; i++)
	{
		memset(&cl_parse_entities[i], 0, sizeof(cl_parse_entities[i]));
		cl_parse_entities[i].sound = i + 1;
	}

	for (i = 0; i < maxsounds; i++)
	{
		sounds[i] = -1;
	}

	cl.frame.num_entities = num_entities;
	cl.frame.parse_entities = 0;
	S_BuildSoundList(sounds, maxsounds);

	for (i = 0; i < maxsounds; i++)
	{
		if (sounds[i] != -1)
		{
			written++;
		}
	}

	free(sounds);
	return written;
}

/* A frame that exactly fills the list fills all of it. */
TEST(sound_list_frame_fills_list)
{
	T_EQ_INT(build_sound_list(MAX_EDICTS, MAX_EDICTS), MAX_EDICTS);
	T_EQ_INT(build_sound_list(1, MAX_EDICTS), 1);
	T_EQ_INT(build_sound_list(0, MAX_EDICTS), 0);
}

/* A frame with more entities than the list has room for is cut at the list. */
TEST(sound_list_frame_past_list_is_cut)
{
	T_EQ_INT(build_sound_list(MAX_EDICTS + 1, MAX_EDICTS), MAX_EDICTS);
	T_EQ_INT(build_sound_list(100000, MAX_EDICTS), MAX_EDICTS);
	T_EQ_INT(build_sound_list(MAX_EDICTS, 10), 10);
	T_EQ_INT(build_sound_list(11, 10), 10);
}

/* A configstring message: svc_configstring, index, text. */
static void
send_configstring(int index, const char *text)
{
	SZ_Init(&net_message, msgbuf, sizeof(msgbuf));
	MSG_WriteByte(&net_message, svc_configstring);
	MSG_WriteShort(&net_message, index);
	MSG_WriteString(&net_message, text);
	MSG_BeginReading(&net_message);
	CL_ParseServerMessage();
}

/* The text of a message that prints, lays out and stuffs, and the
   configstring indexes at and past the end of the table. */
TEST(cl_server_messages_and_configstring_bounds)
{
	char longtext[MAX_QPATH + 8];

	reset_client();

	SZ_Init(&net_message, msgbuf, sizeof(msgbuf));
	MSG_WriteByte(&net_message, svc_nop);
	MSG_WriteByte(&net_message, svc_print);
	MSG_WriteByte(&net_message, PRINT_HIGH);
	MSG_WriteString(&net_message, "hello from the server\n");
	MSG_WriteByte(&net_message, svc_layout);
	MSG_WriteString(&net_message, "xv 0 yv 0");
	MSG_WriteByte(&net_message, svc_centerprint);
	MSG_WriteString(&net_message, "centered");
	MSG_BeginReading(&net_message);
	t_console_reset();
	CL_ParseServerMessage();
	T_CONSOLE_HAS("hello from the server");
	T_EQ_STR(cl.layout, "xv 0 yv 0");

	/* The last index of the table is taken, the one after it is not. */
	send_configstring(MAX_CONFIGSTRINGS - 1, "last");
	T_EQ_STR(cl.configstrings[MAX_CONFIGSTRINGS - 1], "last");

	t_console_reset();
	send_configstring(MAX_CONFIGSTRINGS, "past");
	T_CONSOLE_HAS("bad index");
	t_console_reset();
	send_configstring(-1, "before");
	T_CONSOLE_HAS("bad index");

	/* A string that fills its entry is taken, one byte longer is not. */
	memset(longtext, 'x', sizeof(longtext));
	longtext[MAX_QPATH - 1] = 0;
	send_configstring(CS_NAME, longtext);
	T_EQ_STR(cl.configstrings[CS_NAME], longtext);

	longtext[MAX_QPATH - 1] = 'x';
	longtext[MAX_QPATH] = 0;
	t_console_reset();
	send_configstring(CS_NAME + 1, longtext);
	T_CONSOLE_HAS("string too long");
	T_EQ_STR(cl.configstrings[CS_NAME + 1], "");
}

/* ---- FIX35: a frame with no old frame, and entity removal ---- */

/* A frame carrying the given records as the entity list. A record is a
   bits byte and an entity number byte. deltaframe is the server frame it
   is coded against, -1 for none. */
static void
build_records(int serverframe, int deltaframe, const int *rec, int nrec)
{
	int i;

	SZ_Init(&net_message, msgbuf, sizeof(msgbuf));
	MSG_WriteByte(&net_message, svc_frame);
	MSG_WriteLong(&net_message, serverframe);
	MSG_WriteLong(&net_message, deltaframe);
	MSG_WriteByte(&net_message, 0);
	MSG_WriteByte(&net_message, 0);
	MSG_WriteByte(&net_message, svc_playerinfo);
	MSG_WriteShort(&net_message, 0);
	MSG_WriteLong(&net_message, 0);
	MSG_WriteByte(&net_message, svc_packetentities);

	for (i = 0; i < nrec; i++)
	{
		MSG_WriteByte(&net_message, rec[i * 2]);
		MSG_WriteByte(&net_message, rec[i * 2 + 1]);
	}

	MSG_WriteByte(&net_message, 0);
	MSG_WriteByte(&net_message, 0);
	MSG_BeginReading(&net_message);
}

static int
frame_entity(int index)
{
	return cl_parse_entities[(cl.frame.parse_entities + index) &
		(MAX_PARSE_ENTITIES - 1)].number;
}

/* A removal record in a frame that has no old frame drops the connection,
   whether it comes after other records or first. */
TEST(cl_removal_without_old_frame_refused)
{
	static const int rec[] = { 0, 3, U_REMOVE, 5, 0, 7 };

	reset_client();
	build_records(1, -1, rec, 3);

	T_EXPECT_DROP(CL_ParseServerMessage());
	T_CONSOLE_HAS("removal without an old frame");
	T_CHECK(cls.state != ca_active);
}

TEST(cl_leading_removal_without_old_frame_refused)
{
	static const int rec[] = { U_REMOVE, 1 };

	reset_client();
	build_records(1, -1, rec, 1);

	T_EXPECT_DROP(CL_ParseServerMessage());
	T_CONSOLE_HAS("removal without an old frame");
}

/* The other uses of oldframe (the unchanged run before a record, the delta
   from the old entity, the copy of the remainder) are not entered without
   an old frame: every record is taken from the baseline, in order. */
TEST(cl_records_without_old_frame_come_from_baseline)
{
	static const int rec[] = { 0, 3, 0, 4, 0, 200 };

	reset_client();
	build_records(1, -1, rec, 3);

	CL_ParseServerMessage();

	T_EQ_INT(cl.frame.num_entities, 3);
	T_EQ_INT(frame_entity(0), 3);
	T_EQ_INT(frame_entity(2), 200);
}

/* With an old frame, a removal drops that entity and the unchanged ones
   around it are carried over. */
TEST(cl_removal_with_old_frame_removes_entity)
{
	static const int first[] = { 0, 3, 0, 4, 0, 5 };
	static const int second[] = { U_REMOVE, 4 };

	reset_client();
	build_records(1, -1, first, 3);
	CL_ParseServerMessage();
	T_EQ_INT(cl.frame.num_entities, 3);

	build_records(2, 1, second, 1);
	CL_ParseServerMessage();

	T_CHECK(cl.frame.valid);
	T_EQ_INT(cl.frame.num_entities, 2);
	T_EQ_INT(frame_entity(0), 3);
	T_EQ_INT(frame_entity(1), 5);
}

/* ---- FIX12: the player number in svc_serverdata ---- */

static void
build_serverdata(int playernum)
{
	SZ_Init(&net_message, msgbuf, sizeof(msgbuf));
	MSG_WriteByte(&net_message, svc_serverdata);
	MSG_WriteLong(&net_message, PROTOCOL_VERSION);
	MSG_WriteLong(&net_message, 1);
	MSG_WriteByte(&net_message, 0);
	MSG_WriteString(&net_message, "");
	MSG_WriteShort(&net_message, playernum);
	MSG_WriteString(&net_message, "level");
	MSG_BeginReading(&net_message);
}

/* ---- FIX169: the game directory in svc_serverdata ---- */

static void
build_serverdata_game(const char *game)
{
	SZ_Init(&net_message, msgbuf, sizeof(msgbuf));
	MSG_WriteByte(&net_message, svc_serverdata);
	MSG_WriteLong(&net_message, PROTOCOL_VERSION);
	MSG_WriteLong(&net_message, 1);
	MSG_WriteByte(&net_message, 0);
	MSG_WriteString(&net_message, game);
	MSG_WriteShort(&net_message, 0);
	MSG_WriteString(&net_message, "level");
	MSG_BeginReading(&net_message);
}

static void
refused_game(const char *game)
{
	extern cvar_t *fs_gamedirvar;
	char old[MAX_QPATH];

	reset_client();
	Q_strlcpy(old, fs_gamedirvar->string, sizeof(old));
	build_serverdata_game(game);
	T_EXPECT_DROP(CL_ParseServerMessage());
	T_CONSOLE_HAS("invalid game directory");
	T_EQ_STR(fs_gamedirvar->string, old);
	T_EQ_STR(cl.gamedir, "");
}

TEST(cl_gamedir_control_byte_refused)
{
	refused_game("x\ny");
}

TEST(cl_gamedir_slash_refused)
{
	refused_game("a/b");
}

TEST(cl_gamedir_dotdot_refused)
{
	refused_game("..");
}

TEST(cl_gamedir_colon_refused)
{
	refused_game("a:b");
}

TEST(cl_gamedir_backslash_refused)
{
	refused_game("a\\b");
}

TEST(cl_gamedir_del_refused)
{
	refused_game("a\x7f" "b");
}

TEST(cl_gamedir_lone_dot_refused)
{
	refused_game(".");
}

TEST(cl_gamedir_empty_accepted)
{
	extern cvar_t *fs_gamedirvar;
	char old[MAX_QPATH];

	reset_client();
	Q_strlcpy(old, fs_gamedirvar->string, sizeof(old));
	build_serverdata_game("");
	CL_ParseServerMessage();
	T_EQ_STR(fs_gamedirvar->string, "");
	T_EQ_STR(cl.gamedir, "");
	Cvar_Set("game", old);
}

TEST(fs_gamedirvalid_table)
{
	T_CHECK(FS_GameDirValid(""));
	T_CHECK(FS_GameDirValid("ctf"));
	T_CHECK(!FS_GameDirValid("a:b"));
	T_CHECK(!FS_GameDirValid("a\\b"));
	T_CHECK(!FS_GameDirValid("a\x7f" "b"));
	T_CHECK(!FS_GameDirValid("."));
	T_CHECK(!FS_GameDirValid(".."));
	T_CHECK(!FS_GameDirValid("a/b"));
	T_CHECK(!FS_GameDirValid("x\ny"));

	/* the MAX_QPATH cap: 63 bytes are accepted, 64 refused */
	T_CHECK(FS_GameDirValid(
		"012345678901234567890123456789012345678901234567890123456789012"));
	T_CHECK(!FS_GameDirValid(
		"0123456789012345678901234567890123456789012345678901234567890123"));
}

TEST(cl_gamedir_plain_accepted)
{
	extern cvar_t *fs_gamedirvar;
	char old[MAX_QPATH];

	reset_client();
	Q_strlcpy(old, fs_gamedirvar->string, sizeof(old));
	build_serverdata_game("ctf");
	CL_ParseServerMessage();
	T_EQ_STR(fs_gamedirvar->string, "ctf");
	T_EQ_STR(cl.gamedir, "ctf");
	Cvar_Set("game", old);
}

TEST(cl_playernum_minus_two_refused)
{
	reset_client();
	build_serverdata(-2);
	T_EXPECT_DROP(CL_ParseServerMessage());
	T_CONSOLE_HAS("playernum");
}

TEST(cl_playernum_minus_one_is_cinematic)
{
	reset_client();
	build_serverdata(-1);
	CL_ParseServerMessage();
	T_EQ_INT(cl.playernum, -1);
	T_EQ_STR(t_console(), "");
}

TEST(cl_playernum_zero_joins)
{
	reset_client();
	build_serverdata(0);
	CL_ParseServerMessage();
	T_EQ_INT(cl.playernum, 0);
	T_CONSOLE_HAS("level");
}

TEST(cl_playernum_last_client_joins)
{
	reset_client();
	build_serverdata(MAX_CLIENTS - 1);
	CL_ParseServerMessage();
	T_EQ_INT(cl.playernum, MAX_CLIENTS - 1);
	T_CONSOLE_HAS("level");
}

TEST(cl_playernum_max_clients_refused)
{
	reset_client();
	build_serverdata(MAX_CLIENTS);
	T_EXPECT_DROP(CL_ParseServerMessage());
	T_CONSOLE_HAS("playernum");
}

/* ---- the client time (FIX39) ---- */

/* One frame with the given number and nothing in it. */
static void
build_numbered_frame(int serverframe)
{
	SZ_Init(&net_message, msgbuf, sizeof(msgbuf));
	MSG_WriteByte(&net_message, svc_frame);
	MSG_WriteLong(&net_message, serverframe);
	MSG_WriteLong(&net_message, -1);
	MSG_WriteByte(&net_message, 0);
	MSG_WriteByte(&net_message, 0);
	MSG_WriteByte(&net_message, svc_playerinfo);
	MSG_WriteShort(&net_message, 0);
	MSG_WriteLong(&net_message, 0);
	MSG_WriteByte(&net_message, svc_packetentities);
	MSG_WriteByte(&net_message, 0);
	MSG_WriteByte(&net_message, 0);
	MSG_BeginReading(&net_message);
}

/* The distance from the client time up to the server time. */
static int
time_behind(void)
{
	return (int)((unsigned)cl.frame.servertime - (unsigned)cl.time);
}

/* The frame is taken, its time is the wrapped product, and the client
   time sits in the window just behind it. */
static void
check_frame_number(int serverframe)
{
	reset_client();
	build_numbered_frame(serverframe);
	CL_ParseServerMessage();
	T_EQ_INT(cl.frame.serverframe, serverframe);
	T_EQ_INT(cl.frame.servertime, (int)((unsigned)serverframe * 100u));
	T_CHECK(cl.frame.valid);
	T_CHECK(time_behind() >= 0);
	T_CHECK(time_behind() <= 100);
}

static void
check_frame_number_refused(int serverframe)
{
	reset_client();
	build_numbered_frame(serverframe);
	T_EXPECT_DROP(CL_ParseServerMessage());
	T_CONSOLE_HAS("bad frame number");
}

TEST(cl_frame_number_zero)
{
	check_frame_number(0);
	T_EQ_INT(cl.frame.servertime, 0);
}

TEST(cl_frame_number_one)
{
	check_frame_number(1);
	T_EQ_INT(cl.frame.servertime, 100);
}

TEST(cl_frame_number_minus_one_refused)
{
	check_frame_number_refused(-1);
}

TEST(cl_frame_number_int_min_refused)
{
	check_frame_number_refused(INT_MIN);
}

/* The largest number whose product with 100 fits an int. */
TEST(cl_frame_number_last_without_overflow)
{
	check_frame_number(INT_MAX / 100);
	T_EQ_INT(cl.frame.servertime, (INT_MAX / 100) * 100);
	T_EQ_INT(cl.time, cl.frame.servertime - 100);
}

/* One more wraps: the time goes through the edge and the clamp follows. */
TEST(cl_frame_number_first_with_wrap)
{
	check_frame_number(INT_MAX / 100 + 1);
	T_CHECK(cl.frame.servertime < 0);
}

TEST(cl_frame_number_int_max)
{
	check_frame_number(INT_MAX);
}

/* A client time already near the top follows a wrapped server time: it
   is 93 behind modulo 2^32, inside the window, so it is kept. A signed
   comparison would see it far ahead and pull it to the server time. */
TEST(cl_time_follows_server_time_across_wrap)
{
	reset_client();
	cl.time = INT_MAX - 40;
	build_numbered_frame(INT_MAX / 100 + 1);
	CL_ParseServerMessage();
	T_CHECK(cl.frame.servertime < 0);
	T_CHECK(time_behind() >= 0);
	T_CHECK(time_behind() <= 100);
	T_EQ_INT(cl.time, INT_MAX - 40);
}

/* The client time after a frame numbered 10 (server time 1000), from
   the given time before it. */
static int
time_after_frame_ten(int before)
{
	reset_client();
	cl.time = before;
	build_numbered_frame(10);
	CL_ParseServerMessage();
	return cl.time;
}

/* The window edges: 1000 and 900 stay, one step outside is pulled in. */
TEST(cl_time_clamp_edges)
{
	T_EQ_INT(time_after_frame_ten(1000), 1000);
	T_EQ_INT(time_after_frame_ten(1001), 1000);
	T_EQ_INT(time_after_frame_ten(900), 900);
	T_EQ_INT(time_after_frame_ten(899), 900);
	T_EQ_INT(time_after_frame_ten(950), 950);
	T_EQ_INT(time_after_frame_ten(-5), 900);
}
