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
 * The stock turbulence table as the kernel takes it (MTL_RTW_TURB_SIZE
 * floats): gl1's r_turbsin as its RI_Init halves it (rm_turb_sin), over
 * its largest magnitude, so -1 to 1. 1 with out filled; 0 when the table
 * has no value but 0.
 */
int rm_rtw_turb_table(float *out);

/* the view of the kernel (rm_rtwater.metal's rm_rtw_view, 192 bytes) */
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
	unsigned pad[3];
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

/* One frame's pass, for the r_rt_stats 2 line (counted on the GPU, read once the frame completed). */
typedef struct
{
	unsigned long frame;
	unsigned long pixels;   /* pixels the kernel ran on: the render size */
	unsigned long opaque;   /* pixels of opaque water (material 3) */
	unsigned long trans;    /* pixels whose eye ray found translucent water */
	unsigned long eye;      /* eye rays */
	unsigned long lit;      /* water pixels whose reflection is a lit pixel */
	unsigned long sky;      /* of the sky */
	unsigned long miss;     /* with none */
	double ms;              /* the pass's GPU time; below 0 when not measured */
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
 * open or they cannot be made.
 */
int rmb_rtw_prepare(unsigned long w, unsigned long h);

/* the view the next G-buffer view traces with (copied); NULL turns the pass off for that view */
void rmb_rtw_set_view(const rm_rtw_view_t *v);

/* the stats of the frames whose pass completed since the last call, at most cap; their number */
int rmb_rtw_stats(rm_rtw_stats_t *s, int cap);

/* the view ended (rmb_gbuf_view_end) or the frame did: the forward pass's translucent draws are stock again */
void rmb_rtw_view_end(void);

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
