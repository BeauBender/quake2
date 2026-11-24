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
 * cvars (r_rt_water, r_rt_water_normal, r_rt_water_reflect, TASK158's
 * r_rt_water_hits) and the
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
#include "header/rm_rtscene.h"
#include "header/rm_rtaoscene.h"
#include "header/rm_gbufscene.h"

static cvar_t *r_rt_water;
static cvar_t *r_rt_water_normal;
static cvar_t *r_rt_water_reflect;
static cvar_t *r_rt_water_show;
static cvar_t *r_rt_water_hits;   /* TASK158: 0 TASK58's screen-space hits, 1 world space (rm_rtw_hits) */
static cvar_t *r_rt_stats_ref;

static int rtw_device;      /* rmb_caps_t.raytracing */
static int rtw_opened;      /* rmb_rtw_open succeeded */
static int rtw_refused;     /* it failed in this context: not tried again */
static int rtw_active;      /* this frame traces the reflections */
static int rtw_viewed;      /* RM_RTW_View handed this frame's view to the back end */
static int rtw_ws_told;     /* TASK158: r_rt_water_hits 1's refusal was printed for this pass (rm_rtw_path) */

/* TASK158: the world-space shading's records, built once a map (and again after the pass reopens) */
static const model_t *rtw_shade_world;   /* the world they were built for, NULL for none */
static int rtw_shade_seq;                /* its registration sequence then */
static int rtw_slot_img[MTL_RTW_TEX_WORLD];   /* the table's images: their slots in gltextures */
static int rtw_nslots;

/*
 * TASK158: the shading records of the world's triangles and the images
 * of its texture table (rm_rtw_tex_slots, rm_rtw_shade_build), handed to
 * the back end. Without them (more images than the table holds, or the
 * records not covering the index buffer) no face is shaded in world
 * space: such a hit keeps the screen-space choice.
 */
static void
RM_RTW_BuildShade(void)
{
	const model_t *wm = r_worldmodel;
	const rm_world_t *w = wm ? wm->world : NULL;
	unsigned long ntris;
	unsigned *rec = NULL;
	int *img = NULL, *tex = NULL;
	long s, n;

	rtw_shade_world = wm;
	rtw_shade_seq = wm ? wm->registration_sequence : 0;
	rtw_nslots = 0;
	rmb_rtw_set_shade(NULL, 0);

	if (!w || !w->recs || (w->size.surfs < 1) || (w->size.indices < 3) || (w->size.surfs > 0x7fffffffUL))
	{
		return;
	}

	ntris = w->size.indices / 3;
	img = malloc(w->size.surfs * sizeof(int));
	tex = malloc(w->size.surfs * sizeof(int));
	rec = malloc(ntris * sizeof(unsigned));

	if (img && tex && rec)
	{
		for (s = 0; s < (long)w->size.surfs; s++)
		{
			const int i = w->recs[s].image;

			img[s] = ((i >= 0) && (i < MAX_GLTEXTURES)) ? i : -1;
		}

		n = rm_rtw_tex_slots(img, (long)w->size.surfs, rtw_slot_img, MTL_RTW_TEX_WORLD, tex);

		if ((n >= 0) && (rm_rtw_shade_build(w->recs, (long)w->size.surfs, w->size.indices, tex, rec, ntris) ==
				(long)ntris))
		{
			rtw_nslots = (int)n;
			rmb_rtw_set_shade(rec, ntris);
		}
		else
		{
			R_Printf(PRINT_ALL, "ref_metal: rt water: %s: no world-space shading (%ld images)\n", wm->name, n);
		}
	}

	free(img);
	free(tex);
	free(rec);
}

/*
 * TASK158: the view's world-space shading, as the stock draw shades a
 * face this frame (R_FlushChain's lightmap unit, the warp's and the
 * translucent faces' colours, r_fullbright, the flowing scroll), the
 * dynamic lights the lightmaps left out (RT3 traced: r_dlight_term is
 * off from its RM_RTS_View) and the sky box; then the frame's texture
 * table.
 */
