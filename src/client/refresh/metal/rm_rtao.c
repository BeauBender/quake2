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
 * The pure part of ref_metal's ray-traced ambient occlusion (RT5,
 * TASK57; header/rm_rtao.h): when a frame traces it, the cvars as the
 * pass takes them, the blue-noise tile (void-and-cluster, made here, no
 * asset data), the view the kernel unprojects with and the stats line.
 * No SDL, no Metal: tests/metal/rm_rtao_test.c runs it on the host.
 *
 * =======================================================================
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "header/rm_rtao.h"
#include "header/rm_rtshadow.h"

int
rm_rtao_on(float ao, int device_rt, float fullbright)
{
	if (!device_rt || !isfinite(ao) || (ao == 0))
	{
		return 0;
	}

	/* r_fullbright: gl1 draws the world without its lightmaps, so there is no term to scale */
	if (isfinite(fullbright) && (fullbright != 0))
	{
		return 0;
	}

	return 1;
}

float
rm_rtao_range(float v)
{
	if (!isfinite(v))
	{
		return RM_RTAO_RANGE;
	}

	return (v < RM_RTAO_RANGE_MIN) ? RM_RTAO_RANGE_MIN : (v > RM_RTAO_RANGE_MAX) ? RM_RTAO_RANGE_MAX : v;
}

float
rm_rtao_floor(float v)
{
	if (!isfinite(v))
	{
		return RM_RTAO_FLOOR;
	}

	return (v < 0) ? 0.0f : (v > 1) ? 1.0f : v;
}

unsigned
rm_rtao_frame(float seed, unsigned long count)
{
	if (isfinite(seed) && (seed >= 1))
	{
		return (seed >= 65535.0f) ? 65535u : (unsigned)seed;
	}

	return (unsigned)(count & 0xffffUL);
}

int
rm_rtao_show(float v)
{
	if (!isfinite(v) || !(v > 0))
	{
		return 0;
	}

	return (v >= RM_RTAO_SHOW_MAX) ? RM_RTAO_SHOW_MAX : (int)v;
}

/*
 * The void-and-cluster method (Ulichney, 1993) on the MTL_RTAO_TILE
 * torus. The energy of a pixel is the sum over the set pixels of a
 * Gaussian of their toroidal distance, sigma 1.5, in integers (2^20 at
 * distance 0), so every sum is exact and the result does not depend on
 * the order of additions or on the compiler.
 */
#define BN_T MTL_RTAO_TILE
#define BN_N MTL_RTAO_TILE_N
#define BN_SIGMA 1.5

static void
rm_rtao_bn_put(int64_t *e, const int64_t *g, int p, int sign)
{
	const int px = p % BN_T, py = p / BN_T;
	int q;

	for (q = 0; q < BN_N; q++)
	{
		const int dx = ((q % BN_T) - px) & (BN_T - 1), dy = ((q / BN_T) - py) & (BN_T - 1);

		e[q] += sign * g[dy * BN_T + dx];
	}
}

/* the set pixel of the largest energy (the tightest cluster), or the unset one of the smallest (the largest void) */
static int
rm_rtao_bn_find(const int64_t *e, const unsigned char *set, int cluster)
{
	int q, best = -1;

	for (q = 0; q < BN_N; q++)
	{
		if ((set[q] != 0) != (cluster != 0))
		{
			continue;
		}

		if ((best < 0) || (cluster ? (e[q] > e[best]) : (e[q] < e[best])))
		{
			best = q;
		}
	}

	return best;
}

