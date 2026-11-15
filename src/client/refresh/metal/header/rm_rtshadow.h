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
 * Ray-traced shadows from dynamic lights in ref_metal, RT3 (TASK55;
 * docs/plans/metal-raytracing-design.md, section 2). With r_rt_shadows on,
 * gl1's dynamic-light term leaves the lightmaps (r_dlight_term) and a
 * compute pass after the G-buffer raster traces, per lightmapped world
 * pixel, one shadow ray toward each of the four lights that add the most
 * there; the lighting resolve then adds visibility x (intensity - dist) x
 * colour, the falloff of gl1's R_AddDynamicLights at the pixel.
 *
 * The rm_rts_* functions are plain C with neither SDL nor Metal
 * (rm_rtshadow.c), host-tested by tests/metal/rm_rtshadow_test.c with the
 * per-pixel arithmetic of common/mtl_rts_math.h; the rmb_rts_* functions
 * are the back end's (rmb_rtshadow.m).
 *
 * =======================================================================
 */

#ifndef RM_RTSHADOW_H
#define RM_RTSHADOW_H

#include <stddef.h>

#include "rmb.h"
#include "../common/mtl_shader_math.h"
#include "../common/mtl_rts_math.h"

/* ---- rm_rtshadow.c: no SDL, no Metal ------------------------------------------------ */

/*
 * 1 when a frame traces the shadows and the resolve adds the dynamic
 * lights: shadows (r_rt_shadows) is not 0, the device has ray tracing, and
 * gl1 would add its dynamic lights to the lightmaps (gl1_dynamic not 0,
 * neither r_fullbright nor gl1_flashblend set: with them gl1 adds none, so
 * neither does the effect). A value that is not finite counts as 0.
 */
int rm_rts_on(float shadows, int device_rt, float fullbright, float dynamic, float flashblend);

/*
 * What a shadow ray keeps of its light where it crosses a triangle of
 * texinfo flags texflags, 0 to 255 (design section 1): the triangles of
 * water and translucent surfaces (material ids 3 and 5) pass it on as gl1
 * draws them, 1 - 0.33 for SURF_TRANS33 (171), else 1 - 0.66 for
 * SURF_TRANS66 (87); every other triangle (the world, the sky, water
 * drawn opaque) stops it (0).
 */
unsigned rm_rts_transmit_one(int texflags);

/*
 * The transmission of every triangle of the world's index buffer, as
 * rm_rt_materials walks it (nrecs surface records covering nindices
 * indices): nindices / 3 bytes into out (cap entries), the number of
 * triangles that pass light into *ntrans. The number of triangles; -1
 * when a record breaks rm_rt_materials' rules or the bytes do not fit.
 */
long rm_rts_transmit(const rm_surfrec_t *recs, long nrecs, unsigned long nindices, unsigned char *out,
		unsigned long cap, unsigned long *ntrans);

/* one light of the kernel and the resolve (rm_rtshadow.metal's rm_rts_light, 32 bytes) */
typedef struct
{
	float origin[4];    /* w: the intensity, the light's radius */
	float color[4];
} rm_rts_light_t;

/* the view of the kernel and the resolve (rm_rtshadow.metal's rm_rts_view, 128 bytes) */
typedef struct
{
	float inv[16];      /* GL clip coordinates to world: the inverse of the jittered projection times the modelview */
	float vp[4];        /* the view's viewport at the render size: x, y, w, h in pixels, from the top left */
	float eye[4];       /* the view's origin */
	float bias_k;       /* mtl_rts_bias's k: 4 x 2^-24 / gl_znear */
	unsigned nlights;   /* lights of the frame, at most MTL_RTS_MAX_LIGHTS */
	unsigned trans;     /* 1 when the map holds a triangle that passes light (a ray then looks past it) */
	unsigned show;      /* r_rt_shadows_show */
	unsigned moved;     /* 1 when a brush entity of the frame is moved or turned: every pixel looks for its surface */
	unsigned carried;   /* bit i: a model of the frame carries light i (RM_RT_Carried), its rays pass that model */
	unsigned pad[2];
} rm_rts_view_t;

