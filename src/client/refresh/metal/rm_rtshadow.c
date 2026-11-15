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
 * The pure part of ref_metal's ray-traced dynamic-light shadows (RT3,
 * TASK55): a stub, so tests/metal/rm_rtshadow_test.c fails first.
 *
 * =======================================================================
 */

#include <string.h>

#include "header/rm_rtshadow.h"

int
rm_rts_on(float shadows, int device_rt, float fullbright, float dynamic, float flashblend)
{
	(void)shadows; (void)device_rt; (void)fullbright; (void)dynamic; (void)flashblend;
	return 0;
}

unsigned
rm_rts_transmit_one(int texflags)
{
	(void)texflags;
	return 0;
}

long
rm_rts_transmit(const rm_surfrec_t *recs, long nrecs, unsigned long nindices, unsigned char *out,
		unsigned long cap, unsigned long *ntrans)
{
	(void)recs; (void)nrecs; (void)nindices; (void)out; (void)cap; (void)ntrans;
	return 0;
}

int
rm_rts_invert(const float *m, float *out)
{
	(void)m; (void)out;
	return 0;
}

int
rm_rts_view(const float *proj, const float *world, const rm_viewport_t *vp, const float *eye, float znear,
		rm_rts_view_t *out)
{
	(void)proj; (void)world; (void)vp; (void)eye; (void)znear;
	memset(out, 0, sizeof(*out));
	return 0;
}

int
rm_rts_unproject(const rm_rts_view_t *v, float px, float py, float depth, float *out)
{
	(void)v; (void)px; (void)py; (void)depth; (void)out;
	return 0;
}

mtl_rts_top_t
rm_rts_select(mtl_rts_v3_t p, mtl_rts_v3_t n, const rm_rts_light_t *l, int nl, unsigned *flags)
{
	(void)p; (void)n; (void)l; (void)nl;
	*flags = 0;
	return mtl_rts_top_init();
}

void
rm_rts_term(mtl_rts_v3_t p, mtl_rts_v3_t n, const rm_rts_light_t *l, int nl, unsigned pidx, unsigned pvis,
		float *rgb)
{
	(void)p; (void)n; (void)l; (void)nl; (void)pidx; (void)pvis;
	rgb[0] = rgb[1] = rgb[2] = 0;
}

int
rm_rts_show(float v)
{
	(void)v;
	return 0;
}

int
rm_rts_stats_format(const rm_rts_stats_t *s, char *buf, size_t len)
{
	(void)s;

	if (len)
	{
		buf[0] = 0;
	}

	return 0;
}
