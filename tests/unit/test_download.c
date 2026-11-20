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
void CL_DownloadFileName(char *dest, int destlen, char *fn);

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
		/* A library extension inside a component, or in a longer
		   extension, does not end the name. */
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

/* Each name with the verdict the filter gives it: true is refused. The
   extension check looks at the end of the name only, case-insensitively,
   and after trailing numeric version groups. */
TEST(download_filter_extension_table)
{
	static const struct
	{
		const char *name;
		qboolean refused;
	} table[] = {
		/* library names, refused */
		{"x.dll", true},
		{"x.DLL", true},
		{"x.Dll", true},
		{"dir/x.dylib", true},
		{"x.DyLib", true},
		{"libx.so", true},
		{"libx.SO", true},
		{"libx.so.1", true},
		{"libx.so.1.2", true},
		{"libx.so.1.2.3", true},
		{"libx.SO.10", true},
		{"x.lib", true},
		{"x.LIB", true},
		{"x.a", true},
		{"dir/x.A", true},
		{"a/b/x.a", true},
		/* a script the engine runs as commands, refused */
		{"autoexec.cfg", true},
		{"config.cfg", true},
		{"foo.CFG", true},
		{"dir/x.cfg", true},
		{"default.Cfg", true},
		{"x.cfg.1", true},
		/* a container the filesystem mounts, refused */
		{"x.pak", true},
		{"pak5.pak", true},
		{"dir/X.PAK", true},
		{"x.pak.1", true},
		/* ordinary assets and cfg text elsewhere, accepted */
		{"maps/x.bsp", false},
		{"sound/x.wav", false},
		{"players/x/y.pcx", false},
		{"x.cfgs", false},
		{"x.paks", false},
		{"cfg/x.wav", false},
		{"x.cfg.pcx", false},
		/* the extension text inside a component, accepted */
		{"maps/a.area.bsp", false},
		{"x.solid.pcx", false},
		{"textures/e1u1/a.lib_wall.wal", false},
		{"sound/.au/x.wav", false},
		{"models/items/a.ase", false},
		{"a.dll_x/y.wav", false},
		{"x.dylibs/y.wav", false},
		{"x.so_called/y.wav", false},
		{"x.dll.pcx", false},
		{"x.so.bsp", false},
		/* no extension text at all */
		{"maps/q2dm1.bsp", false},
		{"players/male/grunt.pcx", false},
		/* a version group that is not numeric does not count */
		{"libx.so.x", false},
		{"libx.so.", false},
		{"libx.so.1a", false},
		{"x.dll1", false},
		{"libx.so2", false},
		{"x.a7", false},
		{"x.dll_1", false},
		/* names shorter than the extension */
		{"x", false},
		{"so", false},
		{"dll", false},
		/* only digit groups: the version scan stops at the start */
		{"123", false},
		{"1.2", false},
		/* the other rules, byte for byte as before */
		{".x.wav", true},
		{"/x.wav", true},
		{"a:b.wav", true},
		{"a/../b.wav", true},
		{"a..b.wav", true},
	};
	size_t i;

	for (i = 0; i < sizeof(table) / sizeof(table[0]); i++)
	{
		reset_client();
		Cmd_TokenizeString(va("download %s", table[i].name), false);
		CL_Download_f();

		if ((cls.netchan.message.cursize == 0) != table[i].refused)
		{
			t_fail(__FILE__, __LINE__, "%s: refused %d, want %d",
				table[i].name, cls.netchan.message.cursize == 0,
				(int)table[i].refused);
		}
	}
}

static byte f159buf[512];

/* A hostile server offers a pak holding autoexec.cfg: it must not be
   downloaded, so the config is not run when the search path is rebuilt. */