static void
RM_RTW_Shading(const rm_view_t *view, rm_rtw_view_t *v)
{
	static float lights[MTL_RTS_MAX_LIGHTS * 8];
	int tids[MTL_RTW_TEX_WORLD], sky[6], i, n = 0;
	const int obb = rm_overbright(gl1_overbrightbits->value);
	rm_rtw_shading_t sh;
	float rotate = 0;
	vec3_t axis = { 0, 0, 0 };

	if (!rtw_shade_world || (rtw_shade_world != r_worldmodel) ||
		(rtw_shade_seq != r_worldmodel->registration_sequence))
	{
		RM_RTW_BuildShade();
	}

	memset(&sh, 0, sizeof(sh));
	sh.env = rm_lm_env(obb, gl_lightmap->value != 0);
	sh.scale = obb ? (float)obb : 1.0f;
	sh.flow = rm_flow_scroll(r_newrefdef.time);
	sh.fullbright = r_fullbright->value != 0;

	if (!rm_warp_color(obb != 0, rm_inverse_intensity_value, sh.warp))
	{
		sh.warp[0] = sh.warp[1] = sh.warp[2] = sh.warp[3] = 1;
	}

	rm_trans_color(rm_inverse_intensity_value, 1, sh.trans);

	if (!r_dlight_term)
	{
		n = rm_dlight_count(r_newrefdef.num_dlights);

		for (i = 0; i < n; i++)
		{
			const dlight_t *dl = &r_newrefdef.dlights[i];

			lights[i * 8 + 0] = dl->origin[0];
			lights[i * 8 + 1] = dl->origin[1];
			lights[i * 8 + 2] = dl->origin[2];
			lights[i * 8 + 3] = dl->intensity;
			lights[i * 8 + 4] = dl->color[0];
			lights[i * 8 + 5] = dl->color[1];
			lights[i * 8 + 6] = dl->color[2];
			lights[i * 8 + 7] = 0;
		}
	}

	sh.nlights = n;
	sh.sky = RM_SkyBox(sky, &rotate, axis);
	rm_rtw_sky_rows(r_newrefdef.time, rotate, axis, sh.sky_rows);
	rm_rtw_shading(&sh, view->proj, v);

	for (i = 0; i < rtw_nslots; i++)
	{
		const image_t *image = &gltextures[rtw_slot_img[i]];

		tids[i] = (image->texnum > 0) ? image->texnum : 0;
	}

	rmb_rtw_set_textures(tids, rtw_nslots, r_worldmodel->world ? r_worldmodel->world->lmtex : 0, sh.sky ? sky : NULL);
	rmb_rtw_set_lights(lights, (int)v->nlights);
}

void
RM_RTW_Init(const rmb_caps_t *caps)
{
	r_rt_water = ri.Cvar_Get("r_rt_water", "0", CVAR_ARCHIVE);
	r_rt_water_normal = ri.Cvar_Get("r_rt_water_normal", "0.08", CVAR_ARCHIVE);
	r_rt_water_reflect = ri.Cvar_Get("r_rt_water_reflect", "0.5", CVAR_ARCHIVE);
	r_rt_water_show = ri.Cvar_Get("r_rt_water_show", "0", 0);
	r_rt_water_hits = ri.Cvar_Get("r_rt_water_hits", "0", CVAR_ARCHIVE);   /* TASK158 */
	r_rt_stats_ref = ri.Cvar_Get("r_rt_stats", "0", 0);
	rtw_device = caps->raytracing != 0;
	rtw_opened = rtw_refused = rtw_active = rtw_viewed = rtw_ws_told = 0;
}

void
RM_RTW_Shutdown(void)
{
	if (rtw_opened)
	{
		rmb_rtw_close();
	}

	rtw_opened = rtw_active = rtw_viewed = 0;
	rtw_shade_world = NULL;
}

int
RM_RTW_Wanted(void)
{
	return r_rt_water && rm_rt_wanted(RM_Effect(r_rt_water), rtw_device);
}

