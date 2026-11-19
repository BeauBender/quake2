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
 * TASK60): header/rm_rtgate.h says what each function does;
 * tests/metal/rm_rtgate_test.c holds them.
 *
 * =======================================================================
 */

#include <stdio.h>
#include <string.h>

#include "header/rm_rtgate.h"

#define RTG_FAMILY_FIRST 1
#define RTG_FAMILY_LAST 9   /* rmb_caps_t.families: apple1 to apple9 */

rm_rtg_t
rm_rtg_decide(unsigned families, int raytracing, int raytracing_render)
{
	rm_rtg_t g;
	int n;

	g.family = 0;

	for (n = RTG_FAMILY_FIRST; n <= RTG_FAMILY_LAST; n++)
	{
		if (families & (1u << (n - 1)))
		{
			g.family = n;
		}
	}

	g.path = RM_RTG_NONE;
	g.why = RM_RTG_WHY_OK;

	if (g.family == 0)
	{
		g.why = RM_RTG_WHY_NOFAMILY;
	}
	else if (!raytracing)
	{
		g.why = RM_RTG_WHY_NORT;
	}
	else if (g.family < RM_RTG_COMPUTE_FAMILY)
	{
		g.why = RM_RTG_WHY_FAMILY;
	}
	else if ((g.family >= RM_RTG_RENDER_FAMILY) && raytracing_render)
	{
		g.path = RM_RTG_RENDER;
	}
	else
	{
		g.path = RM_RTG_COMPUTE;
	}

	return g;
}

int
rm_rtg_traces(const rm_rtg_t *g)
{
	return g->path != RM_RTG_NONE;
}

const char *
rm_rtg_why_text(int why)
{
	switch (why)
	{
		case RM_RTG_WHY_NOFAMILY:
			return "no Apple GPU family reported";
		case RM_RTG_WHY_NORT:
			return "no supportsRaytracing";
		case RM_RTG_WHY_FAMILY:
			return "supportsRaytracing below apple6";
		default:
			return "";
	}
}

/* snprintf into buf: 1 when it fits, else 0 with buf empty */
static int
rtg_put(int n, char *buf, size_t len)
{
	if ((n < 0) || ((size_t)n >= len))
	{
		buf[0] = '\0';
		return 0;
	}

	return 1;
}

int
rm_rtg_format(const rm_rtg_t *g, char *buf, size_t len)
{
	if (len == 0)
	{
		return 0;
	}

	/* the decision stays three-way for TASK25, whose render-stage code is not built yet: until it
	   lands the scenes run the compute passes there too, so the line names that path */
	if (g->path == RM_RTG_RENDER)
	{
		return rtg_put(snprintf(buf, len,
				"ref_metal: ray tracing: compute path (apple%d, supportsRaytracing; render-stage available, not yet built)",
				g->family), buf, len);
	}

	if (g->path == RM_RTG_COMPUTE)
	{
		return rtg_put(snprintf(buf, len, "ref_metal: ray tracing: compute path (apple%d, supportsRaytracing)", g->family),
				buf, len);
	}

	if (g->family == 0)
	{
		return rtg_put(snprintf(buf, len, "ref_metal: ray tracing: none (%s); the ray-tracing effects read 0",
				rm_rtg_why_text(g->why)), buf, len);
	}

	return rtg_put(snprintf(buf, len, "ref_metal: ray tracing: none (apple%d: %s); the ray-tracing effects read 0",
			g->family, rm_rtg_why_text(g->why)), buf, len);
}

int
rm_rtg_reset_format(const char *name, const rm_rtg_t *g, char *buf, size_t len)
{
	if (len == 0)
	{
		return 0;
	}

	return rtg_put(snprintf(buf, len, "ref_metal: %s reads 0: no ray tracing on this device (%s)", name,
			rm_rtg_why_text(g->why)), buf, len);
}

/*
 * Every r_rt_* cvar of ref_metal, sorted by name. An effect switch (1)
 * turns on a pass that traces rays, which a device without ray tracing
 * cannot run: it is forced to 0 there, so it reads what the picture shows.
 * A setting (0) is left as it is: it tunes or views an effect and does
 * nothing while the effect is off, or it chooses a raster path that every
 * Metal GPU runs. RT11's r_rt_scale, when it lands, is a setting (it reads
 * 1 there, which RT11 does).
 */
typedef struct
{
	const char *name;
	int effect;
} rtg_cvar_t;

