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

/*
 * The bit of a triangle's surface index (rm_rts_surface_map) that marks a
 * surface without light samples: gl1 adds no dynamic light to it
 * (R_BuildLightMap takes the store's white straight away), so neither
 * does the pass.
 */
#define MTL_RTS_SURF_UNLIT 0x80000000u

#ifdef __METAL_VERSION__
#define MTL_RTS_FLOOR floor
#define MTL_RTS_SQRT sqrt
#else
#define MTL_RTS_FLOOR floorf
#define MTL_RTS_SQRT sqrtf
#endif

/* a luxel's side in texture units (gl1's 16) */
#define MTL_RTS_LUXEL 16.0f

/*
 * The instance masks of the frame's structure with the shadows on
 * (mtl_rts_inst_mask): an alias instance that carries a light (rm_rts_carried)
 * is MTL_RTS_MASK_CARRIER, every other instance MTL_RTS_MASK_OTHER. A ray
 * of mask 0xff meets every instance.
 */
#define MTL_RTS_MASK_OTHER 0x01u
#define MTL_RTS_MASK_CARRIER 0x02u

/* the mask of an instance that carries the lights carry */
static inline unsigned
mtl_rts_inst_mask(unsigned carry)
{
	return carry ? MTL_RTS_MASK_CARRIER : MTL_RTS_MASK_OTHER;
}

/*
 * The mask of the first query of a shadow ray of light bit lbit
 * (mtl_rts_light_bit): every instance (0xff) when no model carries the
 * light; otherwise every instance but the carriers, whose triangles a
 * second query (mask MTL_RTS_MASK_CARRIER) passes or stops at.
 */
static inline unsigned
mtl_rts_ray_mask(unsigned lbit)
{
	return lbit ? MTL_RTS_MASK_OTHER : 0xffu;
}

/*
 * The bit a shadow ray toward light idx takes into its queries (TASK55):
 * 1 << idx when a model of the frame carries that light (bit idx of
 * carried, the union of the instances' rm_rts_carried), 0 otherwise and
 * for an index past the lights. With 0 the ray is every other light's
 * opaque query; with the bit it passes the triangles of the instances
 * that carry the light (mtl_rts_passes) and stops at any other.
 */
static inline unsigned
mtl_rts_light_bit(unsigned carried, unsigned idx)
{
	return ((idx < MTL_RTS_MAX_LIGHTS) && ((carried >> idx) & 1u)) ? (1u << idx) : 0u;
}

/* 1 when a shadow ray of light bit lbit (mtl_rts_light_bit) passes a triangle of an instance carrying the lights carry */
static inline int
mtl_rts_passes(unsigned carry, unsigned lbit)
{
	return (carry & lbit) != 0;
}

typedef struct
{
	float x, y, z;
} mtl_rts_v3_t;

/* a texture axis of a surface as its texinfo holds it: the axis and the shift (gl1's vecs[i]) */
typedef struct
{
	float x, y, z, w;
} mtl_rts_v4_t;

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

/* gl1's in-plane distance of two truncated offsets: the larger plus half the smaller */
static inline float
mtl_rts_octagon(int sd, int td)
{
	return (float)((sd > td) ? (sd + (td >> 1)) : (td + (sd >> 1)));
}

/* the same along the surface's texture axes s and t (gl1's vecs[0] and vecs[1] without their shift) */
static inline mtl_rts_reach_t
mtl_rts_reach_st(mtl_rts_v3_t p, mtl_rts_v3_t n, mtl_rts_v3_t o, float intensity, mtl_rts_v3_t s, mtl_rts_v3_t t)
{
	mtl_rts_reach_t r;
	mtl_rts_v3_t op = mtl_rts_v3(o.x - p.x, o.y - p.y, o.z - p.z);
	const float d = mtl_rts_dot(op, n);

	r.frad = -1.0f;
	r.fdist = 0.0f;

	if (!(d >= 0.0f))
	{
		return r;
	}

	/* the foot of the light on the plane, less the point: in the plane */
	op = mtl_rts_v3(op.x - n.x * d, op.y - n.y * d, op.z - n.z * d);
	r.frad = intensity - d;
	r.fdist = mtl_rts_octagon(mtl_rts_trunc(mtl_rts_dot(op, s)), mtl_rts_trunc(mtl_rts_dot(op, t)));
	return r;
}

