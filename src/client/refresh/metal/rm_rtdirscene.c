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
 * The engine side of RT-primary's direct light (TASK188, PM6 slice 1;
 * header/rm_rtdir.h, header/rm_rtdirscene.h): the cvars (r_rt_direct,
 * which the mode's overlay reads 1, r_rt_direct_exposure and the
 * development view r_rt_direct_show), the map's lights (TASK187's light
 * entities, rm_rtdata.c) and after them its emissive faces as qrad3's
 * surface lights (TASK188 slice 2), uploaded once a map, the exposure fitted from
 * each completed frame's sums, the frame's decision (with it the view
 * takes the G-buffer path), the view and the style table handed to the
 * back end before the raster, and the r_rt_stats lines. Under any
 * presentation but RT-primary nothing here builds or allocates anything.
 *
 * =======================================================================
 */

#include "header/local.h"
#include "header/rm_rt.h"
#include "header/rm_rtdir.h"
#include "header/rm_rtdirscene.h"
#include "header/rm_rtdata.h"
#include "header/rm_rtgate.h"
#include "header/rm_gbufscene.h"

static cvar_t *r_rt_direct;
static cvar_t *r_rt_direct_exposure;
static cvar_t *r_rt_direct_show;
static cvar_t *r_rt_stats_ref;

/* the map's lights, then its emissive faces (TASK188 slice 2) */
static rm_rtdir_light_t dir_lights[RM_RTDIR_MAX_UPLOAD];
static float dir_styles[3 * RM_RTL_STYLES];

static int dir_device;              /* rmb_caps_t.raytracing */
static int dir_opened;              /* rmb_rtdir_open succeeded */
static int dir_refused;             /* it failed in this context: not tried again */
static int dir_active;              /* this frame traces the direct light */
static int dir_viewed;              /* RM_RTDir_View handed this frame's view to the back end */
static const rm_world_t *dir_world; /* the map whose lights are uploaded */
static char dir_map[MAX_QPATH];     /* its model name (a new map's buffers may take the old one's address) */
static long dir_nlights;            /* the records uploaded: the lights, then the emissive faces */
static float dir_fit;               /* the exposure fitted from the last completed frame, 1 at a map's first */
static unsigned long dir_after;     /* the frame of the fit applied, or the last traced before the map's reset */
static int dir_fixed;               /* the frame's exposure is r_rt_direct_exposure's */

void
RM_RTDir_Init(const rmb_caps_t *caps)
{
	/* the switch defaults to 0 like every effect's (rm_rtgate_test --names); RT-primary's overlay reads it 1 */
	r_rt_direct = ri.Cvar_Get("r_rt_direct", "0", 0);
	r_rt_direct_exposure = ri.Cvar_Get("r_rt_direct_exposure", "0", CVAR_ARCHIVE);
	r_rt_direct_show = ri.Cvar_Get("r_rt_direct_show", "0", 0);
	r_rt_stats_ref = ri.Cvar_Get("r_rt_stats", "0", 0);
	dir_device = caps->raytracing != 0;
	dir_opened = dir_refused = dir_active = dir_viewed = dir_fixed = 0;
	dir_world = NULL;
	dir_map[0] = '\0';
	dir_nlights = 0;
	dir_fit = 1.0f;
	dir_after = 0;
}

void
RM_RTDir_Shutdown(void)
{
	if (dir_opened)
	{
		rmb_rtdir_close();
	}

	dir_opened = dir_active = dir_viewed = 0;
	dir_world = NULL;
	dir_nlights = 0;
}

/*
 * rm_rtdir_pack_emitters's face: the polygon of face index face from the
 * world's retained vertices (RM_BuildWorld's, one a polygon point, in the
 * faces lump's order), its plane normal flipped for SURF_PLANEBACK. 0 for
 * a face outside the world model's own range (a brush entity's: qrad3
 * lets only the world's faces emit).
 */
