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
#include "header/rm_gbufscene.h"
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

/*
 * The particles (N9), from gl1_main.c: R_DrawParticles draws gl1's
 * GL_POINTS as the shim does, CPU-made quads (rm_point_quads) with GL's
 * size, snapping and smooth coverage; with gl1_pointparameters 0 at
 * start-up R_DrawParticles2 draws a textured triangle per particle with
 * r_particletexture. Both blend without depth writes.
 */

/* gl1's point state (glPointSize, glPointParameterf, GL_POINT_SMOOTH) and gl_config.pointparameters */
static rm_points_t r_points;
static qboolean r_pointparameters;
/* gl1_particle_max_size as the start-up set it: the cap's base, gl1 never reads the cvar again */
static float r_point_max_base;
static unsigned long particle_skips;

/* the view's particles taken (position and colour), and their quads or triangles */
static rm_avert_t p_in[RM_MAX_PARTICLES];
static rm_avert_t p_verts[4 * RM_MAX_PARTICLES];
static unsigned short p_idx[6 * RM_MAX_PARTICLES];

/* from gl1_main.c, R_Init (the extension is the shim's), and gl1_misc.c, R_SetDefaultState */
void
R_InitParticles(void)
{
	rm_point_state_init(&r_points);
	r_pointparameters = gl1_pointparameters->value != 0;

	if (r_pointparameters)
	{
		rm_point_set_range(&r_points, gl1_particle_min_size->value, gl1_particle_max_size->value);
		r_points.att[0] = gl1_particle_att_a->value;
		r_points.att[1] = gl1_particle_att_b->value;
		r_points.att[2] = gl1_particle_att_c->value;
		r_points.smooth = !gl1_particle_square->value;
		r_point_max_base = r_points.max;
	}
}

/* from gl1_main.c, RI_BeginFrame: gl1_particle_square changed */
void
R_ParticleSquare(void)
{
	if (r_pointparameters)
	{
		r_points.smooth = !gl1_particle_square->value;
	}
	else
	{
		/* particles aren't drawn as GL_POINTS, but as textured triangles */
		R_InitParticleTexture();
	}
}

void
RM_ParticleSkips(rmb_stats_t *s)
{
	s->particle_skips = particle_skips;
}

/* gl1's points: the quads of n points of p_in, untextured, in clip coordinates (drawn with the identity) */
static int
R_DrawPoints(const rm_view_t *view, int n)
{
	rmb_alias_draw_t d;
	rm_points_t pts;
	float identity[16];
	int tw = 0, th = 0;
	long q;

	/* assume the particle size looks good with window height 480px and scale according to real resolution */
	rm_point_set_size(&r_points, gl1_particle_size->value * (float)r_newrefdef.height / 480.0f);

	/* metal_particle_cap 1: the largest size scales by the same factor (0: gl1's, the start-up value) */
	rm_point_set_range(&r_points, r_points.min,
			rm_particle_cap(r_point_max_base, (float)r_newrefdef.height, metal_particle_cap->value == 1.0f ? 1 : 0));

	/*
	 * The sizes above are in frame pixels: a G-buffer or motion view drawn
	 * below the frame's size (r_rt_gbuffer_scale, TASK54) takes them in its
	 * own, so a particle covers as much of the view as at the frame's size
	 * once the view is stretched. Elsewhere the scale is 1 and pts is r_points.
	 */
	pts = r_points;
	rm_gb_point_scale(&pts, RM_GB_PixelScale());

	/* R_SetupGL's glViewport, GL's bottom-left origin in the target the view is drawn into, which the quads are snapped in */
	RM_GB_TargetSize(&tw, &th);
	pts.vx = view->vp.x;
	pts.vy = th - view->vp.y - view->vp.h;
	pts.vw = view->vp.w;
	pts.vh = view->vp.h;

	q = rm_point_quads(&pts, view->world, view->mvp, p_in, n, p_verts, 4 * RM_MAX_PARTICLES,
			p_idx, 6 * RM_MAX_PARTICLES);

	if (q <= 0)
	{
		return q == 0;  /* every point outside the clip volume: nothing to draw */
	}

	memset(&d, 0, sizeof(d));
	d.point = 1;
	d.blend = 1;
	d.cull = RMB_CULL_NONE;  /* GL culls polygons only */
	d.zfar = 1;
	mtl_mat_identity(identity);

	return rmb_draw_alias(p_verts, (unsigned)(4 * q), p_idx, (unsigned long)(6 * q), &d, identity, &view->vp);
}

