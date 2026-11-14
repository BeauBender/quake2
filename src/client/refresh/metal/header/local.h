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

/* the world and the view (N4), by gl1's names */
extern cvar_t *r_drawworld;
extern cvar_t *r_novis;
extern cvar_t *r_lockpvs;
extern cvar_t *r_cull;
extern cvar_t *r_fullbright;
extern cvar_t *r_fixsurfsky;
extern cvar_t *r_farsee;
extern cvar_t *gl_znear;
extern cvar_t *gl_polyblend;
extern cvar_t *gl1_waterwarp;

/* the lightmaps (N5), by gl1's names */
extern cvar_t *r_modulate;
extern cvar_t *gl1_minlight;
extern cvar_t *gl1_overbrightbits;
extern cvar_t *gl_lightmap;     /* r_lightmap */
extern cvar_t *gl1_dynamic;
extern cvar_t *gl1_flashblend;
extern cvar_t *r_lightlevel;

/* from gl1's header/local.h, without the GL fields; a scrap image has no texture of its own (texnum RM_TEXNUM_SCRAP) */
#define MAX_GLTEXTURES 1024
#define RM_TEXNUM_SCRAP -1               /* a scrap image's texnum: the slot is used, its texture is the atlas's */

typedef struct image_s
{
	char name[MAX_QPATH];               /* game path, including extension */
	imagetype_t type;
	int width, height;                  /* source image */
	int upload_width, upload_height;    /* as uploaded: the source size, no power of two */
	int registration_sequence;          /* 0 = free */
	struct msurface_s *texturechain;    /* for sort-by-texture world drawing */
	int texnum;                         /* the back end's texture id, 0 = free slot, RM_TEXNUM_SCRAP for a scrap image */
	int sampler;                        /* RMB_SAMPLER(class, clamp) */
	float sl, tl, sh, th;               /* 0,0 - 1,1 unless part of the scrap */
	qboolean scrap;                     /* part of an atlas (rm_scrap.c), sl, tl, sh, th inside it */
	int scrapnum;                       /* the atlas index of a scrap image, its texture looked up at each draw */
	qboolean has_alpha;
} image_t;

/* the lightmap block of gl1 (BLOCK_WIDTH, BLOCK_HEIGHT), which the lightmap coordinates are relative to */
#define RM_LM_BLOCK_WIDTH RM_LM_WIDTH
#define RM_LM_BLOCK_HEIGHT RM_LM_HEIGHT

#include "model.h"

extern image_t gltextures[MAX_GLTEXTURES];
extern image_t *r_notexture;
extern int registration_sequence;
extern float rm_sw_gamma;
extern float rm_inverse_intensity_value;
extern int numgltextures;
extern unsigned d_8to24table[256];
extern unsigned r_rawpalette[256];
extern byte gammatable[256];

/* rm_image.c */
void R_InitImages(void);
void R_ShutdownImages(void);
void R_FreeUnusedImages(void);
qboolean R_ImageHasFreeSpace(void);
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
int Scrap_Sampler(int texnum);
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

/* rm_surf.c: the world */
extern int r_framecount, r_visframecount;
extern cplane_t frustum[4];
extern vec3_t vup, vpn, vright, r_origin;
void R_MarkLeaves(void);
/* the view a frame's 3D draws share: gl1's world matrix and projection, their product, the viewport and the culling */
typedef struct
{
	float world[16], proj[16], mvp[16];
	rm_viewport_t vp;
	int cull;
} rm_view_t;

/* draws the world (sky box included) in view */
void R_DrawWorld(const rm_view_t *view);
/* gl1's R_DrawAlphaSurfaces: the translucent surfaces of the walk, blended, last to first */
void R_DrawAlphaSurfaces(const rm_view_t *view);
/* the surfaces of the last view R_DrawWorld skipped, by class, into s (metal_stats) */
void RM_WorldSkips(rmb_stats_t *s);
void RM_WorldSkipsClear(void);

/* rm_warp.c (gl1_warp.c): the sky box, the warped water and the batches of per-frame vertices */
void RM_SetSky(const char *name, float rotate, vec3_t axis);
void RM_SkyClear(void);
int RM_AddSkySurface(const rm_world_t *w, const msurface_t *fa);
int RM_DrawSkyBox(const rm_view_t *view);
int RM_SubdivideSurface(model_t *loadmodel, rm_world_t *w, msurface_t *fa);
int RM_EmitWaterPolys(const rm_world_t *w, const msurface_t *fa);
void RM_DynBegin(const rm_view_t *view, unsigned long *skips);
void RM_DynFlush(void);
void RM_DynState(const rmb_dyn_t *d);
void RM_DynSurface(void);
int RM_DynPoly(const rm_world_t *w, const msurface_t *fa, float scroll);

/* rm_lightmap.c: the lightmap slices of the world (gl1_lightmap.c) */
void LM_FreeLightmapBuffers(void);
qboolean LM_BeginBuildingLightmaps(void);
qboolean LM_CreateSurfaceLightmap(msurface_t *surf);
int LM_EndBuildingLightmaps(void);
int LM_UploadSlices(int slices);
byte *LM_SliceBuffer(int slice);

/* rm_light.c (gl1_light.c) */
void R_InitLight(void);
void R_SetCacheState(msurface_t *surf);
qboolean R_BuildLightMap(msurface_t *surf, byte *dest, int stride);
void R_MarkSurfaceLights(dlight_t *light, int bit, mnode_t *node, int lightframecount);
void R_PushDlights(void);
void R_LightPoint(entity_t *currententity, vec3_t p, vec3_t color);
void R_SetLightLevel(entity_t *currententity);

/* rm_misc.c */
void R_InitBuiltinTextures(void);
void R_ScreenShot(void);
void RM_PrintStats(void);
/* the directory of ref_metal.dylib, where ref_metal.metallib lies; 0 when unknown */
int RM_ModuleDir(char *dir, size_t len);

#endif /* RM_LOCAL_H */
