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
 * Not covered on purpose: directory entries whose offset or size lie
 * outside the pak, and a dirofs/dirlen beyond the end of the file. The
 * loader does not check them today; that is FIX22 (#120), whose author
 * should extend this file with the refusals it adds.
 *
 * =======================================================================
 */

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
