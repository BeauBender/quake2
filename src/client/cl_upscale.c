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
 * The local texture upscaler's checks and pixel arithmetic, apart from
 * the console command (cl_upscale_cmd.c) so that the unit tests run
 * them: the names a listing hands it, the git work-tree refusal, the
 * size checks of .wal and .pcx sources, and scale2x on palette indices
 * with wrapped or clamped edges.
 *
 * scale2x, the skin flood fill and the 8-bit conversion are copies of
 * scale2x, R_FloodFillSkin and R_Convert8to32 in
 * src/client/refresh/files/image.c, which the renderers link and the
 * client does not: the PNG written holds what a renderer makes of the
 * scaled indices.
 *
 * =======================================================================
 */

#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>

#include "header/client.h"
#include "header/upscale.h"

/* ---- names ---- */

static int
CL_UpscaleNameChar(int c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		(c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' ||
		c == '/';
}

int
CL_UpscaleNameOk(const char *name)
{
	const char *p;

	if (!name || !name[0] || (name[0] == '/') || strstr(name, ".."))
	{
		return 0;
	}

	for (p = name; *p; p++)
	{
		if (!CL_UpscaleNameChar((unsigned char)*p))
		{
			return 0;
		}

		/* a component that begins with a dot ("./", ".x") */
		if ((*p == '.') && ((p == name) || (p[-1] == '/')))
		{
			return 0;
		}

		/* an empty component ("a//b", "a/") */
		if ((*p == '/') && ((p[1] == '/') || (p[1] == '\0')))
		{
			return 0;
		}
	}

	return 1;
}

int
CL_UpscaleOutName(const char *name, char *out, size_t outsize)
{
	const char *dot, *slash;
	size_t stem;

	if (!out || (outsize == 0))
	{
		return 0;
	}

	out[0] = '\0';

	if (!CL_UpscaleNameOk(name))
	{
		return 0;
	}

	dot = strrchr(name, '.');
	slash = strrchr(name, '/');

	if (!dot || (slash && (dot < slash)) || (dot[1] == '\0'))
	{
		return 0;
	}

	/* the name up to and with its last dot, then png and the terminator */
	stem = (size_t)(dot - name) + 1;

	if ((stem + 4 > outsize) || (stem + 4 > MAX_QPATH))
	{
		return 0;
	}

	memcpy(out, name, stem);
	memcpy(out + stem, "png", 4);

	return 1;
}

/* ---- the git work-tree refusal ---- */

int
CL_UpscaleInGitTree(const char *dir,
		int (*exists)(const char *path, void *ctx), void *ctx)
{
	char path[MAX_OSPATH];
	char probe[MAX_OSPATH + 8];
	size_t len;
	int depth;

	if (!dir || (dir[0] != '/') || !exists)
	{
		return 1;
	}

	len = strlen(dir);

	if (len >= sizeof(path))
	{
		return 1;
	}

	memcpy(path, dir, len + 1);

	/* the directory, then each parent up to the root, at most this many */
	for (depth = 0; depth <= UPSCALE_GIT_DEPTH; depth++)
	{
		char *cut;

		while ((len > 1) && (path[len - 1] == '/'))
		{
			path[--len] = '\0';
		}

		Com_sprintf(probe, sizeof(probe), "%s/.git", (len == 1) ? "" : path);

		if (exists(probe, ctx))
		{
			return 1;
		}

		if (len == 1)
		{
			/* the root, and no .git on the way */
			return 0;
		}

		cut = strrchr(path, '/');
		len = (cut > path) ? (size_t)(cut - path) : 1;
		path[len] = '\0';
	}

	/* deeper than the bound: not walked to the root */
	return 1;
}

int
CL_UpscaleEntryExists(const char *path, void *ctx)
{
	struct stat st;

	(void)ctx;

#ifdef _WIN32
	if (stat(path, &st) == 0)
#else
	if (lstat(path, &st) == 0)
#endif
	{
		return 1;
	}

	/* only "there is no such entry" is an answer */
	return (errno != ENOENT) && (errno != ENOTDIR);
}

int
CL_UpscaleDirInGitTree(const char *dir)
{
	char real[MAX_OSPATH];

	if (!dir || !Sys_Realpath(dir, real, sizeof(real)) ||
		(strlen(real) >= sizeof(real) - 1))
	{
		return 1;
	}

	return CL_UpscaleInGitTree(real, CL_UpscaleEntryExists, NULL);
}

/* ---- sizes ---- */

int
CL_UpscaleSizeOk(long long w, long long h)
{
	return (w >= 1) && (h >= 1) &&
		(w <= UPSCALE_MAX_SIDE) && (h <= UPSCALE_MAX_SIDE);
}

int
CL_UpscaleWalPixels(const unsigned char *data, size_t len, int *w, int *h,
		size_t *offset)
{
	miptex_t mt;
	long long mw, mh;
	size_t ofs;

	if (!data || (len < sizeof(mt)))
	{
		return 0;
	}

	memcpy(&mt, data, sizeof(mt));
	mw = (unsigned)LittleLong((int)mt.width);
	mh = (unsigned)LittleLong((int)mt.height);
	ofs = (unsigned)LittleLong((int)mt.offsets[0]);

	/* the first mip level after the header, all of it in the file */
	if (!CL_UpscaleSizeOk(mw, mh) || (ofs < sizeof(mt)) || (ofs > len) ||
		((size_t)(mw * mh) > len - ofs))
	{
		return 0;
	}

	*w = (int)mw;
	*h = (int)mh;
	*offset = ofs;

	return 1;
}

int
CL_UpscalePcxSize(const unsigned char *data, size_t len, int *w, int *h)
{
	pcx_t pcx;
	long long pw, ph;

	if (!data || (len <= offsetof(pcx_t, data)))
	{
		return 0;
	}

	memcpy(&pcx, data, offsetof(pcx_t, data));
	pw = (long long)(unsigned short)LittleShort((short)pcx.xmax) -
		(unsigned short)LittleShort((short)pcx.xmin) + 1;
	ph = (long long)(unsigned short)LittleShort((short)pcx.ymax) -
		(unsigned short)LittleShort((short)pcx.ymin) + 1;

	/* what PCX_Decode reads as 8-bit indices: one plane of 8 bits */
	if ((pcx.manufacturer != 0x0a) || (pcx.version != 5) ||
		(pcx.encoding != 1) || (pcx.bits_per_pixel != 8) ||
		(pcx.color_planes != 1) || !CL_UpscaleSizeOk(pw, ph))
	{
		return 0;
	}

	*w = (int)pw;
	*h = (int)ph;

	return 1;
}

/* ---- pixels ---- */

int
CL_UpscaleWrapPad(const unsigned char *src, int w, int h, unsigned char *dst)
{
	int pw = w + 2, x, y;

	if ((w < 1) || (h < 1))
	{
		return 0;
	}

	/* padded (x, y) is source ((x - 1) mod w, (y - 1) mod h) */
	for (y = 0; y < h + 2; y++)
	{
		int sy = (y + h - 1) % h;

		for (x = 0; x < pw; x++)
		{
			dst[y * pw + x] = src[sy * w + (x + w - 1) % w];
		}
	}

	return 1;
}

int
CL_UpscaleCrop(const unsigned char *src, int sw, int sh, int x, int y,
		int w, int h, unsigned char *dst)
{
	int row;

	if ((x < 0) || (y < 0) || (w < 1) || (h < 1) ||
		(x > sw - w) || (y > sh - h))
	{
		return 0;
	}

	for (row = 0; row < h; row++)
	{
		memcpy(dst + (size_t)row * w, src + (size_t)(y + row) * sw + x,
			(size_t)w);
	}

	return 1;
}

/* https://en.wikipedia.org/wiki/Pixel-art_scaling_algorithms */
void
CL_UpscaleScale2x(const unsigned char *src, unsigned char *dst, int width,
		int height)
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
		const unsigned char *in_buff = src;
		unsigned char *out_buff = dst;
		const unsigned char *out_buff_full = dst + ((width * height) << 2);

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
				unsigned char a, b, c, d, p;

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

unsigned char *
CL_UpscaleIndices(const unsigned char *src, int w, int h, int factor,
		int wrap)
{
	unsigned char *img;
	int pass;

	if (!src || !CL_UpscaleSizeOk(w, h) || ((factor != 2) && (factor != 4)))
	{
		return NULL;
	}

	img = malloc((size_t)w * h);

	if (!img)
	{
		return NULL;
	}

	memcpy(img, src, (size_t)w * h);

	/* one scale2x per doubling: 2x once, 4x twice */
	for (pass = 0; pass < factor / 2; pass++)
	{
		/* a wrapped tile is scaled with one texel of border, then cropped */
		int border = wrap ? 1 : 0;
		int sw = w + 2 * border, sh = h + 2 * border;
		unsigned char *in = img, *out, *next;

		if (border)
		{
			in = malloc((size_t)sw * sh);

			if (!in)
			{
				free(img);
				return NULL;
			}

			CL_UpscaleWrapPad(img, w, h, in);
		}

		out = malloc((size_t)sw * sh * 4);

		if (!out)
		{
			if (in != img)
			{
				free(in);
			}

			free(img);
			return NULL;
		}

		CL_UpscaleScale2x(in, out, sw, sh);

		if (in != img)
		{
			free(in);
		}

		next = out;

		if (border)
		{
			next = malloc((size_t)w * h * 4);

			if (!next || !CL_UpscaleCrop(out, 2 * sw, 2 * sh, 2 * border,
				2 * border, 2 * w, 2 * h, next))
			{
				free(next);
				free(out);
				free(img);
				return NULL;
			}

			free(out);
		}

		free(img);
		img = next;
		w *= 2;
		h *= 2;
	}

	return img;
}

/* ---- skins and the palette ---- */

typedef struct
{
	short x, y;
} upscale_fill_t;

/* must be a power of 2 */
#define UPSCALE_FIFO_SIZE 0x1000
#define UPSCALE_FIFO_MASK (UPSCALE_FIFO_SIZE - 1)

#define UPSCALE_FILL_STEP(off, dx, dy) \
	{ \
		if (pos[off] == fillcolor) \
		{ \
			pos[off] = 255; \
			fifo[inpt].x = x + (dx), fifo[inpt].y = y + (dy); \
			inpt = (inpt + 1) & UPSCALE_FIFO_MASK; \
		} \
		else if (pos[off] != 255) \
		{ \
			fdc = pos[off]; \
		} \
	}

void
CL_UpscaleFloodFillSkin(unsigned char *skin, int skinwidth, int skinheight,
		const unsigned *table_8to24)
{
	unsigned char fillcolor = *skin; /* assume this is the pixel to fill */
	static upscale_fill_t fifo[UPSCALE_FIFO_SIZE];
	int inpt = 0, outpt = 0;
	int filledcolor = 0;
	int i;

	/* attempt to find opaque black, as R_FloodFillSkin does */
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
	inpt = (inpt + 1) & UPSCALE_FIFO_MASK;

	while (outpt != inpt)
	{
		int x = fifo[outpt].x, y = fifo[outpt].y;
		int fdc = filledcolor;
		unsigned char *pos = &skin[x + skinwidth * y];

		outpt = (outpt + 1) & UPSCALE_FIFO_MASK;

		if (x > 0)
		{
			UPSCALE_FILL_STEP(-1, -1, 0);
		}

		if (x < skinwidth - 1)
		{
			UPSCALE_FILL_STEP(1, 1, 0);
		}

		if (y > 0)
		{
			UPSCALE_FILL_STEP(-skinwidth, 0, -1);
		}

		if (y < skinheight - 1)
		{
			UPSCALE_FILL_STEP(skinwidth, 0, 1);
		}

		skin[x + skinwidth * y] = fdc;
	}
}

void
CL_UpscaleTable(const unsigned char *palette, unsigned *table)
{
	int i;

	for (i = 0; i < 256; i++)
	{
		unsigned char *t = (unsigned char *)&table[i];

		t[0] = palette[i * 3 + 0];
		t[1] = palette[i * 3 + 1];
		t[2] = palette[i * 3 + 2];
		t[3] = (i == 255) ? 0 : 255;
	}
}

void
CL_UpscaleToRGBA(const unsigned char *data, size_t w, size_t h,
		const unsigned *table, unsigned char *rgba)
{
	size_t i, s = w * h;

	for (i = 0; i < s; i++)
	{
		/* an index is a byte: at most 255, the table has 256 entries */
		unsigned char p = data[i];

		memcpy(rgba + i * 4, &table[p], 4);

		/* transparent, so scan around for
		   another color to avoid alpha fringes */
		if (p == 255)
		{
			if ((i > w) && (data[i - w] != 255))
			{
				p = data[i - w];
			}
			else if ((i < s - w) && (data[i + w] != 255))
			{
				p = data[i + w];
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
			memcpy(rgba + i * 4, &table[p], 3);
		}
	}
}
