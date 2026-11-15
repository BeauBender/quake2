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
 * The pure part of ref_metal's G-buffer (RT2, TASK54;
 * docs/plans/metal-raytracing-design.md, section 3): the choice of path,
 * the render size and the attachments' bytes, the viewport at the render
 * size, the projection's jitter, the previous frame's transforms and the
 * matching of an entity to its previous one, and the per-vertex data of
 * an alias model's previous position and normal. rm_gbuf.c, held by
 * tests/metal/rm_gbuf_test.c; no Metal, no engine state.
 *
 * =======================================================================
 */

#ifndef RM_GBUF_H
#define RM_GBUF_H

#include <stddef.h>

#include "rmb.h"

/*
 * The paths of a 3D view. FORWARD is the renderer before RT2, unchanged.
 * MOTION is FORWARD plus a kept depth32float attachment and an rg16float
 * motion-vector attachment (the request of a scaler, TASK111). GBUFFER is
 * the six attachments of design section 3, the lighting resolve and the
 * forward pass after it.
 */
#define RM_GB_PATH_FORWARD 0
#define RM_GB_PATH_MOTION 1
#define RM_GB_PATH_GBUFFER 2

/*
 * The path of a view: FORWARD for a view without the world
 * (RDF_NOWORLDMODEL); GBUFFER when a ray-traced effect is on or gbuffer
 * (r_rt_gbuffer) is not 0; MOTION when motion (r_rt_motion) is not 0;
 * FORWARD otherwise. A value that is not finite counts as 0.
 */
int rm_gb_path(int effects_on, float gbuffer, float motion, int noworld);

/*
 * The render size of a frame fw x fh at scale (r_rt_gbuffer_scale): each
 * side times scale, rounded, at least 1 and at most the frame's. A scale
 * that is not finite, not above 0 or not below 1 gives the frame's size.
 * 0 with both 0 when the frame has no pixels.
 */
int rm_gb_render_size(int fw, int fh, float scale, int *rw, int *rh);

/* the bytes of a path's attachments at rw x rh, by the formats' sizes */
typedef struct
{
	size_t depth;       /* depth32float, 4 */
	size_t stencil;     /* stencil8, 1 */
	size_t normal;      /* rgb10a2, 4 */
	size_t material;    /* r8uint, 1 */
	size_t motion;      /* rg16float, 4 */
	size_t lightmap;    /* rgba8, 4 */
	size_t albedo;      /* rgba8, 4 */
	size_t color;       /* bgra8, 4: the render-size colour target, only below the frame's size */
	size_t total;
} rm_gb_bytes_t;

/*
 * The attachments a path adds to the frame's own colour and
 * depth-stencil targets: none for FORWARD; depth, stencil and motion for
 * MOTION; all six and the stencil for GBUFFER; and the render-size colour
 * target when scaled is set. 0 with *b zeroed for a size outside 1 to
 * 16384 or an unknown path.
 */
int rm_gb_bytes(int path, int rw, int rh, int scaled, rm_gb_bytes_t *b);

/*
 * The viewport vp of the frame fw x fh at the render size rw x rh: each
 * edge times rw / fw (rh / fh), rounded down, the depth range kept. The
 * frame's own size copies vp.
 */
void rm_gb_render_rect(const rm_viewport_t *vp, int fw, int fh, int rw, int rh, rm_viewport_t *out);

/*
 * The projection m (column-major) with a sub-pixel jitter of (jx, jy)
 * pixels of a viewport vw x vh, +x right and +y down: out = T(2 jx / vw,
 * -2 jy / vh, 0) m, so a point lands jx, jy pixels away and its depth is
 * unchanged. A jitter of (0, 0) copies m.
 */
void rm_gb_jitter(const float *m, float jx, float jy, double vw, double vh, float *out);

/*
 * The state of an entity the previous frame is matched on, from its
 * entity_t (rm_gbufscene.c): its model, where it is and was, its angles,
 * its two frames and lerp, and its flags.
 */
