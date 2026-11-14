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
 * The back end of ref_metal in C: the only door to Metal
 * (docs/plans/ref-metal-native.md, sections 2 and 3). The rmb_*
 * functions are implemented in Objective-C (rmb_device.m, rmb_encode.m);
 * the rm_* functions below them are plain C with neither SDL nor Metal
 * (rm_pure.c) and are host-tested by tests/metal/rm_host_test.c. No
 * Objective-C type and no system header beyond the C library appears
 * here.
 *
 * =======================================================================
 */

#ifndef RM_RMB_H
#define RM_RMB_H

#include <stddef.h>

/* ---- capabilities ----------------------------------------------------------- */

/* The bit of Apple GPU family n (1 to 9) in rmb_caps_t.families. */
#define RMB_FAMILY_BIT(n) (1u << ((n) - 1))
#define RMB_FAMILY_FIRST 1
#define RMB_FAMILY_LAST 9
/* ref_metal refuses a device below this family (Init fails). */
#define RMB_FAMILY_FLOOR 5

typedef struct
{
	char device[128];
	unsigned families;          /* RMB_FAMILY_BIT(n) for every apple n the device supports */
	int raytracing;             /* supportsRaytracing */
	int raytracing_render;      /* supportsRaytracingFromRender (macOS 14) */
} rmb_caps_t;

/* ---- the object ledger and the counters ------------------------------------- */

typedef enum
{
	RM_OBJ_DEVICE,
	RM_OBJ_QUEUE,
	RM_OBJ_VIEW,
	RM_OBJ_TEXTURE,
	RM_OBJ_BUFFER,
	RM_OBJ_KINDS
} rm_obj_kind_t;

typedef struct
{
	unsigned long created[RM_OBJ_KINDS];
	unsigned long destroyed[RM_OBJ_KINDS];
	unsigned long unmatched;    /* destroys without a create: a bookkeeping bug */
	unsigned long unreleased;   /* released by ref_metal but still alive afterwards (weak check) */
} rm_ledger_t;

typedef struct
{
	unsigned long frames;       /* frames ended by rmb_frame_end */
	unsigned long no_drawable;  /* of those, frames without a drawable (not presented) */
	unsigned long skipped;      /* frames skipped: no frame slot within the wait */
	unsigned long failed;       /* command buffers that ended in error */
	unsigned long readbacks;    /* successful rmb_read calls */
	unsigned long noop_calls;   /* calls to entry points not implemented yet (front end) */
	unsigned long draws;        /* draws encoded */
	unsigned long uploads;      /* texture uploads blitted */
	unsigned long upload_skips; /* uploads refused by the size check or a failed allocation */
	unsigned long draw_skips;   /* draws refused: no texture, no pipeline, no ring space */
	unsigned long splits;       /* frame command buffers split for a rewritten texture (section 3) */
	unsigned long pipelines;    /* pipeline states built in InitContext */
	unsigned long late_pipelines;   /* pipeline states built after InitContext: must stay 0 */
} rmb_stats_t;

/* A buffer of this size holds every metal_stats line (rm_stats_format, host-tested). */
#define RM_STATS_LINE 512

/* ---- rm_pure.c: no SDL, no Metal --------------------------------------------- */

/* 1 when the families include RMB_FAMILY_FLOOR or a later one. */
int rm_caps_floor_ok(unsigned families);

/*
 * Writes the names of the families 5 to 9 the mask holds ("apple5
 * apple7"), or "none", NUL-terminated. Returns 1 when it fits in len,
 * 0 when it was truncated (or len is 0).
 */
int rm_caps_family_list(unsigned families, char *buf, size_t len);

/*
 * BGRA rows top down (Metal's texture origin), src_stride bytes apart,
 * to tightly packed RGB rows top down: what gl1's R_ScreenShot hands to
 * ri.Vid_WriteScreenshot after its row swap.
 */
void rm_bgra_to_rgb(const unsigned char *bgra, size_t src_stride, int w, int h, unsigned char *rgb);