TEST(download_pak_cfg_never_runs)
{
	static byte pak[12 + 18 + 64];

	memset(pak, 0, sizeof(pak));
	memcpy(pak, "PACK", 4);
	pak[4] = 30;
	pak[8] = 64;
	memcpy(pak + 12, "set f159probe pak\n", 18);
	memcpy(pak + 30, "autoexec.cfg", 12);
	pak[86] = 12;
	pak[90] = 18;

	/* a pak left by an earlier run would be mounted below */
	FS_BuildGameSpecificSearchPath("t_f159");
	Sys_Remove(va("%s/evil.pak", FS_Gamedir()));
	FS_BuildGameSpecificSearchPath("t_f159");
	Cbuf_Execute();
	T_EQ_STR(Cvar_VariableString("f159probe"), "");

	reset_client();
	Cmd_TokenizeString("download evil.pak", false);
	CL_Download_f();
	T_EQ_INT(cls.netchan.message.cursize, 0);

	/* A server that streams it anyway (the old behaviour) is shown up
	   by the config running after the rebuild. */
	if (cls.netchan.message.cursize != 0)
	{
		SZ_Init(&net_message, f159buf, sizeof(f159buf));
		MSG_WriteShort(&net_message, sizeof(pak));
		MSG_WriteByte(&net_message, 100);
		SZ_Write(&net_message, pak, sizeof(pak));
		MSG_BeginReading(&net_message);
		CL_ParseDownload();
	}

	FS_BuildGameSpecificSearchPath("t_f159");
	Cbuf_Execute();
	T_EQ_STR(Cvar_VariableString("f159probe"), "");
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

/* FIX161: a players/ download is written under the write directory, where
   the filesystem finds it, not relative to the working directory. */
TEST(download_players_lands_in_gamedir)
{
	void *buf;
	int len;
	char path[MAX_OSPATH];

	reset_client();
	Q_strlcpy(cls.downloadname, "players/f161m/skin.pcx", sizeof(cls.downloadname));
	Q_strlcpy(cls.downloadtempname, "players/f161m/skin.tmp", sizeof(cls.downloadtempname));
	Com_sprintf(path, sizeof(path), "%s/players/f161m/skin.pcx", FS_Gamedir());
	Sys_Remove(path);

	server_sends(3, 100, "abc", 3);
	CL_ParseDownload();

	T_CHECK(cls.download == NULL);
	len = FS_LoadFile("players/f161m/skin.pcx", &buf);
	T_EQ_INT(len, 3);

	if (len == 3)
	{
		T_EQ_MEM(buf, "abc", 3);
		FS_FreeFile(buf);
	}

	/* it is the file under the write directory */
	{
		FILE *fp = Q_fopen(path, "rb");

		T_CHECK(fp != NULL);

		if (fp)
		{
			fclose(fp);
		}
	}

	Sys_Remove(path);
}

/* FIX161: the final rename never replaces an existing file. */
TEST(download_players_existing_target_kept)
{
	char path[MAX_OSPATH];
	char tmp[MAX_OSPATH];
	char got[8] = { 0 };
	FILE *fp;

	reset_client();
	Q_strlcpy(cls.downloadname, "players/f161k/skin.pcx", sizeof(cls.downloadname));
	Q_strlcpy(cls.downloadtempname, "players/f161k/skin.tmp", sizeof(cls.downloadtempname));
	CL_DownloadFileName(path, sizeof(path), cls.downloadname);
	CL_DownloadFileName(tmp, sizeof(tmp), cls.downloadtempname);
	FS_CreatePath(path);
	fp = Q_fopen(path, "wb");
	T_CHECK(fp != NULL);

	if (!fp)
	{
		return;
	}

	fwrite("keep", 1, 4, fp);
	fclose(fp);

	server_sends(3, 100, "bad", 3);
	t_console_reset();
	CL_ParseDownload();
	T_CHECK(cls.download == NULL);

	fp = Q_fopen(path, "rb");
	T_CHECK(fp != NULL);

	if (fp)
	{
		T_EQ_INT((int)fread(got, 1, sizeof(got), fp), 4);
		T_EQ_MEM(got, "keep", 4);
		fclose(fp);
	}

	/* no runt temp file is left behind */
	fp = Q_fopen(tmp, "rb");
	T_CHECK(fp == NULL);

	if (fp)
	{
		fclose(fp);
	}

	Sys_Remove(path);
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