typedef struct
{
	const void *model;
	float origin[3];
	float oldorigin[3];
	float angles[3];
	int frame, oldframe, flags;
	float backlerp;
} rm_gb_ent_t;

#define RM_GB_MAX_ENTS 128   /* MAX_ENTITIES of ref.h */

/*
 * The previous-frame transforms (TASK111's item 4): one view's camera
 * and projections (unjittered) with its entities.
 */
typedef struct
{
	int valid;              /* a world view was recorded */
	float world[16];        /* the modelview of the view (rm_view_matrix) */
	float proj[16];         /* the projection, unjittered */
	float wproj[16];        /* the view weapon's projection, unjittered */
	int wvalid;             /* wproj was set: a view weapon was drawn */
	float vieworg[3];
	float time;             /* the refdef's time (the sky's rotation) */
	double vw, vh;          /* the viewport's size in pixels at the render size */
	int nents;
	rm_gb_ent_t ents[RM_GB_MAX_ENTS];
} rm_gb_view_t;

typedef struct
{
	rm_gb_view_t prev;      /* the last world view */
	rm_gb_view_t cur;       /* this frame's, made by the caller */
	int match[RM_GB_MAX_ENTS];  /* cur.ents[i]'s previous: an index of prev.ents, or -1 */
} rm_gb_hist_t;

/* forgets the previous view (a map, a vid_restart, a render size): the next frame has none */
void rm_gb_hist_reset(rm_gb_hist_t *h);

/*
 * Matches each of the nc entities of this frame, in order, to the
 * nearest unmatched one of the np previous entities with the same model
 * and the same weapon bit (flags & weapon), whose origin lies within
 * maxdist; match[i] is its index, or -1 when none does (a new entity, a
 * teleport, a first frame). A distance that is not finite matches nothing.
 */
void rm_gb_match(const rm_gb_ent_t *prev, int np, const rm_gb_ent_t *cur, int nc, float maxdist, int weapon,
		int *match);

/* matches h->cur's entities to h->prev's (none without a previous view) */
void rm_gb_hist_match(rm_gb_hist_t *h, float maxdist, int weapon);

/* this frame's entity i's previous state, NULL without one */
const rm_gb_ent_t *rm_gb_hist_prev(const rm_gb_hist_t *h, int i);

/* the frame is over: this view becomes the previous one */
void rm_gb_hist_end(rm_gb_hist_t *h);

/*
 * The xyz index of each vertex rm_alias_emit writes for the same command
 * stream (order, norder; num_xyz vertices), in its order, into map (cap
 * entries): the count, or -1 where rm_alias_emit refuses the stream.
 */
long rm_gb_alias_map(const int *order, long norder, int num_xyz, unsigned short *map, long cap);

/* the light normal of an alias vertex: the two frames' anorms.h entries lerped by backlerp, unit length */
void rm_gb_alias_normal(int index, int oindex, float backlerp, float *out);

/*
 * One alias vertex's G-buffer data, beside rm_avert_t (32 bytes, the
 * layout of rm_gbuf.metal's rm_gb_avx): its position in the previous
 * frame (in that frame's model space, w = 1) and its light normal (model
 * space, w = 0).
 */
typedef struct
{
	float prev[4];
	float normal[4];
} rm_gb_avx_t;

/*
 * The data of the nv vertices of map: prev[map[i]] (the previous frame's
 * lerped positions, num_xyz of them) and the normal lerped between the
 * frame's verts and the old frame's overts (4 bytes a vertex, the normal
 * index last) by backlerp. 0 when an index is outside num_xyz.
 */
int rm_gb_alias_extra(const unsigned short *map, long nv, int num_xyz, const float (*prev)[4],
		const unsigned char *verts, const unsigned char *overts, float backlerp, rm_gb_avx_t *out);

