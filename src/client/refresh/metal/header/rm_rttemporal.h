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
#define RM_RTT_RESET_SIZE 2     /* the render size: r_rt_scale's (RT11), r_rt_gbuffer_scale's, the window's */
#define RM_RTT_RESET_RECT 4     /* the view's rectangle at the render size: viewsize */
#define RM_RTT_RESET_CUT 8      /* the eye moved further than RM_RTT_CUT_DIST on an axis: a camera cut, a teleport */
#define RM_RTT_RESET_ALL 15

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
} rm_rtt_state_t;

/* forgets everything: the renderer's start (a vid_restart), a map load; the next view resets (FIRST) */
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

#endif /* RM_RTTEMPORAL_H */
