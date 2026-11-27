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
 * The pure part of ref_metal's ray-traced water reflections (RT6, TASK58;
 * header/rm_rtwater.h): when a frame traces them, the cvars as the pass
 * takes them, the stock turbulence table the kernel tilts the normal by,
 * the view the kernel unprojects and projects with, and the stats line.
 * No SDL, no Metal: tests/metal/rm_rtwater_test.c runs it on the host.
 *
 * =======================================================================
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "header/rm_rtwater.h"
#include "header/rm_rtshadow.h"

int
rm_rtw_on(float on, int device_rt, unsigned long water_tris)
{
	if (!device_rt || !isfinite(on) || (on == 0))
	{
		return 0;
	}

	/* a map without water has nothing to reflect: the view stays on the stock path */
	return water_tris > 0;
}

float
rm_rtw_amp(float v)
{
	if (!isfinite(v))
	{
		return RM_RTW_NORMAL;
	}

	return (v < 0) ? 0.0f : (v > RM_RTW_NORMAL_MAX) ? RM_RTW_NORMAL_MAX : v;
}

float
rm_rtw_reflect(float v)
{
	if (!isfinite(v))
	{
		return RM_RTW_REFLECT;
	}

	return (v < 0) ? 0.0f : (v > 1) ? 1.0f : v;
}

int
rm_rtw_show(float v)
{
	if (!isfinite(v) || !(v > 0))
	{
		return 0;
	}

	return (v >= RM_RTW_SHOW_MAX) ? RM_RTW_SHOW_MAX : (int)v;
}

int
rm_rtw_hits(float v)
{
	return isfinite(v) && (v >= 1);
}

int
rm_rtw_path(float hits, int ws_ok, int *told, int *say)
{
	*say = 0;

	if (!rm_rtw_hits(hits))
	{
		return 0;
	}

	if (ws_ok)
	{
		return 1;
	}

	if (!*told)
	{
		*told = 1;
		*say = 1;
	}

	return 0;
}

int
rm_rtw_turb_table(float *out)
{
	float m = 0;
	int i;

	for (i = 0; i < MTL_RTW_TURB_SIZE; i++)
	{
		const float v = fabsf(rm_turb_sin(i));

		m = (v > m) ? v : m;
	}

	if (!(m > 0))
	{
		return 0;
	}

	for (i = 0; i < MTL_RTW_TURB_SIZE; i++)
	{
		out[i] = rm_turb_sin(i) / m;
	}

	return 1;
}

int
rm_rtw_view(const float *proj, const float *world, const rm_viewport_t *vp, const float *eye, float znear,
		float time, float amp, float reflect, unsigned show, rm_rtw_view_t *out)
{
	rm_rts_view_t s;
	int c, r, k;

	memset(out, 0, sizeof(*out));

	/* the shadows' view: the same inverse, viewport, eye and depth bias, so every pass finds the same points */
	if (!rm_rts_view(proj, world, vp, eye, znear, &s))
	{
		return 0;
	}

	memcpy(out->inv, s.inv, sizeof(out->inv));
	memcpy(out->vp, s.vp, sizeof(out->vp));
	memcpy(out->eye, s.eye, sizeof(out->eye));

	/* proj x world, column-major, in double: the hits are projected with the matrix inv inverts */
	for (c = 0; c < 4; c++)
	{
		for (r = 0; r < 4; r++)
		{
			double sum = 0;

			for (k = 0; k < 4; k++)
			{
				sum += (double)proj[k * 4 + r] * (double)world[c * 4 + k];
			}

			out->mvp[c * 4 + r] = (float)sum;
		}
	}

	out->bias_k = s.bias_k;
	out->time = isfinite(time) ? time : 0.0f;
	out->amp = amp;
	out->reflect = reflect;
	out->show = show;
	return 1;
}

void
rm_rtw_shading(const rm_rtw_shading_t *sh, const float *proj, rm_rtw_view_t *out)
{
	const float h = out->vp[3];
	int i;

	out->env = sh->env;
	out->scale = isfinite(sh->scale) ? sh->scale : 1.0f;
	out->flow = isfinite(sh->flow) ? sh->flow : 0.0f;
	out->fullbright = sh->fullbright != 0;
	out->nlights = (sh->nlights < 0) ? 0 : (sh->nlights > MTL_RTS_MAX_LIGHTS) ? MTL_RTS_MAX_LIGHTS :
			(unsigned)sh->nlights;
	out->sky = sh->sky != 0;

	for (i = 0; i < 4; i++)
	{
		out->warp[i] = sh->warp[i];
		out->trans[i] = sh->trans[i];
	}

	memcpy(out->sky_rows, sh->sky_rows, sizeof(out->sky_rows));

	/* the side of a pixel one unit from the eye: the view's height spans 2 / proj[5] there */
	out->lod_k = 0.0f;

	if (isfinite(h) && (h > 0) && isfinite(proj[5]) && (proj[5] > 0))
	{
		out->lod_k = 2.0f / (h * proj[5]);
	}
}

