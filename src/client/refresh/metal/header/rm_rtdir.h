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
 * The RT-primary presentation's direct light in ref_metal (TASK188, PM6
 * slice 1; docs/plans/presentation-modes.md, section 4.1). Under
 * r_presentation 3 only, a compute pass after the G-buffer raster gives
 * every lightmapped world pixel the light of the map's point and spot
 * lights (TASK187's light entities) in place of its lightmap: for each
 * light in range, qrad3's linear falloff times N.L times the light's
 * colour and lightstyle, with one shadow ray through the frame's
 * acceleration structure. The sum is in the lightmap's units (a byte of
 * 255 reads 1) and is scaled by one exposure, the fit of rm_rtdir_fit
 * or a fixed r_rt_direct_exposure.
 *
 * The rm_rtdir_* functions are plain C with neither SDL nor Metal
 * (rm_rtdir.c), host-tested by tests/metal/rm_rtdir_test.c with the
 * per-pixel arithmetic of common/mtl_rtdir_math.h; the rmb_rtdir_*
 * functions are the back end's (rmb_rtdir.m).
 *
 * =======================================================================
 */

#ifndef RM_RTDIR_H
#define RM_RTDIR_H

#include <stddef.h>

#include "rmb.h"
#include "rm_rtfx.h"
#include "rm_rtlights.h"
#include "../common/mtl_shader_math.h"
#include "../common/mtl_rtdir_math.h"

/* ---- rm_rtdir.c: no SDL, no Metal ----------------------------------------------------- */

/* the fit's bounds: the exposure never leaves them, so a frame is neither black nor blown out by it */
#define RM_RTDIR_FIT_MIN 0.25f
#define RM_RTDIR_FIT_MAX 16.0f

/* r_rt_direct_exposure's upper bound; 0 (the default) takes the fit */
#define RM_RTDIR_EXPOSURE_MAX 64.0f

/*
 * 1 when a frame traces the direct light: the presentation in use is
 * RT-primary (pres, RM_PRES_*), r_rt_direct reads not 0 (the mode's
 * overlay reads it 1), the device has ray tracing, and r_fullbright is 0.
 * A value that is not finite counts as 0 for r_rt_direct and as set for
 * r_fullbright, as rm_surf.c reads it.
 */
int rm_rtdir_on(int pres, float direct, int device_rt, float fullbright);

/*
 * r_rt_direct_show (development): 0 the picture, 1 the direct term alone
 * (the light the resolve reads, as grey on white albedo), 2 the shadow
 * visibility (each world pixel's share of the lights in range whose ray
 * reached them: white all, black none; dark blue no light in range), 3
 * the direct term alone with no shadow ray traced (every light in range
 * counts as seen). v clamped to 0 to RM_RTDIR_SHOW_MAX before it is cut
 * to an int; 0 for a value that is not finite.
 */
#define RM_RTDIR_SHOW_MAX 3
#define RM_RTDIR_SHOW_DIRECT 1
#define RM_RTDIR_SHOW_VIS 2
#define RM_RTDIR_SHOW_NOSHADOW 3
int rm_rtdir_show(float v);

/* a light as the kernel reads it (rm_rtdir.metal's rm_rtdir_light, 48 bytes) */
typedef struct
{
	float pos[4];       /* origin, intensity */
	float color[4];     /* the normalized _color, the style (0 to 255, as a float) */
	float spot[4];      /* a spotlight's unit direction, the cosine of its cone (MTL_RTDIR_NOSPOT for a point light) */
} rm_rtdir_light_t;

/* what rm_rtdir_pack left out */
typedef struct
{
	long kept;
	long dark;          /* intensity at most 0: lights nothing under the linear falloff */
	long untargeted;    /* a spotlight whose target is missing: qrad3 leaves its normal at zero, so it lights nothing */
	long over;          /* past cap */
	long clamped;       /* kept, with the intensity held to RM_RTDIR_MAX_INTENSITY */
} rm_rtdir_pack_t;

/*
 * The intensity a light is packed with, at most. qrad3 bounds no light
 * value (rm_rtlights.c takes any finite one, as CreateDirectLights does),
 * and its falloff is linear, (intensity - dist): a light reaches as far
 * as its intensity. The +-4096 units on each axis are the stock tools'
 * and the network's bound (the protocol's coordinate, MSG_WriteCoord in
 * src/common/movemsg.c, a short of 1/8 units), not the BSP's: neither the
 * BSP loader nor collision.c holds a vertex to it. Within it no two points
 * are farther apart than 8192 x sqrt(3), about 14189, so a light of 16384
 * reaches every point of such a map already. Above 16384 a light is
 * dimmer than qrad3's wherever it reaches, (16384 - dist) against
 * (intensity - dist), not only shorter-reaching; holding it here keeps
 * the sum of RM_RTL_MAX_LIGHTS lights finite.
 * qrad3's default is 300 (RM_RTL_DEFAULT_LIGHT).
 */
