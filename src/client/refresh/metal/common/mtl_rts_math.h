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
 * The arithmetic of ref_metal's ray-traced dynamic-light shadows (RT3,
 * TASK55; docs/plans/metal-raytracing-design.md, section 2) that the
 * shaders (rm_rtshadow.metal) and the host test
 * (tests/metal/rm_rtshadow_test.c) share: gl1's dynamic-light falloff of
 * R_AddDynamicLights evaluated at a point of a surface, the choice of the
 * four lights a pixel traces, the packing of their visibility, the
 * shadow ray's start off the surface and the light the resolve adds. In
 * the common subset of C99 and the Metal Shading Language, as
 * common/mtl_shader_math.h, which it needs first: no pointers, no library
 * calls, structs by value.
 *
 * =======================================================================
 */

#ifndef MTL_RTS_MATH_H
#define MTL_RTS_MATH_H

/* the shadow rays of a pixel: the design's budget, the nearest lights by their light at the pixel */
#define MTL_RTS_CAP 4

/* the refdef's lights a pixel can take (MAX_DLIGHTS of ref.h; a light's index fits in its 6 bits) */
#define MTL_RTS_MAX_LIGHTS 32

/* a slot without a light in the packed indices */
#define MTL_RTS_NO_LIGHT 63u

/* gl1's DLIGHT_CUTOFF (ref_shared.h): a light adds nowhere on a plane it reaches with less */
#define MTL_RTS_CUTOFF 64.0f

/*
 * How much farther than the light reaches (in the units of the in-plane
 * distance below) a pixel still counts as in its range: two luxels of 16
 * units on each axis, 32 + 32 / 2. A lightmap sample at a point reads
 * luxels at most one luxel away on each axis (bilinear) or two
 * (metal_lightmap_filter's cubic), so a pixel whose sample can hold any
 * of gl1's dynamic light is in range by this margin.
 */
#define MTL_RTS_REACH 48.0f

/* the in-plane offsets gl1 converts to int are kept inside an int's range (gl1's bound is far past any light) */
#define MTL_RTS_MAX_OFFSET 1048576.0f

/* the flags of a pixel's packed indices (mtl_rts_pack_idx) */
#define MTL_RTS_IN_RANGE (1u << 24)     /* a light reaches it (mtl_rts_in_range) */
#define MTL_RTS_SHADOWED (1u << 25)     /* a traced light's visibility is below 1 */

typedef struct
{
	float x, y, z;
} mtl_rts_v3_t;

static inline mtl_rts_v3_t
mtl_rts_v3(float x, float y, float z)
{
	mtl_rts_v3_t v;

	v.x = x;
	v.y = y;
	v.z = z;
	return v;
}

