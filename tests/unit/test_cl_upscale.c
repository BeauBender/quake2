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
 * The texture upscaler's checks and pixel arithmetic (TASK123,
 * src/client/cl_upscale.c): the names a listing may hand it, the git
 * work-tree refusal on a synthetic tree, wrap-padding and crop, scale2x
 * with wrapped and clamped edges, the transparent index, the skin flood
 * fill, the .wal and .pcx size checks, and the command's decisions:
 * which names are sources, the colormap's palette, skip, write or
 * refuse (lstat, links), the paths, the git check of each output
 * directory, the directories walked, the demo and remote-server
 * refusal, the counts and the summary; the file write on disk, and the
 * order of one file's steps with stubs. The listing, loading and
 * encoding (cl_upscale_cmd.c, ImageIO) are not linked here.
 *
 * =======================================================================
 */

#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include "test.h"
#include "../../src/client/header/client.h"
#include "../../src/client/header/upscale.h"

/* ---- names ---- */

TEST(upscale_name_ok_accepts_listing_names)
{
	T_EQ_INT(CL_UpscaleNameOk("textures/e1u1/floor1_1.wal"), 1);
	T_EQ_INT(CL_UpscaleNameOk("env/unit1_bk.pcx"), 1);
	T_EQ_INT(CL_UpscaleNameOk("models/monsters/soldier/skin.pcx"), 1);
	T_EQ_INT(CL_UpscaleNameOk("players/male/grunt.pcx"), 1);
	T_EQ_INT(CL_UpscaleNameOk("textures/e1u1/PIP04_4.wal"), 1);
	T_EQ_INT(CL_UpscaleNameOk("a-b_c.d/e.f"), 1);
	T_EQ_INT(CL_UpscaleNameOk("x"), 1);
	T_EQ_INT(CL_UpscaleNameOk("a.b.c/d.e"), 1);
	/* the animation prefix of the stock textures */
	T_EQ_INT(CL_UpscaleNameOk("textures/e1u1/+0btshoot1.wal"), 1);
	T_EQ_INT(CL_UpscaleNameOk("textures/e2u3/+a_fan.wal"), 1);
	T_EQ_INT(CL_UpscaleNameOk("+"), 1);
}

TEST(upscale_name_ok_refuses)
{
	T_EQ_INT(CL_UpscaleNameOk(NULL), 0);
	T_EQ_INT(CL_UpscaleNameOk(""), 0);
	T_EQ_INT(CL_UpscaleNameOk("/textures/x.wal"), 0);
	T_EQ_INT(CL_UpscaleNameOk("/"), 0);
	T_EQ_INT(CL_UpscaleNameOk(".."), 0);
	T_EQ_INT(CL_UpscaleNameOk("../x.wal"), 0);
	T_EQ_INT(CL_UpscaleNameOk("textures/../x.wal"), 0);
	T_EQ_INT(CL_UpscaleNameOk("textures/.."), 0);
	/* a name of the stock pak0 */
	T_EQ_INT(CL_UpscaleNameOk("models/monsters/tank/../ctank/pain.pcx"), 0);
	T_EQ_INT(CL_UpscaleNameOk("a..b.wal"), 0);
	/* an empty component */
	T_EQ_INT(CL_UpscaleNameOk("textures//x.wal"), 0);
	T_EQ_INT(CL_UpscaleNameOk("textures/"), 0);
	/* a component that begins with a dot, the last one included */
	T_EQ_INT(CL_UpscaleNameOk(".wal"), 0);
	T_EQ_INT(CL_UpscaleNameOk("textures/.x.wal"), 0);
	T_EQ_INT(CL_UpscaleNameOk("./x.wal"), 0);
	T_EQ_INT(CL_UpscaleNameOk("textures/./x.wal"), 0);
	T_EQ_INT(CL_UpscaleNameOk("textures\\x.wal"), 0);
	T_EQ_INT(CL_UpscaleNameOk("c:x.wal"), 0);
	T_EQ_INT(CL_UpscaleNameOk("tex tures/x.wal"), 0);
	T_EQ_INT(CL_UpscaleNameOk("x.wal\n"), 0);
	T_EQ_INT(CL_UpscaleNameOk("\xc3\xa9.wal"), 0);
	T_EQ_INT(CL_UpscaleNameOk("x*.wal"), 0);
	T_EQ_INT(CL_UpscaleNameOk("%s.wal"), 0);
}

/* every byte value: in the set or not, wherever it stands in a component */
TEST(upscale_name_ok_character_set)
{
	int c;

	for (c = 1; c < 256; c++)
	{
		int want = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '_' || c == '-' || c == '+';
		char mid[8], first[8], last[8];

		snprintf(mid, sizeof(mid), "a%cb", c);
		snprintf(first, sizeof(first), "%cb", c);
		snprintf(last, sizeof(last), "a%c", c);

		if (c == '.' || c == '/')
		{
			/* the separators: inside a component only */
			T_EQ_INT(CL_UpscaleNameOk(mid), 1);
			T_EQ_INT(CL_UpscaleNameOk(first), 0);
			continue;
		}

		if (CL_UpscaleNameOk(mid) != want || CL_UpscaleNameOk(first) != want ||
			CL_UpscaleNameOk(last) != want)
		{
			t_fail(__FILE__, __LINE__, "byte %d: want %d", c, want);
		}
	}
}

TEST(upscale_out_name)
{
	char out[MAX_QPATH];
	char name[MAX_QPATH + 8];

	T_EQ_INT(CL_UpscaleOutName("textures/e1u1/floor1_1.wal", out, sizeof(out)), 1);
	T_EQ_STR(out, "textures/e1u1/floor1_1.png");
	T_EQ_INT(CL_UpscaleOutName("env/unit1_bk.pcx", out, sizeof(out)), 1);
	T_EQ_STR(out, "env/unit1_bk.png");
	T_EQ_INT(CL_UpscaleOutName("textures/e1u1/PIP04_4.wal", out, sizeof(out)), 1);
	T_EQ_STR(out, "textures/e1u1/PIP04_4.png");
	T_EQ_INT(CL_UpscaleOutName("a.b/c.d.e", out, sizeof(out)), 1);
	T_EQ_STR(out, "a.b/c.d.png");
	T_EQ_INT(CL_UpscaleOutName("a.p", out, sizeof(out)), 1);
	T_EQ_STR(out, "a.png");
	T_EQ_INT(CL_UpscaleOutName("textures/e1u1/+0btshoot1.wal", out, sizeof(out)), 1);
	T_EQ_STR(out, "textures/e1u1/+0btshoot1.png");

	/* no extension in the last component */
	T_EQ_INT(CL_UpscaleOutName("a.b/c", out, sizeof(out)), 0);
	T_EQ_STR(out, "");
	T_EQ_INT(CL_UpscaleOutName("textures/x", out, sizeof(out)), 0);
	T_EQ_INT(CL_UpscaleOutName("textures/x.", out, sizeof(out)), 0);
	T_EQ_STR(out, "");
	/* a name CL_UpscaleNameOk refuses */
	T_EQ_INT(CL_UpscaleOutName("textures/../x.wal", out, sizeof(out)), 0);
	T_EQ_STR(out, "");
	T_EQ_INT(CL_UpscaleOutName(NULL, out, sizeof(out)), 0);

	/* MAX_QPATH: 63 characters and the terminator fit, 64 do not */
	memset(name, 'a', sizeof(name));
	memcpy(name + 59, ".wal", 5);
	T_EQ_INT((int)strlen(name), 63);
	T_EQ_INT(CL_UpscaleOutName(name, out, sizeof(out)), 1);
	T_EQ_INT((int)strlen(out), 63);
	memset(name, 'a', sizeof(name));
	memcpy(name + 60, ".wal", 5);
	T_EQ_INT(CL_UpscaleOutName(name, out, sizeof(out)), 0);
	T_EQ_STR(out, "");
	/* an extension shorter than png makes the result longer */
	memset(name, 'a', sizeof(name));
	memcpy(name + 61, ".p", 3);
	T_EQ_INT(CL_UpscaleOutName(name, out, sizeof(out)), 0);
	memcpy(name + 59, ".p", 3);
	T_EQ_INT(CL_UpscaleOutName(name, out, sizeof(out)), 1);
	T_EQ_INT((int)strlen(out), 63);

	/* MAX_QPATH bounds it with a larger buffer too */
	{
		char big[MAX_QPATH + 16];

		memset(name, 'a', sizeof(name));
		memcpy(name + 60, ".wal", 5);
		T_EQ_INT(CL_UpscaleOutName(name, big, sizeof(big)), 0);
		memcpy(name + 59, ".wal", 5);
		T_EQ_INT(CL_UpscaleOutName(name, big, sizeof(big)), 1);
		T_EQ_INT((int)strlen(big), 63);
	}

	/* the caller's buffer bounds it as well */
	T_EQ_INT(CL_UpscaleOutName("abc.wal", out, 8), 1);
	T_EQ_STR(out, "abc.png");
	T_EQ_INT(CL_UpscaleOutName("abc.wal", out, 7), 0);
	T_EQ_INT(CL_UpscaleOutName("abc.wal", out, 0), 0);
}

/* ---- the git work-tree refusal ---- */

typedef struct
{
	const char *const *entries;
	char calls[16][MAX_OSPATH];
	int ncalls;
} synth_tree_t;

/* exists over a synthetic tree: the paths listed, and records each call */
static int
synth_exists(const char *path, void *ctx)
{
	synth_tree_t *t = ctx;
	int i;

	if (t->ncalls < 16)
	{
		Q_strlcpy(t->calls[t->ncalls], path, sizeof(t->calls[0]));
	}
	t->ncalls++;

	for (i = 0; t->entries[i]; i++)
	{
		if (!strcmp(t->entries[i], path))
		{
			return 1;
		}
	}

	return 0;
}

static int
in_synth(const char *dir, const char *const *entries)
{
	synth_tree_t t;

	memset(&t, 0, sizeof(t));
	t.entries = entries;
	return CL_UpscaleInGitTree(dir, synth_exists, &t);
}

TEST(upscale_git_tree_synthetic)
{
	static const char *const repo[] = { "/r/w/.git", NULL };
	static const char *const root[] = { "/.git", NULL };
	static const char *const none[] = { NULL };
	static const char *const deep[] = { "/r/w/a/b/.git", NULL };

	T_EQ_INT(in_synth("/r/w", repo), 1);
	T_EQ_INT(in_synth("/r/w/", repo), 1);
	T_EQ_INT(in_synth("/r/w/a/b/baseq2", repo), 1);
	T_EQ_INT(in_synth("/r/w//a", repo), 1);
	T_EQ_INT(in_synth("/r", repo), 0);
	T_EQ_INT(in_synth("/r/x/y", repo), 0);
	T_EQ_INT(in_synth("/r/ww", repo), 0);
	T_EQ_INT(in_synth("/", repo), 0);
	T_EQ_INT(in_synth("/", root), 1);
	T_EQ_INT(in_synth("/a/b/c", root), 1);
	T_EQ_INT(in_synth("/a/b/c", none), 0);
	T_EQ_INT(in_synth("/r/w/a/b", deep), 1);
	T_EQ_INT(in_synth("/r/w/a", deep), 0);

	/* fail closed: not absolute, empty, none */
	T_EQ_INT(in_synth("r/w", none), 1);
	T_EQ_INT(in_synth("", none), 1);
	T_EQ_INT(in_synth(NULL, none), 1);
	T_EQ_INT(CL_UpscaleInGitTree("/a", NULL, NULL), 1);
}

/* the walk: the directory itself, then each parent, then the root, once each */
TEST(upscale_git_tree_walk_order)
{
	static const char *const none[] = { NULL };
	synth_tree_t t;

	memset(&t, 0, sizeof(t));
	t.entries = none;
	T_EQ_INT(CL_UpscaleInGitTree("/a/bb/c", synth_exists, &t), 0);
	T_EQ_INT(t.ncalls, 4);
	T_EQ_STR(t.calls[0], "/a/bb/c/.git");
	T_EQ_STR(t.calls[1], "/a/bb/.git");
	T_EQ_STR(t.calls[2], "/a/.git");
	T_EQ_STR(t.calls[3], "/.git");

	memset(&t, 0, sizeof(t));
	t.entries = none;
	T_EQ_INT(CL_UpscaleInGitTree("/", synth_exists, &t), 0);
	T_EQ_INT(t.ncalls, 1);
	T_EQ_STR(t.calls[0], "/.git");
	/* repeated and trailing slashes are dropped, not looked at twice */
	memset(&t, 0, sizeof(t));
	t.entries = none;
	T_EQ_INT(CL_UpscaleInGitTree("/a//b/", synth_exists, &t), 0);
	T_EQ_INT(t.ncalls, 3);
	T_EQ_STR(t.calls[0], "/a//b/.git");
	T_EQ_STR(t.calls[1], "/a/.git");
	T_EQ_STR(t.calls[2], "/.git");
}

