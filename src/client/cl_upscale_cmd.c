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
 * upscale_textures <2|4> [scale2x|metalfx|vt] [force]: writes 2x or 4x PNG
 * versions of the
 * world textures (textures/ .wal), the sky faces (env/ .pcx) and the
 * model and player skins (models/ and players/ .pcx, not the players'
 * _i.pcx icons) of the player's own data into the write directory,
 * FS_Gamedir(), under the same names with .png, where r_retexturing
 * loads them at the original's size (docs/plans/high-resolution.md,
 * P6). With scale2x (the default) the indices are scaled, once per
 * doubling, then converted with the 8-bit palette as the renderers
 * convert them; with metalfx (TASK124, decision 0012, macOS) they are
 * converted first and scaled by MetalFX's spatial scaler in one pass
 * (CL_UpscaleRGBA, cl_upscale_metalfx.m); with vt (TASK167, macOS 26)
 * the same RGBA path runs VideoToolbox's super-resolution scaler
 * (cl_upscale_vt.m), whose model the operating system downloads once,
 * started from the menu item only (a run without it refuses and writes
 * nothing; decision 0013). Where a backend
 * is unsupported the run says so in one line and uses scale2x. The PNG is written by
 * ImageIO (decision 0010). A file that exists is left
 * alone unless force is given; a link is never written through.
 *
 * This file does the I/O only: listing, loading, scaling, and it hands
 * the real steps to CL_UpscaleWriteOut. Every decision is made in
 * cl_upscale.c, where the unit tests run it: which names are sources
 * and how they are written (CL_UpscaleOutName), which directories are
 * walked, skip, write or refuse, the order of one file's steps, the
 * git work-tree check of the write directory and of each output file's
 * directory, the file write itself, the demo and remote-server
 * refusal, the counts and the summary line.
 *
 * =======================================================================
 */

#include "header/client.h"
#include "header/upscale.h"
#include "header/upscale_metalfx.h"
#include "header/upscale_vt.h"
#include "refresh/files/imgdecode.h"

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#if !(defined(__APPLE__) && TARGET_OS_OSX)
/* cl_upscale_metalfx.m is built for macOS only (decision 0012) */
#define UPSCALE_NO_METALFX "not built for this platform"

int
CL_UpscaleMetalFXAvailable(char *reason, size_t size)
{
	Q_strlcpy(reason, UPSCALE_NO_METALFX, size);
	return 0;
}

int
CL_UpscaleMetalFXBegin(char *reason, size_t size)
{
	Q_strlcpy(reason, UPSCALE_NO_METALFX, size);
	return 0;
}

int
CL_UpscaleMetalFXScale(void *ctx, const unsigned char *in, int iw, int ih,
		unsigned char *out, int ow, int oh)
{
	(void)ctx;
	(void)in;
	(void)iw;
	(void)ih;
	(void)out;
	(void)ow;
	(void)oh;
	return 0;
}

void
CL_UpscaleMetalFXEnd(void)
{
}

/* cl_upscale_vt.m is built for macOS only (TASK167): iOS says unsupported */
void
CL_UpscaleVTFacts(int factor, upscale_vt_facts_t *facts)
{
	(void)factor;
	memset(facts, 0, sizeof(*facts));
}

int
CL_UpscaleVTAvailable(int factor, char *reason, size_t size)
{
	(void)factor;
	Q_strlcpy(reason, UPSCALE_NO_METALFX, size);
	return 0;
}

int
CL_UpscaleVTFetchStart(int factor, char *reason, size_t size)
{
	(void)factor;
	Q_strlcpy(reason, UPSCALE_NO_METALFX, size);
	return 0;
}

upscale_vt_fetch_t
CL_UpscaleVTFetchState(char *reason, size_t size)
{
	Q_strlcpy(reason, "", size);
	return UPSCALE_VT_FETCH_IDLE;
}

int
CL_UpscaleVTBegin(int factor, char *reason, size_t size)
{
	(void)factor;
	Q_strlcpy(reason, UPSCALE_NO_METALFX, size);
	return 0;
}

int
CL_UpscaleVTScale(void *ctx, const unsigned char *in, int iw, int ih,
		unsigned char *out, int ow, int oh)
{
	return CL_UpscaleMetalFXScale(ctx, in, iw, ih, out, ow, oh);
}

void
CL_UpscaleVTEnd(void)
{
}
#endif

