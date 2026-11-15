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
 * The per-pixel arithmetic of ref_metal's ray-traced water reflections
 * (RT6, TASK58; docs/plans/metal-raytracing-design.md, section 2) that the
 * kernel (rm_rtwater.metal) and the host test (tests/metal/rm_rtwater_test.c)
 * share: which pixels are water (the material mask), what an eye ray does
 * at a face on its way to the raster's surface, the stock warp's phase as
 * a tilt of the flat normal, the reflected direction, where a hit's colour
 * is taken from (the lit pixel it is seen at, the sky pixel in its
 * direction, or none) and the blend over the water's colour. Plain C99
 * and Metal Shading Language alike, as common/mtl_shader_math.h; it needs
 * mtl_shader_math.h (mtl_rgba_t, MTL_SQRT), mtl_gbuf_math.h (the material
 * ids) and mtl_rtao_math.h (the instance mask bit) first.
 *
 * =======================================================================
 */

#ifndef MTL_RTW_MATH_H
#define MTL_RTW_MATH_H

#ifndef MTL_RTAO_MASK
#error "mtl_rtw_math.h needs mtl_rtao_math.h first"
#endif

/*
 * The instances a reflection ray (and the eye ray that looks for
 * translucent water) tests: every one but the view weapon and the
 * viewer's own model (rm_rt_inst_t.noao), the same set as an AO ray's, so
 * the bit is shared and no instance's mask changes with the reflections.
 */
#define MTL_RTW_MASK MTL_RTAO_MASK

/* the map's material id of an alias model's triangle (an instance with no map triangle) */
#define MTL_RTW_MAT_ALIAS 0xffu

/* how far a ray looks when nothing bounds it (the longest a map holds, as RM_RTAO_RANGE_MAX) */
#define MTL_RTW_FAR 8192.0f

/* the faces an eye ray passes (translucent faces, alias models) before it gives up on water */
#define MTL_RTW_LAYERS 4

/*
 * The smallest cosine of a reflected ray's angle to the flat plane's
 * normal: 1/32, at least 1.8 degrees above the water (as MTL_RTAO_MIN_COS),
 * so from a start above the plane the ray never meets it.
 */
#define MTL_RTW_MIN_COS 0.03125f

/* the stock turbulence table's size and gl1's TURBSCALE, 256 / (2 pi) */
#define MTL_RTW_TURB_SIZE 256
#define MTL_RTW_TURBSCALE 40.7436654f

/* where a reflection's colour comes from (the kernel's choice, mtl_rtw_choose) */
#define MTL_RTW_MISS 0u     /* nowhere on the screen: no reflection, the stock water */
#define MTL_RTW_LIT 1u      /* the lit pixel the hit is seen at */
#define MTL_RTW_SKY 2u      /* the sky pixel in the ray's direction */

/* a pixel's class as the kernel writes it (the reflection's alpha, 8 bits): 0 not water */
#define MTL_RTW_CLASS_WATER 0x4u   /* a water surface was found at the pixel */
#define MTL_RTW_CLASS_TRANS 0x8u   /* in front of the raster's surface (translucent water, drawn by the forward pass) */
#define MTL_RTW_CLASS_FROM 0x3u    /* the choice, MTL_RTW_MISS, _LIT or _SKY */

/* what an eye ray does at a face of material mat (mtl_rtw_eye_step) */
#define MTL_RTW_EYE_WATER 0
#define MTL_RTW_EYE_PASS 1
#define MTL_RTW_EYE_STOP 2

typedef struct
{
	float x, y, z;
} mtl_rtw_v3_t;

typedef struct
{
	mtl_rtw_v3_t s, t;
} mtl_rtw_axes_t;

#ifdef __METAL_VERSION__
#define MTL_RTW_FLOOR floor
#define MTL_RTW_TRUNC trunc
#else
#include <math.h>
#define MTL_RTW_FLOOR floorf
#define MTL_RTW_TRUNC truncf
#endif