void
RM_RTW_Frame(const refdef_t *fd, int scene_ready)
{
	rm_rtw_stats_t st[RMB_FRAMES_IN_FLIGHT];
	int i, n;

	/*
	 * No view survives from an earlier frame: only RM_RTW_View, before
	 * this frame's raster, hands the back end one (as RT5's, TASK57's
	 * review), so a G-buffer view that does not trace the reflections
	 * never reads a stale one.
	 */
	rtw_active = rtw_viewed = 0;
	rmb_rtw_set_view(NULL);

	if (r_rt_water && scene_ready && rm_rtw_on(RM_Effect(r_rt_water), rtw_device, rmb_rt_map_water()) && fd &&
		!(fd->rdflags & RDF_NOWORLDMODEL))
	{
		if (!rtw_opened && !rtw_refused)
		{
			char err[256];

			rtw_opened = rmb_rtw_open(err, sizeof(err));
			rtw_shade_world = NULL;   /* TASK158: the records go with the pass: built again for it */
			rtw_ws_told = 0;          /* and r_rt_water_hits 1's refusal is said again */

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
		rtw_shade_world = NULL;
	}

	if (!rtw_opened)
	{
		return;
	}

	n = rmb_rtw_stats(st, RMB_FRAMES_IN_FLIGHT);

	/* the settings optimizer's cost hints (TASK203): kept only during its probe */
	for (i = 0; i < n; i++)
	{
		RM_OptCost_Add(RM_COST_WATER, st[i].ms);
	}

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

/*
 * Only a view whose trace ran and whose show view was drawn leaves the
 * forward pass out: without them (the view not handed over, or the
 * trace's scene, materials or geometry not bound) the forward pass draws
 * as without the reflections.
 */
int
RM_RTW_ShowOnly(void)
{
	return rtw_active && rtw_viewed && r_rt_water_show && rm_rtw_show(r_rt_water_show->value) && rmb_rtw_shown();
}

void
RM_RTW_View(const rm_view_t *view)
{
	rm_rtw_view_t v;
	char err[192];
	int w, h, ws, say;

	rtw_viewed = 0;

	/*
	 * Not with r_rt_shadows_show's or r_rt_ao_show's view on (their
	 * RM_*_View ran first): the reflections are neither traced nor blended
	 * over it, so each pass's show view stays its own.
	 */
	if (!rtw_active || RM_RTS_ShowOnly() || RM_RTAO_ShowOnly())
	{
		rmb_rtw_set_view(NULL);
		return;
	}

	/* everything the trace needs that can fail, before the raster: the view's inverse and the outputs */
	RM_GB_TargetSize(&w, &h);

	/* TASK158: setting 1 where the pass built its resources; else TASK58's path, which needs none of them */
	ws = rm_rtw_path(r_rt_water_hits ? RM_Effect(r_rt_water_hits) : 0, rmb_rtw_ws_ok(err, sizeof(err)), &rtw_ws_told, &say);

	if (say)
	{
		R_Printf(PRINT_ALL, "ref_metal: rt water: %s; r_rt_water_hits 1 refused, its hits shaded as 0's\n", err);
	}

	if (!rm_rtw_view(view->proj, view->world, &view->vp, r_newrefdef.vieworg, gl_znear->value, r_newrefdef.time,
			rm_rtw_amp(RM_Effect(r_rt_water_normal)), rm_rtw_reflect(RM_Effect(r_rt_water_reflect)),
			(unsigned)rm_rtw_show(r_rt_water_show->value), &v) ||
		!rmb_rtw_prepare((unsigned long)w, (unsigned long)h, ws))
	{
		rmb_rtw_set_view(NULL);
		return;
	}

	/* TASK158: the world-space shading's per-frame work only on its path; TASK58's has none */
	if (ws)
	{
		RM_RTW_Shading(view, &v);
	}

	rmb_rtw_set_view(&v);
	rtw_viewed = 1;
}
