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

/*
 * The border, in texels, a world texture is wrap-padded with before the
 * metalfx or vt pass (CL_UpscaleRGBA, shared by both; vt's is measured in
 * docs/runbook/TASK167.md). MetalFX's spatial filter reads more than one texel: a
 * probe on the M6 GPU found a one texel border leaves a seam (up to 35
 * levels at 2x, 74 at 4x), and 2, 3 and 4 match 8 exactly. 4 leaves the
 * filters of other GPUs headroom. scale2x reads one texel and keeps 1.
 */
#define UPSCALE_METALFX_BORDER 4

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
 * it, as a repeating tile has them: (w + 2) by (h + 2) into dst. The
 * scale2x path uses this form. 0, and
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
 * The indices data (w by h) as RGBA8 in a malloc'd buffer of w * h * 4
 * bytes that the caller frees, or NULL when it cannot be made, as
 * R_Convert8to32 of
 * src/client/refresh/files/imgscale.c converts them: index 255 keeps alpha
 * 0 and takes the colour of a neighbour that is not 255.
 */
unsigned char *CL_UpscaleToRGBA(const unsigned char *data, size_t w,
		size_t h, const unsigned *table);

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

/* ---- the backends (TASK124) ---- */

/* what scales a tile */
typedef enum
{
	UPSCALE_SCALE2X,	/* scale2x of the indices: the default */
	UPSCALE_METALFX,	/* MetalFX's spatial scaler of the RGBA (macOS) */
	UPSCALE_VT		/* VideoToolbox's super-resolution scaler of the RGBA (macOS 26, TASK167) */
} upscale_backend_t;

#define UPSCALE_USAGE "usage: upscale_textures <2|4> [scale2x|metalfx|vt] [force]\n"

/* "scale2x", "metalfx" or "vt"; NULL for any other value */
const char *CL_UpscaleBackendName(int backend);

/* 1 for a backend that scales the RGBA through CL_UpscaleRGBA (metalfx, vt) */
int CL_UpscaleBackendRGBA(int backend);

/*
 * The command's words, argv[0] its name: <2|4>, then a backend name,
 * then force, the last two optional and in that order. 1 with *factor,
 * *backend (UPSCALE_SCALE2X when none is given) and *force set; 0, and
 * nothing set, for any other line.
 */
int CL_UpscaleParseArgs(int argc, const char *const *argv, int *factor,
		upscale_backend_t *backend, int *force);

/*
 * The backend a run uses: asked when it is scale2x or supported is 1;
 * else scale2x, and line is the one report line that says so, with the
 * reason and a newline. line is "" when nothing is to be said.
 */
upscale_backend_t CL_UpscaleChooseBackend(upscale_backend_t asked,
		int supported, const char *reason, char *line, size_t linesize);

/*
 * The vt backend's support checks (TASK167), as cl_upscale_vt.m reads
 * them before a run; no check downloads anything.
 */
typedef enum
{
	UPSCALE_VT_MODEL_DOWNLOAD,	/* the operating system has to fetch it */
	UPSCALE_VT_MODEL_DOWNLOADING,	/* a fetch is under way */
	UPSCALE_VT_MODEL_READY		/* on this machine */
} upscale_vt_model_t;

typedef struct
{
	int built;	/* cl_upscale_vt.m is built here (the macOS client) */
	int os;		/* the OS has VTSuperResolutionScalerConfiguration */
	int supported;	/* its isSupported */
	int factor;	/* its supportedScaleFactors has the run's factor */
	int config;	/* a configuration at the run's factor was made */
	int model;	/* that configuration's model status, upscale_vt_model_t */
} upscale_vt_facts_t;

typedef enum
{
	UPSCALE_VT_UNSUPPORTED,	/* scale2x, with the reason */
	UPSCALE_VT_READY,	/* the run may scale */
	UPSCALE_VT_FETCH	/* the model is to be fetched first */
} upscale_vt_state_t;

/*
 * What a vt run at factor does with facts, checked in the struct's
 * order: unsupported with a one-line reason in reason ("" otherwise),
 * ready, or fetch the model first (it is missing or being fetched).
 */
upscale_vt_state_t CL_UpscaleVTState(const upscale_vt_facts_t *facts,
		int factor, char *reason, size_t size);

/* said once before the model is fetched; nothing is fetched without vt */
#define UPSCALE_SAY_VT_FETCH "upscale_textures: vt: the operating system " \
	"will now download VideoToolbox's super-resolution model (once, from " \
	"Apple)\n"

/* the longest wait for the model's download, in seconds */
#define UPSCALE_VT_FETCH_SECONDS 900

/* CL_UpscaleWrapPad and CL_UpscaleCrop of RGBA8 texels (4 bytes each) */
int CL_UpscaleWrapPadRGBA(const unsigned char *src, int w, int h,
		unsigned char *dst);
int CL_UpscaleWrapPadRGBABorder(const unsigned char *src, int w, int h, int k,
		unsigned char *dst);
int CL_UpscaleCropRGBA(const unsigned char *src, int sw, int sh, int x, int y,
		int w, int h, unsigned char *dst);

/*
 * The alpha of rgba, w * factor by h * factor texels, from the source
 * indices idx (w by h) by nearest neighbour: 0 where the index is 255
 * (the transparent index), 255 elsewhere. The colour is not touched.
 */
void CL_UpscaleMaskAlpha(const unsigned char *idx, int w, int h, int factor,
		unsigned char *rgba);

/* scales the RGBA8 in (iw by ih) into out (ow by oh): 1 on success */
typedef int (*upscale_scaler_t)(void *ctx, const unsigned char *in, int iw,
		int ih, unsigned char *out, int ow, int oh);

/*
 * The indices idx (w by h) scaled factor times (2 or 4) by an RGBA
 * scaler, as the metalfx backend does it: converted with table
 * (CL_UpscaleToRGBA), wrap-padded by UPSCALE_METALFX_BORDER texels when wrap, scaled
 * in one pass to factor times the padded size, cropped by border times
 * factor texels on each side when wrap, and the alpha set from the indices
 * (CL_UpscaleMaskAlpha), whatever the scaler made of it. A new buffer
 * of w * factor by h * factor RGBA8 texels the caller frees; NULL when
 * the size or factor is refused, memory runs out or scale fails.
 */
unsigned char *CL_UpscaleRGBA(const unsigned char *idx, int w, int h,
		int factor, int wrap, const unsigned *table, upscale_scaler_t scale,
		void *ctx);

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

/*
 * The summary line of a run, with its newline, into out. The backend is
 * named when it is not scale2x ("2x with metalfx"), so a scale2x run
 * says what it said before there were backends.
 */
void CL_UpscaleSummary(const upscale_count_t *n, int factor,
		upscale_backend_t backend, const char *dir, char *out,
		size_t outsize);

/* the upscale_textures console command */
void CL_UpscaleTextures_f(void);

#endif /* CL_HEADER_UPSCALE_H */
