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

/*
 * The presentation modes (TASK183). Enhanced is the stock lighting model
 * with the ray-traced layers on at their defaults (the plan's section
 * 2.2): each effect switch below reads 1 under it, and every other
 * setting (the AO's size, the sun's angles, r_metalfx, ...) stays the
 * user's. Retro and RT-primary are later slices and decide nothing here.
 */
static const char *const pres_enhanced[] = { "r_rt_shadows", "r_rt_sun", "r_rt_ao", "r_rt_water" };

#define PRES_NENHANCED ((int)(sizeof(pres_enhanced) / sizeof(pres_enhanced[0])))

int
rm_pres_decide(float requested, int path, int *why)
{
	int w = RM_PRES_WHY_OK, mode = RM_PRES_STOCK;

	/* a value that is not a number compares false here and is no mode */
	if (!((requested >= 0.0f) && (requested < 4.0f)))
	{
		w = RM_PRES_WHY_NOTBUILT;
	}
	else if ((int)requested == RM_PRES_ENHANCED)
	{
		if (path == RM_RTG_NONE)
		{
			w = RM_PRES_WHY_NORT;
		}
		else
		{
			mode = RM_PRES_ENHANCED;
		}
	}
	else if ((int)requested != RM_PRES_STOCK)
	{
		w = RM_PRES_WHY_NOTBUILT;
	}

	if (why)
	{
		*why = w;
	}

	return mode;
}

float
rm_pres_value(int mode, const char *name, float user)
{
	int i;

	if ((mode != RM_PRES_ENHANCED) || !name)
	{
		return user;
	}

	for (i = 0; i < PRES_NENHANCED; i++)
	{
		if (!strcmp(pres_enhanced[i], name))
		{
			return 1.0f;
		}
	}

	return user;
}

const char *
rm_pres_name(int mode)
{
	switch (mode)
	{
		case RM_PRES_STOCK:
			return "stock";
		case RM_PRES_ENHANCED:
			return "enhanced";
		case RM_PRES_RETRO:
			return "retro";
		case RM_PRES_RTPRIMARY:
			return "ray traced";
		default:
			return "";
	}
}

int
rm_pres_format(float requested, int mode, int why, char *buf, size_t len)
{
	const int r = ((requested >= 0.0f) && (requested < 1e9f)) ? (int)requested : -1;
	const char *rname = (r >= 0) ? rm_pres_name(r) : "";
	char req[48];

	if (len == 0)
	{
		return 0;
	}

	if (why == RM_PRES_WHY_OK)
	{
		if (mode == RM_PRES_ENHANCED)
		{
			return rtg_put(snprintf(buf, len,
					"ref_metal: presentation: enhanced (r_presentation %d): r_rt_shadows, r_rt_sun, r_rt_ao and r_rt_water read 1, the user's values kept",
					mode), buf, len);
		}

		return rtg_put(snprintf(buf, len, "ref_metal: presentation: %s (r_presentation %d)", rm_pres_name(mode), mode),
				buf, len);
	}

	if (r < 0)
	{
		snprintf(req, sizeof(req), "%g", requested);
	}
	else if (rname[0])
	{
		snprintf(req, sizeof(req), "%d (%s)", r, rname);
	}
	else
	{
		snprintf(req, sizeof(req), "%d", r);
	}

	return rtg_put(snprintf(buf, len, "ref_metal: presentation: %s; r_presentation %s reads 0: %s", rm_pres_name(mode),
			req, (why == RM_PRES_WHY_NORT) ? "no ray tracing on this device" : "not in this build"), buf, len);
}

int
rm_ab_value(const char *spec, const char *name, char *out, size_t len)
{
	const char *p = spec ? strchr(spec, '|') : NULL;
	const size_t nlen = name ? strlen(name) : 0;

	if (!p || !nlen || !out || !len)
	{
		return 0;
	}

	for (p++; *p; )
	{
		const char *eq = strchr(p, '=');
		const char *end = strchr(p, ';');

		if (!eq || !end || eq > end)
		{
			return 0;   /* not a B side the harness wrote */
		}

		if (((size_t)(eq - p) == nlen) && !strncmp(p, name, nlen))
		{
			const size_t vlen = (size_t)(end - eq - 1);

			if (vlen >= len)
			{
				return 0;
			}

			memcpy(out, eq + 1, vlen);
			out[vlen] = '\0';

			return 1;
		}

		p = end + 1;
	}

	return 0;
}

int
rm_ab_format(const char *spec, char *buf, size_t len)
{
	const char *bar = spec ? strchr(spec, '|') : NULL;
	int n = 0;

	if (!bar)
	{
		return rtg_put(snprintf(buf, len, "ref_metal: A/B: A: your settings"), buf, len);
	}

	for (const char *p = bar + 1; *p; p++)
	{
		n += (*p == ';');
	}

	return rtg_put(snprintf(buf, len, "ref_metal: A/B: %.*s (%d settings overlaid, no cvar written)",
			(int)(bar - spec), spec, n), buf, len);
}
