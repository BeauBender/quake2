/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 *
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
 * The engine side of ref_metal's G-buffer (RT2, TASK54; header/
 * rm_gbufscene.h). The previous frame of an entity: entity_t carries no
 * stable id, so each world view records every entity's model, origin,
 * old origin, angles, frames, lerp and flags (rm_gb_hist_t, header/
 * rm_gbuf.h), and the next view matches each of its entities, in list
 * order, to the nearest unmatched previous one with the same model and
 * the same RF_WEAPONMODEL bit within RM_GB_MATCH_DIST (rm_gb_match). An
 * entity without a match (a first frame, a new entity, a teleport) takes
 * its own state as the previous one: its pixels move by the camera's
 * motion alone.
 *
 * =======================================================================
 */

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "header/local.h"
#include "header/rm_gbufscene.h"
#include "header/rm_fxscene.h"
#include "common/mtl_matrix.h"
#include "common/mtl_shader_math.h"
#include "common/mtl_gbuf_math.h"

static cvar_t *r_rt_gbuffer;
static cvar_t *r_rt_motion;
static cvar_t *r_rt_gbuffer_scale;
static cvar_t *r_rt_jitter_x;
static cvar_t *r_rt_jitter_y;
static cvar_t *r_rt_gbuffer_show;
static cvar_t *r_rt_motion_test;

static rm_gb_hist_t hist;

/* the view being drawn */
static int path, raster, material;
static float proj_u[16];
static float jitter[2];
static rm_viewport_t vp_frame, vp_render;
static int render_w, render_h;

void
RM_GB_Init(void)
{
	r_rt_gbuffer = ri.Cvar_Get("r_rt_gbuffer", "0", 0);
	r_rt_motion = ri.Cvar_Get("r_rt_motion", "0", 0);
	r_rt_gbuffer_scale = ri.Cvar_Get("r_rt_gbuffer_scale", "1", 0);
	r_rt_jitter_x = ri.Cvar_Get("r_rt_jitter_x", "0", 0);
	r_rt_jitter_y = ri.Cvar_Get("r_rt_jitter_y", "0", 0);
	r_rt_gbuffer_show = ri.Cvar_Get("r_rt_gbuffer_show", "0", 0);
	r_rt_motion_test = ri.Cvar_Get("r_rt_motion_test", "0", 0);
	RM_GB_Reset();
}

void
RM_GB_Reset(void)
{
	rm_gb_hist_reset(&hist);
	path = RM_GB_PATH_FORWARD;
	raster = 0;
}

int
RM_GB_Choose(const refdef_t *fd)
{
	if (!r_rt_gbuffer || !r_rt_motion)
	{
		return RM_GB_PATH_FORWARD;
	}

	/* no ray-traced effect exists before RT3: none is on; a MetalFX scaler asks for the motion path (TASK111) */
	return rm_gb_path(0, r_rt_gbuffer->value, RM_FX_WantsMotion() ? 1.0f : r_rt_motion->value,
			(fd->rdflags & RDF_NOWORLDMODEL) != 0);
}

static void
RM_GB_ParseJitter(void)
{
	const float x = r_rt_jitter_x->value, y = r_rt_jitter_y->value;

	jitter[0] = isfinite(x) ? x : 0.0f;
	jitter[1] = isfinite(y) ? y : 0.0f;
}

