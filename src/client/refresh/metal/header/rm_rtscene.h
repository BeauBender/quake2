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

/* 1 when this frame's instance structure was built (RM_RT_Frame ran and encoded it) */
int RM_RT_SceneReady(void);

/*
 * RT3 (TASK55): the lights an alias instance of this frame carries, bit i
 * the view's light i (rm_rts_carried, with the shadows on; 0 otherwise)
 */
unsigned RM_RT_Carried(void);

/*
 * TASK126 slice 6: one frame's stats slot of a scaled effect, from the
 * effect's scene as it reads them: its divisor, traced size, drawn size
 * and the upsample's fallbacks, kept in the effect's rm_rtt_up_t. Under
 * r_rt_stats 1 a frame whose traced size changed prints
 * "ref_metal: r_rt_stats: upsample: <effect> TWxTH of WxH, F fallbacks",
 * and the map's summary prints each effect's totals (rm_rtt_up_total_format).
 */
#define RM_RT_UP_SHADOWS 0
#define RM_RT_UP_SUN 1
#define RM_RT_UP_AO 2
#define RM_RT_UP_WATER 3
#define RM_RT_UP_N 4
void RM_RT_UpFrame(int effect, unsigned div, unsigned long tw, unsigned long th, unsigned long w, unsigned long h,
		unsigned long fallbacks);

/*
 * RT3 (TASK55), rm_rtshadowscene.c: the dynamic-light shadows. Init and
 * Shutdown with RM_RT_Init and the release of the ray-tracing objects;
 * Wanted: r_rt_shadows asks for the structures (rm_rt_wanted); MapBuilt:
 * a new map's structures were built, the last map's shadow tables go
 * (this map's per-triangle transmission and surface tables are built when
 * the shadows are first on for it); Frame, after RM_RT_Frame: whether
 * this frame traces (Active), which takes the view to the G-buffer path;
 * View, after RM_GB_Begin and before the raster: the view and lights the
 * kernel and the resolve take, and, when everything the trace needs is
 * there, gl1's dynamic-light term out of the lightmaps (r_dlight_term);
 * ShowOnly: r_rt_shadows_show is on, the forward pass is left out so the
 * view shows the pass alone.
 */
void RM_RTS_Init(const rmb_caps_t *caps);
void RM_RTS_Shutdown(void);
int RM_RTS_Wanted(void);
void RM_RTS_MapBuilt(const model_t *world);
void RM_RTS_Frame(const refdef_t *fd, int scene_ready);
int RM_RTS_Active(void);
int RM_RTS_ShowOnly(void);
int RM_RTS_ShowAsked(void);   /* TASK190 slice 3: Wanted and the _show cvar on, known before Frame and View (rm_rtfx_show_asked) */
void RM_RTS_View(const rm_view_t *view);

#endif /* RM_RTSCENE_H */
