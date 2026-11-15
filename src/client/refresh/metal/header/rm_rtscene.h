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
 * The ray-tracing scene of ref_metal (RT1, TASK53; rm_rtscene.c): the
 * hooks the rest of the renderer calls. Each one returns at once, having
 * allocated nothing, while rm_rt_wanted says no (every r_rt_* cvar at its
 * default, or a device without ray tracing). Include after local.h.
 *
 * =======================================================================
 */

#ifndef RM_RTSCENE_H
#define RM_RTSCENE_H

/* RI_Init, after the capability report: registers the r_rt_* cvars, keeps the device's ray-tracing support */
void RM_RT_Init(const rmb_caps_t *caps);

/* RI_Shutdown, before the models and the device go: releases every ray-tracing object */
void RM_RT_Shutdown(void);

/* RI_EndRegistration (map load): builds the world's and the inline models' structures */
void RM_RT_MapLoaded(void);

/* Mod_Free: releases what was built from the model (the map's structures for the world) */
void RM_RT_ModelFree(const model_t *mod);

/* RI_RenderFrame, before the 3D pass: the alias lerps and refits and the instance rebuild */
void RM_RT_Frame(const refdef_t *fd);

#endif /* RM_RTSCENE_H */
