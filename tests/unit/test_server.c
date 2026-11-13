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
