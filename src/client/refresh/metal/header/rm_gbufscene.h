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
 * The engine side of ref_metal's G-buffer (RT2, TASK54): the r_rt_gbuffer,
 * r_rt_motion, r_rt_gbuffer_scale, r_rt_jitter_x and r_rt_jitter_y cvars
 * (and the development views r_rt_gbuffer_show and r_rt_motion_test), the path of a
 * view, its render size and jittered projection, the previous-frame
 * history (rm_gb_hist_t) and the motion uniforms of the world's and each
 * entity's draws. rm_gbufscene.c.
 *
 * =======================================================================
 */

#ifndef RM_GBUFSCENE_H
#define RM_GBUFSCENE_H

#include "rm_gbuf.h"
#include "../common/mtl_shader_math.h"
#include "../common/mtl_gbuf_math.h"

/* the farthest an entity moves from one frame to the next and is still matched (world units) */
#define RM_GB_MATCH_DIST 256.0f

void RM_GB_Init(void);

/* forgets the previous frame: a new map, a vid_restart */
void RM_GB_Reset(void);

/* the path a refdef will take (RM_GB_PATH_*): FORWARD unless asked, and always without a world */
int RM_GB_Choose(const refdef_t *fd);

/*
 * After R_SetupGL of a MOTION or GBUFFER view: the render size (view's
 * viewport scaled), the jittered projection in view (the unjittered one
 * kept for the motion), this frame's history, and the view's first pass
 * opened, the stencil cleared to stencil. 0 when the view cannot be
 * opened: view is as R_SetupGL left it and the caller draws the stock view.
 */
int RM_GB_Begin(rm_view_t *view, int stencil, const float *clear_rgba);

/* the path of the view being drawn, FORWARD outside one */
int RM_GB_Path(void);

/* the G-buffer raster is open (the world's dynamic draws keep their normals) */
int RM_GB_Raster(void);

/* GBUFFER: ends the raster and resolves it, then the forward pass opens */
void RM_GB_Resolve(void);

/* ends the view (the copy into the frame below its size) and keeps it as the previous one */
void RM_GB_End(void);

/* the world's motion for the draws that follow, with the material id (MTL_GB_MAT_*) */
void RM_GB_WorldMotion(int material);

/* the material of the world's draws that follow; a dynamic batch keeps the one it was begun with (rm_warp.c) */
void RM_GB_SetMaterial(int material);
int RM_GB_Material(void);

/* the material of the draw that follows only (a batch's, rm_warp.c's RM_DynFlush) */
void RM_GB_FlushMaterial(int material);

/*
 * An entity's motion for its draws: vm its modelview now (the stock
 * world * entity matrix), the previous one from its matched entity
 * (itself when none), brush: R_DrawBrushModel's angles (pitch and roll
 * turned) instead of R_DrawAliasModel's (pitch turned); a view weapon
 * takes its projection wproj (unjittered).
 */
void RM_GB_EntityMotion(const entity_t *e, const float *vm, int brush, const float *wproj);

/* the sky box's motion: its modelview now (rm_sky_matrix) and the previous one */
void RM_GB_SkyMotion(const float *vm, const float *prev_vm);

/* the previous view's modelview, origin and time (the sky's), 0 without one */
int RM_GB_PrevView(const float **world, const float **vieworg, float *time);

/* the entity's previous state, NULL without a match */
const rm_gb_ent_t *RM_GB_Prev(const entity_t *e);

/* a projection the view draws with: jittered as the view's (in place), the unjittered one into unjit */
void RM_GB_JitterProj(float *proj, float *unjit);

#endif
