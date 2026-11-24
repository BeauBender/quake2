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
 * The per-pixel arithmetic of the RT-primary presentation's direct light
 * (TASK188, PM6 of docs/plans/presentation-modes.md, section 4.1), shared
 * by rm_rtdir.metal and the host test (tests/metal/rm_rtdir_test.c): a
 * map light's contribution at a surface point (qrad3's linear falloff and
 * spotlight cone), what a shadow ray does at a face, the quantized
 * samples of the exposure fit and the light the resolve reads. Plain C
 * that the Metal shading language compiles too; after
 * mtl_shader_math.h (its mtl_rgba_t).
 *
 * The light a world pixel gets here never reads the lightmap at that
 * pixel (the plan's section 6, item 5): mtl_rtdir_light takes no
 * lightmap argument. The lightmap enters only the exposure fit's frame
 * sums (mtl_rtdir_q), never a pixel's own light; the kernel's text is
 * held to that by tests/metal/rm_rtdir_test.c (kernel_broken).
 *
 * =======================================================================
 */

#ifndef MTL_RTDIR_MATH_H
#define MTL_RTDIR_MATH_H

/* qrad3's: a surface is lit by a light only where N.L is above 0.001 (GatherSampleLight) */
#define MTL_RTDIR_MIN_DOT 0.001f

/* a point light's cone: every direction is inside it (the spot test always passes) */
#define MTL_RTDIR_NOSPOT (-2.0f)

/*
 * TASK188 slice 2: an emissive face (SURF_LIGHT), qrad3's emit_surface,
 * is marked by this in place of the cone's cosine; its direction is then
 * the face's normal.
 */
#define MTL_RTDIR_EMITTER (-3.0f)

/*
 * An emissive face lights a point while its contribution before the two
 * cosines, intensity / dist^2, is at least this (qrad3's units, one step
 * of a lightmap byte): qrad3's inverse-square falloff never ends, so this
 * is the face's range, sqrt(intensity / MTL_RTDIR_EMIT_CUT).
 */
#define MTL_RTDIR_EMIT_CUT 1.0f

/* qrad3's units to the lightmap's: a lightmap byte of 255 reads 1 */
#define MTL_RTDIR_UNIT (1.0f / 255.0f)

/* the water and translucent faces a shadow ray passes; one more, or any other face, shadows */
#define MTL_RTDIR_LAYERS 4

/* what a shadow ray does at a face (mtl_rtdir_at) */
#define MTL_RTDIR_AT_PASS 1u      /* water or a translucent face: the ray goes on */
#define MTL_RTDIR_AT_SHADOW 2u    /* any other face (the sky, an alias model among them) */

/* the exposure fit's samples: a pixel's channel mean, at most MTL_RTDIR_QMAX, in steps of 1 / MTL_RTDIR_QSCALE */
#define MTL_RTDIR_QMAX 16.0f
#define MTL_RTDIR_QSCALE 64.0f

/* the fit samples every MTL_RTDIR_FIT_STEP-th pixel a row and a column, so its 32-bit sums cannot wrap below 8K */
#define MTL_RTDIR_FIT_STEP 4u

/*
 * The lights a pixel sums, at most (TASK188 slices 1 and 3): the first
 * MTL_RTDIR_MAX_TRACED of its candidates, strongest first, whose shadow
 * ray reaches it.
 */
#define MTL_RTDIR_MAX_TRACED 16

/*
 * The shadow rays a pixel traces, at most (TASK188 slice 3): of the lights
 * in range, the MTL_RTDIR_MAX_RAYS strongest are the pixel's candidates
 * (mtl_rtdir_keep), each ray of at most MTL_RTDIR_LAYERS + 1
 * intersections, whatever the map holds.
 */
#define MTL_RTDIR_MAX_RAYS 16

/* a pixel's candidates (mtl_rtdir_keep): n of them, their indices in the light buffer and their contributions */
typedef struct
{
	unsigned n;
	unsigned capped;                    /* 1 once a light in range found the set full */
	unsigned i[MTL_RTDIR_MAX_RAYS];
	float s[MTL_RTDIR_MAX_RAYS];
} mtl_rtdir_keep_t;

/*
 * k with the light of index i and contribution s added: appended while
 * fewer than MTL_RTDIR_MAX_RAYS are kept, else in the place of the weakest
 * kept one when s is stronger (the first weakest on a tie; an equal light
 * does not replace it), and capped set either way.
 */
static inline mtl_rtdir_keep_t
mtl_rtdir_keep(mtl_rtdir_keep_t k, unsigned i, float s)
{
	unsigned j, w = 0;

	if (k.n < MTL_RTDIR_MAX_RAYS)
	{
		k.i[k.n] = i;
		k.s[k.n] = s;
		k.n++;
		return k;
	}

	k.capped = 1;

	for (j = 1; j < MTL_RTDIR_MAX_RAYS; j++)
	{
		if (k.s[j] < k.s[w])
		{
			w = j;
		}
	}

	if (s > k.s[w])
	{
		k.i[w] = i;
		k.s[w] = s;
	}

	return k;
}

/* STUB (red): k in the buffer's order */
static inline mtl_rtdir_keep_t
mtl_rtdir_keep_rank(mtl_rtdir_keep_t k)
{
	unsigned a, b;

	for (a = 1; a < k.n; a++)
	{
		for (b = a; (b > 0) && (k.i[b - 1] > k.i[b]); b--)
		{
			const unsigned ti = k.i[b];
			const float ts = k.s[b];

			k.i[b] = k.i[b - 1];
			k.s[b] = k.s[b - 1];
			k.i[b - 1] = ti;
			k.s[b - 1] = ts;
		}
	}

	return k;
}

/* STUB (red): every candidate */
static inline int
mtl_rtdir_walk(unsigned j, unsigned n, unsigned seen)
{
	(void)seen;
	return (j < n) ? 1 : 0;
}

/* STUB (red): more lights in range than candidates */
static inline unsigned
mtl_rtdir_capped(mtl_rtdir_keep_t k, unsigned seen)
{
	(void)seen;
	return k.capped;
}

/*
 * The contribution of one light at a surface point, before its colour
 * and style, in qrad3's units (GatherSampleLight, emit_point and
 * emit_spotlight): (intensity - dist) x ndotl, where ndotl is N.L of the
 * unit normal and the unit vector toward the light, spotdot is minus
 * the dot of that vector and the spotlight's direction, and stopdot is
 * the cosine of its cone (MTL_RTDIR_NOSPOT for a point light). 0 when
 * ndotl is at most MTL_RTDIR_MIN_DOT, when spotdot is at most stopdot
 * (outside the cone), when the light is out of range (dist at least
 * intensity), and for any value that is not a number.
 */
static inline float
mtl_rtdir_scale(float intensity, float dist, float ndotl, float spotdot, float stopdot)
{
	float s;

	if (!(ndotl > MTL_RTDIR_MIN_DOT) || !(spotdot > stopdot))
	{
		return 0.0f;
	}

	s = (intensity - dist) * ndotl;
	return (s > 0.0f) ? s : 0.0f;
}

/*
 * The contribution of an emissive face (qrad3's emit_surface in
 * GatherSampleLight): intensity / dist^2 x ndotl x facedot, facedot being
 * minus the dot of the unit vector toward the light and the face's
 * normal (the point is in front of the face). 0 when either cosine is at
 * most MTL_RTDIR_MIN_DOT, when the point is out of the face's range
 * (intensity / dist^2 below MTL_RTDIR_EMIT_CUT), and for any value that
 * is not a number. A distance under 1 unit counts as 1 (qrad3's), so the
 * result is finite.
 */
static inline float
mtl_rtdir_emit_scale(float intensity, float dist, float ndotl, float facedot)
{
	float s;

	if (!(ndotl > MTL_RTDIR_MIN_DOT) || !(facedot > MTL_RTDIR_MIN_DOT) || !(dist > 0.0f) ||
		!(dist * dist * MTL_RTDIR_EMIT_CUT <= intensity))
	{
		return 0.0f;
	}

	/* qrad3 takes a distance under 1 unit as 1, which also keeps the quotient finite */
	if (dist < 1.0f)
	{
		dist = 1.0f;
	}

	s = intensity / (dist * dist) * ndotl * facedot;
	return (s > 0.0f) ? s : 0.0f;
}

/*
 * The cheap test before the cosines: 1 when a light of the buffer
 * (intensity, its spot word stopdot) can reach a point dist away. A point
 * or spot light's linear falloff ends at its intensity; an emissive
 * face's range is sqrt(intensity / MTL_RTDIR_EMIT_CUT).
 */
static inline int
mtl_rtdir_reach(float intensity, float dist, float stopdot)
{
	if (!(dist > 0.0f))
	{
		return 0;
	}

	if (stopdot == MTL_RTDIR_EMITTER)
	{
		return (dist * dist * MTL_RTDIR_EMIT_CUT <= intensity) ? 1 : 0;
	}

	return (dist < intensity) ? 1 : 0;
}

/*
 * A light of the buffer at a point: mtl_rtdir_emit_scale for an emissive
 * face (stopdot MTL_RTDIR_EMITTER, spotdot then minus the dot of the
 * direction and the face's normal), else mtl_rtdir_scale.
 */
static inline float
mtl_rtdir_contrib(float intensity, float dist, float ndotl, float spotdot, float stopdot)
{
	if (stopdot == MTL_RTDIR_EMITTER)
	{
		return mtl_rtdir_emit_scale(intensity, dist, ndotl, spotdot);
	}

	return mtl_rtdir_scale(intensity, dist, ndotl, spotdot, stopdot);
}

/*
 * What a shadow ray toward a light does at a face of material id mat (the
 * G-buffer's, rm_rt_materials'; 0xff for an alias model): water (3) and
 * translucent faces (5) let it through, everything else shadows it, the
 * sky (4) too, which is a solid face between a point and a light. At the
 * layer-th intersection (0 the nearest), a face it would pass past the
 * first MTL_RTDIR_LAYERS shadows, so a ray asks for at most
 * MTL_RTDIR_LAYERS + 1 intersections. A ray that meets nothing before the
 * light reaches it (the kernel).
 */
static inline unsigned
mtl_rtdir_at(unsigned mat, int layer)
{
	if (((mat == 3u) || (mat == 5u)) && (layer < MTL_RTDIR_LAYERS))
	{
		return MTL_RTDIR_AT_PASS;
	}

	return MTL_RTDIR_AT_SHADOW;
}

/*
 * A sample of the exposure fit: the mean of the three channels, 0 for a
 * value that is not a number or below 0, at most MTL_RTDIR_QMAX, times
 * MTL_RTDIR_QSCALE and rounded; so a sample is at most 1024 and a frame's
 * sum is exact in any order (integer adds).
 */
static inline unsigned
mtl_rtdir_q(float r, float g, float b)
{
	float m = (r + g + b) / 3.0f;

	if (!(m > 0.0f))
	{
		return 0u;
	}

	m = (m < MTL_RTDIR_QMAX) ? m : MTL_RTDIR_QMAX;
	return (unsigned)(m * MTL_RTDIR_QSCALE + 0.5f);
}

/*
 * The light the resolve reads at a world pixel: the direct light raw (in
 * the lightmap's units, the sum over the lights the pixel sees) times the
 * exposure, each channel at most 1 (the 16-bit store's) and 0 for a value
 * that is not a number; alpha 1. No lightmap argument: the pixel's light
 * is the traced light alone.
 */
static inline mtl_rgba_t
mtl_rtdir_light(float r, float g, float b, float exposure)
{
	mtl_rgba_t c;
	float v[3];
	int k;

	v[0] = r * exposure;
	v[1] = g * exposure;
	v[2] = b * exposure;

	for (k = 0; k < 3; k++)
	{
		v[k] = (v[k] > 0.0f) ? ((v[k] < 1.0f) ? v[k] : 1.0f) : 0.0f;
	}

	c.r = v[0];
	c.g = v[1];
	c.b = v[2];
	c.a = 1.0f;
	return c;
}

#endif
