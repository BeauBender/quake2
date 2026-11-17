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
 * The pure part of ref_metal's ray-traced sun and sky shadow (RT4,
 * TASK56; header/rm_rtsun.h): when a frame traces it, the cvars as the
 * pass takes them, the direction from pitch and yaw, the per-map table's
 * parser (scripts/rt/sun-angles.txt), the sky's share of the world's
 * face area from TASK53's material ids, sky_color from the sky box's
 * means, the view the kernel unprojects with and the stats lines. No SDL,
 * no Metal: tests/metal/rm_rtsun_test.c runs it on the host.
 *
 * =======================================================================
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "header/rm_rtsun.h"
#include "header/rm_rtshadow.h"
#include "header/rm_rt.h"

int
rm_rtsun_on(float sun, int device_rt, float fullbright)
{
	if (!device_rt || !isfinite(sun) || (sun == 0))
	{
		return 0;
	}

	/* r_fullbright: gl1 draws the world without its lightmaps, so the floor gate has nothing to read */
	if (isfinite(fullbright) && (fullbright != 0))
	{
		return 0;
	}

	return 1;
}

float
rm_rtsun_scale(float v)
{
	if (!isfinite(v))
	{
		return RM_RTSUN_SCALE;
	}

	return (v < 0) ? 0.0f : (v > RM_RTSUN_SCALE_MAX) ? RM_RTSUN_SCALE_MAX : v;
}

float
rm_rtsun_floor(float v)
{
	if (!isfinite(v))
	{
		return RM_RTSUN_FLOOR;
	}

	return (v < 0) ? 0.0f : (v > 1) ? 1.0f : v;
}

static int
rm_rtsun_blank(char c)
{
	return (c == ' ') || (c == '\t');
}

/*
 * A decimal number at *p, after any blanks: an optional sign, digits, and
 * an optional '.' with digits (at least one digit in all), ending at a
 * blank, at end or at stop; no exponent, no hex, no inf or nan. 1 with
 * *v and *p past it; 0 otherwise.
 */
static int
rm_rtsun_number(const char **p, const char *end, float *v)
{
	char tok[32];
	const char *s = *p, *t;
	size_t n;
	int digits = 0, dot = 0;
	double d;

	while ((s < end) && rm_rtsun_blank(*s))
	{
		s++;
	}

	t = s;

	if ((t < end) && ((*t == '+') || (*t == '-')))
	{
		t++;
	}

	for (; (t < end) && !rm_rtsun_blank(*t); t++)
	{
		if ((*t >= '0') && (*t <= '9'))
		{
			digits++;
		}
		else if ((*t == '.') && !dot)
		{
			dot = 1;
		}
		else
		{
			return 0;
		}
	}

	n = (size_t)(t - s);

	if (!digits || (n >= sizeof(tok)))
	{
		return 0;
	}

	memcpy(tok, s, n);
	tok[n] = 0;
	d = strtod(tok, NULL);

	if (!isfinite(d) || (fabs(d) > 1e6))
	{
		return 0;
	}

	*v = (float)d;
	*p = t;
	return 1;
}

/* "pitch yaw" in [s, end): two numbers in bounds and nothing else but blanks */
static int
rm_rtsun_angles_in(const char *s, const char *end, float *pitch, float *yaw)
{
	float p, y;

	if (!rm_rtsun_number(&s, end, &p) || !rm_rtsun_number(&s, end, &y))
	{
		return 0;
	}

	while ((s < end) && rm_rtsun_blank(*s))
	{
		s++;
	}

	if ((s != end) || !(p >= 0) || !(p <= 90) || !(y >= 0) || !(y < 360))
	{
		return 0;
	}

	*pitch = p;
	*yaw = y;
	return 1;
}

int
rm_rtsun_parse_angles(const char *s, float *pitch, float *yaw)
{
	if (!s)
	{
		return 0;
	}

	return rm_rtsun_angles_in(s, s + strlen(s), pitch, yaw);
}

void
rm_rtsun_dir(float pitch, float yaw, float *out)
{
	const double p = (double)pitch * M_PI / 180.0, y = (double)yaw * M_PI / 180.0;

	out[0] = (float)(cos(p) * cos(y));
	out[1] = (float)(cos(p) * sin(y));
	out[2] = (float)sin(p);
}

static int
rm_rtsun_namechar(char c)
{
	return ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9')) || (c == '_') ||
		(c == '-');
}

/* a and b equal without case, b of length n */
static int
rm_rtsun_same_name(const char *a, const char *b, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
	{
		const char x = (a[i] >= 'A' && a[i] <= 'Z') ? (char)(a[i] - 'A' + 'a') : a[i];
		const char y = (b[i] >= 'A' && b[i] <= 'Z') ? (char)(b[i] - 'A' + 'a') : b[i];

		if (!a[i] || (x != y))
		{
			return 0;
		}
	}

	return a[n] == 0;
}

