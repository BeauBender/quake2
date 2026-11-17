/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 *
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
 * The local texture upscaler's checks and pixel arithmetic, apart from
 * the console command (cl_upscale_cmd.c) so that the unit tests run
 * them: the names a listing hands it, the git work-tree refusal, the
 * size checks of .wal and .pcx sources, and scale2x on palette indices
 * with wrapped or clamped edges, the order of one output file's steps
 * (CL_UpscaleWriteOut) and the file write itself (CL_UpscaleWriteFile).
 *
 * scale2x, the skin flood fill and the 8-bit conversion are scale2x,
 * R_FloodFillSkin and R_Convert8to32 of
 * src/client/refresh/files/imgscale.c, one copy that the renderers and the
 * client both link: the PNG written holds what a renderer makes of the
 * scaled indices.
 *
 * =======================================================================
 */

#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

#include "header/client.h"
#include "header/upscale.h"
#include "refresh/files/imgscale.h"

/* ---- names ---- */

static int
CL_UpscaleNameChar(int c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		(c >= '0' && c <= '9') || c == '_' || c == '-' || c == '+' ||
		c == '.' || c == '/';
}

int
CL_UpscaleNameOk(const char *name)
{
	const char *p;

	if (!name || !name[0] || (name[0] == '/') || strstr(name, ".."))
	{
		return 0;
	}

	for (p = name; *p; p++)
	{
		if (!CL_UpscaleNameChar((unsigned char)*p))
		{
			return 0;
		}

		/* a component that begins with a dot ("./", ".x") */
		if ((*p == '.') && ((p == name) || (p[-1] == '/')))
		{
			return 0;
		}

		/* an empty component ("a//b", "a/") */
		if ((*p == '/') && ((p[1] == '/') || (p[1] == '\0')))
		{
			return 0;
		}
	}

	return 1;
}

int
CL_UpscaleOutName(const char *name, char *out, size_t outsize)
{
	const char *dot, *slash;
	size_t stem;

	if (!out || (outsize == 0))
	{
		return 0;
	}

	out[0] = '\0';

	if (!CL_UpscaleNameOk(name))
	{
		return 0;
	}

	dot = strrchr(name, '.');
	slash = strrchr(name, '/');

	if (!dot || (slash && (dot < slash)) || (dot[1] == '\0'))
	{
		return 0;
	}

	/* the name up to and with its last dot, then png and the terminator */
	stem = (size_t)(dot - name) + 1;

	if ((stem + 4 > outsize) || (stem + 4 > MAX_QPATH))
	{
		return 0;
	}

	memcpy(out, name, stem);
	memcpy(out + stem, "png", 4);

	return 1;
}

/* ---- the git work-tree refusal ---- */

int
CL_UpscaleInGitTree(const char *dir,
		int (*exists)(const char *path, void *ctx), void *ctx)
{
	char path[MAX_OSPATH];
	char probe[MAX_OSPATH + 8];
	size_t len;
	int depth;

	if (!dir || (dir[0] != '/') || !exists)
	{
		return 1;
	}

	len = strlen(dir);

	if (len >= sizeof(path))
	{
		return 1;
	}

	memcpy(path, dir, len + 1);

	/* the directory, then each parent up to the root, at most this many */
	for (depth = 0; depth <= UPSCALE_GIT_DEPTH; depth++)
	{
		char *cut;

		while ((len > 1) && (path[len - 1] == '/'))
		{
			path[--len] = '\0';
		}

		Com_sprintf(probe, sizeof(probe), "%s/.git", (len == 1) ? "" : path);

		if (exists(probe, ctx))
		{
			return 1;
		}

		if (len == 1)
		{
			/* the root, and no .git on the way */
			return 0;
		}

		cut = strrchr(path, '/');
		len = (cut > path) ? (size_t)(cut - path) : 1;
		path[len] = '\0';
	}

	/* deeper than the bound: not walked to the root */
	return 1;
}

int
CL_UpscaleEntryExists(const char *path, void *ctx)
{
	struct stat st;

	(void)ctx;

#ifdef _WIN32
	if (stat(path, &st) == 0)
#else
	if (lstat(path, &st) == 0)
#endif
	{
		return 1;
	}

	/* only "there is no such entry" is an answer */
	return (errno != ENOENT) && (errno != ENOTDIR);
}

