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
 * The per-pixel rules of RT-primary's light on alias models (TASK191,
 * PM9 of docs/plans/presentation-modes.md, section 4.1), shared by the
 * kernel a later slice adds and the host test
 * (tests/metal/rm_rtmodel_test.c): gl1's model colour rule with a traced
 * light in place of the shade light and the shadedot, the tag that
 * carries a model's flags to the kernel through the G-buffer's light
 * attachment, the view weapon's depth and the pull-back point. Plain C99
 * that the Metal shading language compiles too; after mtl_shader_math.h
 * (MTL_SQRT).
 *
 * =======================================================================
 */

#ifndef MTL_RTM_MATH_H
#define MTL_RTM_MATH_H

/* the flags a tag carries (RF_GLOW, RF_MINLIGHT, RF_WEAPONMODEL) */
#define MTL_RTM_GLOW 1u
#define MTL_RTM_MINLIGHT 2u
#define MTL_RTM_WEAPON 4u
#define MTL_RTM_FLAGS 7u

/*
 * gl1's constants of rm_alias_shade, in double precision on the host as
 * gl1 compares and multiplies them (a float against 0.1, shadelight x
 * 0.8), so the host's results are gl1's to the bit; Metal has no double.
 */
#ifdef __METAL_VERSION__
#define MTL_RTM_FLOOR 0.1f
#define MTL_RTM_GLOW_KEEP 0.8f
#else
#define MTL_RTM_FLOOR 0.1
#define MTL_RTM_GLOW_KEEP 0.8
#endif

/*
 * The shadedot factor of the traced rule: 1, since the traced light
 * already carries each light's N.L at the pixel's own normal, which
 * replaces gl1's quantized shadedot (r_avertexnormal_dots).
 */
#define MTL_RTM_DOT_TRACED 1.0f

typedef struct
{
	float x, y, z;
} mtl_rtm_v3_t;

typedef struct
{
	int r, g, b;
} mtl_rtm_idx_t;

static inline mtl_rtm_v3_t
mtl_rtm_v3(float x, float y, float z)
{
	mtl_rtm_v3_t v;

	v.x = x;
	v.y = y;
	v.z = z;
	return v;
}

/* RF_MINLIGHT: when no channel is above 0.1, every channel is 0.1 */
static inline mtl_rtm_v3_t
mtl_rtm_floor(mtl_rtm_v3_t s)
{
	if (!(s.x > MTL_RTM_FLOOR) && !(s.y > MTL_RTM_FLOOR) && !(s.z > MTL_RTM_FLOOR))
	{
		s.x = (float)MTL_RTM_FLOOR;
		s.y = (float)MTL_RTM_FLOOR;
		s.z = (float)MTL_RTM_FLOOR;
	}

	return s;
}

/* RF_GLOW on one channel: the pulse (0.1 sin(7 time), mtl_rtm_glow's scale) added, floored at 0.8 of the light */
static inline float
mtl_rtm_glow1(float s, float scale)
{
	const float keep = (float)(s * MTL_RTM_GLOW_KEEP);
	const float g = s + scale;

	return (g < keep) ? keep : g;
}

static inline mtl_rtm_v3_t
mtl_rtm_glow(mtl_rtm_v3_t s, float scale)
{
	s.x = mtl_rtm_glow1(s.x, scale);
	s.y = mtl_rtm_glow1(s.y, scale);
	s.z = mtl_rtm_glow1(s.z, scale);
	return s;
}

/*
 * The shade light of a model of tag flags f (MTL_RTM_*) from light l, in
 * rm_alias_shade's order: the minlight floor, the glow pulse and its
 * floor, then overbright (gl1_overbrightbits, 0 for none). The light
 * substitutions before them (shells, RF_FULLBRIGHT, the IR goggles) are
 * not here: those models stay stock-lit (rm_rtm_stock_light).
 */
static inline mtl_rtm_v3_t
mtl_rtm_shade(mtl_rtm_v3_t l, unsigned f, float glow, float overbright)
{
	if (f & MTL_RTM_MINLIGHT)
	{
		l = mtl_rtm_floor(l);
	}

	if (f & MTL_RTM_GLOW)
	{
		l = mtl_rtm_glow(l, glow);
	}

	if (overbright != 0.0f)
	{
		l.x *= overbright;
		l.y *= overbright;
		l.z *= overbright;
	}

	return l;
}

/*
 * gl1's colour index of one channel (R_DrawAliasFrameLerp): the shadedot
 * factor dot times the shade light times 255, truncated and clamped to 0
 * .. 255 (a NaN is 0). The caller then takes the minlight table and the
 * gamma table (gl1_minlight, vid_gamma) at the index: neither is a closed
 * form the GPU can repeat to the bit (the gamma table is a pow), so both
 * are inputs.
 */
static inline int
mtl_rtm_index1(float dot, float s)
{
	const float f = dot * s * 255.0f;

	return !(f > 0.0f) ? 0 : ((f >= 255.0f) ? 255 : (int)f);
}

/*
 * The colour indexes of a model pixel of tag flags f with light l and
 * shadedot factor dot: MTL_RTM_DOT_TRACED for a traced light, gl1's
 * shadedot for gl1's own light (the host test's parity).
 */
