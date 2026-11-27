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
 * The pure part of the RT-primary presentation's direct light (TASK188,
 * PM6 slice 1): header/rm_rtdir.h says what each function does;
 * tests/metal/rm_rtdir_test.c holds them.
 *
 * =======================================================================
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "header/rm_rtdir.h"
#include "header/rm_rtgate.h"
#include "header/rm_rtshadow.h"

int
rm_rtdir_on(int pres, float direct, int device_rt, float fullbright)
{
	if ((pres != RM_PRES_RTPRIMARY) || !device_rt || !isfinite(direct) || (direct == 0))
	{
		return 0;
	}

	/* r_fullbright draws the world without its light: any value but 0, NaN among them, as rm_surf.c reads it */
	return (fullbright != 0) ? 0 : 1;
}

int
rm_rtdir_show(float v)
{
	if (!isfinite(v) || !(v > 0))
	{
		return 0;
	}

	return (v >= RM_RTDIR_SHOW_MAX) ? RM_RTDIR_SHOW_MAX : (int)v;
}

long
rm_rtdir_pack(const rm_rtl_lights_t *l, rm_rtdir_light_t *out, long cap, rm_rtdir_pack_t *why)
{
	rm_rtdir_pack_t w;
	long i, n = 0;

	memset(&w, 0, sizeof(w));

	for (i = 0; l && (i < l->nlights) && (i < RM_RTL_MAX_LIGHTS); i++)
	{
		const rm_rtl_light_t *s = &l->light[i];
		rm_rtdir_light_t *d;
		int k;

		if (!isfinite(s->intensity) || !(s->intensity > 0))
		{
			w.dark++;
			continue;
		}

		if (s->spot && (s->target_state == RM_RTL_TARGET_MISSING))
		{
			w.untargeted++;
			continue;
		}

		if (n >= cap)
		{
			w.over++;
			continue;
		}

		d = &out[n++];

		for (k = 0; k < 3; k++)
		{
			d->pos[k] = s->origin[k];
			d->color[k] = s->color[k];
			d->spot[k] = s->spot ? s->dir[k] : 0.0f;
		}

		/* the range and the brightness held to RM_RTDIR_MAX_INTENSITY: past it a light reaches no further point */
		if (s->intensity > RM_RTDIR_MAX_INTENSITY)
		{
			w.clamped++;
		}

		d->pos[3] = (s->intensity > RM_RTDIR_MAX_INTENSITY) ? RM_RTDIR_MAX_INTENSITY : s->intensity;
		d->color[3] = (float)(((s->style >= 0) && (s->style < RM_RTL_STYLES)) ? s->style : 0);
		d->spot[3] = s->spot ? (float)cos((double)s->cone * M_PI / 180.0) : MTL_RTDIR_NOSPOT;
	}

	w.kept = n;

	if (why)
	{
		*why = w;
	}

	return n;
}

void
rm_rtdir_reflectivity(const float *mean, int read, float *out)
{
	float big = 0.0f;
	int k;

	for (k = 0; k < 3; k++)
	{
		const float v = read ? mean[k] : RM_RTDIR_EMIT_UNREAD;

		out[k] = (isfinite(v) && (v > 0.0f)) ? v : 0.0f;
		big = (out[k] > big) ? out[k] : big;
	}

	/* qrad3 scales the reflectivity up, "because the textures are so dim": doubled when its largest channel is under 0.5 */
	if (RM_RTDIR_EMIT_QRAD3_COLOUR && read && (big < 0.5f))
	{
		for (k = 0; k < 3; k++)
		{
			out[k] *= 2.0f;
		}
	}
}

/* qrad3's patch light in light, its largest channel in *big: 1 when that is at least RM_RTDIR_DIRECT_LIGHT */
static int
rm_rtdir_patch_light_d(int value, const float *refl, double *light, double *big)
{
	double rmax = 0;
	int k;

	for (k = 0; k < 3; k++)
	{
		const double r = (refl && isfinite(refl[k]) && (refl[k] > 0)) ? refl[k] : 0.0;

		rmax = (r > rmax) ? r : rmax;
		light[k] = r;
	}

	*big = 0;

	for (k = 0; k < 3; k++)
	{
		light[k] = ((rmax > 0) && (value > 0)) ? (double)value * light[k] * light[k] / rmax : 0.0;
		*big = (light[k] > *big) ? light[k] : *big;
	}

	return isfinite(*big) && (*big >= RM_RTDIR_DIRECT_LIGHT);
}

