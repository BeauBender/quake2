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
 * The engine side of ref_metal's ray-traced sun and sky shadow (RT4,
 * TASK56; header/rm_rtsun.h, header/rm_rtsunscene.h): the cvars (r_rt_sun,
 * r_rt_sunscale, r_rt_sunfloor, r_rt_sun_angles) and the development view
 * r_rt_sun_show, the per-map table built in from scripts/rt/sun-angles.txt,
 * the map's sky share (measured once a map from the world's retained
 * buffers, TASK53's material ids), the frame's decision (with it the view
 * takes the G-buffer path; a map under 2 % sky never takes it, so it draws
 * as with the effect off), the view handed to the back end before the
 * raster, and the pass's r_rt_stats 2 line. With r_rt_sun 0 (the default)
 * nothing here builds or allocates anything.
 *
 * =======================================================================
 */

#include "header/local.h"
#include "header/rm_rt.h"
#include "header/rm_rtsun.h"
#include "header/rm_rtsunscene.h"
#include "header/rm_gbufscene.h"

/*
 * scripts/rt/sun-angles.txt, built into the renderer with a 0 after it.
 * The path is the top directory's: the Makefile runs the compiler there,
 * and lists the file as a prerequisite of this object.
 */
extern const char rm_rtsun_table_text[] __asm__("rm_rtsun_table_label");

__asm__(
	".section __TEXT,__const\n"
	".private_extern rm_rtsun_table_label\n"
	"rm_rtsun_table_label:\n"
	".incbin \"scripts/rt/sun-angles.txt\"\n"
	".byte 0\n"
	".text\n");

#define RM_RTSUN_TABLE_MAX 256

static cvar_t *r_rt_sun;
static cvar_t *r_rt_sunscale;
static cvar_t *r_rt_sunfloor;
static cvar_t *r_rt_sun_angles;
static cvar_t *r_rt_sun_show;
static cvar_t *r_rt_stats_ref;

static rm_rtsun_entry_t sun_table[RM_RTSUN_TABLE_MAX];
static int sun_ntable;

static int sun_device;              /* rmb_caps_t.raytracing */
static int sun_opened;              /* rmb_rtsun_open succeeded */
static int sun_refused;             /* it failed in this context: not tried again */
static int sun_active;              /* this frame traces the sun */
static int sun_viewed;              /* RM_RTSun_View handed this frame's view to the back end */
static const rm_world_t *sun_world; /* the map measured */
static char sun_map[MAX_QPATH];     /* its model name (a new map's buffers may take the old one's address) */
static int sun_sky_ok;              /* its sky is at least 2 % of its face area */
static const rm_rtsun_entry_t *sun_entry;   /* its line of the table, NULL when none */
static int sun_angles_bad;          /* r_rt_sun_angles' value did not parse (printed once a value) */

void
RM_RTSun_Init(const rmb_caps_t *caps)
{
	int nbad, first;

	r_rt_sun = ri.Cvar_Get("r_rt_sun", "0", CVAR_ARCHIVE);
	r_rt_sunscale = ri.Cvar_Get("r_rt_sunscale", "0.35", CVAR_ARCHIVE);
	r_rt_sunfloor = ri.Cvar_Get("r_rt_sunfloor", "0.5", CVAR_ARCHIVE);
	r_rt_sun_angles = ri.Cvar_Get("r_rt_sun_angles", "55 45", CVAR_ARCHIVE);
	r_rt_sun_show = ri.Cvar_Get("r_rt_sun_show", "0", 0);
	r_rt_stats_ref = ri.Cvar_Get("r_rt_stats", "0", 0);
	sun_device = caps->raytracing != 0;
	sun_opened = sun_refused = sun_active = sun_viewed = sun_sky_ok = sun_angles_bad = 0;
	sun_world = NULL;
	sun_entry = NULL;

	sun_ntable = rm_rtsun_table_parse(rm_rtsun_table_text, sun_table, RM_RTSUN_TABLE_MAX, &nbad, &first);

	if (nbad)
	{
		R_Printf(PRINT_ALL, "ref_metal: rt sun: scripts/rt/sun-angles.txt: %d bad lines (the first, line %d) left out\n",
				nbad, first);
	}
}

void
RM_RTSun_Shutdown(void)
{
	if (sun_opened)
	{
		rmb_rtsun_close();
	}

	/* the map's measure is kept: it holds no GPU object, and RM_RTSun_Init forgets it */
	sun_opened = sun_active = sun_viewed = 0;
}

/* the direction this frame: the map's line of the table, else r_rt_sun_angles, else the default */
static void
RM_RTSun_Angles(float *pitch, float *yaw)
{
	if (sun_entry)
	{
		*pitch = sun_entry->pitch;
		*yaw = sun_entry->yaw;
		return;
	}

	if (rm_rtsun_parse_angles(r_rt_sun_angles->string, pitch, yaw))
	{
		sun_angles_bad = 0;
		return;
	}

	if (!sun_angles_bad)
	{
		R_Printf(PRINT_ALL, "ref_metal: rt sun: r_rt_sun_angles \"%s\" is not \"pitch yaw\" (0 to 90, 0 to 360); %g %g\n",
				r_rt_sun_angles->string, RM_RTSUN_PITCH, RM_RTSUN_YAW);
		sun_angles_bad = 1;
	}

	*pitch = RM_RTSUN_PITCH;
	*yaw = RM_RTSUN_YAW;
}

