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
#include "../common/mtl_rts_math.h"
#include "../common/mtl_rtdir_math.h"
#include "../common/mtl_rtsun_math.h"
#include "../common/mtl_rtsky_math.h"

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
 * counts as seen), 4 the dynamic lights' term alone, with their shadow
 * rays (TASK188 slice 3b), 5 the sky light's term alone and 6 the sun's
 * term alone, each with its ray (TASK189). v clamped to 0 to
 * RM_RTDIR_SHOW_MAX before it is cut to an int; 0 for a value that is not
 * finite.
 */
#define RM_RTDIR_SHOW_MAX 6
#define RM_RTDIR_SHOW_DIRECT 1
#define RM_RTDIR_SHOW_VIS 2
#define RM_RTDIR_SHOW_NOSHADOW 3
#define RM_RTDIR_SHOW_DLIGHTS 4
#define RM_RTDIR_SHOW_SKY ((int)MTL_RTSKY_SHOW_SKY)
#define RM_RTDIR_SHOW_SUN ((int)MTL_RTSKY_SHOW_SUN)
int rm_rtdir_show(float v);

/* a dynamic light as the kernel reads it (rm_rtdir.metal's rm_rtdir_dlight, 32 bytes; TASK188 slice 3b) */
typedef struct
{
	float pos[4];       /* origin, intensity */
	float color[4];     /* the refdef's rgb, 0 */
} rm_rtdir_dlight_t;

/*
 * One dynamic light in the kernel's form: 1 with *out written, 0 when
 * rm_dlight_ok refuses it (a value not finite or past its bound; the
 * renderer's checked copy, R_TakeDlights, has none such, so the engine
 * side's light i is RT3's and rm_rts_carried's light i too).
 */
int rm_rtdir_dlight(const float *origin, const float *color, float intensity, rm_rtdir_dlight_t *out);

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

/*
 * qrad3's patch light of an emissive face (BaseLightForFace's refl x value,
 * times the normalized reflectivity, MakePatchForFace) in rgb, in the
 * lightmap's units: value x refl x refl / max(refl). Returns 1 when it is
 * at least RM_RTDIR_DIRECT_LIGHT in some channel (qrad3's DIRECT_LIGHT,
 * which both the face's direct light and its own full-bright light take),
 * else 0. A value that is not finite or below 0 reads 0.
 */
int rm_rtdir_patch_light(int value, const float *refl, float *rgb);

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

/*
 * TASK188 slice 3b: an emissive face drawn at its own light. qrad3 adds a
 * face's patch light to its own lightmap ("the texture itself should still
 * be full bright", BuildFacelights); under RT-primary the kernel adds it to
 * the traced light of a pixel on that face, from this per-triangle table:
 * out[t] the triangle's word (mtl_rtdir_self_pack of
 * rm_rtdir_patch_light), 0 for a triangle of no emissive face. The
 * triangles are the map's index buffer's (rm_rt_materials' walk: surface
 * record s, which is face s, covers its num_indices / 3 triangles from
 * first_index / 3), ntris of them, out zeroed first. A face of e is marked
 * when face says it is a world face and its patch light reaches
 * RM_RTDIR_DIRECT_LIGHT; a record outside the buffer marks nothing.
 * Returns the faces marked, or -1 for no table (out NULL or ntris 0).
 */
long rm_rtdir_self_table(const rm_rtl_emitters_t *e, rm_rtdir_face_fn face, void *ctx, const rm_surfrec_t *recs,
		long nrecs, unsigned *out, unsigned long ntris);

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
 * The exposure fit's ratio: the lightmap's sum to the traced light's sum
 * over the same samples (TASK188 slice 3c: a map's luxels,
 * rm_rtdir_fit_map), so the traced map's mean light is the stock map's;
 * clamped to RM_RTDIR_FIT_MIN to RM_RTDIR_FIT_MAX. prev when there is no
 * sample or the traced sum is 0 (no light reaches a sample).
 */
float rm_rtdir_fit(unsigned long lm_sum, unsigned long raw_sum, unsigned long samples, float prev);

/*
 * The exposure a frame uses: r_rt_direct_exposure when above 0 (at most
 * RM_RTDIR_EXPOSURE_MAX), else fit. *fixed set to 1 for the cvar's.
 */
float rm_rtdir_exposure(float cvar, float fit, int *fixed);

