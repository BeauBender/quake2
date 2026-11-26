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
 * The pure rules of RT-primary's light on models (TASK191, PM9):
 * header/rm_rtmodel.h says what each function does;
 * tests/metal/rm_rtmodel_test.c holds them.
 *
 * =======================================================================
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "header/rm_rtmodel.h"

int
rm_rtm_on(int direct_traced, float models)
{
	return direct_traced && isfinite(models) && (models != 0);
}

int
rm_rtm_traced(int flags, int rdflags)
{
	if (flags & (RM_RF_SHELLS | RM_RF_FULLBRIGHT | RM_RF_TRANSLUCENT))
	{
		return 0;
	}

	if ((rdflags & RM_RDF_IRGOGGLES) && (flags & RM_RF_IR_VISIBLE))
	{
		return 0;
	}

	/* a depth-hacked model that is not the view weapon is drawn in front of everything: it keeps gl1's light */
	if ((flags & RM_RF_DEPTHHACK) && !(flags & RM_RF_WEAPONMODEL))
	{
		return 0;
	}

	return 1;
}

static int
rtm_finite3(const float *v)
{
	return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]);
}

int
rm_rtm_probe(const float *origin, const float *spot, const float *normal, int hit, rm_rtm_probe_t *out)
{
	double len, side;
	int k;

	if ((hit < 0) || !origin || !spot || !normal || !out || !rtm_finite3(origin) || !rtm_finite3(spot) ||
		!rtm_finite3(normal))
	{
		return 0;
	}

	len = sqrt((double)normal[0] * normal[0] + (double)normal[1] * normal[1] + (double)normal[2] * normal[2]);

	if (!(len > 0) || !isfinite(len))
	{
		return 0;
	}

	/* the face's side the model is on: R_RecursiveLightPoint's plane is shared by both faces of a split */
	side = 0;

	for (k = 0; k < 3; k++)
	{
		side += ((double)origin[k] - spot[k]) * normal[k];
	}

	for (k = 0; k < 3; k++)
	{
		out->spot[k] = spot[k];
		out->normal[k] = (float)(((side < 0) ? -normal[k] : normal[k]) / len);
	}

	return 1;
}

static int
rtm_same_key(const rm_rtm_key_t *a, const rm_rtm_key_t *b)
{
	return (a->model == b->model) && (a->weapon == b->weapon) && (a->origin[0] == b->origin[0]) &&
		(a->origin[1] == b->origin[1]) && (a->origin[2] == b->origin[2]);
}

int
rm_rtm_add(rm_rtm_frame_t *f, const rm_rtm_key_t *k, const rm_rtm_probe_t *p)
{
	int i;

	if (!f || !k || !p || (f->n < 0) || (f->n > RM_RTM_MAX))
	{
		return -1;
	}

	for (i = 0; i < f->n; i++)
	{
		if (rtm_same_key(&f->key[i], k))
		{
			return i;
		}
	}

	if (f->n == RM_RTM_MAX)
	{
		return -1;
	}

	f->key[f->n] = *k;
	f->probe[f->n] = *p;
	return f->n++;
}

int
rm_rtm_match(const rm_rtm_key_t *keys, int n, const void *model, int weapon, const float *origin)
{
	const double max2 = (double)RM_RTM_MATCH * RM_RTM_MATCH;
	double best = 0;
	int i, at = -1;

	if (!keys || (n <= 0) || !origin || !rtm_finite3(origin))
	{
		return -1;
	}

	if (n > RM_RTM_MAX)
	{
		n = RM_RTM_MAX;
	}

	for (i = 0; i < n; i++)
	{
		double d2 = 0;
		int k;

		if ((keys[i].model != model) || ((keys[i].weapon != 0) != (weapon != 0)))
		{
			continue;
		}

		for (k = 0; k < 3; k++)
		{
			const double d = (double)keys[i].origin[k] - origin[k];

			d2 += d * d;
		}

		if ((d2 <= max2) && ((at < 0) || (d2 < best)))
		{
			at = i;
			best = d2;
		}
	}

	return at;
}

void
rm_rtm_point(const float *light, float modulate, float *point)
{
	int k;

	for (k = 0; k < 3; k++)
	{
		float l = light[k];

		l = (l > 0.0f) ? ((l < 1.0f) ? l : 1.0f) : 0.0f;
		point[k] = (isfinite(modulate) && (modulate > 0.0f)) ? l * modulate : 0.0f;
	}
}

int
rm_rtm_slot_ready(unsigned long traced, unsigned long fno)
{
	return (traced != 0) && (fno > traced) && (fno - traced == RMB_FRAMES_IN_FLIGHT);
}

int
rm_rtm_stats_format(unsigned long frame, int probes, int matched, int stock, char *buf, size_t len)
{
	int n;

	if (!buf || (len == 0))
	{
		return 0;
	}

	n = snprintf(buf, len, "ref_metal: rt models: frame %lu: %d probes traced, %d models lit by a probe, %d by the lightmap",
			frame, probes, matched, stock);

	if ((n < 0) || ((size_t)n >= len))
	{
		buf[0] = '\0';
		return 0;
	}

	return 1;
}
