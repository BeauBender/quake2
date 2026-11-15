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
 * The pure part of ref_metal's MetalFX scalers (TASK111): header/
 * rm_metalfx.h says what each function does; tests/metal/rm_fx_test.c
 * holds them.
 *
 * =======================================================================
 */

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "header/rm_metalfx.h"

int
rm_fx_mode_of(float v)
{
	if (!isfinite(v) || (v < 0.0f))
	{
		return RM_FX_OFF;
	}

	return (v >= (float)RM_FX_MAX) ? RM_FX_MAX : (int)v;
}

int
rm_fx_supported(int m, unsigned has)
{
	if ((m < 0) || (m > RM_FX_MAX))
	{
		return RM_FX_OFF;
	}

	for (; m > RM_FX_OFF; m--)
	{
		if (has & RM_FX_HAS(m))
		{
			return m;
		}
	}

	return RM_FX_OFF;
}

int
rm_fx_run(int m, int rt_effect, unsigned has)
{
	m = rm_fx_supported(m, has);

	if ((m == RM_FX_DENOISED) && !rt_effect)
	{
		m = rm_fx_supported(RM_FX_TEMPORAL, has);
	}

	return m;
}

int
rm_fx_interp(float v, unsigned has)
{
	return isfinite(v) && (v != 0.0f) && ((has & RM_FX_HAS_INTERP) != 0);
}

const char *
rm_fx_name(int m)
{
	static const char *names[RM_FX_MAX + 1] = { "off", "spatial", "temporal", "temporal denoised" };

	return ((m >= 0) && (m <= RM_FX_MAX)) ? names[m] : "unknown";
}

int
rm_fx_default_rows(unsigned families)
{
	/* apple9 or any later family */
	return (families & ~(RMB_FAMILY_BIT(9) - 1u)) ? RM_FX_ROWS_APPLE9 : RM_FX_ROWS_OTHER;
}

float
rm_fx_scale(float v, int rows, int out_h, float ratio)
{
	float lo;

	if (out_h <= 0)
	{
		return 1.0f;
	}

	if (!isfinite(ratio) || (ratio > RM_FX_RATIO_MAX))
	{
		ratio = RM_FX_RATIO_MAX;
	}
	else if (!(ratio >= 1.0f))
	{
		ratio = 1.0f;
	}

	if (!isfinite(v) || !(v > 0.0f))
	{
		if (rows <= 0)
		{
			return 1.0f;
		}

		v = (float)rows / (float)out_h;
	}

	lo = 1.0f / ratio;
	return (v < lo) ? lo : (v > 1.0f) ? 1.0f : v;
}

/* a side of the input: the output's times scale, rounded, 1 to the output's */
static int
rm_fx_side(int side, float scale)
{
	double v = floor((double)side * scale + 0.5);

	if (v < 1.0)
	{
		return 1;
	}

	return (v > side) ? side : (int)v;
}

/* a rectangle's edge, rounded, when it is finite and within 0 to RM_FX_MAX_SIDE; -1 otherwise */
static int
rm_fx_edge(double v)
{
	if (!isfinite(v) || (v < 0.0) || (v > (double)RM_FX_MAX_SIDE))
	{
		return -1;
	}

	return (int)floor(v + 0.5);
}

int
rm_fx_sizes(const rm_viewport_t *out, float scale, rm_fx_size_t *s)
{
	const int x = rm_fx_edge(out->x), y = rm_fx_edge(out->y), w = rm_fx_edge(out->w), h = rm_fx_edge(out->h);

	memset(s, 0, sizeof(*s));

	if ((x < 0) || (y < 0) || (w < 1) || (h < 1))
	{
		return 0;
	}

	if (!isfinite(scale) || !(scale > 0.0f))
	{
		scale = 1.0f;
	}

	s->ow = w;
	s->oh = h;
	s->ox = x;
	s->oy = y;
	s->iw = rm_fx_side(w, scale);
	s->ih = rm_fx_side(h, scale);
	return 1;
}

void
rm_fx_input_viewport(const rm_viewport_t *out, const rm_fx_size_t *s, rm_viewport_t *in)
{
	in->x = 0;
	in->y = 0;
	in->w = s->iw;
	in->h = s->ih;
	in->znear = out->znear;
	in->zfar = out->zfar;
}

