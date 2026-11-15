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
 * The engine side of ref_metal's ray-traced dynamic-light shadows (RT3,
 * TASK55; header/rm_rtshadow.h): the r_rt_shadows cvar and its
 * development view r_rt_shadows_show, the frame's decision (rm_rts_on:
 * with it, gl1's dynamic-light term leaves the lightmaps, r_dlight_term,
 * and the view takes the G-buffer path), the map's per-triangle
 * transmission, the view and lights handed to the back end, and the
 * pass's r_rt_stats 2 line. With r_rt_shadows 0 (the default) nothing
 * here builds or allocates anything and r_dlight_term stays on.
 *
 * =======================================================================
 */

#include "header/local.h"
#include "header/rm_rt.h"
#include "header/rm_rtshadow.h"
#include "header/rm_rtscene.h"

static cvar_t *r_rt_shadows;
static cvar_t *r_rt_shadows_show;
static cvar_t *r_rt_stats_ref;

static int rts_device;          /* rmb_caps_t.raytracing */
static int rts_opened;          /* rmb_rts_open succeeded */
static int rts_refused;         /* it failed in this context: not tried again */
static int rts_active;          /* this frame traces the shadows */
static unsigned long rts_ntrans;    /* the map's triangles that pass light */

void
RM_RTS_Init(const rmb_caps_t *caps)
{
	r_rt_shadows = ri.Cvar_Get("r_rt_shadows", "0", CVAR_ARCHIVE);
	r_rt_shadows_show = ri.Cvar_Get("r_rt_shadows_show", "0", 0);
	r_rt_stats_ref = ri.Cvar_Get("r_rt_stats", "0", 0);
	rts_device = caps->raytracing != 0;
	rts_opened = rts_refused = rts_active = 0;
	rts_ntrans = 0;
	r_dlight_term = true;
}

void
RM_RTS_Shutdown(void)
{
	if (rts_opened)
	{
		rmb_rts_close();
	}

	rts_opened = rts_active = 0;
	rts_ntrans = 0;
	r_dlight_term = true;
}

int
RM_RTS_Wanted(void)
{
	return r_rt_shadows && rm_rt_wanted(r_rt_shadows->value, rts_device);
}

void
RM_RTS_MapBuilt(const rm_surfrec_t *recs, long nrecs, unsigned long nindices)
{
	unsigned long ntris = nindices / 3, nt = 0;
	unsigned char *t;

	rts_ntrans = 0;

	if (!ntris || !(t = malloc(ntris)))
	{
		return;
	}

	if ((rm_rts_transmit(recs, nrecs, nindices, t, ntris, &nt) == (long)ntris) && nt &&
		rmb_rt_set_transmit(t, ntris))
	{
		rts_ntrans = nt;
	}

	free(t);
}

void
RM_RTS_Frame(const refdef_t *fd, int scene_ready)
{
	rm_rts_stats_t st[RMB_FRAMES_IN_FLIGHT];
	int i, n;

	rts_active = 0;

	if (r_rt_shadows && rm_rts_on(r_rt_shadows->value, rts_device, r_fullbright->value, gl1_dynamic->value,
			gl1_flashblend->value) && scene_ready && fd && !(fd->rdflags & RDF_NOWORLDMODEL))
	{
		if (!rts_opened && !rts_refused)
		{
			char err[256];

			rts_opened = rmb_rts_open(err, sizeof(err));

			if (!rts_opened)
			{
				rts_refused = 1;
				R_Printf(PRINT_ALL, "ref_metal: rt shadows: %s; the dynamic lights stay in the lightmaps\n", err);
			}
		}

		rts_active = rts_opened;
	}
	else if (rts_opened && !RM_RTS_Wanted())
	{
		/* turned off: nothing kept */
		rmb_rts_close();
		rts_opened = 0;
	}

	/* the dynamic-light term leaves the lightmaps only on a frame that adds it back */
	r_dlight_term = !rts_active;

	if (!rts_opened)
	{
		return;
	}

	n = rmb_rts_stats(st, RMB_FRAMES_IN_FLIGHT);

	for (i = 0; i < n && r_rt_stats_ref && (r_rt_stats_ref->value >= 2); i++)
	{
		char line[RM_STATS_LINE];

		if (rm_rts_stats_format(&st[i], line, sizeof(line)))
		{
			R_Printf(PRINT_ALL, "%s\n", line);
		}
	}
}

int
RM_RTS_Active(void)
{
	return rts_active;
}

int
RM_RTS_ShowOnly(void)
{
	return rts_active && r_rt_shadows_show && rm_rts_show(r_rt_shadows_show->value);
}

void
RM_RTS_View(const rm_view_t *view)
{
	static rm_rts_light_t lights[MTL_RTS_MAX_LIGHTS];
	rm_rts_view_t v;
	int i, n;

	if (!rts_active)
	{
		return;
	}

	n = rm_dlight_count(r_newrefdef.num_dlights);

	if (n > MTL_RTS_MAX_LIGHTS)
	{
		n = MTL_RTS_MAX_LIGHTS;
	}

	if ((n <= 0) || !rm_rts_view(view->proj, view->world, &view->vp, r_newrefdef.vieworg, gl_znear->value, &v))
	{
		rmb_rts_set_view(NULL, NULL, 0);
		return;
	}

	/* the renderer's checked copy (R_TakeDlights) */
	for (i = 0; i < n; i++)
	{
		const dlight_t *dl = &r_newrefdef.dlights[i];

		lights[i].origin[0] = dl->origin[0];
		lights[i].origin[1] = dl->origin[1];
		lights[i].origin[2] = dl->origin[2];
		lights[i].origin[3] = dl->intensity;
		lights[i].color[0] = dl->color[0];
		lights[i].color[1] = dl->color[1];
		lights[i].color[2] = dl->color[2];
		lights[i].color[3] = 0;
	}

	v.trans = rts_ntrans != 0;
	v.show = (unsigned)rm_rts_show(r_rt_shadows_show->value);
	rmb_rts_set_view(&v, lights, n);
}