int
rm_rtdir_patch_light(int value, const float *refl, float *rgb)
{
	double light[3], big;
	const int ok = rm_rtdir_patch_light_d(value, refl, light, &big);
	int k;

	for (k = 0; k < 3; k++)
	{
		rgb[k] = ok ? (float)light[k] : 0.0f;
	}

	return ok;
}

int
rm_rtdir_emitter(const rm_rtdir_face_t *f, int value, const float *refl, rm_rtdir_light_t *out, int *clamped)
{
	double c[3] = { 0, 0, 0 }, area2[3] = { 0, 0, 0 }, area, nlen, light[3], big, intensity;
	const float *p0;
	int i, k;

	if (clamped)
	{
		*clamped = 0;
	}

	if (!f || !f->pts || (f->n < 3) || (f->n > RM_RTDIR_EMIT_MAXPTS) || (f->stride < 3) || (value <= 0))
	{
		return RM_RTDIR_EMIT_SHAPELESS;
	}

	nlen = sqrt((double)f->normal[0] * f->normal[0] + (double)f->normal[1] * f->normal[1] +
			(double)f->normal[2] * f->normal[2]);

	if (!isfinite(nlen) || (fabs(nlen - 1.0) > 0.01))
	{
		return RM_RTDIR_EMIT_SHAPELESS;
	}

	/* WindingCenter, the points' mean, and WindingArea, the fan's triangles */
	p0 = f->pts;

	for (i = 0; i < f->n; i++)
	{
		const float *p = f->pts + (size_t)i * (size_t)f->stride;

		for (k = 0; k < 3; k++)
		{
			c[k] += p[k];
		}

		if (i >= 2)
		{
			const float *q = f->pts + (size_t)(i - 1) * (size_t)f->stride;
			const double a[3] = { (double)q[0] - p0[0], (double)q[1] - p0[1], (double)q[2] - p0[2] };
			const double b[3] = { (double)p[0] - p0[0], (double)p[1] - p0[1], (double)p[2] - p0[2] };

			area2[0] += a[1] * b[2] - a[2] * b[1];
			area2[1] += a[2] * b[0] - a[0] * b[2];
			area2[2] += a[0] * b[1] - a[1] * b[0];
		}
	}

	area = 0.5 * sqrt(area2[0] * area2[0] + area2[1] * area2[1] + area2[2] * area2[2]);

	for (k = 0; k < 3; k++)
	{
		c[k] = c[k] / f->n + RM_RTDIR_EMIT_OFFSET * f->normal[k];

		if (!isfinite(c[k]))
		{
			return RM_RTDIR_EMIT_SHAPELESS;
		}
	}

	if (!isfinite(area) || !(area > 0))
	{
		return RM_RTDIR_EMIT_SHAPELESS;
	}

	/* the patch's light: BaseLightForFace's refl x value, times the normalized reflectivity (MakePatchForFace) */
	if (!rm_rtdir_patch_light_d(value, refl, light, &big))
	{
		return RM_RTDIR_EMIT_DIM;
	}

	/* CreateDirectLights: ColorNormalize of the light, its largest channel times the area times direct_scale */
	intensity = big * area * RM_RTDIR_DIRECT_SCALE;

	if (!isfinite(intensity) || (intensity > RM_RTDIR_MAX_EMIT_INTENSITY))
	{
		intensity = RM_RTDIR_MAX_EMIT_INTENSITY;

		if (clamped)
		{
			*clamped = 1;
		}
	}

	for (k = 0; k < 3; k++)
	{
		out->pos[k] = (float)c[k];
		out->color[k] = (float)(light[k] / big);
		out->spot[k] = f->normal[k];
	}

	out->pos[3] = (float)intensity;
	out->color[3] = 0.0f;
	out->spot[3] = MTL_RTDIR_EMITTER;
	return RM_RTDIR_EMIT_KEPT;
}

