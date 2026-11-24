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
 * The pure part of ref_metal's ray tracing, RT1 (TASK53): material ids,
 * the triangle ranges and lists the structures are built from, the
 * exclusion rules and the instance list of a refdef, the byte estimates
 * and the r_rt_stats lines. Neither SDL nor Metal; host-tested by
 * tests/metal/rm_rt_test.c (docs/plans/metal-raytracing-design.md,
 * section 1).
 *
 * =======================================================================
 */

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "header/rm_rt.h"
#include "common/mtl_matrix.h"

/* MD2's MAX_FRAMES */
#define RM_RT_MD2_MAX_FRAMES 512

_Static_assert(RM_RT_MAX_INSTANCES == 129, "the world and every entity of a refdef");

unsigned
rm_rt_material(int texflags)
{
	if (texflags & RM_TEX_SKY)
	{
		return RM_RT_MAT_SKY;
	}

	/* water before alpha: the reflections look for water, and stock water is translucent */
	if (texflags & RM_TEX_WARP)
	{
		return RM_RT_MAT_WATER;
	}

	if (texflags & (RM_TEX_TRANS33 | RM_TEX_TRANS66))
	{
		return RM_RT_MAT_ALPHA;
	}

	return RM_RT_MAT_WORLD;
}

int
rm_rt_wanted(float stats, int device_raytracing)
{
	if (!device_raytracing || !isfinite(stats))
	{
		return 0;
	}

	return stats != 0;
}

long
rm_rt_materials(const rm_surfrec_t *recs, long nrecs, unsigned long nindices, unsigned char *mat,
		unsigned long cap)
{
	unsigned long at = 0, tri = 0;
	long s;

	if ((nrecs < 0) || (nindices % 3) || (nindices / 3 > cap) || (nrecs && (!recs || !mat)))
	{
		return -1;
	}

	for (s = 0; s < nrecs; s++)
	{
		const rm_surfrec_t *r = &recs[s];
		unsigned long n = r->num_indices / 3, k;
		unsigned m;

		if ((r->first_index != at) || (r->num_indices % 3) || (r->num_indices > nindices - at))
		{
			return -1;
		}

		m = rm_rt_material((int)r->texflags);

		for (k = 0; k < n; k++)
		{
			mat[tri + k] = (unsigned char)m;
		}

		tri += n;
		at += r->num_indices;
	}

	return (at == nindices) ? (long)tri : -1;
}

int
rm_rt_range(unsigned long first_index, unsigned long num_indices, unsigned long nindices,
		unsigned long *first_tri, unsigned long *ntris)
{
	if ((num_indices == 0) || (first_index % 3) || (num_indices % 3) || (first_index > nindices) ||
		(num_indices > nindices - first_index))
	{
		return 0;
	}

	*first_tri = first_index / 3;
	*ntris = num_indices / 3;
	return 1;
}

void
rm_rt_count(const unsigned char *mat, unsigned long first_tri, unsigned long ntris, rm_rt_tris_t *t)
{
	unsigned long i;

	memset(t, 0, sizeof(*t));

	for (i = 0; i < ntris; i++)
	{
		const unsigned m = mat[first_tri + i];

		t->tris[(m < RM_RT_MATS) ? m : RM_RT_MAT_NONE]++;
	}

	t->total = ntris;
}

int
rm_rt_frames_bytes(long num_frames, long framesize, long vofs, long num_xyz, size_t cap, size_t *bytes)
{
	*bytes = 0;

	if ((num_frames < 1) || (num_frames > RM_RT_MD2_MAX_FRAMES) || (num_xyz < 1) || (num_xyz > RM_MD2_MAX_VERTS) ||
		(vofs < 0) || (framesize <= 0) || (vofs % 4) || (framesize % 4) || (vofs > framesize) ||
		((framesize - vofs) / 4 < num_xyz) || ((size_t)framesize > cap / (size_t)num_frames))
	{
		return 0;
	}

	*bytes = (size_t)framesize * (size_t)num_frames;
	return 1;
}