static int
RM_RTDir_Face(void *ctx, long face, rm_rtdir_face_t *out)
{
	const model_t *wm = ctx;
	const msurface_t *fa;
	long first = 0, num = wm->numsurfaces;
	int k;

	if (wm->numsubmodels > 0)
	{
		first = wm->submodels[0].firstmodelsurface;
		num = wm->submodels[0].nummodelsurfaces;
	}

	if ((face < first) || (face >= first + num) || (face < 0) || (face >= wm->numsurfaces) || !wm->world ||
		!wm->world->verts)
	{
		return 0;
	}

	fa = &wm->surfaces[face];

	if (!fa->plane || ((unsigned long)fa->first_vertex + fa->num_vertices > (unsigned long)wm->world->size.verts))
	{
		return 0;
	}

	out->pts = wm->world->verts[fa->first_vertex].pos;
	out->stride = (int)(sizeof(rm_wvert_t) / sizeof(float));
	out->n = (int)fa->num_vertices;

	for (k = 0; k < 3; k++)
	{
		out->normal[k] = (fa->flags & SURF_PLANEBACK) ? -fa->plane->normal[k] : fa->plane->normal[k];
	}

	return 1;
}

/* the map's lights and emissive faces into the back end's buffer, once a map (and after the pass opened again) */
static void
RM_RTDir_Lights(void)
{
	const model_t *wm = r_worldmodel;
	const rm_rtl_lights_t *l;
	const rm_rtl_emitters_t *e;
	const char *map = NULL, *emap = NULL;
	rm_rtdir_pack_t why;
	rm_rtdir_emitpack_t ewhy;
	long nemit = 0;

	if ((dir_world == wm->world) && !strcmp(dir_map, wm->name))
	{
		return;
	}

	dir_world = wm->world;
	Q_strlcpy(dir_map, wm->name, sizeof(dir_map));
	/* 1 from here, and no frame traced before (the old map's, still in flight) overwrites it */
	dir_fit = 1.0f;
	dir_after = rmb_rtdir_last_frame();
	memset(&why, 0, sizeof(why));
	memset(&ewhy, 0, sizeof(ewhy));

	/* the data gathered at this map's load, else none: a world with no light entity is lit by nothing here */
	l = RM_RTD_Lights(&map);
	dir_nlights = (l && map && !strcmp(map, wm->name)) ? rm_rtdir_pack(l, dir_lights, RM_RTL_MAX_LIGHTS, &why) : 0;

	/* the emissive faces after them, so a point light keeps its index */
	e = RM_RTD_Emitters(&emap);

	if (e && emap && !strcmp(emap, wm->name))
	{
		nemit = rm_rtdir_pack_emitters(e, RM_RTDir_Face, (void *)wm, dir_lights + dir_nlights, RM_RTDIR_MAX_EMITTERS,
				&ewhy);
	}

	if (!rmb_rtdir_set_lights(dir_lights, dir_nlights + nemit))
	{
		R_Printf(PRINT_ALL, "ref_metal: rt direct: %s: the lights cannot be uploaded; none\n", wm->name);
		dir_nlights = nemit = 0;
	}

	if (r_rt_stats_ref && r_rt_stats_ref->value)
	{
		char line[RM_STATS_LINE];

		why.kept = dir_nlights;
		ewhy.kept = nemit;

		if (rm_rtdir_map_format(wm->name, &why, line, sizeof(line)))
		{
			R_Printf(PRINT_ALL, "%s\n", line);
		}

		if (rm_rtdir_emit_format(wm->name, &ewhy, line, sizeof(line)))
		{
			R_Printf(PRINT_ALL, "%s\n", line);
		}
	}

	dir_nlights += nemit;
}

/*
 * The direct light's ask for the frame's ray-tracing scene (RM_RT_Wanted,
 * the map load's RM_RT_MapLoaded and the scene's keep flag): only under
 * RT-primary, on a device with ray tracing.
 */
int
RM_RTDir_Wanted(void)
{
	return r_rt_direct && (RM_PresentationMode() == RM_PRES_RTPRIMARY) &&
		rm_rt_wanted(RM_Effect(r_rt_direct), dir_device);
}

