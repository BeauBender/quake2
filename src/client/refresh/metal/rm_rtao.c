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
 * TASK57; header/rm_rtao.h). A stub until the pass is written: the host
 * test fails against it.
 *
 * =======================================================================
 */

#include <string.h>

#include "header/rm_rtao.h"

int
rm_rtao_on(float ao, int device_rt, float fullbright)
{
	(void)ao;
	(void)device_rt;
	(void)fullbright;
	return 0;
}

float
rm_rtao_range(float v)
{
	(void)v;
	return 0;
}

float
rm_rtao_floor(float v)
{
	(void)v;
	return 0;
}

unsigned
rm_rtao_frame(float seed, unsigned long count)
{
	(void)seed;
	(void)count;
	return 0;
}

int
rm_rtao_show(float v)
{
	(void)v;
	return 0;
}

int
rm_rtao_bluenoise(unsigned short *rank)
{
	memset(rank, 0, MTL_RTAO_TILE_N * sizeof(*rank));
	return 0;
}

int
rm_rtao_view(const float *proj, const float *world, const rm_viewport_t *vp, const float *eye, float znear,
		float range, float floor_, unsigned frame, unsigned show, rm_rtao_view_t *out)
{
	(void)proj;
	(void)world;
	(void)vp;
	(void)eye;
	(void)znear;
	(void)range;
	(void)floor_;
	(void)frame;
	(void)show;
	memset(out, 0, sizeof(*out));
	return 0;
}

int
rm_rtao_stats_format(const rm_rtao_stats_t *s, char *buf, size_t len)
{
	(void)s;

	if (len)
	{
		buf[0] = 0;
	}

	return 0;
}