upscale_entry_t
CL_UpscaleEntryKind(const char *path)
{
	struct stat st;

	if (!path)
	{
		return UPSCALE_ENTRY_UNKNOWN;
	}

#ifdef _WIN32
	if (stat(path, &st) == 0)
#else
	if (lstat(path, &st) == 0)
#endif
	{
		return S_ISREG(st.st_mode) ? UPSCALE_ENTRY_FILE : UPSCALE_ENTRY_OTHER;
	}

	return (errno == ENOENT) ? UPSCALE_ENTRY_NONE : UPSCALE_ENTRY_UNKNOWN;
}

int
CL_UpscaleDirGit(const char *dir)
{
	char real[MAX_OSPATH];

	if (!dir || !Sys_Realpath(dir, real, sizeof(real)) ||
		(strlen(real) >= sizeof(real) - 1))
	{
		return UPSCALE_GIT_UNKNOWN;
	}

	return CL_UpscaleInGitTree(real, CL_UpscaleEntryExists, NULL) ?
		UPSCALE_GIT_INSIDE : UPSCALE_GIT_OUTSIDE;
}

int
CL_UpscaleDirInGitTree(const char *dir)
{
	return CL_UpscaleDirGit(dir) != UPSCALE_GIT_OUTSIDE;
}

/* ---- sizes ---- */

int
CL_UpscaleSizeOk(long long w, long long h)
{
	return (w >= 1) && (h >= 1) &&
		(w <= UPSCALE_MAX_SIDE) && (h <= UPSCALE_MAX_SIDE);
}

int
CL_UpscaleWalPixels(const unsigned char *data, size_t len, int *w, int *h,
		size_t *offset)
{
	miptex_t mt;
	long long mw, mh;
	size_t ofs;

	if (!data || (len < sizeof(mt)))
	{
		return 0;
	}

	memcpy(&mt, data, sizeof(mt));
	mw = (unsigned)LittleLong((int)mt.width);
	mh = (unsigned)LittleLong((int)mt.height);
	ofs = (unsigned)LittleLong((int)mt.offsets[0]);

	/* the first mip level after the header, all of it in the file */
	if (!CL_UpscaleSizeOk(mw, mh) || (ofs < sizeof(mt)) || (ofs > len) ||
		((size_t)(mw * mh) > len - ofs))
	{
		return 0;
	}

	*w = (int)mw;
	*h = (int)mh;
	*offset = ofs;

	return 1;
}

int
CL_UpscalePcxSize(const unsigned char *data, size_t len, int *w, int *h)
{
	pcx_t pcx;
	long long pw, ph;

	if (!data || (len <= offsetof(pcx_t, data)))
	{
		return 0;
	}

	memcpy(&pcx, data, offsetof(pcx_t, data));
	pw = (long long)(unsigned short)LittleShort((short)pcx.xmax) -
		(unsigned short)LittleShort((short)pcx.xmin) + 1;
	ph = (long long)(unsigned short)LittleShort((short)pcx.ymax) -
		(unsigned short)LittleShort((short)pcx.ymin) + 1;

	/* what PCX_Decode reads as 8-bit indices: one plane of 8 bits */
	if ((pcx.manufacturer != 0x0a) || (pcx.version != 5) ||
		(pcx.encoding != 1) || (pcx.bits_per_pixel != 8) ||
		(pcx.color_planes != 1) || !CL_UpscaleSizeOk(pw, ph))
	{
		return 0;
	}

	*w = (int)pw;
	*h = (int)ph;

	return 1;
}

const unsigned char *
CL_UpscalePalette(const unsigned char *data, size_t len)
{
	int w, h;

	/* after the header and at least one byte of the image */
	if (!CL_UpscalePcxSize(data, len, &w, &h) ||
		(len <= offsetof(pcx_t, data) + 768))
	{
		return NULL;
	}

	return data + len - 768;
}

int
CL_UpscalePcxDecodedOk(const unsigned char *pic, int bits, int pw, int ph,
		int w, int h)
{
	return pic && (bits == 8) && (pw == w) && (ph == h);
}

/* ---- pixels ---- */

int
CL_UpscaleWrapPad(const unsigned char *src, int w, int h, unsigned char *dst)
{
	int pw = w + 2, x, y;

	if ((w < 1) || (h < 1))
	{
		return 0;
	}

	/* padded (x, y) is source ((x - 1) mod w, (y - 1) mod h) */
	for (y = 0; y < h + 2; y++)
	{
		int sy = (y + h - 1) % h;

		for (x = 0; x < pw; x++)
		{
			dst[y * pw + x] = src[sy * w + (x + w - 1) % w];
		}
	}

	return 1;
}