/*
 * gl1's box of an alias entity in its model's space (R_CullAliasModel):
 * the union of its frame's and its old frame's, each translate to
 * translate + 255 x scale, into mins[3] and maxs[3].
 */
void rm_rts_alias_box(const float *scale, const float *translate, const float *oldscale, const float *oldtranslate,
		float *mins, float *maxs);

/*
 * The lights an alias instance carries (TASK55): bit i set when light i's
 * origin, taken into the instance's model space by the inverse of xform
 * (rm_rt_inst_t's object-to-world transform, column-major 4 by 3), lies
 * in its box mins..maxs (rm_rts_alias_box) grown by RM_RTS_CARRY_TOL on
 * every side. A shadow ray toward a light passes the instances that carry
 * it: a rocket's or a bolt's light inside its own model, a muzzle flash's
 * at the origin of the flash and smoke models it spawns and at the gun of
 * the monster that fires it. Every other instance shadows the light. The
 * first nl lights, at most MTL_RTS_MAX_LIGHTS; 0 for a transform that
 * cannot be inverted or a box that is not finite, and no bit for a light
 * that is not.
 */
#define RM_RTS_CARRY_TOL 1.0f
unsigned rm_rts_carried(const float *xform, const float *mins, const float *maxs, const rm_rts_light_t *l, int nl);

/* the 4 x 4 column-major inverse of m in double precision: 1, or 0 with out the identity when m is singular or not finite */
int rm_rts_invert(const float *m, float *out);

/*
 * The view of a G-buffer view: proj (jittered) and world as R_SetupGL and
 * RM_GB_Begin left them, its viewport at the render size, the eye and
 * the near plane's distance. 0 when the product cannot be inverted or
 * znear is not above 0 (nothing is traced then).
 */
int rm_rts_view(const float *proj, const float *world, const rm_viewport_t *vp, const float *eye, float znear,
		rm_rts_view_t *out);

/*
 * The world position of the centre of pixel (px, py) of the render-size
 * target at depth depth (0 to 1, GL's clip depth mapped as mtl_gl_clip_z),
 * as the kernel unprojects it: 1 with out, 0 behind the eye.
 */
int rm_rts_unproject(const rm_rts_view_t *v, float px, float py, float depth, float *out);

/*
 * The kernel's choice at a pixel of surface sf (mtl_rts_surf, or
 * mtl_rts_surf_base) among the nl lights l (at most MTL_RTS_MAX_LIGHTS):
 * each light's add into adds[i] (mtl_rts_surf_light), the four that add
 * the most (mtl_rts_top_add), and in *flags MTL_RTS_IN_RANGE when any
 * light is in range.
 */
mtl_rts_top_t rm_rts_select(mtl_rts_surf_t sf, const rm_rts_light_t *l, int nl, float *adds, unsigned *flags);

/*
 * The term the kernel hands the resolve: the sum over the lights of
 * mtl_rts_light_vis x adds[i] x colour (pidx and pvis the packed slots),
 * per channel into rgb, in the 0 to 255 scale of gl1's sums.
 */
void rm_rts_term(const float *adds, const rm_rts_light_t *l, int nl, unsigned pidx, unsigned pvis, float *rgb);

/*
 * A surface's range of the world's index buffer (msurface_t's first_index
 * and num_indices), and unlit: it has no light samples (gl1 adds no
 * dynamic light to it).
 */
typedef struct
{
	unsigned first_index;
	unsigned num_indices;
	unsigned unlit;
} rm_rts_range_t;

/*
 * The surface of each of the ntris triangles of the world's index buffer,
 * from the nsurf surfaces' ranges: tri_surf[k] is the index of the
 * surface whose range holds triangle k, with MTL_RTS_SURF_UNLIT for an
 * unlit one, ~0 for a triangle no surface holds. The number of triangles
 * assigned; -1 when a range is not whole triangles, passes ntris, or two
 * ranges overlap, or when nsurf is MTL_RTS_SURF_UNLIT or more (so ~0's
 * index, MTL_RTS_SURF_UNLIT - 1, is never a surface's).
 */
long rm_rts_surface_map(const rm_rts_range_t *r, long nsurf, unsigned long ntris, unsigned *tri_surf);

