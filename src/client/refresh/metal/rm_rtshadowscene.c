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
 * transmission and surface tables (built when the shadows are first on
 * for a map), the view and lights handed to the back end before the
 * raster (which is when r_dlight_term is decided), and the pass's
 * r_rt_stats 2 line. With r_rt_shadows 0 (the default) nothing here
 * builds or allocates anything and r_dlight_term stays on.
 *
 * =======================================================================
 */

#include "header/local.h"
#include "header/rm_rt.h"
#include "header/rm_rtshadow.h"
#include "header/rm_rtscene.h"
#include "header/rm_rtaoscene.h"
#include "header/rm_gbufscene.h"

static cvar_t *r_rt_shadows;
static cvar_t *r_rt_shadows_show;
static cvar_t *r_rt_stats_ref;

static int rts_device;          /* rmb_caps_t.raytracing */
static int rts_opened;          /* rmb_rts_open succeeded */
static int rts_refused;         /* it failed in this context: not tried again */
static int rts_active;          /* this frame traces the shadows */
static unsigned long rts_ntrans;    /* the map's triangles that pass light */

/* the map's surface tables (rm_rts_surface_map, the texture axes), uploaded when the pass is open */
static unsigned *rts_tri;
static unsigned *rts_vert;       /* the vertex-to-surface table of the G-buffer's lookup (TASK131), NULL without */
static float *rts_vecs;
static unsigned long rts_ntris, rts_nsurf, rts_nverts;
static int rts_uploaded;
static const rm_world_t *rts_tables_for;    /* the world the tables were built (or refused) for */

static void
RM_RTS_FreeTables(void)
{
	free(rts_tri);
	free(rts_vert);
	free(rts_vecs);
	rts_tri = NULL;
	rts_vert = NULL;
	rts_vecs = NULL;
	rts_ntris = rts_nsurf = rts_nverts = 0;
	rts_uploaded = 0;
}

/* the tables and the transmission buffer gone: the shadows are off, or the map changed */
static void
RM_RTS_DropMap(void)
{
	if (rts_tables_for && rts_ntrans)
	{
		rmb_rt_set_transmit(NULL, 0);
	}

	rts_ntrans = 0;
	rts_tables_for = NULL;
	RM_RTS_FreeTables();

	if (rts_opened)
	{
		rmb_rts_set_map(NULL, 0, NULL, 0, NULL, 0);
	}
}

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
	rts_tables_for = NULL;
	RM_RTS_FreeTables();
	r_dlight_term = true;
}

int
RM_RTS_Wanted(void)
{
	return r_rt_shadows && rm_rt_wanted(RM_Effect(r_rt_shadows), rts_device);
}

/* a new map's structures: the last map's tables go (its triangles are not the new map's); nothing is made here */
void
RM_RTS_MapBuilt(const model_t *wm)
{
	(void)wm;
	RM_RTS_DropMap();
}