void
rm_rtw_sky_rows(float time, float rotate, const float *axis, float *rows)
{
	static const float id[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	const float o[3] = { 0, 0, 0 };
	float m[16];
	int r, c;

	memcpy(m, id, sizeof(m));

	/* rm_sky_matrix about the origin: the box's rotation, the shim's glRotatef (a zero axis leaves it) */
	if (isfinite(time) && isfinite(rotate) && (rotate != 0) && isfinite(axis[0]) && isfinite(axis[1]) &&
		isfinite(axis[2]))
	{
		rm_sky_matrix(m, id, o, time, rotate, axis);
	}

	for (r = 0; r < 3; r++)
	{
		for (c = 0; c < 3; c++)
		{
			rows[r * 4 + c] = m[c * 4 + r];
		}

		rows[r * 4 + 3] = 0;
	}

	/* a product that is not a rotation (a value too large to turn by): the identity */
	for (r = 0; r < 12; r++)
	{
		if (!isfinite(rows[r]))
		{
			memset(rows, 0, 12 * sizeof(float));
			rows[0] = rows[5] = rows[10] = 1;
			return;
		}
	}
}

int
rm_rtw_tex_slots(const int *img, long nrecs, int *slot, int cap, int *tex)
{
	int n = 0;
	long s;

	for (s = 0; s < nrecs; s++)
	{
		int i;

		tex[s] = -1;

		if (img[s] < 0)
		{
			continue;
		}

		/* the images a map names are few: a search of those given an index so far */
		for (i = 0; i < n && slot[i] != img[s]; i++)
		{
		}

		if (i == n)
		{
			if (n >= cap)
			{
				return -1;
			}

			slot[n++] = img[s];
		}

		tex[s] = i;
	}

	return n;
}

long
rm_rtw_shade_build(const rm_surfrec_t *recs, long nrecs, unsigned long nindices, const int *tex, unsigned *out,
		unsigned long cap)
{
	unsigned long at = 0, tri = 0;
	long s;

	if ((nrecs < 0) || (nindices % 3) || (nindices / 3 > cap) || (nrecs && (!recs || !tex || !out)))
	{
		return -1;
	}

	/* the walk of rm_rt_materials: each record where the last ended, whole triangles, inside the buffer */
	for (s = 0; s < nrecs; s++)
	{
		const rm_surfrec_t *r = &recs[s];
		const unsigned long n = r->num_indices / 3;
		const unsigned t = ((tex[s] >= 0) && ((unsigned)tex[s] < MTL_RTW_TEX_WORLD)) ? (unsigned)tex[s] :
				MTL_RTW_SHADE_NOTEX;
		const unsigned sl = ((r->lm_slice >= 0) && (r->lm_slice < (int)MTL_RTW_SHADE_NOLM)) ? (unsigned)r->lm_slice :
				MTL_RTW_SHADE_NOLM;
		unsigned f = 0, w;
		unsigned long k;

		if ((r->first_index != at) || (r->num_indices % 3) || (r->num_indices > nindices - at))
		{
			return -1;
		}

		f |= (r->texflags & RM_TEX_WARP) ? MTL_RTW_SF_WARP : 0;
		f |= (r->texflags & RM_TEX_FLOWING) ? MTL_RTW_SF_FLOW : 0;
		f |= (r->texflags & (RM_TEX_TRANS33 | RM_TEX_TRANS66)) ? MTL_RTW_SF_TRANS : 0;
		f |= (r->texflags & RM_TEX_SKY) ? MTL_RTW_SF_SKY : 0;
		w = mtl_rtw_shade_pack(t, sl, f);

		for (k = 0; k < n; k++)
		{
			out[tri + k] = w;
		}

		tri += n;
		at += r->num_indices;
	}

	return (at == nindices) ? (long)tri : -1;
}

/* a time of the stats line: three decimals, "-" when not measured (below 0 or not finite) */
static void
rm_rtw_ms(double v, char *out, size_t len)
{
	if (isfinite(v) && (v >= 0))
	{
		snprintf(out, len, "%.3f", v);
	}
	else
	{
		snprintf(out, len, "-");
	}
}

int
rm_rtw_stats_format(const rm_rtw_stats_t *s, char *buf, size_t len)
{
	char ms[32], split[200], t[4][32], up[96] = "";
	int n;

	if (!len)
	{
		return 0;
	}

	rm_rtw_ms(s->ms, ms, sizeof(ms));
	split[0] = 0;

	/* TASK158: the world-space path's two passes and their whole span (FIX98: no raster's figures, TASK162 removed that pass) */
	if (s->ws)
	{
		rm_rtw_ms(s->compact_ms, t[0], sizeof(t[0]));
		rm_rtw_ms(s->trace_ms, t[1], sizeof(t[1]));
		rm_rtw_ms(s->span_ms, t[2], sizeof(t[2]));
		snprintf(split, sizeof(split), ", world space: compaction %s, trace %s, span %s ms", t[0], t[1], t[2]);
	}

	/*
	 * TASK126 slice 5: below the drawn size, the traced size and the
	 * upsample's fallbacks, in rm_rtt_traced_format's words (written here, as
	 * rm_rtshadow.c writes them, so the host tests need no rm_rttemporal.c)
	 */
	if (s->div > 1)
	{
		snprintf(up, sizeof(up), ", traced at %lux%lu, %lu upsample fallbacks", s->tw, s->th, s->fallbacks);
	}

	n = snprintf(buf, len, "ref_metal: rt water: frame %lu: %lu pixels, %lu listed, %lu opaque water, "
			"%lu translucent water, %lu eye rays, %lu lit, %lu world, %lu sky, %lu sky box, %lu none, %s ms%s%s",
			s->frame, s->pixels, s->listed, s->opaque, s->trans, s->eye, s->lit, s->world, s->sky, s->skybox, s->miss,
			ms, split, up);

	if ((n < 0) || ((size_t)n >= len))
	{
		buf[0] = 0;
		return 0;
	}

	return 1;
}
