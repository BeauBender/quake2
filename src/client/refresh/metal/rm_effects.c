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
 * Sprites, beams and null models, from gl1_main.c (docs/plans/
 * ref-metal-native.md, sections 2 and 4; as built, section 23):
 * R_DrawSpriteModel, R_DrawBeam and R_DrawNullModel. The arithmetic is
 * rm_pure.c's, host-tested against gl1's lines (the sprite's frame,
 * alpha and corners, the beam's strip and colour, the null model's
 * fans); each draw is one region of the frame ring with the alias
 * vertices (rmb_draw_alias), drawn with the state gl1 sets: a sprite
 * GL_MODULATE with alpha 1 alpha-tested, else blended; a beam untextured
 * and blended without depth writes; a null model untextured in R_LightPoint's
 * colour. Each works on a copy of the entity, whose floats are checked
 * first (rm_entity_floats_ok).
 *
 * =======================================================================
 */

#include "header/local.h"
#include "common/mtl_matrix.h"

/* one draw's vertices and indices: the beam is the largest (a strip of RM_BEAM_VERTS) */
static rm_avert_t e_verts[RM_BEAM_VERTS];
static unsigned short e_idx[3 * (RM_BEAM_VERTS - 2)];

/* gl1's GL_CULL_FACE with glCullFace(GL_FRONT) when r_cull is set (R_SetupGL) */
static int
R_EffectCull(const rm_view_t *view)
{
	return view->cull ? RMB_CULL_FRONT : RMB_CULL_NONE;
}

/* from gl1_main.c */
int
R_DrawSpriteModel(const entity_t *currententity, const model_t *currentmodel, const rm_view_t *view, int depth_write)
{
	static const float tex[4][2] = {
		{ 0, 1 },
		{ 0, 0 },
		{ 1, 0 },
		{ 1, 1 }
	};
	const dsprite_t *psprite = (const dsprite_t *)currentmodel->extradata;
	const dsprframe_t *frame;
	const image_t *skin;
	entity_t e = *currententity;
	float point[4][3], alpha;
	rmb_alias_draw_t d;
	int f, i, ni, blend, atest;

	if (!psprite || !rm_entity_floats_ok(e.origin, e.oldorigin, e.angles, e.backlerp, e.alpha))
	{
		return 0;
	}

	/* don't even bother culling, because it's just a single polygon without a surface cache */
	f = rm_sprite_frame(e.frame, psprite->numframes);

	if (f < 0)
	{
		return 0;
	}

	frame = &psprite->frames[f];

	/* normal sprite */
	alpha = rm_sprite_alpha(e.flags, e.alpha, &blend, &atest);

	skin = currentmodel->skins[f];

	if (!skin || (skin->texnum <= 0))
	{
		skin = r_notexture; /* fallback... */
	}

	if (!rm_sprite_quad(e.origin, frame->width, frame->height, frame->origin_x, frame->origin_y, vup, vright, point))
	{
		return 0;
	}

	memset(e_verts, 0, 4 * sizeof(e_verts[0]));

	/* glColor4f(1, 1, 1, alpha), GL_MODULATE */
	for (i = 0; i < 4; i++)
	{
		memcpy(e_verts[i].pos, point[i], sizeof(point[i]));
		e_verts[i].pos[3] = 1;
		e_verts[i].uv[0] = tex[i][0];
		e_verts[i].uv[1] = tex[i][1];
		e_verts[i].color[0] = e_verts[i].color[1] = e_verts[i].color[2] = 1;
		e_verts[i].color[3] = alpha;
	}

	/* glDrawArrays(GL_TRIANGLE_FAN, 0, 4) */
	ni = rm_prim_indices(0, 4, 0, e_idx, (int)(sizeof(e_idx) / sizeof(e_idx[0])));

	memset(&d, 0, sizeof(d));
	d.tex = skin->texnum;
	d.smp = skin->sampler;
	d.blend = blend;
	d.atest = atest;
	d.depth_write = depth_write;
	d.cull = R_EffectCull(view);
	d.zfar = 1;

	return rmb_draw_alias(e_verts, 4, e_idx, (unsigned long)ni, &d, view->mvp, &view->vp);
}

