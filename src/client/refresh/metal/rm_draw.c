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
 * The 2D entry points, from gl1_draw.c and gl1_main.c (RI_SetPalette,
 * R_SetGL2D): fill, fade, the cinematic frame (DrawStretchRaw) and the
 * palette (N2); pics, characters, tile clears and coloured pics (N3). The
 * state is gl1's 2D state (R_SetGL2D and R_SetDefaultState):
 * glOrtho(0, vid.width, vid.height, 0, -99999, 99999) over the whole
 * target, no depth, no blending but in the fade, gl1's alpha test, colour
 * 1, 1, 1, 1, texture environment GL_REPLACE.
 *
 * gl1 collects characters, stretched pics and tiles in a buffer of quads
 * (gl1_buffer.c, R_Buffer2DQuad) and draws it when the texture changes, when
 * it is full and where it calls R_ApplyGLBuffer; the order of drawing
 * between the buffer and the immediate draws (fill and the cinematic do
 * not flush it) is part of what the pins compare, so the batch here is
 * flushed at the same places (RM_Flush2D).
 *
 * =======================================================================
 */

#include "header/local.h"
#include "common/mtl_matrix.h"

unsigned r_rawpalette[256];

/* the cinematic frame's texture, one per size, rewritten each call */
static int cin_texnum;

/* the console characters' sheet (gl1's draw_chars) */
static image_t *draw_chars;

/* the batch of quads: one texture and sampler, RM_BATCH_QUADS at most (gl1's buffer holds 4096) */
static rmb_vert2d_t batch_v[RM_BATCH_QUADS * 4];
static int batch_n, batch_tex, batch_smp;

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

/* the corners of gl1's R_Buffer2DQuad, a fan's order, in vertices of colour rgba */
static void
RM_QuadF(rmb_vert2d_t *v, const rm_quad_t *q, const float *rgba)
{
	const float px[4] = { q->x0, q->x1, q->x1, q->x0 }, py[4] = { q->y0, q->y0, q->y1, q->y1 };
	const float s[4] = { q->s0, q->s1, q->s1, q->s0 }, t[4] = { q->t0, q->t0, q->t1, q->t1 };
	int i;

	memset(v, 0, 4 * sizeof(*v));

	for (i = 0; i < 4; i++)
	{
		v[i].pos[0] = px[i];
		v[i].pos[1] = py[i];
		v[i].pos[3] = 1.0f;
		v[i].uv[0] = s[i];
		v[i].uv[1] = t[i];
		memcpy(v[i].color, rgba, sizeof(v[i].color));
	}
}

/* from gl1_buffer.c, R_ApplyGLBuffer for buf_2d: draws what is queued */
void
RM_Flush2D(void)
{
	float mvp[16];

	if (batch_n == 0)
	{
		return;
	}

	RM_Mvp2D(mvp);
	rmb_draw_2d_quads(rm_pipe_2d(RM_TEXENV_REPLACE, 1, 0), batch_tex, batch_smp, batch_v, batch_n, mvp);
	batch_n = 0;
}

/* the texture a pic is drawn with: its own, or its atlas's, looked up now (a failed upload can have freed it) */
static int
RM_PicTexture(const image_t *gl)
{
	return gl->scrap ? Scrap_Texture(gl->scrapnum) : gl->texnum;
}

/* the sampler a pic is drawn with: its own, or its atlas's */
static int
RM_PicSampler(const image_t *gl)
{
	return gl->scrap ? Scrap_Sampler(gl->scrapnum) : gl->sampler;
}

/*
 * from gl1_buffer.c, R_UpdateGLBuffer and R_Buffer2DQuad: queues a quad of
 * the texture, drawing the batch first when the texture changes or it is
 * full. The batch cannot overflow: a full one is drawn before the next
 * quad is written.
 */
static void
RM_BufferQuad(int tex, int smp, const rm_quad_t *q)
{
	static const float white[4] = { 1, 1, 1, 1 };

	if ((batch_n > 0) && ((batch_tex != tex) || (batch_smp != smp)))
	{
		RM_Flush2D();
	}

	if (!rm_batch_fits(batch_n, 1, RM_BATCH_QUADS))
	{
		RM_Flush2D();
	}

	if (!rm_batch_fits(batch_n, 1, RM_BATCH_QUADS))
	{
		return;   /* cannot happen: the batch is empty after a flush */
	}

	batch_tex = tex;
	batch_smp = smp;
	RM_QuadF(&batch_v[batch_n * 4], q, white);
	batch_n++;
}

/* an immediate draw of the pic's quad (the path gl1 takes outside the buffer) */
static void
RM_DrawPicNow(const image_t *gl, const rm_quad_t *q, rm_texenv_t env, const float *rgba)
{
	rmb_vert2d_t v[4];
	float mvp[16];

	RM_QuadF(v, q, rgba);
	RM_Mvp2D(mvp);
	rmb_draw_2d(rm_pipe_2d(env, 1, 0), RM_PicTexture(gl), RM_PicSampler(gl), v, 4, mvp);
}

/* from gl1_draw.c */
void
RM_InitDraw(void)
{
	/* load console characters */
	draw_chars = R_FindPic("conchars", (findimage_t)R_FindImage);

	if (!draw_chars)
	{
		ri.Sys_Error(ERR_FATAL, "%s: Couldn't load pics/conchars.pcx", __func__);
	}
}

