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
 * Temporal accumulation's plain part (RT7, TASK59 slice 1): see
 * header/rm_rttemporal.h. No SDL, no Metal.
 *
 * =======================================================================
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "header/rm_rttemporal.h"

float
rm_rtt_alpha(float v)
{
	/* written so that a NaN is off */
	if (!(v > 0.0f) || !isfinite(v))
	{
		return 0.0f;
	}

	return (v < RM_RTT_ALPHA_MAX) ? v : RM_RTT_ALPHA_MAX;
}

void
rm_rtt_state_reset(rm_rtt_state_t *st)
{
	const unsigned seq = st->seq;

	memset(st, 0, sizeof(*st));
	st->seq = seq;
}

unsigned
rm_rtt_step(rm_rtt_state_t *st, const rm_rtt_key_t *k, float *move)
{
	unsigned r = 0;
	float m = 0.0f;
	int i;

	if (!st->valid)
	{
		r = RM_RTT_RESET_FIRST;
	}
	else
	{
		if ((st->last.w != k->w) || (st->last.h != k->h))
		{
			r |= RM_RTT_RESET_SIZE;
		}

		if ((st->last.vx != k->vx) || (st->last.vy != k->vy) || (st->last.vw != k->vw) || (st->last.vh != k->vh))
		{
			r |= RM_RTT_RESET_RECT;
		}

		for (i = 0; i < 3; i++)
		{
			const float d = k->org[i] - st->last.org[i];

			/* written so that a NaN on either side cuts */
			if (!(fabsf(d) <= RM_RTT_CUT_DIST))
			{
				r |= RM_RTT_RESET_CUT;
			}

			m += d * d;
		}
	}

	if (r)
	{
		st->views = 0;
		m = 0.0f;
	}

	*move = (r || !isfinite(m)) ? 0.0f : sqrtf(m);
	st->last = *k;
	st->valid = 1;
	st->drew = 1;
	st->views++;
	st->seq++;
	return r;
}

void
rm_rtt_frame_end(rm_rtt_state_t *st)
{
	if (!st->drew)
	{
		st->valid = 0;
	}

	st->drew = 0;
}

rm_rtt_view_t
rm_rtt_view(float alpha, unsigned reasons, float move)
{
	rm_rtt_view_t v;

	v.alpha = rm_rtt_alpha(alpha);
	v.move = isfinite(move) && (move > 0.0f) ? move : 0.0f;
	v.reset = reasons;
	v.seq = 0;
	return v;
}

void
rm_rtt_hist_reset(rm_rtt_hist_t *h)
{
	h->valid = 0;
}

unsigned
rm_rtt_hist_take_at(rm_rtt_hist_t *h, const rm_rtt_view_t *t, unsigned div)
{
	const unsigned d = div ? div : 1u;
	unsigned r = t->reset;

	/* the history took the view before this one, or it is not a history of the views before it */
	if (!h->valid || (t->seq != h->seq + 1u))
	{
		r |= RM_RTT_RESET_FIRST;
	}

	/* TASK126: the history was taken at another divisor; 0, no take yet, is not one */
	if (h->div && (h->div != d))
	{
		r |= RM_RTT_RESET_SCALE;
	}

	h->valid = 1;
	h->seq = t->seq;
	h->div = d;
	return r;
}

unsigned
rm_rtt_hist_take(rm_rtt_hist_t *h, const rm_rtt_view_t *t)
{
	return rm_rtt_hist_take_at(h, t, 1u);
}

/* buf from the snprintf that returned n: 1 when it fit, 0 with buf emptied otherwise */
static int
rm_rtt_fits(int n, char *buf, size_t len)
{
	if ((n < 0) || ((size_t)n >= len))
	{
		buf[0] = 0;
		return 0;
	}

	return 1;
}

int
rm_rtt_scale_line_format(const char *effect, char *buf, size_t len)
{
	if (!len)
	{
		return 0;
	}

	return rm_rtt_fits(snprintf(buf, len, "ref_metal: r_rt_stats: reset: %s scale", effect), buf, len);
}

int
rm_rtt_traced_format(unsigned w, unsigned h, unsigned long fallbacks, char *buf, size_t len)
{
	if (!len)
	{
		return 0;
	}

	return rm_rtt_fits(snprintf(buf, len, ", traced at %ux%u, %lu upsample fallbacks", w, h, fallbacks), buf, len);
}

int
rm_rtt_up_frame(rm_rtt_up_t *u, unsigned long frame, unsigned div, unsigned long tw, unsigned long th,
		unsigned long pixels, unsigned long fallbacks)
{
	int changed;

	(void)frame;

	if (div <= 1u)
	{
		u->tw = u->th = 0;
		return 0;
	}

	changed = (tw != u->tw) || (th != u->th);
	u->tw = tw;
	u->th = th;
	u->fallbacks += fallbacks;
	u->pixels += pixels;
	return changed;
}

void
rm_rtt_up_map(rm_rtt_up_t *u)
{
	u->fallbacks = 0;
	u->pixels = 0;
}

int
rm_rtt_up_line_format(const char *effect, unsigned long tw, unsigned long th, unsigned long w, unsigned long h,
		unsigned long fallbacks, char *buf, size_t len)
{
	if (!len)
	{
		return 0;
	}

	return rm_rtt_fits(snprintf(buf, len, "ref_metal: r_rt_stats: upsample: %s %lux%lu of %lux%lu, %lu fallbacks",
			effect, tw, th, w, h, fallbacks), buf, len);
}

int
rm_rtt_up_total_format(const char *map, const char *effect, const rm_rtt_up_t *u, char *buf, size_t len)
{
	if (!len)
	{
		return 0;
	}

	if (!u->pixels)
	{
		buf[0] = 0;
		return 0;
	}

	return rm_rtt_fits(snprintf(buf, len, "ref_metal: rt: %s: upsample: %s %llu fallbacks of %llu upsampled pixels "
			"(%.3f %%)", map, effect, u->fallbacks, u->pixels, 100.0 * (double)u->fallbacks / (double)u->pixels),
			buf, len);
}
