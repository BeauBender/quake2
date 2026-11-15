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

/* The largest side of a source image; 4096 after 4x. */
#define UPSCALE_MAX_SIDE 1024

/* The most parent directories looked at for a .git entry. */
#define UPSCALE_GIT_DEPTH 64

/*
 * 1 when a name from a listing may be written: only the characters
 * a-z, A-Z, 0-9, '_', '/', '.' and '-' (an upper-case letter is kept
 * as it is, as the pak names it), not empty, no leading '/', no "..",
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

/* CL_UpscaleInGitTree of dir's real path on disk; 1 when it has none */
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

/* scale2x of src/client/refresh/files/image.c: w by h to 2w by 2h */
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

/* R_FloodFillSkin of src/client/refresh/files/image.c, for a skin */
void CL_UpscaleFloodFillSkin(unsigned char *skin, int w, int h,
		const unsigned *table);

/*
 * The 256 colours of palette (768 bytes of RGB) as the renderers' 8-bit
 * table: RGB and alpha 255 in memory order, index 255 alpha 0.
 */
void CL_UpscaleTable(const unsigned char *palette, unsigned *table);

/*
 * The indices data (w by h) as RGBA8 into rgba, as R_Convert8to32 of
 * src/client/refresh/files/image.c converts them: index 255 keeps alpha
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

/* the upscale_textures console command */
void CL_UpscaleTextures_f(void);

#endif /* CL_HEADER_UPSCALE_H */
