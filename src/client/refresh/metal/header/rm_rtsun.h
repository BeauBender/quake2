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
 * The ray-traced sun and sky shadow in ref_metal, RT4 (TASK56;
 * docs/plans/metal-raytracing-design.md, section 2). With r_rt_sun on, on
 * a map whose sky faces are at least 2 % of the world's face area, a
 * compute pass after the G-buffer raster (and after RT5's AO, which
 * scales the lightmap term only) traces one ray a lightmapped world pixel
 * toward the sun: a ray that reaches a sky face is lit, anything else
 * (another face, an alias model, nothing at all) is shadowed. Where the
 * stylized lightmap is below r_rt_sunfloor, the pixel's light takes the
 * new term sky_color x r_rt_sunscale x visibility x max(0, N.L). The
 * direction is r_rt_sun_angles (pitch yaw), or the map's line of the
 * committed table scripts/rt/sun-angles.txt.
 *
 * The rm_rtsun_* functions are plain C with neither SDL nor Metal
 * (rm_rtsun.c), host-tested by tests/metal/rm_rtsun_test.c with the
 * per-pixel arithmetic of common/mtl_rtsun_math.h; the rmb_rtsun_*
 * functions are the back end's (rmb_rtsun.m).
 *
 * =======================================================================
 */

#ifndef RM_RTSUN_H
#define RM_RTSUN_H

#include <stddef.h>

#include "rmb.h"
#include "rm_rtfx.h"
#include "../common/mtl_shader_math.h"
#include "../common/mtl_rtsun_math.h"

/* ---- rm_rtsun.c: no SDL, no Metal ----------------------------------------------------- */

/* the cvars' defaults (design section 2; the floor is this card's) */
#define RM_RTSUN_SCALE 0.35f
#define RM_RTSUN_FLOOR 0.5f
#define RM_RTSUN_PITCH 55.0f
#define RM_RTSUN_YAW 45.0f

/* r_rt_sunscale's bounds */
#define RM_RTSUN_SCALE_MAX 4.0f

/* the share of the world's face area the sky must reach for the pass to run */
#define RM_RTSUN_SKY_MIN 0.02

/*
 * 1 when a frame traces the sun: r_rt_sun is not 0, the device has ray
 * tracing, and the world is lightmapped (r_fullbright 0: the floor gate
 * reads the lightmap, which r_fullbright leaves out). An r_rt_sun that is
 * not finite counts as 0; r_fullbright counts as set at any value but 0,
 * NaN among them, as rm_surf.c reads it. The map's sky is the scene's to
 * judge (rm_rtsun_sky_enough, rm_rtsun_wanted).
 */
int rm_rtsun_on(float sun, int device_rt, float fullbright);

/*
 * 1 when the sun asks for the frame's ray-tracing scene (RM_RTSun_Wanted,
 * which RM_RT_Wanted and the scene's keep flag read): r_rt_sun is not 0
 * on a device with ray tracing (rm_rt_wanted), and sky_ok, the map's sky
 * at least RM_RTSUN_SKY_MIN of its faces (rm_rtsun_sky_enough).
 */
int rm_rtsun_wanted(float sun, int device_rt, int sky_ok);

/* r_rt_sunscale as the pass takes it: 0 to RM_RTSUN_SCALE_MAX; RM_RTSUN_SCALE when not finite */
float rm_rtsun_scale(float v);

/* r_rt_sunfloor as the pass takes it: 0 to 1; RM_RTSUN_FLOOR when not finite */
float rm_rtsun_floor(float v);

/*
 * A direction "pitch yaw" in degrees, as r_rt_sun_angles and a line of the
 * table hold it: two decimal numbers and nothing else but blanks, pitch
 * (the sun's height above the horizon) 0 to 90, yaw (its compass angle,
 * counter-clockwise from +x, as an entity's yaw) 0 up to but not
 * including 360. 1 with *pitch and *yaw; 0, both untouched, otherwise.
 */
int rm_rtsun_parse_angles(const char *s, float *pitch, float *yaw);

/* the unit vector toward the sun from pitch and yaw in degrees: (cos p cos y, cos p sin y, sin p) */
void rm_rtsun_dir(float pitch, float yaw, float *out);

/* the longest map name of the table, without its terminator */
#define RM_RTSUN_MAPNAME 63