#define RM_RTDIR_MAX_INTENSITY 16384.0f

/*
 * The lights of l in the kernel's form, at most cap of them, in l's
 * order: a point light's spot[3] is MTL_RTDIR_NOSPOT, a spotlight's is
 * cos(cone) (degrees) and its direction is l's; the intensity at most
 * RM_RTDIR_MAX_INTENSITY. A light of intensity at most 0 or not finite,
 * and a spotlight with a missing target, are left out (counted in *why,
 * which may be NULL). Returns the number kept.
 */
long rm_rtdir_pack(const rm_rtl_lights_t *l, rm_rtdir_light_t *out, long cap, rm_rtdir_pack_t *why);

/* ---- the emissive faces as area lights (TASK188 slice 2) ------------------------------- */

/*
 * The emissive faces packed at most (qrad3 has no cap), after the map's
 * lights in the buffer; past it an emitter is counted, not kept. The
 * buffer holds at most RM_RTDIR_MAX_UPLOAD records.
 */
#define RM_RTDIR_MAX_EMITTERS 2048
#define RM_RTDIR_MAX_UPLOAD (RM_RTL_MAX_LIGHTS + RM_RTDIR_MAX_EMITTERS)

/*
 * An emissive face's intensity, at most: its range is
 * sqrt(intensity / MTL_RTDIR_EMIT_CUT), so 16384^2 reaches as far as the
 * strongest point light (RM_RTDIR_MAX_INTENSITY) and the sum stays finite.
 */
#define RM_RTDIR_MAX_EMIT_INTENSITY (16384.0f * 16384.0f)

/* qrad3's: a surface light's intensity is its patch's light times its area times this (direct_scale, -direct) */
#define RM_RTDIR_DIRECT_SCALE 0.4f

/* qrad3's: a patch whose light is below this in every channel sends no direct light (DIRECT_LIGHT) */
#define RM_RTDIR_DIRECT_LIGHT 3.0f

/* qrad3's: a patch's light sits a unit in front of its face (MakePatchForFace) */
#define RM_RTDIR_EMIT_OFFSET 1.0f

/*
 * The emitted colour, the choice TASK187 left open (docs/runbook/TASK188.md).
 * 1: qrad3's reflectivity (CalcTextureReflectivity), the texel mean,
 * doubled when its largest channel is under 0.5; 0: TASK187's raw texel
 * mean. An unread texture reads RM_RTDIR_EMIT_UNREAD: qrad3's 0 (it lights
 * nothing); TASK187's grey is RM_RTL_UNREAD_GREY.
 */
#define RM_RTDIR_EMIT_QRAD3_COLOUR 1
#define RM_RTDIR_EMIT_UNREAD 0.0f

/* the most points of a face's polygon read; a face with more is counted shapeless */
#define RM_RTDIR_EMIT_MAXPTS 64

/*
 * The reflectivity of an emitter's texture (RM_RTDIR_EMIT_QRAD3_COLOUR):
 * mean (0 to 1 a channel, rm_rtl_tex_t's) when read is not 0, else
 * RM_RTDIR_EMIT_UNREAD in each channel. A value that is not finite or
 * below 0 reads 0.
 */
void rm_rtdir_reflectivity(const float *mean, int read, float *out);

/* an emissive face's polygon, as the engine side reads it from the world */
typedef struct
{
	const float *pts;   /* the first point's x, y, z; stride floats from one point to the next */
	int stride;
	int n;              /* its points */
	float normal[3];    /* the face's plane normal, flipped for SURF_PLANEBACK */
} rm_rtdir_face_t;

/* why rm_rtdir_emitter kept a face or not */
enum
{
	RM_RTDIR_EMIT_KEPT = 0,
	RM_RTDIR_EMIT_SHAPELESS,    /* fewer than 3 or more than RM_RTDIR_EMIT_MAXPTS points, no area, a bad normal or a value not finite */
	RM_RTDIR_EMIT_DIM           /* below RM_RTDIR_DIRECT_LIGHT in every channel: qrad3 sends no direct light */
};

