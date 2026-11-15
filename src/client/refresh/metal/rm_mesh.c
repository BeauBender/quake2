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
 * Alias models, from gl1_mesh.c (docs/plans/ref-metal-native.md, sections
 * 2, 4 and 9; as built, section 22): R_DrawAliasModel with gl1's cull,
 * frame bounds, lighting (R_LightPoint at the entity's origin), shade
 * dots, shells, RF_TRANSLUCENT, and the view weapon's projection (r_gunfov,
 * hand) and depth range (RF_DEPTHHACK) of gl1's R_ApplyGLBuffer. The
 * arithmetic is rm_pure.c's, host-tested against gl1's lines: the frame
 * lerp and the per-vertex colour run on the CPU as in gl1, and one draw's
 * vertices go into one region of the frame ring (rmb_draw_alias). The
 * shadows (r_shadows, gl1_stencilshadow: R_DrawAliasShadow, N8) are the
 * same vertices projected onto the plane under the light spot, drawn after
 * the model with gl1's matrix, blend and stencil test. gl1 writes the
 * frame bounds and the turned pitch into the client's entity; this file
 * works on a copy, which draws the same.
 *
 * =======================================================================
 */

#include "header/local.h"
#include "header/rm_gbufscene.h"
#include "common/mtl_matrix.h"

/* gl1's s_lerped, and one draw's vertices and indices (the ring region's contents) */
static float s_lerped[RM_MD2_MAX_VERTS][4];
static rm_avert_t s_verts[RM_ALIAS_MAX_VERTS];
static unsigned short s_idx[RM_ALIAS_MAX_INDICES];

/* the G-buffer's (TASK54): the previous frame's lerped vertices, each drawn vertex's xyz index and its data */
static float s_prev[RM_MD2_MAX_VERTS][4];
static unsigned short s_map[RM_ALIAS_MAX_VERTS];
static rm_gb_avx_t s_avx[RM_ALIAS_MAX_VERTS];
static int s_pass_mode = RM_ALIAS_ALL;

void
R_AliasPassMode(int mode)
{
	s_pass_mode = mode;
}

/* frame f of a model rm_md2_check accepted (f is bounded by rm_alias_frames before) */
static const daliasframe_t *
R_AliasFrame(const dmdl_t *paliashdr, int f)
{
	return (const daliasframe_t *)((const byte *)paliashdr + paliashdr->ofs_frames + (size_t)f * paliashdr->framesize);
}

/* gl1's R_CullAliasModel after its frame bounds (the bounds are the caller's) */
static qboolean
R_CullAliasModel(const dmdl_t *paliashdr, const entity_t *e)
{
	const daliasframe_t *pframe = R_AliasFrame(paliashdr, e->frame), *poldframe = R_AliasFrame(paliashdr, e->oldframe);
	vec3_t angles, vectors[3];
	float normal[4][3], dist[4];
	int f;

	/* rotate the bounding box */
	VectorCopy(e->angles, angles);
	angles[YAW] = -angles[YAW];
	AngleVectors(angles, vectors[0], vectors[1], vectors[2]);

	for (f = 0; f < 4; f++)
	{
		VectorCopy(frustum[f].normal, normal[f]);
		dist[f] = frustum[f].dist;
	}

	return rm_alias_culled(pframe->scale, pframe->translate, poldframe->scale, poldframe->translate,
			pframe == poldframe, e->origin, vectors[0], vectors[1], vectors[2], (const float (*)[3])normal, dist) != 0;
}

/*
 * from gl1_mesh.c, R_DrawAliasShadow and its call in R_DrawAliasModel: the
 * nv vertices the model was drawn with (gl1's s_lerped in the order of
 * the command stream, with the same ni indices: R_SetBufferIndices gives
 * the shadow the model's triangles) projected by rm_shadow_point, in
 * glColor4f(0, 0, 0, 0.5) blended, untextured, with depth writes (the
 * opaque pass) and gl1's stencil test with gl1_stencilshadow, on
 * glTranslatef(origin) and glRotatef(yaw). s_verts is free to reuse: the
 * model's draw copied it into the frame ring.
 */
static int
R_DrawAliasShadow(const entity_t *e, const rm_view_t *view, long nv, long ni)
{
	const float *spot = R_LightSpot();
	float shadevector[3], mv[16], mvp[16], lheight;
	rmb_alias_draw_t d;
	long i;

	lheight = e->origin[2] - spot[2];
	rm_shadevector(e->angles[1], shadevector);

	for (i = 0; i < nv; i++)
	{
		float p[3];

		rm_shadow_point(s_verts[i].pos, shadevector, lheight, p);
		memset(&s_verts[i], 0, sizeof(s_verts[i]));
		memcpy(s_verts[i].pos, p, sizeof(p));
		s_verts[i].pos[3] = 1;
		s_verts[i].color[3] = 0.5f;
	}

	rm_shadow_matrix(mv, view->world, e->origin, e->angles[1]);
	mtl_mat_mul(mvp, view->proj, mv);

	memset(&d, 0, sizeof(d));
	d.blend = 1;
	d.depth_write = 1;
	d.stencil = gl1_stencilshadow->value != 0;
	d.cull = view->cull ? RMB_CULL_FRONT : RMB_CULL_NONE;
	d.zfar = 1;

	return rmb_draw_alias(s_verts, (unsigned)nv, s_idx, (unsigned long)ni, &d, mvp, &view->vp);
}

