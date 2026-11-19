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
 * The pure part of ref_metal's hardware gate for ray tracing (RT8,
 * TASK60; docs/plans/metal-raytracing-design.md, section 4): the path a
 * device takes from what it reports, the table that classifies every
 * r_rt_* cvar as an effect switch or a setting, the per-frame step that
 * puts the effect switches back to 0 on a device without ray tracing,
 * and the report lines. rm_rtgate.c, held by tests/metal/rm_rtgate_test.c;
 * no Metal, no engine state. rm_main.c calls it at RI_Init (start-up and
 * vid_restart) and at RI_BeginFrame.
 *
 * =======================================================================
 */

#ifndef REF_METAL_RTGATE_H
#define REF_METAL_RTGATE_H

#include <stddef.h>

/* The ray-tracing path of a device. */
#define RM_RTG_NONE    0   /* no ray tracing: every effect switch reads 0 */
#define RM_RTG_COMPUTE 1   /* apple6 and up with supportsRaytracing: the compute passes (TASK24) */
#define RM_RTG_RENDER  2   /* apple9 with supportsRaytracing and supportsRaytracingFromRender (TASK25) */

/* Why the path is none (RM_RTG_WHY_OK when it is not). */
#define RM_RTG_WHY_OK       0
#define RM_RTG_WHY_NOFAMILY 1   /* the device reports no Apple GPU family */
#define RM_RTG_WHY_NORT     2   /* the device reports no supportsRaytracing */
#define RM_RTG_WHY_FAMILY   3   /* supportsRaytracing below apple6 */

/* The first Apple family of each path. */
#define RM_RTG_COMPUTE_FAMILY 6
#define RM_RTG_RENDER_FAMILY  9

typedef struct
{
	int path;     /* RM_RTG_NONE, RM_RTG_COMPUTE or RM_RTG_RENDER */
	int why;      /* RM_RTG_WHY_*: RM_RTG_WHY_OK unless the path is none */
	int family;   /* the highest Apple family reported (1 to 9), 0 for none */
} rm_rtg_t;

/*
 * The decision. families holds bit n - 1 for each Apple family n from 1 to
 * 9 the device supports (rmb_caps_t.families; other bits are not read);
 * raytracing and raytracing_render are supportsRaytracing and
 * supportsRaytracingFromRender, any value but 0 for yes. The family is
 * the highest one reported. The path is RM_RTG_RENDER from apple9 with
 * both flags, RM_RTG_COMPUTE from apple6 with supportsRaytracing (apple9
 * without supportsRaytracingFromRender included), else RM_RTG_NONE, whose
 * reason is checked in this order: no family, no supportsRaytracing (a
 * device that reports supportsRaytracingFromRender alone has no
 * structures to trace), a family below apple6.
 */
rm_rtg_t rm_rtg_decide(unsigned families, int raytracing, int raytracing_render);

/* 1 when the path traces rays (the scenes' device flag), 0 for none. */
int rm_rtg_traces(const rm_rtg_t *g);

/* The text of a reason, "" for RM_RTG_WHY_OK and for an unknown one. */
const char *rm_rtg_why_text(int why);

/*
 * The report line, printed at start-up and at vid_restart:
 *   ref_metal: ray tracing: compute path (apple7, supportsRaytracing)
 *   ref_metal: ray tracing: compute path (apple9, supportsRaytracing; render-stage available, not yet built)
 *   ref_metal: ray tracing: none (apple5: <reason>); the ray-tracing effects read 0
 *   ref_metal: ray tracing: none (<reason>); the ray-tracing effects read 0
 * the second for RM_RTG_RENDER, whose code (TASK25) is not built yet, so the compute passes run
 * there; the last for no family. 1 when it fits, else 0 and buf empty (len 0: 0).
 */
int rm_rtg_format(const rm_rtg_t *g, char *buf, size_t len);

/*
 * The line for one effect switch put back to 0:
 *   ref_metal: <name> reads 0: no ray tracing on this device (<reason>)
 * 1 when it fits, else 0 and buf empty (len 0: 0).
 */
int rm_rtg_reset_format(const char *name, const rm_rtg_t *g, char *buf, size_t len);

/*
 * The table of the r_rt_* cvars (rm_rtgate.c gives the reason of each
 * row): every name a Cvar_Get of src/client/refresh/metal creates, each an
 * effect switch (1: forced to 0 without ray tracing; its default is "0")
 * or a setting (0: left as it is). tests/check-ref-metal.sh fails when an
 * r_rt_ name created there is not in it, and when a row names no such cvar.
 * rm_rtg_ncvars counts the rows; rm_rtg_cvar gives row i's name and
 * whether it is an effect, NULL for i out of 0 to ncvars - 1;
 * rm_rtg_find the row of a name, -1 for none.
 */
