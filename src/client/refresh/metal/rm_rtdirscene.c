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
 * back end before the raster, and the r_rt_stats lines. TASK189 (PM7
 * slice 1) adds the sky light (r_rt_direct_sky, its noise's seed
 * r_rt_direct_seed) and the sun (TASK56's direction and r_rt_sunscale,
 * RM_RTSun_Primary) to the view; slice 2 the bounce (r_rt_direct_bounce,
 * the map's per-triangle bounce records with their textures' means and
 * the world's lightmap array). TASK190 slice 1 (PM8) accumulates the light
 * over the views with RT7's reset state (RM_RTT_ViewFor), by r_rt_temporal.
 * Under any presentation but RT-primary nothing here builds or allocates
 * anything.
 *
 * =======================================================================
 */

#include "header/local.h"
#include "header/rm_rt.h"
#include "header/rm_rtdir.h"
#include "header/rm_rtdirscene.h"
#include "header/rm_rtdata.h"
#include "header/rm_rtgate.h"
#include "header/rm_rtao.h"
#include "header/rm_rtsunscene.h"
#include "header/rm_rtaoscene.h"
#include "header/rm_gbufscene.h"

static cvar_t *r_rt_direct;
static cvar_t *r_rt_direct_exposure;
static cvar_t *r_rt_direct_show;
static cvar_t *r_rt_direct_sky;
static cvar_t *r_rt_direct_seed;
static cvar_t *r_rt_direct_bounce;  /* TASK189 slice 2 */
static cvar_t *r_rt_stats_ref;
static unsigned long dir_count;     /* TASK189: the views traced, the sky ray's noise frame without a seed */

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
static float dir_fit;               /* the map's exposure, fitted at its load (TASK188 slice 3c, rm_rtdir_fit_tree) */
static const rm_world_t *fit_world; /* the map dir_fit is of: kept across a reopen of the pass, reset by a new map */
static char fit_map[MAX_QPATH];

void
RM_RTDir_Init(const rmb_caps_t *caps)
{
	/* the switch defaults to 0 like every effect's (rm_rtgate_test --names); RT-primary's overlay reads it 1 */
	r_rt_direct = ri.Cvar_Get("r_rt_direct", "0", 0);
	r_rt_direct_exposure = ri.Cvar_Get("r_rt_direct_exposure", "0", CVAR_ARCHIVE);
	r_rt_direct_show = ri.Cvar_Get("r_rt_direct_show", "0", 0);
	/* TASK189: the sky light's scale, and its noise's seed (r_rt_ao_seed's rule) */
	r_rt_direct_sky = ri.Cvar_Get("r_rt_direct_sky", "1", CVAR_ARCHIVE);
	r_rt_direct_seed = ri.Cvar_Get("r_rt_direct_seed", "0", 0);
	r_rt_direct_bounce = ri.Cvar_Get("r_rt_direct_bounce", "1", CVAR_ARCHIVE);
	r_rt_stats_ref = ri.Cvar_Get("r_rt_stats", "0", 0);
	dir_count = 0;
	dir_device = caps->raytracing != 0;
	dir_opened = dir_refused = dir_active = dir_viewed = dir_fixed = 0;
	dir_world = fit_world = NULL;
	dir_map[0] = fit_map[0] = '\0';
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
	dir_world = fit_world = NULL;
	fit_map[0] = '\0';
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

/* the engine's values of the fit's copies (rm_rtdir.h) */
typedef char rm_rtdir_surf_same[((RM_RTDIR_SURF_LIGHT == SURF_LIGHT) && (RM_RTDIR_SURF_SKY == SURF_SKY) &&
	(RM_RTDIR_SURF_WARP == SURF_WARP) && (RM_RTDIR_SURF_TRANS33 == SURF_TRANS33) &&
	(RM_RTDIR_SURF_TRANS66 == SURF_TRANS66) && (RM_RTDIR_CONTENTS_SOLID == CONTENTS_SOLID)) ? 1 : -1];

/* rm_rtdir_tree_t's read: a node or a leaf of the world's tree (mleaf_t shares mnode_t's contents) */
static void
RM_RTDir_Node(const void *p, rm_rtdir_node_t *out)
{
	const mnode_t *node = p;

	memset(out, 0, sizeof(*out));
	out->leaf = node->contents != CONTENTS_NODE;
	out->contents = node->contents;

	if (!out->leaf)
	{
		VectorCopy(node->plane->normal, out->normal);
		out->dist = node->plane->dist;
		out->child[0] = node->children[0];
		out->child[1] = node->children[1];
	}
}

/*
 * TASK188 slice 3c: the exposure fit's samples, at most cap: of every
 * n-th world face (about RM_RTDIR_FIT_FACES of them) that takes samples
 * (rm_rtdir_fit_face_ok: a lightmap, not a fixture, whose own luxels
 * qrad3 fills with its own light, nor translucent), its centre and the
 * points half way from it to each corner, each with the face's lightmap
 * at its luxel (rm_rtdir_fit_st, rm_rtdir_fit_luxel, every style at 1).
 * Past cap, every k-th of those points (rm_rtdir_fit_every), so the
 * samples cover the whole face list. In the faces' order, so the samples
 * are a function of the map alone. With pts NULL, the points counted.
 */
static long
RM_RTDir_FitPoints(const model_t *wm, rm_rtdir_fitpt_t *pts, long cap, long every)
{
	long first = 0, num = wm->numsurfaces, face, stride, n = 0, g = 0;

	if (wm->numsubmodels > 0)
	{
		first = wm->submodels[0].firstmodelsurface;
		num = wm->submodels[0].nummodelsurfaces;
	}

	stride = 1 + num / RM_RTDIR_FIT_FACES;

	for (face = first; (face < first + num) && (face < wm->numsurfaces) && (!pts || (n < cap)); face += stride)
	{
		const msurface_t *fa;
		const mtexinfo_t *tex;
		rm_rtdir_face_t f;
		float c[3] = { 0, 0, 0 };
		int smax, tmax, v, k;

		if (face < 0)
		{
			continue;
		}

		fa = &wm->surfaces[face];
		tex = fa->texinfo;

		if (!tex || !rm_rtdir_fit_face_ok(tex->flags, fa->samples != NULL) ||
			!rm_lm_size(fa->extents[0], fa->extents[1], &smax, &tmax) || !RM_RTDir_Face((void *)wm, face, &f) || (f.n < 3))
		{
			continue;
		}

		if (!pts)
		{
			n += f.n + 1;
			continue;
		}

		for (v = 0; v < f.n; v++)
		{
			for (k = 0; k < 3; k++)
			{
				c[k] += f.pts[v * f.stride + k] / (float)f.n;
			}
		}

		for (v = -1; (v < f.n) && (n < cap); v++, g++)
		{
			rm_rtdir_fitpt_t *p;
			float st[2];

			if (g % every)
			{
				continue;
			}

			p = &pts[n++];

			for (k = 0; k < 3; k++)
			{
				p->pos[k] = (v < 0) ? c[k] : 0.5f * (c[k] + f.pts[v * f.stride + k]);
				p->normal[k] = f.normal[k];
			}

			rm_rtdir_fit_st(p->pos, tex->vecs, fa->texturemins, st);
			rm_rtdir_fit_luxel(fa->samples, rm_lm_nummaps(fa->styles), smax, tmax, st[0], st[1], p->lm);
		}
	}

	return n;
}

/*
 * TASK189 slice 2: an image's albedo for the bounce, the mean of its .wal's
 * texels through the palette (rm_rtl_wal_mean, as TASK187 reads an
 * emitter's), RM_RTL_UNREAD_GREY for an image that is not a .wal or cannot
 * be read. One read a distinct image a map.
 */
static void
RM_RTDir_Albedo(int img, float (*cache)[3], unsigned char *have, float *out)
{
	const image_t *image;
	size_t len;
	byte *wal = NULL;
	int n, k;

	for (k = 0; k < 3; k++)
	{
		out[k] = RM_RTL_UNREAD_GREY;
	}

	if ((img < 0) || (img >= MAX_GLTEXTURES))
	{
		return;
	}

	if (!have[img])
	{
		image = &gltextures[img];
		len = strlen(image->name);
		have[img] = 1;
		cache[img][0] = cache[img][1] = cache[img][2] = RM_RTL_UNREAD_GREY;

		if ((len > 4) && !Q_stricmp(image->name + len - 4, ".wal"))
		{
			n = ri.FS_LoadFile(image->name, (void **)&wal);

			if (wal)
			{
				if ((n <= 0) || !rm_rtl_wal_mean(wal, (size_t)n, d_8to24table, cache[img]))
				{
					cache[img][0] = cache[img][1] = cache[img][2] = RM_RTL_UNREAD_GREY;
				}

				ri.FS_FreeFile(wal);
			}
		}
	}

	for (k = 0; k < 3; k++)
	{
		out[k] = cache[img][k];
	}
}

/*
 * TASK189 slice 2: the map's bounce records (rm_rtdir_bounce_table), each
 * face's albedo its texture's mean, with the world's lightmap array; none
 * (the bounce then reads nothing) when memory runs out.
 */
static void
RM_RTDir_Bounce(const model_t *wm)
{
	static float cache[MAX_GLTEXTURES][3];
	static unsigned char have[MAX_GLTEXTURES];
	const rm_world_t *w = wm->world;
	const unsigned long ntris = w->size.indices / 3;
	const long nrecs = (long)w->size.surfs;
	unsigned *rec = (ntris && w->recs) ? malloc(ntris * sizeof(*rec)) : NULL;
	float (*albedo)[3] = (rec && (nrecs > 0)) ? malloc((size_t)nrecs * sizeof(*albedo)) : NULL;
	long s, lit = -1;

	memset(have, 0, sizeof(have));

	if (albedo)
	{
		for (s = 0; s < nrecs; s++)
		{
			RM_RTDir_Albedo(w->recs[s].image, cache, have, albedo[s]);
		}

		lit = rm_rtdir_bounce_table(w->recs, nrecs, (const float (*)[3])albedo, rec, ntris);
	}

	if ((lit < 0) || !rmb_rtdir_set_bounce(rec, ntris, w->lmtex))
	{
		lit = 0;
		rmb_rtdir_set_bounce(NULL, 0, 0);
	}

	free(albedo);
	free(rec);

	if (r_rt_stats_ref && r_rt_stats_ref->value)
	{
		R_Printf(PRINT_ALL, "ref_metal: rt direct: %s: %ld of %lu triangles read a lightmap at a bounce's hit\n",
				wm->name, lit, ntris);
	}
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

	/*
	 * TASK188 slice 3c: the map's exposure, once a map, from a sample of its luxels (rm_rtdir_fit_tree); a
	 * reopen of the pass on the same map (r_rt_direct or the presentation away and back) keeps it
	 */
	if ((fit_world != wm->world) || strcmp(fit_map, wm->name))
	{
		const long every = rm_rtdir_fit_every(RM_RTDir_FitPoints(wm, NULL, 0, 1), RM_RTDIR_FIT_POINTS);
		rm_rtdir_fitpt_t *pts = malloc(RM_RTDIR_FIT_POINTS * sizeof(*pts));
		const long npts = (pts && wm->nodes) ? RM_RTDir_FitPoints(wm, pts, RM_RTDIR_FIT_POINTS, every) : 0;
		rm_rtdir_tree_t tree = { wm->nodes, RM_RTDir_Node, 0, RM_RTDIR_FIT_VISITS, 0 };
		rm_rtdir_mapfit_t f;

		/* the old map's fit never survives into a new one's */
		fit_world = wm->world;
		Q_strlcpy(fit_map, wm->name, sizeof(fit_map));
		dir_fit = 1.0f;
		dir_fit = rm_rtdir_fit_tree(dir_lights, dir_nlights + nemit, pts, npts, &tree, &f);
		free(pts);

		if (f.over)
		{
			R_Printf(PRINT_ALL, "ref_metal: rt direct: %s: the exposure fit's line tests passed %lu node visits; "
					"exposure 1\n", wm->name, (unsigned long)RM_RTDIR_FIT_VISITS);
		}

		if (r_rt_stats_ref && r_rt_stats_ref->value)
		{
			R_Printf(PRINT_ALL, "ref_metal: rt direct: %s: exposure fit %.4f over %lu luxels of %ld (%lu near black, "
					"%lu clipped), %lu lines, %lu node visits, every %ld-th point\n", wm->name, (double)dir_fit, f.n,
					npts, f.dark, f.clipped, f.rays, f.visits, every);
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

	RM_RTDir_Bounce(wm);
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

			/* TASK189: the sky light's and the sun's rays */
			if (rm_rtdir_open_format(&st[i], line, sizeof(line)))
			{
				R_Printf(PRINT_ALL, "%s\n", line);
			}

			/* TASK190 slice 1: the accumulation's own GPU time, on a frame that accumulated */
			if (st[i].accumulated)
			{
				if (st[i].tms >= 0)
				{
					R_Printf(PRINT_ALL, "ref_metal: rt direct temporal: frame %lu: %.3f ms\n", st[i].frame, st[i].tms);
				}
				else
				{
					R_Printf(PRINT_ALL, "ref_metal: rt direct temporal: frame %lu: - ms\n", st[i].frame);
				}
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

/* TASK190 slice 3: the view is asked for, whether or not this frame traces it yet (rm_main.c sizes the scaler before Frame) */
int
RM_RTDir_ShowAsked(void)
{
	return RM_RTDir_Wanted() && r_rt_direct_show && rm_rtdir_show(r_rt_direct_show->value);
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

	/*
	 * TASK189: the sky light, the mean of the sky box's five faces above the
	 * horizon (gl1's order, dn last) times r_rt_direct_sky, none without a
	 * sky image; the sun, TASK56's direction and colour
	 */
	{
		float means[6][3], sky[3], sun_dir[3], sun[3];
		int have[6], has_sky;

		RM_SkyMeans(means, have);
		has_sky = rm_rtl_sky_mean((const float (*)[3])means, have, 5, sky);
		RM_RTSun_Primary(sun_dir, sun);
		rm_rtdir_view_open(&v, has_sky ? sky : NULL, RM_Effect(r_rt_direct_sky), sun_dir, sun,
				rm_rtao_frame(r_rt_direct_seed->value, dir_count));
		dir_count++;
	}

	/* TASK189 slice 2: the bounce's scale */
	rm_rtdir_view_bounce(&v, RM_Effect(r_rt_direct_bounce));

	rmb_rtdir_set_view(&v, dir_styles);

	/*
	 * TASK190 slice 1 (PM8): the light accumulated over the views, by
	 * r_rt_temporal as the user set it (no presentation overlays it), with
	 * the one reset state RT7's effects share: at 0 nothing is made and
	 * the trace's light is the resolve's, bit for bit
	 */
	rmb_rtdir_temporal(RM_RTT_ViewFor(view), (unsigned long)w, (unsigned long)h);
	dir_viewed = 1;
}
