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
 * The virtual filesystem (src/common/filesystem.c): .pak directory
 * parsing on synthetic paks this file writes into the scratch data
 * directory, search-path order between paks and loose files, name
 * normalisation in FS_FOpenFile, and the paths FS_CreatePath and the
 * game directory switch refuse.
 *
 * Malformed paks the loader refuses end the process with
 * Com_Error(ERR_FATAL), so they run in a forked child (t_expect_exit).
 * Directory entries whose offset or size lie outside the pak, a
 * directory beyond the end of the file and a bad entry count are
 * skipped with a console line; fs_pak_bounds and fs_pak_bound_functions
 * cover them (FIX22, #120).
 *
 * =======================================================================
 */

#include <dirent.h>
#include <sys/stat.h>

#include "test.h"

typedef struct
{
	const char *name;
	const char *data;
} pakentry_t;

static void
put_le32(byte *p, int v)
{
	p[0] = v & 255;
	p[1] = (v >> 8) & 255;
	p[2] = (v >> 16) & 255;
	p[3] = (v >> 24) & 255;
}

/*
 * Writes a pak: 12 byte header (ident, dirofs, dirlen), file data, then
 * the directory of 64 byte entries (56 byte name, filepos, filelen).
 */
static void
write_pak(const char *relpath, const pakentry_t *e, int n)
{
	byte *buf;
	int i, size = 12, ofs = 12, dirofs;

	for (i = 0; i < n; i++)
	{
		size += strlen(e[i].data) + 64;
	}

	buf = calloc(1, size);

	for (i = 0; i < n; i++)
	{
		int len = strlen(e[i].data);

		memcpy(buf + ofs, e[i].data, len);
		ofs += len;
	}

	dirofs = ofs;
	ofs = 12;

	for (i = 0; i < n; i++)
	{
		byte *d = buf + dirofs + i * 64;
		int len = strlen(e[i].data);

		Q_strlcpy((char *)d, e[i].name, 56);
		put_le32(d + 56, ofs);
		put_le32(d + 60, len);
		ofs += len;
	}

	memcpy(buf, "PACK", 4);
	put_le32(buf + 4, dirofs);
	put_le32(buf + 8, n * 64);

	t_write_file(relpath, buf, size);
	free(buf);
}

/* Loads a file through the VFS as a C string ("<missing>" if absent). */
static const char *
load(const char *name)
{
	static char out[256];
	void *buf;
	int len = FS_LoadFile(name, &buf);

	if (len < 0)
	{
		return "<missing>";
	}

	Com_sprintf(out, sizeof(out), "%.*s", len, (char *)buf);
	FS_FreeFile(buf);
	return out;
}

TEST(fs_pak_directory)
{
	static const pakentry_t pak0[] = {
		{ "maps/one.txt", "one-from-pak0" },
		{ "Sub/Mixed.TXT", "mixed" },
		{ "shadowed.txt", "pak0" },
		{ "empty.txt", "" },
	};
	static const pakentry_t pak1[] = {
		{ "shadowed.txt", "pak1" },
	};
	static const pakentry_t extra[] = {
		{ "extra.txt", "from-extra" },
		{ "shadowed.txt", "extra" },
	};
	t_text_t pin = {0};
	const char *path;
	int count = 0;

	write_pak("data/t_pakmod/pak0.pak", pak0, 4);
	write_pak("data/t_pakmod/pak1.pak", pak1, 1);
	write_pak("data/t_pakmod/extra.pak", extra, 2);
	t_write_file("data/t_pakmod/loose.txt", "loose", 5);
	t_write_file("data/t_pakmod/shadowed.txt", "loose", 5);

	t_console_reset();
	FS_BuildGameSpecificSearchPath("t_pakmod");
	T_CONSOLE_HAS("t_pakmod/pak0.pak' (4 files).");
	T_CONSOLE_HAS("t_pakmod/pak1.pak' (1 files).");
	T_CONSOLE_HAS("t_pakmod/extra.pak' (2 files).");
	T_EQ_STR(Cvar_VariableString("gamedir"), "t_pakmod");

	t_text_printf(&pin, "maps/one.txt      = %s\n", load("maps/one.txt"));
	/* Pak lookups ignore case. */
	t_text_printf(&pin, "sub/mixed.txt     = %s\n", load("sub/mixed.txt"));
	t_text_printf(&pin, "SUB/MIXED.txt     = %s\n", load("SUB/MIXED.txt"));
	t_text_printf(&pin, "empty.txt         = [%s]\n", load("empty.txt"));
	t_text_printf(&pin, "loose.txt         = %s\n", load("loose.txt"));
	/* Search order for one name in pak0, pak1, extra.pak and a loose
	   file in the same directory: the other paks are searched first,
	   then the numbered paks from high to low, then loose files. */
	t_text_printf(&pin, "shadowed.txt      = %s\n", load("shadowed.txt"));
	t_text_printf(&pin, "extra.txt         = %s\n", load("extra.txt"));
	t_text_printf(&pin, "missing.txt       = %s\n", load("missing.txt"));
	/* Name normalisation in FS_FOpenFile. */
	t_text_printf(&pin, "./maps/one.txt    = %s\n", load("./maps/one.txt"));
	t_text_printf(&pin, "maps/./one.txt    = %s\n", load("maps/./one.txt"));
	t_text_printf(&pin, "maps//one.txt     = %s\n", load("maps//one.txt"));
	t_text_printf(&pin, "/maps/one.txt     = %s\n", load("/maps/one.txt"));
	/* The generic baseq2 search path stays behind the mod. */
	t_write_file("data/baseq2/t_base_only.txt", "base", 4);
	t_text_printf(&pin, "t_base_only.txt   = %s\n", load("t_base_only.txt"));

	/* FS_NextPath walks the directories (not the paks), mod first. */
	for (path = FS_NextPath(NULL); path; path = FS_NextPath(path))
	{
		if (strstr(path, t_tmpdir()) == path)
		{
			t_text_printf(&pin, "path %d: <tmp>%s\n", count, path + strlen(t_tmpdir()));
		}

		count++;
	}

	t_pin("fs-pak-directory", pin.data);
	t_text_free(&pin);

	/* Back to baseq2: the mod's files are gone. */
	FS_BuildGameSpecificSearchPath("");
	T_EQ_STR(load("maps/one.txt"), "<missing>");
	T_EQ_STR(load("t_base_only.txt"), "base");
	T_EQ_STR(Cvar_VariableString("gamedir"), "");
}

TEST(fs_read_handles)
{
	static const pakentry_t pak0[] = {
		{ "data.bin", "0123456789" },
	};
	fileHandle_t f;
	char buf[16];
	int len;

	write_pak("data/t_readmod/pak0.pak", pak0, 1);
	FS_BuildGameSpecificSearchPath("t_readmod");

	len = FS_FOpenFile("data.bin", &f, false);
	T_EQ_INT(len, 10);
	T_REQUIRE(f != 0);
	memset(buf, 0, sizeof(buf));
	T_EQ_INT(FS_Read(buf, 4, f), 4);
	T_EQ_STR(buf, "0123");
	T_EQ_INT(FS_Read(buf, 6, f), 6);
	T_EQ_STR(buf, "456789");
	FS_FCloseFile(f);

	/* gamedir_only: the pak lives in the mod's data dir, which is not
	   fs_gamedir (the binary's directory, last in the raw path). */
	T_EQ_INT(FS_FOpenFile("data.bin", &f, true), -1);
	T_EQ_INT(f, 0);

	T_EQ_INT(FS_FOpenFile("nothere.bin", &f, false), -1);
	T_EQ_INT(f, 0);

	FS_BuildGameSpecificSearchPath("");
}

TEST(fs_refused_paths)
{
	char path[MAX_OSPATH];

	/* FS_CreatePath refuses anything with "..". */
	Com_sprintf(path, sizeof(path), "%s/a/../b/file", t_tmpdir());
	t_console_reset();
	FS_CreatePath(path);
	T_CONSOLE_HAS("WARNING: refusing to create relative path");

	/* ...and creates every directory of a good one. */
	Com_sprintf(path, sizeof(path), "%s/made/x/y/file", t_tmpdir());
	FS_CreatePath(path);
	t_write_file("made/x/y/file", "ok", 2);

	/* The game directory must be a single name. */
	t_console_reset();
	FS_BuildGameSpecificSearchPath("../escape");
	T_CONSOLE_HAS("Gamedir should be a single filename, not a path.");
	FS_BuildGameSpecificSearchPath("a/b");
	FS_BuildGameSpecificSearchPath("a\\b");
	FS_BuildGameSpecificSearchPath(".");
	T_EQ_STR(Cvar_VariableString("gamedir"), "");

	/* FS_FOpenFile refuses names longer than MAX_OSPATH - 1. */
	{
		char longname[MAX_OSPATH + 8];
		fileHandle_t f;

		memset(longname, 'n', sizeof(longname) - 1);
		longname[sizeof(longname) - 1] = '\0';
		t_console_reset();
		T_EQ_INT(FS_FOpenFile(longname, &f, false), -1);
		T_CONSOLE_HAS("used unexpectly long name");
	}
}

/* Children for the malformed paks: each switches to a mod whose pak0
   is broken, which ends the process. */
static void
switch_mod(void *arg)
{
	FS_BuildGameSpecificSearchPath((const char *)arg);
	printf("yq2test-child: mod %s loaded\n", (const char *)arg);
}

TEST(fs_malformed_paks)
{
	static const pakentry_t one[] = { { "a.txt", "a" } };
	byte hdr[12];
	char out[8192];
	t_text_t pin = {0};

	/* Not a pak at all. */
	t_write_file("data/t_bad_ident/pak0.pak", "NOTAPAK.....", 12);

	/* A directory with zero entries. */
	memcpy(hdr, "PACK", 4);
	put_le32(hdr + 4, 12);
	put_le32(hdr + 8, 0);
	t_write_file("data/t_bad_empty/pak0.pak", hdr, 12);

	/* A directory shorter than one entry. */
	put_le32(hdr + 8, 63);
	t_write_file("data/t_bad_short/pak0.pak", hdr, 12);

	/* Negative directory length and offset. */
	put_le32(hdr + 8, -64);
	t_write_file("data/t_bad_neglen/pak0.pak", hdr, 12);
	put_le32(hdr + 4, -1);
	put_le32(hdr + 8, 64);
	t_write_file("data/t_bad_negofs/pak0.pak", hdr, 12);

	/* A good one, as the control. */
	write_pak("data/t_good/pak0.pak", one, 1);

	{
		static const char *mods[] = {
			"t_bad_ident", "t_bad_empty", "t_bad_short",
			"t_bad_neglen", "t_bad_negofs", "t_good"
		};
		size_t i;

		for (i = 0; i < sizeof(mods) / sizeof(mods[0]); i++)
		{
			int status = t_expect_exit(switch_mod, (void *)mods[i], out, sizeof(out));
			const char *err = strstr(out, "Error: ");
			const char *ok = strstr(out, "yq2test-child: ");

			t_text_printf(&pin, "%-13s exit %d: ", mods[i], status);

			if (err)
			{
				/* "Error: FS_LoadPAK: '<tmp>/data/<mod>/pak0.pak' ..." */
				const char *tmp = strstr(err, t_tmpdir());

				if (tmp)
				{
					t_text_printf(&pin, "%.*s<tmp>%.*s\n", (int)(tmp - err), err,
						(int)strcspn(tmp + strlen(t_tmpdir()), "\n"), tmp + strlen(t_tmpdir()));
				}
				else
				{
					t_text_printf(&pin, "%.*s\n", (int)strcspn(err, "\n"), err);
				}
			}
			else if (ok)
			{
				t_text_printf(&pin, "%.*s\n", (int)strcspn(ok, "\n"), ok);
			}
			else
			{
				t_text_printf(&pin, "no message\n");
			}
		}
	}

	t_pin("fs-malformed-paks", pin.data);
	t_text_free(&pin);
}

/*
 * Writes a pak byte for byte: size bytes of 'x', a header claiming a
 * directory at hofs of hlen bytes, and n identical directory entries
 * placed at dirpos. The header and the placement are independent so a
 * test can make them disagree with the file.
 */
static void
write_raw_pak(const char *relpath, int size, int dirpos, int hofs, int hlen,
	const char *name, int fpos, int flen, int n)
{
	byte *buf = malloc(size);
	int i;

	memset(buf, 'x', size);
	memcpy(buf, "PACK", 4);
	put_le32(buf + 4, hofs);
	put_le32(buf + 8, hlen);

	for (i = 0; i < n; i++)
	{
		byte *d = buf + dirpos + i * 64;

		memset(d, 0, 64);
		memcpy(d, name, strlen(name) < 56 ? strlen(name) : 56);
		put_le32(d + 56, fpos);
		put_le32(d + 60, flen);
	}

	t_write_file(relpath, buf, size);
	free(buf);
}

/* Number of open descriptors of this process. */
static int
open_fds(void)
{
	DIR *d = opendir("/dev/fd");
	int n = 0;

	T_REQUIRE(d != NULL);

	while (readdir(d))
	{
		n++;
	}

	closedir(d);
	return n;
}

/* Loads <mod>/pak0.pak; true when the pak was added. */
static qboolean
pak_accepted(const char *mod)
{
	t_console_reset();
	FS_BuildGameSpecificSearchPath(mod);
	return strstr(t_console(), "Added packfile") != NULL;
}

/*
 * What the pak loader bounds: the directory against the size of the
 * file, the entry count, and each entry against the size of the pack.
 * A pak that fails one of these is skipped with a console line and its
 * handle closed. A short header and an empty directory still end the
 * process, as before.
 */
TEST(fs_pak_bounds)
{
	static const char *fatal_mods[] = { "t_pb_only_ident", "t_pb_dirlen0" };
	char out[8192];
	int base, i;
	byte hdr[12];

	/* Layout A: header, 8 data bytes, one directory entry at the end
	   of the file (size 84). */
	write_raw_pak("data/t_pb_good/pak0.pak", 84, 20, 20, 64, "a.txt", 12, 8, 1);
	T_CHECK(pak_accepted("t_pb_good"));
	T_EQ_STR(load("a.txt"), "xxxxxxxx");

	/* The directory ends exactly at the end of the file (the good
	   one), and one byte past it. */
	write_raw_pak("data/t_pb_dir_past/pak0.pak", 84, 20, 21, 64, "a.txt", 12, 8, 1);
	T_CHECK(!pak_accepted("t_pb_dir_past"));
	T_EQ_STR(load("a.txt"), "<missing>");

	/* A directory length that is not a whole number of entries, with
	   room in the file for it. */
	write_raw_pak("data/t_pb_dir_odd/pak0.pak", 85, 20, 20, 65, "a.txt", 12, 8, 1);
	T_CHECK(!pak_accepted("t_pb_dir_odd"));

	/* The entry-count limit and one past it. */
	write_raw_pak("data/t_pb_count_max/pak0.pak", 12 + 64 * MAX_FILES_IN_PACK,
		12, 12, 64 * MAX_FILES_IN_PACK, "f", 12, 0, MAX_FILES_IN_PACK);
	T_CHECK(pak_accepted("t_pb_count_max"));
	write_raw_pak("data/t_pb_count_over/pak0.pak", 12 + 64 * (MAX_FILES_IN_PACK + 1),
		12, 12, 64 * (MAX_FILES_IN_PACK + 1), "f", 12, 0, MAX_FILES_IN_PACK + 1);
	T_CHECK(!pak_accepted("t_pb_count_over"));

	/* Layout B: header, directory, 8 data bytes at the end (size 84).
	   An entry ending exactly at the end of the pack, and one byte
	   past it, by offset and by size. */
	write_raw_pak("data/t_pb_ent_end/pak0.pak", 84, 12, 12, 64, "a.txt", 76, 8, 1);
	T_CHECK(pak_accepted("t_pb_ent_end"));
	T_EQ_STR(load("a.txt"), "xxxxxxxx");
	write_raw_pak("data/t_pb_ent_ofs/pak0.pak", 84, 12, 12, 64, "a.txt", 77, 8, 1);
	T_CHECK(!pak_accepted("t_pb_ent_ofs"));
	write_raw_pak("data/t_pb_ent_len/pak0.pak", 84, 12, 12, 64, "a.txt", 76, 9, 1);
	T_CHECK(!pak_accepted("t_pb_ent_len"));

	/* Negative size and negative offset. */
	write_raw_pak("data/t_pb_ent_negsize/pak0.pak", 84, 12, 12, 64, "a.txt", 76, -1, 1);
	T_CHECK(!pak_accepted("t_pb_ent_negsize"));
	write_raw_pak("data/t_pb_ent_negofs/pak0.pak", 84, 12, 12, 64, "a.txt", -1, 8, 1);
	T_CHECK(!pak_accepted("t_pb_ent_negofs"));

	/* A name that fills its 56-byte field with no terminator. */
	write_raw_pak("data/t_pb_name/pak0.pak", 84, 12, 12, 64,
		"nnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnn", 76, 8, 1);
	T_CHECK(!pak_accepted("t_pb_name"));

	/* No descriptor stays open for a refused pak: the count after a
	   switch to a mod with refused paks equals the count after a
	   switch to a mod with none. */
	t_write_file("data/t_pb_nopak/readme.txt", "x", 1);
	FS_BuildGameSpecificSearchPath("t_pb_nopak");
	base = open_fds();
	for (i = 0; i < 40; i++)
	{
		FS_BuildGameSpecificSearchPath("t_pb_dir_past");
		FS_BuildGameSpecificSearchPath("t_pb_ent_len");
	}
	FS_BuildGameSpecificSearchPath("t_pb_nopak");
	T_EQ_INT(open_fds(), base);

	/* A header with only the identifier, and a directory of length 0,
	   end the process with the "too short" error. */
	t_write_file("data/t_pb_only_ident/pak0.pak", "PACK", 4);
	memcpy(hdr, "PACK", 4);
	put_le32(hdr + 4, 12);
	put_le32(hdr + 8, 0);
	t_write_file("data/t_pb_dirlen0/pak0.pak", hdr, 12);

	for (i = 0; i < 2; i++)
	{
		int status = t_expect_exit(switch_mod, (void *)fatal_mods[i], out, sizeof(out));

		T_CHECK(status != 0);
		T_CHECK(strstr(out, "is too short") != NULL);
	}
}

/* The pure bounds the loader decides with (src/common/filesystem.c). */
qboolean FS_PakDirectoryInBounds(long long packSize, int dirofs, int dirlen);
qboolean FS_PakEntryInBounds(long long packSize, int offset, int size);
qboolean FS_PakNameTerminated(const char *name, size_t fieldSize);

TEST(fs_pak_bound_functions)
{
	char name[56];

	/* Directory: ends exactly at the end, one past, one before. */
	T_CHECK(FS_PakDirectoryInBounds(84, 20, 64));
	T_CHECK(!FS_PakDirectoryInBounds(84, 21, 64));
	T_CHECK(FS_PakDirectoryInBounds(85, 20, 64));
	/* Offset at the end of the file with nothing after it, and past it. */
	T_CHECK(!FS_PakDirectoryInBounds(84, 84, 64));
	T_CHECK(!FS_PakDirectoryInBounds(84, 85, 64));
	/* Length: zero, negative, not a multiple of the entry size. */
	T_CHECK(!FS_PakDirectoryInBounds(1000, 12, 0));
	T_CHECK(!FS_PakDirectoryInBounds(1000, 12, -64));
	T_CHECK(!FS_PakDirectoryInBounds(1000, 12, 63));
	T_CHECK(!FS_PakDirectoryInBounds(1000, 12, 65));
	T_CHECK(!FS_PakDirectoryInBounds(1000, -1, 64));
	/* Offset 0 is a legal value. */
	T_CHECK(FS_PakDirectoryInBounds(64, 0, 64));
	T_CHECK(!FS_PakDirectoryInBounds(-1, 0, 64));
	/* Count limit, one past it, and an offset plus length that would
	   wrap an int. */
	T_CHECK(FS_PakDirectoryInBounds(1 << 20, 12, 64 * MAX_FILES_IN_PACK));
	T_CHECK(!FS_PakDirectoryInBounds(1 << 20, 12, 64 * (MAX_FILES_IN_PACK + 1)));
	T_CHECK(!FS_PakDirectoryInBounds(0x7fffffffLL, 0x7fffffc0, 64 * 4097));
	T_CHECK(!FS_PakDirectoryInBounds(0x7fffffffLL, 0x7fffffff, 64));

	/* Entry: ends exactly at the end, one past, by offset and length. */
	T_CHECK(FS_PakEntryInBounds(84, 76, 8));
	T_CHECK(!FS_PakEntryInBounds(84, 77, 8));
	T_CHECK(!FS_PakEntryInBounds(84, 76, 9));
	T_CHECK(FS_PakEntryInBounds(84, 84, 0));
	T_CHECK(!FS_PakEntryInBounds(84, 85, 0));
	T_CHECK(FS_PakEntryInBounds(84, 0, 0));
	T_CHECK(!FS_PakEntryInBounds(84, -1, 8));
	T_CHECK(!FS_PakEntryInBounds(84, 76, -1));
	T_CHECK(!FS_PakEntryInBounds(-1, 0, 0));
	T_CHECK(!FS_PakEntryInBounds(0x7fffffffLL, 0x7fffffff, 1));
	T_CHECK(FS_PakEntryInBounds(0x7fffffffLL, 0x7ffffffe, 1));

	/* Name: terminator in the last byte, and none. */
	memset(name, 'n', sizeof(name));
	T_CHECK(!FS_PakNameTerminated(name, sizeof(name)));
	name[sizeof(name) - 1] = '\0';
	T_CHECK(FS_PakNameTerminated(name, sizeof(name)));
	name[0] = '\0';
	T_CHECK(FS_PakNameTerminated(name, sizeof(name)));
}

/*
 * The sandbox home directory (iOS: <HOME>/Documents/). The composing
 * helper has no platform condition and runs here; the home directory
 * the engine uses is switched with sys_sandbox_home.
 */
TEST(fs_sandbox_home_dir)
{
	char out[MAX_OSPATH];
	char home[MAX_OSPATH];
	char tight[MAX_OSPATH];
	/* "/Documents/" is 11 bytes and the terminator one more */
	size_t longest = MAX_OSPATH - 1 - 11;

	T_CHECK(Sys_SandboxHomeDir("/c/x", out, sizeof(out)));
	T_EQ_STR(out, "/c/x/Documents/");

	memset(out, 'x', sizeof(out));
	T_CHECK(!Sys_SandboxHomeDir("", out, sizeof(out)));
	T_EQ_STR(out, "");
	memset(out, 'x', sizeof(out));
	T_CHECK(!Sys_SandboxHomeDir(NULL, out, sizeof(out)));
	T_EQ_STR(out, "");
	T_CHECK(!Sys_SandboxHomeDir("/c/x", NULL, sizeof(out)));
	T_CHECK(!Sys_SandboxHomeDir("/c/x", out, 0));

	/* The longest HOME that fits, and one byte more. */
	memset(home, 'h', sizeof(home));
	home[0] = '/';
	home[longest] = '\0';
	T_CHECK(Sys_SandboxHomeDir(home, out, sizeof(out)));
	T_EQ_INT((int)strlen(out), MAX_OSPATH - 1);
	T_CHECK(strcmp(out + longest, "/Documents/") == 0);
	home[longest] = 'h';
	home[longest + 1] = '\0';
	memset(out, 'x', sizeof(out));
	T_CHECK(!Sys_SandboxHomeDir(home, out, sizeof(out)));
	T_EQ_STR(out, "");

	/* The same bound for a short buffer: "/c/x/Documents/" is 15 bytes. */
	T_CHECK(Sys_SandboxHomeDir("/c/x", tight, 16));
	T_CHECK(!Sys_SandboxHomeDir("/c/x", tight, 15));
}

/*
 * Fills Sys_GetHomeDir's cache in this process, as an earlier caller
 * would, with the home <tmp>/<name> in the given mode, and puts HOME
 * and the mode back.
 */
static void
fill_home_cache(const char *name, qboolean sandbox)
{
	char path[MAX_OSPATH], oldhome[MAX_OSPATH];
	const char *env = getenv("HOME");
	const char *xenv = getenv("XDG_DATA_HOME");
	char oldxdg[MAX_OSPATH];

	snprintf(oldhome, sizeof(oldhome), "%s", env ? env : "");
	snprintf(oldxdg, sizeof(oldxdg), "%s", xenv ? xenv : "");
	Sys_ForgetHomeDir();
	snprintf(path, sizeof(path), "%s/%s", t_tmpdir(), name);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/%s/.local", t_tmpdir(), name);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/%s/.local/share", t_tmpdir(), name);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/%s", t_tmpdir(), name);
	setenv("HOME", path, 1);
	unsetenv("XDG_DATA_HOME");
	sys_sandbox_home = sandbox;
	T_CHECK(Sys_GetHomeDir() != NULL);
	sys_sandbox_home = false;

	if (env)
	{
		setenv("HOME", oldhome, 1);
	}
	else
	{
		unsetenv("HOME");
	}

	if (xenv)
	{
		setenv("XDG_DATA_HOME", oldxdg, 1);
	}
	else
	{
		unsetenv("XDG_DATA_HOME");
	}
}