/* from gl1_main.c: the textured triangles of n particles of p_in (gl1_pointparameters 0) */
static int
R_DrawParticles2(const rm_view_t *view, int n)
{
	float vtx[3][3], tex[3][2];
	rmb_alias_draw_t d;
	int i, j, k = 0;

	if (!r_particletexture)
	{
		particle_skips += n;
		return 0;
	}

	rm_particle_tex(tex);

	/* n is rm_particle_count's, at most the ring: the bound is restated for the arrays */
	for (i = 0; (i < n) && (i < RM_MAX_PARTICLES); i++)
	{
		if (!rm_particle_tri(p_in[i].pos, r_origin, vpn, vup, vright, vtx))
		{
			particle_skips++;
			continue;
		}

		for (j = 0; j < 3; j++)
		{
			rm_avert_t *v = &p_verts[3 * k + j];

			memset(v, 0, sizeof(*v));
			memcpy(v->pos, vtx[j], sizeof(vtx[j]));
			v->pos[3] = 1;
			v->uv[0] = tex[j][0];
			v->uv[1] = tex[j][1];
			memcpy(v->color, p_in[i].color, sizeof(v->color));
			p_idx[3 * k + j] = (unsigned short)(3 * k + j);
		}

		k++;
	}

	if (!k)
	{
		return 1;
	}

	/* R_Bind(r_particletexture->texnum), glDepthMask(GL_FALSE), GL_BLEND, GL_MODULATE */
	memset(&d, 0, sizeof(d));
	d.tex = r_particletexture->texnum;
	d.smp = r_particletexture->sampler;
	d.blend = 1;
	d.cull = R_EffectCull(view);
	d.zfar = 1;

	return rmb_draw_alias(p_verts, (unsigned)(3 * k), p_idx, (unsigned long)(3 * k), &d, view->mvp, &view->vp);
}

/*
 * from gl1_main.c: each particle the client sends is read once, into a
 * copy that is checked (rm_particle_ok) and drawn; one past the ring or
 * with a field out of its bound is counted and not drawn
 */
int
R_DrawParticles(const rm_view_t *view)
{
	const particle_t *in = r_newrefdef.particles;
	long refused;
	int n, i, k = 0;

	n = rm_particle_count(r_newrefdef.num_particles, &refused);
	particle_skips = (unsigned long)refused;

	if (n <= 0) /* avoiding VLA with no size and vertexes built on it */
	{
		return 1;
	}

	if (!in)
	{
		particle_skips += n;
		return 0;
	}

	/* n is 0 to the ring (rm_particle_count): the bound is restated for the arrays */
	for (i = 0; (i < n) && (i < RM_MAX_PARTICLES); i++)
	{
		particle_t p = in[i];

		if (!rm_particle_ok(p.origin, p.color, p.alpha))
		{
			particle_skips++;
			continue;
		}

		memset(&p_in[k], 0, sizeof(p_in[k]));
		memcpy(p_in[k].pos, p.origin, sizeof(p.origin));
		p_in[k].pos[3] = 1;
		rm_particle_color(d_8to24table, gammatable, p.color, p.alpha, p_in[k].color);
		k++;
	}

	if (!k)
	{
		return 1;
	}

	return r_pointparameters ? R_DrawPoints(view, k) : R_DrawParticles2(view, k);
}