/* the map's tables, when the shadows are first on for it (RM_RTS_Frame) */
static void
RM_RTS_BuildTables(const model_t *wm)
{
	const rm_world_t *w = wm ? wm->world : NULL;
	unsigned long ntris = w ? w->size.indices / 3 : 0, nt = 0;
	rm_rts_range_t *r;
	unsigned char *t;
	long i, n = wm ? wm->numsurfaces : 0;

	RM_RTS_DropMap();
	rts_tables_for = w;

	if (!ntris || (n <= 0))
	{
		return;
	}

	/* what a shadow ray keeps through each triangle */
	if ((t = malloc(ntris)))
	{
		if ((rm_rts_transmit(w->recs, (long)w->size.surfs, w->size.indices, t, ntris, &nt) == (long)ntris) && nt &&
			rmb_rt_set_transmit(t, ntris))
		{
			rts_ntrans = nt;
		}

		free(t);
	}

	/* each triangle's surface, and each surface's texture axes with their shifts (gl1's vecs) and its plane */
	r = malloc((size_t)n * sizeof(*r));
	rts_tri = malloc(ntris * sizeof(*rts_tri));
	rts_vecs = malloc((size_t)n * 12 * sizeof(*rts_vecs));

	if (!r || !rts_tri || !rts_vecs)
	{
		free(r);
		RM_RTS_FreeTables();
		return;
	}

	for (i = 0; i < n; i++)
	{
		const msurface_t *fa = &wm->surfaces[i];
		int k;

		r[i].first_index = fa->first_index;
		r[i].num_indices = fa->num_indices;
		r[i].unlit = (fa->samples == NULL);   /* gl1 lights it white and adds no dynamic light (R_BuildLightMap) */

		for (k = 0; k < 4; k++)
		{
			rts_vecs[i * 12 + k] = fa->texinfo ? fa->texinfo->vecs[0][k] : 0;
			rts_vecs[i * 12 + 4 + k] = fa->texinfo ? fa->texinfo->vecs[1][k] : 0;
		}

		/* its plane toward its front, as gl1 measures a light against it (R_MarkSurfaceLights, R_AddDynamicLights) */
		rm_rts_plane(fa->plane ? fa->plane->normal : NULL, fa->plane ? fa->plane->dist : 0,
				(fa->flags & SURF_PLANEBACK) != 0, &rts_vecs[i * 12 + 8]);
	}

	if (rm_rts_surface_map(r, n, ntris, rts_tri) < 0)
	{
		R_Printf(PRINT_ALL, "ref_metal: rt shadows: the surfaces do not map the index buffer; base axes only\n");
		free(r);
		RM_RTS_FreeTables();
		return;
	}

	free(r);
	rts_ntris = ntris;
	rts_nsurf = (unsigned long)n;

	/*
	 * TASK131: each vertex's surface, for the G-buffer's surface attachment
	 * (a static world draw writes its vertex); without it every pixel takes
	 * the eye ray, as before.
	 */
	{
		const unsigned long nverts = w->size.verts;
		rm_rts_vrange_t *vr = nverts ? malloc((size_t)n * sizeof(*vr)) : NULL;

		rts_vert = vr ? malloc(nverts * sizeof(*rts_vert)) : NULL;

		if (vr && rts_vert)
		{
			for (i = 0; i < n; i++)
			{
				const msurface_t *fa = &wm->surfaces[i];

				vr[i].first_vertex = fa->first_vertex;
				vr[i].num_vertices = fa->num_vertices;
				vr[i].unlit = (fa->samples == NULL);
			}
		}

		if (!vr || !rts_vert || (rm_rts_vertex_map(vr, n, nverts, rts_vert) < 0))
		{
			if (nverts)
			{
				R_Printf(PRINT_ALL, "ref_metal: rt shadows: the surfaces do not map the vertex buffer; the eye ray only\n");
			}

			free(rts_vert);
			rts_vert = NULL;
		}
		else
		{
			rts_nverts = nverts;
		}

		free(vr);
	}
}