static inline mtl_rtw_v3_t
mtl_rtw_v3(float x, float y, float z)
{
	mtl_rtw_v3_t v;

	v.x = x;
	v.y = y;
	v.z = z;
	return v;
}

static inline float
mtl_rtw_dot(mtl_rtw_v3_t a, mtl_rtw_v3_t b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline mtl_rtw_v3_t
mtl_rtw_mad(mtl_rtw_v3_t a, float s, mtl_rtw_v3_t b)
{
	return mtl_rtw_v3(a.x + s * b.x, a.y + s * b.y, a.z + s * b.z);
}

/* v at unit length; v itself when it has none */
static inline mtl_rtw_v3_t
mtl_rtw_unit(mtl_rtw_v3_t v)
{
	const float l = MTL_SQRT(mtl_rtw_dot(v, v));

	return (l > 0.0f) ? mtl_rtw_v3(v.x / l, v.y / l, v.z / l) : v;
}

/*
 * The material mask: 1 when a G-buffer pixel of material mat is water,
 * the opaque warped water the raster draws (material 3). Translucent water
 * is drawn by the forward pass and is found by an eye ray instead.
 */
static inline int
mtl_rtw_is_water(unsigned mat)
{
	return mat == MTL_GB_MAT_WATER;
}

/*
 * What an eye ray does at a face of the map's material mat (rm_rt_materials,
 * MTL_RTW_MAT_ALIAS for an alias model) on its way to the pixel's raster
 * surface: water (a warped face, translucent or not) is the pixel's water;
 * a translucent face and an alias model (a translucent entity: the opaque
 * ones are at the raster's depth, beyond the ray) are drawn over the lit
 * image later, so the ray passes them; the world's faces and the sky brush
 * stop it, as no water is seen behind them.
 */
static inline int
mtl_rtw_eye_step(unsigned mat)
{
	if (mat == MTL_GB_MAT_WATER)
	{
		return MTL_RTW_EYE_WATER;
	}

	if ((mat == MTL_GB_MAT_TRANS) || (mat == MTL_RTW_MAT_ALIAS))
	{
		return MTL_RTW_EYE_PASS;
	}

	return MTL_RTW_EYE_STOP;
}

/*
 * How far an eye ray looks for translucent water in front of a pixel whose
 * raster surface is dist units from the eye: short of it by TASK55's
 * tolerance (2 units, or 0.2 % of the distance), so the surface itself is
 * not met; MTL_RTW_FAR where the raster drew nothing or the sky (dist not
 * above 0). 0 or less: no ray.
 */
static inline float
mtl_rtw_eye_max(float dist)
{
	float tol;

	if (!(dist > 0.0f))
	{
		return MTL_RTW_FAR;
	}

	tol = (dist * 0.002f > 2.0f) ? dist * 0.002f : 2.0f;
	return dist - tol;
}

/*
 * gl1's index into the turbulence table at texture coordinate c and time,
 * (int)((c * 0.125 + time) * TURBSCALE) & 255 (rm_warp_index), in floats:
 * truncated toward 0, then taken modulo 256 as the mask of a two's
 * complement int takes it. 0 for a value that is not finite.
 */
static inline int
mtl_rtw_warp_index(float c, float time)
{
	const float x = (c * 0.125f + time) * MTL_RTW_TURBSCALE;
	float t;

	if (!(x - x == 0.0f))
	{
		return 0;
	}

	t = MTL_RTW_TRUNC(x);
	t = t - 256.0f * MTL_RTW_FLOOR(t / 256.0f);
	return ((int)t) & (MTL_RTW_TURB_SIZE - 1);
}

/*
 * Quake's texture axes of a plane of unit normal n (qbsp's
 * TextureAxisFromPlane): those of the axial face whose normal is nearest
 * n, the first of floor, ceiling, west, east, south, north on a tie.
 * Water's texture coordinates are these axes' dot products with the point
 * for a texture at scale 1 (its shift only moves the phase).
 */
static inline mtl_rtw_axes_t
mtl_rtw_axes(mtl_rtw_v3_t n)
{
	const float d[6] = { n.z, -n.z, n.x, -n.x, n.y, -n.y };
	mtl_rtw_axes_t a;
	float best = 0.0f;
	int i, k = 0;

	for (i = 0; i < 6; i++)
	{
		if (d[i] > best)
		{
			best = d[i];
			k = i;
		}
	}

	if (k < 2)
	{
		a.s = mtl_rtw_v3(1.0f, 0.0f, 0.0f);
		a.t = mtl_rtw_v3(0.0f, -1.0f, 0.0f);
	}
	else if (k < 4)
	{
		a.s = mtl_rtw_v3(0.0f, 1.0f, 0.0f);
		a.t = mtl_rtw_v3(0.0f, 0.0f, -1.0f);
	}
	else
	{
		a.s = mtl_rtw_v3(1.0f, 0.0f, 0.0f);
		a.t = mtl_rtw_v3(0.0f, 0.0f, -1.0f);
	}

	return a;
}

/*
 * The perturbed normal (design section 2): the flat unit normal n tilted
 * by amp along the plane's texture axes, ps along s and pt along t, ps and
 * pt the stock warp's offsets at the point divided by the table's largest
 * (rm_rtw_turb_table: -1 to 1), so the water's geometry stays flat
 * (TASK53) and the reflection moves with the warp. The axes are projected
 * into the plane first, so the tilt never moves the normal along itself.
 */
static inline mtl_rtw_v3_t
mtl_rtw_normal(mtl_rtw_v3_t n, mtl_rtw_axes_t a, float ps, float pt, float amp)
{
	const mtl_rtw_v3_t s = mtl_rtw_unit(mtl_rtw_mad(a.s, -mtl_rtw_dot(a.s, n), n));
	const mtl_rtw_v3_t t = mtl_rtw_unit(mtl_rtw_mad(a.t, -mtl_rtw_dot(a.t, n), n));

	if (!(amp > 0.0f))
	{
		return n;
	}

	return mtl_rtw_unit(mtl_rtw_mad(mtl_rtw_mad(n, amp * ps, s), amp * pt, t));
}

/*
 * The reflected direction of the unit view direction d (eye to the water)
 * about the perturbed unit normal np, kept at least MTL_RTW_MIN_COS above
 * the flat plane of unit normal n, which faces the eye (d . n < 0): a
 * direction below that is moved up to it in its own vertical plane, at
 * unit length. With np = n and d not grazing, the mirror direction.
 */
static inline mtl_rtw_v3_t
mtl_rtw_reflect(mtl_rtw_v3_t d, mtl_rtw_v3_t np, mtl_rtw_v3_t n)
{
	const mtl_rtw_v3_t r = mtl_rtw_mad(d, -2.0f * mtl_rtw_dot(d, np), np);
	const float e = mtl_rtw_dot(r, n);
	mtl_rtw_v3_t h;
	float hl;

	if (e >= MTL_RTW_MIN_COS)
	{
		return mtl_rtw_unit(r);
	}

	h = mtl_rtw_mad(r, -e, n);
	hl = MTL_SQRT(mtl_rtw_dot(h, h));

	if (!(hl > 0.0f))
	{
		return n;
	}

	return mtl_rtw_mad(mtl_rtw_v3(h.x * (MTL_SQRT(1.0f - MTL_RTW_MIN_COS * MTL_RTW_MIN_COS) / hl),
			h.y * (MTL_SQRT(1.0f - MTL_RTW_MIN_COS * MTL_RTW_MIN_COS) / hl),
			h.z * (MTL_SQRT(1.0f - MTL_RTW_MIN_COS * MTL_RTW_MIN_COS) / hl)), MTL_RTW_MIN_COS, n);
}

/* how near (units, along the eye's ray) the lit pixel's point must be to a hit dist units from the eye to show it */
static inline float
mtl_rtw_seen_tol(float dist)
{
	return (dist * 0.01f > 4.0f) ? dist * 0.01f : 4.0f;
}

/*
 * The sky lookup's choice: where the colour of a reflection ray comes
 * from. hit 0 the ray met nothing, 1 it met the sky brush, 2 another face
 * (a solid one, a translucent one or water: opaque to a reflection ray,
 * design section 1). on_screen: the point it is looked up at (the hit, or
 * for 0 and 1 the ray's direction from the eye, the sky box being around
 * the eye) projects inside the view, at a pixel of G-buffer material mat
 * whose point is off by off units from the hit along the eye's ray (below
 * 0 when that pixel has no depth). The sky's texel where the pixel shows
 * the sky; the lit pixel where it shows the hit (within mtl_rtw_seen_tol
 * of dist, the hit's distance from the eye); nothing otherwise.
 */
static inline unsigned
mtl_rtw_choose(int hit, int on_screen, unsigned mat, float off, float dist)
{
	if (!on_screen)
	{
		return MTL_RTW_MISS;
	}

	if (hit != 2)
	{
		return (mat == MTL_GB_MAT_SKY) ? MTL_RTW_SKY : MTL_RTW_MISS;
	}

	if ((mat == MTL_GB_MAT_NONE) || (mat == MTL_GB_MAT_SKY) || !(off >= 0.0f))
	{
		return MTL_RTW_MISS;
	}

	return (off <= mtl_rtw_seen_tol(dist)) ? MTL_RTW_LIT : MTL_RTW_MISS;
}

/* r_rt_water_reflect as the blend takes it: 0 to 1, 0 when not finite */
static inline float
mtl_rtw_weight(float k)
{
	if (!(k > 0.0f))
	{
		return 0.0f;
	}

	return (k > 1.0f) ? 1.0f : k;
}

/*
 * The water's colour with its reflection (design section 2): the stock
 * colour c's RGB mixed toward the reflection's (rr, rg, rb) by the weight
 * k, its alpha (SURF_TRANS's 0.33 or 0.66, or 1) kept; with no reflection
 * (from MTL_RTW_MISS) or a weight of 0, c bit for bit.
 */
static inline mtl_rgba_t
mtl_rtw_blend(mtl_rgba_t c, float rr, float rg, float rb, float k, unsigned from)
{
	const float w = mtl_rtw_weight(k);

	if ((from == MTL_RTW_MISS) || (w == 0.0f))
	{
		return c;
	}

	c.r = c.r + (rr - c.r) * w;
	c.g = c.g + (rg - c.g) * w;
	c.b = c.b + (rb - c.b) * w;
	return c;
}

/*
 * The class byte of a pixel: 0 when no water was found; else water, the
 * translucent bit when the water lies in front of the raster's surface,
 * and where its reflection came from.
 */
static inline unsigned
mtl_rtw_class(int water, int trans, unsigned from)
{
	if (!water)
	{
		return 0u;
	}

	return MTL_RTW_CLASS_WATER | (trans ? MTL_RTW_CLASS_TRANS : 0u) | (from & MTL_RTW_CLASS_FROM);
}

/*
 * 1 when a translucent face's fragment, dist units from the eye, is the
 * water the kernel found at its pixel (class cls, wdist units away):
 * within TASK55's tolerance (2 units, or 0.2 %), so a pane in front of the
 * water or another face behind it keeps its stock colour.
 */
static inline int
mtl_rtw_is_trans_water(unsigned cls, float wdist, float dist)
{
	const float tol = (wdist * 0.002f > 2.0f) ? wdist * 0.002f : 2.0f;
	const float dd = dist - wdist;

	if (!(cls & MTL_RTW_CLASS_WATER) || !(cls & MTL_RTW_CLASS_TRANS) || !(wdist > 0.0f))
	{
		return 0;
	}

	return (dd <= tol) && (dd >= -tol);
}

#endif