int
CL_UpscaleCrop(const unsigned char *src, int sw, int sh, int x, int y,
		int w, int h, unsigned char *dst)
{
	int row;

	if ((x < 0) || (y < 0) || (w < 1) || (h < 1) ||
		(x > sw - w) || (y > sh - h))
	{
		return 0;
	}

	for (row = 0; row < h; row++)
	{
		memcpy(dst + (size_t)row * w, src + (size_t)(y + row) * sw + x,
			(size_t)w);
	}

	return 1;
}

void
CL_UpscaleScale2x(const unsigned char *src, unsigned char *dst, int width,
		int height)
{
	scale2x(src, dst, width, height);
}

unsigned char *
CL_UpscaleIndices(const unsigned char *src, int w, int h, int factor,
		int wrap)
{
	unsigned char *img;
	int pass;

	if (!src || !CL_UpscaleSizeOk(w, h) || ((factor != 2) && (factor != 4)))
	{
		return NULL;
	}

	img = malloc((size_t)w * h);

	if (!img)
	{
		return NULL;
	}

	memcpy(img, src, (size_t)w * h);

	/* one scale2x per doubling: 2x once, 4x twice */
	for (pass = 0; pass < factor / 2; pass++)
	{
		/* a wrapped tile is scaled with one texel of border, then cropped */
		int border = wrap ? 1 : 0;
		int sw = w + 2 * border, sh = h + 2 * border;
		unsigned char *in = img, *out, *next;

		if (border)
		{
			in = malloc((size_t)sw * sh);

			if (!in)
			{
				free(img);
				return NULL;
			}

			CL_UpscaleWrapPad(img, w, h, in);
		}

		out = malloc((size_t)sw * sh * 4);

		if (!out)
		{
			if (in != img)
			{
				free(in);
			}

			free(img);
			return NULL;
		}

		CL_UpscaleScale2x(in, out, sw, sh);

		if (in != img)
		{
			free(in);
		}

		next = out;

		if (border)
		{
			next = malloc((size_t)w * h * 4);

			if (!next || !CL_UpscaleCrop(out, 2 * sw, 2 * sh, 2 * border,
				2 * border, 2 * w, 2 * h, next))
			{
				free(next);
				free(out);
				free(img);
				return NULL;
			}

			free(out);
		}

		free(img);
		img = next;
		w *= 2;
		h *= 2;
	}

	return img;
}

/* ---- the backends (TASK124) ---- */

const char *
CL_UpscaleBackendName(int backend)
{
	(void)backend;
	return NULL;
}

int
CL_UpscaleParseArgs(int argc, const char *const *argv, int *factor,
		upscale_backend_t *backend, int *force)
{
	(void)argc; (void)argv; (void)factor; (void)backend; (void)force;
	return 0;
}

upscale_backend_t
CL_UpscaleChooseBackend(upscale_backend_t asked, int supported,
		const char *reason, char *line, size_t linesize)
{
	(void)asked; (void)supported; (void)reason;
	if (linesize) line[0] = '\0';
	return UPSCALE_SCALE2X;
}

int
CL_UpscaleWrapPadRGBA(const unsigned char *src, int w, int h,
		unsigned char *dst)
{
	(void)src; (void)w; (void)h; (void)dst;
	return 0;
}

int
CL_UpscaleCropRGBA(const unsigned char *src, int sw, int sh, int x, int y,
		int w, int h, unsigned char *dst)
{
	(void)src; (void)sw; (void)sh; (void)x; (void)y; (void)w; (void)h; (void)dst;
	return 0;
}

void
CL_UpscaleMaskAlpha(const unsigned char *idx, int w, int h, int factor,
		unsigned char *rgba)
{
	(void)idx; (void)w; (void)h; (void)factor; (void)rgba;
}

unsigned char *
CL_UpscaleRGBA(const unsigned char *idx, int w, int h, int factor, int wrap,
		const unsigned *table, upscale_scaler_t scale, void *ctx)
{
	(void)idx; (void)w; (void)h; (void)factor; (void)wrap; (void)table;
	(void)scale; (void)ctx;
	return NULL;
}

/* ---- skins and the palette ---- */

void
CL_UpscaleFloodFillSkin(unsigned char *skin, int skinwidth, int skinheight,
		const unsigned *table_8to24)
{
	R_FloodFillSkin(skin, skinwidth, skinheight, table_8to24);
}

void
CL_UpscaleTable(const unsigned char *palette, unsigned *table)
{
	int i;

	for (i = 0; i < 256; i++)
	{
		unsigned char *t = (unsigned char *)&table[i];

		t[0] = palette[i * 3 + 0];
		t[1] = palette[i * 3 + 1];
		t[2] = palette[i * 3 + 2];
		t[3] = (i == 255) ? 0 : 255;
	}
}