int
rm_rtao_bluenoise(unsigned short *rank)
{
	int64_t *g = malloc(BN_N * sizeof(*g)), *e = malloc(BN_N * sizeof(*e)), *e0 = malloc(BN_N * sizeof(*e0));
	unsigned char *set = malloc(BN_N), *set0 = malloc(BN_N);
	const int ones = BN_N / 10;
	unsigned lcg = 0x5eed1234u;
	int i, n, r, ok = 0;

	if (!g || !e || !e0 || !set || !set0)
	{
		goto out;
	}

	for (i = 0; i < BN_N; i++)
	{
		const int dx = i % BN_T, dy = i / BN_T;
		const int tx = (dx > BN_T / 2) ? BN_T - dx : dx, ty = (dy > BN_T / 2) ? BN_T - dy : dy;

		g[i] = (int64_t)llround(1048576.0 * exp(-(double)(tx * tx + ty * ty) / (2.0 * BN_SIGMA * BN_SIGMA)));
	}

	/* the initial pattern: a tenth of the pixels, from a fixed generator */
	memset(set, 0, BN_N);
	memset(e, 0, BN_N * sizeof(*e));

	for (n = 0; n < ones;)
	{
		int p;

		lcg = lcg * 1664525u + 1013904223u;
		p = (int)((lcg >> 8) % BN_N);

		if (!set[p])
		{
			set[p] = 1;
			rm_rtao_bn_put(e, g, p, 1);
			n++;
		}
	}

	/* its tightest cluster to its largest void until the two are the same pixel */
	for (i = 0; i < 8 * BN_N; i++)
	{
		const int c = rm_rtao_bn_find(e, set, 1);
		int v;

		set[c] = 0;
		rm_rtao_bn_put(e, g, c, -1);
		v = rm_rtao_bn_find(e, set, 0);
		set[v] = 1;
		rm_rtao_bn_put(e, g, v, 1);

		if (v == c)
		{
			break;
		}
	}

	memcpy(set0, set, BN_N);
	memcpy(e0, e, BN_N * sizeof(*e));

	/* phase 1: the pattern's pixels ranked by taking its tightest cluster away, the last rank first */
	for (r = ones - 1; r >= 0; r--)
	{
		const int c = rm_rtao_bn_find(e, set, 1);

		set[c] = 0;
		rm_rtao_bn_put(e, g, c, -1);
		rank[c] = (unsigned short)r;
	}

	/* phases 2 and 3: from the pattern again, its largest void filled with the next rank until every pixel is set */
	memcpy(set, set0, BN_N);
	memcpy(e, e0, BN_N * sizeof(*e));

	for (r = ones; r < BN_N; r++)
	{
		const int v = rm_rtao_bn_find(e, set, 0);

		set[v] = 1;
		rm_rtao_bn_put(e, g, v, 1);
		rank[v] = (unsigned short)r;
	}

	ok = 1;

out:
	free(g);
	free(e);
	free(e0);
	free(set);
	free(set0);
	return ok;
}

int
rm_rtao_view(const float *proj, const float *world, const rm_viewport_t *vp, const float *eye, float znear,
		float range, float floor_, unsigned frame, unsigned show, rm_rtao_view_t *out)
{
	rm_rts_view_t s;

	memset(out, 0, sizeof(*out));

	/* the shadows' view: the same inverse, viewport, eye and depth bias, so both passes find the same points */
	if (!rm_rts_view(proj, world, vp, eye, znear, &s))
	{
		return 0;
	}

	memcpy(out->inv, s.inv, sizeof(out->inv));
	memcpy(out->vp, s.vp, sizeof(out->vp));
	memcpy(out->eye, s.eye, sizeof(out->eye));
	out->bias_k = s.bias_k;
	out->range = range;
	out->floor_ = floor_;
	out->frame = frame;
	out->show = show;
	return 1;
}

int
rm_rtao_stats_format(const rm_rtao_stats_t *s, char *buf, size_t len)
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

	n = snprintf(buf, len, "ref_metal: rt ao: frame %lu: %lu pixels, %lu traced, %lu occluded, %lu at the floor, "
			"%lu up (%lu occluded), %lu rays, %lu hits, %s ms", s->frame, s->pixels, s->traced, s->occluded, s->floored,
			s->up, s->up_occluded, s->rays, s->hits, ms);

	if ((n < 0) || ((size_t)n >= len))
	{
		buf[0] = 0;
		return 0;
	}

	return 1;
}
