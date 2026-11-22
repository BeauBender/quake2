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
 * The pure part of ref_metal's Retro texture upload and its low-resolution
 * 3D view (TASK185, PM3; docs/plans/presentation-modes.md, sections 5.2):
 * the texels of an image quantized to RGB565 (opaque) or ARGB4444 (with
 * alpha) with the 4 x 4 ordered dither of mtl_retro_math.h and kept in 8
 * bits a channel (the existing BGRA8 textures hold them), the 256 x 256
 * cap, the sampler's filter name and the 3D view's scale for 480 lines.
 * Plain C99, no engine state; tests/metal/rm_retro_tex_test.c runs it.
 *
 * =======================================================================
 */

#ifndef MTL_RETRO_TEX_H
#define MTL_RETRO_TEX_H

#include <stddef.h>
#include <string.h>

#include "mtl_retro_math.h"

/* the board's texture limit, a side in texels */
#define MTL_RETRO_TEX_CAP 256

/* the 3D view's height in lines (the window's aspect is kept) */
#define MTL_RETRO_LINES 480

/* gl_texturemode's value under Retro: bilinear magnification, the nearest mip level (R_TextureMode's table) */
#define MTL_RETRO_TEXMODE "GL_LINEAR_MIPMAP_NEAREST"

/*
 * The texels px (w by h, 4 bytes each, alpha last) quantized in place.
 * Opaque images (alpha 0) go to RGB565: 5, 6 and 5 bits. Images with alpha
 * go to ARGB4444: 4 bits each, the alpha too, so 0 and 255 stay as they
 * are. The dither rank is the texel's (x, y) in the image.
 */
static inline void
mtl_retro_tex_quantize(unsigned char *px, int w, int h, int alpha)
{
	int x, y;

	for (y = 0; y < h; y++)
	{
		for (x = 0; x < w; x++)
		{
			unsigned char *p = px + ((size_t)y * (size_t)w + (size_t)x) * 4;
			const unsigned r = mtl_retro_bayer((unsigned)x, (unsigned)y);

			p[0] = (unsigned char)mtl_retro_quant(p[0], alpha ? 4u : 5u, r);
			p[1] = (unsigned char)mtl_retro_quant(p[1], alpha ? 4u : 6u, r);
			p[2] = (unsigned char)mtl_retro_quant(p[2], alpha ? 4u : 5u, r);

			if (alpha)
			{
				p[3] = (unsigned char)mtl_retro_quant(p[3], 4u, r);
			}
		}
	}
}

/* the size an image of w by h has under a cap: both sides halved (rounding up) until each is within cap */
static inline void
mtl_retro_cap_dims(int w, int h, int cap, int *ow, int *oh)
{
	while ((cap > 0) && ((w > cap) || (h > cap)))
	{
		w = (w + 1) / 2;
		h = (h + 1) / 2;
	}

	*ow = w;
	*oh = h;
}

/*
 * px (4 bytes a texel) reduced in place to the size mtl_retro_cap_dims
 * gives, by 2 x 2 box averages (an odd side repeats its last texel);
 * *w and *h are the new size. Each output texel is written after its four
 * sources are read and below every source still to read, so one buffer does.
 */
static inline void
mtl_retro_cap_apply(unsigned char *px, int *w, int *h, int cap)
{
	while ((cap > 0) && ((*w > cap) || (*h > cap)))
	{
		const int nw = (*w + 1) / 2, nh = (*h + 1) / 2;
		int x, y, c;

		for (y = 0; y < nh; y++)
		{
			const int y1 = (2 * y + 1 < *h) ? 2 * y + 1 : *h - 1;

			for (x = 0; x < nw; x++)
			{
				const int x1 = (2 * x + 1 < *w) ? 2 * x + 1 : *w - 1;
				const unsigned char *a = px + ((size_t)(2 * y) * (size_t)*w + (size_t)(2 * x)) * 4;
				const unsigned char *b = px + ((size_t)(2 * y) * (size_t)*w + (size_t)x1) * 4;
				const unsigned char *d = px + ((size_t)y1 * (size_t)*w + (size_t)(2 * x)) * 4;
				const unsigned char *e = px + ((size_t)y1 * (size_t)*w + (size_t)x1) * 4;
				unsigned char o[4];

				for (c = 0; c < 4; c++)
				{
					o[c] = (unsigned char)mtl_retro_box(a[c], b[c], d[c], e[c]);
				}

				memcpy(px + ((size_t)y * (size_t)nw + (size_t)x) * 4, o, 4);
			}
		}

		*w = nw;
		*h = nh;
	}
}

/*
 * The fraction of the target's height the 3D view is drawn at: 480 lines
 * over fh, 1 (no scaling) for a target of 480 lines or fewer. The width
 * follows by the same fraction, the window's aspect.
 */
static inline float
mtl_retro_view_scale(int fh)
{
	return (fh > MTL_RETRO_LINES) ? (float)MTL_RETRO_LINES / (float)fh : 1.0f;
}

#endif
