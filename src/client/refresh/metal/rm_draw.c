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
 * The 2D entry points of N2, from gl1_draw.c and gl1_main.c
 * (RI_SetPalette, R_SetGL2D): fill, fade, the cinematic frame
 * (DrawStretchRaw) and the palette. The state is gl1's 2D state
 * (R_SetGL2D and R_SetDefaultState): glOrtho(0, vid.width, vid.height,
 * 0, -99999, 99999) over the whole target, no depth, no blending but in
 * the fade, gl1's alpha test, colour 1, 1, 1, 1, texture environment
 * GL_REPLACE. Pics and characters are N3's.
 *
 * =======================================================================
 */

#include "header/local.h"
#include "common/mtl_matrix.h"

unsigned r_rawpalette[256];

/* the cinematic frame's texture, one per size, rewritten each call */
static int cin_texnum;

/*
 * gl1's 2D product: the projection of R_SetGL2D times the identity
 * modelview, multiplied as the shim does for every draw.
 */
static void
RM_Mvp2D(float *mvp)
{
	float proj[16], ident[16];

	mtl_mat_identity(ident);

	if (!mtl_mat_ortho(proj, 0, vid.width, vid.height, 0, -99999, 99999))
	{
		mtl_mat_identity(proj);
	}

	mtl_mat_mul(mvp, proj, ident);
}

/* one of gl1's GL_TRIANGLE_FAN quads, x, y to x + w, y + h */
static void
RM_Quad(rmb_vert2d_t *v, int x, int y, int w, int h, const float *tex, const float *color)
{
	const float px[4] = { x, x + w, x + w, x }, py[4] = { y, y, y + h, y + h };
	int i;

	memset(v, 0, 4 * sizeof(*v));

	for (i = 0; i < 4; i++)
	{
		v[i].pos[0] = px[i];
		v[i].pos[1] = py[i];
		v[i].pos[3] = 1.0f;

		if (tex)
		{
			v[i].uv[0] = tex[i * 2];
			v[i].uv[1] = tex[i * 2 + 1];
		}

		memcpy(v[i].color, color, sizeof(v[i].color));
	}
}

/* from gl1_draw.c */
void
RDraw_Fill(int x, int y, int w, int h, int c)
{
	union
	{
		unsigned c;
		byte v[4];
	} color;
	rmb_vert2d_t v[4];
	float rgba[4], mvp[16];

	if ((unsigned)c > 255)
	{
		ri.Sys_Error(ERR_FATAL, "%s: bad color", __func__);
		return;
	}

	color.c = d_8to24table[c];
	rgba[0] = color.v[0] / 255.0;
	rgba[1] = color.v[1] / 255.0;
	rgba[2] = color.v[2] / 255.0;
	rgba[3] = 1;

	RM_Quad(v, x, y, w, h, NULL, rgba);
	RM_Mvp2D(mvp);
	rmb_draw_2d(rm_pipe_2d(RM_TEXENV_UNTEXTURED, 1, 0), 0, 0, v, 4, mvp);
}

/* from gl1_draw.c */
void
RDraw_FadeScreen(void)
{
	const float rgba[4] = { 0, 0, 0, 0.8 };
	rmb_vert2d_t v[4];
	float mvp[16];

	RM_Quad(v, 0, 0, vid.width, vid.height, NULL, rgba);
	RM_Mvp2D(mvp);
	rmb_draw_2d(rm_pipe_2d(RM_TEXENV_UNTEXTURED, 1, 1), 0, 0, v, 4, mvp);
}

/*
 * from gl1_draw.c, the path gl1 takes on the shim (non-power-of-two
 * textures, no palettes): the frame is uploaded at its own size, opaque
 * (gl1's solid internal format), with texture coordinates 0 to 1.
 */
void
RDraw_StretchRaw(int x, int y, int w, int h, int cols, int rows, const byte *data, int bits)
{
	static const float tex[8] = { 0, 0, 1, 0, 1, 1, 0, 1 };
	static const float white[4] = { 1, 1, 1, 1 };
	static unsigned image32[320 * 240]; /* gl1's, was 256 * 256 */
	unsigned *img = image32;
	rmb_vert2d_t v[4];
	float mvp[16];
	size_t bytes;
	int smp;

	/* the size check comes before any conversion or upload: an oversized frame is skipped */
	if (!data || ((bits != 8) && (bits != 32)) || !rm_upload_bytes(cols, rows, RMB_STAGING_BYTES, &bytes))
	{
		rmb_texture_upload(0, NULL, cols, rows, 0, 1);   /* counted as a skipped upload */
		return;
	}

	if (bits == 32)
	{
		cin_texnum = rmb_texture_upload(cin_texnum, data, cols, rows, 0, 1);
	}
	else
	{
		if (bytes > sizeof(image32))
		{
			/* in case there is a bigger video after all */
			img = malloc(bytes);

			if (!img)
			{
				R_Printf(PRINT_ALL, "%s: Couldn't malloc %zu bytes\n", __func__, bytes);
				return;
			}
		}

		if (rm_raw8_to_rgba(data, cols, rows, r_rawpalette, (unsigned char *)img,
				img == image32 ? sizeof(image32) : bytes))
		{
			cin_texnum = rmb_texture_upload(cin_texnum, (const unsigned char *)img, cols, rows, 0, 1);
		}

		if (img != image32)
		{
			free(img);
		}
	}

	if (!cin_texnum)
	{
		return;
	}

	/* gl_filter_max, or GL_NEAREST with r_videos_unfiltered; no mipmaps; texture 0 repeats */
	smp = RMB_SAMPLER(r_videos_unfiltered->value == 0 ? RMB_SMP_FLAT : RMB_SMP_NEAREST, 0);
	RM_Quad(v, x, y, w, h, tex, white);
	RM_Mvp2D(mvp);
	rmb_draw_2d(rm_pipe_2d(RM_TEXENV_REPLACE, 1, 0), cin_texnum, smp, v, 4, mvp);
}

/* from gl1_main.c */
void
RI_SetPalette(const unsigned char *palette)
{
	static const float black[4] = { 0, 0, 0, 0 };
	byte *rp = (byte *)r_rawpalette;
	int i;

	if (palette)
	{
		for (i = 0; i < 256; i++)
		{
			rp[i * 4 + 0] = gammatable[palette[i * 3 + 0]];
			rp[i * 4 + 1] = gammatable[palette[i * 3 + 1]];
			rp[i * 4 + 2] = gammatable[palette[i * 3 + 2]];
			rp[i * 4 + 3] = 0xff;
		}
	}
	else
	{
		for (i = 0; i < 256; i++)
		{
			rp[i * 4 + 0] = LittleLong(d_8to24table[i]) & 0xff;
			rp[i * 4 + 1] = (LittleLong(d_8to24table[i]) >> 8) & 0xff;
			rp[i * 4 + 2] = (LittleLong(d_8to24table[i]) >> 16) & 0xff;
			rp[i * 4 + 3] = 0xff;
		}
	}

	/* glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT) */
	rmb_clear(black);
}

void
RM_ShutdownDraw(void)
{
	rmb_texture_free(cin_texnum);
	cin_texnum = 0;
}
