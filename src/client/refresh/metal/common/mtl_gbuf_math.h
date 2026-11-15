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
 * The arithmetic of ref_metal's G-buffer (RT2, TASK54;
 * docs/plans/metal-raytracing-design.md, section 3) that the shaders
 * (rm_gbuf.metal) and the host test (tests/metal/rm_gbuf_test.c) share:
 * the motion vector of a fragment, the stored normal and the lighting
 * resolve with every ray-traced effect off. Plain C99 and Metal Shading
 * Language alike, as common/mtl_shader_math.h, which it needs first.
 *
 * =======================================================================
 */

#ifndef MTL_GBUF_MATH_H
#define MTL_GBUF_MATH_H

/* the material ids of the G-buffer (design section 3), as rm_rt.h's RM_RT_MAT_* */
#define MTL_GB_MAT_NONE 0
#define MTL_GB_MAT_WORLD 1
#define MTL_GB_MAT_ALIAS 2
#define MTL_GB_MAT_WATER 3
#define MTL_GB_MAT_SKY 4
#define MTL_GB_MAT_TRANS 5

typedef struct
{
	float x, y;
} mtl_mv_t;

/*
 * The motion vector of a point from its clip coordinates in this frame
 * (cx, cy, cw) and in the previous one (px, py, pw), both through the
 * unjittered projections, on a viewport of 2 hw by 2 hh pixels: where the
 * point was minus where it is, in pixels, +x right and +y down (the
 * texture's rows), so MTLFXTemporalScaler takes it with a
 * motionVectorScale of (1, 1). A point on or behind either camera's plane
 * (w not above 0) has none: (0, 0).
 */
static inline mtl_mv_t
mtl_gb_motion(float cx, float cy, float cw, float px, float py, float pw, float hw, float hh)
{
	mtl_mv_t m;

	m.x = 0.0f;
	m.y = 0.0f;

	if ((cw > 0.0f) && (pw > 0.0f))
	{
		m.x = (px / pw - cx / cw) * hw;
		m.y = (cy / cw - py / pw) * hh;
	}

	return m;
}

/* a unit normal as rgb10a2 stores it: each axis from [-1, 1] to [0, 1], alpha 1 (0 with no normal) */
static inline mtl_rgba_t
mtl_gb_normal(float nx, float ny, float nz)
{
	mtl_rgba_t c;
	int none = (nx == 0.0f) && (ny == 0.0f) && (nz == 0.0f);

	c.r = mtl_clamp01(nx * 0.5f + 0.5f);
	c.g = mtl_clamp01(ny * 0.5f + 0.5f);
	c.b = mtl_clamp01(nz * 0.5f + 0.5f);
	c.a = none ? 0.0f : 1.0f;
	return c;
}

/*
 * The lighting resolve with every ray-traced effect at 0 (design section
 * 3, step 5: albedo x (lightmap x AO + dlight x vis + sun x vis) with AO 1
 * and no dlight or sun term): the stock product of the albedo and the
 * light attachment. A world pixel takes gl1's lightmap environment of the
 * frame (mtl_world_lm with env and scale, as rm_fragment_world_lm); an
 * alias model, water and the sky take GL_MODULATE of their light (the
 * vertex colour, the warp's colour, white) and the texel, as
 * rm_fragment_world_color. Any other material is the albedo alone (the
 * resolve does not write those pixels).
 */
static inline mtl_rgba_t
mtl_gb_resolve(mtl_rgba_t albedo, mtl_rgba_t light, unsigned material, int env, float scale)
{
	if (material == MTL_GB_MAT_WORLD)
	{
		return mtl_world_lm(albedo, light, env, scale);
	}

	if ((material == MTL_GB_MAT_ALIAS) || (material == MTL_GB_MAT_WATER) || (material == MTL_GB_MAT_SKY))
	{
		return mtl_world_color(light, albedo);
	}

	return albedo;
}

#endif
