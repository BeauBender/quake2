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
} rmb_stats_t;

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

/* ---- rmb_device.m and rmb_encode.m ------------------------------------------- */

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

#endif /* RM_RMB_H */