/*
 * from gl1_main.c: a beam whose direction has no length is not drawn and
 * leaves the depth writes as they were; any other ends with gl1's
 * glDepthMask(GL_TRUE) (*drawn set), whether or not its draw is refused
 */
int
R_DrawBeam(const entity_t *currententity, const rm_view_t *view, int *drawn)
{
	entity_t e = *currententity;
	float vtx[RM_BEAM_VERTS][3], rgba[4];
	rmb_alias_draw_t d;
	int i, ni, r;

	*drawn = 0;

	if (!rm_entity_floats_ok(e.origin, e.oldorigin, e.angles, e.backlerp, e.alpha))
	{
		return 0;
	}

	r = rm_beam_points(e.origin, e.oldorigin, e.frame, vtx);

	if (r == 0)
	{
		return 1;
	}

	*drawn = 1;

	if (r < 0)
	{
		return 0;
	}

	rm_beam_color(d_8to24table, gammatable, e.skinnum, e.alpha, rgba);
	memset(e_verts, 0, sizeof(e_verts));

	for (i = 0; i < RM_BEAM_VERTS; i++)
	{
		memcpy(e_verts[i].pos, vtx[i], sizeof(vtx[i]));
		e_verts[i].pos[3] = 1;
		memcpy(e_verts[i].color, rgba, sizeof(rgba));
	}

	/* glDrawArrays(GL_TRIANGLE_STRIP, 0, NUM_BEAM_SEGS * 4) */
	ni = rm_prim_indices(1, RM_BEAM_VERTS, 0, e_idx, (int)(sizeof(e_idx) / sizeof(e_idx[0])));

	/* GL_TEXTURE_2D off, GL_BLEND on, glDepthMask(GL_FALSE) */
	memset(&d, 0, sizeof(d));
	d.blend = 1;
	d.cull = R_EffectCull(view);
	d.zfar = 1;

	return rmb_draw_alias(e_verts, RM_BEAM_VERTS, e_idx, (unsigned long)ni, &d, view->mvp, &view->vp);
}

/* from gl1_main.c */
int
R_DrawNullModel(const entity_t *currententity, const rm_view_t *view, int depth_write)
{
	entity_t e = *currententity;
	vec3_t shadelight;
	float vtx[RM_NULL_VERTS][3], mv[16], mvp[16];
	rmb_alias_draw_t d;
	int i, ni;

	if (!rm_entity_floats_ok(e.origin, e.oldorigin, e.angles, e.backlerp, e.alpha))
	{
		return 0;
	}

	if (e.flags & RF_FULLBRIGHT)
	{
		shadelight[0] = shadelight[1] = shadelight[2] = 1.0F;
	}
	else
	{
		R_LightPoint(&e, e.origin, shadelight);
	}

	/* R_RotateForEntity, as gl1 calls it here (the angles unturned) */
	rm_entity_matrix(mv, view->world, e.origin, e.angles);
	mtl_mat_mul(mvp, view->proj, mv);

	/* GL_TEXTURE_2D off, glColor4f(shadelight[0], shadelight[1], shadelight[2], 1) */
	rm_null_model(vtx);
	memset(e_verts, 0, RM_NULL_VERTS * sizeof(e_verts[0]));

	for (i = 0; i < RM_NULL_VERTS; i++)
	{
		memcpy(e_verts[i].pos, vtx[i], sizeof(vtx[i]));
		e_verts[i].pos[3] = 1;
		e_verts[i].color[0] = shadelight[0];
		e_verts[i].color[1] = shadelight[1];
		e_verts[i].color[2] = shadelight[2];
		e_verts[i].color[3] = 1;
	}

	/* two glDrawArrays(GL_TRIANGLE_FAN, 0, 6), in order, in one draw */
	ni = rm_prim_indices(0, 6, 0, e_idx, (int)(sizeof(e_idx) / sizeof(e_idx[0])));
	ni += rm_prim_indices(0, 6, 6, e_idx + ni, (int)(sizeof(e_idx) / sizeof(e_idx[0])) - ni);

	memset(&d, 0, sizeof(d));
	d.depth_write = depth_write;
	d.cull = R_EffectCull(view);
	d.zfar = 1;

	return rmb_draw_alias(e_verts, RM_NULL_VERTS, e_idx, (unsigned long)ni, &d, mvp, &view->vp);
}
