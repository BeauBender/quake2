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
 * The texture environment of one texture unit, shared word for word by
 * shaders.metal and the C side (mgl_internal.h), so the host test runs
 * the arithmetic the fragment function runs. It is written in the
 * common subset of C99 and the Metal shading language: no pointers, no
 * library calls but the square root, structs by value. The clamp, the
 * clip-depth mapping and the coverage of a smooth point (TASK52) moved
 * to src/client/refresh/metal/common/mtl_shader_math.h (TASK85), which
 * ref_metal's shaders include as well; their shim names stay.
 *
 * =======================================================================
 */

#ifndef MGL_TEXENV_H
#define MGL_TEXENV_H

/* What a unit does with its texture (mgl_uniforms_t.texenv0 and texenv1). */
#define MGL_TEXENV_NONE 0       /* the unit is off: the previous colour passes */
#define MGL_TEXENV_REPLACE 1
#define MGL_TEXENV_MODULATE 2
#define MGL_TEXENV_COMBINE 3    /* GL_COMBINE with GL_MODULATE for RGB and alpha, GL_RGB_SCALE */

#include "../../metal/common/mtl_shader_math.h"
#define mgl_clamp01 mtl_clamp01
#define mgl_gl_clip_z mtl_gl_clip_z
#define mgl_point_coverage mtl_point_coverage

typedef struct
{
	float r, g, b, a;
} mgl_rgba_t;

/*
 * GL 1.4, section 3.8.13, tables 3.22 and 3.24: p is the previous
 * colour (the fragment's on unit 0, unit 0's result on unit 1), t the
 * texel. An opaque texture (RGB internal format) has no alpha for
 * GL_REPLACE to take, so the previous alpha stays. GL_COMBINE as gl1
 * sets it (only GL_TEXTURE_ENV_MODE and GL_RGB_SCALE; the sources are
 * GL's defaults, GL_TEXTURE and GL_PREVIOUS, the operands GL_SRC_COLOR
 * and GL_SRC_ALPHA) multiplies texel and previous, scales the RGB by
 * scale (1, 2 or 4) and clamps it to [0, 1]; GL_ALPHA_SCALE stays 1.
 */
static inline mgl_rgba_t
mgl_texenv_stage(int env, int opaque, float scale, mgl_rgba_t p, mgl_rgba_t t)
{
	mgl_rgba_t c = p;

	if (env == MGL_TEXENV_REPLACE)
	{
		c.r = t.r;
		c.g = t.g;
		c.b = t.b;
		c.a = opaque ? p.a : t.a;
	}
	else if (env == MGL_TEXENV_MODULATE)
	{
		c.r = p.r * t.r;
		c.g = p.g * t.g;
		c.b = p.b * t.b;
		c.a = p.a * t.a;
	}
	else if (env == MGL_TEXENV_COMBINE)
	{
		c.r = mgl_clamp01(p.r * t.r * scale);
		c.g = mgl_clamp01(p.g * t.g * scale);
		c.b = mgl_clamp01(p.b * t.b * scale);
		c.a = p.a * t.a;
	}

	return c;
}

#endif /* MGL_TEXENV_H */