unsigned char *
CL_UpscaleToRGBA(const unsigned char *data, size_t w, size_t h,
		const unsigned *table)
{
	/* R_Convert8to32 allocates the RGBA buffer; the caller owns it */
	return (unsigned char *)R_Convert8to32(data, w, h, table);
}

/* ---- the command's decisions ---- */

static const upscale_kind_t upscale_kinds[UPSCALE_KINDS] = {
	{ "textures", "wal", NULL, 1, 1, 0 },
	{ "env", "pcx", NULL, 0, 0, 0 },
	{ "models", "pcx", NULL, 0, 0, 1 },
	{ "players", "pcx", "_i.pcx", 0, 0, 1 }
};

const upscale_kind_t *
CL_UpscaleKind(int k)
{
	if ((k < 0) || (k >= UPSCALE_KINDS))
	{
		return NULL;
	}

	return &upscale_kinds[k];
}

/* 1 when name ends with suffix, ignoring case */
static int
CL_UpscaleEndsWith(const char *name, const char *suffix)
{
	size_t n = strlen(name), s = strlen(suffix);

	return (n >= s) && !Q_stricmp(name + n - s, suffix);
}

int
CL_UpscaleIsSource(const upscale_kind_t *kind, const char *name)
{
	size_t d, e, n;

	if (!kind || !name)
	{
		return 0;
	}

	d = strlen(kind->dir);
	e = strlen(kind->ext);
	n = strlen(name);

	/* "dir/", at least one character, '.' and ext */
	if ((n < d + 1 + 1 + 1 + e) || Q_strncasecmp(name, kind->dir, (int)d) ||
		(name[d] != '/') || (name[n - e - 1] != '.') ||
		Q_stricmp(name + n - e, kind->ext))
	{
		return 0;
	}

	return !kind->notsuffix || !CL_UpscaleEndsWith(name, kind->notsuffix);
}

int
CL_UpscaleListAdd(strlist_t *list, const char *name, int max, int *cut)
{
	if (StrList_Contains(list, name))
	{
		return 1;
	}

	if (list->num >= max)
	{
		*cut = 1;
		return 0;
	}

	StrList_Append(list, name);

	return 1;
}

int
CL_UpscaleListDir(strlist_t *dirs, const char *name, int *cut, int *refused)
{
	if (!CL_UpscaleNameOk(name))
	{
		(*refused)++;
		return 1;
	}

	return CL_UpscaleListAdd(dirs, name, UPSCALE_MAX_DIRS, cut);
}

int
CL_UpscaleOutPath(const char *gamedir, const char *outname, char *out,
		size_t outsize)
{
	if (!out || (outsize == 0))
	{
		return 0;
	}

	out[0] = '\0';

	if (!gamedir || !outname ||
		(strlen(gamedir) + 1 + strlen(outname) >= outsize))
	{
		return 0;
	}

	snprintf(out, outsize, "%s/%s", gamedir, outname);

	return 1;
}

int
CL_UpscaleTempPath(const char *path, long id, char *out, size_t outsize)
{
	int len;

	if (!out || (outsize == 0))
	{
		return 0;
	}

	out[0] = '\0';

	if (!path)
	{
		return 0;
	}

	len = snprintf(out, outsize, "%s.%ld.tmp", path, id);

	if ((len < 0) || ((size_t)len >= outsize))
	{
		out[0] = '\0';
		return 0;
	}

	return 1;
}

int
CL_UpscaleParentDir(const char *path, char *out, size_t outsize)
{
	const char *slash;
	size_t len;

	if (!out || (outsize == 0))
	{
		return 0;
	}

	out[0] = '\0';
	slash = path ? strrchr(path, '/') : NULL;

	if (!slash)
	{
		return 0;
	}

	len = (slash == path) ? 1 : (size_t)(slash - path);

	if (len >= outsize)
	{
		return 0;
	}

	memcpy(out, path, len);
	out[len] = '\0';

	return 1;
}

int
CL_UpscaleDirExists(const char *path)
{
	char dir[MAX_OSPATH];
	struct stat st;

	return CL_UpscaleParentDir(path, dir, sizeof(dir)) &&
		(stat(dir, &st) == 0) && S_ISDIR(st.st_mode);
}

upscale_action_t
CL_UpscaleAction(upscale_entry_t entry, int force)
{
	switch (entry)
	{
		case UPSCALE_ENTRY_NONE:
			return UPSCALE_WRITE;
		case UPSCALE_ENTRY_FILE:
			return force ? UPSCALE_WRITE : UPSCALE_SKIP;
		case UPSCALE_ENTRY_OTHER:
			/* written through, a link would write its target */
			return force ? UPSCALE_REFUSE : UPSCALE_SKIP;
		default:
			return UPSCALE_REFUSE;
	}
}

