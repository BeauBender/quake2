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
 * Ray tracing in ref_metal, RT1 (TASK53): the acceleration structures of
 * docs/plans/metal-raytracing-design.md, section 1. The world's
 * primitive structure and one per inline model are built at map load from
 * the retained world buffers (N4); each visible alias entity's vertices are
 * lerped on the GPU into a buffer of its own and its structure refit; the
 * instance structure is rebuilt every frame before the 3D pass. Nothing
 * reads them yet. With every ray-tracing cvar at its default, or on a
 * device without ray tracing, nothing is built or allocated (rm_rt_wanted).
 *
 * The rm_rt_* functions are plain C with neither SDL nor Metal (rm_rt.c),
 * host-tested by tests/metal/rm_rt_test.c; the rmb_rt_* functions are the
 * back end's (rmb_rt.m). As in rmb.h, no Objective-C type appears here.
 *
 * =======================================================================
 */

#ifndef RM_RT_H
#define RM_RT_H

#include <stddef.h>
#include <stdint.h>

#include "rmb.h"

/* ---- rm_rt.c: no SDL, no Metal ------------------------------------------------ */

/*
 * The material ids of a triangle, the G-buffer's r8 ids of design
 * section 3: 0 none, 1 world, 2 alias, 3 water, 4 sky, 5 translucent
 * ("alpha", section 1).
 */
#define RM_RT_MAT_NONE 0
#define RM_RT_MAT_WORLD 1
#define RM_RT_MAT_ALIAS 2
#define RM_RT_MAT_WATER 3
#define RM_RT_MAT_SKY 4
#define RM_RT_MAT_ALPHA 5
#define RM_RT_MATS 6

/*
 * A surface's material id from its texinfo flags: sky first, then a
 * warped surface is water (flat, un-warped triangles; a translucent water
 * face is water too, its alpha is in its surface record), then
 * SURF_TRANS33 or SURF_TRANS66 is alpha; everything else, flowing
 * surfaces included, is world.
 */
unsigned rm_rt_material(int texflags);

/*
 * 1 when ref_metal builds acceleration structures: a ray-tracing setting
 * asks for them (RT1: r_rt_stats not 0) and the device supports ray
 * tracing (rmb_caps_t.raytracing). With every setting at its default, or
 * on a device without ray tracing (the build VM), 0: nothing is built or
 * allocated. A value that is not finite counts as 0.
 */
int rm_rt_wanted(float stats, int device_raytracing);

/* Triangles by material id, and their sum. */
typedef struct
{
	unsigned long tris[RM_RT_MATS];
	unsigned long total;
} rm_rt_tris_t;

/*
 * The per-triangle material ids of the world's index buffer (nindices
 * indices, all surfaces in buffer order): each of the nrecs surface
 * records must start where the one before it ended, hold a multiple of 3
 * indices and stay inside the buffer, and the records must cover it all.
 * Writes nindices / 3 ids into mat (cap entries). The number of
 * triangles; -1, with nothing guaranteed in mat, when a record breaks a
 * rule or the ids do not fit.
 */
long rm_rt_materials(const rm_surfrec_t *recs, long nrecs, unsigned long nindices, unsigned char *mat,
		unsigned long cap);

/*
 * A model's index range of the world buffer (a submodel's firstindex and
 * numindices) as triangles: 1, with *first_tri and *ntris, when the range
 * starts and ends on a triangle and lies inside nindices; 0 otherwise (an
 * empty range is 0, the model has no structure).
 */
int rm_rt_range(unsigned long first_index, unsigned long num_indices, unsigned long nindices,
		unsigned long *first_tri, unsigned long *ntris);

/* Counts the ntris ids of mat from first_tri into t (zeroed first). */
void rm_rt_count(const unsigned char *mat, unsigned long first_tri, unsigned long ntris, rm_rt_tris_t *t);

/* MD2's MAX_TRIANGLES: an alias model's structure holds at most this many triangles. */
#define RM_RT_MD2_MAX_TRIS 4096