/* with the compiler's base axes at scale 1 (mtl_rts_axis): where the surface's own axes are not known */
static inline mtl_rts_reach_t
mtl_rts_reach(mtl_rts_v3_t p, mtl_rts_v3_t n, mtl_rts_v3_t o, float intensity)
{
	const int axis = mtl_rts_axis(n);
	const mtl_rts_v3_t s = (axis == 0) ? mtl_rts_v3(0, 1, 0) : mtl_rts_v3(1, 0, 0);
	const mtl_rts_v3_t t = (axis == 2) ? mtl_rts_v3(0, 1, 0) : mtl_rts_v3(0, 0, 1);

	return mtl_rts_reach_st(p, n, o, intensity, s, t);
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
 * gl1's add at one luxel (R_AddDynamicLights' inner loop): frad as
 * above, (ui, vi) the light's foot in texture units (vecs . impact +
 * shift) and (ls, lt) the luxel's (a multiple of 16 on each axis: the
 * luxel at s of a surface is at texturemins + 16 s, and texturemins is a
 * multiple of 16).
 */
static inline float
mtl_rts_luxel(float frad, float ui, float vi, float ls, float lt)
{
	float fdist;

	if (!(frad >= MTL_RTS_CUTOFF))
	{
		return 0.0f;
	}

	fdist = mtl_rts_octagon(mtl_rts_trunc(ui - ls), mtl_rts_trunc(vi - lt));
	return (fdist < frad - MTL_RTS_CUTOFF) ? (frad - fdist) : 0.0f;
}

/*
 * The dynamic light the lightmap's bilinear sample holds at point p of a
 * surface with front normal n and texture axes s and t (with their
 * shifts): gl1's add at the four luxels around p, interpolated as the
 * sampler does (the luxel at texture position 16 k is the centre of a
 * texel; p's texture position is s . p + s.w). 0 for a light behind the
 * plane.
 */
static inline float
mtl_rts_add_lm(mtl_rts_v3_t p, mtl_rts_v3_t n, mtl_rts_v3_t o, float intensity, mtl_rts_v4_t s, mtl_rts_v4_t t)
{
	const mtl_rts_v3_t s3 = mtl_rts_v3(s.x, s.y, s.z), t3 = mtl_rts_v3(t.x, t.y, t.z);
	const float d = mtl_rts_dot(mtl_rts_v3(o.x - p.x, o.y - p.y, o.z - p.z), n);
	mtl_rts_v3_t impact;
	float frad, ui, vi, up, vp, s0, t0, fs, ft, a00, a10, a01, a11;

	if (!(d >= 0.0f))
	{
		return 0.0f;
	}

	frad = intensity - d;
	impact = mtl_rts_v3(o.x - n.x * d, o.y - n.y * d, o.z - n.z * d);
	ui = mtl_rts_dot(impact, s3) + s.w;
	vi = mtl_rts_dot(impact, t3) + t.w;
	up = (mtl_rts_dot(p, s3) + s.w) / MTL_RTS_LUXEL;
	vp = (mtl_rts_dot(p, t3) + t.w) / MTL_RTS_LUXEL;
	s0 = MTL_RTS_FLOOR(up);
	t0 = MTL_RTS_FLOOR(vp);
	fs = up - s0;
	ft = vp - t0;
	s0 *= MTL_RTS_LUXEL;
	t0 *= MTL_RTS_LUXEL;
	a00 = mtl_rts_luxel(frad, ui, vi, s0, t0);
	a10 = mtl_rts_luxel(frad, ui, vi, s0 + MTL_RTS_LUXEL, t0);
	a01 = mtl_rts_luxel(frad, ui, vi, s0, t0 + MTL_RTS_LUXEL);
	a11 = mtl_rts_luxel(frad, ui, vi, s0 + MTL_RTS_LUXEL, t0 + MTL_RTS_LUXEL);
	return (a00 * (1.0f - fs) + a10 * fs) * (1.0f - ft) + (a01 * (1.0f - fs) + a11 * fs) * ft;
}

