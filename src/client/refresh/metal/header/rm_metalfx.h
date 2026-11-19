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
 * The pure part of ref_metal's MetalFX scalers (TASK111; decision 0011;
 * docs/plans/metal-raytracing-design.md, section 2): the mode a view runs
 * and its fallback, the internal scale and its per-family default, the
 * input and output sizes, the jitter, the G-buffer path a mode needs, the
 * reset events and the report lines. rm_metalfx.c, held by
 * tests/metal/rm_fx_test.c; no Metal, no engine state.
 *
 * =======================================================================
 */

#ifndef RM_METALFX_H
#define RM_METALFX_H

#include <stddef.h>

#include "rmb.h"

/* r_metalfx */
#define RM_FX_OFF 0
#define RM_FX_SPATIAL 1         /* MTLFXSpatialScaler: colour only, no history, no jitter */
#define RM_FX_TEMPORAL 2        /* MTLFXTemporalScaler: colour, depth, motion and jitter */
#define RM_FX_DENOISED 3        /* MTLFXTemporalDenoisedScaler: only with a ray-traced effect, else run as 2 */
#define RM_FX_MAX 3

/*
 * What the device supports (rmb_fx_support, r_metalfx_modes): bit 1 << m
 * for mode m (bit 0, off, always), RM_FX_HAS_INTERP for the frame
 * interpolator, each from its descriptor's supportsDevice:.
 */
#define RM_FX_HAS(m) (1u << (m))
#define RM_FX_HAS_INTERP (1u << 4)
#define RM_FX_HAS_ALL (RM_FX_HAS(0) | RM_FX_HAS(1) | RM_FX_HAS(2) | RM_FX_HAS(3) | RM_FX_HAS_INTERP)

/* the mode r_metalfx's v asks for: v clamped to 0 to RM_FX_MAX, then truncated; 0 when not finite */
int rm_fx_mode_of(float v);

/* the best mode at or below m that has (RM_FX_HAS bits) holds; off when none does or m is out of range */
int rm_fx_supported(int m, unsigned has);

/*
 * The mode a world view runs for r_metalfx's mode m: rm_fx_supported's,
 * with DENOISED run as TEMPORAL when no ray-traced effect is on (and then
 * the best supported at or below that).
 */
int rm_fx_run(int m, int rt_effect, unsigned has);

/* r_metalfx_interp's v: 1 when it is finite and not 0 and has holds the interpolator, else 0 */
int rm_fx_interp(float v, unsigned has);

/* a mode's name for the report lines: "off", "spatial", "temporal", "temporal denoised"; "unknown" out of range */
const char *rm_fx_name(int m);

/*
 * The default internal height in rows (r_metalfx_scale 0) of a device
 * with families (RMB_FAMILY_BIT): 1080 from apple9 on, 720 below (design
 * section 2, option A's defaults).
 */
#define RM_FX_ROWS_APPLE9 1080
#define RM_FX_ROWS_OTHER 720
int rm_fx_default_rows(unsigned families);

/*
 * The internal size as a fraction of the output's (r_metalfx_scale v): v,
 * or rows / out_h when v is 0, not finite or below 0; clamped to
 * 1 / ratio to 1, where ratio is the largest output-to-input ratio the
 * scaler takes (its supportedInputContentMaxScale), itself clamped to 1
 * to RM_FX_RATIO_MAX (RM_FX_RATIO_MAX when not finite). 1 when out_h is
 * not above 0 or rows is not above 0 for a default.
 */
#define RM_FX_RATIO_MAX 3.0f
float rm_fx_scale(float v, int rows, int out_h, float ratio);

/* the sizes of one scaled view, in pixels */
typedef struct
{
	int iw, ih;     /* the input: the render size, the view drawn at (0, 0) */
	int ow, oh;     /* the output: the world view's rectangle */
	int ox, oy;     /* where the output goes in the frame (top-left origin) */
} rm_fx_size_t;

/*
 * The sizes for the world view's rectangle out (frame pixels, rounded to
 * whole pixels) at scale (rm_fx_scale's): each side of the input the
 * output's times scale, rounded up, so the output-to-input ratio is never
 * above 1 / scale (a product within RM_FX_SIDE_SLACK above a whole number
 * is taken as that number, for a scale that is not exact in a float), at
 * least 1 and at most the output's; a scale that is not finite or not
 * above 0 counts as 1. 0, with *s zeroed,
 * for a rectangle that is not finite, below 1 or above RM_FX_MAX_SIDE a
 * side, or that starts below 0 or past RM_FX_MAX_SIDE.
 */