static const rtg_cvar_t rtg_cvars[] = {
	/* RT5's ambient occlusion pass (TASK57): its rays are what it draws */
	{ "r_rt_ao", 1 },
	/* the AO's floor, ray length, rays, trace size and noise step: read only while r_rt_ao runs */
	{ "r_rt_ao_min", 0 },
	{ "r_rt_ao_range", 0 },
	{ "r_rt_ao_rays", 0 },
	{ "r_rt_ao_scale", 0 },
	{ "r_rt_ao_seed", 0 },
	/* the AO's development view: drawn only while the AO pass runs (RM_RTAO_Show) */
	{ "r_rt_ao_show", 0 },
	/* TASK194's read-only report of the gate's path for the menu: the renderer sets it, it traces nothing */
	{ "r_rt_available", 0 },
	/* TASK173's preview grey: it replaces a lightmap sample on the G-buffer raster path, which every
	   Metal GPU runs, and traces nothing itself; left alone, it does nothing where the path is off */
	{ "r_rt_flatlight", 0 },
	/* RT2's G-buffer (TASK54): a raster path every Metal GPU runs, built and pinned on the VM; the
	   picture the same within one level, and no ray traced */
	{ "r_rt_gbuffer", 0 },
	/* the G-buffer's size, its images' view, the view's jitter: the raster path's, no ray traced */
	{ "r_rt_gbuffer_scale", 0 },
	{ "r_rt_gbuffer_show", 0 },
	/* whether the view weapon enters the ray-traced scene, which no device without ray tracing builds */
	{ "r_rt_gunshadow", 0 },
	{ "r_rt_jitter_x", 0 },
	{ "r_rt_jitter_y", 0 },
	/* RT2's depth and motion on the raster path (an upscaler's input), on any Metal GPU */
	{ "r_rt_motion", 0 },
	/* a check of the motion images, the raster path's */
	{ "r_rt_motion_test", 0 },
	/* RT3's dynamic-light shadows (TASK55): a ray each light and pixel */
	{ "r_rt_shadows", 1 },
	/* the shadows' development view: drawn only while the shadow pass runs (RM_RTS_Show) */
	{ "r_rt_shadows_show", 0 },
	/* the structures' report: draws nothing, and without ray tracing builds nothing and says so once */
	{ "r_rt_stats", 0 },
	/* RT4's sun and sky shadow (TASK56): a ray each pixel toward the sun */
	{ "r_rt_sun", 1 },
	/* the sun's direction, development view, floor and strength: read only while r_rt_sun runs
	   (RM_RTSun_Show needs sun_active) */
	{ "r_rt_sun_angles", 0 },
	{ "r_rt_sun_show", 0 },
	{ "r_rt_sunfloor", 0 },
	{ "r_rt_sunscale", 0 },
	/* RT6's water reflections (TASK58): a ray each water pixel */
	{ "r_rt_water", 1 },
	/* the reflections' hit shading, tilt, blend and development view: read only while r_rt_water runs */
	{ "r_rt_water_hits", 0 },
	{ "r_rt_water_normal", 0 },
	{ "r_rt_water_reflect", 0 },
	{ "r_rt_water_show", 0 },
};

#define RTG_NCVARS ((int)(sizeof(rtg_cvars) / sizeof(rtg_cvars[0])))

int
rm_rtg_ncvars(void)
{
	return RTG_NCVARS;
}

const char *
rm_rtg_cvar(int i, int *effect)
{
	if ((i < 0) || (i >= RTG_NCVARS))
	{
		return NULL;
	}

	if (effect)
	{
		*effect = rtg_cvars[i].effect;
	}

	return rtg_cvars[i].name;
}

int
rm_rtg_find(const char *name)
{
	int i;

	for (i = 0; name && (i < RTG_NCVARS); i++)
	{
		if (!strcmp(rtg_cvars[i].name, name))
		{
			return i;
		}
	}

	return -1;
}

const char *
rm_rtg_effect(int k)
{
	int i;

	for (i = 0; (k >= 0) && (i < RTG_NCVARS); i++)
	{
		if (rtg_cvars[i].effect && (k-- == 0))
		{
			return rtg_cvars[i].name;
		}
	}

	return NULL;
}

void
rm_rtg_state_reset(rm_rtg_state_t *s)
{
	memset(s, 0, sizeof(*s));
}

/* the same value: equal, or both not a number */
static int
rtg_same(float a, float b)
{
	return (a == b) || ((a != a) && (b != b));
}

int
rm_rtg_step(int path, const float *v, int n, rm_rtg_state_t *s, rm_rtg_out_t *out)
{
	int i;

	out->nreset = 0;
	out->nsay = 0;
	n = (n < 0) ? 0 : ((n > RM_RTG_MAX_EFFECTS) ? RM_RTG_MAX_EFFECTS : n);

	for (i = 0; i < n; i++)
	{
		if ((path == RM_RTG_NONE) && (v[i] != 0.0f))
		{
			out->reset[out->nreset++] = i;

			if (!rtg_same(v[i], s->seen[i]))
			{
				out->say[out->nsay++] = i;
			}
		}

		s->seen[i] = v[i];
	}

	return out->nreset;
}