/* UPSCALE_GIT_DEPTH components are walked to the root; with one more the walk stops short and refuses */
TEST(upscale_git_tree_depth_bound)
{
	static const char *const none[] = { NULL };
	char dir[MAX_OSPATH];
	char big[MAX_OSPATH + 16];
	synth_tree_t t;
	int i;

	dir[0] = '\0';
	for (i = 0; i < UPSCALE_GIT_DEPTH; i++)
	{
		Q_strlcat(dir, "/d", sizeof(dir));
	}

	memset(&t, 0, sizeof(t));
	t.entries = none;
	T_EQ_INT(CL_UpscaleInGitTree(dir, synth_exists, &t), 0);
	T_EQ_INT(t.ncalls, UPSCALE_GIT_DEPTH + 1);

	Q_strlcat(dir, "/d", sizeof(dir));
	memset(&t, 0, sizeof(t));
	t.entries = none;
	T_EQ_INT(CL_UpscaleInGitTree(dir, synth_exists, &t), 1);
	T_EQ_INT(t.ncalls, UPSCALE_GIT_DEPTH + 1);

	/* a path buffer full is walked, one more character is refused */
	memset(big, 'a', sizeof(big));
	big[0] = '/';
	big[MAX_OSPATH - 1] = '\0';
	T_EQ_INT(in_synth(big, none), 0);
	big[MAX_OSPATH - 1] = 'a';
	big[MAX_OSPATH] = '\0';
	T_EQ_INT(in_synth(big, none), 1);
}

/*
 * An oracle for the on-disk test, apart from the code under test: an
 * entry named .git in the real path of dir or in a directory above it.
 */
static int
oracle_in_git(const char *dir)
{
	char real[PATH_MAX + 8], probe[PATH_MAX + 16];
	size_t len;

	if (!realpath(dir, real))
	{
		return 1;
	}

	for (len = strlen(real); ; )
	{
		struct stat st;

		snprintf(probe, sizeof(probe), "%.*s/.git", (int)(len == 1 ? 0 : len), real);
		if (lstat(probe, &st) == 0)
		{
			return 1;
		}

		if (len <= 1)
		{
			return 0;
		}

		while (len > 1 && real[len - 1] != '/')
		{
			len--;
		}

		if (len > 1)
		{
			len--;
		}
	}
}

/* the same refusal on disk, on a tree under the scratch directory */
TEST(upscale_git_tree_on_disk)
{
	char path[MAX_OSPATH];

	t_write_file("upscale/repo/.git", "gitdir: elsewhere\n", 18);
	t_write_file("upscale/repo/a/b/baseq2/.keep", "", 0);
	t_write_file("upscale/dirrepo/.git/HEAD", "x", 1);
	t_write_file("upscale/dirrepo/c/.keep", "", 0);
	t_write_file("upscale/plain/c/.keep", "", 0);

	snprintf(path, sizeof(path), "%s/upscale/repo/.git", t_tmpdir());
	T_EQ_INT(CL_UpscaleEntryExists(path, NULL), 1);
	snprintf(path, sizeof(path), "%s/upscale/dirrepo/.git", t_tmpdir());
	T_EQ_INT(CL_UpscaleEntryExists(path, NULL), 1);
	snprintf(path, sizeof(path), "%s/upscale/plain/.git", t_tmpdir());
	T_EQ_INT(CL_UpscaleEntryExists(path, NULL), 0);
	snprintf(path, sizeof(path), "%s/upscale/plain/c/.keep/.git", t_tmpdir());
	T_EQ_INT(CL_UpscaleEntryExists(path, NULL), 0);

	snprintf(path, sizeof(path), "%s/upscale/repo/a/b/baseq2", t_tmpdir());
	T_EQ_INT(CL_UpscaleDirInGitTree(path), 1);
	snprintf(path, sizeof(path), "%s/upscale/repo", t_tmpdir());
	T_EQ_INT(CL_UpscaleDirInGitTree(path), 1);
	snprintf(path, sizeof(path), "%s/upscale/dirrepo/c", t_tmpdir());
	T_EQ_INT(CL_UpscaleDirInGitTree(path), 1);
	/* a relative path is resolved first */
	T_EQ_INT(CL_UpscaleDirInGitTree("."), CL_UpscaleDirInGitTree(getcwd(path, sizeof(path))));
	/* outside the synthetic repositories: as the oracle says (0 unless the scratch directory is in a work tree) */
	snprintf(path, sizeof(path), "%s/upscale/plain/c", t_tmpdir());
	T_EQ_INT(CL_UpscaleDirInGitTree(path), oracle_in_git(path));
	/* the real path is walked: a link into a repository is inside it */
	{
		char target[MAX_OSPATH];

		snprintf(target, sizeof(target), "%s/upscale/repo/a", t_tmpdir());
		snprintf(path, sizeof(path), "%s/upscale/plain/link", t_tmpdir());
		T_REQUIRE(symlink(target, path) == 0);
		T_EQ_INT(CL_UpscaleDirInGitTree(path), 1);
		/* and a link out of one, to the plain tree, is where it leads */
		snprintf(target, sizeof(target), "%s/upscale/plain/c", t_tmpdir());
		snprintf(path, sizeof(path), "%s/upscale/repo/out", t_tmpdir());
		T_REQUIRE(symlink(target, path) == 0);
		T_EQ_INT(CL_UpscaleDirInGitTree(path), oracle_in_git(target));
	}
	/* a directory that does not exist has no real path: refused */
	snprintf(path, sizeof(path), "%s/upscale/missing/x", t_tmpdir());
	T_EQ_INT(CL_UpscaleDirInGitTree(path), 1);
	T_EQ_INT(CL_UpscaleDirInGitTree(NULL), 1);
}

/* ---- pixels ---- */

static const unsigned char tile4[16] = {
	0, 1, 2, 3,
	4, 5, 6, 7,
	8, 9, 10, 11,
	12, 13, 14, 15
};

TEST(upscale_wrap_pad_and_crop_4x4)
{
	static const unsigned char want[36] = {
		15, 12, 13, 14, 15, 12,
		3, 0, 1, 2, 3, 0,
		7, 4, 5, 6, 7, 4,
		11, 8, 9, 10, 11, 8,
		15, 12, 13, 14, 15, 12,
		3, 0, 1, 2, 3, 0
	};
	unsigned char pad[36], back[16], part[4];

	memset(pad, 0xee, sizeof(pad));
	T_EQ_INT(CL_UpscaleWrapPad(tile4, 4, 4, pad), 1);
	T_EQ_MEM(pad, want, sizeof(want));

	memset(back, 0xee, sizeof(back));
	T_EQ_INT(CL_UpscaleCrop(pad, 6, 6, 1, 1, 4, 4, back), 1);
	T_EQ_MEM(back, tile4, sizeof(tile4));

	/* a corner block across the seam */
	T_EQ_INT(CL_UpscaleCrop(pad, 6, 6, 4, 4, 2, 2, part), 1);
	T_EQ_INT(part[0], 15);
	T_EQ_INT(part[1], 12);
	T_EQ_INT(part[2], 3);
	T_EQ_INT(part[3], 0);

	/* a block that leaves the source, or is empty, is not copied */
	memset(part, 0xee, sizeof(part));
	T_EQ_INT(CL_UpscaleCrop(pad, 6, 6, 5, 4, 2, 2, part), 0);
	T_EQ_INT(CL_UpscaleCrop(pad, 6, 6, 4, 5, 2, 2, part), 0);
	T_EQ_INT(CL_UpscaleCrop(pad, 6, 6, -1, 0, 2, 2, part), 0);
	T_EQ_INT(CL_UpscaleCrop(pad, 6, 6, 0, -1, 2, 2, part), 0);
	T_EQ_INT(CL_UpscaleCrop(pad, 6, 6, 0, 0, 0, 2, part), 0);
	T_EQ_INT(CL_UpscaleCrop(pad, 6, 6, 0, 0, 2, 0, part), 0);
	T_EQ_INT(part[0], 0xee);
	T_EQ_INT(CL_UpscaleCrop(pad, 6, 6, 0, 0, 6, 6, pad), 1);
	T_EQ_INT(CL_UpscaleCrop(pad, 6, 6, 5, 5, 1, 1, part), 1);
	T_EQ_INT(part[0], 0);

	/* nothing to wrap */
	T_EQ_INT(CL_UpscaleWrapPad(tile4, 0, 4, pad), 0);
	T_EQ_INT(CL_UpscaleWrapPad(tile4, 4, 0, pad), 0);
	T_EQ_INT(CL_UpscaleWrapPad(tile4, 4, 4, pad), 1);
}

TEST(upscale_wrap_pad_1x1_and_2x1)
{
	unsigned char one = 9, pad1[9], pad2[12];
	static const unsigned char two[2] = { 1, 2 };
	static const unsigned char want2[12] = { 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1 };
	int i;

	CL_UpscaleWrapPad(&one, 1, 1, pad1);
	for (i = 0; i < 9; i++)
	{
		T_EQ_INT(pad1[i], 9);
	}

	CL_UpscaleWrapPad(two, 2, 1, pad2);
	T_EQ_MEM(pad2, want2, sizeof(want2));
}

/* a small deterministic generator, few values so that EPX's equalities occur */
static void
fill_tile(unsigned char *p, int n, unsigned seed, int values)
{
	int i;

	for (i = 0; i < n; i++)
	{
		seed = seed * 1103515245u + 12345u;
		p[i] = (unsigned char)((seed >> 16) % (unsigned)values);
	}
}

/* scale2x of the 3 by 3 tiling of a w by h tile, passes times, cropped to the middle tile */
static void
tiled_reference(const unsigned char *tile, int w, int h, int passes, unsigned char *out)
{
	int tw = 3 * w, th = 3 * h, x, y, p;
	unsigned char *a = malloc((size_t)tw * th * 16);
	unsigned char *b = malloc((size_t)tw * th * 16);

	T_REQUIRE(a && b);

	for (y = 0; y < th; y++)
	{
		for (x = 0; x < tw; x++)
		{
			a[y * tw + x] = tile[(y % h) * w + (x % w)];
		}
	}

	for (p = 0; p < passes; p++)
	{
		unsigned char *t;

		CL_UpscaleScale2x(a, b, tw, th);
		tw *= 2;
		th *= 2;
		t = a;
		a = b;
		b = t;
	}

	T_CHECK(CL_UpscaleCrop(a, tw, th, tw / 3, th / 3, tw / 3, th / 3, out));
	free(a);
	free(b);
}

/* wrapped: as if the tile repeated on every side, so it stays seamless */
TEST(upscale_wrap_equals_tiled_scale2x)
{
	unsigned char tile[8 * 8], ref[32 * 32];
	unsigned seed;
	int differs = 0;

	for (seed = 1; seed <= 40; seed++)
	{
		int w = 4 + (int)(seed % 2) * 4, h = 4 + (int)(seed % 3 == 0) * 4;
		int factor = (seed % 4 < 2) ? 2 : 4;
		unsigned char *got, *clamped;

		fill_tile(tile, w * h, seed, 2 + (int)(seed % 3));
		tiled_reference(tile, w, h, factor / 2, ref);

		got = CL_UpscaleIndices(tile, w, h, factor, 1);
		T_REQUIRE(got != NULL);
		if (memcmp(got, ref, (size_t)w * h * factor * factor) != 0)
		{
			t_fail(__FILE__, __LINE__, "seed %u: %dx%d at %dx is not the tiled scale2x", seed, w, h, factor);
		}

		clamped = CL_UpscaleIndices(tile, w, h, factor, 0);
		T_REQUIRE(clamped != NULL);
		differs += memcmp(got, clamped, (size_t)w * h * factor * factor) != 0;
		free(got);
		free(clamped);
	}

	/* the edges matter: some tiles come out differently clamped */
	T_CHECK(differs > 0);
}

/* clamped, for skins and skies: scale2x as it is, its edges its own */
TEST(upscale_clamp_is_scale2x)
{
	unsigned char tile[8 * 8], one[16 * 16], two[32 * 32];
	unsigned seed;

	for (seed = 1; seed <= 20; seed++)
	{
		unsigned char *got;

		fill_tile(tile, 8 * 8, seed, 3);
		CL_UpscaleScale2x(tile, one, 8, 8);
		got = CL_UpscaleIndices(tile, 8, 8, 2, 0);
		T_REQUIRE(got != NULL);
		T_EQ_MEM(got, one, sizeof(one));
		free(got);

		CL_UpscaleScale2x(one, two, 16, 16);
		got = CL_UpscaleIndices(tile, 8, 8, 4, 0);
		T_REQUIRE(got != NULL);
		T_EQ_MEM(got, two, sizeof(two));
		free(got);
	}
}

/* EPX by hand: each of the four rules rounds one corner of a lone texel */
TEST(upscale_scale2x_rules)
{
	static const unsigned char src[4][4] = {
		{ 1, 0, 0, 0 },
		{ 0, 1, 0, 0 },
		{ 0, 0, 1, 0 },
		{ 0, 0, 0, 1 }
	};
	static const unsigned char want[4][16] = {
		{ 1, 1, 0, 0,  1, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0 },
		{ 0, 0, 1, 1,  0, 0, 0, 1,  0, 0, 0, 0,  0, 0, 0, 0 },
		{ 0, 0, 0, 0,  0, 0, 0, 0,  1, 0, 0, 0,  1, 1, 0, 0 },
		{ 0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 1,  0, 0, 1, 1 }
	};
	unsigned char dst[16];
	unsigned char one = 5, four[4];
	int i;

	for (i = 0; i < 4; i++)
	{
		memset(dst, 0xee, sizeof(dst));
		CL_UpscaleScale2x(src[i], dst, 2, 2);
		T_EQ_MEM(dst, want[i], sizeof(dst));
	}

	CL_UpscaleScale2x(&one, four, 1, 1);
	T_EQ_INT(four[0], 5);
	T_EQ_INT(four[1], 5);
	T_EQ_INT(four[2], 5);
	T_EQ_INT(four[3], 5);
}