long
rm_rt_frame_vertex(long f, long num_frames, long framesize, long vofs)
{
	if ((f < 0) || (f >= num_frames))
	{
		return -1;
	}

	return (f * framesize + vofs) / 4;
}

long
rm_rt_alias_indices(const short *tris, long stride, long num_tris, long num_xyz, unsigned short *out,
		long cap)
{
	long t;
	int k;

	if (!tris || !out || (stride < 3) || (num_tris < 1) || (num_tris > RM_RT_MD2_MAX_TRIS) || (num_xyz < 1) ||
		(num_xyz > RM_MD2_MAX_VERTS) || (cap < 0) || (num_tris > cap / 3))
	{
		return 0;
	}

	for (t = 0; t < num_tris; t++)
	{
		const short *tri = tris + t * stride;

		for (k = 0; k < 3; k++)
		{
			if ((tri[k] < 0) || (tri[k] >= num_xyz))
			{
				return 0;
			}

			out[3 * t + k] = (unsigned short)tri[k];
		}
	}

	return num_tris;
}

void
rm_rt_transform(const float *origin, const float *angles, int brush, float *xform)
{
	float id[16], m[16], a[3];
	int c;

	/* the angles as gl1's R_DrawAliasModel and R_DrawBrushModel hand them to R_RotateForEntity */
	a[0] = -angles[0];
	a[1] = angles[1];
	a[2] = brush ? -angles[2] : angles[2];

	mtl_mat_identity(id);
	rm_entity_matrix(m, id, origin, a);

	/* the GL matrix is column-major 4 by 4; its upper three rows, column by column */
	for (c = 0; c < 4; c++)
	{
		xform[3 * c] = m[4 * c];
		xform[3 * c + 1] = m[4 * c + 1];
		xform[3 * c + 2] = m[4 * c + 2];
	}
}

int
rm_rt_instances(const rm_rt_ent_t *ents, long n, int world, int gunshadow, float hand, rm_rt_inst_t *out,
		int cap, rm_rt_excl_t *excl)
{
	int count = 0, i, ne = rm_entity_count(n);

	memset(excl, 0, sizeof(*excl));

	if (world && (count < cap))
	{
		static const float zero[3] = { 0, 0, 0 };

		out[count].kind = RM_RT_INST_WORLD;
		out[count].model = 0;
		out[count].ent = -1;
		out[count].carry = 0;
		out[count].noao = 0;
		rm_rt_transform(zero, zero, 1, out[count].xform);
		count++;
	}

	for (i = 0; i < ne && count < cap; i++)
	{
		const rm_rt_ent_t *e = &ents[i];

		if (e->flags & RM_RF_BEAM)
		{
			excl->beam++;
			continue;
		}

		if (e->kind == RM_RT_ENT_NULL)
		{
			excl->null++;
			continue;
		}

		if ((e->kind != RM_RT_ENT_ALIAS) && (e->kind != RM_RT_ENT_BRUSH))
		{
			excl->sprite++;
			continue;
		}

		if (e->flags & RM_RF_SHELLS)
		{
			excl->shell++;
			continue;
		}

		if ((e->flags & RM_RF_WEAPONMODEL) && (!gunshadow || !rm_alias_drawn(e->flags, hand)))
		{
			excl->weapon++;
			continue;
		}

		if (!rm_entity_floats_ok(e->origin, e->oldorigin, e->angles, e->backlerp, e->alpha))
		{
			excl->bad++;
			continue;
		}

		if (e->model <= 0)
		{
			excl->nomodel++;
			continue;
		}

		out[count].kind = (e->kind == RM_RT_ENT_BRUSH) ? RM_RT_INST_INLINE : RM_RT_INST_ALIAS;
		out[count].model = e->model;
		out[count].ent = i;
		out[count].carry = 0;
		out[count].noao = (e->flags & (RM_RF_WEAPONMODEL | RM_RT_RF_VIEWERMODEL)) ? 1 : 0;
		rm_rt_transform(e->origin, e->angles, e->kind == RM_RT_ENT_BRUSH, out[count].xform);
		count++;
	}

	return count;
}

