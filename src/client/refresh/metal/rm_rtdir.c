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

int
rm_rtdir_emitter(const rm_rtdir_face_t *f, int value, const float *refl, rm_rtdir_light_t *out, int *clamped)
{
	double c[3] = { 0, 0, 0 }, area2[3] = { 0, 0, 0 }, area, nlen, light[3], big, rmax = 0, intensity;
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
	for (k = 0; k < 3; k++)
	{
		const double r = (refl && isfinite(refl[k]) && (refl[k] > 0)) ? refl[k] : 0.0;

		rmax = (r > rmax) ? r : rmax;
		light[k] = r;
	}

	big = 0;

	for (k = 0; k < 3; k++)
	{
		light[k] = (rmax > 0) ? (double)value * light[k] * light[k] / rmax : 0.0;
		big = (light[k] > big) ? light[k] : big;
	}

	if (!isfinite(big) || !(big >= RM_RTDIR_DIRECT_LIGHT))
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

float
rm_rtdir_fit_lag(const rm_rtdir_stats_t *lagged, unsigned long frame, unsigned long reset, float prev)
{
	/* only the frame exactly the lag behind, of this map: a slot whose last frame did not trace holds an older one */
	if (!lagged || (frame <= RM_RTDIR_FIT_LAG) || (lagged->frame != frame - RM_RTDIR_FIT_LAG) ||
		(lagged->frame <= reset))
	{
		return prev;
	}

	return rm_rtdir_fit(lagged->fit_lm, lagged->fit_raw, lagged->fit_n, prev);
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
