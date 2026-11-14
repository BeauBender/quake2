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
 * Shader arithmetic shared by the shaders of ref_metal and of the shim
 * and by the C side of both, so the host tests run the formulas the
 * shaders run. Moved verbatim from the shim's mgl_texenv.h by TASK85
 * (docs/plans/ref-metal-native.md, section 8). Written in the common
 * subset of C99 and the Metal shading language: no pointers, no library
 * calls but the square root, structs by value.
 *
 * =======================================================================
 */

#ifndef MTL_SHADER_MATH_H
#define MTL_SHADER_MATH_H

static inline float
mtl_clamp01(float v)
{
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

/*
 * The clip depth the vertex function hands Metal for GL's clip depth z
 * at clip w. GL clips z to [-w, w] and maps z / w from [-1, 1] onto the
 * depth range (n + (f - n) (z / w + 1) / 2, GL 1.4, section 2.11.1);
 * Metal clips to [0, w] and maps z / w from [0, 1] (n + (f - n) z / w).
 * (z + w) / 2 gives both the same window depth, so glDepthRange (the
 * view weapon's 0 .. 0.3) is the Metal viewport's znear and zfar as is.
 */
static inline float
mtl_gl_clip_z(float z, float w)
{
	return (z + w) * 0.5f;
}

/*
 * The coverage of a pixel by a smooth point (GL_POINT_SMOOTH, GL 1.4
 * section 3.3.2) of radius r whose centre lies dx, dy pixels from the
 * pixel's centre: r + 1/2 minus the distance, clamped to [0, 1]. That is
 * what Apple's software renderer computes (TASK52 measured it on points
 * of 1 to 64 pixels: the same 8-bit value on every pixel of a point of
 * size 4 and of 7.3), and it sums to the disc's area for r of 2 and up.
 * The fragment function multiplies the alpha by it.
 */
#ifdef __METAL_VERSION__
#define MTL_SQRT sqrt
#else
#include <math.h>
#define MTL_SQRT sqrtf
#endif

static inline float
mtl_point_coverage(float dx, float dy, float r)
{
	return mtl_clamp01(r + 0.5f - MTL_SQRT(dx * dx + dy * dy));
}

#endif /* MTL_SHADER_MATH_H */
