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
 * The per-pixel arithmetic of ref_metal's ray-traced sun and sky shadow
 * (RT4, TASK56), shared by rm_rtsun.metal and the host test
 * (tests/metal/rm_rtsun_test.c): what a sun ray does at a face, the floor
 * gate, the class of a pixel, and the light with the term. Plain C that
 * the Metal shading language compiles too; after mtl_shader_math.h (its
 * mtl_rgba_t) and mtl_rtao_math.h (TASK57's vector and ray start).
 *
 * =======================================================================
 */

#ifndef MTL_RTSUN_MATH_H
#define MTL_RTSUN_MATH_H

#include "mtl_rtao_math.h"

/* what a sun ray does at a face (mtl_rtsun_at) */
#define MTL_RTSUN_AT_LIT 0u       /* the sky: the sun reaches the pixel */
#define MTL_RTSUN_AT_PASS 1u      /* water or a translucent face: the ray goes on */
#define MTL_RTSUN_AT_SHADOW 2u    /* any other face, an alias model among them */

/* the water and translucent faces a ray passes before it counts as shadowed */
#define MTL_RTSUN_LAYERS 4

/* a sun ray's length: past any two points of a map (its coordinates are within +-4096 an axis) */
#define MTL_RTSUN_RANGE 16384.0f

/*
 * What a sun ray does at a face of material id mat (the G-buffer's,
 * rm_rt_materials'; 0xff for an alias model, which has no map triangle):
 * the sky (4) is lit; water (3) and translucent faces (5) let it through;
 * everything else shadows. A ray that meets nothing at all is shadowed
 * too (the kernel): only the sky lets the sun in.
 */
static inline unsigned
mtl_rtsun_at(unsigned mat)
{
	if (mat == 4u)
	{
		return MTL_RTSUN_AT_LIT;
	}

	return ((mat == 3u) || (mat == 5u)) ? MTL_RTSUN_AT_PASS : MTL_RTSUN_AT_SHADOW;
}

/*
 * The floor gate: 1 when the stylized lightmap sample l (the light
 * attachment of a world pixel, 0 to 1 a channel) is below floor_ in its
 * brightest channel, so the term may be added; 0 at or above it (a bright
 * indoor lightmap is not blown out) and for a sample that is not a number.
 */
static inline int
mtl_rtsun_gated(mtl_rgba_t l, float floor_)
{
	float m = l.r;

	if (MTL_RTAO_ISNAN(l.r) || MTL_RTAO_ISNAN(l.g) || MTL_RTAO_ISNAN(l.b))
	{
		return 0;
	}

	m = (l.g > m) ? l.g : m;
	m = (l.b > m) ? l.b : m;
	return (m < floor_) ? 1 : 0;
}

/*
 * max(0, N.L) of the unit normal n and the unit vector l toward the sun,
 * 0 below MTL_RTAO_MIN_COS: a ray is traced only where it leaves the
 * stored plane by at least 1.8 degrees (TASK57's rule), so a pixel never
 * shadows itself; the term left out there is under 1/32 of its peak.
 */
static inline float
mtl_rtsun_ndotl(mtl_rtao_v3_t n, mtl_rtao_v3_t l)
{
	const float d = n.x * l.x + n.y * l.y + n.z * l.z;

	return (d >= MTL_RTAO_MIN_COS) ? d : 0.0f;
}

/* the classes of a pixel (rm_rtsun.metal's output, r_rt_sun_show 2) */
#define MTL_RTSUN_C_NONE 0u       /* not a lightmapped world pixel with a depth and a normal */
#define MTL_RTSUN_C_BRIGHT 1u     /* its lightmap at or above the floor */
#define MTL_RTSUN_C_FACING 2u     /* below the floor, facing away from the sun */
#define MTL_RTSUN_C_SHADOW 3u     /* below the floor, facing it, its ray shadowed */
#define MTL_RTSUN_C_LIT 4u        /* below the floor, facing it, its ray lit: the term added */

/*
 * The light the resolve reads, with the term: for a world pixel (material
 * 1) of class MTL_RTSUN_C_LIT, base (its light: the lightmap sample, or
 * RT5's lightmap x AO) plus sun (sky_color x r_rt_sunscale, a channel)
 * times vis (the 8-bit visibility, 0 to 1) times ndotl, each channel at
 * most 1 (the 16-bit store's); its alpha kept. Every other pixel's light
 * as it was, bit for bit.
 */
static inline mtl_rgba_t
mtl_rtsun_light(mtl_rgba_t base, unsigned material, unsigned cls, float vis, float ndotl, float sr, float sg,
		float sb)
{
	if ((material == 1u) && (cls == MTL_RTSUN_C_LIT))
	{
		const float k = vis * ndotl;
		const float r = base.r + sr * k, g = base.g + sg * k, b = base.b + sb * k;

		base.r = (r < 1.0f) ? r : 1.0f;
		base.g = (g < 1.0f) ? g : 1.0f;
		base.b = (b < 1.0f) ? b : 1.0f;
	}

	return base;
}

#endif
