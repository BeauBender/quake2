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
 * The PVS Decompress
 *
 * =======================================================================
 */

#include "../ref_shared.h"


/*
===================
Mod_DecompressVis

The run-length row at in, which has inlen bytes of the visibility lump
left, into row bytes. Neither end is passed: a row whose input ends inside
it is all visible, as without visibility, and a zero run stops at the
row's end (ref_metal's rm_decompress_vis decodes the same way).
===================
*/
const byte *
Mod_DecompressVis(const byte *in, int inlen, int row)
{
	YQ2_ALIGNAS_TYPE(int) static byte decompressed[MAX_MAP_LEAFS / 8];
	int c, i, o;

	if (row > (int)sizeof(decompressed))
	{
		/* Mod_CheckVis allows at most MAX_MAP_LEAFS clusters */
		row = sizeof(decompressed);
	}

	if (!in)
	{
		/* no vis info, so make all visible */
		memset(decompressed, 0xff, row);
		return decompressed;
	}

	i = 0;
	o = 0;

	while (o < row)
	{
		if (i >= inlen)
		{
			break;
		}

		if (in[i])
		{
			decompressed[o++] = in[i++];
			continue;
		}

		if (i + 1 >= inlen)
		{
			break;
		}

		c = in[i + 1];
		i += 2;

		while (c && (o < row))
		{
			decompressed[o++] = 0;
			c--;
		}
	}

	if (o < row)
	{
		/* the input ended inside the row: everything visible */
		memset(decompressed, 0xff, row);
	}

	return decompressed;
}

/*
===================
Mod_ClusterPVSRow

The PVS row of cluster in vis (vislen bytes), or novis for a map without
visibility and for a cluster outside [0, numclusters): the cluster is a
leaf's, from the map, and Mod_CheckVis put only the first numclusters
offsets inside the lump (ref_metal's Mod_ClusterPVS guards the same way).
===================
*/
const byte *
Mod_ClusterPVSRow(const dvis_t *vis, int vislen, int cluster, const byte *novis)
{
	int ofs;

	if (!vis || (cluster < 0) || (cluster >= vis->numclusters))
	{
		return novis;
	}

	ofs = vis->bitofs[cluster][DVIS_PVS];

	return Mod_DecompressVis((const byte *)vis + ofs, vislen - ofs,
		(vis->numclusters + 7) >> 3);
}

float
Mod_RadiusFromBounds(const vec3_t mins, const vec3_t maxs)
{
	int i;
	vec3_t corner;

	for (i = 0; i < 3; i++)
	{
		corner[i] = fabs(mins[i]) > fabs(maxs[i]) ? fabs(mins[i]) : fabs(maxs[i]);
	}

	return VectorLength(corner);
}