#define RM_FX_MAX_SIDE 16384   /* the G-buffer's largest side (rm_gbuf.c) */
#define RM_FX_SIDE_SLACK 1e-3
int rm_fx_sizes(const rm_viewport_t *out, float scale, rm_fx_size_t *s);

/* the viewport the view is drawn with at the input size: (0, 0, iw, ih), the depth range of out */
void rm_fx_input_viewport(const rm_viewport_t *out, const rm_fx_size_t *s, rm_viewport_t *in);

/*
 * The radical inverse of i in base (2 or more): the i-th element of the
 * Halton sequence, in [0, 1); 0 for i 0 or a base below 2.
 */
float rm_fx_halton(unsigned i, unsigned base);

/*
 * The jitter of the n-th frame since the last reset, in input pixels, +x
 * right and +y down (rm_gb_jitter's and MetalFX's jitterOffset): the
 * Halton (2, 3) pair at index (n mod RM_FX_JITTER_PHASES) + 1, minus 0.5
 * each. (0, 0) for a mode that takes no jitter (off, spatial).
 */
#define RM_FX_JITTER_PHASES 32
void rm_fx_jitter(int mode, unsigned n, float *jx, float *jy);

/*
 * The G-buffer path's motion request (rm_gb_path's motion): 1 when the
 * view must at least take the MOTION path, for any scaler (its render-size
 * colour target; the temporal ones also its depth and motion) or the
 * interpolator (depth and motion); 0 otherwise.
 */
int rm_fx_wants_motion(int mode, int interp);

/* the scaler is given the depth and motion attachments: the temporal modes */
int rm_fx_wants_depth(int mode);

/*
 * The state the next world view's reset is judged against: the last
 * world view drawn with a scaler since the last reset of the state.
 */
typedef struct
{
	int valid;
	int mode;
	rm_fx_size_t size;
	float org[3];
} rm_fx_last_t;

#define RM_FX_RESET_FIRST 1     /* no last view: the first, a map, a vid_restart, a frame that drew no world view */
#define RM_FX_RESET_MODE 2      /* r_metalfx (the mode run) changed */
#define RM_FX_RESET_SIZE 4      /* the input or output size: the scale, the rectangle's size, the window */
#define RM_FX_RESET_RECT 8      /* the rectangle's place (viewsize) */
#define RM_FX_RESET_CUT 16      /* the view moved further than cut: a camera cut, a teleport */
#define RM_FX_RESET_ALL 31

/*
 * The client's own teleport distance: CL_CalcViewValues (cl_entities.c)
 * does not lerp the view across a jump of more than 256 units on an axis.
 */
#define RM_FX_CUT_DIST 256.0f

/*
 * The reasons (RM_FX_RESET_*) the history of a view of mode at sizes s
 * from org resets against last; 0 when it is kept. A cut is a move of
 * more than cut on any axis; a cut or an org that is not finite cuts.
 */
unsigned rm_fx_reset(const rm_fx_last_t *last, int mode, const rm_fx_size_t *s, const float *org, float cut);

/* this view becomes last */
void rm_fx_keep(rm_fx_last_t *last, int mode, const rm_fx_size_t *s, const float *org);

/* every side and the place of a and b are the same */
int rm_fx_same_size(const rm_fx_size_t *a, const rm_fx_size_t *b);

/* the history of the world views' scaler across views and frames (rm_fxscene.c holds one) */
typedef struct
{
	rm_fx_last_t last;      /* the last scaled view since the last reset of the state */
	unsigned n;             /* scaled views since the last history reset: the jitter's index */
	int drew;               /* this frame drew a scaled view */
} rm_fx_state_t;

/* forgets everything: the renderer's start, a map load; the next view resets (FIRST) */
void rm_fx_state_reset(rm_fx_state_t *st);

/*
 * One scaled world view of mode at sizes s from org: its reset reasons
 * against the last view (rm_fx_reset, cut), the jitter index started again
 * on a reset, this view's jitter (rm_fx_jitter at that index) into jx and
 * jy, the index advanced, the view kept as the last and the frame marked
 * as having drawn one. The reasons, 0 when the history is kept.
 */
unsigned rm_fx_step(rm_fx_state_t *st, int mode, const rm_fx_size_t *s, const float *org, float cut, float *jx,
		float *jy);

/* the frame ended: one that drew no scaled view forgets the last, so the next view resets (FIRST) */
void rm_fx_frame_end(rm_fx_state_t *st);