void
RM_RTDir_Frame(const refdef_t *fd, int scene_ready)
{
	rm_rtdir_stats_t st[RMB_FRAMES_IN_FLIGHT];
	int i, n;

	/* no view survives from an earlier frame: only RM_RTDir_View, before this frame's raster, hands one over */
	dir_active = dir_viewed = 0;
	rmb_rtdir_set_view(NULL, NULL);

	if (r_rt_direct && rm_rtdir_on(RM_PresentationMode(), RM_Effect(r_rt_direct), dir_device, r_fullbright->value) &&
		fd && !(fd->rdflags & RDF_NOWORLDMODEL) && r_worldmodel && r_worldmodel->world && scene_ready)
	{
		if (!dir_opened && !dir_refused)
		{
			char err[256];

			dir_opened = rmb_rtdir_open(err, sizeof(err));

			if (!dir_opened)
			{
				dir_refused = 1;
				R_Printf(PRINT_ALL, "ref_metal: rt direct: %s; no direct light\n", err);
			}

			dir_world = NULL;
		}

		if (dir_opened)
		{
			RM_RTDir_Lights();
		}

		dir_active = dir_opened;
	}

	if (dir_opened && !RM_RTDir_Wanted())
	{
		rmb_rtdir_close();
		dir_opened = 0;
		dir_world = NULL;
	}

	if (!dir_opened)
	{
		return;
	}

	/* the fit: the latest completed frame's sums since the map's reset give the exposure the next views take */
	n = rmb_rtdir_stats(st, RMB_FRAMES_IN_FLIGHT);
	dir_fit = rm_rtdir_fit_latest(st, n, &dir_after, dir_fit);

	for (i = 0; i < n; i++)
	{
		if (r_rt_stats_ref && (r_rt_stats_ref->value >= 2))
		{
			char line[RM_STATS_LINE];

			if (rm_rtdir_stats_format(&st[i], (int)dir_nlights, dir_fixed, line, sizeof(line)))
			{
				R_Printf(PRINT_ALL, "%s\n", line);
			}
		}
	}
}

int
RM_RTDir_Active(void)
{
	return dir_active;
}

/* only a view that was handed over is traced and shown */
int
RM_RTDir_ShowOnly(void)
{
	return dir_active && dir_viewed && r_rt_direct_show && rm_rtdir_show(r_rt_direct_show->value);
}

void
RM_RTDir_View(const rm_view_t *view)
{
	float st[MAX_LIGHTSTYLES][3], exposure;
	rm_rtdir_view_t v;
	int w, h, i, n = 0;

	dir_viewed = 0;

	if (!dir_active)
	{
		return;
	}

	/* the lightstyles as the lightmap takes them (rm_light.c): the refdef's rgb times r_modulate */
	if (r_newrefdef.lightstyles)
	{
		for (i = 0; i < MAX_LIGHTSTYLES; i++)
		{
			st[i][0] = r_newrefdef.lightstyles[i].rgb[0];
			st[i][1] = r_newrefdef.lightstyles[i].rgb[1];
			st[i][2] = r_newrefdef.lightstyles[i].rgb[2];
		}

		n = MAX_LIGHTSTYLES;
	}

	rm_rtdir_styles((const float (*)[3])st, n, r_modulate->value, dir_styles);
	exposure = rm_rtdir_exposure(RM_Effect(r_rt_direct_exposure), dir_fit, &dir_fixed);

	RM_GB_TargetSize(&w, &h);

	if (!rm_rtdir_view(view->proj, view->world, &view->vp, r_newrefdef.vieworg, gl_znear->value, exposure,
			(unsigned)dir_nlights, (unsigned)rm_rtdir_show(r_rt_direct_show->value), &v) ||
		!rmb_rtdir_prepare((unsigned long)w, (unsigned long)h))
	{
		rmb_rtdir_set_view(NULL, NULL);
		return;
	}

	rmb_rtdir_set_view(&v, dir_styles);
	dir_viewed = 1;
}
