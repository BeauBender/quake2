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
 * RT5 (TASK57), rm_rtaoscene.c: the engine side of the ray-traced ambient
 * occlusion (header/rm_rtao.h). Init and Shutdown with RM_RT_Init and the
 * release of the ray-tracing objects; Wanted: r_rt_ao asks for the
 * structures and for the frame's scene (rm_rt_wanted); Frame, after
 * RM_RT_Frame: whether this frame traces (Active), which takes the view to
 * the G-buffer path; View, after RM_GB_Begin and before the raster: the
 * view the kernel takes, with the outputs at the render size; ShowOnly:
 * r_rt_ao_show is on, the forward pass is left out so the view shows the
 * pass alone.
 *
 * =======================================================================
 */

#ifndef RM_RTAOSCENE_H
#define RM_RTAOSCENE_H

#include "rm_rttemporal.h"

void RM_RTAO_Init(const rmb_caps_t *caps);
void RM_RTAO_Shutdown(void);
int RM_RTAO_Wanted(void);
void RM_RTAO_Frame(const refdef_t *fd, int scene_ready);
int RM_RTAO_Active(void);
int RM_RTAO_ShowOnly(void);
void RM_RTAO_View(const rm_view_t *view);

/*
 * RT7's history (TASK59): RM_RTT_Reset forgets the last view on a map
 * load, with the scaler's (RM_FX_Reset); RM_RTT_EndFrame ends the frame
 * with the scaler's (RM_FX_EndFrame), so a frame that accumulated no view
 * resets the next.
 */
void RM_RTT_Reset(void);
void RM_RTT_EndFrame(void);

/*
 * The one reset state of the shadows', the sun's and the AO's histories
 * (slice 1b). RM_RTT_BeginView: a world view starts (rm_main.c, before
 * the effects' views). RM_RTT_ViewFor: the view the effects' back ends
 * accumulate with, stepped at the first call of a world view and the same
 * for every effect after it; NULL while r_rt_temporal is 0.
 */
void RM_RTT_BeginView(void);
const rm_rtt_view_t *RM_RTT_ViewFor(const rm_view_t *view);

/*
 * TASK109: the reasons (RM_RTT_RESET_*) of the last step taken since the
 * last call, then 0 until the next step: RM_FX_End reads them once a view,
 * for the r_rt_stats reset line, so a view that steps nothing (the MOTION
 * path, r_rt_temporal 0) reports none.
 */
unsigned RM_RTT_TakeReasons(void);

#endif /* RM_RTAOSCENE_H */