/* one line [s, end) without its newline or comment: -1 blank, 1 an entry in *e, 0 bad */
static int
rm_rtsun_line(const char *s, const char *end, rm_rtsun_entry_t *e)
{
	const char *name;
	size_t n;

	while ((s < end) && rm_rtsun_blank(*s))
	{
		s++;
	}

	if (s == end)
	{
		return -1;
	}

	for (name = s; (s < end) && !rm_rtsun_blank(*s); s++)
	{
		if (!rm_rtsun_namechar(*s))
		{
			return 0;
		}
	}

	n = (size_t)(s - name);

	if ((n < 1) || (n > RM_RTSUN_MAPNAME) || !rm_rtsun_angles_in(s, end, &e->pitch, &e->yaw))
	{
		return 0;
	}

	memcpy(e->map, name, n);
	e->map[n] = 0;
	return 1;
}

int
rm_rtsun_table_parse(const char *text, rm_rtsun_entry_t *out, int cap, int *nbad, int *first_bad)
{
	int n = 0, line = 0;
	const char *s = text;

	*nbad = 0;
	*first_bad = 0;

	while (s && *s)
	{
		const char *eol = strchr(s, '\n'), *end, *hash;
		rm_rtsun_entry_t e;
		int r;

		eol = eol ? eol : s + strlen(s);
		end = eol;
		line++;

		if ((end > s) && (end[-1] == '\r'))
		{
			end--;
		}

		hash = memchr(s, '#', (size_t)(end - s));
		end = hash ? hash : end;
		r = rm_rtsun_line(s, end, &e);

		/* a map named twice: the later line is bad */
		if ((r == 1) && rm_rtsun_table_find(out, n, e.map))
		{
			r = 0;
		}

		if ((r == 1) && (n < cap))
		{
			out[n++] = e;
		}
		else if (r >= 0)
		{
			(*nbad)++;
			*first_bad = *first_bad ? *first_bad : line;
		}

		s = *eol ? eol + 1 : eol;
	}

	return n;
}

const rm_rtsun_entry_t *
rm_rtsun_table_find(const rm_rtsun_entry_t *t, int n, const char *map)
{
	int i;

	if (!map || !map[0])
	{
		return NULL;
	}

	for (i = 0; i < n; i++)
	{
		if (rm_rtsun_same_name(t[i].map, map, strlen(map)))
		{
			return &t[i];
		}
	}

	return NULL;
}

int
rm_rtsun_map_key(const char *model_name, char *out, size_t len)
{
	const char *base, *slash;
	size_t n;

	if (len)
	{
		out[0] = 0;
	}

	if (!model_name || !len)
	{
		return 0;
	}

	slash = strrchr(model_name, '/');
	base = slash ? slash + 1 : model_name;
	n = strlen(base);

	if ((n >= 4) && rm_rtsun_same_name(base + n - 4, ".bsp", 4))
	{
		n -= 4;
	}

	if ((n < 1) || (n >= len))
	{
		return 0;
	}

	memcpy(out, base, n);
	out[n] = 0;
	return 1;
}

/* the area of the triangle of vertices a, b, c (their pos) */
static double
rm_rtsun_tri_area(const float *a, const float *b, const float *c)
{
	const double ux = (double)b[0] - a[0], uy = (double)b[1] - a[1], uz = (double)b[2] - a[2];
	const double vx = (double)c[0] - a[0], vy = (double)c[1] - a[1], vz = (double)c[2] - a[2];
	const double x = uy * vz - uz * vy, y = uz * vx - ux * vz, z = ux * vy - uy * vx;

	return 0.5 * sqrt(x * x + y * y + z * z);
}

