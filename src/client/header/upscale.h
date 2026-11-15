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
 * The local texture upscaler (upscale_textures): the parts the unit
 * tests run (src/client/cl_upscale.c) and the console command
 * (src/client/cl_upscale_cmd.c).
 *
 * =======================================================================
 */

#ifndef CL_HEADER_UPSCALE_H
#define CL_HEADER_UPSCALE_H

#include <stddef.h>

/* after header/client.h: strlist_t, MAX_OSPATH */

/* The largest side of a source image; 4096 after 4x. */
#define UPSCALE_MAX_SIDE 1024

/* The most parent directories looked at for a .git entry. */
#define UPSCALE_GIT_DEPTH 64

/*
 * 1 when a name from a listing may be written: only the characters
 * a-z, A-Z, 0-9, '_', '-', '+' (the animation prefix), '/' and '.' (an
 * upper-case letter is kept as it is, as the pak names it), not empty,
 * no leading '/', no "..",
 * no component that is empty or begins with '.'. 0 otherwise, NULL
 * included.
 */
int CL_UpscaleNameOk(const char *name);

/*
 * The name written for name: name with its extension (after the last
 * '.' of its last component) replaced by png. 1 when name passes
 * CL_UpscaleNameOk, has an extension and the result, with its
 * terminator, fits both outsize and MAX_QPATH; else 0 and out is "".
 */
int CL_UpscaleOutName(const char *name, char *out, size_t outsize);

/*
 * 1 when the absolute directory dir is inside a git work tree: some
 * directory from dir up to "/" holds an entry named .git, as exists
 * says. Also 1, fail closed, when dir is not absolute, is longer than
 * a path buffer, or is not walked to "/" in UPSCALE_GIT_DEPTH + 1 looks
 * (more than UPSCALE_GIT_DEPTH components).
 */
int CL_UpscaleInGitTree(const char *dir,
		int (*exists)(const char *path, void *ctx), void *ctx);

/* exists for CL_UpscaleInGitTree: lstat finds path, or cannot tell */
int CL_UpscaleEntryExists(const char *path, void *ctx);

/* what the git work-tree check says of a directory */
typedef enum
{
	UPSCALE_GIT_OUTSIDE,	/* not inside a git work tree */
	UPSCALE_GIT_INSIDE,	/* inside one, or not walked to "/" */
	UPSCALE_GIT_UNKNOWN	/* no real path: missing, or not resolved */
} upscale_git_t;

/* CL_UpscaleInGitTree of dir's real path on disk, as upscale_git_t */
int CL_UpscaleDirGit(const char *dir);

/* 1 unless CL_UpscaleDirGit says UPSCALE_GIT_OUTSIDE */
int CL_UpscaleDirInGitTree(const char *dir);

/* 1 when a source of w by h may be scaled: 1 to UPSCALE_MAX_SIDE each */
int CL_UpscaleSizeOk(long long w, long long h);

/*
 * The w by h image src with one texel of its own opposite edges around
 * it, as a repeating tile has them: (w + 2) by (h + 2) into dst. 0, and
 * nothing written, when a side is below 1.
 */
int CL_UpscaleWrapPad(const unsigned char *src, int w, int h,
		unsigned char *dst);

/*
 * The w by h block at (x, y) of src, which is sw by sh, into dst. 0, and
 * nothing written, unless the block lies inside src and is not empty.
 */
int CL_UpscaleCrop(const unsigned char *src, int sw, int sh, int x, int y,
		int w, int h, unsigned char *dst);

/* scale2x of src/client/refresh/files/imgscale.c: w by h to 2w by 2h */
void CL_UpscaleScale2x(const unsigned char *src, unsigned char *dst,
		int w, int h);

/*
 * The palette indices src (w by h) scaled factor times (2 or 4) with
 * scale2x, once per doubling. wrap: wrap-padded before each pass and
 * cropped after, so a tile stays seamless; else the edges are clamped
 * (scale2x's own). A new buffer the caller frees, NULL when the size
 * or factor is refused or memory runs out.
 */
unsigned char *CL_UpscaleIndices(const unsigned char *src, int w, int h,
		int factor, int wrap);

/* R_FloodFillSkin of src/client/refresh/files/imgscale.c, for a skin */
void CL_UpscaleFloodFillSkin(unsigned char *skin, int w, int h,
		const unsigned *table);

/*
 * The 256 colours of palette (768 bytes of RGB) as the renderers' 8-bit
 * table: RGB and alpha 255 in memory order, index 255 alpha 0.
 */
void CL_UpscaleTable(const unsigned char *palette, unsigned *table);

/*
 * The indices data (w by h) as RGBA8 into rgba, as R_Convert8to32 of
 * src/client/refresh/files/imgscale.c converts them: index 255 keeps alpha
 * 0 and takes the colour of a neighbour that is not 255.
 */