/*
 * The back end's memo of a scaler it could not make: a scaler of the same
 * mode and sizes is not tried again (and its line not repeated) until the
 * memo is cleared; another mode or size is tried.
 */
typedef struct
{
	int refused;
	int mode;
	rm_fx_size_t size;
} rm_fx_refusal_t;

/* a scaler of mode at s was refused before: do not try it again */
int rm_fx_refused(const rm_fx_refusal_t *r, int mode, const rm_fx_size_t *s);

/* the scaler of mode at s could not be made */
void rm_fx_refuse(rm_fx_refusal_t *r, int mode, const rm_fx_size_t *s);

/* forgets the refusal: a mode turned off, a vid_restart */
void rm_fx_refusal_clear(rm_fx_refusal_t *r);

/* what the back end has to report (rmb_fx_note) */
#define RM_FX_NOTE_NONE 0
#define RM_FX_NOTE_UNMADE 1     /* the scaler cannot be made */
#define RM_FX_NOTE_UNFIT 2      /* the view's targets do not fit the scaler */
#define RM_FX_NOTE_HDR 3        /* r_hdr's float16 path is on: no scaler until HD4 (TASK196) */

/*
 * The line of a back end's note (without a newline): the mode and the
 * sizes, and that the view is stretched. 0, with buf empty, for
 * RM_FX_NOTE_NONE, an unknown note or a line that does not fit.
 */
int rm_fx_note_format(int note, int mode, const rm_fx_size_t *s, char *buf, size_t len);

/*
 * The report line of a fallback (without a newline): what was asked and
 * what is run, and why. 1 when it fits; 0, with buf empty, when it does
 * not. what: "r_metalfx" or "r_metalfx_interp".
 */
int rm_fx_fallback_format(const char *what, int asked, int got, const char *why, char *buf, size_t len);

/*
 * The metal_stats line of a scaled view (without a newline): the mode,
 * the input and output sizes, the reset reasons of this frame and the
 * scaler pass's GPU time (ms, below 0 not measured yet). 1 when it fits.
 */
int rm_fx_stats_format(int mode, const rm_fx_size_t *s, unsigned resets, double gpu_ms, char *buf, size_t len);

/*
 * The line of what the device supports (without a newline): each
 * descriptor's supportsDevice: and r_metalfx_modes's value. 1 when it fits.
 */
int rm_fx_support_format(unsigned has, char *buf, size_t len);

/* ---- the back end (rmb_metalfx.m) --------------------------------------------- */

/* RM_FX_HAS bits of the device: each descriptor's supportsDevice:, 0 where MetalFX is missing */
unsigned rmb_fx_support(void);

/* the largest output-to-input ratio of mode's scaler (supportedInputContentMaxScale), RM_FX_RATIO_MAX where none is given */
float rmb_fx_max_ratio(int mode);

/*
 * The next view's scaler: mode at sizes s with the jitter (jx, jy) and,
 * when reset is set, its history dropped. Mode off frees the scaler. 0
 * when the scaler of a mode cannot be made: the view is stretched as
 * without one.
 */
int rmb_fx_prepare(int mode, const rm_fx_size_t *s, float jx, float jy, int reset);

/* a scaler is prepared for the view being drawn (its view goes to the render-size colour target) */
int rmb_fx_ready(void);

/* no scaler is prepared: every view begins with it (RM_FX_Begin), so none is left from an earlier one */
void rmb_fx_unready(void);

/*
 * The back end's last note (RM_FX_NOTE_*), with its mode and sizes, taken
 * once: the engine prints it (R_Printf), told once for each mode and size.
 */
int rmb_fx_note(int *mode, rm_fx_size_t *s);

/* metal_stats is on: the scaler pass runs in a command buffer of its own, whose GPU time it keeps */
void rmb_fx_set_timing(int on);

/* the GPU time of the last timed scaler pass in ms, below 0 when none completed */
double rmb_fx_gpu_ms(void);

/* frees every MetalFX object (rmb_encode_release, a mode turned off) */
void rmb_fx_release(void);

#ifdef __OBJC__
/*
 * For rmb_gbuf_view_end: upscales the view's render-size colour (and,
 * temporal, its depth and motion) into the frame's rectangle with the
 * prepared scaler. 1 when it wrote the frame; 0 when no scaler is
 * prepared or it could not run: the caller stretches as before.
 */
int rmb_fx_upscale(id<MTLTexture> color, id<MTLTexture> depth, id<MTLTexture> motion, id<MTLTexture> frame);
#endif

#endif