/*
 * A map instance's world-to-object transform as Metal's float4x3 holds it
 * (intersection_result's world_to_object_transform): the columns c0 to c2
 * the rotation, c3 the translation. The world's own instance has the
 * identity; a brush entity's is the inverse of its origin and angles.
 */
typedef struct
{
	mtl_rts_v3_t c0, c1, c2, c3;
} mtl_rts_xform_t;

static inline mtl_rts_xform_t
mtl_rts_xform_identity(void)
{
	mtl_rts_xform_t m;

	m.c0 = mtl_rts_v3(1, 0, 0);
	m.c1 = mtl_rts_v3(0, 1, 0);
	m.c2 = mtl_rts_v3(0, 0, 1);
	m.c3 = mtl_rts_v3(0, 0, 0);
	return m;
}

/* a point through m */
static inline mtl_rts_v3_t
mtl_rts_xpoint(mtl_rts_xform_t m, mtl_rts_v3_t p)
{
	return mtl_rts_v3(m.c0.x * p.x + m.c1.x * p.y + m.c2.x * p.z + m.c3.x,
			m.c0.y * p.x + m.c1.y * p.y + m.c2.y * p.z + m.c3.y,
			m.c0.z * p.x + m.c1.z * p.y + m.c2.z * p.z + m.c3.z);
}

/* a direction through m's rotation (a brush entity's transform is rigid: a normal stays a unit normal) */
static inline mtl_rts_v3_t
mtl_rts_xdir(mtl_rts_xform_t m, mtl_rts_v3_t d)
{
	return mtl_rts_v3(m.c0.x * d.x + m.c1.x * d.y + m.c2.x * d.z,
			m.c0.y * d.x + m.c1.y * d.y + m.c2.y * d.z,
			m.c0.z * d.x + m.c1.z * d.y + m.c2.z * d.z);
}

/*
 * A pixel's surface as gl1 lights it. p and n are in the surface's model
 * space: gl1 marks and lights a brush entity's surfaces with the light's
 * world origin, untransformed, against the model-space planes and texture
 * axes (R_MarkBrushLights, R_AddDynamicLights), so the pass moves the
 * pixel into the model's space instead (mtl_rts_surf) and keeps the
 * light where it is; the world's space is its model's. For a found
 * surface n is its plane's exact normal toward its front and p the
 * pixel's point on that plane, as gl1 measures a light against the plane
 * (the G-buffer's 10-bit normal is off by up to about 1/1000, which tilts
 * the plane by a unit every 1000 units: enough to put a light an eighth
 * of a unit above a floor, where an explosion's light sits, behind it).
 * st: s and t are the surface's own texture axes with their shifts, else
 * the base axes (mtl_rts_reach). unlit: the surface has no light samples
 * (MTL_RTS_SURF_UNLIT), so no light adds there.
 */
typedef struct
{
	mtl_rts_v3_t p, n;
	mtl_rts_v4_t s, t;
	int st;
	int unlit;
} mtl_rts_surf_t;

/* a pixel at world point p with world normal n (the G-buffer's) where its surface is not known: the world's space, the base axes */
static inline mtl_rts_surf_t
mtl_rts_surf_base(mtl_rts_v3_t p, mtl_rts_v3_t n)
{
	mtl_rts_surf_t sf;

	sf.p = p;
	sf.n = n;
	sf.s.x = sf.s.y = sf.s.z = sf.s.w = 0.0f;
	sf.t = sf.s;
	sf.st = 0;
	sf.unlit = 0;
	return sf;
}

