/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 * Copyright (C) 2015 Daniel Gibson
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
 * Three pixel helpers of the 8-bit images, shared by the renderers and
 * the client's texture upscaler (cl_upscale.c): scale2x, R_FloodFillSkin
 * and R_Convert8to32. They use nothing of a renderer (no ri, no image
 * state), only the shared engine code, so every module that links this
 * file has the same bytes.
 *
 * =======================================================================
 */

#include <limits.h>
#include <stdlib.h>

#include "imgscale.h"

/* https://en.wikipedia.org/wiki/Pixel-art_scaling_algorithms */

void
scale2x(const byte *src, byte *dst, int width, int height)
{
	/*
		EPX/Scale2×/AdvMAME2×

		x A x
		C P B -> 1 2
		x D x    3 4

		1=P; 2=P; 3=P; 4=P;
		IF C==A AND C!=D AND A!=B => 1=A
		IF A==B AND A!=C AND B!=D => 2=B
		IF D==C AND D!=B AND C!=A => 3=C
		IF B==D AND B!=A AND D!=C => 4=D
	*/
	{
		const byte *in_buff = src;
		byte *out_buff = dst;
		const byte *out_buff_full = dst + ((width * height) << 2);

		while (out_buff < out_buff_full)
		{
			int x;
			for (x = 0; x < width; x ++)
			{
				// copy one source byte to two destinatuion bytes
				*out_buff = *in_buff;
				out_buff ++;
				*out_buff = *in_buff;
				out_buff ++;

				// next source pixel
				in_buff ++;
			}
			// copy last line one more time
			memcpy(out_buff, out_buff - (width << 1), width << 1);
			out_buff += width << 1;
		}
	}

	{
		int y, h, w;
		h = height - 1;
		w = width - 1;
		for (y = 0; y < height; y ++)
		{
			int x;
			for (x = 0; x < width; x ++)
			{
				byte a, b, c, d, p;

				p = src[(width * (y    )) + (x    )];
				a = (y > 0) ? src[(width * (y - 1)) + (x    )] : p;
				b = (x < w) ? src[(width * (y    )) + (x + 1)] : p;
				c = (x > 0) ? src[(width * (y    )) + (x - 1)] : p;
				d = (y < h) ? src[(width * (y + 1)) + (x    )] : p;

				if ((c == a) && (c != d) && (a != b))
				{
					dst[(2 * width * ((y * 2)    )) + ((x * 2)    )] = a;
				}

				if ((a == b) && (a != c) && (b != d))
				{
					dst[(2 * width * ((y * 2)    )) + ((x * 2) + 1)] = b;
				}

				if ((d == c) && (d != b) && (c != a))
				{
					dst[(2 * width * ((y * 2) + 1)) + ((x * 2)    )] = c;
				}

				if ((b == d) && (b != a) && (d != c))
				{
					dst[(2 * width * ((y * 2) + 1)) + ((x * 2) + 1)] = d;
				}
			}
		}
	}
}



/*
=================
Mod_FloodFillSkin

Fill background pixels so mipmapping doesn't have haloes
=================
*/

typedef struct
{
	short x, y;
} floodfill_t;

/* must be a power of 2 */
#define FLOODFILL_FIFO_SIZE 0x1000
#define FLOODFILL_FIFO_MASK (FLOODFILL_FIFO_SIZE - 1)

#define FLOODFILL_STEP(off, dx, dy)	\
	{ \
		if (pos[off] == fillcolor) \
		{ \
			pos[off] = 255;	\
			fifo[inpt].x = x + (dx), fifo[inpt].y = y + (dy); \
			inpt = (inpt + 1) & FLOODFILL_FIFO_MASK; \
		} \
		else if (pos[off] != 255) \
		{ \
			fdc = pos[off];	\
		} \
	}

/*
 * Fill background pixels so mipmapping doesn't have haloes
 */
void
R_FloodFillSkin(byte *skin, int skinwidth, int skinheight, const unsigned *table_8to24)
{
	byte fillcolor = *skin; /* assume this is the pixel to fill */
	static floodfill_t fifo[FLOODFILL_FIFO_SIZE];
	int inpt = 0, outpt = 0;
	int filledcolor = 0;
	int i;

	// NOTE: there was a if (filledcolor == -1) which didn't make sense b/c filledcolor used to be initialized to -1
	/* attempt to find opaque black */
	for (i = 0; i < 256; ++i)
	{
		if (LittleLong(table_8to24[i]) == (255 << 0)) /* alpha 1.0 */
		{
			filledcolor = i;
			break;
		}
	}

	/* can't fill to filled color or to transparent color (used as visited marker) */
	if ((fillcolor == filledcolor) || (fillcolor == 255))
	{
		return;
	}

	fifo[inpt].x = 0, fifo[inpt].y = 0;
	inpt = (inpt + 1) & FLOODFILL_FIFO_MASK;

	while (outpt != inpt)
	{
		int x = fifo[outpt].x, y = fifo[outpt].y;
		int fdc = filledcolor;
		byte *pos = &skin[x + skinwidth * y];

		outpt = (outpt + 1) & FLOODFILL_FIFO_MASK;

		if (x > 0)
		{
			FLOODFILL_STEP(-1, -1, 0);
		}

		if (x < skinwidth - 1)
		{
			FLOODFILL_STEP(1, 1, 0);
		}

		if (y > 0)
		{
			FLOODFILL_STEP(-skinwidth, 0, -1);
		}

		if (y < skinheight - 1)
		{
			FLOODFILL_STEP(skinwidth, 0, 1);
		}

		skin[x + skinwidth * y] = fdc;
	}
}

unsigned *
R_Convert8to32(const byte *data, size_t width, size_t height, const unsigned *table_8to24)
{
	unsigned *trans;
	size_t i, s;

	if (height == 0 || width > INT_MAX / sizeof(*trans) / height)
	{
		Com_Error(ERR_DROP, "%s: invalid dimensions", __func__);
		return NULL;
	}

	s = width * height;

	trans = malloc(s * sizeof(unsigned));
	YQ2_COM_CHECK_OOM(trans, "malloc()",
		s * sizeof(unsigned))
	if (!trans)
	{
		/* unaware about YQ2_ATTR_NORETURN_FUNCPTR? */
		return NULL;
	}

	for (i = 0; i < s; i++)
	{
		byte p = data[i];
		trans[i] = table_8to24[p];

		/* transparent, so scan around for
		   another color to avoid alpha fringes */
		if (p == 255)
		{
			if ((i > width) && (data[i - width] != 255))
			{
				p = data[i - width];
			}
			else if ((i < s - width) && (data[i + width] != 255))
			{
				p = data[i + width];
			}
			else if ((i > 0) && (data[i - 1] != 255))
			{
				p = data[i - 1];
			}
			else if ((i < s - 1) && (data[i + 1] != 255))
			{
				p = data[i + 1];
			}
			else
			{
				p = 0;
			}

			/* copy rgb components */
			((byte *)&trans[i])[0] = ((byte *)&table_8to24[p])[0];
			((byte *)&trans[i])[1] = ((byte *)&table_8to24[p])[1];
			((byte *)&trans[i])[2] = ((byte *)&table_8to24[p])[2];
		}
	}

	return trans;
}