/* a line of the table */
typedef struct
{
	char map[RM_RTSUN_MAPNAME + 1];
	float pitch, yaw;
} rm_rtsun_entry_t;

/*
 * The table scripts/rt/sun-angles.txt (its header comment is the format):
 * one map a line, "<map> <pitch> <yaw>", the map the bare name (base1 for
 * maps/base1.bsp: letters, digits, '_' and '-', at most RM_RTSUN_MAPNAME,
 * compared without case); '#' starts a comment to the line's end; blank
 * lines are skipped. A line that breaks a rule (a bad name, a missing or
 * extra field, a number that is not one or is out of bounds, a map named
 * twice: the later line) is left out and counted in *nbad, the first such
 * line's number (from 1) in *first_bad (0 when none). Fills at most cap
 * entries of out; a good line past cap counts as bad. The number of
 * entries.
 */
int rm_rtsun_table_parse(const char *text, rm_rtsun_entry_t *out, int cap, int *nbad, int *first_bad);

/* the entry of map (a bare name, compared without case); NULL when the table has none */
const rm_rtsun_entry_t *rm_rtsun_table_find(const rm_rtsun_entry_t *t, int n, const char *map);

/*
 * A world model's name as the table keys it: "maps/base1.bsp" gives
 * "base1" (the path's last part, without ".bsp"). 1 with out filled; 0,
 * out empty, when the result is empty or does not fit in len.
 */
int rm_rtsun_map_key(const char *model_name, char *out, size_t len);

/*
 * The sky's share of a range of the world's triangles: the surface records
 * of recs (nrecs, in buffer order, as the model loader made them) whose
 * index range lies in first_index .. first_index + num_indices of indices
 * (nindices), each triangle's area from verts (nverts) into *total and,
 * for a record whose material id (rm_rt_material, TASK53) is the sky's,
 * into *sky. 1 with both set; 0, both 0, when an index is outside verts or
 * a record's range is outside indices.
 */
int rm_rtsun_sky_area(const rm_surfrec_t *recs, long nrecs, const rm_wvert_t *verts, unsigned long nverts,
		const unsigned *indices, unsigned long nindices, unsigned long first_index, unsigned long num_indices,
		double *sky, double *total);

/* 1 when the sky is at least RM_RTSUN_SKY_MIN of the face area (total above 0, both finite): the pass runs */
int rm_rtsun_sky_enough(double sky, double total);

/* the mean colour of npix RGBA8 pixels, 0 to 1 a channel (alpha left out); 0, 0, 0 for none */
void rm_rtsun_mean(const unsigned char *rgba, size_t npix, float *out);

/*
 * sky_color from the sky box's six faces' means (gl1's order rt, bk, lf, ft,
 * up, dn; have[i] 0 for a face without an image): the mean of the five
 * faces above the horizon (dn left out), scaled so its brightest channel
 * is 1: the sky's hue, its strength left to r_rt_sunscale. White when no
 * face has an image or the mean is black.
 */
void rm_rtsun_sky_color(const float (*means)[3], const int *have, float *out);

/*
 * r_rt_sun_show (development): 0 the picture, 1 the mask (white where the
 * term was not added, black where it was), 2 the classes (black not a
 * traced world pixel, grey above the floor, blue facing away, red
 * shadowed, white lit). v clamped to 0 to RM_RTSUN_SHOW_MAX before it is
 * cut to an int; 0 for a value that is not finite.
 */
#define RM_RTSUN_SHOW_MAX 2
int rm_rtsun_show(float v);

/* the view of the kernel (rm_rtsun.metal's rm_rtsun_view, 144 bytes) */
typedef struct
{
	float inv[16];      /* GL clip coordinates to world: the inverse of the jittered projection times the modelview */
	float vp[4];        /* the view's viewport at the render size: x, y, w, h in pixels, from the top left */
	float eye[4];       /* the view's origin */
	float dir[4];       /* the unit vector toward the sun (rm_rtsun_dir) */
	float sun[4];       /* sky_color x r_rt_sunscale, a channel */
	float bias_k;       /* mtl_rtao_bias's k: 4 x 2^-24 / gl_znear */
	float floor_;       /* rm_rtsun_floor */
	unsigned show;      /* rm_rtsun_show */
	unsigned pad;
} rm_rtsun_view_t;