void rm_ledger_create(rm_ledger_t *l, rm_obj_kind_t kind);
void rm_ledger_destroy(rm_ledger_t *l, rm_obj_kind_t kind);
unsigned long rm_ledger_live(const rm_ledger_t *l);

/* The teardown line, without a newline. Returns 1 when it fits, 0 when truncated. */
int rm_ledger_format(const rm_ledger_t *l, char *buf, size_t len);

/* The metal_stats line, without a newline. Returns 1 when it fits, 0 when truncated. */
int rm_stats_format(const rmb_stats_t *s, const rm_ledger_t *l, char *buf, size_t len);

typedef enum
{
	RM_LOAD_CLEAR,   /* cleared to the pass's clear colour */
	RM_LOAD_KEEP,    /* loaded: what the target holds */
	RM_LOAD_BLACK    /* cleared to black: no pass has written the target yet */
} rm_load_t;

/*
 * The colour load of a pass: clear is 1 when the pass clears (r_clear at
 * the start of the frame), defined is 1 when a pass has written the
 * colour target since it was made.
 */
rm_load_t rm_color_load(int clear, int defined);

/*
 * 1 when rmb_frame_begin must (re)make the colour and depth targets for
 * a w by h frame: either is missing (a failed allocation is retried on
 * the next frame) or the colour target is cw by ch, another size.
 */
int rm_targets_rebuild(int have_color, int have_depth, int cw, int ch, int w, int h);

/* ---- images, the 2D draws (rm_pure.c) ------------------------------------------- */

/*
 * The bytes of a w by h rgba8 upload. 1 with *bytes set when w and h are
 * positive, neither is above RMB_MAX_TEXTURE_SIDE and w * h * 4 fits in
 * cap (the staging ring, RMB_STAGING_BYTES), 0 otherwise: no upload of
 * that size is staged. The product is checked for overflow before it is
 * formed.
 */
int rm_upload_bytes(long w, long h, size_t cap, size_t *bytes);

/*
 * gl1's RDraw_StretchRaw, 8-bit frame (non-power-of-two path): each of
 * cols * rows palette indices becomes its rgba8 entry of palette (r_rawpalette,
 * gl1's byte order: r, g, b, 255 in memory), at the frame's own size, into
 * out, which holds out_len bytes. 0 and nothing written when cols or rows is
 * not positive or out cannot hold cols * rows * 4 bytes.
 */
int rm_raw8_to_rgba(const unsigned char *data, int cols, int rows, const unsigned *palette,
		unsigned char *out, size_t out_len);

/*
 * The triangles of a fan of n vertices (GL_TRIANGLE_FAN, the shim's order:
 * 0, i, i + 1 for i from 1 to n - 2) into idx, which holds cap entries.
 * Returns the number of indices, 0 when n < 3 or they do not fit.
 */
int rm_fan_indices(int n, unsigned short *idx, int cap);

/* What a 2D draw does with its texture (the shim's texture environments). */
typedef enum
{
	RM_TEXENV_REPLACE,
	RM_TEXENV_MODULATE,
	RM_TEXENV_UNTEXTURED,   /* the vertex colour only */
	RM_TEXENVS
} rm_texenv_t;

/*
 * The index of a 2D pipeline in the table InitContext builds: texture
 * environment, gl1's alpha test (GL_GREATER 0.666) on or off, blending
 * (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) on or off. -1 when the texenv is
 * out of range.
 */
#define RM_PIPE_2D_COUNT (RM_TEXENVS * 2 * 2)
int rm_pipe_2d(rm_texenv_t env, int alpha_test, int blend);

/*
 * gl_texturemode (gl1's names, R_TextureMode in gl1_image.c) to the
 * minifying filter (0 to 5: GL_NEAREST, GL_LINEAR, then the four mipmap
 * filters in GL's enum order) and the magnifying one (0 nearest, 1
 * linear). 0 for a name gl1 does not know (nothing written).
 */