/* the map's sky share and its line of the table, once a map */
static void
RM_RTSun_Measure(void)
{
	const model_t *wm = r_worldmodel;
	const rm_world_t *w = wm->world;
	char key[RM_RTSUN_MAPNAME + 1], line[RM_STATS_LINE];
	double sky = 0, total = 0;
	float pitch, yaw;

	sun_world = w;
	Q_strlcpy(sun_map, wm->name, sizeof(sun_map));
	sun_sky_ok = 0;
	sun_entry = NULL;

	if (rm_rtsun_map_key(wm->name, key, sizeof(key)))
	{
		sun_entry = rm_rtsun_table_find(sun_table, sun_ntable, key);
	}
	else
	{
		Q_strlcpy(key, "?", sizeof(key));
	}

	/* the world's own faces (the first model's range), the brush entities' left out */
	if ((wm->numsubmodels < 1) || !rm_rtsun_sky_area(w->recs, (long)w->size.surfs, w->verts, w->size.verts, w->indices,
			w->size.indices, (unsigned long)wm->submodels[0].firstindex, (unsigned long)wm->submodels[0].numindices,
			&sky, &total))
	{
		R_Printf(PRINT_ALL, "ref_metal: rt sun: %s: the world's faces cannot be measured; no sun\n", key);
		return;
	}

	sun_sky_ok = rm_rtsun_sky_enough(sky, total);
	RM_RTSun_Angles(&pitch, &yaw);

	if (rm_rtsun_map_format(key, sky, total, sun_entry != NULL, pitch, yaw, line, sizeof(line)))
	{
		R_Printf(PRINT_ALL, "%s\n", line);
	}
}

/* 1 when the map has at least 2 % sky; measured on the first call a map */
static int
RM_RTSun_SkyOk(void)
{
	if (!r_worldmodel || !r_worldmodel->world)
	{
		return 0;
	}

	if ((sun_world != r_worldmodel->world) || strcmp(sun_map, r_worldmodel->name))
	{
		RM_RTSun_Measure();
	}

	return sun_sky_ok;
}

/*
 * The sun's ask for the frame's ray-tracing scene (RM_RT_Wanted, the map
 * load's RM_RT_MapLoaded and the scene's keep flag): none on a map under
 * 2 % sky, so nothing is built or kept for it there. The map is measured
 * only while r_rt_sun is on, on a device with ray tracing.
 */
int
RM_RTSun_Wanted(void)
{
	return r_rt_sun && rm_rt_wanted(r_rt_sun->value, sun_device) &&
		rm_rtsun_wanted(r_rt_sun->value, sun_device, RM_RTSun_SkyOk());
}

void
RM_RTSun_Frame(const refdef_t *fd, int scene_ready)
{
	rm_rtsun_stats_t st[RMB_FRAMES_IN_FLIGHT];
	int i, n;

	/* no view survives from an earlier frame: only RM_RTSun_View, before this frame's raster, hands one over */
	sun_active = sun_viewed = 0;
	rmb_rtsun_set_view(NULL);

	/* under 2 % sky: nothing opens, the view keeps its path, the frame is the effect off's */
	if (r_rt_sun && rm_rtsun_on(r_rt_sun->value, sun_device, r_fullbright->value) && fd &&
		!(fd->rdflags & RDF_NOWORLDMODEL) && RM_RTSun_SkyOk() && scene_ready)
	{
		if (!sun_opened && !sun_refused)
		{
			char err[256];

			sun_opened = rmb_rtsun_open(err, sizeof(err));

			if (!sun_opened)
			{
				sun_refused = 1;
				R_Printf(PRINT_ALL, "ref_metal: rt sun: %s; no sun\n", err);
			}
		}

		sun_active = sun_opened;
	}

	if (sun_opened && !RM_RTSun_Wanted())
	{
		/* turned off, or a map under 2 % sky (one opened on an earlier map): nothing kept */
		rmb_rtsun_close();
		sun_opened = 0;
	}

	if (!sun_opened)
	{
		return;
	}

	n = rmb_rtsun_stats(st, RMB_FRAMES_IN_FLIGHT);

	for (i = 0; i < n && r_rt_stats_ref && (r_rt_stats_ref->value >= 2); i++)
	{
		char line[RM_STATS_LINE];

		if (rm_rtsun_stats_format(&st[i], line, sizeof(line)))
		{
			R_Printf(PRINT_ALL, "%s\n", line);
		}
	}
}

int
RM_RTSun_Active(void)
{
	return sun_active;
}

/* only a view that was handed over is traced and shown */
int
RM_RTSun_ShowOnly(void)
{
	return sun_active && sun_viewed && r_rt_sun_show && rm_rtsun_show(r_rt_sun_show->value);
}

void
RM_RTSun_View(const rm_view_t *view)
{
	rm_rtsun_view_t v;
	float pitch, yaw, dir[3], sky[3], means[6][3];
	int have[6], w, h;

	sun_viewed = 0;

	if (!sun_active)
	{
		return;
	}

	/* everything the trace needs that can fail, before the raster: the view's inverse and the outputs */
	RM_GB_TargetSize(&w, &h);
	RM_RTSun_Angles(&pitch, &yaw);
	rm_rtsun_dir(pitch, yaw, dir);
	RM_SkyMeans(means, have);
	rm_rtsun_sky_color((const float (*)[3])means, have, sky);

	if (!rm_rtsun_view(view->proj, view->world, &view->vp, r_newrefdef.vieworg, gl_znear->value, dir, sky,
			rm_rtsun_scale(r_rt_sunscale->value), rm_rtsun_floor(r_rt_sunfloor->value),
			(unsigned)rm_rtsun_show(r_rt_sun_show->value), &v) ||
		!rmb_rtsun_prepare((unsigned long)w, (unsigned long)h))
	{
		rmb_rtsun_set_view(NULL);
		return;
	}

	rmb_rtsun_set_view(&v);
	sun_viewed = 1;
}
