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

int
rm_rtw_stats_format(const rm_rtw_stats_t *s, char *buf, size_t len)
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

	n = snprintf(buf, len, "ref_metal: rt water: frame %lu: %lu pixels, %lu opaque water, %lu translucent water, "
			"%lu eye rays, %lu lit, %lu sky, %lu none, %s ms", s->frame, s->pixels, s->opaque, s->trans, s->eye,
			s->lit, s->sky, s->miss, ms);

	if ((n < 0) || ((size_t)n >= len))
	{
		buf[0] = 0;
		return 0;
	}

	return 1;
}