TEST(upscale_indices_refuses)
{
	unsigned char px[4] = { 0 };

	T_CHECK(CL_UpscaleIndices(px, 2, 2, 3, 1) == NULL);
	T_CHECK(CL_UpscaleIndices(px, 2, 2, 1, 1) == NULL);
	T_CHECK(CL_UpscaleIndices(px, 2, 2, 8, 0) == NULL);
	T_CHECK(CL_UpscaleIndices(px, 0, 2, 2, 1) == NULL);
	T_CHECK(CL_UpscaleIndices(px, 2, -1, 2, 1) == NULL);
	T_CHECK(CL_UpscaleIndices(px, UPSCALE_MAX_SIDE + 1, 1, 2, 0) == NULL);
	T_CHECK(CL_UpscaleIndices(NULL, 2, 2, 2, 0) == NULL);
}

TEST(upscale_size_ok)
{
	T_EQ_INT(CL_UpscaleSizeOk(1, 1), 1);
	T_EQ_INT(CL_UpscaleSizeOk(UPSCALE_MAX_SIDE, UPSCALE_MAX_SIDE), 1);
	T_EQ_INT(CL_UpscaleSizeOk(UPSCALE_MAX_SIDE + 1, 1), 0);
	T_EQ_INT(CL_UpscaleSizeOk(1, UPSCALE_MAX_SIDE + 1), 0);
	T_EQ_INT(CL_UpscaleSizeOk(0, 1), 0);
	T_EQ_INT(CL_UpscaleSizeOk(1, 0), 0);
	T_EQ_INT(CL_UpscaleSizeOk(-1, 4), 0);
	T_EQ_INT(CL_UpscaleSizeOk(4, -1), 0);
	T_EQ_INT(CL_UpscaleSizeOk(0x100000000LL + 4, 4), 0);
}

/* ---- the transparent index ---- */

static void
grey_palette(unsigned char *pal)
{
	int i;

	for (i = 0; i < 256; i++)
	{
		pal[i * 3 + 0] = (unsigned char)i;
		pal[i * 3 + 1] = (unsigned char)(255 - i);
		pal[i * 3 + 2] = (unsigned char)(i ^ 0x5a);
	}
}

TEST(upscale_table_index_255_transparent)
{
	unsigned char pal[768];
	unsigned table[256];
	int i;

	grey_palette(pal);
	CL_UpscaleTable(pal, table);

	for (i = 0; i < 256; i++)
	{
		const unsigned char *t = (const unsigned char *)&table[i];

		T_EQ_INT(t[0], pal[i * 3 + 0]);
		T_EQ_INT(t[1], pal[i * 3 + 1]);
		T_EQ_INT(t[2], pal[i * 3 + 2]);
		T_EQ_INT(t[3], i == 255 ? 0 : 255);
	}
}

/*
 * CL_UpscaleToRGBA's buffer copied into out, w * h * 4 bytes, and freed.
 * Its NULL return needs R_Convert8to32 to fail, which it does only through
 * Com_Error, and that does not return, so no test reaches it.
 */
static void
to_rgba(const unsigned char *data, size_t w, size_t h, const unsigned *table,
		unsigned char *out)
{
	unsigned char *got = CL_UpscaleToRGBA(data, w, h, table);

	T_REQUIRE(got != NULL);
	memcpy(out, got, w * h * 4);
	free(got);
}

/* R_Convert8to32's order for a 255 texel's colour: above, below, left, right, else index 0 */
TEST(upscale_to_rgba_neighbour_colour)
{
	static const unsigned char img[] = {
		10, 20, 30,
		40, 255, 60,
		70, 80, 90
	};
	static const unsigned char row[] = { 255, 255, 7 };
	static const unsigned char lone[] = { 255 };
	unsigned char pal[768], rgba[9 * 4], copy[9];
	unsigned table[256];
	int i;

	grey_palette(pal);
	CL_UpscaleTable(pal, table);

	to_rgba(img, 3, 3, table, rgba);
	for (i = 0; i < 9; i++)
	{
		int want = img[i] == 255 ? 20 : img[i];

		T_EQ_INT(rgba[i * 4 + 0], pal[want * 3 + 0]);
		T_EQ_INT(rgba[i * 4 + 1], pal[want * 3 + 1]);
		T_EQ_INT(rgba[i * 4 + 2], pal[want * 3 + 2]);
		T_EQ_INT(rgba[i * 4 + 3], img[i] == 255 ? 0 : 255);
	}

	/* above is 255 too: below */
	memcpy(copy, img, sizeof(copy));
	copy[1] = 255;
	to_rgba(copy, 3, 3, table, rgba);
	T_EQ_INT(rgba[4 * 4 + 0], pal[80 * 3]);
	T_EQ_INT(rgba[1 * 4 + 3], 0);
	/* index 1, in the first row: above is out, below is 255, so left */
	T_EQ_INT(rgba[1 * 4 + 0], pal[10 * 3]);
	/* and with the left 255 too: right */
	copy[0] = 255;
	to_rgba(copy, 3, 3, table, rgba);
	T_EQ_INT(rgba[1 * 4 + 0], pal[30 * 3]);
	/* index 0: nothing above or left, below (40) */
	T_EQ_INT(rgba[0 * 4 + 0], pal[40 * 3]);
	/* index 3 (40) itself stays */
	T_EQ_INT(rgba[3 * 4 + 0], pal[40 * 3]);

	/* index i == w: the texel above is index 0, which R_Convert8to32's i > w passes over */
	memcpy(copy, img, sizeof(copy));
	copy[3] = 255;
	to_rgba(copy, 3, 3, table, rgba);
	T_EQ_INT(rgba[3 * 4 + 0], pal[70 * 3]);

	/* one row: left, then right, and index 0 when neither */
	to_rgba(row, 3, 1, table, rgba);
	T_EQ_INT(rgba[0 * 4 + 0], pal[0]);
	T_EQ_INT(rgba[1 * 4 + 0], pal[7 * 3]);
	T_EQ_INT(rgba[0 * 4 + 3], 0);
	T_EQ_INT(rgba[1 * 4 + 3], 0);
	T_EQ_INT(rgba[2 * 4 + 3], 255);

	/* nothing around it: index 0's colour */
	to_rgba(lone, 1, 1, table, rgba);
	T_EQ_INT(rgba[0], pal[0]);
	T_EQ_INT(rgba[1], pal[1]);
	T_EQ_INT(rgba[2], pal[2]);
	T_EQ_INT(rgba[3], 0);
}

/* scaled, a transparent texel stays transparent: 255 is copied, never blended */
TEST(upscale_transparent_kept_through_scale)
{
	unsigned char src[16], pal[768], *big, *rgba;
	unsigned table[256];
	int x, y, factor;

	grey_palette(pal);
	CL_UpscaleTable(pal, table);

	for (factor = 2; factor <= 4; factor += 2)
	{
		int s = 4 * factor, clear = 0;

		memset(src, 7, sizeof(src));
		src[1 * 4 + 1] = 255;
		big = CL_UpscaleIndices(src, 4, 4, factor, 1);
		T_REQUIRE(big != NULL);
		rgba = malloc((size_t)s * s * 4);
		T_REQUIRE(rgba != NULL);
		to_rgba(big, s, s, table, rgba);

		for (y = 0; y < s; y++)
		{
			for (x = 0; x < s; x++)
			{
				int in = (x / factor == 1) && (y / factor == 1);
				unsigned char i = big[y * s + x];

				/* only inside the texel's own block, and nothing but 7 and 255 */
				T_CHECK(i == 7 || (i == 255 && in));
				T_EQ_INT(rgba[(y * s + x) * 4 + 3], i == 255 ? 0 : 255);
				clear += i == 255;
			}
		}

		/* 2x: the whole block; 4x: EPX rounds its four corners */
		T_EQ_INT(clear, factor == 2 ? 4 : 12);
		/* a transparent texel at the block's edge takes the colour around it */
		T_EQ_INT(rgba[(factor * s + factor + 1) * 4 + 0], pal[7 * 3]);
		T_EQ_INT(rgba[(factor * s + factor + 1) * 4 + 3], 0);

		free(rgba);
		free(big);
	}
}

/* ---- skins ---- */

TEST(upscale_flood_fill_skin)
{
	/* the background (3) from the corner, an island of 3 inside the 9s */
	unsigned char skin[] = {
		3, 3, 3, 3,
		3, 9, 9, 3,
		3, 9, 3, 9,
		3, 3, 9, 9
	};
	unsigned char black[] = { 0, 0, 5, 5 };
	unsigned char clear[] = { 255, 255, 5, 5 };
	unsigned char pal[768];
	unsigned table[256];
	int i;

	grey_palette(pal);
	CL_UpscaleTable(pal, table);

	CL_UpscaleFloodFillSkin(skin, 4, 4, table);
	for (i = 0; i < 16; i++)
	{
		/* the corner's region takes a neighbour's colour, or index 0 */
		if (i == 2 * 4 + 2)
		{
			/* the island is not connected to the corner: it stays */
			T_EQ_INT(skin[i], 3);
		}
		else if (i == 1 * 4 + 1 || i == 1 * 4 + 2 || i == 2 * 4 + 1 ||
			i == 2 * 4 + 3 || i == 3 * 4 + 2 || i == 3 * 4 + 3)
		{
			T_EQ_INT(skin[i], 9);
		}
		else
		{
			T_CHECK(skin[i] == 0 || skin[i] == 9);
		}
	}
	/* R_FloodFillSkin's order: the corner has no other colour beside it, its right neighbour has 9 below */
	T_EQ_INT(skin[0], 0);
	T_EQ_INT(skin[1], 9);

	/* the corner already the fill colour (index 0) or transparent: unchanged */
	CL_UpscaleFloodFillSkin(black, 2, 2, table);
	T_EQ_INT(black[0], 0);
	T_EQ_INT(black[1], 0);
	CL_UpscaleFloodFillSkin(clear, 2, 2, table);
	T_EQ_INT(clear[0], 255);
	T_EQ_INT(clear[1], 255);
}

/* ---- the source files' sizes ---- */

static void
put_le32(unsigned char *p, unsigned v)
{
	p[0] = (unsigned char)v;
	p[1] = (unsigned char)(v >> 8);
	p[2] = (unsigned char)(v >> 16);
	p[3] = (unsigned char)(v >> 24);
}

TEST(upscale_wal_pixels)
{
	unsigned char wal[sizeof(miptex_t) + 16];
	size_t hdr = sizeof(miptex_t), ofs;
	int w, h;

	memset(wal, 0, sizeof(wal));
	put_le32(wal + 32, 4);
	put_le32(wal + 36, 4);
	put_le32(wal + 40, (unsigned)hdr);

	T_EQ_INT(CL_UpscaleWalPixels(wal, sizeof(wal), &w, &h, &ofs), 1);
	T_EQ_INT(w, 4);
	T_EQ_INT(h, 4);
	T_EQ_INT((long long)ofs, (long long)hdr);

	/* one byte short of the pixels */
	T_EQ_INT(CL_UpscaleWalPixels(wal, sizeof(wal) - 1, &w, &h, &ofs), 0);
	/* shorter than the header */
	T_EQ_INT(CL_UpscaleWalPixels(wal, hdr - 1, &w, &h, &ofs), 0);
	T_EQ_INT(CL_UpscaleWalPixels(NULL, sizeof(wal), &w, &h, &ofs), 0);

	/* the offset past the end, and one that wraps */
	put_le32(wal + 40, (unsigned)sizeof(wal));
	T_EQ_INT(CL_UpscaleWalPixels(wal, sizeof(wal), &w, &h, &ofs), 0);
	put_le32(wal + 40, 0xfffffff0u);
	T_EQ_INT(CL_UpscaleWalPixels(wal, sizeof(wal), &w, &h, &ofs), 0);
	/* the pixels start after the header (the renderers refuse an offset of 0) */
	put_le32(wal + 40, 0);
	T_EQ_INT(CL_UpscaleWalPixels(wal, sizeof(wal), &w, &h, &ofs), 0);
	put_le32(wal + 40, (unsigned)hdr - 1);
	T_EQ_INT(CL_UpscaleWalPixels(wal, sizeof(wal), &w, &h, &ofs), 0);
	put_le32(wal + 40, (unsigned)hdr);

	/* sizes */
	put_le32(wal + 32, 0);
	T_EQ_INT(CL_UpscaleWalPixels(wal, sizeof(wal), &w, &h, &ofs), 0);
	put_le32(wal + 32, UPSCALE_MAX_SIDE + 1);
	T_EQ_INT(CL_UpscaleWalPixels(wal, sizeof(wal), &w, &h, &ofs), 0);
	put_le32(wal + 32, 0x80000004u);
	T_EQ_INT(CL_UpscaleWalPixels(wal, sizeof(wal), &w, &h, &ofs), 0);
	put_le32(wal + 32, 4);
	put_le32(wal + 36, 0x10000u);
	T_EQ_INT(CL_UpscaleWalPixels(wal, sizeof(wal), &w, &h, &ofs), 0);
	put_le32(wal + 36, 2);
	T_EQ_INT(CL_UpscaleWalPixels(wal, sizeof(wal), &w, &h, &ofs), 1);
	T_EQ_INT(h, 2);
}

static void
put_le16(unsigned char *p, unsigned v)
{
	p[0] = (unsigned char)v;
	p[1] = (unsigned char)(v >> 8);
}