int rm_texture_mode(const char *name, int *min, int *mag_linear);

/*
 * The directory part of path ("." for a bare name) into dir, which holds
 * len bytes. 1 when it fits; 0, with dir empty when len > 0, otherwise.
 */
int rm_dir_of(const char *path, char *dir, size_t len);

/* ---- the scrap, pics and characters (rm_pure.c) ---------------------------------- */

/* gl1's scrap: four atlases of 256 by 256 texels, the first two without filtering */
#define RM_SCRAP_WIDTH 256
#define RM_SCRAP_HEIGHT 256
#define RM_MAX_SCRAPS 4
#define RM_MAX_SCRAPS_NOLERP 2

/*
 * gl1's CommonAllocBlock: a w by h block (the border already added) in an
 * atlas whose column heights are allocated[aw]. 1 with *x, *y the block's
 * corner and the columns raised; 0 and nothing written when it does not fit
 * or is larger than the atlas (gl1 computes aw - w unsigned there; a width
 * above aw is refused here). The spots are gl1's: the rightmost column
 * start is aw - w - 1.
 */
int rm_scrap_alloc(int *allocated, int aw, int ah, int w, int h, int *x, int *y);

/*
 * Copies a w by h block of texels (rows tight) to (x, y) of an atlas of
 * aw by ah texels. 0 and nothing written when the block is not inside it.
 */
int rm_scrap_copy(unsigned *texels, int aw, int ah, int x, int y, int w, int h, const unsigned *pic);

/*
 * Whether Scrap_Update writes an atlas: 1 when it is dirty, whether its
 * texture tex exists or not (a failed upload freed it; the update makes it
 * again with id 0, so a transient failure heals on a later draw).
 */
int rm_scrap_needs_upload(int dirty, int tex);

/*
 * The texture of atlas n in tex[count], 0 for an index outside it. A scrap
 * image keeps its atlas index and looks its texture up at each draw, so
 * an id freed by a failed upload, and taken by another texture, is never
 * drawn for it.
 */
int rm_scrap_texture(const int *tex, int count, int n);

/*
 * gl1's Scrap_Update filter of atlas n: nearest for the first
 * RM_MAX_SCRAPS_NOLERP atlases and for all under r_2D_unfiltered
 * (unfiltered), filtered without mipmaps otherwise and for an index
 * outside the table.
 */
int rm_scrap_sampler(int n, int unfiltered);

/* One 2D quad: corners and the texture coordinates of the corners (up left, down right). */
typedef struct
{
	float x0, y0, x1, y1;
	float s0, t0, s1, t1;
} rm_quad_t;

/*
 * gl1's RDraw_CharScaled: the cell of character num of a 16 by 16 sheet
 * whose image holds the coordinates st (sl, tl, sh, th). 0 for a space
 * and for a character wholly above the screen (y <= -8): nothing drawn.
 */
int rm_char_quad(rm_quad_t *q, int x, int y, int num, float scale, const float *st);

/* gl1's RDraw_PicScaled, RDraw_StretchPic: a pic of w by h at (x, y), scaled by factor. */
void rm_pic_quad(rm_quad_t *q, int x, int y, int w, int h, float factor, const float *st);

/*
 * gl1's RDraw_TileClear: the tile at offset (x2, y2) of an iw by ih pic
 * repeated over the box (x, y, w, h), the last column and row clipped to
 * the box with the texture coordinates cut in proportion. 0 when iw or ih
 * is not positive or the offset is outside the box.
 */
int rm_tile_quad(rm_quad_t *q, int x, int y, int w, int h, int iw, int ih,
		long x2, long y2, const float *st);

/*
 * Whether add more quads fit in a batch of cap with count in it: 1, or 0
 * for a negative or zero add, a count outside 0..cap, or a sum above cap
 * (checked without forming an overflowing sum).
 */
int rm_batch_fits(long count, long add, long cap);

