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
 * vertices go into one region of the frame ring (rmb_draw_alias). No
 * shadows (N8). gl1 writes the frame bounds and the turned pitch into the
 * client's entity; this file works on a copy, which draws the same.
 *
 * =======================================================================
 */

#include "header/local.h"
#include "common/mtl_matrix.h"

/* gl1's s_lerped, and one draw's vertices and indices (the ring region's contents) */
static float s_lerped[RM_MD2_MAX_VERTS][4];
static rm_avert_t s_verts[RM_ALIAS_MAX_VERTS];
static unsigned short s_idx[RM_ALIAS_MAX_INDICES];

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
 * from gl1_mesh.c and gl1_buffer.c (buf_alias): draws the entity, in the
 * pass whose depth writes are depth_write (gl1's glDepthMask). 0 when a
 * model or entity value is refused or the draw is (the caller counts it).
 */
int
R_DrawAliasModel(const entity_t *currententity, const model_t *currentmodel, const rm_view_t *view, int depth_write)
{
	const dmdl_t *paliashdr = (const dmdl_t *)currentmodel->extradata;
	const daliasframe_t *frame, *oldframe;
	entity_t e;
	vec3_t shadelight, point, delta, vectors[3], move, frontv, backv;
	float a[3], mv[16], wproj[16], mvp[16];
	const float *proj = view->proj;
	const image_t *skin;
	rmb_alias_draw_t d;
	long nv, ni;
	int shell, row;

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
		skin = currentmodel->skins[rm_alias_skin(e.skinnum, (const void *const *)currentmodel->skins)];
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

	return rmb_draw_alias(s_verts, (unsigned)nv, s_idx, (unsigned long)ni, &d, mvp, &view->vp);
}