static void
pcx_header(unsigned char *p, int xmin, int ymin, int xmax, int ymax)
{
	memset(p, 0, 129);
	p[0] = 0x0a;
	p[1] = 5;
	p[2] = 1;
	p[3] = 8;
	put_le16(p + 4, (unsigned)xmin);
	put_le16(p + 6, (unsigned)ymin);
	put_le16(p + 8, (unsigned)xmax);
	put_le16(p + 10, (unsigned)ymax);
	p[65] = 1;
	put_le16(p + 66, (unsigned)(xmax - xmin + 1));
}

TEST(upscale_pcx_size)
{
	unsigned char pcx[129];
	int w, h;

	pcx_header(pcx, 0, 0, 255, 127);
	T_EQ_INT(CL_UpscalePcxSize(pcx, sizeof(pcx), &w, &h), 1);
	T_EQ_INT(w, 256);
	T_EQ_INT(h, 128);

	pcx_header(pcx, 2, 3, 2, 3);
	T_EQ_INT(CL_UpscalePcxSize(pcx, sizeof(pcx), &w, &h), 1);
	T_EQ_INT(w, 1);
	T_EQ_INT(h, 1);

	T_EQ_INT(CL_UpscalePcxSize(pcx, 128, &w, &h), 0);
	T_EQ_INT(CL_UpscalePcxSize(NULL, sizeof(pcx), &w, &h), 0);

	pcx_header(pcx, 0, 0, UPSCALE_MAX_SIDE - 1, 0);
	T_EQ_INT(CL_UpscalePcxSize(pcx, sizeof(pcx), &w, &h), 1);
	pcx_header(pcx, 0, 0, UPSCALE_MAX_SIDE, 0);
	T_EQ_INT(CL_UpscalePcxSize(pcx, sizeof(pcx), &w, &h), 0);
	pcx_header(pcx, 0, 0, 0, UPSCALE_MAX_SIDE);
	T_EQ_INT(CL_UpscalePcxSize(pcx, sizeof(pcx), &w, &h), 0);
	pcx_header(pcx, 5, 0, 4, 0);
	T_EQ_INT(CL_UpscalePcxSize(pcx, sizeof(pcx), &w, &h), 0);
	pcx_header(pcx, 0, 0, 65535, 65535);
	T_EQ_INT(CL_UpscalePcxSize(pcx, sizeof(pcx), &w, &h), 0);

	/* only 8-bit, one plane, the PCX the renderers read as indices */
	pcx_header(pcx, 0, 0, 3, 3);
	pcx[65] = 3;
	T_EQ_INT(CL_UpscalePcxSize(pcx, sizeof(pcx), &w, &h), 0);
	pcx_header(pcx, 0, 0, 3, 3);
	pcx[3] = 4;
	T_EQ_INT(CL_UpscalePcxSize(pcx, sizeof(pcx), &w, &h), 0);
	pcx_header(pcx, 0, 0, 3, 3);
	pcx[0] = 0x0b;
	T_EQ_INT(CL_UpscalePcxSize(pcx, sizeof(pcx), &w, &h), 0);
	pcx_header(pcx, 0, 0, 3, 3);
	pcx[1] = 4;
	T_EQ_INT(CL_UpscalePcxSize(pcx, sizeof(pcx), &w, &h), 0);
	pcx_header(pcx, 0, 0, 3, 3);
	pcx[2] = 0;
	T_EQ_INT(CL_UpscalePcxSize(pcx, sizeof(pcx), &w, &h), 0);
}

/* the colormap's palette: the last 768 bytes, of an 8-bit one-plane file only */
TEST(upscale_palette)
{
	static unsigned char pcx[128 + 1 + 768];

	pcx_header(pcx, 0, 0, 255, 319);
	pcx[128] = 0x0c;
	pcx[129] = 0x11;
	pcx[sizeof(pcx) - 1] = 0x22;
	T_CHECK(CL_UpscalePalette(pcx, sizeof(pcx)) == pcx + sizeof(pcx) - 768);
	T_EQ_INT(CL_UpscalePalette(pcx, sizeof(pcx))[0], 0x11);
	T_EQ_INT(CL_UpscalePalette(pcx, sizeof(pcx))[767], 0x22);

	/* the palette after the header, not over it */
	T_CHECK(CL_UpscalePalette(pcx, sizeof(pcx) - 1) == NULL);
	T_CHECK(CL_UpscalePalette(pcx, 768) == NULL);
	T_CHECK(CL_UpscalePalette(pcx, 129) == NULL);
	T_CHECK(CL_UpscalePalette(NULL, sizeof(pcx)) == NULL);

	/* the header as the skins are checked: 8 bits, one plane */
	pcx[3] = 4;
	T_CHECK(CL_UpscalePalette(pcx, sizeof(pcx)) == NULL);
	pcx[3] = 8;
	pcx[65] = 3;
	T_CHECK(CL_UpscalePalette(pcx, sizeof(pcx)) == NULL);
	pcx[65] = 1;
	T_CHECK(CL_UpscalePalette(pcx, sizeof(pcx)) != NULL);
}

TEST(upscale_pcx_decoded_ok)
{
	unsigned char pic[1];

	T_EQ_INT(CL_UpscalePcxDecodedOk(pic, 8, 4, 3, 4, 3), 1);
	T_EQ_INT(CL_UpscalePcxDecodedOk(NULL, 8, 4, 3, 4, 3), 0);
	T_EQ_INT(CL_UpscalePcxDecodedOk(pic, 24, 4, 3, 4, 3), 0);
	T_EQ_INT(CL_UpscalePcxDecodedOk(pic, 8, 5, 3, 4, 3), 0);
	T_EQ_INT(CL_UpscalePcxDecodedOk(pic, 8, 4, 2, 4, 3), 0);
	T_EQ_INT(CL_UpscalePcxDecodedOk(pic, 8, 3, 4, 4, 3), 0);
}

/* ---- the command's decisions ---- */

TEST(upscale_kinds)
{
	const upscale_kind_t *k;

	k = CL_UpscaleKind(0);
	T_REQUIRE(k != NULL);
	T_EQ_STR(k->dir, "textures");
	T_EQ_STR(k->ext, "wal");
	T_CHECK(k->notsuffix == NULL);
	T_EQ_INT(k->wal, 1);
	T_EQ_INT(k->wrap, 1);
	T_EQ_INT(k->skin, 0);

	k = CL_UpscaleKind(1);
	T_REQUIRE(k != NULL);
	T_EQ_STR(k->dir, "env");
	T_EQ_STR(k->ext, "pcx");
	T_CHECK(k->notsuffix == NULL);
	T_EQ_INT(k->wal, 0);
	T_EQ_INT(k->wrap, 0);
	T_EQ_INT(k->skin, 0);

	k = CL_UpscaleKind(2);
	T_REQUIRE(k != NULL);
	T_EQ_STR(k->dir, "models");
	T_EQ_STR(k->ext, "pcx");
	T_CHECK(k->notsuffix == NULL);
	T_EQ_INT(k->wal, 0);
	T_EQ_INT(k->wrap, 0);
	T_EQ_INT(k->skin, 1);

	k = CL_UpscaleKind(3);
	T_REQUIRE(k != NULL);
	T_EQ_STR(k->dir, "players");
	T_EQ_STR(k->ext, "pcx");
	T_EQ_STR(k->notsuffix, "_i.pcx");
	T_EQ_INT(k->wal, 0);
	T_EQ_INT(k->wrap, 0);
	T_EQ_INT(k->skin, 1);

	T_CHECK(CL_UpscaleKind(UPSCALE_KINDS) == NULL);
	T_CHECK(CL_UpscaleKind(-1) == NULL);
}

TEST(upscale_is_source)
{
	const upscale_kind_t *tex = CL_UpscaleKind(0), *env = CL_UpscaleKind(1);
	const upscale_kind_t *mod = CL_UpscaleKind(2), *pl = CL_UpscaleKind(3);

	T_EQ_INT(CL_UpscaleIsSource(tex, "textures/e1u1/floor1_1.wal"), 1);
	T_EQ_INT(CL_UpscaleIsSource(tex, "textures/e1u1/FLOOR1_1.WAL"), 1);
	T_EQ_INT(CL_UpscaleIsSource(tex, "textures/x.wal"), 1);
	T_EQ_INT(CL_UpscaleIsSource(tex, "TEXTURES/x.wal"), 1);
	/* another directory of the same length */
	T_EQ_INT(CL_UpscaleIsSource(tex, "texturez/x.wal"), 0);
	T_EQ_INT(CL_UpscaleIsSource(mod, "players/x/y.pcx"), 0);
	/* the shortest: one character before the extension */
	T_EQ_INT(CL_UpscaleIsSource(tex, "textures/.wal"), 0);
	T_EQ_INT(CL_UpscaleIsSource(tex, "textures/"), 0);
	T_EQ_INT(CL_UpscaleIsSource(tex, "textures"), 0);
	T_EQ_INT(CL_UpscaleIsSource(tex, "textures/e1u1/x.pcx"), 0);
	T_EQ_INT(CL_UpscaleIsSource(tex, "textures/e1u1/xwal"), 0);
	T_EQ_INT(CL_UpscaleIsSource(tex, "textures/e1u1/x.wal.bak"), 0);
	T_EQ_INT(CL_UpscaleIsSource(tex, "texturesx/x.wal"), 0);
	T_EQ_INT(CL_UpscaleIsSource(tex, "textures.wal"), 0);
	T_EQ_INT(CL_UpscaleIsSource(tex, "env/x.wal"), 0);
	T_EQ_INT(CL_UpscaleIsSource(tex, "a/textures/x.wal"), 0);
	T_EQ_INT(CL_UpscaleIsSource(env, "env/unit1_bk.pcx"), 1);
	T_EQ_INT(CL_UpscaleIsSource(env, "env/unit1_bk.tga"), 0);

	/* the players' _i.pcx icons are not sources, the models' are */
	T_EQ_INT(CL_UpscaleIsSource(pl, "players/male/grunt.pcx"), 1);
	T_EQ_INT(CL_UpscaleIsSource(pl, "players/male/grunt_i.pcx"), 0);
	T_EQ_INT(CL_UpscaleIsSource(pl, "players/male/GRUNT_I.PCX"), 0);
	T_EQ_INT(CL_UpscaleIsSource(pl, "players/male/_i.pcx"), 0);
	T_EQ_INT(CL_UpscaleIsSource(pl, "players/male/i.pcx"), 1);
	T_EQ_INT(CL_UpscaleIsSource(pl, "players/male/grunt_j.pcx"), 1);
	T_EQ_INT(CL_UpscaleIsSource(mod, "models/x/skin_i.pcx"), 1);
	T_EQ_INT(CL_UpscaleIsSource(mod, "players/male/grunt.pcx"), 0);

	T_EQ_INT(CL_UpscaleIsSource(NULL, "textures/x.wal"), 0);
	T_EQ_INT(CL_UpscaleIsSource(tex, NULL), 0);
}

TEST(upscale_list_add)
{
	strlist_t l;
	int cut = 0;

	StrList_Init(&l, 0);
	T_EQ_INT(CL_UpscaleListAdd(&l, "a", 2, &cut), 1);
	T_EQ_INT(CL_UpscaleListAdd(&l, "a", 2, &cut), 1);
	T_EQ_INT(l.num, 1);
	T_EQ_INT(CL_UpscaleListAdd(&l, "b", 2, &cut), 1);
	T_EQ_INT(l.num, 2);
	T_EQ_INT(cut, 0);
	/* full: a name that is there is not a cut, a new one is */
	T_EQ_INT(CL_UpscaleListAdd(&l, "b", 2, &cut), 1);
	T_EQ_INT(cut, 0);
	T_EQ_INT(CL_UpscaleListAdd(&l, "c", 2, &cut), 0);
	T_EQ_INT(cut, 1);
	T_EQ_INT(l.num, 2);
	T_EQ_STR(l.data[0], "a");
	T_EQ_STR(l.data[1], "b");
	StrList_Free(&l);

	cut = 0;
	StrList_Init(&l, 0);
	T_EQ_INT(CL_UpscaleListAdd(&l, "x", 0, &cut), 0);
	T_EQ_INT(cut, 1);
	T_EQ_INT(l.num, 0);
	StrList_Free(&l);
}

TEST(upscale_out_path)
{
	char out[32];

	T_EQ_INT(CL_UpscaleOutPath("/g/baseq2", "textures/x.png", out, sizeof(out)), 1);
	T_EQ_STR(out, "/g/baseq2/textures/x.png");
	/* 24 characters and the terminator */
	T_EQ_INT(CL_UpscaleOutPath("/g/baseq2", "textures/x.png", out, 25), 1);
	T_EQ_INT(CL_UpscaleOutPath("/g/baseq2", "textures/x.png", out, 24), 0);
	T_EQ_STR(out, "");
	T_EQ_INT(CL_UpscaleOutPath(NULL, "x.png", out, sizeof(out)), 0);
	T_EQ_INT(CL_UpscaleOutPath("/g", NULL, out, sizeof(out)), 0);
	T_EQ_INT(CL_UpscaleOutPath("/g", "x.png", out, 0), 0);
}