/*
 * Every name with the extension ext in dir and the directories below
 * it, from the paks and the loose files of every search path, into
 * files (once each). The listing is bounded: UPSCALE_MAX_DIRS
 * directories, UPSCALE_MAX_FILES names; n->cut is set when it stops at
 * either, and a loose directory whose name is refused is counted in
 * n->baddirs.
 */
static void
CL_UpscaleList(const char *dir, const char *ext, strlist_t *files,
		upscale_count_t *n)
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
			if (!CL_UpscaleListAdd(files, found.data[i], UPSCALE_MAX_FILES, &n->cut))
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
			if (!CL_UpscaleListDir(&dirs, found.data[i], &n->cut, &n->baddirs))
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

/* one source and what is needed to scale it: the steps' context */
typedef struct
{
	const char *name;
	const upscale_kind_t *kind;
	int factor;
	upscale_backend_t backend;
	const unsigned *table;
	upscale_gitcache_t *git;
} upscale_job_t;

static upscale_entry_t
CL_UpscaleStepEntry(void *ctx, const char *path)
{
	(void)ctx;

	return CL_UpscaleEntryKind(path);
}

static int
CL_UpscaleStepMakeDirs(void *ctx, const char *path)
{
	(void)ctx;

	/* the output name has no "..", so FS_CreatePath makes its directories */
	FS_CreatePath(path);

	return CL_UpscaleDirExists(path);
}

static int
CL_UpscaleStepGit(void *ctx, const char *path)
{
	return CL_UpscalePathInGitTree(((upscale_job_t *)ctx)->git, path);
}

/* the source loaded, scaled and encoded as a PNG */
static upscale_outcome_t
CL_UpscaleStepMake(void *ctx, unsigned char **png, size_t *pnglen)
{
	const upscale_job_t *job = ctx;
	unsigned char *idx, *big, *rgba = NULL;
	int w = 0, h = 0, refused = 0, factor = job->factor;

	idx = CL_UpscaleLoad(job->name, job->kind->wal, &w, &h, &refused);

	if (!idx)
	{
		Com_DPrintf("upscale_textures: %s %s\n", refused ? "refused size of" :
			"cannot read", job->name);
		return refused ? UPSCALE_REFUSED : UPSCALE_FAILED;
	}

	if (job->kind->skin)
	{
		CL_UpscaleFloodFillSkin(idx, w, h, job->table);
	}

	if (CL_UpscaleBackendRGBA(job->backend))
	{
		/* the RGBA padded, scaled in one pass, cropped, its alpha from the indices */
		rgba = CL_UpscaleRGBA(idx, w, h, factor, job->kind->wrap, job->table,
			(job->backend == UPSCALE_VT) ? CL_UpscaleVTScale :
			CL_UpscaleMetalFXScale, NULL);
		free(idx);
	}
	else
	{
		big = CL_UpscaleIndices(idx, w, h, factor, job->kind->wrap);
		free(idx);

		/* w and h are at most UPSCALE_MAX_SIDE, so the RGBA size fits */
		if (big)
		{
			rgba = CL_UpscaleToRGBA(big, (size_t)w * factor,
				(size_t)h * factor, job->table);
		}

		free(big);
	}

	if (!rgba || (IMGDEC_EncodePNG(rgba, (size_t)w * factor,
		(size_t)h * factor, png, pnglen) != IMGDEC_OK))
	{
		Com_DPrintf("upscale_textures: cannot scale %s\n", job->name);
		free(rgba);
		free(*png);
		*png = NULL;
		return UPSCALE_FAILED;
	}

	free(rgba);

	return UPSCALE_WRITTEN;
}

static int
CL_UpscaleStepWrite(void *ctx, const char *path, const unsigned char *data,
		size_t len)
{
	(void)ctx;

	return CL_UpscaleWriteFile(path, data, len);
}

static void
CL_UpscaleStepSay(void *ctx, int developer, const char *path,
		const char *text)
{
	(void)ctx;

	if (developer)
	{
		Com_DPrintf("upscale_textures: %s: %s\n", path, text);
	}
	else
	{
		Com_Printf("upscale_textures: %s: %s\n", path, text);
	}
}

static const upscale_steps_t upscale_steps = {
	CL_UpscaleStepEntry,
	CL_UpscaleStepMakeDirs,
	CL_UpscaleStepGit,
	CL_UpscaleStepMake,
	CL_UpscaleStepWrite,
	CL_UpscaleStepSay
};

