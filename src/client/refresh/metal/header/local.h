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
 * The front end of ref_metal: its globals and the functions its C files
 * share (docs/plans/ref-metal-native.md, section 2). No graphics API type
 * and no graphics API header; the back end is reached through rmb.h.
 *
 * =======================================================================
 */

#ifndef RM_LOCAL_H
#define RM_LOCAL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../ref_shared.h"
#include "rmb.h"

#define REF_VERSION DISPLAYNAME " Metal Refresher"

/* the cvars, by gl1's names where gl1 has them */
extern cvar_t *r_clear;
extern cvar_t *r_mode;
extern cvar_t *r_customwidth;
extern cvar_t *r_customheight;
extern cvar_t *r_norefresh;
extern cvar_t *r_vsync;
extern cvar_t *vid_fullscreen;
extern cvar_t *metal_stats;

/* calls to entry points that do nothing yet (rm_main.c) */
extern unsigned long rm_noop_calls;

/* rm_sdl.c: the window seat */
extern qboolean IsHighDPIaware;
int RI_PrepareForWindow(void);
int RI_InitContext(void *win);
void RI_GetDrawableSize(int *width, int *height);
void RI_ShutdownContext(void);
qboolean RI_IsVSyncActive(void);
int RI_GetSDLVersion(void);

extern cvar_t *gl_texturemode;
extern cvar_t *gl_anisotropic;
extern cvar_t *gl_nolerp_list;
extern cvar_t *r_lerp_list;
extern cvar_t *r_2D_unfiltered;
extern cvar_t *r_videos_unfiltered;
extern cvar_t *r_retexturing;
extern cvar_t *r_validation;
extern cvar_t *vid_gamma;

/* from gl1's header/local.h, without the GL fields; a scrap image's texnum is its atlas's texture */
#define MAX_GLTEXTURES 1024

typedef struct image_s
{
	char name[MAX_QPATH];               /* game path, including extension */
	imagetype_t type;
	int width, height;                  /* source image */
	int upload_width, upload_height;    /* as uploaded: the source size, no power of two */
	int registration_sequence;          /* 0 = free */
	struct msurface_s *texturechain;    /* for sort-by-texture world drawing */
	int texnum;                         /* the back end's texture id, 0 = free slot (an atlas's, for a scrap image) */
	int sampler;                        /* RMB_SAMPLER(class, clamp) */
	float sl, tl, sh, th;               /* 0,0 - 1,1 unless part of the scrap */
	qboolean scrap;                     /* part of an atlas (rm_scrap.c), sl, tl, sh, th inside it */
	qboolean has_alpha;
} image_t;

extern image_t gltextures[MAX_GLTEXTURES];
extern int numgltextures;
extern unsigned d_8to24table[256];
extern unsigned r_rawpalette[256];
extern byte gammatable[256];

/* rm_image.c */
void R_InitImages(void);
void R_ShutdownImages(void);
void R_FreeUnusedImages(void);
void R_BeginImageRegistration(void);
void R_TextureMode(const char *string);
void R_ImageList_f(void);
void R_LightScaleTexture(unsigned *in, int inwidth, int inheight, qboolean only_gamma);
image_t *R_FindImage(const char *name, imagetype_t type);
image_t *R_LoadPic(const char *name, byte *pic, int width, int realwidth,
		int height, int realheight, size_t data_size, imagetype_t type, int bits);

/* rm_scrap.c */
void Scrap_Init(void);
void Scrap_Shutdown(void);
int Scrap_AllocBlock(unsigned w, unsigned h, int *x, int *y, const unsigned *pic, int scrap_offset);
int Scrap_Texture(int texnum);
int Scrap_Sampler(int tex);
void Scrap_Update(void);

/* rm_draw.c */
void RM_InitDraw(void);
void RM_ShutdownDraw(void);
/* draws the batched 2D quads, as gl1's R_ApplyGLBuffer does where it is called */
void RM_Flush2D(void);
image_t *RDraw_FindPic(const char *name);
void RDraw_GetPicSize(int *w, int *h, const char *name);
void RDraw_PicScaled(int x, int y, const char *pic, float factor);
void RDraw_PicScaledCol(int x, int y, const char *pic, float factor, const float color[3]);
void RDraw_StretchPic(int x, int y, int w, int h, const char *pic);
void RDraw_CharScaled(int x, int y, int num, float scale);
void RDraw_TileClear(int x, int y, int w, int h, const char *pic);
void RDraw_Fill(int x, int y, int w, int h, int c);
void RDraw_FadeScreen(void);
void RDraw_StretchRaw(int x, int y, int w, int h, int cols, int rows, const byte *data, int bits);
void RI_SetPalette(const unsigned char *palette);

/* rm_misc.c */
void R_ScreenShot(void);
void RM_PrintStats(void);
/* the directory of ref_metal.dylib, where ref_metal.metallib lies; 0 when unknown */
int RM_ModuleDir(char *dir, size_t len);

#endif /* RM_LOCAL_H */