void CL_UpscaleToRGBA(const unsigned char *data, size_t w, size_t h,
		const unsigned *table, unsigned char *rgba);

/*
 * The first mip level of a .wal file of len bytes: 1 with its size and
 * the offset of its w * h indices when the header fits, the size
 * passes CL_UpscaleSizeOk and the pixels lie inside the file; else 0.
 */
int CL_UpscaleWalPixels(const unsigned char *data, size_t len, int *w,
		int *h, size_t *offset);

/* The size of a .pcx file's image from its header, as CL_UpscaleWalPixels. */
int CL_UpscalePcxSize(const unsigned char *data, size_t len, int *w, int *h);

/*
 * The 768 bytes of RGB at the end of a .pcx colormap of len bytes, as
 * PCX_Decode takes them: when the header passes CL_UpscalePcxSize (one
 * plane of 8 bits) and the palette lies after the header (len > 768
 * and more); else NULL.
 */
const unsigned char *CL_UpscalePalette(const unsigned char *data, size_t len);

/*
 * 1 when a decoded .pcx is what its header said: pic, 8-bit indices,
 * pw by ph equal to w by h.
 */
int CL_UpscalePcxDecodedOk(const unsigned char *pic, int bits, int pw,
		int ph, int w, int h);

/* ---- the command's decisions; cl_upscale_cmd.c does the I/O ---- */

/* What is scaled: the names below dir with the extension ext. */
typedef struct
{
	const char *dir;
	const char *ext;
	const char *notsuffix;	/* names that end so are not sources */
	int wal;		/* a .wal; else an 8-bit .pcx */
	int wrap;		/* a repeating world texture: seamless */
	int skin;		/* flood-filled first, as the renderers fill skins */
} upscale_kind_t;

#define UPSCALE_KINDS 4

/* The kind k, 0 to UPSCALE_KINDS - 1; NULL for any other k. */
const upscale_kind_t *CL_UpscaleKind(int k);

/*
 * 1 when name is a source of kind: it begins with kind's dir and a '/',
 * ends with '.' and kind's ext and not with kind's notsuffix (case
 * ignored), so a player's _i.pcx icon is not one.
 */
int CL_UpscaleIsSource(const upscale_kind_t *kind, const char *name);

/* the most directories walked and names taken from the listings */
#define UPSCALE_MAX_DIRS 4096
#define UPSCALE_MAX_FILES 65536

/*
 * Appends name to list unless it is there: 1 when it is in the list
 * afterwards. 0, and *cut set to 1, when list already holds max names.
 */
int CL_UpscaleListAdd(strlist_t *list, const char *name, int max, int *cut);

/*
 * A loose directory name from a listing, added to dirs to be walked
 * (CL_UpscaleListAdd, at most UPSCALE_MAX_DIRS): 1 when the walk goes
 * on. A name CL_UpscaleNameOk refuses is not walked and is counted in
 * *refused, so the summary says its files were not looked at.
 */
int CL_UpscaleListDir(strlist_t *dirs, const char *name, int *cut,
		int *refused);

/*
 * "gamedir/outname" into out: 1 when it fits outsize, else 0 and out
 * is "".
 */
int CL_UpscaleOutPath(const char *gamedir, const char *outname, char *out,
		size_t outsize);

/*
 * The temporary name a file is written to before it is renamed to
 * path, in the same directory: "path.<id>.tmp". 1 when it fits outsize,
 * else 0 and out is "".
 */
int CL_UpscaleTempPath(const char *path, long id, char *out, size_t outsize);

/*
 * The directory of path, up to its last '/' ("/" for a file in the
 * root): 1 when path has a '/' and the directory fits outsize, else 0
 * and out is "".
 */
int CL_UpscaleParentDir(const char *path, char *out, size_t outsize);

/* what lstat finds at a final name: a link is not followed */
typedef enum
{
	UPSCALE_ENTRY_NONE,	/* no such entry */
	UPSCALE_ENTRY_FILE,	/* a regular file */
	UPSCALE_ENTRY_OTHER,	/* a symbolic link, a directory, ... */
	UPSCALE_ENTRY_UNKNOWN	/* lstat cannot tell */
} upscale_entry_t;

upscale_entry_t CL_UpscaleEntryKind(const char *path);

/* 1 when the directory of the file path (CL_UpscaleParentDir) is one */
int CL_UpscaleDirExists(const char *path);

typedef enum
{
	UPSCALE_SKIP,
	UPSCALE_WRITE,
	UPSCALE_REFUSE
} upscale_action_t;

/*
 * What is done with a final name: nothing there is written; a file
 * there is skipped, or written again with force; anything else there
 * (a link, a directory) is skipped, and with force refused, never
 * followed; when lstat cannot tell, refused.
 */
