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
 * Ray-traced ambient occlusion in ref_metal, RT5 (TASK57;
 * docs/plans/metal-raytracing-design.md, section 2). With r_rt_ao on, a
 * compute pass after the G-buffer raster traces two cosine-weighted
 * hemisphere rays of r_rt_ao_range units from each lightmapped world
 * pixel, its numbers from a blue-noise tile made in code; the AO, 8 bits a
 * pixel, scales the pixel's lightmap term, clamped to [r_rt_ao_min, 1],
 * in the light the lighting resolve reads (lightmap x AO). The dynamic
 * lights' term of RT3 is added after it, so AO leaves it.
 *
 * The rm_rtao_* functions are plain C with neither SDL nor Metal
 * (rm_rtao.c), host-tested by tests/metal/rm_rtao_test.c with the
 * per-pixel arithmetic of common/mtl_rtao_math.h; the rmb_rtao_* functions
 * are the back end's (rmb_rtao.m).
 *
 * =======================================================================
 */

#ifndef RM_RTAO_H
#define RM_RTAO_H

#include <stddef.h>

#include "rmb.h"
#include "../common/mtl_shader_math.h"
#include "../common/mtl_rtao_math.h"

/* ---- rm_rtao.c: no SDL, no Metal ------------------------------------------------------ */

/* the cvars' defaults (design section 2) */
#define RM_RTAO_RANGE 128.0f
#define RM_RTAO_FLOOR 0.4f

/* the range's bounds: above the ray's start (mtl_rtao_bias), below the longest a map holds */
#define RM_RTAO_RANGE_MIN 1.0f
#define RM_RTAO_RANGE_MAX 8192.0f

/*
 * 1 when a frame traces the AO: r_rt_ao is not 0, the device has ray
 * tracing, and the world is lightmapped (r_fullbright 0: with it gl1
 * draws no lightmap term, so the AO has none to scale). A value that is
 * not finite counts as 0.
 */
int rm_rtao_on(float ao, int device_rt, float fullbright);

/* r_rt_ao_range as the pass takes it: RM_RTAO_RANGE_MIN to _MAX units; RM_RTAO_RANGE when not finite */
float rm_rtao_range(float v);

/* r_rt_ao_min as the pass takes it: 0 to 1; RM_RTAO_FLOOR when not finite */
float rm_rtao_floor(float v);

/*
 * The noise's frame: with r_rt_ao_seed 1 or more the frame is that seed
 * (cut to an integer, at most 65535), the same on every frame, so frames
 * are stateless (the pins); with 0, below 0 or a value that is not
 * finite, the renderer's frame count, so the noise moves on every frame.
 * Below 65536 either way.
 */
unsigned rm_rtao_frame(float seed, unsigned long count);

/*
 * r_rt_ao_show (development): 0 the picture, 1 the factor the lightmap
 * term takes, grey, on the world's pixels (black elsewhere), 2 the
 * classes (black not traced, white unoccluded, blue occluded above the
 * floor, red at the floor). v clamped to 0 to RM_RTAO_SHOW_MAX before it
 * is cut to an int; 0 for a value that is not finite.
 */
#define RM_RTAO_SHOW_MAX 2
int rm_rtao_show(float v);

/*
 * The blue-noise tile (MTL_RTAO_TILE_N ranks, row by row): each rank 0 to
 * MTL_RTAO_TILE_N - 1 once, by Ulichney's void-and-cluster on the torus
 * (a Gaussian of sigma 1.5 pixels; the initial pattern from a fixed
 * generator), so the tile is the same on every run and every host. 1 with
 * rank filled; 0 when it cannot allocate.
 */
int rm_rtao_bluenoise(unsigned short *rank);

