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
 * RT-primary's light on rastered alias models (TASK191 slice 2, PM9):
 * the scene (rm_rtmodelscene.c) and the Metal back end (rmb_rtmodel.m)
 * of the model pass, a row of the effect table (rm_rtfx.c) after the
 * direct light, whose switch it shares: it runs only where the direct
 * light traced the frame, under r_rt_direct_models (default 1; 0 keeps
 * every model stock-lit). Its kernel, rm_rtm_trace (rm_rtmodel.metal),
 * lights each model pixel with the world's traced terms at its own
 * point and normal, in gl1's model colour (common/mtl_rtm_math.h).
 *
 * =======================================================================
 */

#ifndef RM_RTMODELSCENE_H
#define RM_RTMODELSCENE_H

#include "rm_rtfx.h"

/* rm_rtmodel.metal's rm_rtm_params */
typedef struct
{
	float glow;         /* RF_GLOW's pulse at the frame's time (rm_rtm_glow_scale) */
	float overbright;   /* gl1_overbrightbits as rm_alias_shade reads it, 0 for none */
	unsigned pad0, pad1;
} rm_rtm_params_t;

/* the minlight table then the gamma table, the kernel's tabs */
#define RM_RTM_TABLES 512

/* one frame of the pass, read once it completed */
typedef struct
{
	unsigned long frame;
	unsigned long pixels;   /* the model pixels traced */
	unsigned long rays;     /* their shadow rays toward the map's and the dynamic lights */
	unsigned long open;     /* their sky, sun and bounce rays */
	double ms;              /* the pass's GPU time, -1 when the device gave none */
} rm_rtm_stats_t;

/* the r_rt_stats 2 line of a frame into buf; 0 when it does not fit */
int rm_rtm_stats_format(const rm_rtm_stats_t *s, char *buf, int len);

/*
 * 1 when this frame's GBUFFER view lights its rastered models by traced
 * light (r_rt_direct_models on and the direct light traced the frame):
 * R_EntitySplit then splits by rm_rtm_entity_split under the mode.
 */
int RM_TracedModels(void);

#ifdef RM_LOCAL_H
void RM_RTModel_Init(const rmb_caps_t *caps);
void RM_RTModel_Shutdown(void);
int RM_RTModel_Wanted(void);
void RM_RTModel_Frame(const refdef_t *fd, int scene_ready);
int RM_RTModel_Active(void);
int RM_RTModel_ShowOnly(void);
int RM_RTModel_ShowAsked(void);   /* TASK190 slice 3's row entry: 0, the view is the direct light's (RM_RTDir_ShowAsked) */
void RM_RTModel_View(const rm_view_t *view);
#endif

/* ---- rmb_rtmodel.m: the Metal back end ---------------------------------------------- */

/* the pipeline and the counts; 0 with err filled when they cannot be made */
int rmb_rtmodel_open(char *err, unsigned long errlen);
void rmb_rtmodel_close(void);

/* this frame's parameters and tables (RM_RTM_TABLES bytes); NULL for none: the pass then runs nothing */
void rmb_rtmodel_set_view(const rm_rtm_params_t *p, const unsigned char *tabs);

/* the stats of the frames completed since the last call, at most cap; their number */
int rmb_rtmodel_stats(rm_rtm_stats_t *s, int cap);

/* the effect table's backend row: after the direct light's trace, the model pixels of io's light */
void rmb_rtmodel_fx_trace(rmb_rtfx_io_t *io);

#endif /* RM_RTMODELSCENE_H */