static void
sandbox_child(void *arg)
{
	char path[MAX_OSPATH];
	FILE *f;
	void *buf;

	(void)arg;
	snprintf(path, sizeof(path), "%s/sandbox", t_tmpdir());
	setenv("HOME", path, 1);
	Sys_ForgetHomeDir();

	/* Start over with the home directory in the search path. */
	FS_ShutdownFilesystem();
	is_portable = false;
	sys_sandbox_home = true;
	FS_InitFilesystem();

	T_CHECK(FS_LoadFile("pics/colormap.pcx", &buf) > 0);
	FS_FreeFile(buf);

	snprintf(path, sizeof(path), "%s/sandbox/Documents/baseq2", t_tmpdir());
	T_EQ_STR(FS_Gamedir(), path);

	/* Nothing is written but under the test's own tree. */
	snprintf(path, sizeof(path), "%s/", t_tmpdir());
	T_CHECK(strncmp(FS_Gamedir(), path, strlen(path)) == 0);

	if (strncmp(FS_Gamedir(), path, strlen(path)) != 0)
	{
		return;
	}

	/* What the engine writes lands next to the paks. */
	snprintf(path, sizeof(path), "%s/config.cfg", FS_Gamedir());
	FS_CreatePath(path);
	f = Q_fopen(path, "w");
	T_CHECK(f != NULL);

	if (f)
	{
		fputs("seta x 1\n", f);
		fclose(f);
	}

	printf("home=%s\n", Sys_GetHomeDir());
}