size_t
rm_rt_instance_bytes(long n)
{
	return ((n < 0) || (n > RM_RT_MAX_INSTANCES)) ? 0 : (size_t)n * RM_RT_INSTANCE_BYTES;
}

size_t
rm_rt_avert_bytes(long num_xyz)
{
	return ((num_xyz < 0) || (num_xyz > RM_MD2_MAX_VERTS)) ? 0 : (size_t)num_xyz * RM_RT_AVERT_BYTES;
}

size_t
rm_rt_aindex_bytes(long num_tris)
{
	return ((num_tris < 0) || (num_tris > RM_RT_MD2_MAX_TRIS)) ? 0 : (size_t)num_tris * 3 * sizeof(unsigned short);
}

static int
rm_rt_cmp(const void *a, const void *b)
{
	const double x = *(const double *)a, y = *(const double *)b;

	return (x > y) - (x < y);
}

int
rm_rt_spread(double *ms, unsigned long n, rm_rt_spread_t *out)
{
	unsigned long rank;

	memset(out, 0, sizeof(*out));

	if (n == 0)
	{
		return 0;
	}

	qsort(ms, n, sizeof(*ms), rm_rt_cmp);
	out->n = n;
	out->min = ms[0];
	out->max = ms[n - 1];
	out->median = (n % 2) ? ms[n / 2] : (ms[n / 2 - 1] + ms[n / 2]) / 2;

	/* nearest rank: the smallest value with at least 95 % of the values at or below it */
	rank = (95 * n + 99) / 100;
	out->p95 = ms[(rank ? rank : 1) - 1];
	return 1;
}

/* appends to buf (len bytes, *used so far): 1, or 0 when it does not fit */
static int
rm_rt_append(char *buf, size_t len, size_t *used, const char *fmt, ...)
{
	va_list ap;
	int n;

	if (*used >= len)
	{
		return 0;
	}

	va_start(ap, fmt);
	n = vsnprintf(buf + *used, len - *used, fmt, ap);
	va_end(ap);

	if ((n < 0) || ((size_t)n >= len - *used))
	{
		return 0;
	}

	*used += (size_t)n;
	return 1;
}

static int
rm_rt_tris_format(char *buf, size_t len, size_t *used, const rm_rt_tris_t *t)
{
	return rm_rt_append(buf, len, used, "%lu triangles (world %lu, sky %lu, water %lu, alpha %lu)", t->total,
			t->tris[RM_RT_MAT_WORLD], t->tris[RM_RT_MAT_SKY], t->tris[RM_RT_MAT_WATER], t->tris[RM_RT_MAT_ALPHA]);
}

/* the line in buf when ok, else buf empty: the result */
static int
rm_rt_done(char *buf, size_t len, int ok)
{
	if (!ok && len)
	{
		buf[0] = '\0';
	}

	return ok;
}

int
rm_rt_map_format(const char *map, const rm_rt_map_stats_t *s, char *buf, size_t len)
{
	size_t used = 0;
	int ok;

	if (len == 0)
	{
		return 0;
	}

	buf[0] = '\0';
	ok = rm_rt_append(buf, len, &used, "ref_metal: rt: %s: world ", map) &&
		rm_rt_tris_format(buf, len, &used, &s->world) &&
		rm_rt_append(buf, len, &used, ", %zu bytes; %lu inline models, ", s->world_bytes, s->inline_models) &&
		rm_rt_tris_format(buf, len, &used, &s->inlines) &&
		rm_rt_append(buf, len, &used, ", %zu bytes; materials %zu bytes, build scratch %zu bytes; ",
			s->inline_bytes, s->material_bytes, s->scratch_bytes);

	if (ok)
	{
		ok = (s->build_ms >= 0) ? rm_rt_append(buf, len, &used, "built in %.3f ms GPU", s->build_ms) :
			rm_rt_append(buf, len, &used, "build time not measured");
	}

	return rm_rt_done(buf, len, ok);
}

