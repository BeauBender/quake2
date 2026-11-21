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
extern cvar_t *qport;      /* src/common/netchan.c */

static byte msgbuf[4096];
static byte netbuf[4096];
static int gated_runs;
static int player_runs;

/* Stands in for a command that refuses while connected, as
   upscale_textures does: it counts the runs the real gate lets through,
   every state below ca_connected (CL_UpscaleAllowed is called with
   cls.state >= ca_connected). */
static void
T_Gated_f(void)
{
	if (cls.state < ca_connected)
	{
		gated_runs++;
	}
}

/* A server line that drops the connection. */
static void
T_Err_f(void)
{
	Com_Error(ERR_DROP, "t_err");
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

	/* CL_Disconnect sends its goodbye through Netchan_Transmit, which
	   reads the qport and showpackets cvars. An IP address with no open
	   socket sends nothing, where the zeroed loopback address would leave
	   the packets queued for the server tests. */
	if (!qport)
	{
		Netchan_Init();
	}

	cls.netchan.remote_address.type = NA_IP;
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

	if (!Cmd_Exists("t_err"))
	{
		Cmd_AddCommand("t_err", T_Err_f);
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
	T_EQ_INT(cmd_text.cursize, 0);
}

/* svc_reconnect ends the connection without CL_Disconnect. Server text
   queued before it in the same message does not run in the same frame's
   Cbuf_Execute, where the state is below ca_connected. */
TEST(cl_stufftext_before_reconnect_dropped)
{
	setup();
	cls.state = ca_active;
	SZ_Init(&net_message, msgbuf, sizeof(msgbuf));
	MSG_WriteByte(&net_message, svc_stufftext);
	MSG_WriteString(&net_message, "t_gated\n");
	MSG_WriteByte(&net_message, svc_reconnect);
	MSG_BeginReading(&net_message);
	CL_ParseServerMessage();
	T_EQ_INT(cls.state, ca_connecting);
	Cbuf_Execute();
	T_EQ_INT(gated_runs, 0);
	T_EQ_INT(cmd_text.cursize, 0);
}

/* The same with the stufftext after svc_reconnect in the message: the
   parse loop goes on after svc_reconnect, and the text is not queued. */
TEST(cl_stufftext_after_reconnect_dropped)
{
	setup();
	cls.state = ca_active;
	SZ_Init(&net_message, msgbuf, sizeof(msgbuf));
	MSG_WriteByte(&net_message, svc_reconnect);
	MSG_WriteByte(&net_message, svc_stufftext);
	MSG_WriteString(&net_message, "t_gated\n");
	MSG_BeginReading(&net_message);
	CL_ParseServerMessage();
	T_EQ_INT(cls.state, ca_connecting);
	Cbuf_Execute();
	T_EQ_INT(gated_runs, 0);
	T_EQ_INT(cmd_text.cursize, 0);
}

/* After a server line drops the connection, the buffer's source is the
   player again: player text queued after the drop is the player's and
   survives a later CL_Disconnect. */
TEST(cl_drop_resets_source_to_player)
{
	setup();
	stufftext("t_err\n");
	drop_frame_then_next();
	Cbuf_AddText("t_player\n");
	CL_Disconnect();
	Cbuf_Execute();
	T_EQ_INT(player_runs, 1);
}

/* The same when the client was already disconnected, where CL_Drop
   returns before it reaches CL_Disconnect. */
TEST(cl_drop_while_disconnected_resets_source_to_player)
{
	setup();
	cls.state = ca_disconnected;
	Cbuf_AddServerText("t_err\n");
	T_EXPECT_DROP(Cbuf_Execute());
	CL_Drop();
	Cbuf_AddText("t_player\n");
	CL_Disconnect();
	Cbuf_Execute();
	T_EQ_INT(player_runs, 1);
}

/* Cbuf_ExecutingServerText (FIX151): what t_src saw, and how often. */
static int src_seen;
static int src_runs;

static void
T_Src_f(void)
{
	src_seen = Cbuf_ExecutingServerText();
	src_runs++;
}

static void
src_setup(void)
{
	setup();
	src_seen = -1;
	src_runs = 0;

	if (!Cmd_Exists("t_src"))
	{
		Cmd_AddCommand("t_src", T_Src_f);
	}
}

/* True only while a server line runs: not while its text waits in the
   buffer, not after it, and not for the player's line. */
TEST(cbuf_executing_server_text_only_inside_a_server_line)
{
	src_setup();
	T_EQ_INT(Cbuf_ExecutingServerText(), 0);
	Cbuf_AddServerText("t_src\n");
	T_EQ_INT(Cbuf_ExecutingServerText(), 0);
	Cbuf_Execute();
	T_EQ_INT(src_runs, 1);
	T_EQ_INT(src_seen, 1);
	T_EQ_INT(Cbuf_ExecutingServerText(), 0);

	Cbuf_AddText("t_src\n");
	Cbuf_Execute();
	T_EQ_INT(src_runs, 2);
	T_EQ_INT(src_seen, 0);
}

/* A line with any server byte in it is the server's. */
TEST(cbuf_executing_server_text_for_a_mixed_line)
{
	src_setup();
	Cbuf_AddServerText("t_s");
	Cbuf_AddText("rc\n");
	Cbuf_Execute();
	T_EQ_INT(src_runs, 1);
	T_EQ_INT(src_seen, 1);
	T_EQ_INT(Cbuf_ExecutingServerText(), 0);
}

/* False again after a server line leaves Cbuf_Execute through
   Com_Error, once CL_Drop has run, as Com_Error runs it in the client.
   Com_Error resets the source itself, before its teardown (FIX153;
   FIX113 reset it in Cbuf_DropServerText). */
TEST(cbuf_executing_server_text_false_after_error_drop)
{
	src_setup();
	Cbuf_AddServerText("t_err\n");
	drop_frame_then_next();
	T_EQ_INT(Cbuf_ExecutingServerText(), 0);

	Cbuf_AddText("t_src\n");
	Cbuf_Execute();
	T_EQ_INT(src_runs, 1);
	T_EQ_INT(src_seen, 0);
}