/*
 * The motion uniforms of a G-buffer or motion draw (rm_gbuf.metal's
 * rm_uniforms_mv, 160 bytes): this frame's unjittered and the previous
 * frame's model-view-projection, the model's rotation for the normals,
 * half the viewport's size in pixels and the material id.
 */
typedef struct
{
	float cur[16];
	float prev[16];
	float nrm[16];
	float half_vp[2];
	unsigned material;
	unsigned pad;
} rm_gb_uniforms_t;

/* the lighting resolve's uniforms (rm_gbuf.metal's rm_uniforms_resolve): the world's lightmap environment */
typedef struct
{
	int env;
	float scale;
	unsigned pad[2];
} rm_gb_resolve_t;

/* ---- the back end (rmb_gbuf.m) ------------------------------------------------------ */

/* the pass of the 3D view that is open */
#define RMB_GB_PASS_NONE 0      /* the stock 3D pass, or no pass of a G-buffer or motion view */
#define RMB_GB_PASS_RASTER 1    /* the G-buffer raster: the six attachments */
#define RMB_GB_PASS_MOTION 2    /* a forward pass with the motion attachment: MOTION's, and GBUFFER's after the resolve */
#define RMB_GB_PASS_OTHER 3     /* the resolve or the copy: no 3D draw */

/*
 * Opens a view of path MOTION or GBUFFER at the render size rw x rh: the
 * attachments are made (again) for that size and path, and the first pass
 * opens (GBUFFER's raster, MOTION's forward pass with the motion
 * attachment), the stencil cleared to stencil. Below the frame's size the
 * view goes to a render-size colour target cleared to clear_rgba (black
 * when NULL). 0 when nothing opened: the caller draws the stock view.
 */
int rmb_gbuf_view_begin(int path, int rw, int rh, int stencil, const float *clear_rgba);

/* GBUFFER: ends the raster, resolves into the view's colour target (r: the world's lightmap environment), opens the forward pass */
int rmb_gbuf_resolve(const rm_gb_resolve_t *r);

/*
 * Ends the view: below the frame's size the render-size colour target's
 * rectangle src is stretched (bilinear) onto the frame's rectangle dst.
 * The attachments stay valid until the frame's command buffer ends.
 */
void rmb_gbuf_view_end(const rm_viewport_t *src, const rm_viewport_t *dst);

/* the motion uniforms of the next draws; NULL: none, the draw's own matrix both now and before (no motion) */
void rmb_gbuf_set_motion(const rm_gb_uniforms_t *u);

/* the material id of the next draws, the rest of the motion uniforms kept */
void rmb_gbuf_set_material(unsigned material);

/* the next alias draw's G-buffer data, one per vertex (n must be its vertex count, else it gets none) */
void rmb_gbuf_set_alias_extra(const rm_gb_avx_t *x, unsigned n);

/* the next dynamic world draw's normals, one per vertex (nv must be its vertex count, else they are 0) */
void rmb_gbuf_set_dyn_normals(const float (*n)[4], unsigned nv);

/* the static buffer of the world's per-vertex normals (rm_model.c), 0 without one */
void rmb_gbuf_set_world_normals(int nbuf);

/* RMB_GB_PASS_* */
int rmb_gbuf_pass_kind(void);

/* the attachments' size: 1 with it while a set is made, 0 otherwise */
int rmb_gbuf_size(int *w, int *h);

/*
 * The G-buffer contract (design section 3, TASK111): the depth
 * (depth32Float) and motion-vector (rg16Float) attachments of the last
 * MOTION or GBUFFER view, at the render size, nil without one. Written by
 * the raster (MOTION: the forward pass), valid from the end of that pass
 * until the frame's command buffer ends.
 */
#ifdef __OBJC__
id<MTLTexture> rmb_gbuf_depth(void);
id<MTLTexture> rmb_gbuf_motion(void);
#endif

#endif