/*
 * The bytes the lerp reads of an alias model's frames: num_frames frames
 * of framesize bytes each, every frame holding num_xyz 4-byte vertices
 * from vofs (RM_MD2_FRAME_HEADER). 1 with *bytes when the counts are
 * inside MD2's limits, framesize and vofs are multiples of 4, the vertices
 * fit in a frame and the product is at most cap (checked by division, no
 * overflow); 0 with *bytes 0 otherwise.
 */
int rm_rt_frames_bytes(long num_frames, long framesize, long vofs, long num_xyz, size_t cap, size_t *bytes);

/*
 * The first vertex of frame f in the frames' 4-byte vertices, as the lerp
 * kernel indexes them: (f * framesize + vofs) / 4. -1 when f is outside
 * 0 .. num_frames - 1.
 */
long rm_rt_frame_vertex(long f, long num_frames, long framesize, long vofs);

/*
 * An alias model's triangle list for its structure: num_tris MD2 triangles
 * of stride shorts each, index_xyz (3 shorts) first, into out (3 per
 * triangle, cap entries). The file's triangles are not checked by the
 * loader (gl1 draws the command stream), so each index must be in
 * 0 .. num_xyz - 1. The number of triangles; 0 when num_tris is 0 or
 * past RM_RT_MD2_MAX_TRIS, num_xyz is outside 1 .. RM_MD2_MAX_VERTS, an
 * index is out of range or out is too small.
 */
long rm_rt_alias_indices(const short *tris, long stride, long num_tris, long num_xyz, unsigned short *out,
		long cap);

/* What an entity of the refdef is, from its model (the front end decides it). */
#define RM_RT_ENT_NULL 0     /* no model */
#define RM_RT_ENT_ALIAS 1
#define RM_RT_ENT_BRUSH 2    /* an inline model of the world */
#define RM_RT_ENT_SPRITE 3

typedef struct
{
	int kind;               /* RM_RT_ENT_* */
	int flags;              /* entity_t.flags */
	int model;              /* the inline model's number (brush), the back end's model id (alias); 0 none */
	float origin[3], angles[3];
	float oldorigin[3], backlerp, alpha;   /* checked for finite values, as the raster's draws do */
} rm_rt_ent_t;

/* An instance of the instance structure. */
#define RM_RT_INST_WORLD 0
#define RM_RT_INST_INLINE 1
#define RM_RT_INST_ALIAS 2

/* the world and at most every entity */
#define RM_RT_MAX_INSTANCES (1 + RM_MAX_ENTITIES)

typedef struct
{
	int kind;               /* RM_RT_INST_* */
	int model;              /* the inline model's number, the alias model's id; 0 for the world */
	int ent;                /* the entity's index in the refdef, -1 for the world */
	/* RT3 (TASK55): the lights the instance carries (rm_rts_carried, bit i light i), set by RM_RT_Frame; 0 here */
	unsigned carry;
	/* the object-to-world transform, column-major 4 by 3: three basis columns, then the translation (MTLPackedFloat4x3) */
	float xform[12];
} rm_rt_inst_t;

/* The entities of a refdef left out of the instance structure, by reason. */
typedef struct
{
	unsigned long beam, null, sprite, weapon, shell, nomodel, bad;
} rm_rt_excl_t;

/*
 * The object-to-world transform of an entity, gl1's R_RotateForEntity on
 * the identity: translate to origin, then rotate about z by the yaw, about
 * y by minus the pitch, about x by minus the roll, with gl1's turned
 * angles: an alias model's pitch is negated (R_DrawAliasModel), a brush
 * model's pitch and roll (R_DrawBrushModel). Into xform as above.
 */
void rm_rt_transform(const float *origin, const float *angles, int brush, float *xform);