int rm_rtg_ncvars(void);
const char *rm_rtg_cvar(int i, int *effect);
int rm_rtg_find(const char *name);

/* The name of the k-th effect switch of the table, in its order; NULL for k below 0 or past the last. */
const char *rm_rtg_effect(int k);

/* The effect switches the step can hold (the table has fewer). */
#define RM_RTG_MAX_EFFECTS 16

/* What the step remembers from one frame to the next: each switch's value. */
typedef struct
{
	float seen[RM_RTG_MAX_EFFECTS];
} rm_rtg_state_t;

/* What one step says to do: the switches to set to 0, and those to name in a line. */
typedef struct
{
	int nreset;
	int reset[RM_RTG_MAX_EFFECTS];
	int nsay;
	int say[RM_RTG_MAX_EFFECTS];
} rm_rtg_out_t;

/* Every value seen 0 (start-up and vid_restart). */
void rm_rtg_state_reset(rm_rtg_state_t *s);

/*
 * One step, before a frame draws, over the n effect switches (n clamped
 * to 0 to RM_RTG_MAX_EFFECTS) whose values are v[0] to v[n - 1]. With path
 * RM_RTG_NONE, each switch whose value is not 0 (a value that is not a
 * finite number included) is listed in reset, in order, and also in say
 * when its value is not the one seen at the last step: a line per change,
 * not per frame, even when a reset does not take. With any other path
 * nothing is listed. Every value is then remembered as seen; every index
 * listed is below n. Returns out->nreset.
 */
int rm_rtg_step(int path, const float *v, int n, rm_rtg_state_t *s, rm_rtg_out_t *out);

/*
 * The presentation modes (TASK183, docs/plans/presentation-modes.md),
 * selected by the archived integer r_presentation at start-up and at
 * vid_restart. A mode is an overlay: rm_pres_value gives the value the
 * renderer uses for a cvar in place of the user's, and no cvar is
 * written, so the user's own values come back when the mode is 0 again.
 * The values are never renumbered (config.cfg stores them).
 */
#define RM_PRES_STOCK 0
#define RM_PRES_ENHANCED 1
#define RM_PRES_RETRO 2
#define RM_PRES_RTPRIMARY 3

#define RM_PRES_WHY_OK 0
#define RM_PRES_WHY_NORT 1      /* the mode needs ray tracing; the path is none */
#define RM_PRES_WHY_NOTBUILT 2  /* a mode of a later slice, or a value that is no mode */

/*
 * The mode the renderer runs for the requested value (r_presentation, a
 * fraction truncated) on a device whose gate gave path: the value itself
 * when this build has the mode and the device runs it, else stock, with
 * the reason in *why (which may be NULL).
 */
int rm_pres_decide(float requested, int path, int *why);

/*
 * The value the renderer uses for the cvar name whose user value is user:
 * the mode's own value for a cvar the mode decides, else user. Stock
 * decides none. Enhanced turns on r_rt_shadows, r_rt_sun, r_rt_ao and
 * r_rt_water (1), and leaves every other setting the user's.
 */
float rm_pres_value(int mode, const char *name, float user);

/* the mode's name in the menu and the report lines; "" for none */
const char *rm_pres_name(int mode);

/*
 * The report line at start-up and at vid_restart: the mode in use, and,
 * when it is not the one requested, the value requested and why. 1 when
 * it fits in buf, else 0 with buf empty.
 */
int rm_pres_format(float requested, int mode, int why, char *buf, size_t len);

/*
 * The A/B toggle of the tuning harness (TASK195): r_rt_ab, which the
 * client's rt_ab writes, is empty for A (the user's values) or
 * "<label>|name=value;name=value;..." for B. It is an overlay as the
 * presentation is: rm_ab_value copies the B value of the cvar name into
 * out and returns 1 when spec is a B side that holds it, else 0 (the
 * user's value stands); no cvar is written. rm_ab_format is the report
 * line when the side changes ("ref_metal: A/B: A: your settings", or the
 * label and how many settings it holds); 1 when it fits in buf.
 */
int rm_ab_value(const char *spec, const char *name, char *out, size_t len);
int rm_ab_format(const char *spec, char *buf, size_t len);

/*
 * Whether an overlay supplies the value of the cvar name, as RM_Effect
 * reads it: the presentation mode decides it (rm_pres_value) or the B side
 * ab_spec holds it (rm_ab_value, a value of up to RM_AB_MAXVALUE - 1
 * characters). 0: the value read is the user's own.
 */
#define RM_AB_MAXVALUE 64
int rm_effect_overlaid(int pres_mode, const char *ab_spec, const char *name);

#endif
