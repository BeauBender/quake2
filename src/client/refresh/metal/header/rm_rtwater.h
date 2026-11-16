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
 * Ray-traced water reflections in ref_metal, RT6 (TASK58;
 * docs/plans/metal-raytracing-design.md, section 2). With r_rt_water on,
 * a compute pass after the lighting resolve (so after every other pass,
 * whose lit result it reads) finds each pixel's water: the opaque water
 * the raster drew (material 3), or translucent water in front of the
 * raster's surface by a ray from the eye. From it one reflection ray, its
 * direction about the flat normal tilted by the stock warp's phase
 * (r_rt_water_normal), one bounce. The hit's colour is the lit pixel it
 * is seen at (the lightmap, AO and dynamic-light terms as resolved), the
 * sky pixel in its direction for a sky hit, or none where the screen does
 * not show it. The RGB is blended over the water's colour at
 * r_rt_water_reflect, the water's alpha kept: over opaque water in a pass
 * after the resolve, over translucent water in its own forward draw.
 *
 * The rm_rtw_* functions are plain C with neither SDL nor Metal
 * (rm_rtwater.c), host-tested by tests/metal/rm_rtwater_test.c with the
 * per-pixel arithmetic of common/mtl_rtw_math.h; the rmb_rtw_* functions
 * are the back end's (rmb_rtwater.m).
 *
 * =======================================================================
 */

#ifndef RM_RTWATER_H
#define RM_RTWATER_H

#include <stddef.h>

#include "rmb.h"
#include "../common/mtl_shader_math.h"
#include "../common/mtl_gbuf_math.h"
#include "../common/mtl_rtao_math.h"
#include "../common/mtl_rts_math.h"
#include "../common/mtl_rtw_math.h"

/* ---- rm_rtwater.c: no SDL, no Metal --------------------------------------------------- */

/* the cvars' defaults (design section 2) */
#define RM_RTW_NORMAL 0.08f
#define RM_RTW_REFLECT 0.5f

/* the tilt's bound: at 1 the normal leans up to 55 degrees */
#define RM_RTW_NORMAL_MAX 1.0f

/*
 * 1 when a frame traces the reflections: r_rt_water is not 0, the device
 * has ray tracing, and the map holds water (a warped triangle, of the
 * world or a brush model: rm_rt_materials). A value that is not finite
 * counts as 0.
 */
int rm_rtw_on(float on, int device_rt, unsigned long water_tris);

/* r_rt_water_normal as the pass takes it: 0 to RM_RTW_NORMAL_MAX; RM_RTW_NORMAL when not finite */
float rm_rtw_amp(float v);

/* r_rt_water_reflect as the pass takes it: 0 to 1; RM_RTW_REFLECT when not finite */
float rm_rtw_reflect(float v);

/*
 * r_rt_water_show (development): 0 the picture; 1 the mask of the pixels
 * that must not change, white where no water was found and black on the
 * water (framediff --mask counts the white); 2 the classes (black not
 * water, green a reflection from a lit pixel, blue from the sky, red
 * none). v clamped to 0 to RM_RTW_SHOW_MAX before it is cut to an int; 0
 * for a value that is not finite.
 */
#define RM_RTW_SHOW_MAX 2
int rm_rtw_show(float v);

/*
 * TASK158: r_rt_water_hits, how a reflection's hit is shaded: 0 (the
 * default) TASK58's screen-space path, the kernel over every pixel and a
 * hit's colour the picture's or none; 1 this card's world-space path, the
 * candidates' raster, the compaction and the trace over its list, a hit
 * the picture does not show shaded in world space. 1 from 1 up; 0 below
 * 1 and for a value that is not finite.
 */
int rm_rtw_hits(float v);

/*
 * TASK158: the path a view takes at r_rt_water_hits hits: 1 the
 * world-space path where hits asks for it (rm_rtw_hits) and the pass
 * built its resources (ws_ok, rmb_rtw_ws_ok); else 0, TASK58's, which
 * needs none of them. Where hits asks for it and they were not built,
 * *say is 1 the first time (*told 0, then set), so the refusal is
 * printed once a pass; 0 otherwise.
 */
int rm_rtw_path(float hits, int ws_ok, int *told, int *say);

/*
 * The stock turbulence table as the kernel takes it (MTL_RTW_TURB_SIZE
 * floats): gl1's r_turbsin as its RI_Init halves it (rm_turb_sin), over
 * its largest magnitude, so -1 to 1. 1 with out filled; 0 when the table
 * has no value but 0.
 */
int rm_rtw_turb_table(float *out);