/*
 * A surface's plane toward its front for the kernel (mtl_rts_surf): gl1's
 * plane (normal, dist), both negated when the surface faces its back
 * (SURF_PLANEBACK), into out[4]; a missing or non-finite plane gives
 * (0, 0, 1, 0).
 */
void rm_rts_plane(const float *normal, float dist, int back, float *out);

/*
 * r_rt_shadows_show (development): 0 the picture, 1 the mask of the
 * double-count check (white where no dynamic light is in range, black
 * where one is), 2 the classes (black none in range, blue in range but no
 * light added, green traced and lit, red traced and shadowed by one
 * light at least). v clamped to 0 to RM_RTS_SHOW_MAX before it is cut to
 * an int; 0 for a value that is not finite.
 */
#define RM_RTS_SHOW_MAX 2
int rm_rts_show(float v);

/* One frame's shadow pass, for the r_rt_stats 2 line (counted on the GPU, read once the frame completed). */
typedef struct
{
	unsigned long frame;
	unsigned long pixels;       /* pixels the kernel ran on: the render size */
	unsigned long inrange;      /* pixels with a light in range */
	unsigned long traced;       /* pixels that traced a ray */
	unsigned long shadowed;     /* pixels with a traced light below full visibility */
	unsigned long rays;
	double ms;                  /* the pass's GPU time; below 0 when not measured */
} rm_rts_stats_t;

/* the r_rt_stats 2 line of a frame's pass, without a newline: 1 when it fits in len; 0 with buf empty otherwise */
int rm_rts_stats_format(const rm_rts_stats_t *s, char *buf, size_t len);

/* ---- rmb_rtshadow.m: the Metal back end ---------------------------------------------- */

/* the kernel's and the resolve's pipelines: 1, or 0 with err set; their objects are counted as RM_OBJ_RT */
int rmb_rts_open(char *err, size_t errlen);

/* releases the pipelines, the output textures, the map's tables and the counters */
void rmb_rts_close(void);

/*
 * The map's tables: tri_surf (rm_rts_surface_map, ntris entries) and for
 * each of the nsurf surfaces its texture axes, vecs[0] then vecs[1], and
 * its plane (rm_rts_plane): 12 floats a surface. 1 when kept; 0 frees
 * them (the kernel then takes the base axes).
 */
int rmb_rts_set_map(const unsigned *tri_surf, unsigned long ntris, const float *vecs, unsigned long nsurf);

/*
 * Before the G-buffer raster: the kernel's outputs at the render size w x
 * h. 1 when the view can trace (the frame then leaves the dynamic lights
 * out of the lightmaps); 0 when the pass is not open or they cannot be
 * made.
 */
int rmb_rts_prepare(unsigned long w, unsigned long h);

/*
 * The view and lights the next G-buffer view traces and resolves with
 * (copied); n 0, or v NULL, turns the pass off for that view: the resolve
 * is the stock one, or with v->show set the development view with no
 * light in range.
 */
void rmb_rts_set_view(const rm_rts_view_t *v, const rm_rts_light_t *lights, int n);

/* the stats of the frames whose pass completed since the last call, at most cap; their number */
int rmb_rts_stats(rm_rts_stats_t *s, int cap);

#ifdef __OBJC__
/*
 * rmb_gbuf_resolve's hooks (rmb_gbuf.m). rmb_rts_trace, after the
 * raster ended: the kernel on the frame's command buffer over the
 * render-size depth, normal and material attachments; 1 when it was
 * encoded. rmb_rts_resolve, in the resolve's pass: binds the resolve that
 * adds the term (and r_rt_shadows_show's views) when the frame traced,
 * with the stock resolve's uniforms and attachments; 0 otherwise (the
 * caller binds the stock resolve).
 */
int rmb_rts_trace(id<MTLTexture> depth, id<MTLTexture> normal, id<MTLTexture> material);
int rmb_rts_resolve(id<MTLRenderCommandEncoder> enc, const void *resolve, size_t resolve_len,
		id<MTLTexture> albedo, id<MTLTexture> light, id<MTLTexture> material, id<MTLTexture> depth,
		id<MTLTexture> normal);
#endif

#endif /* RM_RTSHADOW_H */