TEST(fs_sandbox_documents)
{
	static const pakentry_t pak0[] = {
		{ "pics/colormap.pcx", "colormap" },
	};
	char out[1024], path[MAX_OSPATH];
	struct stat sb;
	int status;

	write_pak("sandbox/Documents/baseq2/pak0.pak", pak0, 1);

	/* An earlier caller has filled Sys_GetHomeDir's cache with another
	   home in the same mode: the child clears it (Sys_ForgetHomeDir). */
	fill_home_cache("prefill", true);

	status = t_expect_exit(sandbox_child, NULL, out, sizeof(out));
	T_EQ_INT(status, 0);

	snprintf(path, sizeof(path), "home=%s/sandbox/Documents/\n", t_tmpdir());
	T_CHECK(strstr(out, path) != NULL);

	snprintf(path, sizeof(path), "%s/sandbox/Documents/baseq2/config.cfg", t_tmpdir());
	T_CHECK(stat(path, &sb) == 0 && sb.st_size == 9);

	/* The parent process never left the macOS rules. */
	T_CHECK(!sys_sandbox_home);
}

static void
plain_home_child(void *arg)
{
	char home[MAX_OSPATH], docs[MAX_OSPATH];
	const char *dir;
	struct stat sb;

	(void)arg;
	snprintf(home, sizeof(home), "%s/plainhome", t_tmpdir());
	mkdir(home, 0755);
	snprintf(docs, sizeof(docs), "%s/.local", home);
	mkdir(docs, 0755);
	snprintf(docs, sizeof(docs), "%s/.local/share", home);
	mkdir(docs, 0755);
	setenv("HOME", home, 1);
	unsetenv("XDG_DATA_HOME");
	sys_sandbox_home = false;
	Sys_ForgetHomeDir();

	/* The macOS rules: under HOME, never Documents. */
	dir = Sys_GetHomeDir();
	T_CHECK(dir != NULL);

	if (dir == NULL)
	{
		return;
	}

	snprintf(docs, sizeof(docs), "%s/", home);
	T_CHECK(strncmp(dir, docs, strlen(docs)) == 0);
	T_CHECK(strstr(dir + strlen(home), "Documents") == NULL);
	T_CHECK(dir[strlen(dir) - 1] == '/');
	T_CHECK(stat(dir, &sb) == 0 && S_ISDIR(sb.st_mode));
	printf("plain=%s\n", dir);
}