/*
 * One emissive face as an area light, qrad3's emit_surface of its patch
 * (CreateDirectLights), the face taken whole as one patch: the patch's
 * light is value x refl x refl / max(refl) (BaseLightForFace, then times
 * the normalized reflectivity), its colour that light normalized, its
 * intensity the light's largest channel times the polygon's area times
 * RM_RTDIR_DIRECT_SCALE, at most RM_RTDIR_MAX_EMIT_INTENSITY (*clamped set
 * to 1 then), at the points' mean (WindingCenter) plus
 * RM_RTDIR_EMIT_OFFSET along the normal; style 0 (qrad3's surface lights
 * have no style); spot the normal and MTL_RTDIR_EMITTER. Returns
 * RM_RTDIR_EMIT_KEPT with *out written, else why it was left out.
 */
int rm_rtdir_emitter(const rm_rtdir_face_t *f, int value, const float *refl, rm_rtdir_light_t *out, int *clamped);

/* what rm_rtdir_pack_emitters left out */
typedef struct
{
	long kept;
	long bmodel;        /* not a world face: qrad3 lets only the world's faces emit (MakePatchForFace) */
	long shapeless;     /* RM_RTDIR_EMIT_SHAPELESS */
	long dim;           /* RM_RTDIR_EMIT_DIM, an unread texture's among them */
	long unread;        /* kept or not, the emitters whose texture was not read (or past the texture cap) */
	long over;          /* past cap */
	long clamped;       /* kept, with the intensity held to RM_RTDIR_MAX_EMIT_INTENSITY */
} rm_rtdir_emitpack_t;

/* the polygon of face index face of the faces lump: 1 with *out written, 0 when it is not a world face */
typedef int (*rm_rtdir_face_fn)(void *ctx, long face, rm_rtdir_face_t *out);

/*
 * The emissive faces of e in the kernel's form (rm_rtdir_emitter), in e's
 * order, at most cap of them; face gives each one's polygon. Each one's
 * reflectivity is its texture's (rm_rtdir_reflectivity; a texture past
 * the cap is unread). Counted in *why, which may be NULL. Returns the
 * number kept.
 */
long rm_rtdir_pack_emitters(const rm_rtl_emitters_t *e, rm_rtdir_face_fn face, void *ctx, rm_rtdir_light_t *out,
		long cap, rm_rtdir_emitpack_t *why);

/* the emitters' line of the map, once uploaded: 1 when it fits in len; 0 with buf empty otherwise */
int rm_rtdir_emit_format(const char *map, const rm_rtdir_emitpack_t *p, char *buf, size_t len);

/*
 * The per-frame style table: style s's rgb (refdef lightstyles, 0 to
 * RM_RTL_STYLES - 1) times modulate (r_modulate, as the lightmap takes
 * it), in out[3 * s]. styles may be NULL (every style 1). n is the number
 * of styles given; the rest read 1. A value that is not finite reads 0.
 */
void rm_rtdir_styles(const float (*styles)[3], int n, float modulate, float *out);

/*
 * The exposure fit, the stated rule of this slice: the ratio of the
 * lightmap's sum to the raw direct light's sum over the same sampled
 * world pixels (every MTL_RTDIR_FIT_STEP-th pixel a row and a column, of
 * one completed frame), so the traced frame's mean light is the stock
 * frame's; clamped to RM_RTDIR_FIT_MIN to RM_RTDIR_FIT_MAX. prev, the
 * exposure in use (1 at a map's first frame), when there is no sample or
 * the raw sum is 0 (no light reaches a sampled pixel).
 */
float rm_rtdir_fit(unsigned long lm_sum, unsigned long raw_sum, unsigned long samples, float prev);

/*
 * The exposure a frame uses: r_rt_direct_exposure when above 0 (at most
 * RM_RTDIR_EXPOSURE_MAX), else fit. *fixed set to 1 for the cvar's.
 */
float rm_rtdir_exposure(float cvar, float fit, int *fixed);

/* the view of the kernel (rm_rtdir.metal's rm_rtdir_view, 112 bytes) */
typedef struct
{
	float inv[16];      /* GL clip coordinates to world, as rm_rts_view */
	float vp[4];        /* the view's viewport at the render size */
	float eye[4];       /* the view's origin */
	float bias_k;       /* mtl_rtao_bias's k */
	float exposure;     /* r_rt_direct_exposure's, else 0 for the back end's fit at the lag (rm_rtdir_fit_lag) */
	unsigned nlights;   /* the lights in the buffer */
	unsigned show;      /* rm_rtdir_show */
} rm_rtdir_view_t;