/*
 * The instance list of a frame (design section 1): the world first, with
 * the identity transform, when world is 1; then, in refdef order, every
 * brush entity whose inline model has a structure (model above 0) and
 * every alias entity with a model id, each with rm_rt_transform. Left out
 * and counted in excl: beams (RF_BEAM, whatever their model), entities
 * without a model, sprites, shells (RF_SHELL_*: the client adds a shell as
 * a second entity over the model, which would occlude it), the view weapon
 * (RF_WEAPONMODEL) unless gunshadow is 1 or when the raster hides it
 * (hand 2, rm_alias_drawn), an entity whose model has no structure
 * (nomodel), and one with a float that is not finite (bad). At most n
 * entities (rm_entity_count) and cap instances. The number of instances.
 */
int rm_rt_instances(const rm_rt_ent_t *ents, long n, int world, int gunshadow, float hand, rm_rt_inst_t *out,
		int cap, rm_rt_excl_t *excl);

/* The bytes of an instance descriptor (MTLAccelerationStructureInstanceDescriptor) and of a lerped vertex. */
#define RM_RT_INSTANCE_BYTES 64
#define RM_RT_AVERT_BYTES 16

/*
 * The bytes of n instance descriptors, of the lerped vertices of a model
 * of num_xyz vertices and of its triangle list of num_tris triangles
 * (uint16 indices): 0 for a count outside its limit.
 */
size_t rm_rt_instance_bytes(long n);
size_t rm_rt_avert_bytes(long num_xyz);
size_t rm_rt_aindex_bytes(long num_tris);

/* The map's structures, for the r_rt_stats line at map load. */
typedef struct
{
	rm_rt_tris_t world;             /* the world structure's triangles by material */
	unsigned long inline_models;    /* inline models with a structure */
	rm_rt_tris_t inlines;           /* their triangles, by material */
	size_t world_bytes, inline_bytes, scratch_bytes, material_bytes;
	double build_ms;                /* GPU time of the build; below 0 when not measured */
} rm_rt_map_stats_t;

/* One frame's structures, for the r_rt_stats 2 line. */
typedef struct
{
	unsigned long frame;
	unsigned long instances, inlines, aliases, alias_tris, built, refit, skipped;
	rm_rt_excl_t excl;
	size_t alias_bytes, vertex_bytes, model_bytes, ias_bytes, scratch_bytes;
	double alias_ms, ias_ms;        /* GPU time of the lerps and refits, of the instance rebuild; below 0 when not measured */
} rm_rt_frame_stats_t;

/* The spread of n GPU times. */
typedef struct
{
	unsigned long n;
	double min, median, p95, max;
} rm_rt_spread_t;

/*
 * The spread of the n times of ms (sorted in place): the minimum, the
 * median (the mean of the two middle ones for an even n), the 95th
 * percentile (nearest rank) and the maximum. 0 with all fields 0 when n
 * is 0.
 */
int rm_rt_spread(double *ms, unsigned long n, rm_rt_spread_t *out);

/*
 * The r_rt_stats lines, without a newline: the map's (at map load), a
 * frame's (r_rt_stats 2) and the summary of the frames since the map
 * loaded (when the map is freed, r_rt_stats goes to 0 or the renderer
 * shuts down). 1 when the line fits in len; 0, with buf empty, when it does not.
 */
int rm_rt_map_format(const char *map, const rm_rt_map_stats_t *s, char *buf, size_t len);
int rm_rt_frame_format(const rm_rt_frame_stats_t *s, char *buf, size_t len);
int rm_rt_summary_format(const char *map, unsigned long frames, const rm_rt_spread_t *alias,
		const rm_rt_spread_t *ias, unsigned long busiest_aliases, unsigned long busiest_tris, char *buf, size_t len);

/* ---- rmb_rt.m: the Metal back end ------------------------------------------------ */

/* The lerp of one alias instance: gl1's R_LerpVerts without the shell offset, the factors of rm_alias_lerp_setup. */
typedef struct
{
	int mid;                /* rmb_rt_model id */
	long frame, oldframe;   /* rm_rt_frame_vertex of the entity's frame and oldframe */
	float move[3], frontv[3], backv[3];
} rmb_rt_lerp_t;

/*
 * Opens the ray-tracing objects (the lerp pipeline, the GPU timestamp
 * buffer): 1, or 0 with err set. Only when rm_rt_wanted; every object is
 * counted in the ledger as RM_OBJ_RT.
 */