int
CL_UpscaleCachedInGitTree(upscale_gitcache_t *cache, const char *dir,
		int (*check)(const char *dir))
{
	size_t len;

	if (!cache || !dir || !check)
	{
		return 1;
	}

	if (cache->valid && !strcmp(cache->dir, dir))
	{
		return cache->answer;
	}

	len = strlen(dir);

	if (len >= sizeof(cache->dir))
	{
		return check(dir);
	}

	memcpy(cache->dir, dir, len + 1);
	cache->answer = check(dir);
	cache->valid = 1;

	return cache->answer;
}

int
CL_UpscalePathInGitTree(upscale_gitcache_t *cache, const char *path)
{
	char dir[MAX_OSPATH];

	if (!CL_UpscaleParentDir(path, dir, sizeof(dir)))
	{
		return UPSCALE_GIT_UNKNOWN;
	}

	return CL_UpscaleCachedInGitTree(cache, dir, CL_UpscaleDirGit);
}

int
CL_UpscaleAllowed(int connected, int local, int demo)
{
	return !demo && (!connected || local);
}

int
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

	/* a new file: not one that is there, and never through a link */
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

	/* on disk before it takes the name */
	ok = (done == len) && (fsync(fd) == 0);

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

upscale_outcome_t
CL_UpscaleWriteOut(const char *path, int force, const upscale_steps_t *steps,
		void *ctx, size_t *bytes)
{
	upscale_entry_t entry;
	upscale_outcome_t made;
	unsigned char *data = NULL;
	size_t len = 0;
	int git;

	*bytes = 0;
	entry = steps->entry(ctx, path);

	switch (CL_UpscaleAction(entry, force))
	{
		case UPSCALE_SKIP:
			return UPSCALE_SKIPPED;
		case UPSCALE_WRITE:
			break;
		default:
			steps->say(ctx, 0, path, (entry == UPSCALE_ENTRY_OTHER) ?
				UPSCALE_SAY_NOT_FILE : UPSCALE_SAY_UNKNOWN);
			return UPSCALE_REFUSED;
	}

	if (!steps->makedirs(ctx, path))
	{
		steps->say(ctx, 0, path, UPSCALE_SAY_NO_DIR);
		return UPSCALE_FAILED;
	}

	/* before anything is read: a file in a work tree is not even made */
	git = steps->git(ctx, path);

	if (git == UPSCALE_GIT_INSIDE)
	{
		steps->say(ctx, 1, path, UPSCALE_SAY_GIT);
		return UPSCALE_REFUSED;
	}

	if (git != UPSCALE_GIT_OUTSIDE)
	{
		steps->say(ctx, 0, path, UPSCALE_SAY_NO_REALPATH);
		return UPSCALE_FAILED;
	}

	made = steps->make(ctx, &data, &len);

	if ((made != UPSCALE_WRITTEN) || !data)
	{
		free(data);
		return (made == UPSCALE_REFUSED) ? UPSCALE_REFUSED : UPSCALE_FAILED;
	}

	if (!steps->write(ctx, path, data, len))
	{
		free(data);
		steps->say(ctx, 0, path, UPSCALE_SAY_WRITE);
		return UPSCALE_FAILED;
	}

	free(data);
	*bytes = len;

	return UPSCALE_WRITTEN;
}

void
CL_UpscaleCount(upscale_count_t *n, upscale_outcome_t outcome, size_t bytes)
{
	switch (outcome)
	{
		case UPSCALE_WRITTEN:
			n->written++;
			n->bytes += bytes;
			break;
		case UPSCALE_SKIPPED:
			n->skipped++;
			break;
		case UPSCALE_REFUSED:
			n->refused++;
			break;
		default:
			n->failed++;
			break;
	}
}

void
CL_UpscaleSummary(const upscale_count_t *n, int factor,
		upscale_backend_t backend, const char *dir, char *out, size_t outsize)
{
	(void)backend;

	char dirs[96] = "";

	if (n->baddirs > 0)
	{
		snprintf(dirs, sizeof(dirs), "; %d director%s with a refused name "
			"not looked at", n->baddirs, (n->baddirs == 1) ? "y" : "ies");
	}

	snprintf(out, outsize, "upscale_textures: %d written, %d skipped, "
		"%d refused, %d failed, %llu bytes, %dx, in %s%s%s\n", n->written,
		n->skipped, n->refused, n->failed, n->bytes, factor, dir, dirs,
		n->cut ? "; a listing was cut at its limit, not every file was "
		"looked at" : "");
}
