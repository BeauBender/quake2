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
 * surface lights (TASK188 slice 2), uploaded once a map, with the map's
 * exposure fitted then from a sample of its luxels (slice 3c), the frame's decision (with it the view
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
static long dir_nself;              /* the emissive faces drawn at their own light (TASK188 slice 3b) */
static float dir_styles[3 * RM_RTL_STYLES];

static int dir_device;              /* rmb_caps_t.raytracing */
static int dir_opened;              /* rmb_rtdir_open succeeded */
static int dir_refused;             /* it failed in this context: not tried again */
static int dir_active;              /* this frame traces the direct light */
static int dir_viewed;              /* RM_RTDir_View handed this frame's view to the back end */
static const rm_world_t *dir_world; /* the map whose lights are uploaded */
static char dir_map[MAX_QPATH];     /* its model name (a new map's buffers may take the old one's address) */
static long dir_nlights;            /* the records uploaded: the lights, then the emissive faces */
static int dir_fixed;               /* the frame's exposure is r_rt_direct_exposure's */
static float dir_fit;               /* the map's exposure, fitted at its load (TASK188 slice 3c, rm_rtdir_fit_map) */

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

/* the deepest the fit's line test walks the map's tree; past it a line counts as blocked */
#define RM_RTDIR_FIT_DEPTH 1024

/* 1 when the segment a to b crosses no solid leaf of the tree under node (the world's line test, as R_RecursiveLightPoint walks) */
static int
RM_RTDir_ClearNode(const mnode_t *node, const float *a, const float *b, int depth)
{
	float front, back, frac, mid[3];
	int side, k;

	if (!node || (depth > RM_RTDIR_FIT_DEPTH))
	{
		return 0;
	}

	if (node->contents != CONTENTS_NODE)
	{
		return !(node->contents & CONTENTS_SOLID);
	}

	front = DotProduct(a, node->plane->normal) - node->plane->dist;
	back = DotProduct(b, node->plane->normal) - node->plane->dist;
	side = front < 0;

	if ((back < 0) == side)
	{
		return RM_RTDir_ClearNode(node->children[side], a, b, depth + 1);
	}

	frac = front / (front - back);

	for (k = 0; k < 3; k++)
	{
		mid[k] = a[k] + (b[k] - a[k]) * frac;
	}

	return RM_RTDir_ClearNode(node->children[side], a, mid, depth + 1) &&
		RM_RTDir_ClearNode(node->children[!side], mid, b, depth + 1);
}

/* the exposure fit's shadow ray (rm_rtdir_clear_fn): the world's solid leaves alone, as qrad3 traces */
static int
RM_RTDir_Clear(void *ctx, const float *a, const float *b)
{
	const model_t *wm = ctx;

	return RM_RTDir_ClearNode(wm->nodes, a, b, 0);
}

/*
 * TASK188 slice 3c: the exposure fit's samples, at most cap: of every
 * n-th world face (about RM_RTDIR_FIT_FACES of them) that has a
 * lightmap and is not a fixture (SURF_LIGHT, whose own luxels qrad3 fills
 * with its own light) nor translucent, its centre and the points half way
 * from it to each corner, each with the face's lightmap at its luxel
 * (rm_rtdir_fit_luxel, every style at 1). In the face's order, so the
 * samples are a function of the map alone.
 */