long
rm_rtdir_pack_emitters(const rm_rtl_emitters_t *e, rm_rtdir_face_fn face, void *ctx, rm_rtdir_light_t *out,
		long cap, rm_rtdir_emitpack_t *why)
{
	rm_rtdir_emitpack_t w;
	long i, n = 0;

	memset(&w, 0, sizeof(w));

	for (i = 0; e && face && (i < e->n) && (i < RM_RTL_MAX_EMITTERS); i++)
	{
		static const float grey0[3] = { 0, 0, 0 };
		const rm_rtl_emit_t *s = &e->emit[i];
		const rm_rtl_tex_t *t = ((s->tex >= 0) && (s->tex < e->ntex) && (s->tex < RM_RTL_MAX_TEXTURES)) ?
			&e->tex[s->tex] : NULL;
		rm_rtdir_face_t f;
		rm_rtdir_light_t l;
		float refl[3];
		int r, cl;

		if (!t || !t->read)
		{
			w.unread++;
		}

		if (!face(ctx, s->face, &f))
		{
			w.bmodel++;
			continue;
		}

		rm_rtdir_reflectivity(t ? t->mean : grey0, t ? t->read : 0, refl);
		r = rm_rtdir_emitter(&f, s->value, refl, &l, &cl);

		if (r == RM_RTDIR_EMIT_SHAPELESS)
		{
			w.shapeless++;
			continue;
		}

		if (r == RM_RTDIR_EMIT_DIM)
		{
			w.dim++;
			continue;
		}

		if (n >= cap)
		{
			w.over++;
			continue;
		}

		w.clamped += cl;
		out[n++] = l;
	}

	w.kept = n;

	if (why)
	{
		*why = w;
	}

	return n;
}

long
rm_rtdir_self_table(const rm_rtl_emitters_t *e, rm_rtdir_face_fn face, void *ctx, const rm_surfrec_t *recs,
		long nrecs, unsigned *out, unsigned long ntris)
{
	long i, marked = 0;

	if (!out || (ntris == 0))
	{
		return -1;
	}

	memset(out, 0, ntris * sizeof(*out));

	for (i = 0; e && face && recs && (i < e->n) && (i < RM_RTL_MAX_EMITTERS); i++)
	{
		static const float grey0[3] = { 0, 0, 0 };
		const rm_rtl_emit_t *s = &e->emit[i];
		const rm_rtl_tex_t *t = ((s->tex >= 0) && (s->tex < e->ntex) && (s->tex < RM_RTL_MAX_TEXTURES)) ?
			&e->tex[s->tex] : NULL;
		rm_rtdir_face_t f;
		float refl[3], rgb[3];
		unsigned long first, n, k;
		unsigned w;

		if ((s->face < 0) || (s->face >= nrecs) || !face(ctx, s->face, &f))
		{
			continue;
		}

		rm_rtdir_reflectivity(t ? t->mean : grey0, t ? t->read : 0, refl);

		if (!rm_rtdir_patch_light(s->value, refl, rgb))
		{
			continue;
		}

		/* the face's triangles: whole, inside the buffer */
		first = recs[s->face].first_index;
		n = recs[s->face].num_indices;

		if ((first % 3) || (n % 3) || (first / 3 > ntris) || (n / 3 > ntris - first / 3))
		{
			continue;
		}

		w = mtl_rtdir_self_pack(rgb[0], rgb[1], rgb[2]);

		for (k = 0; k < n / 3; k++)
		{
			out[first / 3 + k] = w;
		}

		marked++;
	}

	return marked;
}

int
rm_rtdir_dlight(const float *origin, const float *color, float intensity, rm_rtdir_dlight_t *out)
{
	int k;

	if (!origin || !color || !out || !rm_dlight_ok(origin, color, intensity))
	{
		return 0;
	}

	for (k = 0; k < 3; k++)
	{
		out->pos[k] = origin[k];
		out->color[k] = color[k];
	}

	out->pos[3] = intensity;
	out->color[3] = 0.0f;
	return 1;
}

void
rm_rtdir_styles(const float (*styles)[3], int n, float modulate, float *out)
{
	int s, k;

	for (s = 0; s < RM_RTL_STYLES; s++)
	{
		for (k = 0; k < 3; k++)
		{
			const float v = (styles && (s < n)) ? styles[s][k] * modulate : modulate;

			out[3 * s + k] = isfinite(v) ? v : 0.0f;
		}
	}
}

float
rm_rtdir_fit(unsigned long lm_sum, unsigned long raw_sum, unsigned long samples, float prev)
{
	double e;

	if (!samples || !raw_sum)
	{
		return prev;
	}

	e = (double)lm_sum / (double)raw_sum;

	if (e < RM_RTDIR_FIT_MIN)
	{
		return RM_RTDIR_FIT_MIN;
	}

	return (e > RM_RTDIR_FIT_MAX) ? RM_RTDIR_FIT_MAX : (float)e;
}