static inline mtl_rtm_idx_t
mtl_rtm_color(mtl_rtm_v3_t l, float dot, unsigned f, float glow, float overbright)
{
	const mtl_rtm_v3_t s = mtl_rtm_shade(l, f, glow, overbright);
	mtl_rtm_idx_t i;

	i.r = mtl_rtm_index1(dot, s.x);
	i.g = mtl_rtm_index1(dot, s.y);
	i.b = mtl_rtm_index1(dot, s.z);
	return i;
}

/*
 * The tag: a vertex colour the CPU writes in place of gl1's colour on a
 * traced model, which the G-buffer raster stores in its light attachment
 * (saturate(in.color), RGBA8Unorm). Its rgb is a mid grey, so a frame
 * whose kernel did not run shows grey models, not black or white ones;
 * its alpha is MTL_RTM_TAG_BASE + the flags, in units of 1/255. A
 * rastered model is opaque (rm_rtm_entity_split sends a translucent one
 * forward), so its own colour's alpha is 1 (255) and never a tag; the
 * rgb is checked too. Steps of 1/255 survive an 8-bit unorm store and a
 * 16-bit one (k/255 is k x 257/65535 exactly) and a saturate.
 */
#define MTL_RTM_TAG_GREY 128
#define MTL_RTM_TAG_BASE 160

static inline mtl_rgba_t
mtl_rtm_tag(unsigned f)
{
	mtl_rgba_t c;

	c.r = (float)MTL_RTM_TAG_GREY / 255.0f;
	c.g = c.r;
	c.b = c.r;
	c.a = (float)(MTL_RTM_TAG_BASE + (int)(f & MTL_RTM_FLAGS)) / 255.0f;
	return c;
}

/* a stored channel (0 to 1) in units of 1/255, rounded; -1 outside 0 .. 1 or a NaN */
static inline int
mtl_rtm_byte(float v)
{
	return ((v >= 0.0f) && (v <= 1.0f)) ? (int)(v * 255.0f + 0.5f) : -1;
}

/* 1 when stored colour c is a tag */
static inline int
mtl_rtm_is_tag(mtl_rgba_t c)
{
	const int a = mtl_rtm_byte(c.a) - MTL_RTM_TAG_BASE;

	return (mtl_rtm_byte(c.r) == MTL_RTM_TAG_GREY) && (mtl_rtm_byte(c.g) == MTL_RTM_TAG_GREY) &&
		(mtl_rtm_byte(c.b) == MTL_RTM_TAG_GREY) && (a >= 0) && (a <= (int)MTL_RTM_FLAGS);
}

/* the flags of a tag (mtl_rtm_is_tag); 0 for any other colour */
static inline unsigned
mtl_rtm_tag_flags(mtl_rgba_t c)
{
	if (!mtl_rtm_is_tag(c))
	{
		return 0u;
	}

	return (unsigned)(mtl_rtm_byte(c.a) - MTL_RTM_TAG_BASE) & MTL_RTM_FLAGS;
}

/*
 * The view weapon's depth: gl1 draws it at the depth range 0 .. 0.3
 * (rm_alias_depth's RF_DEPTHHACK), so a stored depth d is 0.3 of the
 * clip depth (mtl_gl_clip_z's 0 .. 1) of its own projection.
 */
#define MTL_RTM_WEAPON_ZFAR 0.3f

/* the clip depth (0 .. 1) of a weapon pixel of stored depth d, the depth rm_rts_unproject takes */
static inline float
mtl_rtm_weapon_depth(float d)
{
	return d / MTL_RTM_WEAPON_ZFAR;
}

/* the same as GL's normalized device z, -1 .. 1 */
static inline float
mtl_rtm_weapon_ndc(float d)
{
	return mtl_rtm_weapon_depth(d) * 2.0f - 1.0f;
}

/*
 * The pull-back point: a weapon pixel at p that the eye ray from eye
 * finds a face in front of (hit distance t below |p - eye|, the weapon
 * inside a wall) is shaded MTL_RTM_PULLBACK units before that face, on
 * the eye's side; at the eye when the face is nearer than that. A t that
 * is not below the distance (no face in between, a NaN, an infinity) or
 * is below 0 keeps p.
 */
#define MTL_RTM_PULLBACK 0.25f

static inline mtl_rtm_v3_t
mtl_rtm_pullback(mtl_rtm_v3_t eye, mtl_rtm_v3_t p, float t)
{
	const mtl_rtm_v3_t d = mtl_rtm_v3(p.x - eye.x, p.y - eye.y, p.z - eye.z);
	const float len = MTL_SQRT(d.x * d.x + d.y * d.y + d.z * d.z);
	float k;

	if (!(t >= 0.0f) || !(t < len))
	{
		return p;
	}

	k = (t > MTL_RTM_PULLBACK) ? (t - MTL_RTM_PULLBACK) / len : 0.0f;
	return mtl_rtm_v3(eye.x + d.x * k, eye.y + d.y * k, eye.z + d.z * k);
}

#endif
