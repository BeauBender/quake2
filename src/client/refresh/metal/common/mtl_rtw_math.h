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
 * The instance mask bit of what a reflection ray (and the eye ray that
 * looks for translucent water) tests: rmb_rt_frame sets it on every
 * instance but the view weapon and the viewer's own model
 * (rm_rt_inst_t.noao), the same set as the AO rays' bit, but a bit of its
 * own beside TASK55's two and TASK57's MTL_RTAO_MASK, so each effect's
 * rays can be given their own set. Every instance carries a shadow bit, so
 * the shadow rays' queries (0xff, 0x01, 0x02) and the AO rays' (0x04) meet
 * what they met before.
 */
#define MTL_RTW_MASK 0x08u

/* an instance's mask with the reflections' bit: mask its shadows' and AO's, norefl 1 for what reflection rays pass */
static inline unsigned
mtl_rtw_inst_mask(unsigned mask, unsigned norefl)
{
	return norefl ? mask : (mask | MTL_RTW_MASK);
}

/*
 * TASK158: the bit of the map's instances among those reflection rays
 * test (the world and the inline brush models: doors, plats), which alias
 * instances do not carry. A ray of r_rt_water_hits 1 that has passed
 * MTL_RTW_LAYERS hidden model faces is traced on with this mask alone, so
 * it passes every alias model and meets what it would meet without them
 * (mtl_rtw_ws_next).
 */
#define MTL_RTW_MASK_MAP 0x10u

/* mtl_rtw_inst_mask with the map's bit: alias 1 for an alias model's instance, which never carries it */
static inline unsigned
mtl_rtw_inst_mask_ws(unsigned mask, unsigned norefl, unsigned alias)
{
	return (norefl || alias) ? mtl_rtw_inst_mask(mask, norefl) : (mtl_rtw_inst_mask(mask, norefl) | MTL_RTW_MASK_MAP);
}

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

/*
 * TASK158: with MTL_RTW_LIT or MTL_RTW_SKY, the colour was shaded in world
 * space, not read from the picture: the hit face's texture and lightmap
 * (and the dynamic lights the lightmaps left out), or the sky box's texel
 * in the ray's direction (mtl_rtw_choose_ws).
 */
#define MTL_RTW_WS 0x10u

/* a pixel's class as the kernel writes it (the reflection's alpha, 8 bits): 0 not water */
#define MTL_RTW_CLASS_WATER 0x4u   /* a water surface was found at the pixel */
#define MTL_RTW_CLASS_TRANS 0x8u   /* in front of the raster's surface (translucent water, drawn by the forward pass) */
#define MTL_RTW_CLASS_FROM 0x3u    /* the choice, MTL_RTW_MISS, _LIT or _SKY */
#define MTL_RTW_CLASS_WS MTL_RTW_WS   /* the choice was shaded in world space */

/*
 * TASK158: the class byte its separate candidates' raster wrote where a
 * water triangle lay in front of the depth, for the compaction to list.
 * TASK162 marks those pixels in the G-buffer instead (mtl_rtw_marked), so
 * no pass writes it now; it stays the reference the host test compares
 * the marks with. It has no water bit, so a reader that met it would take
 * the pixel as one without water.
 */
#define MTL_RTW_CLASS_CAND 0x80u

/* TASK158: the threads of a threadgroup of the trace over the list (one pixel each) */
#define MTL_RTW_LIST_GROUP 64u

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
#define MTL_RTW_LOG2 log2
#else
#include <math.h>
#define MTL_RTW_FLOOR floorf
#define MTL_RTW_TRUNC truncf
#define MTL_RTW_LOG2 log2f
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

/*
 * TASK158: the choice with world-space shading behind the picture. The
 * picture's colour wherever mtl_rtw_choose finds it (it is exact and
 * cheaper: the resolve's lightmap, AO and shadowed dynamic lights); else,
 * for a face (hit 2) that face_ok says can be shaded (a map triangle with
 * a texture: not an alias model), the face in world space; for the sky
 * brush or nothing met (hit 0 or 1) with sky_ok (the sky box's six
 * images are bound), the sky box's texel in the ray's direction; else
 * none. The world-space choices carry MTL_RTW_WS.
 */