/*
 * The view of a G-buffer view: proj (jittered) and world as R_SetupGL and
 * RM_GB_Begin left them, its viewport at the render size, the eye and the
 * near plane's distance (TASK55's rm_rts_view), the direction, sky_color,
 * the scale, the floor and the view as the functions above read them. 0
 * when the product cannot be inverted or znear is not above 0.
 */
int rm_rtsun_view(const float *proj, const float *world, const rm_viewport_t *vp, const float *eye, float znear,
		const float *dir, const float *sky, float scale, float floor_, unsigned show, rm_rtsun_view_t *out);

/* One frame's sun pass, for the r_rt_stats 2 line (counted on the GPU, read once the frame completed). */
typedef struct
{
	unsigned long frame;
	unsigned long pixels;       /* pixels the kernel ran on: the render size */
	unsigned long world;        /* lightmapped world pixels with a depth and a normal */
	unsigned long bright;       /* of those, at or above the floor: no ray, no term */
	unsigned long facing;       /* below the floor but facing away from the sun: no ray, no term */
	unsigned long rays;         /* below the floor and facing it: one ray each */
	unsigned long lit;          /* rays that reached the sky: the term added */
	double ms;                  /* the pass's GPU time; below 0 when not measured */
} rm_rtsun_stats_t;

/* the r_rt_stats 2 line of a frame's pass, without a newline: 1 when it fits in len; 0 with buf empty otherwise */
int rm_rtsun_stats_format(const rm_rtsun_stats_t *s, char *buf, size_t len);

/*
 * The map's line, printed once a map: its sky area, its share, whether the
 * pass runs, and the direction with where it came from (the table or
 * r_rt_sun_angles). 1 when it fits in len; 0 with buf empty otherwise.
 */
int rm_rtsun_map_format(const char *map, double sky, double total, int from_table, float pitch, float yaw,
		char *buf, size_t len);

/* ---- rmb_rtsun.m: the Metal back end --------------------------------------------------- */

/* the kernel's and the development view's pipelines: 1, or 0 with err set; counted as RM_OBJ_RT */
int rmb_rtsun_open(char *err, size_t errlen);

/* releases the pipelines, the output textures and the counters */
void rmb_rtsun_close(void);

/*
 * Before the G-buffer raster: the kernel's outputs at the render size w x
 * h (the class, R8Uint, and the light the resolve reads, RGBA16Unorm). 1
 * when the view can trace; 0 when the pass is not open or they cannot be
 * made.
 */
int rmb_rtsun_prepare(unsigned long w, unsigned long h);

/* the view the next G-buffer view traces with (copied); NULL turns the pass off for that view */
void rmb_rtsun_set_view(const rm_rtsun_view_t *v);

/* the stats of the frames whose pass completed since the last call, at most cap; their number */
int rmb_rtsun_stats(rm_rtsun_stats_t *s, int cap);

/* TASK179: the effect table's backend rows (rm_rtfx.c): rmb_rtsun_trace, its light in io's place, and rmb_rtsun_show */
void rmb_rtsun_fx_trace(rmb_rtfx_io_t *io);
void rmb_rtsun_fx_show(rmb_rtfx_io_t *io);

#ifdef __OBJC__
/*
 * rmb_gbuf_resolve's hooks (rmb_gbuf.m). rmb_rtsun_trace, after RT5's AO
 * kernel: the kernel on the frame's command buffer over the render-size
 * depth, normal and material attachments, the raster's light (lm, the
 * stylized lightmap the floor gate reads) and the light the resolve would
 * read (base: lm, or RT5's lightmap x AO); out, the light with the sun's
 * term added, for the resolve to read in its place, or nil when nothing
 * was traced. rmb_rtsun_show, in the resolve's pass after the resolve:
 * draws r_rt_sun_show's view, when the frame traced and it is on; 1 when
 * it drew.
 */
id<MTLTexture> rmb_rtsun_trace(id<MTLTexture> depth, id<MTLTexture> normal, id<MTLTexture> material,
		id<MTLTexture> lm, id<MTLTexture> base);
int rmb_rtsun_show(id<MTLRenderCommandEncoder> enc);
#endif

#endif /* RM_RTSUN_H */
