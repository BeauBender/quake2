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
#include "header/rm_rtfx.h"

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
 * Every r_rt_* cvar of ref_metal, sorted by name. An effect switch (the
 * switch of a row of the effect table, rm_rtfx_is_switch: TASK179 keeps
 * the one list there) turns on a pass that traces rays, which a device
 * without ray tracing cannot run: it is forced to 0 there, so it reads
 * what the picture shows. Every other name is a setting, left as it
 * is: it tunes or views an effect and does
 * nothing while the effect is off, or it chooses a raster path that every
 * Metal GPU runs. RT11's r_metalfx_scale, when it lands, is a setting (it reads
 * 1 there, which RT11 does).
 */
static const char *const rtg_cvars[] = {
	/* TASK195's A/B side: an overlay of the tuning settings' values, which turns on no pass itself */
	"r_rt_ab",
	/* RT5's ambient occlusion pass (TASK57): its rays are what it draws */
	"r_rt_ao",
	/* the AO's floor, ray length, rays, trace size and noise step: read only while r_rt_ao runs */
	"r_rt_ao_min",
	"r_rt_ao_range",
	"r_rt_ao_rays",
	"r_rt_ao_scale",
	"r_rt_ao_seed",
	/* the AO's development view: drawn only while the AO pass runs (RM_RTAO_Show) */
	"r_rt_ao_show",
	/* TASK194's read-only report of the gate's path for the menu: the renderer sets it, it traces nothing */
	"r_rt_available",
	/* TASK188's direct light of RT-primary (PM6): a ray each light in range and pixel, only under
	   r_presentation 3, whose overlay reads it 1 */
	"r_rt_direct",
	/* its fixed exposure and its development view: read only while the direct light runs */
	"r_rt_direct_exposure",
	"r_rt_direct_show",
	/* TASK173's preview grey: it replaces a lightmap sample on the G-buffer raster path, which every
	   Metal GPU runs, and traces nothing itself; left alone, it does nothing where the path is off */
	"r_rt_flatlight",
	/* RT2's G-buffer (TASK54): a raster path every Metal GPU runs, built and pinned on the VM; the
	   picture the same within one level, and no ray traced */
	"r_rt_gbuffer",
	/* the G-buffer's size, its images' view, the view's jitter: the raster path's, no ray traced */
	"r_rt_gbuffer_scale",
	"r_rt_gbuffer_show",
	/* whether the view weapon enters the ray-traced scene, which no device without ray tracing builds */
	"r_rt_gunshadow",
	"r_rt_jitter_x",
	"r_rt_jitter_y",
	/* RT2's depth and motion on the raster path (an upscaler's input), on any Metal GPU */
	"r_rt_motion",
	/* a check of the motion images, the raster path's */
	"r_rt_motion_test",
	/* RT3's dynamic-light shadows (TASK55): a ray each light and pixel */
	"r_rt_shadows",
	/* the shadows' development view: drawn only while the shadow pass runs (RM_RTS_Show) */
	"r_rt_shadows_show",
	/* the structures' report: draws nothing, and without ray tracing builds nothing and says so once */
	"r_rt_stats",
	/* RT4's sun and sky shadow (TASK56): a ray each pixel toward the sun */
	"r_rt_sun",
	/* the sun's direction, development view, floor and strength: read only while r_rt_sun runs
	   (RM_RTSun_Show needs sun_active) */
	"r_rt_sun_angles",
	"r_rt_sun_show",
	"r_rt_sunfloor",
	"r_rt_sunscale",
	/* RT7's temporal accumulation (TASK59): it blends what the effects traced and traces no ray itself;
	   read only while an effect it accumulates runs */
	"r_rt_temporal",
	/* RT6's water reflections (TASK58): a ray each water pixel */
	"r_rt_water",
	/* the reflections' hit shading, tilt, blend and development view: read only while r_rt_water runs */
	"r_rt_water_hits",
	"r_rt_water_normal",
	"r_rt_water_reflect",
	"r_rt_water_show",
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
		*effect = rm_rtfx_is_switch(rtg_cvars[i]);
	}

	return rtg_cvars[i];
}