/* the view of the kernel (rm_rtdir.metal's rm_rtdir_view, 176 bytes) */
typedef struct
{
	float inv[16];      /* GL clip coordinates to world, as rm_rts_view */
	float vp[4];        /* the view's viewport at the render size */
	float eye[4];       /* the view's origin */
	float bias_k;       /* mtl_rtao_bias's k */
	float exposure;     /* r_rt_direct_exposure's, else the map's fit (rm_rtdir_fit_map) */
	unsigned nlights;   /* the lights in the buffer */
	unsigned show;      /* rm_rtdir_show */
	unsigned ndlights;  /* the frame's dynamic lights (the back end's, rmb_rtdir_set_dlights), 0 from rm_rtdir_view */
	unsigned nself;     /* the per-triangle table's entries (the back end's, rmb_rtdir_set_self), 0 from rm_rtdir_view */
	unsigned frame;     /* TASK189: the sky ray's noise frame (rm_rtao_frame of r_rt_direct_seed), 0 from rm_rtdir_view */
	unsigned pad;
	float sun_dir[4];   /* TASK189: the unit direction toward the sun, 0; 0 from rm_rtdir_view */
	float sun[4];       /* TASK189: sky_color x r_rt_sunscale a channel, 0; 0 from rm_rtdir_view (no sun ray) */
	float sky[4];       /* TASK189: the sky's mean colour x r_rt_direct_sky a channel, 0; 0 from rm_rtdir_view (no sky ray) */
} rm_rtdir_view_t;

/*
 * The view of a G-buffer view, as rm_rtsun_view: 0 when the product
 * cannot be inverted or znear is not above 0.
 */
int rm_rtdir_view(const float *proj, const float *world, const rm_viewport_t *vp, const float *eye, float znear,
		float exposure, unsigned nlights, unsigned show, rm_rtdir_view_t *out);

/* TASK189: r_rt_direct_sky as the pass takes it: 0 to MTL_RTSKY_SCALE_MAX, 0 for a value that is not finite */
float rm_rtdir_sky_scale(float v);

/*
 * TASK189: the sky light and the sun of RT-primary's light model into a
 * view made by rm_rtdir_view. sky the sky's mean colour (rm_rtl_sky_mean,
 * NULL when the map has no sky image: no sky ray) times sky_scale
 * (rm_rtdir_sky_scale's); sun_dir the unit direction toward the sun
 * (rm_rtsun_dir of TASK56's angles) and sun its colour, sky_color times
 * r_rt_sunscale (rm_rtsun_sky_color, rm_rtsun_scale), as RT2's view takes
 * them (NULL: no sun ray); frame the sky ray's noise frame (rm_rtao_frame).
 * A channel that is not finite or below 0 reads 0, and so does a
 * direction that is not finite.
 */
void rm_rtdir_view_open(rm_rtdir_view_t *v, const float *sky, float sky_scale, const float *sun_dir, const float *sun,
		unsigned frame);

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
	unsigned long sky_rays;     /* TASK189: the sky rays traced (one a world pixel at most, MTL_RTSKY_RAYS) */
	unsigned long sky_hits;     /* the ones that reached the sky */
	unsigned long sun_rays;     /* the sun rays traced (one a world pixel facing the sun at most, MTL_RTSKY_SUN_RAYS) */
	unsigned long sun_lit;      /* the ones that reached the sky toward the sun */
	float exposure;             /* the exposure the frame used */
	double ms;                  /* the pass's GPU time; below 0 when not measured */
} rm_rtdir_stats_t;

/*
 * TASK188 slice 3c: the exposure fit at map load, from a sample of
 * luxels (docs/plans/presentation-modes.md section 4.1). A sample is a
 * point on a world face, its unit normal, and the face's lightmap there:
 * its maps summed with every style at 1, as qrad3 baked them, in the
 * lightmap's bytes. It enters the fit only when that lightmap is neither
 * near black (its brightest channel under RM_RTDIR_FIT_DARK) nor clipped
 * (a channel of the summed maps at RM_RTDIR_FIT_CLIP or more, the sum the
 * stock renderer holds to 255 (R_BuildLightMap), so two styles of 130 count as clipped): a dark
 * room that a bright fixture's traced light reaches would pull the ratio
 * down and crush the map, and a clipped luxel holds less than its light. The map's
 * exposure is rm_rtdir_fit of the entered samples' sums (mtl_rtdir_q of
 * the lightmap and of the traced light, both over 255), from 1.
 */