static inline unsigned
mtl_rtw_choose_ws(int hit, int on_screen, unsigned mat, float off, float dist, int face_ok, int sky_ok)
{
	const unsigned s = mtl_rtw_choose(hit, on_screen, mat, off, dist);

	if (s != MTL_RTW_MISS)
	{
		return s;
	}

	if (hit == 2)
	{
		return face_ok ? (MTL_RTW_LIT | MTL_RTW_WS) : MTL_RTW_MISS;
	}

	return sky_ok ? (MTL_RTW_SKY | MTL_RTW_WS) : MTL_RTW_MISS;
}

/* what the reflection ray of r_rt_water_hits 1 does after a hit's choice (mtl_rtw_ws_next) */
#define MTL_RTW_NEXT_STOP 0   /* the choice stands */
#define MTL_RTW_NEXT_PASS 1   /* pass the model: intersect again just past it, with the same mask */
#define MTL_RTW_NEXT_MAP 2    /* pass every model: intersect again just past it with MTL_RTW_MASK_MAP alone */

/*
 * TASK158: the reflection ray's step after the choice from (mtl_rtw_choose_ws)
 * at its layer'th hit (0 the first) of kind hit (as mtl_rtw_choose's), alias
 * 1 when the face is an alias model's, map 1 once the ray is traced with
 * MTL_RTW_MASK_MAP. A model the picture does not show (off the screen or
 * hidden) is passed: the ray goes on past it, so the reflection shows what
 * lies behind the model and never the model's shape, as a hole or
 * otherwise. Every other choice stands. A closed model has a front and a
 * back face (the rays cull none), so after MTL_RTW_LAYERS model faces the
 * ray is traced on with the map's instances alone, and ends at what it
 * would meet without the models. That is the bound: past MTL_RTW_LAYERS
 * hidden model faces a model the picture shows is passed too, and the
 * reflection there shows what lies behind it (at 0, none). The step works
 * per face: a face of a model the view does not show at that pixel is
 * passed, so a shown model's unseen far side is passed too. Hidden brush models (doors, plats) are
 * map faces and are shaded. r_rt_water_hits 0 takes the same step for the
 * alias models and passes hidden brush models too (mtl_rtw_ss_next, FIX81
 * and FIX92).
 */
static inline int
mtl_rtw_ws_next(int hit, int alias, unsigned from, int layer, int map)
{
	if ((hit != 2) || !alias || (from != MTL_RTW_MISS) || map)
	{
		return MTL_RTW_NEXT_STOP;
	}

	return (layer + 1 < MTL_RTW_LAYERS) ? MTL_RTW_NEXT_PASS : MTL_RTW_NEXT_MAP;
}

/*
 * FIX92: 1 when a face whose instance's first word is b (rmb_rt_frame's
 * per-instance words: its first triangle of the world's index buffer, ~0
 * for an alias model) is an inline brush model's (a door, a plat, a lift):
 * not an alias model's and not the world's, whose first word is world
 * (rmb_rt_bind_geometry's fourth word, ~0 when the world has no
 * structure). A Metal intersection gives the instance, not its mask, so
 * this word, not a mask bit, tells the two apart.
 */
static inline int
mtl_rtw_brush(unsigned b, unsigned world)
{
	return (b != 0xffffffffu) && (b != world);
}