/* the view of the kernel (rm_rtwater.metal's rm_rtw_view, 288 bytes) */
typedef struct
{
	float inv[16];      /* GL clip coordinates to world: the inverse of the jittered projection times the modelview */
	float mvp[16];      /* world to GL clip coordinates: that product itself, as the raster draws */
	float vp[4];        /* the view's viewport at the render size: x, y, w, h in pixels, from the top left */
	float eye[4];       /* the view's origin */
	float bias_k;       /* mtl_rtao_bias's k: 4 x 2^-24 / gl_znear */
	float time;         /* the refdef's time, the warp's (r_newrefdef.time) */
	float amp;          /* rm_rtw_amp */
	float reflect;      /* rm_rtw_reflect */
	unsigned show;      /* rm_rtw_show */

	/* TASK158: the world-space shading of a hit (rm_rtw_shading) */
	int env;            /* gl1's lightmap unit (rm_lm_env), as the resolve's */
	float scale;        /* its scale: the overbright bits, or 1 */
	float lod_k;        /* mtl_rtw_lod's k: 2 / (the viewport's height x proj[5]) */
	float flow;         /* a flowing face's scroll (rm_flow_scroll) */
	unsigned nlights;   /* the dynamic lights the lightmaps left out (RT3 traced), at most MTL_RTS_MAX_LIGHTS */
	unsigned fullbright;   /* r_fullbright: the texel alone */
	unsigned sky;       /* 1 when the sky box's six images are in the table (MTL_RTW_TEX_SKY) */
	float warp[4];      /* a warped face's colour (rm_warp_color, white where it gives none) */
	float trans[4];     /* a translucent face's (rm_trans_color's RGB) */
	float sky_rows[12]; /* the sky's rotation, its rows as three float4s (rm_rtw_sky_rows) */
} rm_rtw_view_t;

/*
 * The view of a G-buffer view: proj (jittered) and world as R_SetupGL and
 * RM_GB_Begin left them, its viewport at the render size, the eye and the
 * near plane's distance (TASK55's rm_rts_view), with the cvars' values as
 * the functions above read them. 0 when the product cannot be inverted or
 * znear is not above 0 (nothing is traced then).
 */
int rm_rtw_view(const float *proj, const float *world, const rm_viewport_t *vp, const float *eye, float znear,
		float time, float amp, float reflect, unsigned show, rm_rtw_view_t *out);

/*
 * TASK158: the world-space shading of the view (the fields after show):
 * gl1's lightmap unit and its scale, the flowing scroll, the warp's and
 * the translucent faces' colours (RGBA, their alpha unused), r_fullbright,
 * the number of dynamic lights the lightmaps left out (clamped to
 * MTL_RTS_MAX_LIGHTS) and the sky (rotation rows, rm_rtw_sky_rows; sky 0
 * for none). The mip factor lod_k comes from proj[5] and the viewport's
 * height: 0 when either is not above 0 or not finite.
 */
typedef struct
{
	int env;
	float scale;
	float flow;
	float warp[4];
	float trans[4];
	int fullbright;
	int nlights;
	int sky;
	float sky_rows[12];
} rm_rtw_shading_t;

void rm_rtw_shading(const rm_rtw_shading_t *sh, const float *proj, rm_rtw_view_t *out);

/*
 * TASK158: the sky's rotation as gl1 turns the box (rm_sky_matrix about
 * the origin: time x rotate degrees about axis), its three rows as float4s
 * (w 0); the identity with rotate 0, a zero axis or a value not finite.
 */
void rm_rtw_sky_rows(float time, float rotate, const float *axis, float *rows);

/*
 * TASK158: the world's textures as the kernel's table holds them: each
 * record's image (img[s], its slot in gltextures, below 0 for none) given
 * an index of the table, the first record of an image its next, at most
 * cap (MTL_RTW_TEX_WORLD). slot[i] the image of index i, tex[s] the index
 * of record s (-1 for none). The number of indices, or -1 when there are
 * more images than cap.
 */
int rm_rtw_tex_slots(const int *img, long nrecs, int *slot, int cap, int *tex);

/*
 * TASK158: the shading records of the world's triangles (mtl_rtw_shade_pack),
 * one a triangle of the index buffer, in the walk of rm_rt_materials: each
 * surface record where the last ended, whole triangles, inside nindices.
 * tex[s] the record's index in the table (rm_rtw_tex_slots; below 0 or
 * not below MTL_RTW_TEX_WORLD for none), its lightmap slice (none below 0
 * or above 254), its flags from texflags (RM_TEX_WARP, _FLOWING,
 * _TRANS33 or 66, _SKY). The number of triangles written, or -1 when the
 * records do not cover the index buffer or it does not fit in cap.
 */
long rm_rtw_shade_build(const rm_surfrec_t *recs, long nrecs, unsigned long nindices, const int *tex, unsigned *out,
		unsigned long cap);

