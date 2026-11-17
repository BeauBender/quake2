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
 * Connectionless server packets (src/server/sv_conless.c): what anyone
 * on the network can send a server before connecting. Packets go in and
 * replies come back over the engine's in-memory loopback (NA_LOOPBACK in
 * src/backends/unix/network.c), so no socket is opened.
 *
 * Not covered: "connect" with the right protocol version allocates a
 * client and calls into the game module, which the suite does not load;
 * replies to non-loopback addresses need a socket.
 *
 * =======================================================================
 */

#include "test.h"
#include "../../src/server/header/server.h"

/* Public in sv_conless.c; sv_main.c declares it locally the same way. */
void SV_ConnectionlessPacket(void);

static byte inbuf[MAX_MSGLEN];
static byte replybuf[MAX_MSGLEN];

/* Delivers one out-of-band packet from the loopback client. */
static void
client_sends(const char *text)
{
	SZ_Init(&net_message, inbuf, sizeof(inbuf));
	MSG_WriteLong(&net_message, -1);
	SZ_Print(&net_message, text);
	memset(&net_from, 0, sizeof(net_from));
	net_from.type = NA_LOOPBACK;
	SV_ConnectionlessPacket();
}

/* Appends every reply waiting for the client, escaped, to pin. */
static int
drain_replies(t_text_t *pin)
{
	sizebuf_t reply;
	netadr_t from;
	int n = 0;

	SZ_Init(&reply, replybuf, sizeof(replybuf));

	while (NET_GetPacket(NS_CLIENT, &from, &reply))
	{
		int i;

		t_text_printf(pin, "  reply: ");

		for (i = 0; i < reply.cursize; i++)
		{
			unsigned char c = reply.data[i];

			if (c == '\n')
			{
				t_text_printf(pin, "\\n");
			}
			else if (c < 32 || c > 126)
			{
				t_text_printf(pin, "\\x%02x", c);
			}
			else
			{
				t_text_printf(pin, "%c", c);
			}
		}

		t_text_printf(pin, "\n");
		n++;
	}

	return n;
}

TEST(sv_connectionless_packets)
{
	t_text_t pin = {0};
	client_t *saved_clients = svs.clients;
	static client_t clients[2];
	static const char *packets[] = {
		"ping",
		"ack",
		"info 34",
		"info 33",
		"status",
		"connect 33 1234 0 \"\\name\\x\"",
		"rcon",
		"rcon wrong echo hi",
		"rcon secretx echo hi",
		"rcon secre echo hi",
		"rcon secret echo hi there",
		"rcon secret set t_rcon_set 7",
		"nonsense with args",
		"",
	};
	size_t i;

	/* A server with two free client slots and a known hostname. */
	memset(clients, 0, sizeof(clients));
	svs.clients = clients;
	Cvar_FullSet("maxclients", "2", CVAR_SERVERINFO | CVAR_LATCH);
	Cvar_Set("hostname", "testserver");
	Cvar_Set("rcon_password", "secret");
	{
		/* Drop anything an earlier test left on the loopback. */
		t_text_t junk = {0};

		drain_replies(&junk);
		t_text_free(&junk);
	}

	for (i = 0; i < sizeof(packets) / sizeof(packets[0]); i++)
	{
		const char *con;

		t_text_printf(&pin, "packet \"%s\":\n", packets[i]);
		t_console_reset();
		client_sends(packets[i]);
		drain_replies(&pin);

		/* Console lines the server printed, if any (read before
		   re-arming the capture, which clears its buffer). */
		con = t_console();
		t_console_rearm();

		while (*con)
		{
			size_t len = strcspn(con, "\n");

			if (len)
			{
				t_text_printf(&pin, "  console: %.*s\n", (int)len, con);
			}

			con += len;

			if (*con)
			{
				con++;
			}
		}
	}

	T_EQ_STR(Cvar_VariableString("t_rcon_set"), "7");

	/* getchallenge: the number is random, the shape is not. */
	{
		sizebuf_t reply;
		netadr_t from;
		int challenge = -1;
		char tail[8] = "";

		client_sends("getchallenge");
		SZ_Init(&reply, replybuf, sizeof(replybuf));
		T_REQUIRE(NET_GetPacket(NS_CLIENT, &from, &reply));
		MSG_BeginReading(&reply);
		T_EQ_INT(MSG_ReadLong(&reply), -1);
		T_EQ_INT(sscanf(MSG_ReadStringLine(&reply), "challenge %d %7s", &challenge, tail), 2);
		T_CHECK(challenge >= 0 && challenge <= 0x7fff);
		T_EQ_STR(tail, "p=34");

		/* The same address gets the same challenge again. */
		client_sends("getchallenge");
		SZ_Init(&reply, replybuf, sizeof(replybuf));
		T_REQUIRE(NET_GetPacket(NS_CLIENT, &from, &reply));
		MSG_BeginReading(&reply);
		MSG_ReadLong(&reply);
		{
			int again = -2;

			sscanf(MSG_ReadStringLine(&reply), "challenge %d", &again);
			T_EQ_INT(again, challenge);
		}
	}

	svs.clients = saved_clients;
	Cvar_Set("rcon_password", "");
	Cvar_FullSet("maxclients", "1", CVAR_SERVERINFO | CVAR_LATCH);

	t_pin("sv-connectionless", pin.data);
	t_text_free(&pin);
}

/*
 * Receive-buffer bounds (FIX26, FIX33). The buffers below are filled
 * with a known byte past the received data, so a read beyond cursize
 * shows up in the result instead of depending on stray memory.
 */

#define BOUND_FILL 'Z'