int
rm_rtg_find(const char *name)
{
	int i;

	for (i = 0; name && (i < RTG_NCVARS); i++)
	{
		if (!strcmp(rtg_cvars[i], name))
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
		if (rm_rtfx_is_switch(rtg_cvars[i]) && (k-- == 0))
		{
			return rtg_cvars[i];
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
 * user's. Retro (TASK184, PM2) needs no ray tracing and runs on every
 * device; its post-process decides no cvar of its own, but plan call P8
 * (TASK197) has it read every ray-traced layer and r_metalfx as 0, so no
 * layer or scaler smears or relights the quantized view. r_rt_direct is
 * not in its table and keeps the user's value: that switch runs nothing
 * outside RT-primary (rm_rtdir_on). No cvar is written; the overlay is
 * read-only like Enhanced's.
 * RT-primary (TASK188, PM6 slice 1) turns on its direct light
 * (r_rt_direct) and, as the plan's section 2.2 has it, the water
 * reflections; AO is off (the bounce, PM7, replaces it), and so are the
 * sun and the dlight shadows until PM7 and PM6's second slice make them
 * terms of the light model, so no layer adds the lightmap's light back.
 */
typedef struct
{
	const char *name;
	float value;
} pres_row_t;

/*
 * A mode's overlay is a table of rows, a cvar and the value the mode gives
 * it. scripts/rt/preset-to-table.sh prints the rows of a saved preset in
 * this format (TASK195); a host test compiles them in through
 * RM_PRES_ENHANCED_ROWS.
 */
#ifndef RM_PRES_ENHANCED_ROWS
#define RM_PRES_ENHANCED_ROWS \
	{ "r_rt_shadows", 1.0f }, \
	{ "r_rt_sun", 1.0f }, \
	{ "r_rt_ao", 1.0f }, \
	{ "r_rt_water", 1.0f },
#endif

static const pres_row_t pres_enhanced[] = {
	RM_PRES_ENHANCED_ROWS
};

#define PRES_NENHANCED ((int)(sizeof(pres_enhanced) / sizeof(pres_enhanced[0])))

/* Retro's table (P8): the layers' switches and the scaler, all read as 0 (r_rt_direct is RT-primary's alone) */
static const pres_row_t pres_retro[] = {
	{ "r_rt_shadows", 0.0f },
	{ "r_rt_sun", 0.0f },
	{ "r_rt_ao", 0.0f },
	{ "r_rt_water", 0.0f },
	{ "r_rt_gunshadow", 0.0f },
	{ "r_metalfx", 0.0f },
};

#define PRES_NRETRO ((int)(sizeof(pres_retro) / sizeof(pres_retro[0])))

/* RT-primary's table (TASK188): the direct light on, the layers it replaces off, the water on */
static const pres_row_t pres_rtprimary[] = {
	{ "r_rt_direct", 1.0f },
	{ "r_rt_shadows", 0.0f },
	{ "r_rt_sun", 0.0f },
	{ "r_rt_ao", 0.0f },
	{ "r_rt_water", 1.0f },
};

#define PRES_NRTPRIMARY ((int)(sizeof(pres_rtprimary) / sizeof(pres_rtprimary[0])))

/* the table a mode overlays, NULL when it decides nothing */
static const pres_row_t *
pres_table(int mode, int *n)
{
	if (mode == RM_PRES_ENHANCED)
	{
		*n = PRES_NENHANCED;
		return pres_enhanced;
	}

	if (mode == RM_PRES_RETRO)
	{
		*n = PRES_NRETRO;
		return pres_retro;
	}

	if (mode == RM_PRES_RTPRIMARY)
	{
		*n = PRES_NRTPRIMARY;
		return pres_rtprimary;
	}

	*n = 0;
	return NULL;
}

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
	else if ((int)requested == RM_PRES_RETRO)
	{
		mode = RM_PRES_RETRO;
	}
	else if ((int)requested == RM_PRES_RTPRIMARY)
	{
		/* by capability only (decision 0014): wherever the device traces rays */
		if (path == RM_RTG_NONE)
		{
			w = RM_PRES_WHY_NORT;
		}
		else
		{
			mode = RM_PRES_RTPRIMARY;
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
	int i, n;
	const pres_row_t *t = pres_table(mode, &n);

	if (!t || !name)
	{
		return user;
	}

	for (i = 0; i < n; i++)
	{
		if (!strcmp(t[i].name, name))
		{
			return t[i].value;
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

		if (mode == RM_PRES_RETRO)
		{
			return rtg_put(snprintf(buf, len,
					"ref_metal: presentation: retro (r_presentation %d): the 3D view in RGB565 with the 4 x 4 dither, r_retro_filter and r_retro_gamma read each frame; r_rt_shadows, r_rt_sun, r_rt_ao, r_rt_water, r_rt_gunshadow and r_metalfx read 0",
					mode), buf, len);
		}

		if (mode == RM_PRES_RTPRIMARY)
		{
			return rtg_put(snprintf(buf, len,
					"ref_metal: presentation: ray traced (r_presentation %d): the lightmap off at the visible world surface, the direct light of the map's lights in its place; r_rt_direct and r_rt_water read 1, r_rt_shadows, r_rt_sun and r_rt_ao read 0",
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
rm_effect_overlaid(int pres_mode, const char *ab_spec, const char *name)
{
	char b[RM_AB_MAXVALUE];

	if (!name)
	{
		return 0;
	}

	{
		int n;
		const pres_row_t *t = pres_table(pres_mode, &n);

		for (int i = 0; t && i < n; i++)
		{
			if (!strcmp(t[i].name, name))
			{
				return 1;
			}
		}
	}

	return ab_spec && ab_spec[0] && rm_ab_value(ab_spec, name, b, sizeof(b));
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
