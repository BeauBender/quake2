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
 * The engine side of ref_metal's ray-traced water reflections (RT6,
 * TASK58; header/rm_rtwater.h, header/rm_rtwaterscene.h): the r_rt_water
 * cvars (r_rt_water, r_rt_water_normal, r_rt_water_reflect) and the
 * development view r_rt_water_show, the frame's decision (rm_rtw_on: with
 * it the view takes the G-buffer path), the view handed to the back end
 * before the raster, and the pass's r_rt_stats 2 line. With r_rt_water 0
 * (the default) nothing here builds or allocates anything.
 *
 * =======================================================================
 */

#include "header/local.h"
#include "header/rm_rt.h"
#include "header/rm_rtwater.h"
#include "header/rm_rtwaterscene.h"
#include "header/rm_gbufscene.h"

static cvar_t *r_rt_water;
static cvar_t *r_rt_water_normal;
static cvar_t *r_rt_water_reflect;
static cvar_t *r_rt_water_show;
static cvar_t *r_rt_stats_ref;

static int rtw_device;      /* rmb_caps_t.raytracing */
static int rtw_opened;      /* rmb_rtw_open succeeded */
static int rtw_refused;     /* it failed in this context: not tried again */
static int rtw_active;      /* this frame traces the reflections */

void
RM_RTW_Init(const rmb_caps_t *caps)
{
	r_rt_water = ri.Cvar_Get("r_rt_water", "0", CVAR_ARCHIVE);
	r_rt_water_normal = ri.Cvar_Get("r_rt_water_normal", "0.08", CVAR_ARCHIVE);
	r_rt_water_reflect = ri.Cvar_Get("r_rt_water_reflect", "0.5", CVAR_ARCHIVE);
	r_rt_water_show = ri.Cvar_Get("r_rt_water_show", "0", 0);
	r_rt_stats_ref = ri.Cvar_Get("r_rt_stats", "0", 0);
	rtw_device = caps->raytracing != 0;
	rtw_opened = rtw_refused = rtw_active = 0;
}

void
RM_RTW_Shutdown(void)
{
	if (rtw_opened)
	{
		rmb_rtw_close();
	}

	rtw_opened = rtw_active = 0;
}

int
RM_RTW_Wanted(void)
{
	return r_rt_water && rm_rt_wanted(r_rt_water->value, rtw_device);
}

void
RM_RTW_Frame(const refdef_t *fd, int scene_ready)
{
	rm_rtw_stats_t st[RMB_FRAMES_IN_FLIGHT];
	int i, n;

	rtw_active = 0;

	if (r_rt_water && scene_ready && rm_rtw_on(r_rt_water->value, rtw_device, rmb_rt_map_water()) && fd &&
		!(fd->rdflags & RDF_NOWORLDMODEL))
	{
		if (!rtw_opened && !rtw_refused)
		{
			char err[256];

			rtw_opened = rmb_rtw_open(err, sizeof(err));

			if (!rtw_opened)
			{
				rtw_refused = 1;
				R_Printf(PRINT_ALL, "ref_metal: rt water: %s; no water reflections\n", err);
			}
		}

		rtw_active = rtw_opened;
	}
	else if (rtw_opened && !RM_RTW_Wanted())
	{
		/* turned off: nothing kept */
		rmb_rtw_close();
		rtw_opened = 0;
	}

	if (!rtw_opened)
	{
		return;
	}

	n = rmb_rtw_stats(st, RMB_FRAMES_IN_FLIGHT);

	for (i = 0; i < n && r_rt_stats_ref && (r_rt_stats_ref->value >= 2); i++)
	{
		char line[RM_STATS_LINE];

		if (rm_rtw_stats_format(&st[i], line, sizeof(line)))
		{
			R_Printf(PRINT_ALL, "%s\n", line);
		}
	}
}

int
RM_RTW_Active(void)
{
	return rtw_active;
}

int
RM_RTW_ShowOnly(void)
{
	return rtw_active && r_rt_water_show && rm_rtw_show(r_rt_water_show->value);
}

void
RM_RTW_View(const rm_view_t *view)
{
	rm_rtw_view_t v;
	int w, h;

	if (!rtw_active)
	{
		return;
	}

	/* everything the trace needs that can fail, before the raster: the view's inverse and the outputs */
	RM_GB_TargetSize(&w, &h);

	if (!rm_rtw_view(view->proj, view->world, &view->vp, r_newrefdef.vieworg, gl_znear->value, r_newrefdef.time,
			rm_rtw_amp(r_rt_water_normal->value), rm_rtw_reflect(r_rt_water_reflect->value),
			(unsigned)rm_rtw_show(r_rt_water_show->value), &v) ||
		!rmb_rtw_prepare((unsigned long)w, (unsigned long)h))
	{
		rmb_rtw_set_view(NULL);
		return;
	}

	rmb_rtw_set_view(&v);
}
