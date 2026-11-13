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
 * Server side of downloads (SV_BeginDownload_f and SV_NextDownload_f in
 * src/server/sv_user.c), reached through the public
 * SV_ExecuteUserCommand with a fake client: a negative resume offset is
 * refused, and the read position stays inside the loaded file. Run under
 * ASAN, an out-of-bounds read of the file buffer fails the test.
 *
 * =======================================================================
 */

#include <limits.h>

#include "test.h"
#include "../../src/server/header/server.h"

#define DL_NAME "models/t66/blob.bin"
#define DL_SIZE 3000

static byte msgbuf[MAX_MSGLEN];
static client_t clients[2];
static edict_t edicts[4];
static game_export_t fake_ge;

static client_t *saved_clients;
static game_export_t *saved_ge;
static client_t *saved_sv_client;

static void
setup(void)
{
	static byte blob[DL_SIZE];
	size_t i;

	for (i = 0; i < sizeof(blob); i++)
	{
		blob[i] = (byte)(i * 7 + 1);
	}

	t_write_file("data/baseq2/" DL_NAME, blob, sizeof(blob));

	saved_clients = svs.clients;
	saved_ge = ge;
	saved_sv_client = sv_client;

	memset(clients, 0, sizeof(clients));
	memset(&fake_ge, 0, sizeof(fake_ge));
	fake_ge.edicts = edicts;
	fake_ge.edict_size = sizeof(edicts[0]);
	ge = &fake_ge;
	svs.clients = clients;
	sv_client = &clients[0];
	SZ_Init(&sv_client->netchan.message, msgbuf, sizeof(msgbuf));
}

static void
teardown(void)
{
	if (clients[0].download)
	{
		FS_FreeFile(clients[0].download);
		clients[0].download = NULL;
	}

	svs.clients = saved_clients;
	ge = saved_ge;
	sv_client = saved_sv_client;
}

static void
run(const char *cmd)
{
	char buf[MAX_OSPATH + 32];

	SZ_Clear(&sv_client->netchan.message);
	Q_strlcpy(buf, cmd, sizeof(buf));
	SV_ExecuteUserCommand(buf);
}

/* The message is "svc_download, -1, 0": the way a download is refused. */
static void
expect_refusal(void)
{
	sizebuf_t *m = &sv_client->netchan.message;

	T_EQ_INT(m->cursize, 4);

	if (m->cursize == 4)
	{
		MSG_BeginReading(m);
		T_EQ_INT(MSG_ReadByte(m), svc_download);
		T_EQ_INT(MSG_ReadShort(m), -1);
		T_EQ_INT(MSG_ReadByte(m), 0);
	}

	T_CHECK(sv_client->download == NULL);
}

/* The message is a chunk of size bytes copied from the file at pos. */
static void
expect_chunk(int pos, int size)
{
	sizebuf_t *m = &sv_client->netchan.message;

	T_EQ_INT(m->cursize, 4 + size);

	if (m->cursize == 4 + size)
	{
		int i;
		int bad = 0;

		MSG_BeginReading(m);
		T_EQ_INT(MSG_ReadByte(m), svc_download);
		T_EQ_INT(MSG_ReadShort(m), size);
		MSG_ReadByte(m);

		for (i = 0; i < size; i++)
		{
			if (m->data[4 + i] != (byte)((pos + i) * 7 + 1))
			{
				bad++;
			}
		}

		T_EQ_INT(bad, 0);
	}
}

/* Offset at the accepted boundary: zero, and exactly the file size. */
TEST(sv_download_offset_at_limits)
{
	setup();

	run("download " DL_NAME);
	expect_chunk(0, 1024);
	T_EQ_INT(sv_client->downloadcount, 1024);

	run("download " DL_NAME " 2000");
	expect_chunk(2000, 1000);

	/* An offset equal to the size is a finished file: one empty chunk. */
	run("download " DL_NAME " 3000");
	expect_chunk(3000, 0);
	T_CHECK(sv_client->download == NULL);

	/* One past the size is clamped to the size, not read from. */
	run("download " DL_NAME " 3001");
	expect_chunk(3000, 0);
	run("download " DL_NAME " 2147483647");
	expect_chunk(3000, 0);

	teardown();
}

/* Offsets just below zero are refused like any bad download. */
TEST(sv_download_negative_offset_refused)
{
	setup();

	run("download " DL_NAME " -1");
	expect_refusal();
	run("download " DL_NAME " -1024");
	expect_refusal();
	run("download " DL_NAME " -2147483648");
	expect_refusal();

	/* The next request still works. */
	run("download " DL_NAME);
	expect_chunk(0, 1024);

	teardown();
}

/* The read position of a running download, at and past its limits. */
TEST(sv_download_next_position_in_range)
{
	setup();

	run("download " DL_NAME);
	T_REQUIRE(sv_client->download != NULL);

	/* Last valid positions: one byte before the end, then the end. */
	sv_client->downloadcount = 2999;
	run("nextdl");
	expect_chunk(2999, 1);
	T_CHECK(sv_client->download == NULL);

	run("download " DL_NAME);
	T_REQUIRE(sv_client->download != NULL);
	sv_client->downloadcount = DL_SIZE;
	run("nextdl");
	expect_chunk(DL_SIZE, 0);
	T_CHECK(sv_client->download == NULL);

	/* One past the end: refused and the file is released. */
	run("download " DL_NAME);
	T_REQUIRE(sv_client->download != NULL);
	sv_client->downloadcount = DL_SIZE + 1;
	run("nextdl");
	expect_refusal();

	/* Below zero: refused too. */
	run("download " DL_NAME);
	T_REQUIRE(sv_client->download != NULL);
	sv_client->downloadcount = -1;
	run("nextdl");
	expect_refusal();

	run("download " DL_NAME);
	T_REQUIRE(sv_client->download != NULL);
	sv_client->downloadcount = INT_MIN;
	run("nextdl");
	expect_refusal();

	teardown();
}
