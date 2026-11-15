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
 * The pure part of ref_metal's G-buffer (RT2, TASK54): header/rm_gbuf.h
 * says what each function does; tests/metal/rm_gbuf_test.c holds them.
 *
 * =======================================================================
 */

#include <math.h>
#include <string.h>

#include "header/rm_gbuf.h"

/* the 162 light normals, as rm_pure.c's table of gl1's r_avertexnormals */
static const float rm_gb_anorms[RM_NUMVERTEXNORMALS][3] = {
#include "../constants/anorms.h"
};

#define RM_GB_MAX_SIDE 16384

static int
rm_gb_on(float v)
{
	return isfinite(v) && (v != 0.0f);
}

int
rm_gb_path(int effects_on, float gbuffer, float motion, int noworld)
{
	if (noworld)
	{
		return RM_GB_PATH_FORWARD;
	}

	if (effects_on || rm_gb_on(gbuffer))
	{
		return RM_GB_PATH_GBUFFER;
	}

	return rm_gb_on(motion) ? RM_GB_PATH_MOTION : RM_GB_PATH_FORWARD;
}

static int
rm_gb_side(int side, float scale)
{
	double v = floor((double)side * scale + 0.5);

	if (v < 1.0)
	{
		return 1;
	}

	return (v > side) ? side : (int)v;
}

int
rm_gb_render_size(int fw, int fh, float scale, int *rw, int *rh)
{
	*rw = *rh = 0;

	if ((fw <= 0) || (fh <= 0))
	{
		return 0;
	}

	if (!isfinite(scale) || !(scale > 0.0f) || !(scale < 1.0f))
	{
		*rw = fw;
		*rh = fh;
		return 1;
	}

	*rw = rm_gb_side(fw, scale);
	*rh = rm_gb_side(fh, scale);
	return 1;
}

int
rm_gb_bytes(int path, int rw, int rh, int scaled, rm_gb_bytes_t *b)
{
	size_t px;

	memset(b, 0, sizeof(*b));

	if ((rw < 1) || (rh < 1) || (rw > RM_GB_MAX_SIDE) || (rh > RM_GB_MAX_SIDE) ||
		((path != RM_GB_PATH_FORWARD) && (path != RM_GB_PATH_MOTION) && (path != RM_GB_PATH_GBUFFER)))
	{
		return 0;
	}

	if (path == RM_GB_PATH_FORWARD)
	{
		return 1;
	}

	px = (size_t)rw * (size_t)rh;
	b->depth = px * 4;
	b->stencil = px;
	b->motion = px * 4;

	if (path == RM_GB_PATH_GBUFFER)
	{
		b->normal = px * 4;
		b->material = px;
		b->lightmap = px * 4;
		b->albedo = px * 4;
	}

	if (scaled)
	{
		b->color = px * 4;
	}

	b->total = b->depth + b->stencil + b->normal + b->material + b->motion + b->lightmap + b->albedo + b->color;
	return 1;
}

static double
rm_gb_edge(double e, int from, int to)
{
	return floor(e * (double)to / (double)from);
}

void
rm_gb_render_rect(const rm_viewport_t *vp, int fw, int fh, int rw, int rh, rm_viewport_t *out)
{
	double x0, x1, y0, y1;

	*out = *vp;

	if ((fw <= 0) || (fh <= 0) || ((rw == fw) && (rh == fh)))
	{
		return;
	}

	x0 = rm_gb_edge(vp->x, fw, rw);
	x1 = rm_gb_edge(vp->x + vp->w, fw, rw);
	y0 = rm_gb_edge(vp->y, fh, rh);
	y1 = rm_gb_edge(vp->y + vp->h, fh, rh);
	out->x = x0;
	out->y = y0;
	out->w = x1 - x0;
	out->h = y1 - y0;
}

void
rm_gb_jitter(const float *m, float jx, float jy, double vw, double vh, float *out)
{
	float ox, oy;
	int c;

	memcpy(out, m, 16 * sizeof(float));

	if (((jx == 0.0f) && (jy == 0.0f)) || !(vw > 0) || !(vh > 0))
	{
		return;
	}

	/* clip x += ox * clip w: each column's x and y rows take the w row's times the offset */
	ox = (float)(2.0 * jx / vw);
	oy = (float)(-2.0 * jy / vh);

	for (c = 0; c < 4; c++)
	{
		out[c * 4 + 0] = m[c * 4 + 0] + ox * m[c * 4 + 3];
		out[c * 4 + 1] = m[c * 4 + 1] + oy * m[c * 4 + 3];
	}
}

int
rm_gb_entity_split(int flags, int kind, float shadows)
{
	if ((flags & (RM_RF_TRANSLUCENT | RM_RF_BEAM | RM_RF_WEAPONMODEL | RM_RF_SHELLS)) ||
		((kind != RM_GB_KIND_ALIAS) && (kind != RM_GB_KIND_BRUSH)))
	{
		return RM_GB_ENT_FORWARD;
	}

	return RM_GB_ENT_RASTER | (((kind == RM_GB_KIND_ALIAS) && rm_shadow_drawn(flags, shadows)) ? RM_GB_ENT_SHADOW : 0);
}

float
rm_gb_pixel_scale(const rm_viewport_t *frame, const rm_viewport_t *render)
{
	double k;

	if (!frame || !render || !isfinite(frame->h) || !isfinite(render->h) || !(frame->h > 0) || !(render->h > 0))
	{
		return 1.0f;
	}

	k = render->h / frame->h;
	return (k < 1.0) ? (float)k : 1.0f;
}

void
rm_gb_point_scale(rm_points_t *s, float k)
{
	if (!s || !isfinite(k) || !(k > 0.0f) || !(k < 1.0f))
	{
		return;
	}

	s->size *= k;
	s->min *= k;
	s->max *= k;
}