/*
 * FIX81: the reflection ray's step at r_rt_water_hits 0 (rm_rtw_trace),
 * after TASK58's choice from (mtl_rtw_choose) at its layer'th hit: the
 * rule of r_rt_water_hits 1 (mtl_rtw_ws_next) for the alias models, so
 * both settings pass over exactly the same ones. Before it every face
 * stopped the ray, so an alias model the picture does not show (behind a
 * wall, in the PVS) ended it with no reflection, and the water showed the
 * model's outline as a hole in front of a wall the picture shows. Now the
 * ray goes on behind such a model and ends where it would end without it:
 * after MTL_RTW_LAYERS of their faces with the map's instances alone
 * (MTL_RTW_MASK_MAP), so in at most MTL_RTW_LAYERS + 1 intersections. Past
 * that bound a model the picture shows behind them is passed too (no
 * reflection at 0, see mtl_rtw_ws_next). A ray whose first hit is anything
 * else is cast once, as before, and a world face the picture does not
 * show still gives none.
 *
 * FIX92: brush 1 when the face is an inline brush model's (mtl_rtw_brush).
 * At 0 a brush model the picture does not show (a door, a plat, a lift) is
 * passed as an alias model is, and counts towards the same MTL_RTW_LAYERS
 * faces, so the bound stays MTL_RTW_LAYERS + 1 intersections; at 1 it is
 * shaded where it is instead (mtl_rtw_ws_next is unchanged). The last
 * cast's mask, the map's, still holds the brush models: past the bound one
 * the picture shows gives its reflection, a hidden one none.
 */
static inline int
mtl_rtw_ss_next(int hit, int alias, int brush, unsigned from, int layer, int map)
{
	return mtl_rtw_ws_next(hit, alias || brush, from, layer, map);
}

/*
 * TASK158: a map triangle's shading record, one 32-bit word a triangle of
 * the world's index buffer (rm_rtw_shade_build): the index of its texture
 * in the frame's table (12 bits, MTL_RTW_SHADE_NOTEX for none), its
 * lightmap slice (8 bits, MTL_RTW_SHADE_NOLM for none) and its flags.
 */
#define MTL_RTW_SHADE_NOTEX 0xfffu
#define MTL_RTW_SHADE_NOLM 0xffu

/* the frame's texture table: the world's images first (at most MAX_GLTEXTURES), then the sky box's six faces */
#define MTL_RTW_TEX_WORLD 1024u
#define MTL_RTW_TEX_SKY MTL_RTW_TEX_WORLD
#define MTL_RTW_TEX_COUNT (MTL_RTW_TEX_WORLD + 6u)

#define MTL_RTW_SF_WARP 0x01u    /* SURF_WARP: gl1 draws it with the warp's colour, no lightmap */
#define MTL_RTW_SF_FLOW 0x02u    /* SURF_FLOWING: its texture scrolls (rm_flow_scroll) */
#define MTL_RTW_SF_TRANS 0x04u   /* SURF_TRANS33 or 66: drawn with the inverse intensity, no lightmap */
#define MTL_RTW_SF_SKY 0x08u     /* SURF_SKY: never shaded as a face (the sky brush) */

static inline unsigned
mtl_rtw_shade_pack(unsigned tex, unsigned slice, unsigned flags)
{
	return (tex & 0xfffu) | ((slice & 0xffu) << 12) | ((flags & 0xffu) << 20);
}

static inline unsigned
mtl_rtw_shade_tex(unsigned s)
{
	return s & 0xfffu;
}

static inline unsigned
mtl_rtw_shade_slice(unsigned s)
{
	return (s >> 12) & 0xffu;
}

static inline unsigned
mtl_rtw_shade_flags(unsigned s)
{
	return (s >> 20) & 0xffu;
}

/* 1 when a face of record s can be shaded in world space: it has a texture and is not the sky brush */
static inline int
mtl_rtw_shade_ok(unsigned s)
{
	return (mtl_rtw_shade_tex(s) < MTL_RTW_TEX_WORLD) && !(mtl_rtw_shade_flags(s) & MTL_RTW_SF_SKY);
}

/* an attribute at a triangle's hit: a at its first vertex, b and c at the others, (u, v) the hit's barycentrics */
static inline float
mtl_rtw_bary(float a, float b, float c, float u, float v)
{
	return a + (b - a) * u + (c - a) * v;
}

/*
 * The mip level a world-space hit samples its texture at, dist units
 * along the eye's and the reflection's paths: the footprint of a pixel
 * there, dist x k (k = 2 / (the view's height in pixels x proj[5]), the
 * side of a pixel one unit away), in texels of a texture at scale 1 (one
 * texel a unit), as log2; 0 at a footprint of a texel or less or a value
 * not finite, at most MTL_RTW_LOD_MAX.
 */