/*
 * The indices of n quads of 4 vertices each (gl1's R_Buffer2DQuad: 0 1 2,
 * 0 2 3 of each quad) into idx, which holds cap entries. The number of
 * indices, 0 when n < 1, they do not fit or a vertex index would pass 65535.
 */
int rm_quad_indices(int n, unsigned short *idx, int cap);

/* gl1's glColor4f(r, g, b, 1) of RDraw_PicScaledCol, which modulates the texel. */
void rm_tint_color(float *rgba, const float *rgb);

/* ---- rmb_device.m and rmb_encode.m ------------------------------------------- */

/*
 * Frames in flight: the drawable count and frame semaphore of rmb_device.m
 * and the per-frame vertex rings of rmb_encode.m, one constant for both.
 */
#define RMB_FRAMES_IN_FLIGHT 3

/* The device and the queue, and the capabilities. 0 with err filled on failure. */
int rmb_open(rmb_caps_t *caps, char *err, size_t errlen);

/*
 * Takes the CAMetalLayer of the SDL Metal view (SDL_Metal_GetLayer, made
 * by rm_sdl.c, which owns the view; the back end includes no SDL).
 * 0 with err on failure.
 */
int rmb_attach(void *metal_layer, int vsync, char *err, size_t errlen);

/* Waits (bounded) for the frames on the GPU and releases the layer and the targets. */
void rmb_detach(void);

/* Counts an object the front end makes or releases (the SDL Metal view). */
void rmb_ledger_note(rm_obj_kind_t kind, int created);

/* Detaches, releases the device and the queue; fills the final counters. */
void rmb_close(rmb_stats_t *stats, rm_ledger_t *ledger);

void rmb_set_vsync(int on);
int rmb_vsync(void);

/* The counters so far. */
void rmb_stats(rmb_stats_t *stats, rm_ledger_t *ledger);

/*
 * Takes a frame slot and a command buffer, sizes the targets and the
 * layer's drawable to w by h pixels (the window's size in pixels), and
 * opens the frame's first pass, colour only: cleared to clear_rgba, or
 * loaded when it is NULL (rm_color_load: black on a target no pass has
 * written). This is gl1's R_Clear at the end of RI_BeginFrame. 0 when
 * the frame is skipped.
 */
int rmb_frame_begin(int w, int h, const float *clear_rgba);

/*
 * Ends the open pass and opens the 3D pass on the colour target and its
 * depth32Float_stencil8 attachment: colour loaded, depth cleared to 1,
 * stencil to 0. 0 when there is no open frame.
 */
int rmb_pass_3d(void);

/* Ends the 3D pass and opens the 2D pass: colour loaded, no depth. */
int rmb_pass_2d(void);

/*
 * Reads the colour target into bgra (rows top down, w * 4 bytes each,
 * w and h from rmb_target_size); len is the size of bgra. Inside a frame
 * it reads what was drawn so far, outside one the last frame. Returns 0
 * (nothing written) when the frame is skipped, there is no target, no
 * pass has written the target since it was made, bgra is too small or
 * the GPU did not finish within its wait.
 */
int rmb_read(unsigned char *bgra, size_t len);

/* The size of the colour target, 0 0 without one. */
void rmb_target_size(int *w, int *h);

/* Blits the frame to the drawable when there is one, presents and commits. */
void rmb_frame_end(void);

/*
 * gl1's mid-frame colour clear (RI_SetPalette clears to 0, 0, 0, 0): the
 * open pass is ended and a colour-only pass cleared to rgba opened.
 */
void rmb_clear(const float *rgba);

/* ---- rmb_resource.m: textures, samplers, pipelines ---------------------------- */

/* The staging ring all texture uploads go through (section 3). */
#define RMB_STAGING_BYTES (64u << 20)
/*
 * The longest texture side an upload may have. Metal aborts the process
 * (a failed descriptor assertion, not an error) on a side above the
 * device's maximum, which is 16384 on every family from the apple5 floor
 * on; gl1 on real GL answers GL_INVALID_VALUE instead. 16384 and not the
 * shim's 8192: it is the device's own limit, so nothing a GL driver on
 * this hardware takes is refused, and the staging ring bounds the other
 * side (16384 by 1024 is the whole ring).
 */
