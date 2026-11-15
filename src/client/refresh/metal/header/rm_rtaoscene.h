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

void RM_RTAO_Init(const rmb_caps_t *caps);
void RM_RTAO_Shutdown(void);
int RM_RTAO_Wanted(void);
void RM_RTAO_Frame(const refdef_t *fd, int scene_ready);
int RM_RTAO_Active(void);
int RM_RTAO_ShowOnly(void);
void RM_RTAO_View(const rm_view_t *view);

#endif /* RM_RTAOSCENE_H */