int
RM_GB_Begin(rm_view_t *view, int stencil, const float *clear_rgba)
{
	int fw = 0, fh = 0, rw, rh, i, n;
	rm_viewport_t keep_vp = view->vp;
	float keep_proj[16], keep_mvp[16];

	path = RM_GB_Choose(&r_newrefdef);
	raster = 0;

	if (path == RM_GB_PATH_FORWARD)
	{
		return 0;
	}

	rmb_target_size(&fw, &fh);

	if (!rm_gb_render_size(fw, fh, r_rt_gbuffer_scale->value, &rw, &rh))
	{
		path = RM_GB_PATH_FORWARD;
		return 0;
	}

	memcpy(keep_proj, view->proj, sizeof(keep_proj));
	memcpy(keep_mvp, view->mvp, sizeof(keep_mvp));

	/* the view at the render size, drawn with the jittered projection; the motion takes the unjittered one */
	vp_frame = view->vp;

	/* a MetalFX scaler (TASK111): the view at its input size, at the target's origin, with its jitter */
	if (!RM_FX_Begin(&vp_frame, r_newrefdef.vieworg, &rw, &rh, &view->vp, jitter))
	{
		rm_gb_render_rect(&vp_frame, fw, fh, rw, rh, &view->vp);
		RM_GB_ParseJitter();
	}

	vp_render = view->vp;
	memcpy(proj_u, view->proj, sizeof(proj_u));
	rm_gb_jitter(proj_u, jitter[0], jitter[1], vp_render.w, vp_render.h, view->proj);
	mtl_mat_mul(view->mvp, view->proj, view->world);

	/* this frame's history, matched to the previous one */
	memset(&hist.cur, 0, sizeof(hist.cur));
	hist.cur.valid = 1;
	memcpy(hist.cur.world, view->world, sizeof(hist.cur.world));
	memcpy(hist.cur.proj, proj_u, sizeof(hist.cur.proj));
	VectorCopy(r_newrefdef.vieworg, hist.cur.vieworg);
	hist.cur.time = r_newrefdef.time;
	hist.cur.vw = vp_render.w;
	hist.cur.vh = vp_render.h;
	n = rm_entity_count(r_newrefdef.num_entities);
	n = (n > RM_GB_MAX_ENTS) ? RM_GB_MAX_ENTS : n;
	hist.cur.nents = n;

	for (i = 0; i < n; i++)
	{
		const entity_t *e = &r_newrefdef.entities[i];
		rm_gb_ent_t *g = &hist.cur.ents[i];

		g->model = e->model;
		VectorCopy(e->origin, g->origin);
		VectorCopy(e->oldorigin, g->oldorigin);
		VectorCopy(e->angles, g->angles);
		g->frame = e->frame;
		g->oldframe = e->oldframe;
		g->flags = e->flags;
		g->backlerp = e->backlerp;
	}

	/*
	 * r_rt_motion_test: the previous view is this one turned by the cvar's
	 * degrees about the camera's vertical axis, every entity its own
	 * previous one, so every pixel's motion is that of a known camera turn
	 */
	if (isfinite(r_rt_motion_test->value) && (r_rt_motion_test->value != 0.0f))
	{
		float r[16];

		hist.prev = hist.cur;
		mtl_mat_identity(r);
		mtl_mat_rotate(r, r_rt_motion_test->value, 0, 1, 0);
		mtl_mat_mul(hist.prev.world, r, hist.cur.world);
		hist.prev.wvalid = 0;
		R_Printf(PRINT_DEVELOPER, "ref_metal: r_rt_motion_test %g: projection %f %f, viewport %gx%g\n",
				r_rt_motion_test->value, proj_u[0], proj_u[5], vp_render.w, vp_render.h);
	}

	rm_gb_hist_match(&hist, RM_GB_MATCH_DIST, RF_WEAPONMODEL);

	/* only the raster reads the world's normals: a MOTION view never makes them */
	rmb_gbuf_set_world_normals((path == RM_GB_PATH_GBUFFER) ? RM_WorldNormals() : 0);

	if (!rmb_gbuf_view_begin(path, rw, rh, stencil, clear_rgba))
	{
		view->vp = keep_vp;
		memcpy(view->proj, keep_proj, sizeof(keep_proj));
		memcpy(view->mvp, keep_mvp, sizeof(keep_mvp));
		memset(&hist.cur, 0, sizeof(hist.cur));
		path = RM_GB_PATH_FORWARD;
		return 0;
	}

	raster = (path == RM_GB_PATH_GBUFFER);
	render_w = rw;
	render_h = rh;
	RM_GB_WorldMotion(MTL_GB_MAT_WORLD);
	return 1;
}

int
RM_GB_Path(void)
{
	return path;
}

float
RM_GB_PixelScale(void)
{
	return (path == RM_GB_PATH_FORWARD) ? 1.0f : rm_gb_pixel_scale(&vp_frame, &vp_render);
}

void
RM_GB_TargetSize(int *w, int *h)
{
	if (path == RM_GB_PATH_FORWARD)
	{
		rmb_target_size(w, h);
		return;
	}

	*w = render_w;
	*h = render_h;
}

int
RM_GB_Raster(void)
{
	return raster;
}

void
RM_GB_Resolve(void)
{
	rm_gb_resolve_t r;
	int obb = rm_overbright(gl1_overbrightbits->value);

	if (!raster)
	{
		return;
	}

	/* the world's lightmap environment of R_FlushChain; r_fullbright draws the texel alone, white light */
	memset(&r, 0, sizeof(r));

	if (r_fullbright->value)
	{
		r.env = MTL_LM_MODULATE;
		r.scale = 1.0f;
	}
	else
	{
		r.env = rm_lm_env(obb, gl_lightmap->value != 0);
		r.scale = obb ? (float)obb : 1.0f;
	}

	raster = 0;
	rmb_gbuf_resolve(&r);
	RM_GB_WorldMotion(MTL_GB_MAT_WORLD);
}

void
RM_GB_End(void)
{
	if (path == RM_GB_PATH_FORWARD)
	{
		return;
	}

	rmb_gbuf_set_show(rm_gb_show(r_rt_gbuffer_show->value));
	rmb_gbuf_view_end(&vp_render, &vp_frame);
	RM_FX_End();
	rm_gb_hist_end(&hist);
	path = RM_GB_PATH_FORWARD;
	raster = 0;
}

