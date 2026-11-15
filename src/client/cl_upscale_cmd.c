/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * =======================================================================
 *
 * upscale_textures <2|4> [force]: writes 2x or 4x PNG versions of the
 * world textures (textures/ .wal), the sky faces (env/ .pcx) and the
 * model and player skins (models/ and players/ .pcx, not the players'
 * _i.pcx icons) of the player's own data into the write directory,
 * FS_Gamedir(), under the same names with .png, where r_retexturing
 * loads them at the original's size (docs/plans/high-resolution.md,
 * P6). The indices are scaled with scale2x, once per doubling, then
 * converted with the 8-bit palette as the renderers convert them; the
 * PNG is written by ImageIO (decision 0010). A file that exists is left
 * alone unless force is given.
 *
 * Every name comes from a listing of the paks and directories and is
 * checked by CL_UpscaleOutName before it is used; the write directory
 * must not be inside a git work tree (cl_upscale.c).
 *
 * =======================================================================
 */

#include "header/client.h"
#include "header/upscale.h"
#include "refresh/files/imgdecode.h"

/* What is scaled: listed below dir, with the extension ext. */
typedef struct
{
	const char *dir;
	const char *ext;
	const char *notsuffix;	/* names that end so are not sources */
	int wrap;		/* a repeating world texture: seamless */
	int skin;		/* flood-filled first, as the renderers fill skins */
} upscale_kind_t;

static const upscale_kind_t upscale_kinds[] = {
	{ "textures", "wal", NULL, 1, 0 },
	{ "env", "pcx", NULL, 0, 0 },
	{ "models", "pcx", NULL, 0, 1 },
	{ "players", "pcx", "_i.pcx", 0, 1 }
};

/* the most directories walked and files taken from the listings */
#define UPSCALE_MAX_DIRS 4096
#define UPSCALE_MAX_FILES 65536

typedef struct
{
	int written, skipped, refused, failed;
	size_t bytes;
} upscale_count_t;

/*
 * Every name with the extension ext in dir and the directories below
 * it, from the paks and the loose files of every search path, into
 * files (once each). The listing is bounded: UPSCALE_MAX_DIRS
 * directories, UPSCALE_MAX_FILES names.
 */
static void
CL_UpscaleList(const char *dir, const char *ext, strlist_t *files)
{
	strlist_t dirs;
	int d;

	StrList_Init(&dirs, 0);
	StrList_Append(&dirs, dir);

	for (d = 0; (d < dirs.num) && (d < UPSCALE_MAX_DIRS); d++)
	{
		char pattern[MAX_OSPATH];
		strlist_t found;
		int i;

		/* in a pak the star also matches '/': everything below */
		Com_sprintf(pattern, sizeof(pattern), "%s/*.%s", dirs.data[d], ext);
		found = FS_ListFiles2(pattern, 0, SFF_SUBDIR);

		for (i = 0; (i < found.num) && (files->num < UPSCALE_MAX_FILES); i++)
		{
			if (!StrList_Contains(files, found.data[i]))
			{
				StrList_Append(files, found.data[i]);
			}
		}

		StrList_Free(&found);

		/* loose directories are listed one level at a time */
		Com_sprintf(pattern, sizeof(pattern), "%s/*", dirs.data[d]);
		found = FS_ListFiles2(pattern, SFF_SUBDIR, 0);

		for (i = 0; (i < found.num) && (dirs.num < UPSCALE_MAX_DIRS); i++)
		{
			if (CL_UpscaleNameOk(found.data[i]) &&
				!StrList_Contains(&dirs, found.data[i]))
			{
				StrList_Append(&dirs, found.data[i]);
			}
		}

		StrList_Free(&found);
	}

	StrList_Free(&dirs);
}

/* 1 when name ends with suffix, ignoring case */
static int
CL_UpscaleEndsWith(const char *name, const char *suffix)
{
	size_t n = strlen(name), s = strlen(suffix);

	return (n >= s) && !Q_stricmp(name + n - s, suffix);
}

/*
 * The palette indices of name, w by h: the first mip level of a .wal,
 * or an 8-bit .pcx, its size checked before anything is allocated for
 * its pixels. NULL when the file is refused or cannot be read.
 */
static unsigned char *
CL_UpscaleLoad(const char *name, int wal, int *w, int *h, int *refused)
{
	unsigned char *raw = NULL, *idx = NULL;
	int len, ok;
	size_t ofs = 0;

	*refused = 0;
	len = FS_LoadFile(name, (void **)&raw);

	if (!raw || (len <= 0))
	{
		return NULL;
	}

	ok = wal ? CL_UpscaleWalPixels(raw, (size_t)len, w, h, &ofs) :
		CL_UpscalePcxSize(raw, (size_t)len, w, h);

	if (!ok)
	{
		FS_FreeFile(raw);
		*refused = 1;
		return NULL;
	}

	if (wal)
	{
		idx = malloc((size_t)*w * *h);

		if (idx)
		{
			memcpy(idx, raw + ofs, (size_t)*w * *h);
		}

		FS_FreeFile(raw);
	}
	else
	{
		char path[MAX_QPATH];
		byte *pic = NULL, *pal = NULL;
		int pw = 0, ph = 0, bits = 0;

		FS_FreeFile(raw);
		Q_strlcpy(path, name, sizeof(path));
		SCR_LoadImageWithPalette(path, &pic, &pal, &pw, &ph, &bits);
		free(pal);

		/* what the header said, as 8-bit indices */
		if (pic && (bits == 8) && (pw == *w) && (ph == *h))
		{
			idx = pic;
		}
		else
		{
			free(pic);
		}
	}

	return idx;
}