TEST(upscale_temp_path)
{
	char out[32];

	T_EQ_INT(CL_UpscaleTempPath("/g/x.png", 4321, out, sizeof(out)), 1);
	T_EQ_STR(out, "/g/x.png.4321.tmp");
	/* 17 characters and the terminator */
	T_EQ_INT(CL_UpscaleTempPath("/g/x.png", 4321, out, 18), 1);
	T_EQ_INT(CL_UpscaleTempPath("/g/x.png", 4321, out, 17), 0);
	T_EQ_STR(out, "");
	T_EQ_INT(CL_UpscaleTempPath(NULL, 1, out, sizeof(out)), 0);
	T_EQ_INT(CL_UpscaleTempPath("/g/x.png", 1, out, 0), 0);
}

TEST(upscale_parent_dir)
{
	char out[32];

	T_EQ_INT(CL_UpscaleParentDir("/g/baseq2/textures/x.png", out, sizeof(out)), 1);
	T_EQ_STR(out, "/g/baseq2/textures");
	T_EQ_INT(CL_UpscaleParentDir("/x.png", out, sizeof(out)), 1);
	T_EQ_STR(out, "/");
	T_EQ_INT(CL_UpscaleParentDir("/a/b", out, 3), 1);
	T_EQ_STR(out, "/a");
	T_EQ_INT(CL_UpscaleParentDir("/a/b", out, 2), 0);
	T_EQ_STR(out, "");
	T_EQ_INT(CL_UpscaleParentDir("x.png", out, sizeof(out)), 0);
	T_EQ_STR(out, "");
	T_EQ_INT(CL_UpscaleParentDir(NULL, out, sizeof(out)), 0);
	T_EQ_INT(CL_UpscaleParentDir("/a/b", out, 0), 0);
}

TEST(upscale_action)
{
	T_EQ_INT(CL_UpscaleAction(UPSCALE_ENTRY_NONE, 0), UPSCALE_WRITE);
	T_EQ_INT(CL_UpscaleAction(UPSCALE_ENTRY_NONE, 1), UPSCALE_WRITE);
	T_EQ_INT(CL_UpscaleAction(UPSCALE_ENTRY_FILE, 0), UPSCALE_SKIP);
	T_EQ_INT(CL_UpscaleAction(UPSCALE_ENTRY_FILE, 1), UPSCALE_WRITE);
	/* a link is never followed: left alone, and with force refused */
	T_EQ_INT(CL_UpscaleAction(UPSCALE_ENTRY_OTHER, 0), UPSCALE_SKIP);
	T_EQ_INT(CL_UpscaleAction(UPSCALE_ENTRY_OTHER, 1), UPSCALE_REFUSE);
	T_EQ_INT(CL_UpscaleAction(UPSCALE_ENTRY_UNKNOWN, 0), UPSCALE_REFUSE);
	T_EQ_INT(CL_UpscaleAction(UPSCALE_ENTRY_UNKNOWN, 1), UPSCALE_REFUSE);
	T_EQ_INT(CL_UpscaleAction((upscale_entry_t)99, 1), UPSCALE_REFUSE);
}

/* a directory with mode 000 above the path; the caller restores it */
static int
locked_dir(char *dir, size_t size)
{
	t_write_file("upscale/locked/sub/.keep", "", 0);
	snprintf(dir, size, "%s/upscale/locked", t_tmpdir());

	/* root searches any directory: the case cannot be made */
	return (geteuid() != 0) && (chmod(dir, 0) == 0);
}

TEST(upscale_entry_kind_on_disk)
{
	char path[MAX_OSPATH], target[MAX_OSPATH], dir[MAX_OSPATH];

	t_write_file("upscale/entry/file.png", "x", 1);
	snprintf(path, sizeof(path), "%s/upscale/entry/file.png", t_tmpdir());
	T_EQ_INT(CL_UpscaleEntryKind(path), UPSCALE_ENTRY_FILE);
	snprintf(target, sizeof(target), "%s", path);
	snprintf(path, sizeof(path), "%s/upscale/entry/missing.png", t_tmpdir());
	T_EQ_INT(CL_UpscaleEntryKind(path), UPSCALE_ENTRY_NONE);
	snprintf(path, sizeof(path), "%s/upscale/entry", t_tmpdir());
	T_EQ_INT(CL_UpscaleEntryKind(path), UPSCALE_ENTRY_OTHER);

	/* a link to a file, or to nothing, is a link */
	snprintf(path, sizeof(path), "%s/upscale/entry/link.png", t_tmpdir());
	T_REQUIRE(symlink(target, path) == 0);
	T_EQ_INT(CL_UpscaleEntryKind(path), UPSCALE_ENTRY_OTHER);
	snprintf(target, sizeof(target), "%s/upscale/entry/nothing", t_tmpdir());
	snprintf(path, sizeof(path), "%s/upscale/entry/dangling.png", t_tmpdir());
	T_REQUIRE(symlink(target, path) == 0);
	T_EQ_INT(CL_UpscaleEntryKind(path), UPSCALE_ENTRY_OTHER);

	/* below a file: lstat cannot tell, so it is not "none" */
	snprintf(path, sizeof(path), "%s/upscale/entry/file.png/x.png", t_tmpdir());
	T_EQ_INT(CL_UpscaleEntryKind(path), UPSCALE_ENTRY_UNKNOWN);

	if (locked_dir(dir, sizeof(dir)))
	{
		snprintf(path, sizeof(path), "%s/sub/x.png", dir);
		T_EQ_INT(CL_UpscaleEntryKind(path), UPSCALE_ENTRY_UNKNOWN);
		T_REQUIRE(chmod(dir, 0755) == 0);
	}
}

/* the .git probe fails closed: an entry lstat cannot look at counts as there */
TEST(upscale_git_probe_fails_closed)
{
	char path[MAX_OSPATH], dir[MAX_OSPATH];

	if (!locked_dir(dir, sizeof(dir)))
	{
		return;
	}

	snprintf(path, sizeof(path), "%s/sub/.git", dir);
	T_EQ_INT(CL_UpscaleEntryExists(path, NULL), 1);
	T_REQUIRE(chmod(dir, 0755) == 0);
	/* searchable again: the answer is the entry's */
	T_EQ_INT(CL_UpscaleEntryExists(path, NULL), 0);
}

typedef struct
{
	int calls;
	int answer;
} count_check_t;

static count_check_t count_check;

static int
counted_check(const char *dir)
{
	(void)dir;
	count_check.calls++;
	return count_check.answer;
}

TEST(upscale_cached_git_check)
{
	upscale_gitcache_t cache;
	char big[MAX_OSPATH + 8];

	memset(&cache, 0, sizeof(cache));
	memset(&count_check, 0, sizeof(count_check));

	count_check.answer = 1;
	T_EQ_INT(CL_UpscaleCachedInGitTree(&cache, "/g/a", counted_check), 1);
	T_EQ_INT(count_check.calls, 1);
	count_check.answer = 0;
	T_EQ_INT(CL_UpscaleCachedInGitTree(&cache, "/g/a", counted_check), 1);
	T_EQ_INT(count_check.calls, 1);
	T_EQ_INT(CL_UpscaleCachedInGitTree(&cache, "/g/b", counted_check), 0);
	T_EQ_INT(count_check.calls, 2);
	count_check.answer = 1;
	T_EQ_INT(CL_UpscaleCachedInGitTree(&cache, "/g/b", counted_check), 0);
	T_EQ_INT(count_check.calls, 2);
	/* the last directory only: a prefix is another directory */
	T_EQ_INT(CL_UpscaleCachedInGitTree(&cache, "/g", counted_check), 1);
	T_EQ_INT(count_check.calls, 3);
	T_EQ_INT(CL_UpscaleCachedInGitTree(&cache, "/g/a", counted_check), 1);
	T_EQ_INT(count_check.calls, 4);

	/* too long for the cache: checked every time */
	memset(big, 'a', sizeof(big));
	big[0] = '/';
	big[sizeof(big) - 1] = '\0';
	count_check.answer = 0;
	T_EQ_INT(CL_UpscaleCachedInGitTree(&cache, big, counted_check), 0);
	T_EQ_INT(CL_UpscaleCachedInGitTree(&cache, big, counted_check), 0);
	T_EQ_INT(count_check.calls, 6);
	/* and the directory kept is still the last one that fitted */
	T_EQ_INT(CL_UpscaleCachedInGitTree(&cache, "/g/a", counted_check), 1);
	T_EQ_INT(count_check.calls, 6);

	/* no directory: refused, nothing called */
	T_EQ_INT(CL_UpscaleCachedInGitTree(&cache, NULL, counted_check), 1);
	T_EQ_INT(count_check.calls, 6);
}

/* each output file's own directory: a nested repository, a link into one */
TEST(upscale_out_dir_in_git_tree)
{
	char path[MAX_OSPATH], target[MAX_OSPATH], home[MAX_OSPATH];
	upscale_gitcache_t cache;

	memset(&cache, 0, sizeof(cache));

	t_write_file("upscale/home/baseq2/textures/.git/HEAD", "x", 1);
	t_write_file("upscale/home/baseq2/textures/e1u1/.keep", "", 0);
	t_write_file("upscale/home/baseq2/env/.keep", "", 0);
	t_write_file("upscale/linked/.git", "gitdir: elsewhere\n", 18);
	t_write_file("upscale/linked/m/.keep", "", 0);
	t_write_file("upscale/home2/baseq2/.keep", "", 0);

	snprintf(home, sizeof(home), "%s/upscale/home/baseq2", t_tmpdir());
	T_EQ_INT(CL_UpscaleDirInGitTree(home), oracle_in_git(home));

	snprintf(path, sizeof(path), "%s/textures/e1u1/x.png", home);
	T_EQ_INT(CL_UpscalePathInGitTree(&cache, path), 1);
	snprintf(path, sizeof(path), "%s/textures/x.png", home);
	T_EQ_INT(CL_UpscalePathInGitTree(&cache, path), 1);
	snprintf(path, sizeof(path), "%s/env/x.png", home);
	T_EQ_INT(CL_UpscalePathInGitTree(&cache, path), oracle_in_git(home));

	/* models/ is a link into a repository */
	snprintf(target, sizeof(target), "%s/upscale/linked", t_tmpdir());
	snprintf(path, sizeof(path), "%s/upscale/home2/baseq2/models", t_tmpdir());
	T_REQUIRE(symlink(target, path) == 0);
	snprintf(home, sizeof(home), "%s/upscale/home2/baseq2", t_tmpdir());
	T_EQ_INT(CL_UpscaleDirInGitTree(home), oracle_in_git(home));
	snprintf(path, sizeof(path), "%s/models/m/skin.png", home);
	T_EQ_INT(CL_UpscalePathInGitTree(&cache, path), 1);
	snprintf(path, sizeof(path), "%s/models/skin.png", home);
	T_EQ_INT(CL_UpscalePathInGitTree(&cache, path), 1);

	/* no directory in the path, or one that does not exist: no real path */
	T_EQ_INT(CL_UpscalePathInGitTree(&cache, "x.png"), UPSCALE_GIT_UNKNOWN);
	snprintf(path, sizeof(path), "%s/missing/x.png", home);
	T_EQ_INT(CL_UpscalePathInGitTree(&cache, path), UPSCALE_GIT_UNKNOWN);
}

/* the three answers, and the boolean form that fails closed on two */
TEST(upscale_dir_git)
{
	char dir[MAX_OSPATH];
	int inside;

	t_write_file("upscale/dirgit/repo/.git", "x", 1);
	t_write_file("upscale/dirgit/plain/.keep", "", 0);

	snprintf(dir, sizeof(dir), "%s/upscale/dirgit/repo", t_tmpdir());
	T_EQ_INT(CL_UpscaleDirGit(dir), UPSCALE_GIT_INSIDE);
	T_EQ_INT(CL_UpscaleDirInGitTree(dir), 1);

	snprintf(dir, sizeof(dir), "%s/upscale/dirgit/plain", t_tmpdir());
	inside = oracle_in_git(dir);
	T_EQ_INT(CL_UpscaleDirGit(dir), inside ? UPSCALE_GIT_INSIDE :
		UPSCALE_GIT_OUTSIDE);
	T_EQ_INT(CL_UpscaleDirInGitTree(dir), inside);

	snprintf(dir, sizeof(dir), "%s/upscale/dirgit/missing", t_tmpdir());
	T_EQ_INT(CL_UpscaleDirGit(dir), UPSCALE_GIT_UNKNOWN);
	T_EQ_INT(CL_UpscaleDirInGitTree(dir), 1);
	T_EQ_INT(CL_UpscaleDirGit(NULL), UPSCALE_GIT_UNKNOWN);
	T_EQ_INT(CL_UpscaleDirInGitTree(NULL), 1);
}

TEST(upscale_dir_exists)
{
	char path[MAX_OSPATH];

	t_write_file("upscale/direx/sub/file.png", "x", 1);
	snprintf(path, sizeof(path), "%s/upscale/direx/sub/x.png", t_tmpdir());
	T_EQ_INT(CL_UpscaleDirExists(path), 1);
	snprintf(path, sizeof(path), "%s/upscale/direx/none/x.png", t_tmpdir());
	T_EQ_INT(CL_UpscaleDirExists(path), 0);
	/* a file where the directory would be */
	snprintf(path, sizeof(path), "%s/upscale/direx/sub/file.png/x.png", t_tmpdir());
	T_EQ_INT(CL_UpscaleDirExists(path), 0);
	T_EQ_INT(CL_UpscaleDirExists("x.png"), 0);
	T_EQ_INT(CL_UpscaleDirExists(NULL), 0);
}

