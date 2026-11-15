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
#define MTL_FLOOR floor
#else
#include <math.h>
#define MTL_SQRT sqrtf
#define MTL_FLOOR floorf
#endif

static inline float
mtl_point_coverage(float dx, float dy, float r)
{
	return mtl_clamp01(r + 0.5f - MTL_SQRT(dx * dx + dy * dy));
}

/*
 * ref_metal's lightmapped world (world_lm, N5): gl1's two texture units
 * on its multitexture path as the shim computes them (mgl_texenv_stage
 * of gl1/metal/mgl_texenv.h, the same expressions in the same order, so
 * the host test holds the two equal bit for bit). Unit 0 is GL_REPLACE
 * with the diffuse texel d (an image gl1 uploads as RGB is stored with
 * alpha 255, so its alpha is the previous colour's 1, as the shim's
 * opaque REPLACE keeps it); unit 1 applies the lightmap texel l (an
 * RGBA texture) with env: GL_MODULATE, GL_REPLACE (r_lightmap 1) or
 * GL_COMBINE (gl1_overbrightbits: texel times previous, the RGB scaled
 * by scale, clamped to [0, 1], alpha not scaled).
 */
#define MTL_LM_MODULATE 0
#define MTL_LM_REPLACE 1
#define MTL_LM_COMBINE 2

typedef struct
{
	float r, g, b, a;
} mtl_rgba_t;

static inline mtl_rgba_t
mtl_world_lm(mtl_rgba_t d, mtl_rgba_t l, int env, float scale)
{
	mtl_rgba_t c = d;

	if (env == MTL_LM_REPLACE)
	{
		c.r = l.r;
		c.g = l.g;
		c.b = l.b;
		c.a = l.a;
	}
	else if (env == MTL_LM_COMBINE)
	{
		c.r = mtl_clamp01(d.r * l.r * scale);
		c.g = mtl_clamp01(d.g * l.g * scale);
		c.b = mtl_clamp01(d.b * l.b * scale);
		c.a = d.a * l.a;
	}
	else
	{
		c.r = d.r * l.r;
		c.g = d.g * l.g;
		c.b = d.b * l.b;
		c.a = d.a * l.a;
	}

	return c;
}

/*
 * ref_metal's world_tex with a colour (N6): gl1's GL_MODULATE of the
 * current colour p (glColor4f, saturated by the caller) and the texel t
 * on its single unit, as the shim's mgl_texenv_stage computes it: the
 * warped water (inverse intensity times the texel) and the translucent
 * surfaces (the alpha in the colour's alpha, blended after).
 */
static inline mtl_rgba_t
mtl_world_color(mtl_rgba_t p, mtl_rgba_t t)
{
	mtl_rgba_t c;

	c.r = p.r * t.r;
	c.g = p.g * t.g;
	c.b = p.b * t.b;
	c.a = p.a * t.a;
	return c;
}

/*
 * Bicubic lightmap filtering (TASK120, metal_lightmap_filter 1; P5 of
 * docs/plans/high-resolution.md). A cubic B-spline over the 4 by 4
 * luxels around a sample, built from four bilinear taps as GPU Gems 2,
 * chapter 20 does: in one axis the weights w0 .. w3 of luxels i - 1 .. i + 2
 * pair into g0 = w0 + w1 (the tap between i - 1 and i, at i - 1 + w1 / g0)
 * and g1 = w2 + w3 (between i + 1 and i + 2, at i + 1 + w3 / g1), and the
 * 2D sample is the sum of the four taps, each weighted by the product of
 * its two axes' g. The weights are positive and sum to 1, so the result
 * lies between the smallest and the largest luxel read: no ringing.
 *
 * Every tap position is clamped to half a luxel inside the face's
 * lightmap rectangle, [lo + 1/2, hi - 1/2] in luxels (a luxel's centre is
 * at its index + 1/2), so a bilinear tap there reads luxels of the
 * rectangle only and never the neighbouring face's in the same slice. A
 * rectangle one luxel wide has both bounds at its centre. Coordinates are
 * in luxels (the texture coordinate times the slice's size); the rectangle
 * is (left, top, right, bottom) of the half-open [left, right) by
 * [top, bottom). The shader's function that reads the sample is
 * rm_lm_sample_cubic (rm_shaders.metal), which RT2 (TASK54) calls too.
 */
typedef struct
{
	float w0, w1, w2, w3;
} mtl_bspline_t;

/* the uniform cubic B-spline weights of the luxels i - 1 .. i + 2 for the fraction f of [0, 1) */
static inline mtl_bspline_t
mtl_bspline_weights(float f)
{
	mtl_bspline_t w;
	float f2 = f * f, f3 = f2 * f, m = 1.0f - f;

	w.w0 = m * m * m / 6.0f;
	w.w1 = (3.0f * f3 - 6.0f * f2 + 4.0f) / 6.0f;
	w.w2 = (-3.0f * f3 + 3.0f * f2 + 3.0f * f + 1.0f) / 6.0f;
	w.w3 = f3 / 6.0f;
	return w;
}

/* one axis: the two tap positions (luxels) and their weights, which sum to 1 */
typedef struct
{
	float p0, p1, g0, g1;
} mtl_cubic_axis_t;

static inline float
mtl_clamp_range(float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static inline mtl_cubic_axis_t
mtl_lm_cubic_axis(float x, float lo, float hi)
{
	mtl_cubic_axis_t a;
	float c = x - 0.5f;
	float i = MTL_FLOOR(c);
	mtl_bspline_t w = mtl_bspline_weights(c - i);
	float inner = lo + 0.5f;
	float outer = hi - 0.5f > inner ? hi - 0.5f : inner;

	a.g0 = w.w0 + w.w1;
	a.g1 = w.w2 + w.w3;
	a.p0 = mtl_clamp_range(i - 1.0f + w.w1 / a.g0 + 0.5f, inner, outer);
	a.p1 = mtl_clamp_range(i + 1.0f + w.w3 / a.g1 + 0.5f, inner, outer);
	return a;
}

/* the four taps of a sample at (x, y) luxels in the rectangle (left, top, right, bottom): positions and weights */
typedef struct
{
	float x0, x1, y0, y1;
	float w00, w10, w01, w11;      /* w<ix><iy>: the tap at x<ix>, y<iy> */
} mtl_cubic_taps_t;

static inline mtl_cubic_taps_t
mtl_lm_cubic(float x, float y, float left, float top, float right, float bottom)
{
	mtl_cubic_taps_t t;
	mtl_cubic_axis_t ax = mtl_lm_cubic_axis(x, left, right);
	mtl_cubic_axis_t ay = mtl_lm_cubic_axis(y, top, bottom);

	t.x0 = ax.p0;
	t.x1 = ax.p1;
	t.y0 = ay.p0;
	t.y1 = ay.p1;
	t.w00 = ax.g0 * ay.g0;
	t.w10 = ax.g1 * ay.g0;
	t.w01 = ax.g0 * ay.g1;
	t.w11 = ax.g1 * ay.g1;
	return t;
}

/* 1 when the rectangle holds at least one luxel in each axis (a vertex without a lightmap has the rectangle 0) */
static inline int
mtl_lm_rect_valid(float left, float top, float right, float bottom)
{
	return (right - left >= 1.0f) && (bottom - top >= 1.0f);
}

#endif /* MTL_SHADER_MATH_H */
