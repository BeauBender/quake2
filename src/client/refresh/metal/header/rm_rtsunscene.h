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
 * RT4 (TASK56), rm_rtsunscene.c: the engine side of the ray-traced sun
 * and sky shadow (header/rm_rtsun.h). Init and Shutdown with RM_RT_Init
 * and the release of the ray-tracing objects; Wanted: r_rt_sun asks for
 * the structures and for the frame's scene (rm_rt_wanted); Frame, after
 * RM_RT_Frame: the map's sky share and direction once a map, and whether
 * this frame traces (Active), which takes the view to the G-buffer path;
 * a map under 2 % sky never does, so it draws as with the effect off;
 * View, after RM_GB_Begin and before the raster: the view the kernel
 * takes, with the outputs at the render size; ShowOnly: r_rt_sun_show is
 * on, the forward pass is left out so the view shows the pass alone.
 *
 * =======================================================================
 */

#ifndef RM_RTSUNSCENE_H
#define RM_RTSUNSCENE_H

void RM_RTSun_Init(const rmb_caps_t *caps);
void RM_RTSun_Shutdown(void);
int RM_RTSun_Wanted(void);
void RM_RTSun_Frame(const refdef_t *fd, int scene_ready);
int RM_RTSun_Active(void);
int RM_RTSun_ShowOnly(void);
int RM_RTSun_ShowAsked(void);   /* TASK190 slice 3: Wanted and the _show cvar on, known before Frame and View (rm_rtfx_show_asked) */
void RM_RTSun_View(const rm_view_t *view);

/*
 * TASK189: the sun of RT-primary's light model (rm_rtdirscene.c), as RT2's
 * view takes it but whatever r_rt_sun reads (RT-primary's overlay reads it
 * 0): dir the unit direction toward the sun from the map's line of
 * scripts/rt/sun-angles.txt, else r_rt_sun_angles, else the default; sun
 * sky_color times r_rt_sunscale, a channel. No map is measured and nothing
 * is opened.
 */
void RM_RTSun_Primary(float *dir, float *sun);

#endif /* RM_RTSUNSCENE_H */