static long
RM_RTDir_FitPoints(const model_t *wm, rm_rtdir_fitpt_t *pts, long cap)
{
	long first = 0, num = wm->numsurfaces, face, stride, n = 0;

	if (wm->numsubmodels > 0)
	{
		first = wm->submodels[0].firstmodelsurface;
		num = wm->submodels[0].nummodelsurfaces;
	}

	stride = 1 + num / RM_RTDIR_FIT_FACES;

	for (face = first; (face < first + num) && (face < wm->numsurfaces) && (n < cap); face += stride)
	{
		const msurface_t *fa = &wm->surfaces[face];
		const mtexinfo_t *tex = fa->texinfo;
		rm_rtdir_face_t f;
		float c[3] = { 0, 0, 0 };
		int smax, tmax, v, k;

		if ((face < 0) || !fa->samples || !tex || (tex->flags & (SURF_LIGHT | SURF_TRANS33 | SURF_TRANS66 | SURF_SKY | SURF_WARP)) ||
			!rm_lm_size(fa->extents[0], fa->extents[1], &smax, &tmax) || !RM_RTDir_Face((void *)wm, face, &f) || (f.n < 3))
		{
			continue;
		}

		for (v = 0; v < f.n; v++)
		{
			for (k = 0; k < 3; k++)
			{
				c[k] += f.pts[v * f.stride + k] / (float)f.n;
			}
		}

		for (v = -1; (v < f.n) && (n < cap); v++)
		{
			rm_rtdir_fitpt_t *p = &pts[n++];

			for (k = 0; k < 3; k++)
			{
				p->pos[k] = (v < 0) ? c[k] : 0.5f * (c[k] + f.pts[v * f.stride + k]);
				p->normal[k] = f.normal[k];
			}

			rm_rtdir_fit_luxel(fa->samples, rm_lm_nummaps(fa->styles), smax, tmax,
					(DotProduct(p->pos, tex->vecs[0]) + tex->vecs[0][3] - fa->texturemins[0]) / 16.0f,
					(DotProduct(p->pos, tex->vecs[1]) + tex->vecs[1][3] - fa->texturemins[1]) / 16.0f, p->lm);
		}
	}

	return n;
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
	dir_fit = 1.0f;
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

	/* TASK188 slice 3c: the map's exposure, once at its load, from a sample of its luxels (rm_rtdir_fit_map) */
	{
		rm_rtdir_fitpt_t *pts = malloc(RM_RTDIR_FIT_POINTS * sizeof(*pts));
		const long npts = (pts && wm->nodes) ? RM_RTDir_FitPoints(wm, pts, RM_RTDIR_FIT_POINTS) : 0;
		rm_rtdir_mapfit_t f;

		dir_fit = rm_rtdir_fit_map(dir_lights, dir_nlights + nemit, pts, npts, RM_RTDir_Clear, (void *)wm, &f);
		free(pts);

		if (r_rt_stats_ref && r_rt_stats_ref->value)
		{
			R_Printf(PRINT_ALL, "ref_metal: rt direct: %s: exposure fit %.4f over %lu luxels of %ld (%lu near black, "
					"%lu clipped), %lu lines\n", wm->name, (double)dir_fit, f.n, npts, f.dark, f.clipped, f.rays);
		}
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

	/* TASK188 slice 3b: each emissive face's own light, per triangle of the map's index buffer */
	{
		const unsigned long ntris = wm->world->size.indices / 3;
		unsigned *self = (e && emap && !strcmp(emap, wm->name) && ntris) ? malloc(ntris * sizeof(*self)) : NULL;

		dir_nself = self ? rm_rtdir_self_table(e, RM_RTDir_Face, (void *)wm, wm->world->recs,
				(long)wm->world->size.surfs, self, ntris) : 0;

		if ((dir_nself < 0) || !rmb_rtdir_set_self(self, (dir_nself > 0) ? ntris : 0))
		{
			dir_nself = 0;
			rmb_rtdir_set_self(NULL, 0);
		}

		free(self);

		if (r_rt_stats_ref && r_rt_stats_ref->value)
		{
			R_Printf(PRINT_ALL, "ref_metal: rt direct: %s: %ld emissive faces drawn at their own light\n", wm->name,
					dir_nself);
		}
	}
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

	/* the lines of the frames completed since */
	n = rmb_rtdir_stats(st, RMB_FRAMES_IN_FLIGHT);

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

	/* TASK188 slice 3b: the frame's dynamic lights, the renderer's checked copy (R_TakeDlights) in its order */
	{
		rm_rtdir_dlight_t dl[MTL_RTDIR_MAX_DLIGHTS];
		const int nd = r_newrefdef.dlights ? rm_dlight_count(r_newrefdef.num_dlights) : 0;
		int k = 0;

		for (i = 0; (i < nd) && (k < MTL_RTDIR_MAX_DLIGHTS); i++)
		{
			const dlight_t *d = &r_newrefdef.dlights[i];

			k += rm_rtdir_dlight(d->origin, d->color, d->intensity, &dl[k]);
		}

		rmb_rtdir_set_dlights(dl, k);
	}
	/* r_rt_direct_exposure's, else the map's fit (TASK188 slice 3c) */
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