#define RM_RTDIR_FIT_DARK 16.0f
#define RM_RTDIR_FIT_CLIP 255.0f

/* the samples a map's fit takes, at most; the faces it takes them from, about (every n-th face past this) */
#define RM_RTDIR_FIT_POINTS 8192
#define RM_RTDIR_FIT_FACES 1024

/* a sample's start is this far off its face along the normal; its ray stops a unit short of the light, as the kernel's */
#define RM_RTDIR_FIT_BIAS 1.0f

typedef struct
{
	float pos[3];
	float normal[3];    /* unit, toward the side the face is seen from */
	float lm[3];        /* the lightmap there, every style at 1, in bytes */
} rm_rtdir_fitpt_t;

/* 1 when nothing solid lies between a and b (a world line test) */
typedef int (*rm_rtdir_clear_fn)(void *ctx, const float *a, const float *b);

typedef struct
{
	unsigned long lm, raw;  /* the entered samples' sums (mtl_rtdir_q) */
	unsigned long n;        /* the samples entered */
	unsigned long dark;     /* left out: near black */
	unsigned long clipped;  /* left out: clipped */
	unsigned long rays;     /* the lines tested */
	unsigned long visits;   /* rm_rtdir_fit_tree's: the tree's nodes those lines visited */
	int over;               /* rm_rtdir_fit_tree's: past RM_RTDIR_FIT_VISITS, the exposure 1 */
	float exposure;
} rm_rtdir_mapfit_t;

/* 1 when a sample of lightmap lm (3 bytes' worth, every style at 1) enters the fit; 0 near black, clipped or not a number */
int rm_rtdir_fit_enters(const float *lm);

/*
 * The traced light at p (unit normal n), every style at 1, in the
 * lightmap's units, into out[3]: the kernel's rule (rm_rtdir_direct) on
 * the host, the MTL_RTDIR_MAX_RAYS strongest candidates traced strongest
 * first, the first MTL_RTDIR_MAX_TRACED that clear reaches summed, with
 * clear as the shadow ray. No dynamic light (none at map load) and no
 * own light (a fixture's faces take no sample). Its rays added to *rays.
 */
void rm_rtdir_fit_light(const rm_rtdir_light_t *l, long n, const float *p, const float *nrm, rm_rtdir_clear_fn clear,
		void *ctx, float *out, unsigned long *rays);

/* the map's exposure over npts samples (rm_rtdir_fit_enters, rm_rtdir_fit_light); 1 with none entered; out may be NULL */
float rm_rtdir_fit_map(const rm_rtdir_light_t *l, long n, const rm_rtdir_fitpt_t *pts, long npts, rm_rtdir_clear_fn clear,
		void *ctx, rm_rtdir_mapfit_t *out);

/*
 * A face's lightmap at the luxel nearest the texture coordinates (s, t)
 * (its texturemins taken off, in luxels of 16 units), its nummaps maps of
 * smax by tmax luxels summed into out[3]: the luxel held to the face.
 */
void rm_rtdir_fit_luxel(const unsigned char *samples, int nummaps, int smax, int tmax, float s, float t, float *out);

/*
 * A sample's texture coordinates on its face, for rm_rtdir_fit_luxel:
 * pos on the texinfo's axes vecs (four floats each, the offset last),
 * less the face's texturemins, in luxels of 16 units, into st[2].
 */
void rm_rtdir_fit_st(const float *pos, const float vecs[2][4], const short *texturemins, float *st);

/* the texinfo flags (the engine's SURF_*) whose faces take no sample: a fixture, translucent, the sky, a warp */
#define RM_RTDIR_SURF_LIGHT 0x1
#define RM_RTDIR_SURF_SKY 0x4
#define RM_RTDIR_SURF_WARP 0x8
#define RM_RTDIR_SURF_TRANS33 0x10
#define RM_RTDIR_SURF_TRANS66 0x20

/* 1 when a face of texinfo flags (has_samples: a lightmap) takes samples: a lightmap, and none of those five flags */
int rm_rtdir_fit_face_ok(int flags, int has_samples);

