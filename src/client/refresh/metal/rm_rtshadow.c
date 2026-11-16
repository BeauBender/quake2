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

void
rm_rts_alias_box(const float *scale, const float *translate, const float *oldscale, const float *oldtranslate,
		float *mins, float *maxs)
{
	int i;

	for (i = 0; i < 3; i++)
	{
		const float a = translate[i], b = translate[i] + scale[i] * 255;
		const float c = oldtranslate[i], d = oldtranslate[i] + oldscale[i] * 255;

		mins[i] = fminf(fminf(a, b), fminf(c, d));
		maxs[i] = fmaxf(fmaxf(a, b), fmaxf(c, d));
	}
}

unsigned
rm_rts_carried(const float *xform, const float *mins, const float *maxs, const rm_rts_light_t *l, int nl)
{
	double m[9], inv[9], det;
	unsigned carry = 0;
	int i, k;

	if (!xform || !mins || !maxs || !l || (nl <= 0))
	{
		return 0;
	}

	for (i = 0; i < 12; i++)
	{
		if (!isfinite(xform[i]))
		{
			return 0;
		}
	}

	for (i = 0; i < 3; i++)
	{
		if (!isfinite(mins[i]) || !isfinite(maxs[i]) || (mins[i] > maxs[i]))
		{
			return 0;
		}
	}

	/* the basis, row r and column c at m[3 * r + c] (xform holds the columns), and its inverse by the adjugate */
	for (i = 0; i < 3; i++)
	{
		for (k = 0; k < 3; k++)
		{
			m[3 * i + k] = xform[3 * k + i];
		}
	}

	inv[0] = m[4] * m[8] - m[5] * m[7];
	inv[1] = m[2] * m[7] - m[1] * m[8];
	inv[2] = m[1] * m[5] - m[2] * m[4];
	inv[3] = m[5] * m[6] - m[3] * m[8];
	inv[4] = m[0] * m[8] - m[2] * m[6];
	inv[5] = m[2] * m[3] - m[0] * m[5];
	inv[6] = m[3] * m[7] - m[4] * m[6];
	inv[7] = m[1] * m[6] - m[0] * m[7];
	inv[8] = m[0] * m[4] - m[1] * m[3];
	det = m[0] * inv[0] + m[1] * inv[3] + m[2] * inv[6];

	if (!isfinite(det) || (fabs(det) < 1e-12))
	{
		return 0;
	}

	if (nl > MTL_RTS_MAX_LIGHTS)
	{
		nl = MTL_RTS_MAX_LIGHTS;
	}

	for (i = 0; i < nl; i++)
	{
		double d[3];
		int inside = 1;

		for (k = 0; k < 3; k++)
		{
			d[k] = (double)l[i].origin[k] - xform[9 + k];
		}

		/* the light in the model's space, against the box grown by the tolerance (a light not finite fails every test) */
		for (k = 0; k < 3 && inside; k++)
		{
			const double q = (inv[3 * k] * d[0] + inv[3 * k + 1] * d[1] + inv[3 * k + 2] * d[2]) / det;

			inside = (q >= (double)mins[k] - RM_RTS_CARRY_TOL) && (q <= (double)maxs[k] + RM_RTS_CARRY_TOL);
		}

		if (inside)
		{
			carry |= 1u << i;
		}
	}

	return carry;
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
rm_rts_select(mtl_rts_surf_t sf, const rm_rts_light_t *l, int nl, float *adds, unsigned *flags)
{
	mtl_rts_top_t t = mtl_rts_top_init();
	int i;

	*flags = 0;

	for (i = 0; i < nl && i < MTL_RTS_MAX_LIGHTS; i++)
	{
		const mtl_rts_lit_t li = mtl_rts_surf_light(sf, mtl_rts_v3(l[i].origin[0], l[i].origin[1], l[i].origin[2]),
				l[i].origin[3]);

		adds[i] = li.add;

		if (li.in_range)
		{
			*flags |= MTL_RTS_IN_RANGE;
		}

		t = mtl_rts_top_add(t, (unsigned)i, adds[i]);
	}

	return t;
}

void
rm_rts_term(const float *adds, const rm_rts_light_t *l, int nl, unsigned pidx, unsigned pvis, float *rgb)
{
	int i;

	rgb[0] = rgb[1] = rgb[2] = 0;

	for (i = 0; i < nl && i < MTL_RTS_MAX_LIGHTS; i++)
	{
		float w;

		if (!(adds[i] > 0))
		{
			continue;
		}

		w = mtl_rts_light_vis(pidx, pvis, (unsigned)i) * adds[i];
		rgb[0] += w * l[i].color[0];
		rgb[1] += w * l[i].color[1];
		rgb[2] += w * l[i].color[2];
	}
}

long
rm_rts_surface_map(const rm_rts_range_t *r, long nsurf, unsigned long ntris, unsigned *tri_surf)
{
	unsigned long k, done = 0;
	long i;

	for (k = 0; k < ntris; k++)
	{
		tri_surf[k] = 0xffffffffu;
	}

	if ((nsurf < 0) || (nsurf && !r) || ((unsigned long)nsurf >= MTL_RTS_SURF_UNLIT))
	{
		return -1;
	}

	for (i = 0; i < nsurf; i++)
	{
		const unsigned long first = r[i].first_index, n = r[i].num_indices;

		if ((first % 3) || (n % 3) || (first / 3 > ntris) || (n / 3 > ntris - first / 3))
		{
			return -1;
		}

		for (k = first / 3; k < (first + n) / 3; k++)
		{
			if (tri_surf[k] != 0xffffffffu)
			{
				return -1;
			}

			tri_surf[k] = (unsigned)i | (r[i].unlit ? MTL_RTS_SURF_UNLIT : 0);
			done++;
		}
	}

	return (long)done;
}

long
rm_rts_vertex_map(const rm_rts_vrange_t *r, long nsurf, unsigned long nverts, unsigned *vert_surf)
{
	unsigned long k, done = 0;
	long i;

	if (nverts > MTL_RTS_GB_VMAX)
	{
		return -1;
	}

	for (k = 0; k < nverts; k++)
	{
		vert_surf[k] = 0xffffffffu;
	}

	if ((nsurf < 0) || (nsurf && !r) || ((unsigned long)nsurf >= MTL_RTS_SURF_UNLIT))
	{
		return -1;
	}

	for (i = 0; i < nsurf; i++)
	{
		const unsigned long first = r[i].first_vertex, n = r[i].num_vertices;

		if ((first > nverts) || (n > nverts - first))
		{
			return -1;
		}

		for (k = first; k < first + n; k++)
		{
			if (vert_surf[k] != 0xffffffffu)
			{
				return -1;
			}

			vert_surf[k] = (unsigned)i | (r[i].unlit ? MTL_RTS_SURF_UNLIT : 0);
			done++;
		}
	}

	return (long)done;
}

int
rm_rts_ent_rows(const float *origin, const float *angles, float *out)
{
	float xf[12], m[16], inv[16];
	int c, r, ok = 1;

	for (c = 0; c < 3; c++)
	{
		ok = ok && isfinite(origin[c]) && isfinite(angles[c]);
	}

	/* the instance's object-to-world transform as rm_rt_instances makes it, as a 4 x 4 (column-major) */
	if (ok)
	{
		rm_rt_transform(origin, angles, 1, xf);

		for (c = 0; c < 4; c++)
		{
			m[4 * c] = xf[3 * c];
			m[4 * c + 1] = xf[3 * c + 1];
			m[4 * c + 2] = xf[3 * c + 2];
			m[4 * c + 3] = (c == 3) ? 1.0f : 0.0f;
		}

		ok = rm_rts_invert(m, inv);
	}

	if (!ok)
	{
		for (c = 0; c < 16; c++)
		{
			inv[c] = (c % 5 == 0) ? 1.0f : 0.0f;
		}
	}

	/* the inverse's upper three rows, then whether it may be used */
	for (r = 0; r < 3; r++)
	{
		for (c = 0; c < 4; c++)
		{
			out[4 * r + c] = inv[4 * c + r];
		}
	}

	out[12] = ok ? 1.0f : 0.0f;
	out[13] = out[14] = out[15] = 0.0f;
	return ok;
}

static mtl_rts_v4_t
rm_rts_row(const float *f)
{
	mtl_rts_v4_t v;

	v.x = f[0];
	v.y = f[1];
	v.z = f[2];
	v.w = f[3];
	return v;
}

int
rm_rts_gb_lookup(unsigned v, const unsigned *vert_surf, unsigned long nverts, unsigned long nsurf,
		const float *rows, int nents, unsigned *raw, mtl_rts_xform_t *w2o)
{
	const mtl_rts_gb_t g = mtl_rts_gb_decode(v, (nverts > MTL_RTS_GB_VMAX) ? 0u : (unsigned)nverts,
			(nents < 0) ? 0u : (unsigned)nents);

	*w2o = mtl_rts_xform_identity();

	/* the kernel's order: the value, the vertex's surface, then the entity's row */
	if (!g.ok || !vert_surf || (nsurf > MTL_RTS_SURF_UNLIT) || !mtl_rts_gb_surf_ok(vert_surf[g.vid], (unsigned)nsurf))
	{
		return 0;
	}

	if (g.ent)
	{
		const float *row = rows ? rows + (size_t)(g.ent - 1) * RM_RTS_ROW_FLOATS : NULL;

		if (!row || (row[12] == 0.0f))
		{
			return 0;
		}

		*w2o = mtl_rts_xform_rows(rm_rts_row(row), rm_rts_row(row + 4), rm_rts_row(row + 8));
	}

	*raw = vert_surf[g.vid];
	return 1;
}

void
rm_rts_plane(const float *normal, float dist, int back, float *out)
{
	const float k = back ? -1.0f : 1.0f;

	out[0] = 0;
	out[1] = 0;
	out[2] = 1;
	out[3] = 0;

	if (!normal || !isfinite(normal[0]) || !isfinite(normal[1]) || !isfinite(normal[2]) || !isfinite(dist))
	{
		return;
	}

	out[0] = k * normal[0];
	out[1] = k * normal[1];
	out[2] = k * normal[2];
	out[3] = k * dist;
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