upscale_action_t CL_UpscaleAction(upscale_entry_t entry, int force);

/* the answer of the git work-tree check for the last directory asked */
typedef struct
{
	char dir[MAX_OSPATH];
	int answer;
	int valid;
} upscale_gitcache_t;

/*
 * check(dir), from cache when dir is the directory asked last; else
 * check is called and its answer kept for dir. A dir that does not fit
 * the cache is checked every time.
 */
int CL_UpscaleCachedInGitTree(upscale_gitcache_t *cache, const char *dir,
		int (*check)(const char *dir));

/*
 * CL_UpscaleDirGit of the directory of the file path (cached as above):
 * UPSCALE_GIT_UNKNOWN when path has no directory. The file is written
 * only on UPSCALE_GIT_OUTSIDE.
 */
int CL_UpscalePathInGitTree(upscale_gitcache_t *cache, const char *path);

/*
 * 1 when the command may run: no demo plays (demo, cl.attractloop), and
 * the client is not connected to a server or is connected to the local
 * one. This stops the command when a connected remote server or a demo
 * sends it as stufftext, and nothing more: stufftext is not filtered
 * (by design), so a server that has the client disconnect first can
 * still start it.
 */
int CL_UpscaleAllowed(int connected, int local, int demo);

typedef enum
{
	UPSCALE_WRITTEN,
	UPSCALE_SKIPPED,
	UPSCALE_REFUSED,
	UPSCALE_FAILED
} upscale_outcome_t;

typedef struct
{
	int written, skipped, refused, failed;
	unsigned long long bytes;	/* of the files written */
	int cut;	/* a listing stopped at its limit */
	int baddirs;	/* loose directories not walked: name refused */
} upscale_count_t;

/*
 * len bytes of data into path: written to a new temporary name in the
 * same directory (CL_UpscaleTempPath with the process id; O_EXCL, a
 * link never followed), synced, closed, then renamed over path, so
 * path is either what it was or all of the new file, and a link at
 * path is replaced, not written through. The temporary file is removed
 * on any failure after it was made; one that was there before is not
 * touched. 1 on success; always 0 on Windows.
 */
int CL_UpscaleWriteFile(const char *path, const unsigned char *data,
		size_t len);

/* the messages of CL_UpscaleWriteOut, after the path */
#define UPSCALE_SAY_NOT_FILE "a link or not a regular file is there, left as it is"
#define UPSCALE_SAY_UNKNOWN "lstat cannot tell what is there, left as it is"
#define UPSCALE_SAY_NO_DIR "its directory cannot be made, nothing written"
#define UPSCALE_SAY_GIT "its directory is inside a git work tree, refused"
#define UPSCALE_SAY_NO_REALPATH "its directory has no real path, nothing written"
#define UPSCALE_SAY_WRITE "cannot be written"

/* the steps of one output file; cl_upscale_cmd.c has the real ones */
typedef struct
{
	upscale_entry_t (*entry)(void *ctx, const char *path);
	/* creates path's directories: 1 when its directory is there after */
	int (*makedirs)(void *ctx, const char *path);
	/* path's directory, as upscale_git_t */
	int (*git)(void *ctx, const char *path);
	/* loads and scales the source: on UPSCALE_WRITTEN the PNG, freed with free */
	upscale_outcome_t (*make)(void *ctx, unsigned char **data, size_t *len);
	int (*write)(void *ctx, const char *path, const unsigned char *data,
			size_t len);
	/* a message about path, one of UPSCALE_SAY_*; developer: only then */
	void (*say)(void *ctx, int developer, const char *path, const char *text);
} upscale_steps_t;

/*
 * One output file at path, in this order: what is there
 * (CL_UpscaleAction: skipped, refused, or on), its directories made
 * (failed when it is not there after), the git check of its directory
 * (refused inside a work tree, failed without a real path), the PNG
 * made (refused or failed as make says), then written (failed when the
 * write fails). Nothing is made or written after a step that stops.
 * *bytes is the PNG's size when written, else 0.
 */
upscale_outcome_t CL_UpscaleWriteOut(const char *path, int force,
		const upscale_steps_t *steps, void *ctx, size_t *bytes);

/* counts one file's outcome; bytes count only when it was written */
void CL_UpscaleCount(upscale_count_t *n, upscale_outcome_t outcome,
		size_t bytes);

/* the summary line of a run, with its newline, into out */
void CL_UpscaleSummary(const upscale_count_t *n, int factor,
		const char *dir, char *out, size_t outsize);

/* the upscale_textures console command */
void CL_UpscaleTextures_f(void);

#endif /* CL_HEADER_UPSCALE_H */