TEST(upscale_list_dir)
{
	strlist_t dirs;
	int cut = 0, refused = 0, i;

	StrList_Init(&dirs, 0);
	T_EQ_INT(CL_UpscaleListDir(&dirs, "textures/e1u1", &cut, &refused), 1);
	T_EQ_INT(dirs.num, 1);
	/* refused names are counted, not walked, and the walk goes on */
	T_EQ_INT(CL_UpscaleListDir(&dirs, "textures/bad dir", &cut, &refused), 1);
	T_EQ_INT(CL_UpscaleListDir(&dirs, "textures/.hidden", &cut, &refused), 1);
	T_EQ_INT(CL_UpscaleListDir(&dirs, NULL, &cut, &refused), 1);
	T_EQ_INT(dirs.num, 1);
	T_EQ_INT(refused, 3);
	T_EQ_INT(cut, 0);
	/* a name already there is not walked twice */
	T_EQ_INT(CL_UpscaleListDir(&dirs, "textures/e1u1", &cut, &refused), 1);
	T_EQ_INT(dirs.num, 1);

	/* full: the walk stops at UPSCALE_MAX_DIRS */
	for (i = 1; i < UPSCALE_MAX_DIRS; i++)
	{
		char name[32];

		snprintf(name, sizeof(name), "textures/d%d", i);
		T_REQUIRE(CL_UpscaleListDir(&dirs, name, &cut, &refused) == 1);
	}

	T_EQ_INT(dirs.num, UPSCALE_MAX_DIRS);
	T_EQ_INT(cut, 0);
	T_EQ_INT(CL_UpscaleListDir(&dirs, "textures/one_more", &cut, &refused), 0);
	T_EQ_INT(cut, 1);
	T_EQ_INT(dirs.num, UPSCALE_MAX_DIRS);
	T_EQ_INT(refused, 3);
	StrList_Free(&dirs);
}

TEST(upscale_allowed)
{
	T_EQ_INT(CL_UpscaleAllowed(0, 0, 0), 1);
	T_EQ_INT(CL_UpscaleAllowed(0, 1, 0), 1);
	T_EQ_INT(CL_UpscaleAllowed(1, 1, 0), 1);
	T_EQ_INT(CL_UpscaleAllowed(1, 0, 0), 0);
	/* a demo plays from the local server: refused however it connects */
	T_EQ_INT(CL_UpscaleAllowed(1, 1, 1), 0);
	T_EQ_INT(CL_UpscaleAllowed(0, 0, 1), 0);
	T_EQ_INT(CL_UpscaleAllowed(0, 1, 1), 0);
	T_EQ_INT(CL_UpscaleAllowed(1, 0, 1), 0);
}

/* ---- the file write, on disk ---- */

/* the bytes of the file at <t_tmpdir()>/rel into buf, -1 when there is none */
static long
read_back(const char *rel, char *buf, size_t size)
{
	char path[MAX_OSPATH];
	FILE *f;
	size_t n;

	snprintf(path, sizeof(path), "%s/%s", t_tmpdir(), rel);
	f = fopen(path, "rb");

	if (!f)
	{
		return -1;
	}

	n = fread(buf, 1, size, f);
	fclose(f);

	return (long)n;
}

/* 1 when an entry named <path>.<this pid>.tmp is there, a link included */
static int
temp_left(const char *path)
{
	char tmp[MAX_OSPATH];
	struct stat st;

	T_REQUIRE(CL_UpscaleTempPath(path, (long)getpid(), tmp, sizeof(tmp)));

	return lstat(tmp, &st) == 0;
}

TEST(upscale_write_file_new_and_replaced)
{
	char path[MAX_OSPATH], buf[64];

	t_write_file("upscale/write/new/.keep", "", 0);
	snprintf(path, sizeof(path), "%s/upscale/write/new/x.png", t_tmpdir());

	/* (a) a new file, and no temporary file left */
	T_EQ_INT(CL_UpscaleWriteFile(path, (const unsigned char *)"first", 5), 1);
	T_EQ_INT(read_back("upscale/write/new/x.png", buf, sizeof(buf)), 5);
	T_EQ_MEM(buf, "first", 5);
	T_EQ_INT(temp_left(path), 0);

	/* (b) a regular file there is replaced, a longer one cut to the new */
	t_write_file("upscale/write/new/y.png", "an older and longer file", 24);
	snprintf(path, sizeof(path), "%s/upscale/write/new/y.png", t_tmpdir());
	T_EQ_INT(CL_UpscaleWriteFile(path, (const unsigned char *)"second", 6), 1);
	T_EQ_INT(read_back("upscale/write/new/y.png", buf, sizeof(buf)), 6);
	T_EQ_MEM(buf, "second", 6);
	T_EQ_INT(temp_left(path), 0);

	/* nothing at all is a file too */
	T_EQ_INT(CL_UpscaleWriteFile(path, (const unsigned char *)"", 0), 1);
	T_EQ_INT(read_back("upscale/write/new/y.png", buf, sizeof(buf)), 0);

	/* no directory, or no name: nothing */
	snprintf(path, sizeof(path), "%s/upscale/write/none/x.png", t_tmpdir());
	T_EQ_INT(CL_UpscaleWriteFile(path, (const unsigned char *)"x", 1), 0);
	T_EQ_INT(temp_left(path), 0);
	T_EQ_INT(CL_UpscaleWriteFile(NULL, (const unsigned char *)"x", 1), 0);
}

TEST(upscale_write_file_never_through_a_link)
{
	char path[MAX_OSPATH], tmp[MAX_OSPATH], target[MAX_OSPATH], buf[64];
	struct stat st;

	t_write_file("upscale/write/link/target.png", "target", 6);
	t_write_file("upscale/write/link/x.png", "old", 3);
	snprintf(target, sizeof(target), "%s/upscale/write/link/target.png", t_tmpdir());
	snprintf(path, sizeof(path), "%s/upscale/write/link/x.png", t_tmpdir());
	T_REQUIRE(CL_UpscaleTempPath(path, (long)getpid(), tmp, sizeof(tmp)));

	/* (c) a link at the temporary name: not followed, path as it was */
	T_REQUIRE(symlink(target, tmp) == 0);
	T_EQ_INT(CL_UpscaleWriteFile(path, (const unsigned char *)"new", 3), 0);
	T_EQ_INT(read_back("upscale/write/link/target.png", buf, sizeof(buf)), 6);
	T_EQ_MEM(buf, "target", 6);
	T_EQ_INT(read_back("upscale/write/link/x.png", buf, sizeof(buf)), 3);
	T_EQ_MEM(buf, "old", 3);
	/* the link is not this process's to remove */
	T_REQUIRE(lstat(tmp, &st) == 0);
	T_CHECK(S_ISLNK(st.st_mode));
	T_REQUIRE(unlink(tmp) == 0);

	/* a regular file at the temporary name is not taken over either */
	T_REQUIRE(CL_UpscaleTempPath("upscale/write/link/x.png", (long)getpid(),
		tmp, sizeof(tmp)));
	t_write_file(tmp, "stale", 5);
	T_EQ_INT(CL_UpscaleWriteFile(path, (const unsigned char *)"new", 3), 0);
	T_EQ_INT(read_back(tmp, buf, sizeof(buf)), 5);
	T_EQ_MEM(buf, "stale", 5);
	T_EQ_INT(read_back("upscale/write/link/x.png", buf, sizeof(buf)), 3);
	T_EQ_MEM(buf, "old", 3);
	T_REQUIRE(CL_UpscaleTempPath(path, (long)getpid(), tmp, sizeof(tmp)));
	T_REQUIRE(unlink(tmp) == 0);

	/* a link at path itself is replaced by the file, its target untouched */
	T_REQUIRE(unlink(path) == 0);
	T_REQUIRE(symlink(target, path) == 0);
	T_EQ_INT(CL_UpscaleWriteFile(path, (const unsigned char *)"new", 3), 1);
	T_REQUIRE(lstat(path, &st) == 0);
	T_CHECK(S_ISREG(st.st_mode));
	T_EQ_INT(read_back("upscale/write/link/x.png", buf, sizeof(buf)), 3);
	T_EQ_MEM(buf, "new", 3);
	T_EQ_INT(read_back("upscale/write/link/target.png", buf, sizeof(buf)), 6);
	T_EQ_MEM(buf, "target", 6);
	T_EQ_INT(temp_left(path), 0);
}

TEST(upscale_write_file_fails_clean)
{
	char path[MAX_OSPATH], buf[64];
	unsigned char big[4096];
	struct rlimit old, low;
	void (*xfsz)(int);
	struct stat st;
	int ok;

	/* (d) a directory at path: the rename fails, no temporary file left */
	t_write_file("upscale/write/dir/x.png/inside", "in", 2);
	snprintf(path, sizeof(path), "%s/upscale/write/dir/x.png", t_tmpdir());
	T_EQ_INT(CL_UpscaleWriteFile(path, (const unsigned char *)"new", 3), 0);
	T_EQ_INT(temp_left(path), 0);
	T_REQUIRE(lstat(path, &st) == 0);
	T_CHECK(S_ISDIR(st.st_mode));
	T_EQ_INT(read_back("upscale/write/dir/x.png/inside", buf, sizeof(buf)), 2);

	/* a short write (the file size limit): path as it was, no temporary */
	t_write_file("upscale/write/dir/y.png", "old", 3);
	snprintf(path, sizeof(path), "%s/upscale/write/dir/y.png", t_tmpdir());
	memset(big, 'b', sizeof(big));
	T_REQUIRE(getrlimit(RLIMIT_FSIZE, &old) == 0);
	low = old;
	low.rlim_cur = 1024;
	xfsz = signal(SIGXFSZ, SIG_IGN);
	T_REQUIRE(setrlimit(RLIMIT_FSIZE, &low) == 0);
	ok = CL_UpscaleWriteFile(path, big, sizeof(big));
	T_REQUIRE(setrlimit(RLIMIT_FSIZE, &old) == 0);
	signal(SIGXFSZ, xfsz);
	T_EQ_INT(ok, 0);
	T_EQ_INT(temp_left(path), 0);
	T_EQ_INT(read_back("upscale/write/dir/y.png", buf, sizeof(buf)), 3);
	T_EQ_MEM(buf, "old", 3);
}

/* ---- the order of one file's steps, with stubs ---- */

typedef struct
{
	upscale_entry_t entry;
	int dirs;	/* makedirs' answer */
	int git;
	upscale_outcome_t made;
	int nodata;	/* make says written but gives no PNG */
	int wrote;	/* write's answer */
	char log[128];
	char said[160];
	int developer;
} stub_steps_t;

static void
stub_log(stub_steps_t *s, const char *step)
{
	size_t len = strlen(s->log);

	snprintf(s->log + len, sizeof(s->log) - len, "%s%s", len ? " " : "", step);
}

static upscale_entry_t
stub_entry(void *ctx, const char *path)
{
	(void)path;
	stub_log(ctx, "entry");
	return ((stub_steps_t *)ctx)->entry;
}

static int
stub_makedirs(void *ctx, const char *path)
{
	(void)path;
	stub_log(ctx, "dirs");
	return ((stub_steps_t *)ctx)->dirs;
}

static int
stub_git(void *ctx, const char *path)
{
	(void)path;
	stub_log(ctx, "git");
	return ((stub_steps_t *)ctx)->git;
}

static upscale_outcome_t
stub_make(void *ctx, unsigned char **data, size_t *len)
{
	stub_steps_t *s = ctx;

	stub_log(s, "make");

	if ((s->made == UPSCALE_WRITTEN) && !s->nodata)
	{
		*data = malloc(3);
		T_REQUIRE(*data != NULL);
		memcpy(*data, "png", 3);
		*len = 3;
	}

	return s->made;
}

static int
stub_write(void *ctx, const char *path, const unsigned char *data, size_t len)
{
	(void)path;
	T_REQUIRE(data != NULL);
	T_EQ_INT((int)len, 3);
	T_EQ_MEM(data, "png", 3);
	stub_log(ctx, "write");
	return ((stub_steps_t *)ctx)->wrote;
}

static void
stub_say(void *ctx, int developer, const char *path, const char *text)
{
	stub_steps_t *s = ctx;

	T_EQ_STR(path, "/g/baseq2/textures/x.png");
	T_EQ_STR(s->said, "");
	snprintf(s->said, sizeof(s->said), "%s", text);
	s->developer = developer;
}

static const upscale_steps_t stub_steps = {
	stub_entry, stub_makedirs, stub_git, stub_make, stub_write, stub_say
};

/* every step answers yes but the one a case changes */
static stub_steps_t
stub_yes(void)
{
	stub_steps_t s;

	memset(&s, 0, sizeof(s));
	s.entry = UPSCALE_ENTRY_NONE;
	s.dirs = 1;
	s.git = UPSCALE_GIT_OUTSIDE;
	s.made = UPSCALE_WRITTEN;
	s.wrote = 1;

	return s;
}

/* one case: the outcome, the steps run in order, and the message */
static void
write_out_case(stub_steps_t s, int force, upscale_outcome_t outcome,
		const char *log, const char *said, int developer)
{
	size_t bytes = 99;

	T_EQ_INT(CL_UpscaleWriteOut("/g/baseq2/textures/x.png", force,
		&stub_steps, &s, &bytes), outcome);
	T_EQ_STR(s.log, log);
	T_EQ_STR(s.said, said);
	T_EQ_INT(s.developer, developer);
	T_CHECK(bytes == ((outcome == UPSCALE_WRITTEN) ? 3 : 0));
}