/* from gl1_draw.c */
void
RDraw_CharScaled(int x, int y, int num, float scale)
{
	float st[4];
	rm_quad_t q;

	if (!draw_chars)
	{
		return;
	}

	st[0] = draw_chars->sl; st[1] = draw_chars->tl; st[2] = draw_chars->sh; st[3] = draw_chars->th;

	if (!rm_char_quad(&q, x, y, num, scale, st))
	{
		return;
	}

	if (draw_chars->scrap)
	{
		Scrap_Update();
	}

	RM_BufferQuad(RM_PicTexture(draw_chars), RM_PicSampler(draw_chars), &q);
}

/* from gl1_draw.c */
image_t *
RDraw_FindPic(const char *name)
{
	return R_FindPic(name, (findimage_t)R_FindImage);
}

/* from gl1_draw.c, which answers -1 -1 for a missing pic */
void
RDraw_GetPicSize(int *w, int *h, const char *pic)
{
	const image_t *gl = R_FindPic(pic, (findimage_t)R_FindImage);

	*w = gl ? gl->width : -1;
	*h = gl ? gl->height : -1;
}

/* from gl1_draw.c */
void
RDraw_StretchPic(int x, int y, int w, int h, const char *pic)
{
	image_t *gl = R_FindPic(pic, (findimage_t)R_FindImage);
	float st[4];
	rm_quad_t q;

	if (!gl)
	{
		R_Printf(PRINT_ALL, "%s(): Can't find pic: %s\n", __func__, pic);
		return;
	}

	if (gl->scrap)
	{
		Scrap_Update();
	}

	st[0] = gl->sl; st[1] = gl->tl; st[2] = gl->sh; st[3] = gl->th;
	rm_pic_quad(&q, x, y, w, h, 1.0f, st);
	RM_BufferQuad(RM_PicTexture(gl), RM_PicSampler(gl), &q);
}

/* from gl1_draw.c: an atlas pic joins the batch, the others are drawn at once, in order */
void
RDraw_PicScaled(int x, int y, const char *pic, float factor)
{
	static const float white[4] = { 1, 1, 1, 1 };
	image_t *gl = R_FindPic(pic, (findimage_t)R_FindImage);
	float st[4];
	rm_quad_t q;

	if (!gl)
	{
		R_Printf(PRINT_ALL, "Can't find pic: %s\n", pic);
		return;
	}

	if (gl->scrap)
	{
		Scrap_Update();
	}

	st[0] = gl->sl; st[1] = gl->tl; st[2] = gl->sh; st[3] = gl->th;
	rm_pic_quad(&q, x, y, gl->width, gl->height, factor, st);

	if (gl->scrap)
	{
		RM_BufferQuad(RM_PicTexture(gl), RM_PicSampler(gl), &q);
		return;
	}

	RM_Flush2D();   /* respect drawing order */
	RM_DrawPicNow(gl, &q, RM_TEXENV_REPLACE, white);
}

/* from gl1_draw.c: the pic modulated by the colour (GL_MODULATE), alpha 1 */
void
RDraw_PicScaledCol(int x, int y, const char *pic, float factor, const float color[3])
{
	image_t *gl = R_FindPic(pic, (findimage_t)R_FindImage);
	float st[4], rgba[4];
	rm_quad_t q;

	if (!gl)
	{
		R_Printf(PRINT_ALL, "Can't find pic: %s\n", pic);
		return;
	}

	if (gl->scrap)
	{
		Scrap_Update();
	}

	st[0] = gl->sl; st[1] = gl->tl; st[2] = gl->sh; st[3] = gl->th;
	rm_pic_quad(&q, x, y, gl->width, gl->height, factor, st);
	rm_tint_color(rgba, color);
	RM_Flush2D();
	RM_DrawPicNow(gl, &q, RM_TEXENV_MODULATE, rgba);
}

/* from gl1_draw.c: repeats a pic over the box, the last column and row clipped */
void
RDraw_TileClear(int x, int y, int w, int h, const char *pic)
{
	const image_t *image = R_FindPic(pic, (findimage_t)R_FindImage);
	float st[4];
	long x2, y2;

	if (!image)
	{
		R_Printf(PRINT_ALL, "%s(): Can't find pic: %s\n", __func__, pic);
		return;
	}

	if (image->scrap)
	{
		Scrap_Update();
	}

	if ((image->width < 1) || (image->height < 1))
	{
		return;
	}

	st[0] = image->sl; st[1] = image->tl; st[2] = image->sh; st[3] = image->th;

	for (x2 = 0; x2 < w; x2 += image->width)
	{
		for (y2 = 0; y2 < h; y2 += image->height)
		{
			rm_quad_t q;

			if (rm_tile_quad(&q, x, y, w, h, image->width, image->height, x2, y2, st))
			{
				RM_BufferQuad(RM_PicTexture(image), RM_PicSampler(image), &q);
			}
		}
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

	RM_Flush2D();   /* draw what needs to be hidden */
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
		rmb_upload_refused();
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
	batch_n = 0;
	draw_chars = NULL;
	Scrap_Shutdown();
	rmb_texture_free(cin_texnum);
	cin_texnum = 0;
}