#define MTL_RTW_LOD_MAX 12.0f

static inline float
mtl_rtw_lod(float dist, float k)
{
	const float f = dist * k;

	if (!(f > 1.0f))
	{
		return 0.0f;
	}

	return (f >= 4096.0f) ? MTL_RTW_LOD_MAX : MTL_RTW_LOG2(f);
}

/*
 * The colour of a face shaded in world space, as the stock draw shades it
 * (mtl_gb_resolve and the forward pass's): the texel alone with
 * r_fullbright; a warped face, the texel times the warp's colour; a
 * translucent one, times the inverse intensity's; any other, the texel
 * times the light (its lightmap sample, raised by the dynamic lights the
 * lightmaps left out; white for a face without one) with gl1's lightmap
 * unit (env, scale). The alpha is the texel's.
 */
static inline mtl_rgba_t
mtl_rtw_face_color(mtl_rgba_t albedo, mtl_rgba_t light, unsigned flags, int env, float scale, mtl_rgba_t warp,
		mtl_rgba_t trans, int fullbright)
{
	if (fullbright)
	{
		return albedo;
	}

	if (flags & MTL_RTW_SF_WARP)
	{
		return mtl_world_color(warp, albedo);
	}

	if (flags & MTL_RTW_SF_TRANS)
	{
		return mtl_world_color(trans, albedo);
	}

	return mtl_world_lm(albedo, light, env, scale);
}

/*
 * The sky box's face and texture coordinates in direction d (in the box's
 * own frame: the world direction turned back by the sky's rotation), as
 * gl1 draws the box (R_DrawSkyBox, MakeSkyVec): the face of d's largest
 * axis (gl1's 0 to 5 of DrawSkyPolygon, the first on a tie of x with y or
 * z, y with z), s and t from vec_to_st, mapped from -1..1 to 0..1 and
 * held inside sky_min..sky_max (1/512 to 511/512), t turned over. 0 for a
 * direction of no length.
 */
typedef struct
{
	int ok;
	int face;
	float s, t;
} mtl_rtw_sky_t;

static inline float
mtl_rtw_sky_comp(mtl_rtw_v3_t d, int j)
{
	const float v = (j == 1 || j == -1) ? d.x : (j == 2 || j == -2) ? d.y : d.z;

	return (j < 0) ? -v : v;
}

static inline mtl_rtw_sky_t
mtl_rtw_sky_st(mtl_rtw_v3_t d)
{
	/* gl1's vec_to_st */
	const int st[6][3] = { { -2, 3, 1 }, { 2, 3, -1 }, { 1, 3, 2 }, { -1, 3, -2 }, { -2, -1, 3 }, { -2, 1, -3 } };
	const float ax = (d.x < 0.0f) ? -d.x : d.x, ay = (d.y < 0.0f) ? -d.y : d.y, az = (d.z < 0.0f) ? -d.z : d.z;
	mtl_rtw_sky_t out;
	float dv, s, t;
	int axis;

	out.ok = 0;
	out.face = 0;
	out.s = out.t = 0.5f;

	if ((ax > ay) && (ax > az))
	{
		axis = (d.x < 0.0f) ? 1 : 0;
	}
	else if ((ay > az) && (ay > ax))
	{
		axis = (d.y < 0.0f) ? 3 : 2;
	}
	else
	{
		axis = (d.z < 0.0f) ? 5 : 4;
	}

	dv = mtl_rtw_sky_comp(d, st[axis][2]);

	if (!(dv > 0.0f))
	{
		return out;
	}

	s = mtl_rtw_sky_comp(d, st[axis][0]) / dv;
	t = mtl_rtw_sky_comp(d, st[axis][1]) / dv;
	s = (s + 1.0f) * 0.5f;
	t = (t + 1.0f) * 0.5f;
	s = (s < 1.0f / 512.0f) ? 1.0f / 512.0f : (s > 511.0f / 512.0f) ? 511.0f / 512.0f : s;
	t = (t < 1.0f / 512.0f) ? 1.0f / 512.0f : (t > 511.0f / 512.0f) ? 511.0f / 512.0f : t;
	out.ok = 1;
	out.face = axis;
	out.s = s;
	out.t = 1.0f - t;
	return out;
}

