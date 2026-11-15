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
 * alone unless force is given; a link is never written through.
 *
 * This file does the I/O only: listing, loading, writing. Every
 * decision is made in cl_upscale.c, where the unit tests run it: which
 * names are sources and how they are written (CL_UpscaleOutName), skip,
 * write or refuse, the git work-tree check of the write directory and
 * of each output file's directory, the remote-server refusal, the
 * counts and the summary line.
 *
 * =======================================================================
 */

#include "header/client.h"
#include "header/upscale.h"
#include "refresh/files/imgdecode.h"

#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif

/*
 * Every name with the extension ext in dir and the directories below
 * it, from the paks and the loose files of every search path, into
 * files (once each). The listing is bounded: UPSCALE_MAX_DIRS
 * directories, UPSCALE_MAX_FILES names; *cut is set when it stops at
 * either.
 */
static void
CL_UpscaleList(const char *dir, const char *ext, strlist_t *files, int *cut)
{
	strlist_t dirs;
	int d;

	StrList_Init(&dirs, 0);
	StrList_Append(&dirs, dir);

	/* CL_UpscaleListAdd keeps dirs.num at most UPSCALE_MAX_DIRS */
	for (d = 0; d < dirs.num; d++)
	{
		char pattern[MAX_OSPATH];
		strlist_t found;
		int i;

		/* in a pak the star also matches '/': everything below */
		Com_sprintf(pattern, sizeof(pattern), "%s/*.%s", dirs.data[d], ext);
		found = FS_ListFiles2(pattern, 0, SFF_SUBDIR);

		for (i = 0; i < found.num; i++)
		{
			if (!CL_UpscaleListAdd(files, found.data[i], UPSCALE_MAX_FILES, cut))
			{
				break;
			}
		}

		StrList_Free(&found);

		/* loose directories are listed one level at a time */
		Com_sprintf(pattern, sizeof(pattern), "%s/*", dirs.data[d]);
		found = FS_ListFiles2(pattern, SFF_SUBDIR, 0);

		for (i = 0; i < found.num; i++)
		{
			if (CL_UpscaleNameOk(found.data[i]) &&
				!CL_UpscaleListAdd(&dirs, found.data[i], UPSCALE_MAX_DIRS, cut))
			{
				break;
			}
		}

		StrList_Free(&found);
	}

	StrList_Free(&dirs);
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
		if (CL_UpscalePcxDecodedOk(pic, bits, pw, ph, *w, *h))
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

/*
 * len bytes of data into path: written to a new temporary name in the
 * same directory (O_EXCL, a link never followed), closed, then renamed
 * over path, so path is either what it was or all of the new file. The
 * temporary file is removed on any failure. 1 on success.
 */
static int
CL_UpscaleWriteFile(const char *path, const unsigned char *data, size_t len)
{
#ifdef _WIN32
	(void)path;
	(void)data;
	(void)len;

	return 0;
#else
	char tmp[MAX_OSPATH];
	size_t done = 0;
	int fd, ok;

	if (!CL_UpscaleTempPath(path, (long)getpid(), tmp, sizeof(tmp)))
	{
		return 0;
	}

	fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0666);

	if (fd < 0)
	{
		return 0;
	}

	while (done < len)
	{
		ssize_t n = write(fd, data + done, len - done);

		if ((n < 0) && (errno == EINTR))
		{
			continue;
		}

		if (n <= 0)
		{
			break;
		}

		done += (size_t)n;
	}

	ok = (done == len);

	if (close(fd) != 0)
	{
		ok = 0;
	}

	if (ok && (rename(tmp, path) != 0))
	{
		ok = 0;
	}

	if (!ok)
	{
		unlink(tmp);
	}

	return ok;
#endif
}