/* One frame's pass, for the r_rt_stats 2 line (counted on the GPU, read once the frame completed). */
typedef struct
{
	unsigned long frame;
	unsigned long pixels;   /* the render size (the compaction runs on every pixel) */
	unsigned long opaque;   /* pixels of opaque water (material 3) */
	unsigned long trans;    /* pixels whose eye ray found translucent water */
	unsigned long eye;      /* eye rays */
	unsigned long listed;   /* TASK158: pixels the trace ran on (the compaction's list) */
	unsigned long lit;      /* water pixels whose reflection is a lit pixel */
	unsigned long world;    /* TASK158: a face shaded in world space */
	unsigned long sky;      /* of the sky, from the picture */
	unsigned long skybox;   /* TASK158: the sky box's texel, in world space */
	unsigned long miss;     /* with none */
	double ms;              /* the pass's GPU time; below 0 when not measured */

	/*
	 * TASK158: 1 when the frame took the world-space path (r_rt_water_hits
	 * 1): then ms is the sum of its three passes' spans, each below 0 when
	 * not measured: the candidates' raster (the start of its vertex stage
	 * to the end of its fragment stage), the compaction (its encoder, with
	 * the arguments) and the trace (its encoder); span_ms is the whole,
	 * the raster's start to the trace's end, which is below the sum where
	 * the spans overlap; vert_ms and frag_ms the raster's vertex stage (its
	 * start to its end) and its fragment stage, apart.
	 */
	int ws;
	double raster_ms, compact_ms, trace_ms, span_ms, vert_ms, frag_ms;
} rm_rtw_stats_t;

/* the r_rt_stats 2 line of a frame's pass, without a newline: 1 when it fits in len; 0 with buf empty otherwise */
int rm_rtw_stats_format(const rm_rtw_stats_t *s, char *buf, size_t len);

/* ---- rmb_rtwater.m: the Metal back end ------------------------------------------------- */

/* the kernel's, the blend's, the translucent draw's and the development view's pipelines: 1, or 0 with err set */
int rmb_rtw_open(char *err, size_t errlen);

/* releases the pipelines, the outputs and the counters */
void rmb_rtw_close(void);

/*
 * Before the G-buffer raster: the kernel's outputs at the render size w x
 * h (the reflection and class, RGBA8Unorm; the translucent water's
 * distance, R32Float). 1 when the view can trace; 0 when the pass is not
 * open or they cannot be made. ws (TASK158, rm_rtw_hits): 1 the
 * world-space path, which also makes the compaction's list and the
 * reflection target a render target of the candidates' raster; 0
 * TASK58's path, whose outputs are TASK58's and which has no list.
 */
int rmb_rtw_prepare(unsigned long w, unsigned long h, int ws);

/* the view the next G-buffer view traces with (copied); NULL turns the pass off for that view */
void rmb_rtw_set_view(const rm_rtw_view_t *v);

/* the stats of the frames whose pass completed since the last call, at most cap; their number */
int rmb_rtw_stats(rm_rtw_stats_t *s, int cap);

/* the view ended (rmb_gbuf_view_end) or the frame did: the forward pass's translucent draws are stock again */
void rmb_rtw_view_end(void);

/* 1 when this view's trace was dispatched and its r_rt_water_show view drawn (rmb_rtw_blend), until the view ends */
int rmb_rtw_shown(void);

/* TASK158: the map's triangles' shading records (rm_rtw_shade_build), n of them, copied; NULL or 0 releases them */
void rmb_rtw_set_shade(const unsigned *rec, unsigned long n);

/*
 * TASK158: this frame's texture table: the back end's texture ids of the
 * world's images, n in the order of rm_rtw_tex_slots (at most
 * MTL_RTW_TEX_WORLD), the lightmap array's, and the sky box's six in face
 * order (NULL for none). An id without a texture is shaded white.
 */
void rmb_rtw_set_textures(const int *tids, int n, int lmtex, const int *sky);

/* TASK158: the dynamic lights the lightmaps left out, 8 floats each (origin, intensity, colour, 0), at most MTL_RTS_MAX_LIGHTS */
void rmb_rtw_set_lights(const float *l, int n);

#ifdef __OBJC__
/*
 * rmb_gbuf.m's hooks. rmb_rtw_trace, after the resolve (the lit result in
 * target, the render-size colour the resolve wrote): the kernel on the
 * frame's command buffer over it and the depth, normal and material
 * attachments; 1 when it was dispatched, and then rmb_rtw_blend, in a pass
 * over the target opened after it: the reflection over the opaque water
 * (or r_rt_water_show's view over every pixel). rmb_rtw_forward_pipe: the
 * translucent faces' pipeline of the forward pass (rm_vertex_world, the
 * stock colour, the reflection where the face is the water the kernel
 * found), nil when this view did not trace; rmb_rtw_bind_forward binds
 * what it reads at fragment index 6 and 7.
 */
int rmb_rtw_trace(id<MTLTexture> target, id<MTLTexture> depth, id<MTLTexture> normal, id<MTLTexture> material);
int rmb_rtw_blend(id<MTLRenderCommandEncoder> enc, id<MTLTexture> material);
id<MTLRenderPipelineState> rmb_rtw_forward_pipe(void);
void rmb_rtw_bind_forward(id<MTLRenderCommandEncoder> enc);
#endif

#endif /* RM_RTWATER_H */
