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
 * Downloads and save directories: CL_DownloadFilter (the only gate on a
 * path a server can make the client write, src/client/cl_download.c),
 * the chunk-size check in CL_ParseDownload, and the savedir check in
 * SV_Loadgame_f (both fixed in TASK1). cl_download.c is linked
 * with the client state stubs in stub_client.c (and, with other client files,
 * stub_client_full.c).
 *
 * Not reachable here: SV_Savegame_f's savedir check comes after "must be
 * in a game", which needs a loaded game module and map. The server side
 * of downloads (SV_BeginDownload_f, sv_user.c) is tested in
 * test_sv_download.c.
 *
 * =======================================================================
 */

#include "test.h"
#include "../../src/client/header/client.h"

/* Public in cl_download.c; cl_parse.c declares it locally the same way. */
void CL_ParseDownload(void);

static byte netbuf[MAX_MSGLEN];

static void
reset_client(void)
{
	memset(&cls, 0, sizeof(cls));
	SZ_Init(&cls.netchan.message, netbuf, sizeof(netbuf));

	cl_nodownload_list = Cvar_Get("cl_nodownload_list", "", 0);
	Cvar_Set("cl_nodownload_list", "");
}

/* Runs "download <path>" and describes the outcome in one line. */
static void
try_download(t_text_t *pin, const char *path)
{
	char cmd[MAX_OSPATH + 16];
	const char *out;
	char *nl;

	reset_client();
	Com_sprintf(cmd, sizeof(cmd), "download %s", path);
	Cmd_TokenizeString(cmd, false);
	t_console_reset();
	CL_Download_f();
	out = t_console();

	if (cls.netchan.message.cursize > 0)
	{
		MSG_BeginReading(&cls.netchan.message);
		T_EQ_INT(MSG_ReadByte(&cls.netchan.message), clc_stringcmd);
		t_text_printf(pin, "%-28s sent \"%s\" (temp %s)\n", path,
			MSG_ReadString(&cls.netchan.message), cls.downloadtempname);
		return;
	}

	/* First console line, if any. */
	nl = strchr(out, '\n');
	t_text_printf(pin, "%-28s refused: %.*s\n", path,
		nl ? (int)(nl - out) : (int)strlen(out), out);
}

TEST(download_filter)
{
	static const char *paths[] = {
		"maps/q2dm1.bsp",
		"players/male/grunt.pcx",
		"sound/world/amb10.wav",
		"textures/e1u1/floor1_1.wal",
		"../outside.cfg",
		"maps/../../outside.cfg",
		"maps/..",
		".hidden",
		"/etc/passwd",
		"c:/autoexec.cfg",
		"maps:stream",
		"game.dll",
		"baseq2/game.so",
		"lib/libfoo.dylib",
		"thing.lib",
		"archive.a",
		/* ".a" is matched anywhere in the path, not only at the end. */
		"models/items/a.ase",
		"sound/.au/x.wav",
		/* Backslashes are not special to the filter. */
		"maps\\sub\\x.bsp",
	};
	t_text_t pin = {0};
	size_t i;

	for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++)
	{
		try_download(&pin, paths[i]);
	}

	t_pin("download-filter", pin.data);
	t_text_free(&pin);
}

TEST(download_filter_nodownload_list_and_existing)
{
	char path[MAX_QPATH];

	reset_client();
	Cvar_Set("cl_nodownload_list", "badmap sound/evil");
	Cmd_TokenizeString("download maps/BadMap.bsp", false);
	t_console_reset();
	CL_Download_f();
	T_CONSOLE_HAS("Filename is filtered by cl_nodownload_list: maps/BadMap.bsp");
	T_EQ_INT(cls.netchan.message.cursize, 0);

	Cmd_TokenizeString("download sound/evil/laugh.wav", false);
	CL_Download_f();
	T_EQ_INT(cls.netchan.message.cursize, 0);

	Cmd_TokenizeString("download maps/goodmap.bsp", false);
	CL_Download_f();
	T_CHECK(cls.netchan.message.cursize > 0);

	/* A file that already exists in the search path is not fetched. */
	reset_client();
	t_write_file("data/baseq2/maps/have.bsp", "x", 1);
	Cmd_TokenizeString("download maps/have.bsp", false);
	t_console_reset();
	CL_Download_f();
	T_EQ_INT(cls.netchan.message.cursize, 0);
	T_EQ_STR(t_console(), "");

	/* CL_CheckOrDownloadFile turns backslashes into slashes first, so
	   a backslashed ".." is refused too, and its caller's buffer is
	   rewritten in place. */
	reset_client();
	Q_strlcpy(path, "maps\\..\\..\\x.cfg", sizeof(path));
	T_CHECK(CL_CheckOrDownloadFile(path));
	T_EQ_STR(path, "maps/../../x.cfg");
	T_EQ_INT(cls.netchan.message.cursize, 0);

	Q_strlcpy(path, "maps\\new.bsp", sizeof(path));
	T_CHECK(!CL_CheckOrDownloadFile(path));
	T_EQ_STR(cls.downloadname, "maps/new.bsp");
	T_EQ_STR(cls.downloadtempname, "maps/new.tmp");
	T_EQ_INT(cls.downloadnumber, 1);
}