/*
 * The view of a G-buffer view, as rm_rtsun_view: 0 when the product
 * cannot be inverted or znear is not above 0.
 */
int rm_rtdir_view(const float *proj, const float *world, const rm_viewport_t *vp, const float *eye, float znear,
		float exposure, unsigned nlights, unsigned show, rm_rtdir_view_t *out);

/* One frame's direct-light pass, for the r_rt_stats 2 line (counted on the GPU, read once the frame completed). */
typedef struct
{
	unsigned long frame;
	unsigned long pixels;       /* pixels the kernel ran on */
	unsigned long world;        /* lightmapped world pixels with a depth and a normal */
	unsigned long long inrange; /* lights in range and facing, summed over those pixels (64-bit on the GPU too) */
	unsigned long long rays;    /* shadow rays traced: one each candidate a pixel tries (0 under r_rt_direct_show 3) */
	unsigned long long lit;     /* rays that reached their light */
	unsigned long capped;       /* pixels past the bound (mtl_rtdir_capped): fewer lights seen than the cap, more in range */
	unsigned long fit_lm;       /* the fit's sums and samples (rm_rtdir_fit) */
	unsigned long fit_raw;
	unsigned long fit_n;
	float exposure;             /* the exposure the frame used */
	double ms;                  /* the pass's GPU time; below 0 when not measured */
} rm_rtdir_stats_t;

/*
 * TASK188 slice 3: the exposure fit at a fixed lag. The pass's back end
 * keeps RM_RTDIR_FIT_LAG slots (RMB_FRAMES_IN_FLIGHT), frame f in slot
 * f % RM_RTDIR_FIT_LAG, so the slot frame f takes last held frame
 * f - RM_RTDIR_FIT_LAG, which the frame semaphore has seen complete
 * before frame f began. lagged is that slot's record (its frame and the
 * fit's sums; NULL for none). Its fit (rm_rtdir_fit, from prev) is frame
 * f's exposure when lagged->frame is exactly f - RM_RTDIR_FIT_LAG and
 * after reset (the last frame traced before the map's lights were set);
 * else prev, the exposure in use, is kept. So a frame's exposure follows
 * from the frames traced before it, never from the order in which they
 * completed.
 */
#define RM_RTDIR_FIT_LAG 3

float rm_rtdir_fit_lag(const rm_rtdir_stats_t *lagged, unsigned long frame, unsigned long reset, float prev);

/* the r_rt_stats 2 line of a frame's pass, without a newline: 1 when it fits in len; 0 with buf empty otherwise */
int rm_rtdir_stats_format(const rm_rtdir_stats_t *s, int nlights, int fixed, char *buf, size_t len);

/* the map's line, once the lights are uploaded: 1 when it fits in len; 0 with buf empty otherwise */
int rm_rtdir_map_format(const char *map, const rm_rtdir_pack_t *p, char *buf, size_t len);

/* ---- rmb_rtdir.m: the Metal back end --------------------------------------------------- */

/* the kernel's and the development view's pipelines: 1, or 0 with err set; counted as RM_OBJ_RT */
int rmb_rtdir_open(char *err, size_t errlen);

/* releases the pipelines, the outputs, the lights and the counters */
void rmb_rtdir_close(void);

/*
 * the map's lights (copied into a buffer the frames read); 1, or 0 when it cannot be made (n 0 keeps none); the
 * exposure fit starts again at 1, from the frames encoded after this call (rm_rtdir_fit_lag's reset)
 */
int rmb_rtdir_set_lights(const rm_rtdir_light_t *l, long n);

/* before the raster: the outputs at the render size; 1 when the view can trace */
int rmb_rtdir_prepare(unsigned long w, unsigned long h);

/* the view and the style table (3 x RM_RTL_STYLES floats) the next G-buffer view traces with; NULL turns it off */
void rmb_rtdir_set_view(const rm_rtdir_view_t *v, const float *styles);

/* the stats of the frames whose pass completed since the last call, at most cap, in slot order; their number */
int rmb_rtdir_stats(rm_rtdir_stats_t *s, int cap);

/* TASK179: the effect table's backend rows (rm_rtfx.c): the trace, its light in io's place, and the view */
void rmb_rtdir_fx_trace(rmb_rtfx_io_t *io);
void rmb_rtdir_fx_show(rmb_rtfx_io_t *io);

#endif /* RM_RTDIR_H */