/* scales name and writes it as its output name in the write directory */
static upscale_outcome_t
CL_UpscaleOne(const char *name, const upscale_kind_t *kind, int factor,
		upscale_backend_t backend, int force, const unsigned *table,
		upscale_gitcache_t *git, size_t *bytes)
{
	char outname[MAX_QPATH], path[MAX_OSPATH];
	upscale_job_t job;

	*bytes = 0;

	if (!CL_UpscaleOutName(name, outname, sizeof(outname)) ||
		!CL_UpscaleOutPath(FS_Gamedir(), outname, path, sizeof(path)))
	{
		Com_DPrintf("upscale_textures: refused name %s\n", name);
		return UPSCALE_REFUSED;
	}

	job.name = name;
	job.kind = kind;
	job.factor = factor;
	job.backend = backend;
	job.table = table;
	job.git = git;

	return CL_UpscaleWriteOut(path, force, &upscale_steps, &job, bytes);
}

/*
 * The checks of a vt run at factor (CL_UpscaleVTState), then the run
 * started: 1 when it may scale; 0 with the reason, for scale2x; -1 with
 * the reason when the model is missing or being downloaded, and the run
 * writes nothing. A run never fetches the model: only the menu item
 * does (decision 0013), so no command, stuffed text among them, starts
 * a download. Nothing a run finds is remembered.
 */
static int
CL_UpscaleVTRun(int factor, char *reason, size_t size)
{
	upscale_vt_facts_t facts;
	upscale_vt_state_t state;

	CL_UpscaleVTFacts(factor, &facts);
	state = CL_UpscaleVTState(&facts, factor, reason, size);

	if (state == UPSCALE_VT_FETCH)
	{
		return -1;
	}

	return (state == UPSCALE_VT_READY) && CL_UpscaleVTBegin(factor, reason, size);
}

void
CL_UpscaleTextures_f(void)
{
	const char *colormap = "pics/colormap.pcx";
	const unsigned char *palette;
	unsigned char *raw = NULL;
	char line[MAX_OSPATH + 512], reason[192];
	const char *argv[4];
	upscale_count_t n;
	upscale_gitcache_t git;
	upscale_backend_t backend;
	unsigned table[256];
	int argc, factor, force, len, k, supported;

	argc = Cmd_Argc();

	for (k = 0; (k < argc) && (k < 4); k++)
	{
		argv[k] = Cmd_Argv(k);
	}

	if ((argc > 4) ||
		!CL_UpscaleParseArgs(argc, argv, &factor, &backend, &force))
	{
		Com_Printf("%s", UPSCALE_USAGE);
		return;
	}

	if (!CL_UpscaleAllowed(cls.state >= ca_connected,
		NET_IsLocalAddress(cls.netchan.remote_address), cl.attractloop))
	{
		Com_Printf("upscale_textures: refused while a demo plays or while connected to a remote server\n");
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

	/* a backend's objects live for this run only; unsupported: scale2x, one line */
	reason[0] = '\0';
	supported = 0;

	if (backend == UPSCALE_METALFX)
	{
		supported = CL_UpscaleMetalFXBegin(reason, sizeof(reason));
	}
	else if (backend == UPSCALE_VT)
	{
		supported = CL_UpscaleVTRun(factor, reason, sizeof(reason));

		if (supported < 0)
		{
			Com_Printf("upscale_textures: vt: %s; nothing written\n", reason);
			return;
		}
	}

	backend = CL_UpscaleChooseBackend(backend, supported, reason, line,
		sizeof(line));

	if (line[0])
	{
		Com_Printf("%s", line);
	}

	memset(&n, 0, sizeof(n));
	memset(&git, 0, sizeof(git));

	for (k = 0; k < UPSCALE_KINDS; k++)
	{
		const upscale_kind_t *kind = CL_UpscaleKind(k);
		strlist_t files;
		int i;

		StrList_Init(&files, 0);
		CL_UpscaleList(kind->dir, kind->ext, &files, &n);

		for (i = 0; i < files.num; i++)
		{
			size_t bytes = 0;

			if (CL_UpscaleIsSource(kind, files.data[i]))
			{
				CL_UpscaleCount(&n, CL_UpscaleOne(files.data[i], kind, factor,
					backend, force, table, &git, &bytes), bytes);
			}
		}

		StrList_Free(&files);
	}

	if (supported && (backend == UPSCALE_METALFX))
	{
		CL_UpscaleMetalFXEnd();
	}
	else if (supported && (backend == UPSCALE_VT))
	{
		CL_UpscaleVTEnd();
	}

	CL_UpscaleSummary(&n, factor, backend, FS_Gamedir(), line, sizeof(line));
	Com_Printf("%s", line);
}