int
rm_rt_cost_format(const double sum[4], const unsigned long n[4], char *buf, size_t len)
{
	double ms[4];
	size_t used = 0;
	int i, any = 0;

	if (len == 0)
	{
		return 0;
	}

	buf[0] = '\0';

	for (i = 0; i < 4; i++)
	{
		ms[i] = n[i] ? sum[i] / (double)n[i] : 0.0;
		any |= (n[i] != 0);
	}

	return rm_rt_done(buf, len, any && rm_rt_append(buf, len, &used, "shadows=%.3f sun=%.3f ao=%.3f water=%.3f",
			ms[0], ms[1], ms[2], ms[3]));
}

int
rm_rt_frame_format(const rm_rt_frame_stats_t *s, char *buf, size_t len)
{
	size_t used = 0;
	int ok;

	if (len == 0)
	{
		return 0;
	}

	buf[0] = '\0';
	ok = rm_rt_append(buf, len, &used, "ref_metal: rt: frame %lu: %lu instances (%lu inline, %lu alias), "
			"alias %lu triangles, %lu built, %lu refit, %lu skipped; ", s->frame, s->instances, s->inlines,
			s->aliases, s->alias_tris, s->built, s->refit, s->skipped) &&
		rm_rt_append(buf, len, &used, "left out: %lu beams, %lu null, %lu sprites, %lu weapon, %lu shells, "
			"%lu without a structure, %lu bad; ", s->excl.beam, s->excl.null, s->excl.sprite, s->excl.weapon,
			s->excl.shell, s->excl.nomodel, s->excl.bad) &&
		rm_rt_append(buf, len, &used, "bytes: alias structures %zu, vertices %zu, models %zu, instance "
			"structure %zu, scratch %zu; ", s->alias_bytes, s->vertex_bytes, s->model_bytes, s->ias_bytes,
			s->scratch_bytes);

	if (ok)
	{
		ok = ((s->alias_ms >= 0) && (s->ias_ms >= 0)) ?
			rm_rt_append(buf, len, &used, "gpu ms: alias %.3f, instances %.3f", s->alias_ms, s->ias_ms) :
			rm_rt_append(buf, len, &used, "gpu ms: not measured");
	}

	return rm_rt_done(buf, len, ok);
}

int
rm_rt_summary_format(const char *map, unsigned long frames, const rm_rt_spread_t *alias,
		const rm_rt_spread_t *ias, unsigned long busiest_aliases, unsigned long busiest_tris, char *buf, size_t len)
{
	size_t used = 0;
	int ok;

	if (len == 0)
	{
		return 0;
	}

	buf[0] = '\0';
	ok = rm_rt_append(buf, len, &used, "ref_metal: rt: %s: %lu frames; ", map, frames);

	if (ok && alias->n && ias->n)
	{
		ok = rm_rt_append(buf, len, &used, "gpu ms over %lu frames: alias lerp and refit min %.3f, median %.3f, "
				"p95 %.3f, max %.3f; instance rebuild min %.3f, median %.3f, p95 %.3f, max %.3f; ", alias->n,
				alias->min, alias->median, alias->p95, alias->max, ias->min, ias->median, ias->p95, ias->max);
	}
	else if (ok)
	{
		ok = rm_rt_append(buf, len, &used, "gpu ms not measured; ");
	}

	ok = ok && rm_rt_append(buf, len, &used, "busiest frame %lu alias instances, %lu alias triangles",
			busiest_aliases, busiest_tris);
	return rm_rt_done(buf, len, ok);
}