/* the previous view's, or this one's without one */
static const float *
RM_GB_PrevWorld(void)
{
	return hist.prev.valid ? hist.prev.world : hist.cur.world;
}

static const float *
RM_GB_PrevProj(void)
{
	return hist.prev.valid ? hist.prev.proj : proj_u;
}

static void
RM_GB_Identity(float *m)
{
	mtl_mat_identity(m);
}

void
RM_GB_WorldMotion(int m)
{
	rm_gb_uniforms_t u;

	if (path == RM_GB_PATH_FORWARD)
	{
		return;
	}

	memset(&u, 0, sizeof(u));
	mtl_mat_mul(u.cur, proj_u, hist.cur.world);
	mtl_mat_mul(u.prev, RM_GB_PrevProj(), RM_GB_PrevWorld());
	RM_GB_Identity(u.nrm);
	u.material = (unsigned)m;
	material = m;
	rmb_gbuf_set_motion(&u);
}

void
RM_GB_SetMaterial(int m)
{
	material = m;

	if (path != RM_GB_PATH_FORWARD)
	{
		rmb_gbuf_set_material((unsigned)m);
	}
}

void
RM_GB_FlushMaterial(int m)
{
	rmb_gbuf_set_material((unsigned)m);
}

int
RM_GB_Material(void)
{
	return material;
}

const rm_gb_ent_t *
RM_GB_Prev(const entity_t *e)
{
	uintptr_t base = (uintptr_t)r_newrefdef.entities, at = (uintptr_t)e;
	long i;

	if (!e || !r_newrefdef.entities || (at < base))
	{
		return NULL;
	}

	i = (long)((at - base) / sizeof(entity_t));

	if (((at - base) % sizeof(entity_t)) || (i >= hist.cur.nents))
	{
		return NULL;
	}

	return rm_gb_hist_prev(&hist, (int)i);
}

void
RM_GB_EntityMotion(const entity_t *e, const float *vm, int brush, const float *wproj)
{
	const rm_gb_ent_t *p = RM_GB_Prev(e);
	const float *o = p ? p->origin : e->origin, *ang = p ? p->angles : e->angles;
	const float *pproj;
	rm_gb_uniforms_t u;
	float a[3], pa[3], pm[16], zero[3] = { 0, 0, 0 }, id[16];

	if (path == RM_GB_PATH_FORWARD)
	{
		return;
	}

	/* the angles as R_DrawAliasModel and R_DrawBrushModel pass them to rm_entity_matrix */
	a[0] = -e->angles[0];
	a[1] = e->angles[1];
	a[2] = brush ? -e->angles[2] : e->angles[2];
	pa[0] = -ang[0];
	pa[1] = ang[1];
	pa[2] = brush ? -ang[2] : ang[2];

	if (wproj)
	{
		pproj = hist.prev.wvalid ? hist.prev.wproj : wproj;
		memcpy(hist.cur.wproj, wproj, sizeof(hist.cur.wproj));
		hist.cur.wvalid = 1;
	}
	else
	{
		pproj = RM_GB_PrevProj();
	}

	memset(&u, 0, sizeof(u));
	mtl_mat_mul(u.cur, wproj ? wproj : proj_u, vm);
	rm_entity_matrix(pm, RM_GB_PrevWorld(), o, pa);
	mtl_mat_mul(u.prev, pproj, pm);
	RM_GB_Identity(id);
	rm_entity_matrix(u.nrm, id, zero, a);
	u.material = brush ? MTL_GB_MAT_WORLD : MTL_GB_MAT_ALIAS;
	material = (int)u.material;
	rmb_gbuf_set_motion(&u);
}

void
RM_GB_SkyMotion(const float *vm, const float *prev_vm)
{
	rm_gb_uniforms_t u;

	if (path == RM_GB_PATH_FORWARD)
	{
		return;
	}

	memset(&u, 0, sizeof(u));
	mtl_mat_mul(u.cur, proj_u, vm);
	mtl_mat_mul(u.prev, RM_GB_PrevProj(), prev_vm ? prev_vm : vm);
	RM_GB_Identity(u.nrm);
	u.material = MTL_GB_MAT_SKY;
	material = MTL_GB_MAT_SKY;
	rmb_gbuf_set_motion(&u);
}

int
RM_GB_PrevView(const float **world, const float **vieworg, float *time)
{
	if (!hist.prev.valid)
	{
		return 0;
	}

	*world = hist.prev.world;
	*vieworg = hist.prev.vieworg;
	*time = hist.prev.time;
	return 1;
}

void
RM_GB_JitterProj(float *proj, float *unjit)
{
	memcpy(unjit, proj, 16 * sizeof(float));

	if (path == RM_GB_PATH_FORWARD)
	{
		return;
	}

	rm_gb_jitter(unjit, jitter[0], jitter[1], vp_render.w, vp_render.h, proj);
}