/* the view of the kernel (rm_rtao.metal's rm_rtao_view, 128 bytes) */
typedef struct
{
	float inv[16];      /* GL clip coordinates to world: the inverse of the jittered projection times the modelview */
	float vp[4];        /* the view's viewport at the render size: x, y, w, h in pixels, from the top left */
	float eye[4];       /* the view's origin */
	float bias_k;       /* mtl_rtao_bias's k: 4 x 2^-24 / gl_znear */
	float range;        /* rm_rtao_range */
	float floor_;       /* rm_rtao_floor */
	unsigned frame;     /* rm_rtao_frame */
	unsigned show;      /* rm_rtao_show */
	unsigned pad[3];
} rm_rtao_view_t;

/*
 * The view of a G-buffer view: proj (jittered) and world as R_SetupGL and
 * RM_GB_Begin left them, its viewport at the render size, the eye and the
 * near plane's distance (TASK55's rm_rts_view), with the cvars' values as
 * the functions above read them. 0 when the product cannot be inverted or
 * znear is not above 0 (nothing is traced then).
 */
int rm_rtao_view(const float *proj, const float *world, const rm_viewport_t *vp, const float *eye, float znear,
		float range, float floor_, unsigned frame, unsigned show, rm_rtao_view_t *out);

/* One frame's AO pass, for the r_rt_stats 2 line (counted on the GPU, read once the frame completed). */
typedef struct
{
	unsigned long frame;
	unsigned long pixels;       /* pixels the kernel ran on: the render size */
	unsigned long traced;       /* lightmapped world pixels, which traced their rays */
	unsigned long occluded;     /* traced pixels whose AO is below 1 */
	unsigned long floored;      /* traced pixels whose AO is below the floor (the factor is the floor) */
	unsigned long up;           /* traced pixels whose stored normal is within 8 degrees of up (floors) */
	unsigned long up_occluded;  /* of those, occluded */
	unsigned long rays;
	unsigned long hits;         /* rays that met an occluder */
	double ms;                  /* the pass's GPU time; below 0 when not measured */
} rm_rtao_stats_t;

/* the r_rt_stats 2 line of a frame's pass, without a newline: 1 when it fits in len; 0 with buf empty otherwise */
int rm_rtao_stats_format(const rm_rtao_stats_t *s, char *buf, size_t len);

/* ---- rmb_rtao.m: the Metal back end ---------------------------------------------------- */

/* the kernel's and the development view's pipelines and the noise tile: 1, or 0 with err set; counted as RM_OBJ_RT */
int rmb_rtao_open(char *err, size_t errlen);

/* releases the pipelines, the tile, the output textures and the counters */
void rmb_rtao_close(void);

/*
 * Before the G-buffer raster: the kernel's outputs at the render size w x
 * h (the AO, R8Unorm, and the light the resolve reads, RGBA16Unorm). 1
 * when the view can trace; 0 when the pass is not open or they cannot be
 * made.
 */
int rmb_rtao_prepare(unsigned long w, unsigned long h);

/* the view the next G-buffer view traces with (copied); NULL turns the pass off for that view */
void rmb_rtao_set_view(const rm_rtao_view_t *v);

/* the stats of the frames whose pass completed since the last call, at most cap; their number */
int rmb_rtao_stats(rm_rtao_stats_t *s, int cap);

#ifdef __OBJC__
/*
 * rmb_gbuf_resolve's hooks (rmb_gbuf.m). rmb_rtao_trace, after the raster
 * (and RT3's kernel) ended: the kernel on the frame's command buffer over
 * the render-size depth, normal, material and light attachments; the
 * light with the AO, for the resolve to read in place of the light
 * attachment (each world pixel's lightmap term times the factor, every
 * other pixel's light as it was), or nil when nothing was traced (the
 * resolve reads the attachment). rmb_rtao_show, in the resolve's pass
 * after the resolve: draws r_rt_ao_show's view over the pixels the raster
 * wrote, when the frame traced and it is on; 1 when it drew.
 */
id<MTLTexture> rmb_rtao_trace(id<MTLTexture> depth, id<MTLTexture> normal, id<MTLTexture> material,
		id<MTLTexture> light);
int rmb_rtao_show(id<MTLRenderCommandEncoder> enc, id<MTLTexture> material);
#endif

#endif /* RM_RTAO_H */