static inline float
mtl_rts_dot(mtl_rts_v3_t a, mtl_rts_v3_t b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline float
mtl_rts_abs(float v)
{
	return (v < 0.0f) ? -v : v;
}

/*
 * The world axis a surface's texture axes leave out, as the map compiler
 * chose them (qbsp's TextureAxisFromPlane: the base axis pair of the
 * axial plane nearest the normal, the floor's first, then the walls
 * facing x, then those facing y): 2 (z) for a floor or a ceiling, whose
 * texture axes are x and y; 0 (x) for a wall facing x (axes y and z); 1
 * (y) for a wall facing y (axes x and z).
 */
static inline int
mtl_rts_axis(mtl_rts_v3_t n)
{
	const float ax = mtl_rts_abs(n.x), ay = mtl_rts_abs(n.y), az = mtl_rts_abs(n.z);

	if ((az >= ax) && (az >= ay))
	{
		return 2;
	}

	return (ax >= ay) ? 0 : 1;
}

/* an in-plane offset as gl1's int conversion takes it: truncated toward 0, its size (|(int)v|) */
static inline int
mtl_rts_trunc(float v)
{
	float a = mtl_rts_abs(v);

	if (!(a <= MTL_RTS_MAX_OFFSET))
	{
		a = MTL_RTS_MAX_OFFSET;
	}

	return (int)a;
}

/*
 * What gl1's R_AddDynamicLights measures for one light and one point of
 * a surface: frad, the light's intensity less its distance to the
 * surface's plane, and fdist, the distance in the plane from the point to
 * the light's foot on it (its "impact"), as gl1 takes it for a luxel: the
 * offsets along the two texture axes truncated to int, then the larger
 * plus half the smaller (integer halving). The point p and the light o in
 * world units, n the surface's unit normal toward its front. A light
 * behind the plane (n . (o - p) below 0) lights nothing there: gl1 marks
 * a surface for a light on its front only (R_MarkSurfaceLights), so frad
 * is -1.
 */
typedef struct
{
	float frad;
	float fdist;
} mtl_rts_reach_t;

static inline mtl_rts_reach_t
mtl_rts_reach(mtl_rts_v3_t p, mtl_rts_v3_t n, mtl_rts_v3_t o, float intensity)
{
	mtl_rts_reach_t r;
	mtl_rts_v3_t op = mtl_rts_v3(o.x - p.x, o.y - p.y, o.z - p.z);
	const float d = mtl_rts_dot(op, n);
	float u, v;
	int sd, td, axis;

	r.frad = -1.0f;
	r.fdist = 0.0f;

	if (!(d >= 0.0f))
	{
		return r;
	}

	/* the foot of the light on the plane, less the point: in the plane */
	op = mtl_rts_v3(op.x - n.x * d, op.y - n.y * d, op.z - n.z * d);
	axis = mtl_rts_axis(n);
	u = (axis == 0) ? op.y : op.x;
	v = (axis == 2) ? op.y : op.z;
	sd = mtl_rts_trunc(u);
	td = mtl_rts_trunc(v);

	r.frad = intensity - d;
	r.fdist = (float)((sd > td) ? (sd + (td >> 1)) : (td + (sd >> 1)));
	return r;
}

/*
 * The light gl1 adds at that point, per unit of the light's colour (the
 * 0 to 255 scale of the lightmap's sums): frad - fdist when frad is at
 * least DLIGHT_CUTOFF and fdist is below frad - DLIGHT_CUTOFF, else 0.
 */
static inline float
mtl_rts_add(mtl_rts_reach_t r)
{
	if (!(r.frad >= MTL_RTS_CUTOFF) || !(r.fdist < r.frad - MTL_RTS_CUTOFF))
	{
		return 0.0f;
	}

	return r.frad - r.fdist;
}

/* the light reaches the point by MTL_RTS_REACH more than it adds: a sample there may read a luxel it lit */
static inline int
mtl_rts_in_range(mtl_rts_reach_t r)
{
	return (r.frad >= MTL_RTS_CUTOFF) && (r.fdist < r.frad - MTL_RTS_CUTOFF + MTL_RTS_REACH);
}

/*
 * The four lights a pixel traces: the largest keys (mtl_rts_add, the
 * light each adds there: "intensity - dist"), largest first, a tie kept
 * in the order the lights came; a key not above 0 is never kept.
 */
typedef struct
{
	float key[MTL_RTS_CAP];
	unsigned idx[MTL_RTS_CAP];
} mtl_rts_top_t;

static inline mtl_rts_top_t
mtl_rts_top_init(void)
{
	mtl_rts_top_t t;
	int k;

	for (k = 0; k < MTL_RTS_CAP; k++)
	{
		t.key[k] = 0.0f;
		t.idx[k] = MTL_RTS_NO_LIGHT;
	}

	return t;
}

static inline mtl_rts_top_t
mtl_rts_top_add(mtl_rts_top_t t, unsigned idx, float key)
{
	int k, at = MTL_RTS_CAP;

	if (!(key > 0.0f))
	{
		return t;
	}

	for (k = MTL_RTS_CAP - 1; k >= 0; k--)
	{
		if ((t.idx[k] == MTL_RTS_NO_LIGHT) || (key > t.key[k]))
		{
			at = k;
		}
	}

	if (at == MTL_RTS_CAP)
	{
		return t;
	}

	for (k = MTL_RTS_CAP - 1; k > at; k--)
	{
		t.key[k] = t.key[k - 1];
		t.idx[k] = t.idx[k - 1];
	}

	t.key[at] = key;
	t.idx[at] = idx;
	return t;
}

/* the four slots' light indices, 6 bits each from bit 0 (MTL_RTS_NO_LIGHT for none), with the flags */
static inline unsigned
mtl_rts_pack_idx(mtl_rts_top_t t, unsigned flags)
{
	unsigned p = flags & (MTL_RTS_IN_RANGE | MTL_RTS_SHADOWED);
	int k;

	for (k = 0; k < MTL_RTS_CAP; k++)
	{
		p |= (t.idx[k] & 63u) << (6 * k);
	}

	return p;
}

/* slot k's light index of packed indices p */
static inline unsigned
mtl_rts_idx(unsigned p, int k)
{
	return (p >> (6 * k)) & 63u;
}

/* a visibility (0 shadowed to 1 lit) as 8 bits, rounded; 0 for a value that is not a number */
static inline unsigned
mtl_rts_vis8(float v)
{
	if (!(v > 0.0f))
	{
		return 0u;
	}

	return (v >= 1.0f) ? 255u : (unsigned)(v * 255.0f + 0.5f);
}

/* the four slots' visibility, 8 bits each from bit 0 (slot 0 lowest) */
static inline unsigned
mtl_rts_pack_vis(float v0, float v1, float v2, float v3)
{
	return mtl_rts_vis8(v0) | (mtl_rts_vis8(v1) << 8) | (mtl_rts_vis8(v2) << 16) | (mtl_rts_vis8(v3) << 24);
}

/* slot k's visibility of packed visibility p, 0 to 1 */
static inline float
mtl_rts_vis(unsigned p, int k)
{
	return (float)((p >> (8 * k)) & 255u) / 255.0f;
}

/*
 * The visibility of light idx at a pixel whose slots are pidx (indices)
 * and pvis (visibility): the slot's when the light was traced, 1 when it
 * was not (a light past the four nearest adds its light unshadowed, so
 * none is lost to the cap).
 */
static inline float
mtl_rts_light_vis(unsigned pidx, unsigned pvis, unsigned idx)
{
	int k;

	for (k = 0; k < MTL_RTS_CAP; k++)
	{
		if (mtl_rts_idx(pidx, k) == idx)
		{
			return mtl_rts_vis(pvis, k);
		}
	}

	return 1.0f;
}

/*
 * How far off the surface, along its normal, a shadow ray starts, for a
 * point dist units from the eye: half a unit, plus k dist^2, k being four
 * steps of a float depth near 1 (2^-24 each) per unit of the near plane's
 * distance (rmb: 4 x 2^-24 / gl_znear). The position comes from the depth
 * buffer, whose step at distance dist is 2^-24 dist^2 / znear, so the
 * start is above the true plane wherever the depth's error is within four
 * steps; half a unit is under the thinnest brush a map holds (one unit)
 * and takes the rest of the error (the normal's 10 bits move nothing
 * here: the start is offset along it, not placed by it).
 */
static inline float
mtl_rts_bias(float dist, float k)
{
	return 0.5f + k * dist * dist;
}

/*
 * The light of a lightmapped pixel with the dynamic lights added: l the
 * static lightmap's sample (0 to 1, as the G-buffer keeps it), t the
 * light the dynamic lights add there (the 0 to 255 scale of gl1's sums,
 * per channel). As R_BuildLightMap's store: where the brightest channel
 * passes 1, all three are scaled down by it, and the alpha is the
 * brightest channel (at most 1). Without a term (all three 0) the light
 * is l, bit for bit.
 */
static inline mtl_rgba_t
mtl_rts_light(mtl_rgba_t l, float tr, float tg, float tb)
{
	mtl_rgba_t c = l;
	float m;

	if (!(tr > 0.0f) && !(tg > 0.0f) && !(tb > 0.0f))
	{
		return l;
	}

	c.r = l.r + ((tr > 0.0f) ? tr : 0.0f) / 255.0f;
	c.g = l.g + ((tg > 0.0f) ? tg : 0.0f) / 255.0f;
	c.b = l.b + ((tb > 0.0f) ? tb : 0.0f) / 255.0f;
	m = (c.r > c.g) ? c.r : c.g;
	m = (c.b > m) ? c.b : m;
	c.a = (m > 1.0f) ? 1.0f : m;

	if (m > 1.0f)
	{
		c.r /= m;
		c.g /= m;
		c.b /= m;
	}

	return c;
}

#endif