/*
 * The G-buffer's data of an alias draw (TASK54): the previous frame's
 * position of each of its nv vertices, from the matched entity's frames,
 * lerp, origin and angles lerped as this frame's (s_lerped when it has no
 * match), and each vertex's lerped light normal; then the entity's
 * motion uniforms, vm its modelview now, wproj the view weapon's
 * projection (unjittered) or NULL.
 */
static void
R_AliasMotion(const entity_t *cur, const entity_t *e, const dmdl_t *paliashdr, int shell, long nv,
		const float *vm, const float *wproj)
{
	const rm_gb_ent_t *p = RM_GB_Prev(cur);
	const daliasframe_t *frame = R_AliasFrame(paliashdr, e->frame), *oldframe = R_AliasFrame(paliashdr, e->oldframe);
	long n;

	if (p && rm_entity_floats_ok(p->origin, p->oldorigin, p->angles, p->backlerp, 1.0f))
	{
		const daliasframe_t *pf, *pof;
		vec3_t delta, vectors[3], move, frontv, backv;
		int f = p->frame, of = p->oldframe;
		float backlerp = r_lerpmodels->value ? p->backlerp : 0;

		if (!(p->flags & RF_WEAPONMODEL))
		{
			rm_alias_frames(&f, &of, paliashdr->num_frames, 1);
		}

		rm_alias_frames(&f, &of, paliashdr->num_frames, 0);
		pf = R_AliasFrame(paliashdr, f);
		pof = R_AliasFrame(paliashdr, of);
		VectorSubtract(p->oldorigin, p->origin, delta);
		AngleVectors(p->angles, vectors[0], vectors[1], vectors[2]);
		rm_alias_lerp_setup(pf->scale, pf->translate, pof->scale, pof->translate, backlerp, delta,
				vectors[0], vectors[1], vectors[2], move, frontv, backv);
		rm_alias_lerp(paliashdr->num_xyz, (const byte *)pf->verts, (const byte *)pof->verts, shell, move, frontv,
				backv, s_prev);
	}
	else
	{
		memcpy(s_prev, s_lerped, (size_t)paliashdr->num_xyz * sizeof(s_prev[0]));
	}

	n = rm_gb_alias_map((const int *)((const byte *)paliashdr + paliashdr->ofs_glcmds), paliashdr->num_glcmds,
			paliashdr->num_xyz, s_map, RM_ALIAS_MAX_VERTS);

	if ((n == nv) && rm_gb_alias_extra(s_map, nv, paliashdr->num_xyz, (const float (*)[4])s_prev,
			(const byte *)frame->verts, (const byte *)oldframe->verts, e->backlerp, s_avx))
	{
		rmb_gbuf_set_alias_extra(s_avx, (unsigned)nv);
	}
	else
	{
		rmb_gbuf_set_alias_extra(NULL, 0);
	}

	RM_GB_EntityMotion(cur, vm, 0, wproj);
}

/*
 * from gl1_mesh.c and gl1_buffer.c (buf_alias): draws the entity, in the
 * pass whose depth writes are depth_write (gl1's glDepthMask). 0 when a
 * model or entity value is refused or the draw is (the caller counts it).
 * R_AliasPassMode can leave out the model or the shadow (TASK54).
 */