static byte svbuf[64];

/* Puts a svc_download payload (short size, byte percent, data) in
   net_message the way CL_ParseServerMessage leaves it. */
static void
server_sends(int size, int percent, const char *data, int datalen)
{
	SZ_Init(&net_message, svbuf, sizeof(svbuf));
	MSG_WriteShort(&net_message, size);
	MSG_WriteByte(&net_message, percent);
	SZ_Write(&net_message, data, datalen);
	MSG_BeginReading(&net_message);
}

TEST(download_chunk_size)
{
	void *buf;
	int len;

	reset_client();
	Q_strlcpy(cls.downloadname, "maps/chunk.bsp", sizeof(cls.downloadname));
	Q_strlcpy(cls.downloadtempname, "maps/chunk.tmp", sizeof(cls.downloadtempname));

	/* TASK1: a size larger than the rest of the message drops the
	   connection before anything is written. */
	server_sends(40, 50, "abc", 3);
	T_EXPECT_DROP(CL_ParseDownload());
	T_CONSOLE_HAS("Bad download chunk size 40, only 3 bytes in message.");
	T_CHECK(cls.download == NULL);
	T_EQ_INT(FS_LoadFile("maps/chunk.tmp", NULL), -1);

	/* The boundary: one byte more than the message holds is refused. */
	server_sends(4, 50, "abc", 3);
	T_EXPECT_DROP(CL_ParseDownload());
	T_CONSOLE_HAS("Bad download chunk size 4, only 3 bytes in message.");
	T_CHECK(cls.download == NULL);
	T_EQ_INT(FS_LoadFile("maps/chunk.tmp", NULL), -1);

	server_sends(-2, 50, "abc", 3);
	T_EXPECT_DROP(CL_ParseDownload());
	T_CONSOLE_HAS("Bad download chunk size -2");

	/* -1 means the server does not have the file. */
	server_sends(-1, 0, "", 0);
	t_console_reset();
	CL_ParseDownload();
	T_CONSOLE_HAS("Server does not have this file.");

	/* Two good chunks, each exactly as long as the rest of its message
	   (the accepted side of the boundary): the temp file grows, then is
	   renamed. */
	server_sends(3, 50, "abc", 3);
	CL_ParseDownload();
	T_CHECK(cls.download != NULL);
	T_EQ_INT(net_message.readcount, net_message.cursize);
	T_EQ_INT(cls.downloadpercent, 50);

	server_sends(2, 100, "de", 2);
	CL_ParseDownload();
	T_CHECK(cls.download == NULL);

	len = FS_LoadFile("maps/chunk.bsp", &buf);
	T_EQ_INT(len, 5);

	if (len == 5)
	{
		T_EQ_MEM(buf, "abcde", 5);
		FS_FreeFile(buf);
	}

	T_EQ_INT(FS_LoadFile("maps/chunk.tmp", NULL), -1);
}

TEST(loadgame_savedir)
{
	/* TASK1: the check used to print and fall through. */
	static const char *bad[] = { "../x", "a/b", "a\\b", ".." };
	size_t i;

	for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
	{
		char cmd[64];

		Com_sprintf(cmd, sizeof(cmd), "load \"%s\"\n", bad[i]);
		t_console_reset();
		Cbuf_AddText(cmd);
		Cbuf_Execute();
		T_CONSOLE_HAS("Bad savedir.");

		if (strstr(t_console(), "No such savegame"))
		{
			t_fail(__FILE__, __LINE__, "load %s went past the savedir check:\n%s",
				bad[i], t_console());
		}
	}

	/* A plain name gets to the existence check. */
	t_console_reset();
	Cbuf_AddText("load nosuchsave\n");
	Cbuf_Execute();
	T_CONSOLE_HAS("No such savegame: ");
	T_CONSOLE_HAS("/save/nosuchsave/server.ssv");
}