int rmb_rt_open(char *err, size_t errlen);

/* Releases every ray-tracing object, the map's and the models' included. */
void rmb_rt_close(void);

/*
 * Builds the world's structure and one per inline model from the retained
 * world buffers (rmb_buffer_static ids vbuf, ibuf; RM_WVERT_STRIDE, uint32
 * indices), ranges[0] the world's and ranges[i] inline model i's (ntris 0:
 * none), and the per-triangle material buffer (ntris_all ids). The uploads
 * before it are committed first; the build is waited for (bounded). 1 with
 * s filled; 0 when a structure could not be made (none is kept).
 */
typedef struct
{
	unsigned long first_tri, ntris;
} rmb_rt_range_t;

int rmb_rt_build_map(int vbuf, int ibuf, const rmb_rt_range_t *ranges, int nranges, const unsigned char *mat,
		unsigned long ntris_all, rm_rt_map_stats_t *s);

/* Releases the map's structures (the world's, the inline models'). */
void rmb_rt_free_map(void);

/*
 * An alias model's frames (bytes from rm_rt_frames_bytes) and triangle
 * list (num_tris triangles of rm_rt_alias_indices), for the lerp and its
 * structures: the model's id, or 0 when it could not be made.
 */
int rmb_rt_model(const void *frames, size_t frame_bytes, long num_xyz, const unsigned short *idx, long num_tris);

/* Releases a model's buffers and its instances' structures. */
void rmb_rt_model_free(int mid);

/*
 * One frame (design section 3, step 1), encoded into the frame's command
 * buffer before the 3D pass: the lerps of the alias instances (lerps[j]
 * for the j-th RM_RT_INST_ALIAS of inst), their structures built once and
 * refit after, then the instance structure rebuilt from the n instances.
 * GPU times are those of an earlier frame (the timestamps are read once
 * it completed). With shadows (RT3's r_rt_shadows on) the frame also keeps
 * its scene for rmb_rt_bind_scene (each instance's first triangle and the
 * lights it carries, in the frame ring; an instance that carries one is
 * non-opaque); without, nothing beyond TASK53's structures is taken. 1 with
 * s filled; 0 when nothing was encoded (no frame, a structure missing).
 */
int rmb_rt_frame(const rm_rt_inst_t *inst, int n, const rmb_rt_lerp_t *lerps, int nlerps, int shadows,
		rm_rt_frame_stats_t *s);

/*
 * The GPU times (ms) of the timed frames that completed since the last
 * call: the alias lerps and refits (0 in a frame without an alias
 * instance) and the instance rebuild, at most cap of each. Their number;
 * 0 where the device samples no timestamps at the encoders' boundaries.
 */
int rmb_rt_times(double *alias_ms, double *ias_ms, int cap);

/*
 * RT3 (TASK55): the map's per-triangle transmission (rm_rts_transmit, ntris
 * the map's triangles) for the shadow rays; 1 when kept. Freed with the
 * map, or by t NULL (the shadows turned off).
 */
int rmb_rt_set_transmit(const unsigned char *t, unsigned long ntris);

/* GPU ticks of the device's timestamps to milliseconds (rmb_rt_open's correlation) */
double rmb_rt_gpu_ms(uint64_t ticks);

#ifdef __OBJC__
/*
 * RT3: binds frame fno's instance structure at buffer index ias_at (each
 * structure it holds marked used by ce), two words an instance at base_at:
 * its first triangle of the map's buffers (~0 for an alias model) and the
 * lights it carries (rm_rts_carried), the
 * per-triangle transmission at trans_at and the number of its triangles
 * at ntris_at (0 without one). 0, with nothing bound, when frame fno
 * built no instance structure.
 */
int rmb_rt_bind_scene(id<MTLComputeCommandEncoder> ce, unsigned long fno, int ias_at, int base_at, int trans_at,
		int ntris_at);
#endif

#endif /* RM_RT_H */
