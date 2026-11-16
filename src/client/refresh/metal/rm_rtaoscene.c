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
 * The engine side of ref_metal's ray-traced ambient occlusion (RT5,
 * TASK57; header/rm_rtao.h, header/rm_rtaoscene.h): the r_rt_ao cvars
 * (r_rt_ao, r_rt_ao_range, r_rt_ao_min, r_rt_ao_seed, and TASK157's
 * r_rt_ao_scale and r_rt_ao_rays) and the development
 * view r_rt_ao_show, the frame's decision (rm_rtao_on: with it the view
 * takes the G-buffer path), the view handed to the back end before the
 * raster, and the pass's r_rt_stats 2 line. With r_rt_ao 0 (the default)
 * nothing here builds or allocates anything.
 *
 * =======================================================================
 */

#include "header/local.h"
#include "header/rm_rt.h"
#include "header/rm_rtao.h"
#include "header/rm_rtaoscene.h"
#include "header/rm_gbufscene.h"

_Static_assert(RM_RT_RF_VIEWERMODEL == RF_VIEWERMODEL, "rm_rt.h's viewer-model flag is shared.h's");

static cvar_t *r_rt_ao;
static cvar_t *r_rt_ao_range;
static cvar_t *r_rt_ao_min;
static cvar_t *r_rt_ao_seed;
static cvar_t *r_rt_ao_show;
static cvar_t *r_rt_ao_scale;   /* TASK157: the traced size, a fraction of the view's width and height */
static cvar_t *r_rt_ao_rays;    /* TASK157: the rays a traced pixel */
static cvar_t *r_rt_stats_ref;

static int ao_device;           /* rmb_caps_t.raytracing */
static int ao_opened;           /* rmb_rtao_open succeeded */
static int ao_refused;          /* it failed in this context: not tried again */
static int ao_active;           /* this frame traces the AO */
static int ao_viewed;           /* RM_RTAO_View handed this frame's view to the back end */
static unsigned long ao_count;  /* the views traced, the noise's frame without a seed */

void
RM_RTAO_Init(const rmb_caps_t *caps)
{
	r_rt_ao = ri.Cvar_Get("r_rt_ao", "0", CVAR_ARCHIVE);
	r_rt_ao_range = ri.Cvar_Get("r_rt_ao_range", "128", CVAR_ARCHIVE);
	r_rt_ao_min = ri.Cvar_Get("r_rt_ao_min", "0.4", CVAR_ARCHIVE);
	r_rt_ao_seed = ri.Cvar_Get("r_rt_ao_seed", "0", 0);
	r_rt_ao_show = ri.Cvar_Get("r_rt_ao_show", "0", 0);
	r_rt_ao_scale = ri.Cvar_Get("r_rt_ao_scale", "0.5", CVAR_ARCHIVE);
	r_rt_ao_rays = ri.Cvar_Get("r_rt_ao_rays", "2", CVAR_ARCHIVE);
	r_rt_stats_ref = ri.Cvar_Get("r_rt_stats", "0", 0);
	ao_device = caps->raytracing != 0;
	ao_opened = ao_refused = ao_active = ao_viewed = 0;
	ao_count = 0;
}

void
RM_RTAO_Shutdown(void)
{
	if (ao_opened)
	{
		rmb_rtao_close();
	}

	ao_opened = ao_active = ao_viewed = 0;
}

int
RM_RTAO_Wanted(void)
{
	return r_rt_ao && rm_rt_wanted(r_rt_ao->value, ao_device);
}

void
RM_RTAO_Frame(const refdef_t *fd, int scene_ready)
{
	rm_rtao_stats_t st[RMB_FRAMES_IN_FLIGHT];
	int i, n;

	/*
	 * No view survives from an earlier frame: only RM_RTAO_View, before
	 * this frame's raster, hands the back end one (review round 1, N3),
	 * so a G-buffer view that does not trace the AO never reads a stale one.
	 */
	ao_active = ao_viewed = 0;
	rmb_rtao_set_view(NULL);

	if (r_rt_ao && rm_rtao_on(r_rt_ao->value, ao_device, r_fullbright->value) && scene_ready && fd &&
		!(fd->rdflags & RDF_NOWORLDMODEL))
	{
		if (!ao_opened && !ao_refused)
		{
			char err[256];

			ao_opened = rmb_rtao_open(err, sizeof(err));

			if (!ao_opened)
			{
				ao_refused = 1;
				R_Printf(PRINT_ALL, "ref_metal: rt ao: %s; no ambient occlusion\n", err);
			}
		}

		ao_active = ao_opened;
	}
	else if (ao_opened && !RM_RTAO_Wanted())
	{
		/* turned off: nothing kept */
		rmb_rtao_close();
		ao_opened = 0;
	}

	if (!ao_opened)
	{
		return;
	}

	n = rmb_rtao_stats(st, RMB_FRAMES_IN_FLIGHT);

	for (i = 0; i < n && r_rt_stats_ref && (r_rt_stats_ref->value >= 2); i++)
	{
		char line[RM_STATS_LINE];

		if (rm_rtao_stats_format(&st[i], line, sizeof(line)))
		{
			R_Printf(PRINT_ALL, "%s\n", line);
		}
	}
}

int
RM_RTAO_Active(void)
{
	return ao_active;
}

/* only a view that was handed over is traced and shown: without one the forward pass draws as without the AO (N2) */
int
RM_RTAO_ShowOnly(void)
{
	return ao_active && ao_viewed && r_rt_ao_show && rm_rtao_show(r_rt_ao_show->value);
}

void
RM_RTAO_View(const rm_view_t *view)
{
	rm_rtao_view_t v;
	int w, h, div;

	ao_viewed = 0;

	if (!ao_active)
	{
		return;
	}

	/* everything the trace needs that can fail, before the raster: the view's inverse and the outputs */
	RM_GB_TargetSize(&w, &h);
	div = rm_rtao_div(r_rt_ao_scale->value);

	if (!rm_rtao_view(view->proj, view->world, &view->vp, r_newrefdef.vieworg, gl_znear->value,
			rm_rtao_range(r_rt_ao_range->value), rm_rtao_floor(r_rt_ao_min->value),
			rm_rtao_frame(r_rt_ao_seed->value, ao_count), (unsigned)rm_rtao_show(r_rt_ao_show->value),
			(unsigned)div, (unsigned)rm_rtao_rays(r_rt_ao_rays->value), &v) ||
		!rmb_rtao_prepare((unsigned long)w, (unsigned long)h, (unsigned)div))
	{
		rmb_rtao_set_view(NULL);
		return;
	}

	rmb_rtao_set_view(&v);
	ao_viewed = 1;
	ao_count++;
}