/*
 * The fit's sample stride over the total candidate points of the faces
 * it walks: every k-th point is taken, k the least that keeps them at
 * cap or fewer (1 while total is at most cap), so a map past the cap is
 * sampled across its whole face list, not its first faces alone.
 */
long rm_rtdir_fit_every(long total, long cap);

/* the deepest the fit's line test walks a map's tree; past it a line counts as blocked */
#define RM_RTDIR_FIT_DEPTH 1024

/*
 * The node visits the fit's line tests make in one map, at most: past it
 * every line counts as blocked and the map's exposure is 1, so a crafted
 * tree cannot stall the map's first RT-primary frame (r_rt_stats prints
 * a map's count on its fit line).
 */
#define RM_RTDIR_FIT_VISITS (1UL << 26)

/* the engine's CONTENTS_SOLID: a leaf with it blocks the fit's line */
#define RM_RTDIR_CONTENTS_SOLID 1

/* one node of the caller's tree, as rm_rtdir_tree_t's read gives it */
typedef struct
{
	int leaf;           /* 1 for a leaf */
	int contents;       /* a leaf's contents (RM_RTDIR_CONTENTS_SOLID blocks) */
	float normal[3];    /* a node's plane: normal . p - dist is in front at 0 or more */
	float dist;
	const void *child[2]; /* a node's children: in front, behind */
} rm_rtdir_node_t;

typedef struct
{
	const void *root;
	void (*read)(const void *node, rm_rtdir_node_t *out);
	unsigned long visits;   /* the nodes visited so far */
	unsigned long cap;      /* RM_RTDIR_FIT_VISITS, or a test's */
	int over;               /* a line wanted more than cap: every line since reads blocked */
} rm_rtdir_tree_t;

/*
 * The fit's line test (an rm_rtdir_clear_fn, ctx an rm_rtdir_tree_t):
 * 1 when the segment a to b crosses no solid leaf of the tree, as
 * R_RecursiveLightPoint walks it (a point at 0 is in front; a segment
 * crossing a plane is split there, each half walked down its side). 0
 * past RM_RTDIR_FIT_DEPTH, for a missing child, and past the visit cap.
 */
int rm_rtdir_tree_clear(void *ctx, const float *a, const float *b);

/* rm_rtdir_fit_map with rm_rtdir_tree_clear over tree; 1 (and out->over set) when the walk passed its visit cap */
float rm_rtdir_fit_tree(const rm_rtdir_light_t *l, long n, const rm_rtdir_fitpt_t *pts, long npts, rm_rtdir_tree_t *tree,
		rm_rtdir_mapfit_t *out);

/* the r_rt_stats 2 line of a frame's pass, without a newline: 1 when it fits in len; 0 with buf empty otherwise */
int rm_rtdir_stats_format(const rm_rtdir_stats_t *s, int nlights, int fixed, char *buf, size_t len);

/*
 * TASK189: the r_rt_stats 2 line of a frame's sky light and sun, without
 * a newline: 1 when it fits in len; 0 with buf empty otherwise
 */
int rm_rtdir_open_format(const rm_rtdir_stats_t *s, char *buf, size_t len);

/* the map's line, once the lights are uploaded: 1 when it fits in len; 0 with buf empty otherwise */
int rm_rtdir_map_format(const char *map, const rm_rtdir_pack_t *p, char *buf, size_t len);

/* ---- rmb_rtdir.m: the Metal back end --------------------------------------------------- */

/* the kernel's and the development view's pipelines: 1, or 0 with err set; counted as RM_OBJ_RT */
int rmb_rtdir_open(char *err, size_t errlen);

/* releases the pipelines, the outputs, the lights and the counters */
void rmb_rtdir_close(void);

/* the map's lights (copied into a buffer the frames read); 1, or 0 when it cannot be made (n 0 keeps none) */
int rmb_rtdir_set_lights(const rm_rtdir_light_t *l, long n);

/* TASK188 slice 3b: the map's per-triangle table (rm_rtdir_self_table); 1, or 0 when it cannot be made (n 0 keeps none) */
int rmb_rtdir_set_self(const unsigned *t, unsigned long n);

/* TASK188 slice 3b: the frame's dynamic lights (rm_rtdir_dlights), at most MTL_RTDIR_MAX_DLIGHTS; before rmb_rtdir_set_view */
void rmb_rtdir_set_dlights(const rm_rtdir_dlight_t *d, int n);

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