#define RMB_MAX_TEXTURE_SIDE 16384
/* Texture ids run from 1 to RMB_MAX_TEXTURES - 1; 0 is none. */
#define RMB_MAX_TEXTURES 4096

/* The sampler classes of gl1's images, each in a repeat and a clamp form. */
typedef enum
{
	RMB_SMP_MIP,        /* mipmapped: gl_texturemode's min and mag filter, r_anisotropic */
	RMB_SMP_FLAT,       /* no mipmaps: gl_texturemode's mag filter for both */
	RMB_SMP_NEAREST,    /* r_nolerp_list, r_2D_unfiltered, r_videos_unfiltered */
	RMB_SMP_CLASSES
} rmb_smp_t;

#define RMB_SAMPLER(cls, clamp) ((cls) * 2 + ((clamp) ? 1 : 0))

/*
 * gl1's filter state of one image as a sampler (repeat form): at load
 * (R_LoadPic) an image on r_nolerp_list, or a pic under r_2D_unfiltered,
 * is GL_NEAREST, mipmapped or not; after a mode change (R_TextureMode,
 * mode_change 1) a mipmapped image takes the mode whatever the lists
 * say, and only one without mipmaps stays GL_NEAREST.
 */
int rm_image_sampler(int mipmapped, int nolerp, int mode_change);

/*
 * Loads ref_metal.metallib from dir and builds the 2D pipeline table and
 * the samplers. 0 with err filled on failure.
 */
int rmb_resources_open(const char *dir, char *err, size_t errlen);
void rmb_resources_close(void);

/* Rebuilds the sampler table between frames: min 0 to 5, mag_linear, anisotropy 0 (off) or 1 to 16. */
void rmb_samplers_set(int min, int mag_linear, int aniso);

/*
 * Writes w by h rgba8 texels (rows top down, tightly packed) into
 * texture id, making it (or making it again at another size) first;
 * with mipmapped the mip chain is generated. id 0 takes a free id.
 * Returns the id, 0 when the size check refuses the upload or an
 * allocation fails (counted as an upload skip; nothing is written).
 * Every 0 also frees the id passed in (its texture is gone), so on 0
 * the caller forgets its id and no id is lost.
 * opaque forces alpha 255 (gl1's solid internal format on the shim).
 */
int rmb_texture_upload(int id, const unsigned char *rgba, int w, int h, int mipmapped, int opaque);
void rmb_texture_free(int id);

/* Counts an upload the front end refused by rm_upload_bytes before converting it. */
void rmb_upload_refused(void);

/* One 2D vertex: the shim's layout (float4 position, float2 uv, float4 colour). */
typedef struct
{
	float pos[4];
	float uv[2];
	float pad[2];
	float color[4];
} rmb_vert2d_t;

/*
 * Draws a fan of n vertices (gl1's GL_TRIANGLE_FAN quads) with 2D
 * pipeline pipe, texture tex (0 for the untextured pipelines) and
 * sampler smp, in the open pass, at the full-target viewport, with mvp
 * (column-major) on every vertex. 0 when the draw is skipped (counted).
 */
int rmb_draw_2d(int pipe, int tex, int smp, const rmb_vert2d_t *v, int n, const float *mvp);

/*
 * The same for nquads quads of 4 vertices each (a batch of gl1's
 * R_Buffer2DQuad, one draw). 0 when the draw is skipped (counted), or
 * nquads is outside 1 to RM_BATCH_QUADS.
 */
#define RM_BATCH_QUADS 1024
int rmb_draw_2d_quads(int pipe, int tex, int smp, const rmb_vert2d_t *v, int nquads, const float *mvp);

#endif /* RM_RMB_H */