void
RM_RTS_Frame(const refdef_t *fd, int scene_ready)
{
	rm_rts_stats_t st[RMB_FRAMES_IN_FLIGHT];
	int i, n;

	rts_active = 0;

	if (r_rt_shadows && rm_rts_on(RM_Effect(r_rt_shadows), rts_device, r_fullbright->value, gl1_dynamic->value,
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

		/* the map's tables when the shadows are first on for it, then up to the GPU once the pass is open */
		if (rts_opened && (rts_tables_for != r_worldmodel->world))
		{
			RM_RTS_BuildTables(r_worldmodel);
		}

		if (rts_opened && !rts_uploaded && rts_tri)
		{
			rts_uploaded = rmb_rts_set_map(rts_tri, rts_ntris, rts_vert, rts_nverts, rts_vecs, rts_nsurf);

			if (!rts_uploaded)
			{
				RM_RTS_FreeTables();
			}
		}
	}
	else if (rts_opened && !RM_RTS_Wanted())
	{
		/* turned off: nothing kept (the tables are built again when it is back on) */
		RM_RTS_DropMap();
		rmb_rts_close();
		rts_opened = 0;
	}

	/*
	 * gl1's dynamic-light term stays in the lightmaps until RM_RTS_View,
	 * before the raster, knows the view can trace and add it back.
	 */
	r_dlight_term = true;

	if (!rts_opened)
	{
		return;
	}

	n = rmb_rts_stats(st, RMB_FRAMES_IN_FLIGHT);

	/* the settings optimizer's cost hints (TASK203): kept only during its probe */
	for (i = 0; i < n; i++)
	{
		RM_OptCost_Add(RM_COST_SHADOWS, st[i].ms);
	}

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

/* TASK190 slice 3: the view is asked for, whether or not this frame traces it yet (rm_main.c sizes the scaler before Frame) */
int
RM_RTS_ShowAsked(void)
{
	return RM_RTS_Wanted() && r_rt_shadows_show && rm_rts_show(r_rt_shadows_show->value);
}

/* 1 when a brush entity of the frame is moved or turned: its surfaces are lit in its model's space */
static unsigned
RM_RTS_Moved(void)
{
	int i, ne = rm_entity_count(r_newrefdef.num_entities);

	for (i = 0; i < ne; i++)
	{
		const entity_t *e = &r_newrefdef.entities[i];

		if (e->model && (e->model->type == mod_brush) &&
			(e->origin[0] || e->origin[1] || e->origin[2] || e->angles[0] || e->angles[1] || e->angles[2]))
		{
			return 1;
		}
	}

	return 0;
}

/*
 * TASK131: the frame's entity table for the G-buffer's lookup: row i the
 * world-to-object transform of the refdef's entity i when it is a brush
 * model (whose draws the surface attachment names, RM_GB_EntityMotion),
 * none for the others (the raster never names them).
 */
static void
RM_RTS_Ents(void)
{
	static float rows[MTL_RTS_GB_ENTS * RM_RTS_ROW_FLOATS];
	int i, ne = rm_entity_count(r_newrefdef.num_entities), last = 0;

	if (ne > (int)MTL_RTS_GB_ENTS)
	{
		ne = (int)MTL_RTS_GB_ENTS;
	}

	for (i = 0; i < ne; i++)
	{
		const entity_t *e = &r_newrefdef.entities[i];
		float *row = &rows[i * RM_RTS_ROW_FLOATS];

		if (e->model && (e->model->type == mod_brush))
		{
			rm_rts_ent_rows(e->origin, e->angles, row);
			last = i + 1;
		}
		else
		{
			memset(row, 0, RM_RTS_ROW_FLOATS * sizeof(*row));   /* not usable (row[12] 0): no draw names it */
		}
	}

	rmb_rts_set_ents(rows, last);
}

void
RM_RTS_View(const rm_view_t *view)
{
	static rm_rts_light_t lights[MTL_RTS_MAX_LIGHTS];
	rm_rts_view_t v;
	int i, n, w, h;

	r_dlight_term = true;

	if (!rts_active)
	{
		return;
	}

	n = rm_dlight_count(r_newrefdef.num_dlights);

	if (n > MTL_RTS_MAX_LIGHTS)
	{
		n = MTL_RTS_MAX_LIGHTS;
	}

	/*
	 * Everything the trace needs that can fail is settled here, before the
	 * raster builds the lightmaps: the view's inverse and the kernel's
	 * outputs at the render size. Only then do the dynamic lights leave the
	 * lightmaps; otherwise gl1's term stays in them and nothing is traced
	 * (r_rt_shadows_show then draws its view with no light in range).
	 */
	RM_GB_TargetSize(&w, &h);

	if ((n <= 0) || !rm_rts_view(view->proj, view->world, &view->vp, r_newrefdef.vieworg, gl_znear->value, &v) ||
		!rmb_rts_prepare((unsigned long)w, (unsigned long)h))
	{
		memset(&v, 0, sizeof(v));
		v.show = (unsigned)rm_rts_show(r_rt_shadows_show->value);
		rmb_rts_set_view(&v, NULL, 0);
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
	v.moved = RM_RTS_Moved();
	v.carried = RM_RT_Carried();
	RM_RTS_Ents();
	rmb_rts_set_view(&v, lights, n);
	rmb_rts_temporal(RM_RTT_ViewFor(view), (unsigned long)w, (unsigned long)h);

	/* the dynamic-light term leaves the lightmaps: the resolve adds it back */
	r_dlight_term = false;
}