static int think_calls;
static int disconnect_calls;

static void
fake_think(edict_t *ent, usercmd_t *cmd)
{
	(void)ent;
	(void)cmd;
	think_calls++;
}

static void
fake_disconnect(edict_t *ent)
{
	(void)ent;
	disconnect_calls++;
}

/*
 * Runs one clc_move through SV_ExecuteClientMessage for a spawned
 * client. The whole move is laid out in a buffer filled with BOUND_FILL
 * and its checksum is computed over the complete body; cursize says how
 * much of it was "received". Returns the number of ClientThink calls.
 */
static int
run_move(int cursize)
{
	static edict_t edicts[4];
	static client_t clients[2];
	static game_export_t fake_ge;
	static byte buf[MAX_MSGLEN];
	game_export_t *saved_ge = ge;
	client_t *saved_clients = svs.clients;
	client_t *saved_sv_client = sv_client;
	int i;

	memset(clients, 0, sizeof(clients));
	memset(&fake_ge, 0, sizeof(fake_ge));
	fake_ge.edicts = edicts;
	fake_ge.edict_size = sizeof(edicts[0]);
	fake_ge.ClientThink = fake_think;
	fake_ge.ClientDisconnect = fake_disconnect;
	ge = &fake_ge;
	svs.clients = clients;
	clients[0].state = cs_spawned;
	clients[0].netchan.incoming_sequence = 5;
	clients[0].lastframe = -1;
	SZ_Init(&clients[0].netchan.message, replybuf, sizeof(replybuf));
	think_calls = 0;
	disconnect_calls = 0;

	/* clc_move, checksum, lastframe, three minimal delta usercmds
	   (bits 0, msec, lightlevel): 15 bytes. */
	memset(buf, BOUND_FILL, sizeof(buf));
	buf[0] = clc_move;
	memset(buf + 2, 0, 4);

	for (i = 0; i < 3; i++)
	{
		buf[6 + i * 3] = 0;
		buf[7 + i * 3] = 10;
		buf[8 + i * 3] = 0;
	}

	buf[1] = COM_BlockSequenceCRCByte(buf + 2, 15 - 2,
			clients[0].netchan.incoming_sequence);

	SZ_Init(&net_message, buf, sizeof(buf));
	net_message.cursize = cursize;
	MSG_BeginReading(&net_message);
	SV_ExecuteClientMessage(&clients[0]);

	ge = saved_ge;
	svs.clients = saved_clients;
	sv_client = saved_sv_client;

	return think_calls;
}

TEST(sv_clc_move_bounds)
{
	int n;

	/* A complete move runs once and the client stays. */
	T_EQ_INT(run_move(15), 1);
	T_EQ_INT(disconnect_calls, 0);

	/* Cut short at every length from the checksum byte on: nothing
	   runs. The checksum would match if it covered the bytes past
	   cursize, so passing shows it does not. The client is dropped,
	   as for any other read past the end. */
	for (n = 2; n < 15; n++)
	{
		T_EQ_INT(run_move(n), 0);
		T_EQ_INT(disconnect_calls, 1);
	}
}

/* Sends "rcon ..." the way the receive path does: no terminator, the
   rest of the buffer holds other data. */
static void
rcon_sends(const char *text)
{
	memset(inbuf, BOUND_FILL, sizeof(inbuf));
	SZ_Init(&net_message, inbuf, sizeof(inbuf));
	MSG_WriteLong(&net_message, -1);
	SZ_Print(&net_message, text);
	/* SZ_Print stores a terminator and counts it; drop it. */
	net_message.data[net_message.cursize - 1] = BOUND_FILL;
	net_message.cursize--;
	memset(&net_from, 0, sizeof(net_from));
	net_from.type = NA_LOOPBACK;
	SV_ConnectionlessPacket();
}

TEST(sv_rcon_log_line)
{
	t_text_t junk = {0};
	sizebuf_t reply;
	netadr_t from;

	Cvar_Set("rcon_password", "hunter2pw");
	drain_replies(&junk);
	t_text_free(&junk);

	/* Correct password: names the sender and the command, not the
	   password, and nothing from past the received data. */
	t_console_reset();
	rcon_sends("rcon hunter2pw echo hello");
	T_CONSOLE_HAS("Rcon from [0.0.0.0]:0");
	T_CONSOLE_HAS("echo hello");
	T_CHECK(strstr(t_console(), "hunter2pw") == NULL);
	T_CHECK(strchr(t_console(), BOUND_FILL) == NULL);
	t_console_rearm();

	/* The command ran and its output went back to the sender. */
	memset(replybuf, 0, sizeof(replybuf));
	SZ_Init(&reply, replybuf, sizeof(replybuf));
	T_REQUIRE(NET_GetPacket(NS_CLIENT, &from, &reply));
	T_CHECK(strstr((char *)reply.data + 4, "hello") != NULL);

	/* Wrong password: the attempt is logged without what was supplied. */
	t_console_reset();
	rcon_sends("rcon hunter2px echo hello");
	T_CONSOLE_HAS("Bad rcon from [0.0.0.0]:0");
	T_CHECK(strstr(t_console(), "hunter2") == NULL);
	T_CHECK(strstr(t_console(), "echo") == NULL);
	T_CHECK(strchr(t_console(), BOUND_FILL) == NULL);
	t_console_rearm();

	/* No arguments at all, long tail of fill. */
	t_console_reset();
	rcon_sends("rcon");
	T_CHECK(strchr(t_console(), BOUND_FILL) == NULL);
	t_console_rearm();

	drain_replies(&junk);
	t_text_free(&junk);
	Cvar_Set("rcon_password", "");
}
