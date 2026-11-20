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
 * Text a server put in the command buffer does not outlive the
 * connection it came from (FIX113).
 *
 * svc_stufftext (src/client/cl_parse.c) queues server text in the
 * command buffer (src/common/cmdparser.c). When that text drops the
 * connection, the rest of it must not run afterwards, while the client
 * is disconnected: a command that refuses while connected would run
 * anyway. The player's own text, queued before or after the server's,
 * and a player's own "disconnect; <cmd>", behave as before.
 *
 * The suite is built with DEDICATED_ONLY, so Com_Error(ERR_DROP) does
 * not call CL_Drop itself; each test calls it after the drop, as
 * Com_Error does in the client.
 *
 * =======================================================================
 */

#include "test.h"
#include "../../src/client/header/client.h"
#include "stub_client.h"

extern sizebuf_t cmd_text; /* src/common/cmdparser.c */

static byte msgbuf[4096];
static byte netbuf[4096];
static int gated_runs;
static int player_runs;

/* Stands in for a command that refuses while connected, as
   upscale_textures does: it counts only runs while disconnected. */
static void
T_Gated_f(void)
{
	if (cls.state == ca_disconnected)
	{
		gated_runs++;
	}
}

static void
T_Player_f(void)
{
	player_runs++;
}

static void
setup(void)
{
	stub_client_cvars();
	memset(&cls, 0, sizeof(cls));
	memset(&cl, 0, sizeof(cl));
	SZ_Init(&cls.netchan.message, netbuf, sizeof(netbuf));
	cls.state = ca_connected;
	cls.serverProtocol = PROTOCOL_VERSION;
	gated_runs = 0;
	player_runs = 0;

	SZ_Clear(&cmd_text);

	if (!Cmd_Exists("disconnect"))
	{
		Cmd_AddCommand("disconnect", CL_Disconnect_f);
	}

	if (!Cmd_Exists("t_gated"))
	{
		Cmd_AddCommand("t_gated", T_Gated_f);
	}

	if (!Cmd_Exists("t_player"))
	{
		Cmd_AddCommand("t_player", T_Player_f);
	}
}

/* The server sends one svc_stufftext with this text. */
static void
stufftext(const char *text)
{
	SZ_Init(&net_message, msgbuf, sizeof(msgbuf));
	MSG_WriteByte(&net_message, svc_stufftext);
	MSG_WriteString(&net_message, text);
	MSG_BeginReading(&net_message);
	CL_ParseServerMessage();
}

/* One frame whose command buffer drops the connection, then the next
   frame's Cbuf_Execute. */
static void
drop_frame_then_next(void)
{
	T_EXPECT_DROP(Cbuf_Execute());
	CL_Drop();
	T_EQ_INT(cls.state, ca_disconnected);
	Cbuf_Execute();
}

/* Server text after a "disconnect" in the same stufftext does not run
   once the connection is gone. */
TEST(cl_stufftext_after_disconnect_dropped)
{
	setup();
	stufftext("disconnect\nt_gated\n");
	drop_frame_then_next();
	T_EQ_INT(gated_runs, 0);
	T_EQ_INT(cmd_text.cursize, 0);
}

/* The same with ";" separators and with the rest in a second stufftext
   of the same connection. */
TEST(cl_stufftext_later_packets_dropped)
{
	setup();
	stufftext("disconnect;t_gated;");
	stufftext("t_gated\n");
	drop_frame_then_next();
	T_EQ_INT(gated_runs, 0);
}

/* The player's own "disconnect; <cmd>" still runs <cmd>. */
TEST(cl_player_disconnect_then_cmd_runs)
{
	setup();
	Cbuf_AddText("disconnect; t_gated\n");
	drop_frame_then_next();
	T_EQ_INT(gated_runs, 1);
}

/* Player text queued after the server text survives the drop the
   server text causes; the server text does not. */
TEST(cl_player_text_after_server_text_survives)
{
	setup();
	Cbuf_AddText("t_player\n");
	stufftext("disconnect\nt_gated\n");
	Cbuf_AddText("t_player; t_gated\n");
	drop_frame_then_next();
	T_EQ_INT(player_runs, 2);
	T_EQ_INT(gated_runs, 1);
}

/* Player text queued before and after the server text survives a
   disconnect that leaves all three in the buffer. */
TEST(cl_player_text_around_server_text_survives)
{
	setup();
	Cbuf_AddText("t_player\n");
	stufftext("t_gated\n");
	Cbuf_AddText("t_player; t_gated\n");
	CL_Disconnect();
	Cbuf_Execute();
	T_EQ_INT(player_runs, 2);
	T_EQ_INT(gated_runs, 1);
}

/* Text a server line puts in the buffer, here through an alias it
   defines and runs, is the server's too. */
TEST(cl_server_alias_text_dropped)
{
	setup();
	stufftext("alias t_srv \"disconnect; t_gated\"\nt_srv\n");
	drop_frame_then_next();
	T_EQ_INT(gated_runs, 0);
	Cbuf_AddText("unalias t_srv\n");
	Cbuf_Execute();
}