/* scales name and writes it as its output name in the write directory */
static upscale_outcome_t
CL_UpscaleOne(const char *name, const upscale_kind_t *kind, int factor,
		int force, const unsigned *table, upscale_gitcache_t *git,
		size_t *bytes)
{
	char outname[MAX_QPATH], path[MAX_OSPATH];
	unsigned char *idx, *big, *rgba = NULL, *png = NULL;
	size_t pnglen = 0;
	int w = 0, h = 0, refused = 0;
	upscale_action_t action;

	*bytes = 0;

	if (!CL_UpscaleOutName(name, outname, sizeof(outname)) ||
		!CL_UpscaleOutPath(FS_Gamedir(), outname, path, sizeof(path)))
	{
		Com_DPrintf("upscale_textures: refused name %s\n", name);
		return UPSCALE_REFUSED;
	}

	action = CL_UpscaleAction(CL_UpscaleEntryKind(path), force);

	if (action == UPSCALE_SKIP)
	{
		return UPSCALE_SKIPPED;
	}

	if (action != UPSCALE_WRITE)
	{
		Com_Printf("upscale_textures: %s is a link or not a file, left as it is\n", path);
		return UPSCALE_REFUSED;
	}

	/* outname has no "..", so FS_CreatePath makes its directories */
	FS_CreatePath(path);

	if (CL_UpscalePathInGitTree(git, path))
	{
		Com_DPrintf("upscale_textures: refused %s: its directory is inside a git work tree\n", path);
		return UPSCALE_REFUSED;
	}

	idx = CL_UpscaleLoad(name, kind->wal, &w, &h, &refused);

	if (!idx)
	{
		Com_DPrintf("upscale_textures: %s %s\n", refused ? "refused size of" : "cannot read", name);
		return refused ? UPSCALE_REFUSED : UPSCALE_FAILED;
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

	free(big);

	if (!rgba || (IMGDEC_EncodePNG(rgba, (size_t)w * factor,
		(size_t)h * factor, &png, &pnglen) != IMGDEC_OK))
	{
		Com_DPrintf("upscale_textures: cannot scale %s\n", name);
		free(rgba);
		free(png);
		return UPSCALE_FAILED;
	}

	free(rgba);

	if (!CL_UpscaleWriteFile(path, png, pnglen))
	{
		Com_Printf("upscale_textures: cannot write %s\n", path);
		free(png);
		return UPSCALE_FAILED;
	}

	free(png);
	*bytes = pnglen;

	return UPSCALE_WRITTEN;
}

void
CL_UpscaleTextures_f(void)
{
	const char *colormap = "pics/colormap.pcx";
	const unsigned char *palette;
	unsigned char *raw = NULL;
	char line[MAX_OSPATH + 256];
	upscale_count_t n;
	upscale_gitcache_t git;
	unsigned table[256];
	int factor, force, len, k;

	if ((Cmd_Argc() < 2) || (Cmd_Argc() > 3) ||
		(strcmp(Cmd_Argv(1), "2") && strcmp(Cmd_Argv(1), "4")) ||
		((Cmd_Argc() == 3) && strcmp(Cmd_Argv(2), "force")))
	{
		Com_Printf("usage: upscale_textures <2|4> [force]\n");
		return;
	}

	factor = atoi(Cmd_Argv(1));
	force = (Cmd_Argc() == 3);

	if (!CL_UpscaleAllowed(cls.state >= ca_connected,
		NET_IsLocalAddress(cls.netchan.remote_address)))
	{
		Com_Printf("upscale_textures: refused while connected to a remote server\n");
		return;
	}

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

	len = FS_LoadFile(colormap, (void **)&raw);
	palette = (raw && (len > 0)) ? CL_UpscalePalette(raw, (size_t)len) : NULL;

	if (!palette)
	{
		Com_Printf("upscale_textures: no 8-bit palette in %s\n", colormap);

		if (raw)
		{
			FS_FreeFile(raw);
		}

		return;
	}

	CL_UpscaleTable(palette, table);
	FS_FreeFile(raw);

	memset(&n, 0, sizeof(n));
	memset(&git, 0, sizeof(git));

	for (k = 0; k < UPSCALE_KINDS; k++)
	{
		const upscale_kind_t *kind = CL_UpscaleKind(k);
		strlist_t files;
		int i;

		StrList_Init(&files, 0);
		CL_UpscaleList(kind->dir, kind->ext, &files, &n.cut);

		for (i = 0; i < files.num; i++)
		{
			size_t bytes = 0;

			if (CL_UpscaleIsSource(kind, files.data[i]))
			{
				CL_UpscaleCount(&n, CL_UpscaleOne(files.data[i], kind, factor,
					force, table, &git, &bytes), bytes);
			}
		}

		StrList_Free(&files);
	}

	CL_UpscaleSummary(&n, factor, FS_Gamedir(), line, sizeof(line));
	Com_Printf("%s", line);
}
