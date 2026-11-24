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

		d->pos[3] = s->intensity;
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
			"%lu in range, %lu shadow rays (%.2f a world pixel), %lu lit, fit %lu/%lu over %lu samples, "
			"exposure %.4f (%s), %s ms", s->frame, s->pixels, s->world, nlights, s->inrange, s->rays,
			s->world ? (double)s->rays / (double)s->world : 0.0, s->lit, s->fit_lm, s->fit_raw, s->fit_n,
			(double)s->exposure, fixed ? "r_rt_direct_exposure" : "fit", ms), buf, len);
}

int
rm_rtdir_map_format(const char *map, const rm_rtdir_pack_t *p, char *buf, size_t len)
{
	if (!len)
	{
		return 0;
	}

	return rtdir_put(snprintf(buf, len, "ref_metal: rt direct: %s: %ld lights uploaded (%ld dark, %ld spotlights "
			"without a target, %ld past the cap left out)", map ? map : "?", p->kept, p->dark, p->untargeted, p->over),
			buf, len);
}
