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
 * Temporal accumulation in ref_metal, RT7 (TASK59 slice 1;
 * docs/plans/metal-raytracing-design.md, section 2): r_rt_temporal, the
 * history's reset events and the state they are judged against. Plain C
 * with neither SDL nor Metal, host-tested by tests/metal/rm_rttemporal_test.c
 * with the per-pixel rule of common/mtl_rtt_math.h.
 *
 * =======================================================================
 */

#ifndef RM_RTTEMPORAL_H
#define RM_RTTEMPORAL_H

#include <stddef.h>

#include "../common/mtl_rtt_math.h"

typedef mtl_rtt_view_t rm_rtt_view_t;

/* the history's weight at most: 0.98, so that a history still follows a change */
#define RM_RTT_ALPHA_MAX 0.98f

/*
 * r_rt_temporal as the blend takes it: 0 (off: no history is made, written
 * or read) for 0, a value below it or one that is not finite; at most
 * RM_RTT_ALPHA_MAX.
 */
float rm_rtt_alpha(float v);

/* the client's teleport distance, the scaler's cut (RM_FX_CUT_DIST, rm_metalfx.h): 256 units on an axis */
#define RM_RTT_CUT_DIST 256.0f

/* the reasons (rm_rtt_step) a view resets the history; RM_RTT_RESET_ALL all of them */
#define RM_RTT_RESET_FIRST 1    /* no last view: the first, a map load, a vid_restart, a frame without an accumulated view
                                   (every effect off, r_rt_temporal 0, a cinematic, the console with no map) */
#define RM_RTT_RESET_SIZE 2     /* the render size: r_metalfx_scale's (RT11), r_rt_gbuffer_scale's, the window's */
#define RM_RTT_RESET_RECT 4     /* the view's rectangle at the render size: viewsize */
#define RM_RTT_RESET_CUT 8      /* the eye moved further than RM_RTT_CUT_DIST on an axis: a camera cut, a teleport */
#define RM_RTT_RESET_SCALE 16   /* one effect's own: its history was taken at another divisor (TASK126, rm_rtt_hist_take_at) */
#define RM_RTT_RESET_ALL 31

/* one accumulated world view: the render size, its rectangle in it and the eye */
typedef struct
{
	unsigned w, h;          /* the render size */
	int vx, vy, vw, vh;     /* the view's rectangle at the render size */
	float org[3];
} rm_rtt_key_t;

/* the history across views and frames */
typedef struct
{
	int valid;              /* last holds a view */
	rm_rtt_key_t last;      /* the last accumulated view since the last reset of the state */
	int drew;               /* this frame accumulated a view */
	unsigned long views;    /* views accumulated since the last history reset */
	unsigned seq;           /* the number of the last view stepped; kept by rm_rtt_state_reset, so it never repeats */
} rm_rtt_state_t;

/*
 * Forgets everything but seq: the renderer's start (a vid_restart), a map
 * load; the next view resets (FIRST).
 */
void rm_rtt_state_reset(rm_rtt_state_t *st);

/*
 * One accumulated world view k: its reset reasons against the last view,
 * the eye's move since it (0 on a reset) into move, the view kept as the
 * last and the frame marked as having accumulated one. The reasons, 0
 * when the history is kept.
 */
unsigned rm_rtt_step(rm_rtt_state_t *st, const rm_rtt_key_t *k, float *move);

/*
 * The frame ended: one that accumulated no view forgets the last, so the
 * next view resets (FIRST). This is the reset of the first view with an
 * effect on after one with every effect off: with every effect off no view
 * is accumulated, so nothing writes the history and what it holds is stale.
 */
void rm_rtt_frame_end(rm_rtt_state_t *st);

/*
 * The view of the kernels from this view's step: alpha (rm_rtt_alpha),
 * reasons and move as rm_rtt_step gave them.
 */
rm_rtt_view_t rm_rtt_view(float alpha, unsigned reasons, float move);

/*
 * One effect's history (TASK59 slice 1b): the shadows', the sun's and the
 * AO's each keep one, judged against the one state above, so that a reset
 * or a cut resets all three in the same view. It holds the number of the
 * last view whose accumulation was encoded into it.
 */
typedef struct
{
	int valid;              /* seq holds a view: a dispatch was encoded since the history was made */
	unsigned seq;
	unsigned div;           /* the effect's divisor at the last take (TASK126), 0 before the first; kept by rm_rtt_hist_reset */
} rm_rtt_hist_t;