TEST(upscale_write_out_order)
{
	stub_steps_t s;
	const char *all = "entry dirs git make write";

	write_out_case(stub_yes(), 0, UPSCALE_WRITTEN, all, "", 0);

	s = stub_yes();
	s.entry = UPSCALE_ENTRY_FILE;
	write_out_case(s, 0, UPSCALE_SKIPPED, "entry", "", 0);
	write_out_case(s, 1, UPSCALE_WRITTEN, all, "", 0);

	s.entry = UPSCALE_ENTRY_OTHER;
	write_out_case(s, 0, UPSCALE_SKIPPED, "entry", "", 0);
	write_out_case(s, 1, UPSCALE_REFUSED, "entry", UPSCALE_SAY_NOT_FILE, 0);

	s.entry = UPSCALE_ENTRY_UNKNOWN;
	write_out_case(s, 0, UPSCALE_REFUSED, "entry", UPSCALE_SAY_UNKNOWN, 0);
	write_out_case(s, 1, UPSCALE_REFUSED, "entry", UPSCALE_SAY_UNKNOWN, 0);

	/* no directory: failed, nothing read or written */
	s = stub_yes();
	s.dirs = 0;
	write_out_case(s, 0, UPSCALE_FAILED, "entry dirs", UPSCALE_SAY_NO_DIR, 0);

	/* the git check comes after the directories and before anything is made */
	s = stub_yes();
	s.git = UPSCALE_GIT_INSIDE;
	write_out_case(s, 0, UPSCALE_REFUSED, "entry dirs git", UPSCALE_SAY_GIT, 1);
	s.git = UPSCALE_GIT_UNKNOWN;
	write_out_case(s, 0, UPSCALE_FAILED, "entry dirs git", UPSCALE_SAY_NO_REALPATH, 0);
	s.git = 7;
	write_out_case(s, 0, UPSCALE_FAILED, "entry dirs git", UPSCALE_SAY_NO_REALPATH, 0);

	/* make's own answer; it says its own message */
	s = stub_yes();
	s.made = UPSCALE_REFUSED;
	write_out_case(s, 0, UPSCALE_REFUSED, "entry dirs git make", "", 0);
	s.made = UPSCALE_FAILED;
	write_out_case(s, 0, UPSCALE_FAILED, "entry dirs git make", "", 0);
	s.made = UPSCALE_SKIPPED;
	write_out_case(s, 0, UPSCALE_FAILED, "entry dirs git make", "", 0);
	s.made = UPSCALE_WRITTEN;
	s.nodata = 1;
	write_out_case(s, 0, UPSCALE_FAILED, "entry dirs git make", "", 0);

	s = stub_yes();
	s.wrote = 0;
	write_out_case(s, 0, UPSCALE_FAILED, all, UPSCALE_SAY_WRITE, 0);
}

TEST(upscale_count)
{
	upscale_count_t n;

	memset(&n, 0, sizeof(n));
	CL_UpscaleCount(&n, UPSCALE_WRITTEN, 100);
	CL_UpscaleCount(&n, UPSCALE_WRITTEN, 23);
	CL_UpscaleCount(&n, UPSCALE_SKIPPED, 1000);
	CL_UpscaleCount(&n, UPSCALE_REFUSED, 1000);
	CL_UpscaleCount(&n, UPSCALE_REFUSED, 1000);
	CL_UpscaleCount(&n, UPSCALE_FAILED, 1000);
	CL_UpscaleCount(&n, UPSCALE_FAILED, 1000);
	CL_UpscaleCount(&n, UPSCALE_FAILED, 1000);
	T_EQ_INT(n.written, 2);
	T_EQ_INT(n.skipped, 1);
	T_EQ_INT(n.refused, 2);
	T_EQ_INT(n.failed, 3);
	T_CHECK(n.bytes == 123);
	T_EQ_INT(n.cut, 0);
	/* an outcome that is none of these is a failure */
	CL_UpscaleCount(&n, (upscale_outcome_t)99, 1000);
	T_EQ_INT(n.failed, 4);
	T_CHECK(n.bytes == 123);
}

TEST(upscale_summary)
{
	upscale_count_t n;
	char out[256];

	memset(&n, 0, sizeof(n));
	n.written = 3;
	n.skipped = 1;
	n.refused = 2;
	n.failed = 4;
	n.bytes = 5000000000ULL;
	CL_UpscaleSummary(&n, 2, UPSCALE_SCALE2X, "/g/baseq2", out, sizeof(out));
	T_EQ_STR(out, "upscale_textures: 3 written, 1 skipped, 2 refused, 4 failed, "
		"5000000000 bytes, 2x, in /g/baseq2\n");

	n.cut = 1;
	CL_UpscaleSummary(&n, 4, UPSCALE_SCALE2X, "/g/baseq2", out, sizeof(out));
	T_EQ_STR(out, "upscale_textures: 3 written, 1 skipped, 2 refused, 4 failed, "
		"5000000000 bytes, 4x, in /g/baseq2; a listing was cut at its limit, "
		"not every file was looked at\n");

	/* loose directories whose names are refused: their files not looked at */
	n.cut = 0;
	n.baddirs = 1;
	CL_UpscaleSummary(&n, 2, UPSCALE_SCALE2X, "/g/baseq2", out, sizeof(out));
	T_EQ_STR(out, "upscale_textures: 3 written, 1 skipped, 2 refused, 4 failed, "
		"5000000000 bytes, 2x, in /g/baseq2; 1 directory with a refused name "
		"not looked at\n");
	n.cut = 1;
	n.baddirs = 2147483647;
	CL_UpscaleSummary(&n, 2, UPSCALE_SCALE2X, "/g/baseq2", out, sizeof(out));
	T_EQ_STR(out, "upscale_textures: 3 written, 1 skipped, 2 refused, 4 failed, "
		"5000000000 bytes, 2x, in /g/baseq2; 2147483647 directories with a "
		"refused name not looked at; a listing was cut at its limit, not "
		"every file was looked at\n");
}

/* ---- the backends (TASK124) ---- */

TEST(upscale_backend_name)
{
	T_EQ_STR(CL_UpscaleBackendName(UPSCALE_SCALE2X), "scale2x");
	T_EQ_STR(CL_UpscaleBackendName(UPSCALE_METALFX), "metalfx");
	T_CHECK(CL_UpscaleBackendName(2) == NULL);
	T_CHECK(CL_UpscaleBackendName(-1) == NULL);
}

/* CL_UpscaleParseArgs of one line: 1 with what it set, else 0 */
static int
upscale_parse(const char *line, int *factor, upscale_backend_t *backend,
		int *force)
{
	const char *argv[8];
	char buf[128];
	char *p;
	int argc = 0;

	Q_strlcpy(buf, line, sizeof(buf));

	for (p = strtok(buf, " "); p && (argc < 8); p = strtok(NULL, " "))
	{
		argv[argc++] = p;
	}

	*factor = -7;
	*backend = (upscale_backend_t)-7;
	*force = -7;

	return CL_UpscaleParseArgs(argc, argv, factor, backend, force);
}

TEST(upscale_parse_args_accepts)
{
	static const struct
	{
		const char *line;
		int factor, backend, force;
	} ok[] = {
		/* the first card's lines mean what they meant */
		{ "upscale_textures 2", 2, UPSCALE_SCALE2X, 0 },
		{ "upscale_textures 4", 4, UPSCALE_SCALE2X, 0 },
		{ "upscale_textures 2 force", 2, UPSCALE_SCALE2X, 1 },
		{ "upscale_textures 4 force", 4, UPSCALE_SCALE2X, 1 },
		/* the backend argument */
		{ "upscale_textures 2 scale2x", 2, UPSCALE_SCALE2X, 0 },
		{ "upscale_textures 4 scale2x force", 4, UPSCALE_SCALE2X, 1 },
		{ "upscale_textures 2 metalfx", 2, UPSCALE_METALFX, 0 },
		{ "upscale_textures 4 metalfx", 4, UPSCALE_METALFX, 0 },
		{ "upscale_textures 4 metalfx force", 4, UPSCALE_METALFX, 1 },
	};
	size_t i;

	for (i = 0; i < sizeof(ok) / sizeof(ok[0]); i++)
	{
		int factor, force;
		upscale_backend_t backend;

		T_EQ_INT(upscale_parse(ok[i].line, &factor, &backend, &force), 1);
		T_EQ_INT(factor, ok[i].factor);
		T_EQ_INT((int)backend, ok[i].backend);
		T_EQ_INT(force, ok[i].force);
	}
}

TEST(upscale_parse_args_refuses)
{
	static const char *bad[] = {
		"upscale_textures",
		"upscale_textures 3",
		"upscale_textures 8 metalfx",
		"upscale_textures metalfx",
		"upscale_textures metalfx 2",
		/* a bad backend name */
		"upscale_textures 2 vt",
		"upscale_textures 2 MetalFX",
		"upscale_textures 2 metalfx2",
		"upscale_textures 2 scale",
		"upscale_textures 4 bogus force",
		/* the order is <2|4> [backend] [force] */
		"upscale_textures 2 force metalfx",
		"upscale_textures 2 metalfx metalfx",
		"upscale_textures 2 scale2x metalfx",
		"upscale_textures 2 force force",
		"upscale_textures 2 metalfx force extra",
		"upscale_textures 2 metalfx forced",
	};
	size_t i;

	for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
	{
		int factor, force;
		upscale_backend_t backend;

		T_EQ_INT(upscale_parse(bad[i], &factor, &backend, &force), 0);
		/* nothing set */
		T_EQ_INT(factor, -7);
		T_EQ_INT(force, -7);
		T_EQ_INT((int)backend, -7);
	}

	T_EQ_INT(CL_UpscaleParseArgs(0, NULL, NULL, NULL, NULL), 0);
	T_EQ_STR(UPSCALE_USAGE,
		"usage: upscale_textures <2|4> [scale2x|metalfx] [force]\n");
}

TEST(upscale_choose_backend)
{
	char line[256];

	/* scale2x is always there and says nothing */
	strcpy(line, "x");
	T_EQ_INT(CL_UpscaleChooseBackend(UPSCALE_SCALE2X, 0, "no device", line,
		sizeof(line)), UPSCALE_SCALE2X);
	T_EQ_STR(line, "");

	/* a supported metalfx runs, and nothing is said */
	strcpy(line, "x");
	T_EQ_INT(CL_UpscaleChooseBackend(UPSCALE_METALFX, 1, NULL, line,
		sizeof(line)), UPSCALE_METALFX);
	T_EQ_STR(line, "");

	/* an unsupported one falls back to scale2x with one line */
	T_EQ_INT(CL_UpscaleChooseBackend(UPSCALE_METALFX, 0, "no Metal device",
		line, sizeof(line)), UPSCALE_SCALE2X);
	T_EQ_STR(line, "upscale_textures: metalfx is unsupported here (no Metal "
		"device); using scale2x\n");
	T_EQ_INT(CL_UpscaleChooseBackend(UPSCALE_METALFX, 0, "", line,
		sizeof(line)), UPSCALE_SCALE2X);
	T_EQ_STR(line, "upscale_textures: metalfx is unsupported here; using "
		"scale2x\n");
	T_EQ_INT(CL_UpscaleChooseBackend(UPSCALE_METALFX, 0, NULL, line,
		sizeof(line)), UPSCALE_SCALE2X);
	T_EQ_STR(line, "upscale_textures: metalfx is unsupported here; using "
		"scale2x\n");

	/* a value that is no backend is scale2x, said as unsupported */
	T_EQ_INT(CL_UpscaleChooseBackend((upscale_backend_t)9, 1, NULL, line,
		sizeof(line)), UPSCALE_SCALE2X);
	T_EQ_STR(line, "upscale_textures: backend 9 is unsupported here; using "
		"scale2x\n");
}

/* one RGBA8 texel: r = tag, g = x, b = y, a = 200 */
static void
upscale_rgba_texel(unsigned char *t, int tag, int x, int y)
{
	t[0] = (unsigned char)tag;
	t[1] = (unsigned char)x;
	t[2] = (unsigned char)y;
	t[3] = 200;
}

TEST(upscale_rgba_pad_and_crop)
{
	unsigned char src[3 * 2 * 4], pad[5 * 4 * 4], back[3 * 2 * 4];
	int x, y;

	for (y = 0; y < 2; y++)
	{
		for (x = 0; x < 3; x++)
		{
			upscale_rgba_texel(src + (y * 3 + x) * 4, 7, x, y);
		}
	}

	T_EQ_INT(CL_UpscaleWrapPadRGBA(src, 0, 2, pad), 0);
	T_EQ_INT(CL_UpscaleWrapPadRGBA(src, 3, 2, pad), 1);

	/* padded (x, y) is source ((x - 1) mod 3, (y - 1) mod 2), all 4 bytes */
	for (y = 0; y < 4; y++)
	{
		for (x = 0; x < 5; x++)
		{
			const unsigned char *t = pad + (y * 5 + x) * 4;

			T_EQ_INT(t[0], 7);
			T_EQ_INT(t[1], (x + 2) % 3);
			T_EQ_INT(t[2], (y + 1) % 2);
			T_EQ_INT(t[3], 200);
		}
	}

	/* the crop takes whole texels back out */
	T_EQ_INT(CL_UpscaleCropRGBA(pad, 5, 4, 1, 1, 3, 2, back), 1);
	T_EQ_MEM(back, src, sizeof(src));
	T_EQ_INT(CL_UpscaleCropRGBA(pad, 5, 4, 3, 1, 3, 2, back), 0);
	T_EQ_INT(CL_UpscaleCropRGBA(pad, 5, 4, 0, 3, 3, 2, back), 0);
	T_EQ_INT(CL_UpscaleCropRGBA(pad, 5, 4, -1, 0, 3, 2, back), 0);
	T_EQ_INT(CL_UpscaleCropRGBA(pad, 5, 4, 0, 0, 0, 2, back), 0);

	/* the 8-bit forms are unchanged */
	{
		const unsigned char i8[4] = { 1, 2, 3, 4 };
		unsigned char p8[16], c8[4];

		T_EQ_INT(CL_UpscaleWrapPad(i8, 2, 2, p8), 1);
		T_EQ_INT(p8[0], 4);
		T_EQ_INT(p8[5], 1);
		T_EQ_INT(p8[15], 1);
		T_EQ_INT(CL_UpscaleCrop(p8, 4, 4, 1, 1, 2, 2, c8), 1);
		T_EQ_MEM(c8, i8, 4);
	}
}

