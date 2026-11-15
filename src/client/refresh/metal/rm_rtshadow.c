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
 * The pure part of ref_metal's ray-traced dynamic-light shadows (RT3,
 * TASK55; header/rm_rtshadow.h): when the effect runs, what a shadow ray
 * keeps of its light through each triangle, the view the kernel
 * unprojects its pixels with, the kernel's choice of lights and the
 * resolve's sum as plain C over the shared per-pixel arithmetic
 * (common/mtl_rts_math.h), the development view's value and the stats
 * line. Held by tests/metal/rm_rtshadow_test.c; no SDL, no Metal.
 *
 * =======================================================================
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "header/rm_rtshadow.h"
#include "header/rm_rt.h"

int
rm_rts_on(float shadows, int device_rt, float fullbright, float dynamic, float flashblend)
{
	if (!device_rt || !isfinite(shadows) || (shadows == 0))
	{
		return 0;
	}

	/* gl1 adds no dynamic light to a lightmap under any of these (R_RegenAllLightmaps, R_PushDlights) */
	if ((isfinite(fullbright) && (fullbright != 0)) || !isfinite(dynamic) || (dynamic == 0) ||
		(isfinite(flashblend) && (flashblend != 0)))
	{
		return 0;
	}

	return 1;
}

unsigned
rm_rts_transmit_one(int texflags)
{
	const unsigned m = rm_rt_material(texflags);

	if ((m != RM_RT_MAT_WATER) && (m != RM_RT_MAT_ALPHA))
	{
		return 0;
	}

	if (!(texflags & (RM_TEX_TRANS33 | RM_TEX_TRANS66)))
	{
		return 0;   /* water drawn opaque */
	}

	/* 1 - gl1's alpha (R_DrawAlphaSurfaces): 0.67 and 0.34, rounded to 8 bits */
	return mtl_rts_vis8(1.0f - rm_trans_alpha(texflags));
}

long
rm_rts_transmit(const rm_surfrec_t *recs, long nrecs, unsigned long nindices, unsigned char *out,
		unsigned long cap, unsigned long *ntrans)
{
	unsigned long at = 0, tri = 0, passing = 0;
	long s;

	if (ntrans)
	{
		*ntrans = 0;
	}

	if ((nrecs < 0) || (nindices % 3) || (nindices / 3 > cap) || (nrecs && (!recs || !out)))
	{
		return -1;
	}

	/* the walk of rm_rt_materials: each record where the last ended, whole triangles, inside the buffer */
	for (s = 0; s < nrecs; s++)
	{
		const rm_surfrec_t *r = &recs[s];
		unsigned long n = r->num_indices / 3, k;
		unsigned t;

		if ((r->first_index != at) || (r->num_indices % 3) || (r->num_indices > nindices - at))
		{
			return -1;
		}

		t = rm_rts_transmit_one((int)r->texflags);

		for (k = 0; k < n; k++)
		{
			out[tri + k] = (unsigned char)t;
		}

		passing += t ? n : 0;
		tri += n;
		at += r->num_indices;
	}

	if (at != nindices)
	{
		return -1;
	}

	if (ntrans)
	{
		*ntrans = passing;
	}

	return (long)tri;
}

int
rm_rts_invert(const float *m, float *out)
{
	double a[16], inv[16], det;
	int i;

	for (i = 0; i < 16; i++)
	{
		a[i] = m[i];
		out[i] = (i % 5 == 0) ? 1.0f : 0.0f;

		if (!isfinite(a[i]))
		{
			return 0;
		}
	}

	/* the cofactors (column-major; the inverse of a 4 x 4 by its adjugate) */
	inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] +
		a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
	inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] -
		a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
	inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] +
		a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
	inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] -
		a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
	inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] -
		a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
	inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] +
		a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
	inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] -
		a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
	inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] +
		a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
	inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] +
		a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
	inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] -
		a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
	inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] +
		a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
	inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] -
		a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
	inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] -
		a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
	inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] +
		a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
	inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] -
		a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
	inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] +
		a[8] * a[1] * a[6] - a[8] * a[2] * a[5];

	det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];

	if (!isfinite(det) || (fabs(det) < 1e-30))
	{
		return 0;
	}

	for (i = 0; i < 16; i++)
	{
		out[i] = (float)(inv[i] / det);
	}

	return 1;
}

