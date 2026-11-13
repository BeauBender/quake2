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
 * The client's connection handshake (CL_ConnectionlessPacket in
 * src/client/cl_network.c, reached through CL_ReadPackets): a challenge
 * or client_connect reply is honoured only while connecting and only
 * from the address the client asked. The "server" is the engine's
 * in-memory loopback (NA_LOOPBACK), so no socket is opened; the address
 * the client asked is chosen with the server name: "localhost" is the
 * loopback itself (the reply comes from the right place), a numeric
 * address is somewhere else (the same reply comes from the wrong place).
 *
 * =======================================================================
 */

#include "test.h"
#include "../../src/client/header/client.h"
#include "stub_client.h"

/* Public in cl_network.c; cl_main.c declares it locally the same way. */
void CL_CheckForResend(void);

static byte inbuf[MAX_MSGLEN];
static byte clientbuf[MAX_MSGLEN];
static byte sinkbuf[MAX_MSGLEN];

static netadr_t loopback;

/* A client that is connecting to the named server. */
static void
start_connecting(const char *server)
{
	sizebuf_t sink;
	netadr_t from;

	stub_client_cvars();
	memset(&cls, 0, sizeof(cls));
	memset(&cl, 0, sizeof(cl));
	SZ_Init(&cls.netchan.message, clientbuf, sizeof(clientbuf));
	SZ_Init(&net_message, inbuf, sizeof(inbuf));
	memset(&loopback, 0, sizeof(loopback));
	loopback.type = NA_LOOPBACK;

	/* Drop what earlier tests left on the loopback, both directions. */
	SZ_Init(&sink, sinkbuf, sizeof(sinkbuf));
	while (NET_GetPacket(NS_SERVER, &from, &sink))
	{
		SZ_Clear(&sink);
	}

	while (NET_GetPacket(NS_CLIENT, &from, &sink))
	{
		SZ_Clear(&sink);
	}

	cls.state = ca_connecting;
	Q_strlcpy(cls.servername, server, sizeof(cls.servername));
	cls.connect_time = -99999;
	cls.realtime = 0;
	cls.challenge = 7;

	/* The first resend remembers the address and asks for a challenge. */
	CL_CheckForResend();
}

/* The server's side: what the client sent it since the last call. */
static int
server_received(void)
{
	sizebuf_t sink;
	netadr_t from;
	int n = 0;

	SZ_Init(&sink, sinkbuf, sizeof(sinkbuf));

	while (NET_GetPacket(NS_SERVER, &from, &sink))
	{
		n++;
		SZ_Clear(&sink);
	}

	return n;
}

/* The loopback "server" sends one connectionless packet; the client reads it. */
static void
reply(const char *text)
{
	Netchan_OutOfBandPrint(NS_SERVER, loopback, "%s", text);
	CL_ReadPackets();
}

/* Replies from the address the client asked are honoured. */
TEST(cl_handshake_accepted_from_remembered_address)
{
	start_connecting("localhost");
	T_EQ_INT(server_received(), 1); /* the getchallenge */

	reply("challenge 1234");
	T_EQ_INT(cls.challenge, 1234);
	T_EQ_INT(server_received(), 1); /* the connect packet that answers it */
	T_EQ_INT(cls.state, ca_connecting);

	reply("client_connect");
	T_EQ_INT(cls.state, ca_connected);
	T_CHECK(cls.netchan.message.cursize > 0);
	T_EQ_INT(cls.netchan.remote_address.type, NA_LOOPBACK);

	cls.state = ca_disconnected;
}

/* The same replies from any other address are dropped. */
TEST(cl_handshake_refused_from_other_address)
{
	start_connecting("127.0.0.1:27999");

	reply("challenge 1234");
	T_EQ_INT(cls.challenge, 7);
	T_EQ_INT(server_received(), 0); /* no connect packet went out */

	reply("client_connect");
	T_EQ_INT(cls.state, ca_connecting);
	T_EQ_INT(cls.netchan.message.cursize, 0);
	T_EQ_INT(cls.netchan.remote_address.type, 0);

	/* Nothing was learned from the stranger: the client still talks to
	   the address it asked. */
	cls.state = ca_disconnected;
}

/* A reply is also dropped when the client is not connecting at all. */
TEST(cl_handshake_refused_when_not_connecting)
{
	start_connecting("localhost");
	cls.state = ca_disconnected;

	reply("challenge 1234");
	T_EQ_INT(cls.challenge, 7);
	reply("client_connect");
	T_EQ_INT(cls.state, ca_disconnected);

	/* A client that has the connection already ignores a second one. */
	start_connecting("localhost");
	cls.state = ca_connected;
	reply("client_connect");
	T_EQ_INT(cls.state, ca_connected);
	T_EQ_INT(cls.netchan.message.cursize, 0);

	cls.state = ca_disconnected;
}