int
rm_gb_show(float v)
{
	if (!isfinite(v) || !(v > 0.0f))
	{
		return 0;
	}

	return (v < (float)RM_GB_SHOW_MAX) ? (int)v : RM_GB_SHOW_MAX;
}

void
rm_gb_hist_reset(rm_gb_hist_t *h)
{
	memset(h, 0, sizeof(*h));
}

void
rm_gb_match(const rm_gb_ent_t *prev, int np, const rm_gb_ent_t *cur, int nc, float maxdist, int weapon,
		int *match)
{
	unsigned char used[RM_GB_MAX_ENTS];
	const float maxd2 = maxdist * maxdist;
	int i, j;

	if (nc > RM_GB_MAX_ENTS)
	{
		nc = RM_GB_MAX_ENTS;
	}

	if (np > RM_GB_MAX_ENTS)
	{
		np = RM_GB_MAX_ENTS;
	}

	memset(used, 0, sizeof(used));

	for (i = 0; i < nc; i++)
	{
		int best = -1;
		float bestd = 0;

		for (j = 0; j < np; j++)
		{
			float dx, dy, dz, d;

			if (used[j] || (prev[j].model != cur[i].model) || ((prev[j].flags ^ cur[i].flags) & weapon))
			{
				continue;
			}

			dx = cur[i].origin[0] - prev[j].origin[0];
			dy = cur[i].origin[1] - prev[j].origin[1];
			dz = cur[i].origin[2] - prev[j].origin[2];
			d = dx * dx + dy * dy + dz * dz;

			/* a distance that is not finite fails the bound */
			if (!(d <= maxd2))
			{
				continue;
			}

			if ((best < 0) || (d < bestd))
			{
				best = j;
				bestd = d;
			}
		}

		match[i] = best;

		if (best >= 0)
		{
			used[best] = 1;
		}
	}
}

void
rm_gb_hist_match(rm_gb_hist_t *h, float maxdist, int weapon)
{
	int i, nc = h->cur.nents;

	if (nc > RM_GB_MAX_ENTS)
	{
		nc = RM_GB_MAX_ENTS;
	}

	for (i = 0; i < RM_GB_MAX_ENTS; i++)
	{
		h->match[i] = -1;
	}

	if (!h->prev.valid || (nc <= 0))
	{
		return;
	}

	rm_gb_match(h->prev.ents, h->prev.nents, h->cur.ents, nc, maxdist, weapon, h->match);
}

const rm_gb_ent_t *
rm_gb_hist_prev(const rm_gb_hist_t *h, int i)
{
	if ((i < 0) || (i >= RM_GB_MAX_ENTS) || (i >= h->cur.nents) || (h->match[i] < 0) ||
		(h->match[i] >= h->prev.nents) || (h->match[i] >= RM_GB_MAX_ENTS))
	{
		return NULL;
	}

	return &h->prev.ents[h->match[i]];
}

void
rm_gb_hist_end(rm_gb_hist_t *h)
{
	int i;

	h->prev = h->cur;
	h->prev.valid = 1;
	memset(&h->cur, 0, sizeof(h->cur));

	for (i = 0; i < RM_GB_MAX_ENTS; i++)
	{
		h->match[i] = -1;
	}
}

long
rm_gb_alias_map(const int *order, long norder, int num_xyz, unsigned short *map, long cap)
{
	long pos = 0, nv = 0;

	for (;;)
	{
		long count;

		if (pos >= norder)
		{
			return -1;
		}

		count = order[pos++];

		if (!count)
		{
			break;
		}

		if (count < 0)
		{
			count = -count;
		}

		/* rm_alias_emit's bounds on a primitive, but the index buffer's (the caller's room is its own) */
		if ((count < 3) || (count > (norder - pos) / 3) || (count > cap - nv))
		{
			return -1;
		}

		do
		{
			const int index_xyz = order[pos + 2];

			if ((index_xyz < 0) || (index_xyz >= num_xyz))
			{
				return -1;
			}

			map[nv++] = (unsigned short)index_xyz;
			pos += 3;
		}
		while (--count);
	}

	return nv;
}

void
rm_gb_alias_normal(int index, int oindex, float backlerp, float *out)
{
	const float *n, *on;
	float len;
	int i;

	n = rm_gb_anorms[((index >= 0) && (index < RM_NUMVERTEXNORMALS)) ? index : 0];
	on = rm_gb_anorms[((oindex >= 0) && (oindex < RM_NUMVERTEXNORMALS)) ? oindex : 0];

	for (i = 0; i < 3; i++)
	{
		out[i] = n[i] * (1.0f - backlerp) + on[i] * backlerp;
	}

	len = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);

	if (!(len > 1e-6f))
	{
		memcpy(out, n, 3 * sizeof(float));
		return;
	}

	for (i = 0; i < 3; i++)
	{
		out[i] /= len;
	}
}

int
rm_gb_alias_extra(const unsigned short *map, long nv, int num_xyz, const float (*prev)[4],
		const unsigned char *verts, const unsigned char *overts, float backlerp, rm_gb_avx_t *out)
{
	long i;

	for (i = 0; i < nv; i++)
	{
		const int x = map[i];

		if (x >= num_xyz)
		{
			return 0;
		}

		out[i].prev[0] = prev[x][0];
		out[i].prev[1] = prev[x][1];
		out[i].prev[2] = prev[x][2];
		out[i].prev[3] = 1.0f;
		rm_gb_alias_normal(verts[4 * x + 3], overts[4 * x + 3], backlerp, out[i].normal);
		out[i].normal[3] = 0.0f;
	}

	return 1;
}