int
rm_rts_view(const float *proj, const float *world, const rm_viewport_t *vp, const float *eye, float znear,
		rm_rts_view_t *out)
{
	double m[16];
	float f[16];
	int r, c, k;

	memset(out, 0, sizeof(*out));

	if (!isfinite(znear) || !(znear > 0) || !(vp->w > 0) || !(vp->h > 0))
	{
		return 0;
	}

	/* proj x world in double (column-major), then its inverse */
	for (c = 0; c < 4; c++)
	{
		for (r = 0; r < 4; r++)
		{
			double sum = 0;

			for (k = 0; k < 4; k++)
			{
				sum += (double)proj[k * 4 + r] * (double)world[c * 4 + k];
			}

			m[c * 4 + r] = sum;
			f[c * 4 + r] = (float)sum;
		}
	}

	(void)m;

	if (!rm_rts_invert(f, out->inv))
	{
		return 0;
	}

	out->vp[0] = (float)vp->x;
	out->vp[1] = (float)vp->y;
	out->vp[2] = (float)vp->w;
	out->vp[3] = (float)vp->h;
	out->eye[0] = eye[0];
	out->eye[1] = eye[1];
	out->eye[2] = eye[2];
	out->eye[3] = 1;
	out->bias_k = 4.0f / 16777216.0f / znear;
	return 1;
}

int
rm_rts_unproject(const rm_rts_view_t *v, float px, float py, float depth, float *out)
{
	/* the pixel's centre in GL's clip coordinates: x right, y up, z from mtl_gl_clip_z's [0, 1] back to [-1, 1] */
	const float cx = (px + 0.5f - v->vp[0]) / v->vp[2] * 2.0f - 1.0f;
	const float cy = 1.0f - (py + 0.5f - v->vp[1]) / v->vp[3] * 2.0f;
	const float cz = depth * 2.0f - 1.0f;
	float h[4];
	int i;

	for (i = 0; i < 4; i++)
	{
		h[i] = v->inv[i] * cx + v->inv[4 + i] * cy + v->inv[8 + i] * cz + v->inv[12 + i];
	}

	if (!(h[3] > 0) && !(h[3] < 0))
	{
		return 0;
	}

	for (i = 0; i < 3; i++)
	{
		out[i] = h[i] / h[3];
	}

	return 1;
}

mtl_rts_top_t
rm_rts_select(mtl_rts_v3_t p, mtl_rts_v3_t n, const rm_rts_light_t *l, int nl, unsigned *flags)
{
	mtl_rts_top_t t = mtl_rts_top_init();
	int i;

	*flags = 0;

	for (i = 0; i < nl && i < MTL_RTS_MAX_LIGHTS; i++)
	{
		const mtl_rts_reach_t r = mtl_rts_reach(p, n, mtl_rts_v3(l[i].origin[0], l[i].origin[1], l[i].origin[2]),
				l[i].origin[3]);

		if (mtl_rts_in_range(r))
		{
			*flags |= MTL_RTS_IN_RANGE;
		}

		t = mtl_rts_top_add(t, (unsigned)i, mtl_rts_add(r));
	}

	return t;
}

void
rm_rts_term(mtl_rts_v3_t p, mtl_rts_v3_t n, const rm_rts_light_t *l, int nl, unsigned pidx, unsigned pvis,
		float *rgb)
{
	int i;

	rgb[0] = rgb[1] = rgb[2] = 0;

	for (i = 0; i < nl && i < MTL_RTS_MAX_LIGHTS; i++)
	{
		const float add = mtl_rts_add(mtl_rts_reach(p, n,
				mtl_rts_v3(l[i].origin[0], l[i].origin[1], l[i].origin[2]), l[i].origin[3]));
		float w;

		if (!(add > 0))
		{
			continue;
		}

		w = mtl_rts_light_vis(pidx, pvis, (unsigned)i) * add;
		rgb[0] += w * l[i].color[0];
		rgb[1] += w * l[i].color[1];
		rgb[2] += w * l[i].color[2];
	}
}

int
rm_rts_show(float v)
{
	if (!isfinite(v) || !(v > 0))
	{
		return 0;
	}

	return (v >= RM_RTS_SHOW_MAX) ? RM_RTS_SHOW_MAX : (int)v;
}

int
rm_rts_stats_format(const rm_rts_stats_t *s, char *buf, size_t len)
{
	char ms[32];
	int n;

	if (!len)
	{
		return 0;
	}

	if (s->ms >= 0)
	{
		snprintf(ms, sizeof(ms), "%.3f", s->ms);
	}
	else
	{
		snprintf(ms, sizeof(ms), "-");
	}

	n = snprintf(buf, len, "ref_metal: rt shadows: frame %lu: %lu pixels, %lu in range, %lu traced, %lu shadowed, "
			"%lu rays, %s ms", s->frame, s->pixels, s->inrange, s->traced, s->shadowed, s->rays, ms);

	if ((n < 0) || ((size_t)n >= len))
	{
		buf[0] = 0;
		return 0;
	}

	return 1;
}