int
rm_rtsun_sky_area(const rm_surfrec_t *recs, long nrecs, const rm_wvert_t *verts, unsigned long nverts,
		const unsigned *indices, unsigned long nindices, unsigned long first_index, unsigned long num_indices,
		double *sky, double *total)
{
	double s = 0, t = 0;
	long r;

	*sky = *total = 0;

	if ((nrecs < 0) || (first_index > nindices) || (num_indices > nindices - first_index) ||
		(nrecs && (!recs || !indices || !verts)))
	{
		return 0;
	}

	for (r = 0; r < nrecs; r++)
	{
		const unsigned long a = recs[r].first_index, n = recs[r].num_indices;
		const int is_sky = rm_rt_material((int)recs[r].texflags) == RM_RT_MAT_SKY;
		unsigned long k;

		if ((n % 3) || (a > nindices) || (n > nindices - a))
		{
			return 0;
		}

		/* outside the range (another model's), counted by no one here */
		if ((a + n <= first_index) || (a >= first_index + num_indices))
		{
			continue;
		}

		/* across one of its ends: the records do not follow the models */
		if ((a < first_index) || (a + n > first_index + num_indices))
		{
			return 0;
		}

		for (k = a; k < a + n; k += 3)
		{
			const unsigned i0 = indices[k], i1 = indices[k + 1], i2 = indices[k + 2];
			double area;

			if ((i0 >= nverts) || (i1 >= nverts) || (i2 >= nverts))
			{
				return 0;
			}

			area = rm_rtsun_tri_area(verts[i0].pos, verts[i1].pos, verts[i2].pos);
			t += area;
			s += is_sky ? area : 0;
		}
	}

	*sky = s;
	*total = t;
	return 1;
}

int
rm_rtsun_sky_enough(double sky, double total)
{
	return isfinite(sky) && isfinite(total) && (total > 0) && (sky >= RM_RTSUN_SKY_MIN * total);
}

void
rm_rtsun_mean(const unsigned char *rgba, size_t npix, float *out)
{
	double c[3] = { 0, 0, 0 };
	size_t i;
	int k;

	for (i = 0; i < npix; i++)
	{
		for (k = 0; k < 3; k++)
		{
			c[k] += rgba[i * 4 + k];
		}
	}

	for (k = 0; k < 3; k++)
	{
		out[k] = npix ? (float)(c[k] / (255.0 * (double)npix)) : 0.0f;
	}
}

void
rm_rtsun_sky_color(const float (*means)[3], const int *have, float *out)
{
	double c[3] = { 0, 0, 0 }, m;
	int i, k, n = 0;

	/* the faces above the horizon: rt, bk, lf, ft and up; dn (5) left out */
	for (i = 0; have && (i < 5); i++)
	{
		if (!have[i])
		{
			continue;
		}

		for (k = 0; k < 3; k++)
		{
			c[k] += means[i][k];
		}

		n++;
	}

	m = (c[0] > c[1]) ? c[0] : c[1];
	m = (c[2] > m) ? c[2] : m;

	if (!n || !isfinite(m) || !(m > 0))
	{
		out[0] = out[1] = out[2] = 1.0f;
		return;
	}

	for (k = 0; k < 3; k++)
	{
		out[k] = (float)(c[k] / m);
	}
}

int
rm_rtsun_show(float v)
{
	if (!isfinite(v) || !(v > 0))
	{
		return 0;
	}

	return (v >= RM_RTSUN_SHOW_MAX) ? RM_RTSUN_SHOW_MAX : (int)v;
}

int
rm_rtsun_view(const float *proj, const float *world, const rm_viewport_t *vp, const float *eye, float znear,
		const float *dir, const float *sky, float scale, float floor_, unsigned show, rm_rtsun_view_t *out)
{
	rm_rts_view_t s;
	int k;

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

	for (k = 0; k < 3; k++)
	{
		out->dir[k] = dir[k];
		out->sun[k] = sky[k] * scale;
	}

	out->floor_ = floor_;
	out->show = show;
	return 1;
}

int
rm_rtsun_stats_format(const rm_rtsun_stats_t *s, char *buf, size_t len)
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

	n = snprintf(buf, len, "ref_metal: rt sun: frame %lu: %lu pixels, %lu world, %lu bright, %lu facing away, "
			"%lu rays, %lu lit, %s ms", s->frame, s->pixels, s->world, s->bright, s->facing, s->rays, s->lit, ms);

	if ((n < 0) || ((size_t)n >= len))
	{
		buf[0] = 0;
		return 0;
	}

	return 1;
}

int
rm_rtsun_map_format(const char *map, double sky, double total, int from_table, float pitch, float yaw,
		char *buf, size_t len)
{
	const double share = (isfinite(sky) && isfinite(total) && (total > 0)) ? 100.0 * sky / total : 0.0;
	int n;

	if (!len)
	{
		return 0;
	}

	n = snprintf(buf, len, "ref_metal: rt sun: %s: sky %.0f of %.0f square units (%.2f %%), %s; "
			"the sun at pitch %g yaw %g (%s)", map, sky, total, share,
			rm_rtsun_sky_enough(sky, total) ? "the pass runs" : "under 2 %: the pass is skipped", pitch, yaw,
			from_table ? "scripts/rt/sun-angles.txt" : "r_rt_sun_angles");

	if ((n < 0) || ((size_t)n >= len))
	{
		buf[0] = 0;
		return 0;
	}

	return 1;
}