/* a direction in the sky box's frame: d turned by the transpose of the rotation whose rows are r0, r1, r2 */
static inline mtl_rtw_v3_t
mtl_rtw_sky_dir(mtl_rtw_v3_t d, mtl_rtw_v3_t r0, mtl_rtw_v3_t r1, mtl_rtw_v3_t r2)
{
	return mtl_rtw_v3(r0.x * d.x + r1.x * d.y + r2.x * d.z, r0.y * d.x + r1.y * d.y + r2.y * d.z,
			r0.z * d.x + r1.z * d.y + r2.z * d.z);
}

/* the threadgroups of the trace over n listed pixels, MTL_RTW_LIST_GROUP a group */
static inline unsigned
mtl_rtw_groups(unsigned n)
{
	return n / MTL_RTW_LIST_GROUP + ((n % MTL_RTW_LIST_GROUP) ? 1u : 0u);
}

/* a listed pixel, x in the low 16 bits and y in the high (a render size is at most 16384) */
static inline unsigned
mtl_rtw_list_pack(unsigned x, unsigned y)
{
	return (x & 0xffffu) | ((y & 0xffffu) << 16);
}

/*
 * 1 when the compaction lists a pixel: opaque water the raster drew
 * (material 3), or the water's marks (TASK162, mtl_rtw_marked; TASK158's
 * candidates' raster before) hold it or a pixel next to it
 * (any of the nine, so the eye ray of TASK58 is cast wherever it could
 * meet water: the raster and the ray agree on a pixel's centre but for
 * ties on an edge and the float error)
 */
static inline int
mtl_rtw_listed(unsigned mat, unsigned marks)
{
	return mtl_rtw_is_water(mat) || (marks != 0u);
}

/*
 * TASK162: the water's mark in the G-buffer's normal attachment
 * (RGB10A2Unorm), in place of TASK158's separate raster. The raster
 * writes the two alpha bits 1 where the pixel has a normal and 0 where it
 * has none, and every reader tests a > 0.5. At r_rt_water_hits 1 its last
 * draw, the map's water triangles in front of its depth
 * (rm_fragment_rtw_mark), blends that alpha to MTL_RTW_MARK_A +
 * MTL_RTW_MARK_A x a (mtl_rtw_mark_blend): 1 to 2/3, 0 to 1/3, and a
 * second layer keeps the level (4/9 stores as 1/3, 5/9 as 2/3). A pixel
 * is marked at levels 1 and 2 (mtl_rtw_marked); it has a normal above 0.5
 * at all four, as before. The pixels it marks are the ones the separate
 * raster marked MTL_RTW_CLASS_CAND.
 */
#define MTL_RTW_MARK_A (1.0f / 3.0f)

/* the mark's blend over a stored alpha a: the source alpha MTL_RTW_MARK_A, the destination's factor the source's alpha */
static inline float
mtl_rtw_mark_blend(float a)
{
	return MTL_RTW_MARK_A + MTL_RTW_MARK_A * a;
}

/* the two-bit level the normal attachment stores for alpha a, to the nearest (0 for NaN) */
static inline unsigned
mtl_rtw_alpha_level(float a)
{
	if (!(a > 0.0f))
	{
		return 0u;
	}

	return (unsigned)(mtl_clamp01(a) * 3.0f + 0.5f);
}

/* 1 when the normal attachment's alpha a, as a read gives it (its level over 3), carries the water's mark */
static inline int
mtl_rtw_marked(float a)
{
	const unsigned l = mtl_rtw_alpha_level(a);

	return (l == 1u) || (l == 2u);
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
 * and where its reflection came from (with MTL_RTW_WS, TASK158).
 */
static inline unsigned
mtl_rtw_class(int water, int trans, unsigned from)
{
	if (!water)
	{
		return 0u;
	}

	return MTL_RTW_CLASS_WATER | (trans ? MTL_RTW_CLASS_TRANS : 0u) | (from & (MTL_RTW_CLASS_FROM | MTL_RTW_CLASS_WS));
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