/*
 * A pixel at world point p on a found surface: w2o its instance's
 * transform, s and t its texture axes, plane its plane in its model's
 * space facing its front (the unit normal, and in w the distance from the
 * origin: gl1's plane, negated for a SURF_PLANEBACK surface).
 */
static inline mtl_rts_surf_t
mtl_rts_surf(mtl_rts_v3_t p, mtl_rts_xform_t w2o, mtl_rts_v4_t s, mtl_rts_v4_t t, mtl_rts_v4_t plane, int unlit)
{
	mtl_rts_surf_t sf;
	const mtl_rts_v3_t pm = mtl_rts_xpoint(w2o, p);
	const mtl_rts_v3_t n = mtl_rts_v3(plane.x, plane.y, plane.z);
	const float off = mtl_rts_dot(pm, n) - plane.w;

	sf.p = mtl_rts_v3(pm.x - n.x * off, pm.y - n.y * off, pm.z - n.z * off);
	sf.n = n;
	sf.s = s;
	sf.t = t;
	sf.st = 1;
	sf.unlit = unlit ? 1 : 0;
	return sf;
}

/*
 * The world's test before the surface is looked for: a light that may
 * add at a pixel whose plane, from the G-buffer's normal n and the
 * depth's point p, is pd below it (dot(o - p, n)), dist from it. It holds
 * wherever gl1's exact test (pd at least 0, intensity - pd at least
 * DLIGHT_CUTOFF) may, with the normal's error over that distance and the
 * depth's: half a unit plus 1/250 of the distance.
 */
static inline int
mtl_rts_near(float pd, float intensity, float dist)
{
	const float tol = 0.5f + dist * 0.004f;

	return (pd >= -tol) && (intensity - pd >= MTL_RTS_CUTOFF - tol);
}

/*
 * The G-buffer's surface attachment (TASK131; design section 3), an
 * R32Uint the raster writes while the shadows are on, which gives the
 * pass each world pixel's surface without a ray from the eye. A static
 * world draw (the world's and the brush models' faces, from the world's
 * vertex buffer) writes its triangle's first vertex plus one in the low
 * 24 bits and in the top 8 the entity that draws it: 0 the world, i + 1
 * the refdef's entity i (a brush model), MTL_RTS_GB_ENT_NONE a brush
 * model whose entity the raster cannot name. Every other draw writes 0:
 * an alias model, and a dynamic world draw (the warped and flowing
 * batches), whose vertices are not the world's buffer's.
 */
#define MTL_RTS_GB_VMASK 0x00ffffffu
#define MTL_RTS_GB_VMAX 0x00fffffeu     /* the most vertices a map's table holds: the last one plus one fits the 24 bits */
#define MTL_RTS_GB_ENTS 128u            /* the entities an index names: MAX_ENTITIES of ref.h (RM_MAX_ENTITIES) */
#define MTL_RTS_GB_ENT_NONE 0xffu

/* the tag of a static world draw by entity ent (0, i + 1 or MTL_RTS_GB_ENT_NONE); the raster writes tag + vertex */
static inline unsigned
mtl_rts_gb_tag(unsigned ent)
{
	return ((ent & 0xffu) << 24) | 1u;
}

/* what the raster writes for vertex vid of a draw tagged tag (0: a draw that names no vertex) */
static inline unsigned
mtl_rts_gb_value(unsigned tag, unsigned vid)
{
	return (tag != 0u) ? tag + vid : 0u;
}

/* a pixel's value taken apart: ok when it names a vertex of the map's table (nverts) and the world or one of nents entities */
typedef struct
{
	int ok;
	unsigned vid;
	unsigned ent;
} mtl_rts_gb_t;