int
rm_rtdir_fit_enters(const float *lm)
{
	float most = 0.0f;
	int k;

	for (k = 0; k < 3; k++)
	{
		if (!isfinite(lm[k]) || (lm[k] >= RM_RTDIR_FIT_CLIP))
		{
			return 0;
		}

		most = (lm[k] > most) ? lm[k] : most;
	}

	return most >= RM_RTDIR_FIT_DARK;
}

void
rm_rtdir_fit_light(const rm_rtdir_light_t *l, long n, const float *p, const float *nrm, rm_rtdir_clear_fn clear,
		void *ctx, float *out, unsigned long *rays)
{
	mtl_rtdir_keep_t k;
	float o[3];
	unsigned seen = 0, j;
	long i;
	int c;

	out[0] = out[1] = out[2] = 0.0f;
	k.n = 0;
	k.capped = 0;

	for (c = 0; c < 3; c++)
	{
		o[c] = p[c] + nrm[c] * RM_RTDIR_FIT_BIAS;
	}

	/* the kernel's first loop: the candidates among the lights in range and facing */
	for (i = 0; l && (i < n); i++)
	{
		const float dv[3] = { l[i].pos[0] - p[0], l[i].pos[1] - p[1], l[i].pos[2] - p[2] };
		const float dist = sqrtf(dv[0] * dv[0] + dv[1] * dv[1] + dv[2] * dv[2]);
		float s, nl = 0.0f, sd = 0.0f;

		if (!mtl_rtdir_reach(l[i].pos[3], dist, l[i].spot[3]))
		{
			continue;
		}

		for (c = 0; c < 3; c++)
		{
			nl += nrm[c] * dv[c] / dist;
			sd -= dv[c] / dist * l[i].spot[c];
		}

		s = mtl_rtdir_contrib(l[i].pos[3], dist, nl, sd, l[i].spot[3]);

		if (s > 0.0f)
		{
			k = mtl_rtdir_keep(k, (unsigned)i, s);
		}
	}

	k = mtl_rtdir_keep_rank(k);

	/* its second: strongest first, one line each, until MTL_RTDIR_MAX_TRACED clear */
	for (j = 0; mtl_rtdir_walk(j, k.n, seen); j++)
	{
		const rm_rtdir_light_t *li = &l[k.i[j]];
		const float to[3] = { li->pos[0] - o[0], li->pos[1] - o[1], li->pos[2] - o[2] };
		const float len = sqrtf(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
		float end[3];

		for (c = 0; c < 3; c++)
		{
			end[c] = (len > 0.0f) ? o[c] + to[c] * ((len - 1.0f) / len) : o[c];
		}

		if (rays)
		{
			(*rays)++;
		}

		if ((len > 1.0f) && !clear(ctx, o, end))
		{
			continue;
		}

		seen++;

		for (c = 0; c < 3; c++)
		{
			out[c] += k.s[j] * li->color[c];
		}
	}
}

float
rm_rtdir_fit_map(const rm_rtdir_light_t *l, long n, const rm_rtdir_fitpt_t *pts, long npts, rm_rtdir_clear_fn clear,
		void *ctx, rm_rtdir_mapfit_t *out)
{
	rm_rtdir_mapfit_t f;
	long i;

	memset(&f, 0, sizeof(f));

	for (i = 0; pts && clear && (i < npts); i++)
	{
		const float *lm = pts[i].lm;
		float raw[3];

		if (!rm_rtdir_fit_enters(lm))
		{
			int clipped = 0, c;

			for (c = 0; c < 3; c++)
			{
				clipped |= isfinite(lm[c]) && (lm[c] >= RM_RTDIR_FIT_CLIP);
			}

			if (clipped)
			{
				f.clipped++;
			}
			else
			{
				f.dark++;
			}

			continue;
		}

		rm_rtdir_fit_light(l, n, pts[i].pos, pts[i].normal, clear, ctx, raw, &f.rays);
		f.lm += mtl_rtdir_q(lm[0] * MTL_RTDIR_UNIT, lm[1] * MTL_RTDIR_UNIT, lm[2] * MTL_RTDIR_UNIT);
		f.raw += mtl_rtdir_q(raw[0] * MTL_RTDIR_UNIT, raw[1] * MTL_RTDIR_UNIT, raw[2] * MTL_RTDIR_UNIT);
		f.n++;
	}

	f.exposure = rm_rtdir_fit(f.lm, f.raw, f.n, 1.0f);

	if (out)
	{
		*out = f;
	}

	return f.exposure;
}

void
rm_rtdir_fit_luxel(const unsigned char *samples, int nummaps, int smax, int tmax, float s, float t, float *out)
{
	const float half = 0.5f;
	int i = 0, j = 0, m, c;

	out[0] = out[1] = out[2] = 0.0f;

	if (!samples || (nummaps < 1) || (smax < 1) || (tmax < 1))
	{
		return;
	}

	/* the nearest luxel, held to the face; a coordinate that is not a number reads luxel 0 */
	if (s + half > 0.0f)
	{
		i = (s + half < (float)smax) ? (int)(s + half) : smax - 1;
	}

	if (t + half > 0.0f)
	{
		j = (t + half < (float)tmax) ? (int)(t + half) : tmax - 1;
	}

	for (m = 0; m < nummaps; m++)
	{
		const unsigned char *x = samples + ((size_t)m * smax * tmax + (size_t)j * smax + i) * 3;

		for (c = 0; c < 3; c++)
		{
			out[c] += x[c];
		}
	}
}

void
rm_rtdir_fit_st(const float *pos, const float vecs[2][4], const short *texturemins, float *st)
{
	int a;

	for (a = 0; a < 2; a++)
	{
		st[a] = (pos[0] * vecs[a][0] + pos[1] * vecs[a][1] + pos[2] * vecs[a][2] + vecs[a][3] - texturemins[a]) / 16.0f;
	}
}

int
rm_rtdir_fit_face_ok(int flags, int has_samples)
{
	return has_samples &&
		!(flags & (RM_RTDIR_SURF_LIGHT | RM_RTDIR_SURF_TRANS33 | RM_RTDIR_SURF_TRANS66 | RM_RTDIR_SURF_SKY | RM_RTDIR_SURF_WARP));
}

int
rm_rtdir_lights_current(const void *last, const char *last_name, int last_lmtex, const void *world, const char *name,
		int lmtex)
{
	(void)last_lmtex;
	(void)lmtex;

	return last_name && name && (last == world) && !strcmp(last_name, name);
}

long
rm_rtdir_fit_every(long total, long cap)
{
	if ((total <= cap) || (cap < 1))
	{
		return 1;
	}

	return 1 + (total - 1) / cap;
}

/* rm_rtdir_tree_clear's walk of the tree under node, depth levels down */
static int
rm_rtdir_clear_node(rm_rtdir_tree_t *t, const void *node, const float *a, const float *b, int depth)
{
	rm_rtdir_node_t nd;
	float front, back, frac, mid[3];
	int side, k;

	if (!node || (depth > RM_RTDIR_FIT_DEPTH))
	{
		return 0;
	}

	if (t->visits >= t->cap)
	{
		t->over = 1;
		return 0;
	}

	t->visits++;
	t->read(node, &nd);

	if (nd.leaf)
	{
		return !(nd.contents & RM_RTDIR_CONTENTS_SOLID);
	}

	front = a[0] * nd.normal[0] + a[1] * nd.normal[1] + a[2] * nd.normal[2] - nd.dist;
	back = b[0] * nd.normal[0] + b[1] * nd.normal[1] + b[2] * nd.normal[2] - nd.dist;
	side = front < 0;

	if ((back < 0) == side)
	{
		return rm_rtdir_clear_node(t, nd.child[side], a, b, depth + 1);
	}

	frac = front / (front - back);

	for (k = 0; k < 3; k++)
	{
		mid[k] = a[k] + (b[k] - a[k]) * frac;
	}

	return rm_rtdir_clear_node(t, nd.child[side], a, mid, depth + 1) &&
		rm_rtdir_clear_node(t, nd.child[!side], mid, b, depth + 1);
}

int
rm_rtdir_tree_clear(void *ctx, const float *a, const float *b)
{
	rm_rtdir_tree_t *t = ctx;

	if (!t || !t->read || t->over)
	{
		return 0;
	}

	return rm_rtdir_clear_node(t, t->root, a, b, 0);
}

float
rm_rtdir_fit_tree(const rm_rtdir_light_t *l, long n, const rm_rtdir_fitpt_t *pts, long npts, rm_rtdir_tree_t *tree,
		rm_rtdir_mapfit_t *out)
{
	rm_rtdir_mapfit_t f;

	f.exposure = rm_rtdir_fit_map(l, n, pts, npts, tree ? rm_rtdir_tree_clear : NULL, tree, &f);
	f.visits = tree ? tree->visits : 0;
	f.over = tree && tree->over;

	if (f.over)
	{
		f.exposure = 1.0f;
	}

	if (out)
	{
		*out = f;
	}

	return f.exposure;
}

float
rm_rtdir_exposure(float cvar, float fit, int *fixed)
{
	const int f = isfinite(cvar) && (cvar > 0);

	if (fixed)
	{
		*fixed = f;
	}

	if (f)
	{
		return (cvar > RM_RTDIR_EXPOSURE_MAX) ? RM_RTDIR_EXPOSURE_MAX : cvar;
	}

	return fit;
}

int
rm_rtdir_view(const float *proj, const float *world, const rm_viewport_t *vp, const float *eye, float znear,
		float exposure, unsigned nlights, unsigned show, rm_rtdir_view_t *out)
{
	rm_rts_view_t s;

	memset(out, 0, sizeof(*out));

	/* the shadows' view: the same inverse, viewport, eye and depth bias, so the passes find the same points */
	if (!rm_rts_view(proj, world, vp, eye, znear, &s))
	{
		return 0;
	}

	memcpy(out->inv, s.inv, sizeof(out->inv));
	memcpy(out->vp, s.vp, sizeof(out->vp));
	memcpy(out->eye, s.eye, sizeof(out->eye));
	out->bias_k = s.bias_k;
	out->exposure = exposure;
	out->nlights = nlights;
	out->show = show;
	return 1;
}

float
rm_rtdir_sky_scale(float v)
{
	if (!isfinite(v) || !(v > 0))
	{
		return 0;
	}

	return (v < MTL_RTSKY_SCALE_MAX) ? v : MTL_RTSKY_SCALE_MAX;
}

/* a channel of a term's colour: 0 for a value that is not finite or below 0 */
static float
rtdir_channel(float c)
{
	return (isfinite(c) && (c > 0)) ? c : 0;
}

void
rm_rtdir_view_open(rm_rtdir_view_t *v, const float *sky, float sky_scale, const float *sun_dir, const float *sun,
		unsigned frame)
{
	const float k = rm_rtdir_sky_scale(sky_scale);
	int i, dir_ok = sun_dir != NULL;

	for (i = 0; dir_ok && (i < 3); i++)
	{
		dir_ok = isfinite(sun_dir[i]);
	}

	for (i = 0; i < 3; i++)
	{
		v->sky[i] = sky ? rtdir_channel(rtdir_channel(sky[i]) * k) : 0;
		v->sun[i] = (sun && dir_ok) ? rtdir_channel(sun[i]) : 0;
		v->sun_dir[i] = dir_ok ? sun_dir[i] : 0;
	}

	v->sky[3] = v->sun[3] = v->sun_dir[3] = 0;
	v->frame = frame;
}

void
rm_rtdir_view_bounce(rm_rtdir_view_t *v, float scale)
{
	v->bounce[0] = rm_rtdir_bounce_scale(scale);
	v->bounce[1] = v->bounce[2] = v->bounce[3] = 0;
}

/* snprintf into buf: 1 when it fits, else 0 with buf empty */
static int
rtdir_put(int n, char *buf, size_t len)
{
	if ((n < 0) || ((size_t)n >= len))
	{
		buf[0] = '\0';
		return 0;
	}

	return 1;
}

int
rm_rtdir_stats_format(const rm_rtdir_stats_t *s, int nlights, int fixed, char *buf, size_t len)
{
	char ms[32];

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

	return rtdir_put(snprintf(buf, len, "ref_metal: rt direct: frame %lu: %lu pixels, %lu world, %d lights, "
			"%llu in range, %llu shadow rays (%.2f a world pixel), %llu lit, %lu pixels past the bound (%d rays, %d lights), "
			"fit %lu/%lu over %lu samples, exposure %.4f (%s), %s ms", s->frame, s->pixels, s->world, nlights,
			s->inrange, s->rays, s->world ? (double)s->rays / (double)s->world : 0.0, s->lit, s->capped,
			MTL_RTDIR_MAX_RAYS, MTL_RTDIR_MAX_TRACED, s->fit_lm, s->fit_raw, s->fit_n, (double)s->exposure,
			fixed ? "r_rt_direct_exposure" : "fit", ms), buf, len);
}

int
rm_rtdir_open_format(const rm_rtdir_stats_t *s, char *buf, size_t len)
{
	if (!len)
	{
		return 0;
	}

	return rtdir_put(snprintf(buf, len, "ref_metal: rt direct sky: frame %lu: %lu world, %lu sky rays (%lu reached the sky), "
			"%lu sun rays (%lu lit), %lu bounce rays (%lu read a lightmap), at most %d rays a pixel", s->frame, s->world,
			s->sky_rays, s->sky_hits, s->sun_rays, s->sun_lit, s->bounce_rays, s->bounce_hits, (int)MTL_RTSKY_PIXEL_RAYS),
			buf, len);
}

float
rm_rtdir_bounce_scale(float v)
{
	if (!isfinite(v) || !(v > 0))
	{
		return 0;
	}

	return (v < MTL_RTSKY_BOUNCE_MAX) ? v : MTL_RTSKY_BOUNCE_MAX;
}

/* an albedo channel as a record's byte: 0 for a value that is not finite or below 0, 255 at 1 and above */
static unsigned
rtdir_albedo_byte(float a)
{
	if (!isfinite(a) || !(a > 0))
	{
		return 0;
	}

	return (a >= 1) ? 255u : (unsigned)(a * 255.0f + 0.5f);
}

long
rm_rtdir_bounce_table(const rm_surfrec_t *recs, long nrecs, const float (*albedo)[3], unsigned *out,
		unsigned long ntris)
{
	const unsigned none = mtl_rtsky_brec_pack(0, 0, 0, MTL_RTSKY_NOLM);
	unsigned long k, lit = 0;
	long s;

	if (!out || (ntris == 0))
	{
		return -1;
	}

	for (k = 0; k < ntris; k++)
	{
		out[k] = none;
	}

	for (s = 0; recs && albedo && (s < nrecs); s++)
	{
		const rm_surfrec_t *r = &recs[s];
		const unsigned long first = r->first_index, n = r->num_indices;
		const int nolm = (r->texflags & (RM_TEX_SKY | RM_TEX_WARP | RM_TEX_TRANS33 | RM_TEX_TRANS66)) ||
			(r->lm_slice < 0) || (r->lm_slice >= (int)MTL_RTSKY_NOLM);
		unsigned w;

		/* the face's triangles: whole, inside the buffer */
		if ((first % 3) || (n % 3) || (first / 3 > ntris) || (n / 3 > ntris - first / 3))
		{
			continue;
		}

		w = mtl_rtsky_brec_pack(rtdir_albedo_byte(albedo[s][0]), rtdir_albedo_byte(albedo[s][1]),
				rtdir_albedo_byte(albedo[s][2]), nolm ? MTL_RTSKY_NOLM : (unsigned)r->lm_slice);

		for (k = 0; k < n / 3; k++)
		{
			out[first / 3 + k] = w;
		}

		lit += nolm ? 0 : n / 3;
	}

	return (long)lit;
}

int
rm_rtdir_map_format(const char *map, const rm_rtdir_pack_t *p, char *buf, size_t len)
{
	if (!len)
	{
		return 0;
	}

	return rtdir_put(snprintf(buf, len, "ref_metal: rt direct: %s: %ld lights uploaded (%ld dark, %ld spotlights "
			"without a target, %ld past the cap left out, %ld brighter than %d held to it)", map ? map : "?", p->kept,
			p->dark, p->untargeted, p->over, p->clamped, (int)RM_RTDIR_MAX_INTENSITY), buf, len);
}

int
rm_rtdir_emit_format(const char *map, const rm_rtdir_emitpack_t *p, char *buf, size_t len)
{
	if (!len)
	{
		return 0;
	}

	return rtdir_put(snprintf(buf, len, "ref_metal: rt direct: %s: %ld emissive faces uploaded (%ld not the world's, "
			"%ld shapeless, %ld below qrad3's direct light, %ld of an unread texture, %ld past the cap of %d left out, "
			"%ld held to the intensity cap)", map ? map : "?", p->kept, p->bmodel, p->shapeless, p->dim, p->unread,
			p->over, RM_RTDIR_MAX_EMITTERS, p->clamped), buf, len);
}
