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
 * TASK188 (PM6 slice 1), rm_rtdirscene.c: the engine side of
 * RT-primary's direct light (header/rm_rtdir.h). Init and Shutdown with
 * RM_RT_Init and the release of the ray-tracing objects; Wanted: under
 * r_presentation 3, r_rt_direct (which the mode reads 1) asks for the
 * structures and the frame's scene; Frame, after RM_RT_Frame: the map's
 * lights uploaded once a map, the exposure fitted from the frames that
 * completed, and whether this frame traces (Active), which takes the
 * view to the G-buffer path; View, after RM_GB_Begin and before the
 * raster: the view and the style table the kernel takes; ShowOnly:
 * r_rt_direct_show is on, the forward pass is left out so the view
 * shows the term alone.
 *
 * =======================================================================
 */

#ifndef RM_RTDIRSCENE_H
#define RM_RTDIRSCENE_H

void RM_RTDir_Init(const rmb_caps_t *caps);
void RM_RTDir_Shutdown(void);
int RM_RTDir_Wanted(void);
int RM_RTDir_Will(const refdef_t *fd);   /* TASK191 slice 3: Frame traces fd once its scene is built (Frame's own test) */
void RM_RTDir_Frame(const refdef_t *fd, int scene_ready);
int RM_RTDir_Active(void);
int RM_RTDir_ShowOnly(void);
int RM_RTDir_ShowAsked(void);   /* TASK190 slice 3: Wanted and the _show cvar on, known before Frame and View (rm_rtfx_show_asked) */
void RM_RTDir_View(const rm_view_t *view);

#endif /* RM_RTDIRSCENE_H */
