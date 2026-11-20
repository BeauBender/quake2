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
 * The table of ref_metal's ray-traced effects (TASK179, AR-17;
 * docs/plans/metal-raytracing-design.md, "Adding an effect"). Each row of
 * rm_rtfx.c names one effect, its switch (the cvar the hardware gate,
 * rm_rtgate.c, forces to 0 without ray tracing: the gate takes it from
 * here) and its entry points, NULL where the effect has none. rm_main.c,
 * rm_rtscene.c, rm_gbufscene.c and rmb_gbuf.m reach the effects only
 * through the walks below, in the order of rm_rtfx_order; the pipeline a
 * 3D draw takes (rm_rtfx_pipe) is decided here too.
 * tests/metal/rm_rtfx_test.c holds the order, the switches and the rule,
 * and tests/check-ref-metal.sh refuses an effect named in rm_main.c or
 * rmb_gbuf.m outside the table.
 *
 * Pure C: the backend's state of one resolve (rmb_rtfx_io_t) is opaque
 * here and defined in rmb_internal.h, under __OBJC__.
 *
 * =======================================================================
 */

#ifndef RM_RTFX_H
#define RM_RTFX_H

/* the effects, in the table's rows (rm_rtfx.c); RM_RTFX_COUNT rows */
enum
{
	RM_RTFX_SHADOWS,   /* RT3 (TASK55): the dynamic lights' shadows */
	RM_RTFX_AO,        /* RT5 (TASK57): the ambient occlusion */
	RM_RTFX_SUN,       /* RT4 (TASK56): the sun and sky shadow */
	RM_RTFX_WATER,     /* RT6 (TASK58): the water reflections */
	RM_RTFX_COUNT
};

/*
 * The order every walk takes, each id once: the shadows, the AO, the sun,
 * the water. The AO goes before the sun, which adds its term to the
 * lightmap x AO light; the water's reflections go last, from the
 * resolve's lit result.
 */
extern const int rm_rtfx_order[RM_RTFX_COUNT];

/* The effect's name ("shadows") and its switch ("r_rt_shadows"); NULL for an id out of range. */
const char *rm_rtfx_name(int id);
const char *rm_rtfx_switch(int id);

/* 1 when name is the switch of a row of the table (the gate's effect switches), else 0. */
int rm_rtfx_is_switch(const char *name);

/*
 * The pipeline of a 3D draw (rmb_gbuf_pipe3d): pass is the open pass
 * (RMB_GB_PASS_*, rm_gbuf.h), pipe the stock pipeline (RM_PIPE_*), surf 1
 * when the raster has the surface attachment (TASK131). forward is asked,
 * with io, only for a blended world face in the forward pass of a traced
 * view; it returns 1 when an effect has a pipeline for it (RT6's
 * reflection on the translucent water). Returns:
 */
#define RM_RTFX_PIPE_NONE        0   /* nil: a pipe out of range, or a pass that draws no 3D */
#define RM_RTFX_PIPE_STOCK       1   /* rmb_pipe3d's, outside the G-buffer's passes */
#define RM_RTFX_PIPE_RASTER      2   /* the G-buffer raster's */
#define RM_RTFX_PIPE_RASTER_SURF 3   /* the raster's with the surface attachment */
#define RM_RTFX_PIPE_MOTION      4   /* the forward pass's, with the motion attachment */
#define RM_RTFX_PIPE_FORWARD     5   /* the one forward put in io */

typedef struct rmb_rtfx_io_s rmb_rtfx_io_t;

int rm_rtfx_pipe(int pass, int pipe, int surf, int (*forward)(rmb_rtfx_io_t *io), rmb_rtfx_io_t *io);

/*
 * The backend's walks (rmb_gbuf.m), each row's entry in the order, the
 * rows without one skipped. rm_rtfx_mark: on the raster's last draw.
 * rm_rtfx_trace: after the raster ended, before the resolve's pass; each
 * may replace io's light. rm_rtfx_resolve: in the resolve's pass, 1 when
 * an effect drew the resolve (the first that does; else the caller draws
 * the stock one). rm_rtfx_show: over the resolve, in its pass.
 * rm_rtfx_post: after the resolve; an effect opens its pass through io.
 * rm_rtfx_forward: 1 when an effect put a forward pipeline in io (the
 * first). rm_rtfx_bind: what those pipelines read, on the forward pass's
 * encoder in io. rm_rtfx_view_end: the view's passes ended.
 */
void rm_rtfx_mark(rmb_rtfx_io_t *io);
void rm_rtfx_trace(rmb_rtfx_io_t *io);
int rm_rtfx_resolve(rmb_rtfx_io_t *io);
void rm_rtfx_show(rmb_rtfx_io_t *io);
void rm_rtfx_post(rmb_rtfx_io_t *io);
int rm_rtfx_forward(rmb_rtfx_io_t *io);
void rm_rtfx_bind(rmb_rtfx_io_t *io);
void rm_rtfx_view_end(void);

/*
 * The scene's walks (rm_main.c, rm_rtscene.c, rm_gbufscene.c), over each
 * effect's RM_*_Init, _Shutdown, _Wanted, _Frame, _Active, _ShowOnly and
 * _View. rm_rtfx_wanted, _active and _show_only: 1 when any effect's is.
 * They take refdef_t and rm_view_t, so their prototypes need local.h.
 */
#ifdef RM_LOCAL_H
void rm_rtfx_init(const rmb_caps_t *caps);
void rm_rtfx_shutdown(void);
int rm_rtfx_wanted(void);
void rm_rtfx_frame(const refdef_t *fd, int scene_ready);
int rm_rtfx_active(void);
int rm_rtfx_show_only(void);
void rm_rtfx_view(const rm_view_t *view);
#endif

#endif /* RM_RTFX_H */