int
R_DrawAliasModel(const entity_t *currententity, const model_t *currentmodel, const rm_view_t *view, int depth_write)
{
	const dmdl_t *paliashdr = (const dmdl_t *)currentmodel->extradata;
	const daliasframe_t *frame, *oldframe;
	entity_t e;
	vec3_t shadelight, point, delta, vectors[3], move, frontv, backv;
	float a[3], mv[16], wproj[16], wproj_u[16], mvp[16];
	const float *proj = view->proj;
	const image_t *skin;
	rmb_alias_draw_t d;
	long nv, ni;
	int shell, row, ok;

	/* the model is one rm_md2_check accepted (rm_model.c), whose counts the draw takes */
	if (!paliashdr || (currentmodel->md2.verts <= 0) || (paliashdr->num_xyz != currentmodel->md2.num_xyz) ||
		(paliashdr->num_frames <= 0))
	{
		return 0;
	}

	/* the server's floats: an entity with one that is not finite is not drawn */
	if (!rm_entity_floats_ok(currententity->origin, currententity->oldorigin, currententity->angles,
			currententity->backlerp, currententity->alpha))
	{
		return 0;
	}

	e = *currententity;

	if (!(e.flags & RF_WEAPONMODEL))
	{
		rm_alias_frames(&e.frame, &e.oldframe, paliashdr->num_frames, 1);

		if (R_CullAliasModel(paliashdr, &e))
		{
			return 1;
		}
	}

	if (!rm_alias_drawn(e.flags, gl_lefthand->value))
	{
		return 1;
	}

	/* get lighting information */
	VectorClear(point);

	if (rm_alias_lit(e.flags))
	{
		R_LightPoint(&e, e.origin, point);

		/* player lighting hack for communication back to server */
		if (e.flags & RF_WEAPONMODEL)
		{
			r_lightlevel->value = rm_light_level(point);
		}
	}

	rm_alias_shade(e.flags, r_newrefdef.rdflags, point, r_newrefdef.time, gl1_overbrightbits->value, shadelight);
	row = rm_shadedot_row(e.angles[1]);

	/* R_RotateForEntity with the pitch turned, as gl1 calls it */
	a[0] = -e.angles[PITCH];
	a[1] = e.angles[YAW];
	a[2] = e.angles[ROLL];
	rm_entity_matrix(mv, view->world, e.origin, a);

	/* select skin */
	if (e.skin)
	{
		skin = e.skin; /* custom player skin */
	}
	else
	{
		const int si = rm_alias_skin(e.skinnum, (const void *const *)currentmodel->skins);

		skin = ((si >= 0) && (si < MAX_MD2SKINS)) ? currentmodel->skins[si] : NULL;
	}

	if (!skin || (skin->texnum <= 0))
	{
		skin = r_notexture; /* fallback... */
	}

	rm_alias_frames(&e.frame, &e.oldframe, paliashdr->num_frames, 0);

	if (!r_lerpmodels->value)
	{
		e.backlerp = 0;
	}

	/* R_DrawAliasFrameLerp */
	frame = R_AliasFrame(paliashdr, e.frame);
	oldframe = R_AliasFrame(paliashdr, e.oldframe);
	shell = (e.flags & RM_RF_SHELLS) != 0;

	VectorSubtract(e.oldorigin, e.origin, delta);
	AngleVectors(e.angles, vectors[0], vectors[1], vectors[2]);
	rm_alias_lerp_setup(frame->scale, frame->translate, oldframe->scale, oldframe->translate, e.backlerp, delta,
			vectors[0], vectors[1], vectors[2], move, frontv, backv);
	rm_alias_lerp(paliashdr->num_xyz, (const byte *)frame->verts, (const byte *)oldframe->verts, shell, move, frontv,
			backv, s_lerped);

	nv = rm_alias_emit((const int *)((const byte *)paliashdr + paliashdr->ofs_glcmds), paliashdr->num_glcmds,
			(const float (*)[4])s_lerped, (const byte *)frame->verts, paliashdr->num_xyz, row, shadelight,
			(e.flags & RM_RF_SHELLS_RGB) != 0, R_Minlight(), gammatable, rm_alias_alpha(e.flags, e.alpha), s_verts,
			RM_ALIAS_MAX_VERTS, s_idx, RM_ALIAS_MAX_INDICES, &ni);

	if (nv <= 0)
	{
		return 0;
	}

	/* the view weapon's projection (R_ApplyGLBuffer); a frustum glFrustum refuses leaves the identity or mirror */
	if (e.flags & RF_WEAPONMODEL)
	{
		rm_weapon_projection(wproj, r_newrefdef.fov_y, r_gunfov->value, gl_lefthand->value, r_newrefdef.width,
				r_newrefdef.height, gl_znear->value, r_farsee->value != 0, (r_newrefdef.rdflags & RDF_UNDERWATER) != 0,
				r_newrefdef.time, gl1_waterwarp->value);
		RM_GB_JitterProj(wproj, wproj_u);   /* jittered as the view's on a MOTION or GBUFFER view (TASK54) */
		proj = wproj;
	}

	mtl_mat_mul(mvp, proj, mv);

	memset(&d, 0, sizeof(d));

	/* a shell is drawn with GL_TEXTURE_2D off */
	if (!shell)
	{
		d.tex = skin->texnum;
		d.smp = skin->sampler;
	}

	d.blend = (e.flags & RF_TRANSLUCENT) != 0;
	d.depth_write = depth_write;
	d.cull = rm_alias_cull_mode(view->cull, e.flags, gl_lefthand->value);
	rm_alias_depth(e.flags, &d.znear, &d.zfar);

	ok = 1;

	if (s_pass_mode != RM_ALIAS_SHADOW)
	{
		if (RM_GB_Path() != RM_GB_PATH_FORWARD)
		{
			R_AliasMotion(currententity, &e, paliashdr, shell, nv, mv, (e.flags & RF_WEAPONMODEL) ? wproj_u : NULL);
		}

		ok = rmb_draw_alias(s_verts, (unsigned)nv, s_idx, (unsigned long)ni, &d, mvp, &view->vp);
		RM_GB_WorldMotion(MTL_GB_MAT_WORLD);
	}

	/* gl1 draws the shadow after the model, never for a translucent one or the view weapon */
	if ((s_pass_mode != RM_ALIAS_MODEL) && rm_shadow_drawn(e.flags, gl_shadows->value) &&
		!R_DrawAliasShadow(&e, view, nv, ni))
	{
		ok = 0;
	}

	return ok;
}