TEST(upscale_mask_alpha)
{
	const unsigned char idx[2 * 2] = { 255, 3, 4, 255 };
	unsigned char rgba[4 * 4 * 4];
	int x, y;

	memset(rgba, 77, sizeof(rgba));
	CL_UpscaleMaskAlpha(idx, 2, 2, 2, rgba);

	for (y = 0; y < 4; y++)
	{
		for (x = 0; x < 4; x++)
		{
			const unsigned char *t = rgba + (y * 4 + x) * 4;

			T_EQ_INT(t[3], (idx[(y / 2) * 2 + x / 2] == 255) ? 0 : 255);
			/* the colour is not touched */
			T_EQ_INT(t[0], 77);
			T_EQ_INT(t[1], 77);
			T_EQ_INT(t[2], 77);
		}
	}
}

/* a stand-in for MetalFX: nearest neighbour, alpha 255 everywhere */
typedef struct
{
	int calls, iw, ih, ow, oh, fail;
	unsigned char in[16 * 16 * 4];
} upscale_fake_scaler_t;

static int
upscale_fake_scale(void *ctx, const unsigned char *in, int iw, int ih,
		unsigned char *out, int ow, int oh)
{
	upscale_fake_scaler_t *f = ctx;
	int x, y;

	f->calls++;
	f->iw = iw;
	f->ih = ih;
	f->ow = ow;
	f->oh = oh;

	if (f->fail || ((size_t)iw * ih * 4 > sizeof(f->in)))
	{
		return 0;
	}

	memcpy(f->in, in, (size_t)iw * ih * 4);

	for (y = 0; y < oh; y++)
	{
		for (x = 0; x < ow; x++)
		{
			const unsigned char *s = in + ((y * ih / oh) * iw + x * iw / ow) * 4;
			unsigned char *d = out + ((size_t)y * ow + x) * 4;

			d[0] = s[0];
			d[1] = s[1];
			d[2] = s[2];
			d[3] = 255;
		}
	}

	return 1;
}

/* a palette where index i is (i, 255 - i, i / 2) */
static void
upscale_test_table(unsigned *table)
{
	unsigned char pal[768];
	int i;

	for (i = 0; i < 256; i++)
	{
		pal[i * 3 + 0] = (unsigned char)i;
		pal[i * 3 + 1] = (unsigned char)(255 - i);
		pal[i * 3 + 2] = (unsigned char)(i / 2);
	}

	CL_UpscaleTable(pal, table);
}

/*
 * The metalfx path's tile on the stand-in: the scaler sees the RGBA of
 * the indices, wrap-padded for a world texture; the output is cropped
 * back to factor times the source, and its alpha is the source's
 * transparent index, though the scaler made it 255 everywhere.
 */
static void
upscale_rgba_case(int factor, int wrap)
{
	/* 4 by 3, with the transparent index at (1, 0) and (3, 2) */
	const unsigned char idx[4 * 3] = {
		10, 255, 12, 13,
		20, 21, 22, 23,
		30, 31, 32, 255
	};
	const int w = 4, h = 3, border = wrap ? UPSCALE_METALFX_BORDER : 0;
	upscale_fake_scaler_t fake;
	unsigned table[256];
	unsigned char *rgba, *want, *out;
	int x, y;

	upscale_test_table(table);
	memset(&fake, 0, sizeof(fake));

	rgba = CL_UpscaleToRGBA(idx, w, h, table);
	T_REQUIRE(rgba != NULL);
	out = CL_UpscaleRGBA(idx, w, h, factor, wrap, table, upscale_fake_scale,
		&fake);
	T_REQUIRE(out != NULL);

	/* one pass, at factor times the (padded) input */
	T_EQ_INT(fake.calls, 1);
	T_EQ_INT(fake.iw, w + 2 * border);
	T_EQ_INT(fake.ih, h + 2 * border);
	T_EQ_INT(fake.ow, factor * (w + 2 * border));
	T_EQ_INT(fake.oh, factor * (h + 2 * border));

	/* the input: the RGBA of the indices, wrap-padded or as it is */
	want = malloc((size_t)(w + 2 * border) * (h + 2 * border) * 4);
	T_REQUIRE(want != NULL);

	if (wrap)
	{
		T_EQ_INT(CL_UpscaleWrapPadRGBABorder(rgba, w, h, border, want), 1);
	}
	else
	{
		memcpy(want, rgba, (size_t)w * h * 4);
	}

	T_EQ_MEM(fake.in, want, (size_t)fake.iw * fake.ih * 4);
	free(want);

	/* the output: cropped, so texel (x, y) is source (x / f, y / f) */
	for (y = 0; y < h * factor; y++)
	{
		for (x = 0; x < w * factor; x++)
		{
			const unsigned char *t = out + ((size_t)y * w * factor + x) * 4;
			const unsigned char *s = rgba + ((y / factor) * w + x / factor) * 4;

			T_EQ_INT(t[0], s[0]);
			T_EQ_INT(t[1], s[1]);
			T_EQ_INT(t[2], s[2]);
			/* the transparent index, not the scaler's 255 */
			T_EQ_INT(t[3], (idx[(y / factor) * w + x / factor] == 255) ? 0 : 255);
		}
	}

	/* the transparent texel took a neighbour's colour (R_Convert8to32) */
	T_CHECK(rgba[1 * 4 + 3] == 0);
	T_CHECK(rgba[1 * 4 + 0] != 255);

	free(out);
	free(rgba);
}

TEST(upscale_rgba_wrapped_tile)
{
	upscale_rgba_case(2, 1);
	upscale_rgba_case(4, 1);
}

/*
 * A stand-in for a filter that reads neighbours: nearest neighbour of a
 * 5 by 5 box blur (two texels each way), clamped at the edges of what it
 * is given, as MetalFX's spatial filter is at the edges of its input.
 */
static int
upscale_blur_scale(void *ctx, const unsigned char *in, int iw, int ih,
		unsigned char *out, int ow, int oh)
{
	int x, y, dx, dy, c;

	(void)ctx;

	for (y = 0; y < oh; y++)
	{
		for (x = 0; x < ow; x++)
		{
			int sx = x * iw / ow, sy = y * ih / oh;

			for (c = 0; c < 4; c++)
			{
				int sum = 0;

				for (dy = -2; dy <= 2; dy++)
				{
					for (dx = -2; dx <= 2; dx++)
					{
						int px = sx + dx, py = sy + dy;

						px = (px < 0) ? 0 : (px >= iw) ? iw - 1 : px;
						py = (py < 0) ? 0 : (py >= ih) ? ih - 1 : py;
						sum += in[((size_t)py * iw + px) * 4 + c];
					}
				}

				out[((size_t)y * ow + x) * 4 + c] = (unsigned char)(sum / 25);
			}
		}
	}

	return 1;
}

/*
 * A scaler that reads two texels each way must see a tile as it repeats:
 * the cropped result equals the centre tile of the same scale applied to
 * a 3 by 3 tiling of the source. A one texel border does not give that.
 */
TEST(upscale_rgba_wrapped_tile_wide_filter)
{
	const int w = 4, h = 3;
	unsigned char idx[4 * 3];
	unsigned table[256];
	int factor, i, x, y, tx, ty;

	for (i = 0; i < w * h; i++)
	{
		idx[i] = (unsigned char)(i * 21 + 5);
	}

	upscale_test_table(table);

	for (factor = 2; factor <= 4; factor += 2)
	{
		unsigned char *rgba = CL_UpscaleToRGBA(idx, w, h, table);
		unsigned char *tiled = malloc((size_t)9 * w * h * 4);
		unsigned char *big = malloc((size_t)9 * w * h * factor * factor * 4);
		unsigned char *out;

		T_REQUIRE(rgba && tiled && big);

		for (y = 0; y < 3 * h; y++)
		{
			for (x = 0; x < 3 * w; x++)
			{
				memcpy(tiled + ((size_t)y * 3 * w + x) * 4,
					rgba + (size_t)((y % h) * w + (x % w)) * 4, 4);
			}
		}

		T_EQ_INT(upscale_blur_scale(NULL, tiled, 3 * w, 3 * h, big,
			3 * w * factor, 3 * h * factor), 1);
		out = CL_UpscaleRGBA(idx, w, h, factor, 1, table, upscale_blur_scale,
			NULL);
		T_REQUIRE(out != NULL);

		for (y = 0; y < h * factor; y++)
		{
			for (ty = y + h * factor, x = 0; x < w * factor; x++)
			{
				const unsigned char *a = out + ((size_t)y * w * factor + x) * 4;
				const unsigned char *b = big +
					((size_t)ty * 3 * w * factor + x + w * factor) * 4;

				for (tx = 0; tx < 3; tx++)
				{
					T_EQ_INT(a[tx], b[tx]);
				}
			}
		}

		free(out);
		free(big);
		free(tiled);
		free(rgba);
	}
}

TEST(upscale_wrap_pad_border)
{
	const unsigned char src[3 * 2 * 4] = {
		1, 0, 0, 9, 2, 0, 0, 9, 3, 0, 0, 9,
		4, 0, 0, 9, 5, 0, 0, 9, 6, 0, 0, 9
	};
	unsigned char pad[(3 + 8) * (2 + 8) * 4];
	int x, y;

	/* a border larger than the tile wraps around more than once */
	T_EQ_INT(CL_UpscaleWrapPadRGBABorder(src, 3, 2, 4, pad), 1);

	for (y = 0; y < 10; y++)
	{
		for (x = 0; x < 11; x++)
		{
			int sx = ((x - 4) % 3 + 3) % 3, sy = ((y - 4) % 2 + 2) % 2;

			T_EQ_INT(pad[(y * 11 + x) * 4], src[(sy * 3 + sx) * 4]);
		}
	}

	T_EQ_INT(CL_UpscaleWrapPadRGBABorder(src, 0, 2, 4, pad), 0);
	T_EQ_INT(CL_UpscaleWrapPadRGBABorder(src, 3, 2, -1, pad), 0);
}

TEST(upscale_rgba_skin_not_padded)
{
	upscale_rgba_case(2, 0);
	upscale_rgba_case(4, 0);
}

TEST(upscale_rgba_refuses)
{
	const unsigned char idx[4] = { 1, 2, 3, 255 };
	upscale_fake_scaler_t fake;
	unsigned table[256];

	upscale_test_table(table);
	memset(&fake, 0, sizeof(fake));

	T_CHECK(CL_UpscaleRGBA(idx, 2, 2, 3, 1, table, upscale_fake_scale, &fake) == NULL);
	T_CHECK(CL_UpscaleRGBA(idx, 2, 2, 1, 1, table, upscale_fake_scale, &fake) == NULL);
	T_CHECK(CL_UpscaleRGBA(idx, 0, 2, 2, 1, table, upscale_fake_scale, &fake) == NULL);
	T_CHECK(CL_UpscaleRGBA(idx, 2, UPSCALE_MAX_SIDE + 1, 2, 1, table,
		upscale_fake_scale, &fake) == NULL);
	T_CHECK(CL_UpscaleRGBA(NULL, 2, 2, 2, 1, table, upscale_fake_scale, &fake) == NULL);
	T_CHECK(CL_UpscaleRGBA(idx, 2, 2, 2, 1, table, NULL, &fake) == NULL);
	T_EQ_INT(fake.calls, 0);

	/* a scaler that fails: nothing comes back */
	fake.fail = 1;
	T_CHECK(CL_UpscaleRGBA(idx, 2, 2, 2, 1, table, upscale_fake_scale, &fake) == NULL);
	T_EQ_INT(fake.calls, 1);
}

TEST(upscale_summary_names_metalfx)
{
	upscale_count_t n;
	char out[256];

	memset(&n, 0, sizeof(n));
	n.written = 3;
	n.bytes = 10;
	CL_UpscaleSummary(&n, 4, UPSCALE_METALFX, "/g/baseq2", out, sizeof(out));
	T_EQ_STR(out, "upscale_textures: 3 written, 0 skipped, 0 refused, 0 failed, "
		"10 bytes, 4x with metalfx, in /g/baseq2\n");
	CL_UpscaleSummary(&n, 2, UPSCALE_SCALE2X, "/g/baseq2", out, sizeof(out));
	T_EQ_STR(out, "upscale_textures: 3 written, 0 skipped, 0 refused, 0 failed, "
		"10 bytes, 2x, in /g/baseq2\n");
}
