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
 * The engine side of ref_metal's MetalFX scalers (TASK111): the
 * r_metalfx, r_metalfx_scale, r_metalfx_interp and r_metalfx_modes cvars,
 * their fallback on this device, the reset events and the metal_stats
 * line. rm_fxscene.c; the decisions are rm_metalfx.c's.
 *
 * =======================================================================
 */

#ifndef RM_FXSCENE_H
#define RM_FXSCENE_H

#include "rm_metalfx.h"

/* the cvars, after rmb_open: what the device supports, into r_metalfx_modes */
void RM_FX_Init(const rmb_caps_t *caps);

/* forgets the last view: a map load (the next view resets the history) */
void RM_FX_Reset(void);

/* the world views need the MOTION path for the scaler or the interpolator (rm_gb_path's motion) */
int RM_FX_WantsMotion(void);

/*
 * A world view at out (frame pixels) on a MOTION or GBUFFER path: when a
 * scaler runs, its input size into rw x rh, the viewport the view is
 * drawn with into in and the jitter (input pixels) into jitter[2], the
 * scaler prepared; 1 then. 0 when no scaler runs for it: the outputs are
 * left as they are.
 */
int RM_FX_Begin(const rm_viewport_t *out, const float *vieworg, int *rw, int *rh, rm_viewport_t *in, float *jitter);

/* the view ended (RM_GB_End): the metal_stats line of a scaled view */
void RM_FX_End(void);

/* the frame ended (RI_EndFrame): one that drew no world view with a scaler resets the next one */
void RM_FX_EndFrame(void);

#endif