/* scales name and writes it as outname in the write directory */
static void
CL_UpscaleOne(const char *name, const upscale_kind_t *kind, int factor,
		int force, const unsigned *table, upscale_count_t *n)
{
	char outname[MAX_QPATH], path[MAX_OSPATH];
	unsigned char *idx, *big = NULL, *rgba = NULL, *png = NULL;
	size_t pnglen = 0;
	int w = 0, h = 0, refused = 0, ok;
	FILE *f;

	if (!CL_UpscaleOutName(name, outname, sizeof(outname)) ||
		(strlen(FS_Gamedir()) + 1 + strlen(outname) >= sizeof(path)))
	{
		Com_DPrintf("upscale_textures: refused name %s\n", name);
		n->refused++;
		return;
	}

	Com_sprintf(path, sizeof(path), "%s/%s", FS_Gamedir(), outname);

	if (!force && Sys_IsFile(path))
	{
		n->skipped++;
		return;
	}

	idx = CL_UpscaleLoad(name, !strcmp(kind->ext, "wal"), &w, &h, &refused);

	if (!idx)
	{
		Com_DPrintf("upscale_textures: %s %s\n", refused ? "refused size of" : "cannot read", name);
		if (refused)
		{
			n->refused++;
		}
		else
		{
			n->failed++;
		}

		return;
	}

	if (kind->skin)
	{
		CL_UpscaleFloodFillSkin(idx, w, h, table);
	}

	big = CL_UpscaleIndices(idx, w, h, factor, kind->wrap);
	free(idx);

	/* w and h are at most UPSCALE_MAX_SIDE, so the RGBA size fits */
	if (big)
	{
		rgba = malloc((size_t)w * factor * h * factor * 4);
	}

	if (rgba)
	{
		CL_UpscaleToRGBA(big, (size_t)w * factor, (size_t)h * factor, table, rgba);
	}

	if (!rgba || (IMGDEC_EncodePNG(rgba, (size_t)w * factor,
		(size_t)h * factor, &png, &pnglen) != IMGDEC_OK))
	{
		Com_DPrintf("upscale_textures: cannot scale %s\n", name);
		n->failed++;
		free(big);
		free(rgba);
		return;
	}

	free(big);
	free(rgba);

	/* outname has no "..", so FS_CreatePath makes its directories */
	FS_CreatePath(path);
	f = Q_fopen(path, "wb");
	ok = f && (fwrite(png, 1, pnglen, f) == pnglen);

	if (f && (fclose(f) != 0))
	{
		ok = 0;
	}

	if (ok)
	{
		n->written++;
		n->bytes += pnglen;
	}
	else
	{
		Com_Printf("upscale_textures: cannot write %s\n", path);
		n->failed++;
	}

	free(png);
}

void
CL_UpscaleTextures_f(void)
{
	char colormap[] = "pics/colormap.pcx";
	upscale_count_t n;
	unsigned table[256];
	byte *pic = NULL, *pal = NULL;
	int factor, force, pw = 0, ph = 0, bits = 0;
	size_t k;

	if ((Cmd_Argc() < 2) || (Cmd_Argc() > 3) ||
		(strcmp(Cmd_Argv(1), "2") && strcmp(Cmd_Argv(1), "4")) ||
		((Cmd_Argc() == 3) && strcmp(Cmd_Argv(2), "force")))
	{
		Com_Printf("usage: upscale_textures <2|4> [force]\n");
		return;
	}

	factor = atoi(Cmd_Argv(1));
	force = (Cmd_Argc() == 3);

#ifndef __APPLE__
	Com_Printf("upscale_textures: no PNG encoder on this platform\n");
	return;
#endif

	if (CL_UpscaleDirInGitTree(FS_Gamedir()))
	{
		Com_Printf("upscale_textures: refusing to write: %s is inside a git work tree, or its real path is unknown\n",
			FS_Gamedir());
		return;
	}

	SCR_LoadImageWithPalette(colormap, &pic, &pal, &pw, &ph, &bits);
	free(pic);

	if (!pal)
	{
		Com_Printf("upscale_textures: no palette in %s\n", colormap);
		return;
	}

	CL_UpscaleTable(pal, table);
	free(pal);

	memset(&n, 0, sizeof(n));

	for (k = 0; k < sizeof(upscale_kinds) / sizeof(upscale_kinds[0]); k++)
	{
		const upscale_kind_t *kind = &upscale_kinds[k];
		strlist_t files;
		int i;

		StrList_Init(&files, 0);
		CL_UpscaleList(kind->dir, kind->ext, &files);

		for (i = 0; i < files.num; i++)
		{
			if (kind->notsuffix && CL_UpscaleEndsWith(files.data[i], kind->notsuffix))
			{
				continue;
			}

			CL_UpscaleOne(files.data[i], kind, factor, force, table, &n);
		}

		StrList_Free(&files);
	}

	Com_Printf("upscale_textures: %d written, %d skipped, %d refused, %d failed, "
		"%llu bytes, %dx, in %s\n", n.written, n.skipped, n.refused, n.failed,
		(unsigned long long)n.bytes, factor, FS_Gamedir());
}