/*
 * The non-sandbox branch of Sys_GetHomeDir (the macOS rules): the home
 * directory is under HOME and is not Documents.
 */
TEST(fs_plain_home_dir)
{
	char out[1024], want[MAX_OSPATH];
	int status;

	/* The cache holds the same home in the sandbox mode: the child clears it. */
	fill_home_cache("plainhome", true);
	status = t_expect_exit(plain_home_child, NULL, out, sizeof(out));
	T_EQ_INT(status, 0);
	snprintf(want, sizeof(want), "plain=%s/plainhome/", t_tmpdir());
	T_CHECK(strstr(out, want) != NULL);
	T_CHECK(strstr(out, "/Documents") == NULL);
}

static void
home_change_child(void *arg)
{
	char first[MAX_OSPATH], h[MAX_OSPATH];
	const char *d;
	int i;

	(void)arg;

	for (i = 1; i <= 2; i++)
	{
		snprintf(h, sizeof(h), "%s/hc%d", t_tmpdir(), i);
		mkdir(h, 0755);
		snprintf(h, sizeof(h), "%s/hc%d/.local", t_tmpdir(), i);
		mkdir(h, 0755);
		snprintf(h, sizeof(h), "%s/hc%d/.local/share", t_tmpdir(), i);
		mkdir(h, 0755);
	}

	snprintf(h, sizeof(h), "%s/hc1", t_tmpdir());
	setenv("HOME", h, 1);
	unsetenv("XDG_DATA_HOME");
	sys_sandbox_home = false;
	Sys_ForgetHomeDir();
	d = Sys_GetHomeDir();
	T_CHECK(d != NULL);

	if (d == NULL)
	{
		return;
	}

	snprintf(first, sizeof(first), "%s", d);

	/* The console's "setenv HOME <dir>", which a server can send. */
	snprintf(h, sizeof(h), "%s/hc2", t_tmpdir());
	setenv("HOME", h, 1);
	d = Sys_GetHomeDir();
	T_CHECK(d != NULL && strcmp(first, d) == 0);

	/* The other mode does not move it either. */
	sys_sandbox_home = true;
	d = Sys_GetHomeDir();
	T_CHECK(d != NULL && strcmp(first, d) == 0);
}

