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
 * The pure arithmetic of the frame core, kept apart from mgl_frame.c
 * (which needs SDL) so that tests/check-metal-shim.sh can compile it
 * on any host: the GL-to-Metal rectangle flip, the read contract and
 * the row and channel conversion of glReadPixels, the matrix product,
 * the unpacking of texture uploads. All are declared in
 * mgl_internal.h. The matrix product and the allocator of the per-frame
 * vertex ring moved to src/client/refresh/metal/common/ (TASK85).
 *
 * =======================================================================
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "mgl_internal.h"

/* ---- viewport, scissor, depth range ------------------------------------ */

void
mgl_compute_raster(const mgl_state_t *s, int tw, int th, mgl_raster_t *r)
{
	long long x0, y0, x1, y1;

	r->x = s->viewport[0];
	r->y = (double)th - s->viewport[1] - s->viewport[3];
	r->w = s->viewport[2];
	r->h = s->viewport[3];
	r->znear = s->depth_near;
	r->zfar = s->depth_far;

	r->scissor_empty = 0;

	if (!(s->enables & MGL_EN_SCISSOR_TEST))
	{
		r->sx = r->sy = 0;
		r->sw = tw;
		r->sh = th;
		return;
	}

	x0 = s->scissor[0];
	y0 = (long long)th - s->scissor[1] - s->scissor[3];
	x1 = x0 + s->scissor[2];
	y1 = y0 + s->scissor[3];

	x0 = x0 < 0 ? 0 : (x0 > tw ? tw : x0);
	x1 = x1 < 0 ? 0 : (x1 > tw ? tw : x1);
	y0 = y0 < 0 ? 0 : (y0 > th ? th : y0);
	y1 = y1 < 0 ? 0 : (y1 > th ? th : y1);

	r->sx = (int)x0;
	r->sy = (int)y0;
	r->sw = (int)(x1 - x0);
	r->sh = (int)(y1 - y0);
	r->scissor_empty = (r->sw <= 0 || r->sh <= 0);
}

/* ---- readback ------------------------------------------------------------- */

GLenum
mgl_read_contract(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format,
		GLenum type, GLint alignment, int tw, int th,
		size_t *bpp, size_t *rowbytes, size_t *total)
{
	unsigned long long row, sum;

	*bpp = *rowbytes = *total = 0;

	if (w < 0 || h < 0)
	{
		return GL_INVALID_VALUE;
	}

	switch (format)
	{
		case GL_RGB:
			*bpp = 3;
			break;
		case GL_RGBA:
		case GL_BGRA:
			*bpp = 4;
			break;
		default:
			return GL_INVALID_ENUM;
	}

	if (type != GL_UNSIGNED_BYTE)
	{
		*bpp = 0;
		return GL_INVALID_ENUM;
	}

	if (alignment != 1 && alignment != 2 && alignment != 4 && alignment != 8)
	{
		return GL_INVALID_VALUE;
	}

	/* The rectangle lies inside the target, or nothing is read. */
	if (x < 0 || y < 0 || (long long)x + w > tw || (long long)y + h > th)
	{
		return GL_INVALID_VALUE;
	}

	if (w == 0 || h == 0)
	{
		return GL_NO_ERROR;
	}

	row = (unsigned long long)w * *bpp;
	row = (row + (unsigned)alignment - 1) / (unsigned)alignment * (unsigned)alignment;
	/* GL needs no padding after the last row. */
	sum = row * (unsigned long long)(h - 1) + (unsigned long long)w * *bpp;

	if (sum > (unsigned long long)SIZE_MAX)
	{
		return GL_OUT_OF_MEMORY;
	}

	*rowbytes = (size_t)row;
	*total = (size_t)sum;
	return GL_NO_ERROR;
}

void
mgl_convert_readback(const unsigned char *bgra, int w, int h, GLenum format,
		size_t bpp, size_t rowbytes, unsigned char *dst)
{
	const unsigned char *src;
	unsigned char *out;
	size_t r, c;

	/* GL row r (bottom up) is row h - 1 - r of what Metal returned. */
	for (r = 0; r < (size_t)h; r++)
	{
		src = bgra + ((size_t)h - 1 - r) * (size_t)w * 4;
		out = dst + r * rowbytes;

		for (c = 0; c < (size_t)w; c++, src += 4, out += bpp)
		{
			if (format == GL_BGRA)
			{
				memcpy(out, src, 4);
				continue;
			}

			out[0] = src[2];
			out[1] = src[1];
			out[2] = src[0];

			if (bpp == 4)
			{
				out[3] = src[3];
			}
		}
	}
}

/* ---- pixel unpacking ------------------------------------------------------- */

void
mgl_unpack_rgba(const unsigned char *src, GLenum format, int w, int h,
		int alignment, int row_length, int opaque, unsigned char *out)
{
	size_t bpp = format == GL_RGB ? 3 : 4;
	size_t stride = (size_t)(row_length > 0 ? row_length : w) * bpp;
	int x, y;

	/* GL pads each source row to the unpack alignment. */
	stride = (stride + (size_t)alignment - 1) / (size_t)alignment * (size_t)alignment;

	for (y = 0; y < h; y++)
	{
		const unsigned char *p = src + (size_t)y * stride;

		for (x = 0; x < w; x++, p += bpp, out += 4)
		{
			out[0] = p[0];
			out[1] = p[1];
			out[2] = p[2];
			out[3] = (bpp == 4 && !opaque) ? p[3] : 255;
		}
	}
}