/*
 * The history was made anew or released: its texels hold no view, so the
 * next take resets (FIRST). The divisor is kept, so a history made again
 * at an effect's new traced size still resets for its scale.
 */
void rm_rtt_hist_reset(rm_rtt_hist_t *h);

/*
 * The view t is encoded into the history: the reasons its dispatch takes,
 * t's own, with FIRST when the history did not take the view before t,
 * so a view whose dispatch was not encoded (an early return of the trace
 * after rm_rtt_step, the effect off for views the others accumulated, a
 * history just made) leaves a history that is not read again. Called
 * only where the dispatch is encoded.
 */
unsigned rm_rtt_hist_take(rm_rtt_hist_t *h, const rm_rtt_view_t *t);

/*
 * rm_rtt_hist_take for an effect traced at divisor div (TASK126; 0 is 1,
 * common/mtl_rtup_math.h's mtl_rtup_div): with RM_RTT_RESET_SCALE as well
 * when the history's last take was at another divisor, and none when it
 * has had no take. rm_rtt_hist_take is the take at divisor 1.
 */
unsigned rm_rtt_hist_take_at(rm_rtt_hist_t *h, const rm_rtt_view_t *t, unsigned div);

/*
 * TASK126's lines, without a newline; each 1 when it fits in len, 0 with
 * buf empty otherwise. The r_rt_stats 1 line of a view that reset effect's
 * history for its scale: "ref_metal: r_rt_stats: reset: <effect> scale".
 * The part an effect traced w x h below the drawn size adds to its
 * r_rt_stats 2 line: ", traced at WxH, F upsample fallbacks".
 */
int rm_rtt_scale_line_format(const char *effect, char *buf, size_t len);
int rm_rtt_traced_format(unsigned w, unsigned h, unsigned long fallbacks, char *buf, size_t len);

/*
 * TASK126 slice 6: one effect's upsample, as its r_rt_stats lines report
 * it. tw x th is the traced size of the effect's last frame below divisor
 * 1 (0 x 0 before one, and after a frame at divisor 1, which upsamples
 * nothing); fallbacks and pixels are the totals of the frames below
 * divisor 1 since the map loaded: the upsample's fallbacks and the drawn
 * pixels it wrote. frame is the number of the newest frame handed over
 * (0 before one): a back end hands its completed frames over in slot
 * order, not frame order, so a frame older than it is judged for the
 * totals alone. Zeroed, it is an effect that has upsampled nothing.
 */
typedef struct
{
	unsigned long tw, th;
	unsigned long long fallbacks, pixels;
	unsigned long frame;
} rm_rtt_up_t;

/*
 * One frame's stats of the effect: its number (rmb_frame_compute's),
 * divisor (0 is 1), traced size, drawn pixels and fallbacks. Below
 * divisor 1 the frame is added to the totals. A frame older than the
 * newest one handed over adds to the totals and nothing else: 0. A newer
 * one: 1 when its traced size is not the last frame's (the size-change
 * line is due), else 0; at divisor 1 the traced size is forgotten and
 * nothing is added: 0. So the line is due once per change of the traced
 * size, in frame order, whatever order the frames are handed over in.
 */
int rm_rtt_up_frame(rm_rtt_up_t *u, unsigned long frame, unsigned div, unsigned long tw, unsigned long th,
		unsigned long pixels, unsigned long fallbacks);

/* A map loaded: the totals start again; the traced size is kept, so the line is not printed again for it. */
void rm_rtt_up_map(rm_rtt_up_t *u);

/*
 * The lines, without a newline; each 1 when it fits in len, 0 with buf
 * empty otherwise. The r_rt_stats 1 line of a frame whose traced size
 * changed: "ref_metal: r_rt_stats: upsample: <effect> TWxTH of WxH, F
 * fallbacks", F that frame's. The effect's line of the r_rt_stats 1 map
 * summary: "ref_metal: rt: <map>: upsample: <effect> F fallbacks of P
 * upsampled pixels (S %)", S F over P in percent, to three places; 0,
 * buf empty, for an effect that upsampled no pixel.
 */
int rm_rtt_up_line_format(const char *effect, unsigned long tw, unsigned long th, unsigned long w, unsigned long h,
		unsigned long fallbacks, char *buf, size_t len);
int rm_rtt_up_total_format(const char *map, const char *effect, const rm_rtt_up_t *u, char *buf, size_t len);

#endif /* RM_RTTEMPORAL_H */