/*
 * The home directory is built once: a later change of HOME (the
 * "setenv" console command) leaves it where it was.
 */
TEST(fs_home_dir_ignores_later_home)
{
	char out[2048];
	int status = t_expect_exit(home_change_child, NULL, out, sizeof(out));

	T_EQ_INT(status, 0);
}

TEST(fs_missing_data_message)
{
	char out[2048], wrong[MAX_OSPATH], dir[MAX_OSPATH];
	static const pakentry_t pak0[] = {
		{ "pics/colormap.pcx", "c" },
	};

	/* Nothing in the directory, and no directory at all. */
	snprintf(dir, sizeof(dir), "%s/msg_none", t_tmpdir());
	t_write_file("msg_none/.keep", "", 0);
	Sys_MissingDataMessage(dir, out, sizeof(out));
	T_CHECK(strstr(out, "pak0.pak") != NULL);
	T_CHECK(strstr(out, "Files > On My iPhone (or iPad) > Quake II > baseq2") != NULL);
	T_CHECK(strstr(out, "renamed") == NULL);
	T_CHECK(!Sys_PakCaseMismatch(dir, wrong, sizeof(wrong)));
	T_CHECK(!Sys_PakCaseMismatch("/nonexistent/t_dir", wrong, sizeof(wrong)));

	/* No home directory (VID_CheckGameData's fallback): the plain message.
	   This cannot fail if the NULL guard in vid.c is reverted: vid.c is
	   not linked into the suite (it needs SDL). What it holds is the
	   message function's own NULL handling, which the guard relies on. */
	T_CHECK(!Sys_PakCaseMismatch(NULL, wrong, sizeof(wrong)));
	Sys_MissingDataMessage(NULL, out, sizeof(out));
	T_CHECK(strstr(out, "pak0.pak") != NULL);
	T_CHECK(strstr(out, "renamed") == NULL);

	/* A pak in the wrong case and no correct one. */
	snprintf(dir, sizeof(dir), "%s/msg_upper", t_tmpdir());
	t_write_file("msg_upper/PAK0.PAK", "x", 1);
	t_write_file("msg_upper/readme.txt", "x", 1);
	T_CHECK(Sys_PakCaseMismatch(dir, wrong, sizeof(wrong)));
	T_EQ_STR(wrong, "PAK0.PAK");
	Sys_MissingDataMessage(dir, out, sizeof(out));
	T_CHECK(strstr(out, "PAK0.PAK must be renamed") != NULL);

	/* Mixed case and a two digit number count; a letter for a number
	   and a longer number do not. */
	snprintf(dir, sizeof(dir), "%s/msg_mixed", t_tmpdir());
	t_write_file("msg_mixed/Pak12.pak", "x", 1);
	T_CHECK(Sys_PakCaseMismatch(dir, wrong, sizeof(wrong)));
	T_EQ_STR(wrong, "Pak12.pak");
	snprintf(dir, sizeof(dir), "%s/msg_other", t_tmpdir());
	t_write_file("msg_other/PAKX.PAK", "x", 1);
	t_write_file("msg_other/PAK123.PAK", "x", 1);
	t_write_file("msg_other/PAK.PAK", "x", 1);
	T_CHECK(!Sys_PakCaseMismatch(dir, wrong, sizeof(wrong)));

	/* A correct pak0.pak is not reported. */
	write_pak("msg_ok/pak0.pak", pak0, 1);
	snprintf(dir, sizeof(dir), "%s/msg_ok", t_tmpdir());
	T_CHECK(!Sys_PakCaseMismatch(dir, wrong, sizeof(wrong)));
	T_EQ_STR(wrong, "");
}