float
rm_fx_halton(unsigned i, unsigned base)
{
	double f = 1.0, r = 0.0;

	if (base < 2)
	{
		return 0.0f;
	}

	while (i > 0)
	{
		f /= base;
		r += f * (i % base);
		i /= base;
	}

	/* a float rounds 1 - 2^-32 up to 1: keep it inside [0, 1) */
	return (r < (double)nextafterf(1.0f, 0.0f)) ? (float)r : nextafterf(1.0f, 0.0f);
}

void
rm_fx_jitter(int mode, unsigned n, float *jx, float *jy)
{
	const unsigned i = (n % RM_FX_JITTER_PHASES) + 1;

	if (!rm_fx_wants_depth(mode))
	{
		*jx = *jy = 0.0f;
		return;
	}

	*jx = rm_fx_halton(i, 2) - 0.5f;
	*jy = rm_fx_halton(i, 3) - 0.5f;
}

int
rm_fx_wants_motion(int mode, int interp)
{
	return ((mode > RM_FX_OFF) && (mode <= RM_FX_MAX)) || interp;
}

int
rm_fx_wants_depth(int mode)
{
	return (mode == RM_FX_TEMPORAL) || (mode == RM_FX_DENOISED);
}

unsigned
rm_fx_reset(const rm_fx_last_t *last, int mode, const rm_fx_size_t *s, const float *org, float cut)
{
	unsigned r = 0;
	int i;

	if (!last->valid)
	{
		return RM_FX_RESET_FIRST;
	}

	if (last->mode != mode)
	{
		r |= RM_FX_RESET_MODE;
	}

	if ((last->size.iw != s->iw) || (last->size.ih != s->ih) || (last->size.ow != s->ow) ||
		(last->size.oh != s->oh))
	{
		r |= RM_FX_RESET_SIZE;
	}

	if ((last->size.ox != s->ox) || (last->size.oy != s->oy))
	{
		r |= RM_FX_RESET_RECT;
	}

	for (i = 0; i < 3; i++)
	{
		/* written so that a NaN on either side cuts */
		if (!(fabsf(org[i] - last->org[i]) <= cut))
		{
			r |= RM_FX_RESET_CUT;
		}
	}

	return r;
}

void
rm_fx_keep(rm_fx_last_t *last, int mode, const rm_fx_size_t *s, const float *org)
{
	last->valid = 1;
	last->mode = mode;
	last->size = *s;
	memcpy(last->org, org, sizeof(last->org));
}

/* appends to buf at *used; 0, with buf emptied, when it does not fit */
static int
rm_fx_append(char *buf, size_t len, size_t *used, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf + *used, len - *used, fmt, ap);
	va_end(ap);

	if ((n < 0) || ((size_t)n >= len - *used))
	{
		buf[0] = '\0';
		return 0;
	}

	*used += (size_t)n;
	return 1;
}

int
rm_fx_fallback_format(const char *what, int asked, int got, const char *why, char *buf, size_t len)
{
	const int named = strcmp(what, "r_metalfx") == 0;
	size_t used = 0;
	int ok;

	if (len == 0)
	{
		return 0;
	}

	buf[0] = '\0';
	ok = rm_fx_append(buf, len, &used, "ref_metal: %s %d", what, asked);
	ok = ok && (!named || rm_fx_append(buf, len, &used, " (%s)", rm_fx_name(asked)));
	ok = ok && rm_fx_append(buf, len, &used, " %s %d", why, got);
	ok = ok && (!named || rm_fx_append(buf, len, &used, " (%s)", rm_fx_name(got)));
	return ok;
}

int
rm_fx_stats_format(int mode, const rm_fx_size_t *s, unsigned resets, double gpu_ms, char *buf, size_t len)
{
	static const char *reasons[] = { "first", "mode", "size", "rect", "cut" };
	size_t used = 0, k;
	int ok;

	if (len == 0)
	{
		return 0;
	}

	buf[0] = '\0';
	ok = rm_fx_append(buf, len, &used, "ref_metal: metal_stats: metalfx %s: input %dx%d, output %dx%d at %d,%d, reset",
			rm_fx_name(mode), s->iw, s->ih, s->ow, s->oh, s->ox, s->oy);

	for (k = 0; ok && (k < sizeof(reasons) / sizeof(reasons[0])); k++)
	{
		if (resets & (1u << k))
		{
			ok = rm_fx_append(buf, len, &used, " %s", reasons[k]);
		}
	}

	ok = ok && (resets || rm_fx_append(buf, len, &used, " none"));
	ok = ok && ((gpu_ms >= 0.0) ? rm_fx_append(buf, len, &used, ", scaler %.3f ms", gpu_ms) :
			rm_fx_append(buf, len, &used, ", scaler not timed"));
	return ok;
}
