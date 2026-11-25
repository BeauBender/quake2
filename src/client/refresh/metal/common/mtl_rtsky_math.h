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
 * The per-pixel arithmetic of RT-primary's sky light and sun (TASK189,
 * PM7 slice 1; docs/plans/presentation-modes.md, section 4.1), shared by
 * rm_rtdir.metal and the host test (tests/metal/rm_rtdir_test.c): what a
 * sky or sun ray does at a face (RT2's sun rule, TASK56, reused as it
 * is), the sky ray's direction (the AO's one ray's, TASK157), which views
 * trace which term, a term's value and the light the resolve reads with
 * the two terms. Plain C that the Metal shading language compiles too;
 * after mtl_shader_math.h, mtl_rtao_math.h, mtl_rtsun_math.h and
 * mtl_rtdir_math.h, none of which it changes.
 *
 * Neither term reads the lightmap (the plan's section 6, item 5): a ray
 * that reaches the sky returns the sky's colour, never a luxel, and the
 * sun's term is the RT2 rule's without its floor gate (the gate reads the
 * lightmap at the pixel, which RT-primary has turned off).
 *
 * =======================================================================
 */

#ifndef MTL_RTSKY_MATH_H
#define MTL_RTSKY_MATH_H

/* the sky rays a world pixel traces: one, cosine-weighted about its normal */
#define MTL_RTSKY_RAYS 1

/* the sun rays a world pixel traces: one, toward TASK56's direction */
#define MTL_RTSKY_SUN_RAYS 1

/*
 * The rays a world pixel traces under RT-primary, at most, whatever the
 * map and the frame hold: the map's lights' MTL_RTDIR_MAX_RAYS, the
 * dynamic lights' MTL_RTDIR_MAX_DLIGHTS, the own light's one eye ray
 * (TASK188 slice 3b), the sky's and the sun's. Each ray asks for at most
 * MTL_RTDIR_LAYERS + 1 (MTL_RTSUN_LAYERS + 1 for the sky and the sun)
 * intersections.
 */
#define MTL_RTSKY_PIXEL_RAYS (MTL_RTDIR_MAX_RAYS + MTL_RTDIR_MAX_DLIGHTS + 1 + MTL_RTSKY_RAYS + MTL_RTSKY_SUN_RAYS)

/* r_rt_direct_sky, the sky term's scale, at most */
#define MTL_RTSKY_SCALE_MAX 4.0f

/* the r_rt_direct_show views of the two terms alone (rm_rtdir.h's RM_RTDIR_SHOW_SKY and RM_RTDIR_SHOW_SUN) */
#define MTL_RTSKY_SHOW_SKY 5u
#define MTL_RTSKY_SHOW_SUN 6u

/* the two terms, for mtl_rtsky_traces */
#define MTL_RTSKY_TERM_SKY 0u
#define MTL_RTSKY_TERM_SUN 1u

/*
 * What a sky or sun ray does at the face its layer-th intersection met (0
 * the nearest): RT2's sun rule, mtl_rtsun_step of mtl_rtsun_at. The sky
 * (4) is reached (MTL_RTSUN_AT_LIT); water (3) and translucent faces (5)
 * are passed (MTL_RTSUN_AT_PASS) up to MTL_RTSUN_LAYERS of them; any
 * other face, an alias model's among them, shadows. A ray that meets
 * nothing is shadowed too (the kernel): only the sky lets its light in.
 */
static inline unsigned
mtl_rtsky_at(unsigned mat, int layer)
{
	return mtl_rtsun_step(mtl_rtsun_at(mat), layer);
}

/*
 * 1 when a world pixel under r_rt_direct_show view show traces term
 * (MTL_RTSKY_TERM_*) whose colour's largest channel is scale: the picture
 * and views 1 and 2 trace both, view 5 the sky alone and view 6 the sun
 * alone; view 3 (no ray at all) and view 4 (the dynamic lights alone)
 * neither. 0 when scale is not above 0, a value that is not a number
 * among them: no ray for a term that adds nothing.
 */
static inline int
mtl_rtsky_traces(unsigned show, unsigned term, float scale)
{
	if (!(scale > 0.0f))
	{
		return 0;
	}

	if (show <= 2u)
	{
		return 1;
	}

	return (show == ((term == MTL_RTSKY_TERM_SKY) ? MTL_RTSKY_SHOW_SKY : MTL_RTSKY_SHOW_SUN)) ? 1 : 0;
}

/* 1 under the views of the two terms alone (5 and 6): the direct light is left out of the write */
static inline int
mtl_rtsky_alone(unsigned show)
{
	return ((show == MTL_RTSKY_SHOW_SKY) || (show == MTL_RTSKY_SHOW_SUN)) ? 1 : 0;
}

/*
 * The sky ray's direction about the unit normal n from its two numbers u
 * (azimuth) and v (elevation), each in [0, 1): cosine-weighted over the
 * hemisphere, mtl_rtao_dir's ray 0 with the azimuth's number doubled, as
 * the AO's one ray (r_rt_ao_rays 1), so its azimuth covers the circle; at
 * least MTL_RTAO_MIN_COS above the stored plane, so it never meets the
 * pixel's own face. With a cosine-weighted ray the share of rays that
 * reach a sky of one colour is the irradiance over pi, so one ray's hit
 * (0 or 1) times the colour is the term's estimate.
 */
static inline mtl_rtao_v3_t
mtl_rtsky_dir(mtl_rtao_v3_t n, float u, float v)
{
	return mtl_rtao_dir(n, 0u, 2.0f * u, v);
}

/*
 * One channel of a term: c (the term's colour: the sky's mean colour times
 * r_rt_direct_sky, or sky_color times r_rt_sunscale for the sun) times k
 * (the sky: 1 when its ray reached the sky, else 0; the sun: its ray's
 * visibility, 0 or 1, times max(0, N.L), mtl_rtsun_ndotl); 0 for a value
 * that is not a number or below 0.
 */
static inline float
mtl_rtsky_term(float c, float k)
{
	const float s = c * k;

	return (s > 0.0f) ? s : 0.0f;
}

/*
 * The light the resolve reads at a traced world pixel with the two terms:
 * c (mtl_rtdir_light's, the direct light times the exposure) plus (r, g,
 * b), the sky term plus the sun term, in the light attachment's units as
 * RT2 adds its sun (not scaled by the exposure, which is fitted to the
 * map's point lights against its lightmap); each channel at most 1 (the
 * 16-bit store's) and 0 for a value that is not a number. With alone
 * (views 5 and 6) the terms by themselves, c's light left out. Alpha c's.
 */
static inline mtl_rgba_t
mtl_rtsky_light(mtl_rgba_t c, float r, float g, float b, int alone)
{
	float v[3];
	int k;

	v[0] = (alone ? 0.0f : c.r) + r;
	v[1] = (alone ? 0.0f : c.g) + g;
	v[2] = (alone ? 0.0f : c.b) + b;

	for (k = 0; k < 3; k++)
	{
		v[k] = (v[k] > 0.0f) ? ((v[k] < 1.0f) ? v[k] : 1.0f) : 0.0f;
	}

	c.r = v[0];
	c.g = v[1];
	c.b = v[2];
	return c;
}

#endif
