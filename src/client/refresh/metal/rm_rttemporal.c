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
	memset(st, 0, sizeof(*st));
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
	v.pad = 0;
	return v;
}
