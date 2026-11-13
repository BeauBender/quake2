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
