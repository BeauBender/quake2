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
 * fill and the .wal and .pcx size checks. The command itself
 * (cl_upscale_cmd.c, ImageIO) is not linked here.
 *
 * =======================================================================
 */

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
	/* a component that begins with a dot, the last one included */
	T_EQ_INT(CL_UpscaleNameOk(".wal"), 0);
	T_EQ_INT(CL_UpscaleNameOk("textures/.x.wal"), 0);
	T_EQ_INT(CL_UpscaleNameOk("./x.wal"), 0);
	T_EQ_INT(CL_UpscaleNameOk("textures/./x.wal"), 0);
	T_EQ_INT(CL_UpscaleNameOk("textures/+0btshoot1.wal"), 0);
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
			(c >= '0' && c <= '9') || c == '_' || c == '-';
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
}

/* UPSCALE_GIT_DEPTH components are walked to the root; one more is refused unwalked */
TEST(upscale_git_tree_depth_bound)
{
	static const char *const none[] = { NULL };
	char dir[MAX_OSPATH];
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
	T_EQ_INT(t.ncalls, 0);

	/* longer than a path buffer */
	memset(dir, 'a', sizeof(dir));
	dir[0] = '/';
	dir[sizeof(dir) - 1] = '\0';
	T_EQ_INT(in_synth(dir, none), 1);
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
	CL_UpscaleWrapPad(tile4, 4, 4, pad);
	T_EQ_MEM(pad, want, sizeof(want));

	memset(back, 0xee, sizeof(back));
	CL_UpscaleCrop(pad, 6, 1, 1, 4, 4, back);
	T_EQ_MEM(back, tile4, sizeof(tile4));

	/* a corner block across the seam */
	CL_UpscaleCrop(pad, 6, 4, 4, 2, 2, part);
	T_EQ_INT(part[0], 15);
	T_EQ_INT(part[1], 12);
	T_EQ_INT(part[2], 3);
	T_EQ_INT(part[3], 0);
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

	CL_UpscaleCrop(a, tw, tw / 3, th / 3, tw / 3, th / 3, out);
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

	CL_UpscaleToRGBA(img, 3, 3, table, rgba);
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
	CL_UpscaleToRGBA(copy, 3, 3, table, rgba);
	T_EQ_INT(rgba[4 * 4 + 0], pal[80 * 3]);
	T_EQ_INT(rgba[1 * 4 + 3], 0);
	/* index 1, in the first row: above is out, below is 255, so left */
	T_EQ_INT(rgba[1 * 4 + 0], pal[10 * 3]);
	/* and with the left 255 too: right */
	copy[0] = 255;
	CL_UpscaleToRGBA(copy, 3, 3, table, rgba);
	T_EQ_INT(rgba[1 * 4 + 0], pal[30 * 3]);
	/* index 0: nothing above or left, below (40) */
	T_EQ_INT(rgba[0 * 4 + 0], pal[40 * 3]);
	/* index 3 (40) itself stays */
	T_EQ_INT(rgba[3 * 4 + 0], pal[40 * 3]);

	/* index i == w: the texel above is index 0, which R_Convert8to32's i > w passes over */
	memcpy(copy, img, sizeof(copy));
	copy[3] = 255;
	CL_UpscaleToRGBA(copy, 3, 3, table, rgba);
	T_EQ_INT(rgba[3 * 4 + 0], pal[70 * 3]);

	/* one row: left, then right, and index 0 when neither */
	CL_UpscaleToRGBA(row, 3, 1, table, rgba);
	T_EQ_INT(rgba[0 * 4 + 0], pal[0]);
	T_EQ_INT(rgba[1 * 4 + 0], pal[7 * 3]);
	T_EQ_INT(rgba[0 * 4 + 3], 0);
	T_EQ_INT(rgba[1 * 4 + 3], 0);
	T_EQ_INT(rgba[2 * 4 + 3], 255);

	/* nothing around it: index 0's colour */
	CL_UpscaleToRGBA(lone, 1, 1, table, rgba);
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
		CL_UpscaleToRGBA(big, s, s, table, rgba);

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