static inline mtl_rts_gb_t
mtl_rts_gb_decode(unsigned v, unsigned nverts, unsigned nents)
{
	mtl_rts_gb_t g;

	g.vid = (v & MTL_RTS_GB_VMASK) - 1u;
	g.ent = v >> 24;
	g.ok = ((v & MTL_RTS_GB_VMASK) != 0u) && (g.vid < nverts) && (g.ent <= nents) &&
		(nents <= MTL_RTS_GB_ENTS);
	return g;
}

/* a vertex's entry of the map's vertex table (rm_rts_vertex_map) names one of its nsurf surfaces */
static inline int
mtl_rts_gb_surf_ok(unsigned raw, unsigned nsurf)
{
	return (raw & ~MTL_RTS_SURF_UNLIT) < nsurf;
}

/* a brush entity's world-to-object transform from its three rows (rm_rts_ent_rows): x' = r0 . (x, y, z, 1), ... */
static inline mtl_rts_xform_t
mtl_rts_xform_rows(mtl_rts_v4_t r0, mtl_rts_v4_t r1, mtl_rts_v4_t r2)
{
	mtl_rts_xform_t m;

	m.c0 = mtl_rts_v3(r0.x, r1.x, r2.x);
	m.c1 = mtl_rts_v3(r0.y, r1.y, r2.y);
	m.c2 = mtl_rts_v3(r0.z, r1.z, r2.z);
	m.c3 = mtl_rts_v3(r0.w, r1.w, r2.w);
	return m;
}

/*
 * The test before a known surface is lit (TASK131): mtl_rts_near against
 * the surface's own plane (its unit normal and, in w, its distance, in its
 * model's space: rm_rts_plane), with pm the pixel in that space and o the
 * light's world origin, as gl1 measures it (R_AddDynamicLights).
 */
static inline int
mtl_rts_near_plane(mtl_rts_v4_t plane, mtl_rts_v3_t pm, mtl_rts_v3_t o, float intensity)
{
	const mtl_rts_v3_t d = mtl_rts_v3(o.x - pm.x, o.y - pm.y, o.z - pm.z);
	const float pd = o.x * plane.x + o.y * plane.y + o.z * plane.z - plane.w;

	return mtl_rts_near(pd, intensity, MTL_RTS_SQRT(mtl_rts_dot(d, d)));
}

/* one light at a pixel: what it adds there (mtl_rts_add_lm, or mtl_rts_add on the base axes) and whether it is in range */
typedef struct
{
	float add;
	int in_range;
} mtl_rts_lit_t;

static inline mtl_rts_lit_t
mtl_rts_surf_light(mtl_rts_surf_t sf, mtl_rts_v3_t o, float intensity)
{
	mtl_rts_lit_t l;
	mtl_rts_reach_t r;

	l.add = 0.0f;
	l.in_range = 0;

	if (sf.unlit)
	{
		return l;
	}

	if (sf.st)
	{
		r = mtl_rts_reach_st(sf.p, sf.n, o, intensity, mtl_rts_v3(sf.s.x, sf.s.y, sf.s.z), mtl_rts_v3(sf.t.x, sf.t.y, sf.t.z));
		l.add = mtl_rts_add_lm(sf.p, sf.n, o, intensity, sf.s, sf.t);
	}
	else
	{
		r = mtl_rts_reach(sf.p, sf.n, o, intensity);
		l.add = mtl_rts_add(r);
	}

	l.in_range = mtl_rts_in_range(r);
	return l;
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
 * TASK126 slice 4: 1 when the packed indices pidx hold light idx in one of
 * their slots, so that a traced pixel's visibility of it is a traced one
 * (mtl_rts_light_vis says 1 for a light they do not hold, which is no
 * sample of it). MTL_RTS_NO_LIGHT, the empty slots' index, is no light.
 */
static inline int
mtl_rts_holds(unsigned pidx, unsigned idx)
{
	int k;

	if (idx >= MTL_RTS_NO_LIGHT)
	{
		return 0;
	}

	for (k = 0; k < MTL_RTS_CAP; k++)
	{
		if (mtl_rts_idx(pidx, k) == idx)
		{
			return 1;
		}
	}

	return 0;
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
