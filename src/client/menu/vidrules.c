/*
 * Copyright (C) 2026 Yamagi Quake II contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * =======================================================================
 *
 * What the video menu shows for each renderer, see header/vidrules.h.
 *
 * =======================================================================
 */

#include <math.h>

#include "../../common/header/common.h"
#include "header/vidrules.h"

#define ARRSIZE(a) ((int)(sizeof(a) / sizeof((a)[0])))

vidref_t
VidRules_Renderer(const char *name)
{
	if (!name)
	{
		return VIDREF_UNKNOWN;
	}
	if (Q_stricmp(name, "gl1") == 0)
	{
		return VIDREF_GL1;
	}
	if (Q_stricmp(name, "gl3") == 0)
	{
		return VIDREF_GL3;
	}
	if (Q_stricmp(name, "gl4") == 0)
	{
		return VIDREF_GL4;
	}
	if (Q_stricmp(name, "vk") == 0)
	{
		return VIDREF_VK;
	}
	if (Q_stricmp(name, "soft") == 0)
	{
		return VIDREF_SOFT;
	}
	if (Q_stricmp(name, "metal") == 0)
	{
		return VIDREF_METAL;
	}
	return VIDREF_UNKNOWN;
}

vidrule_t
VidRules_Screen(vidplat_t plat, vidscr_t item)
{
	static const char *const ios_reason[VIDSCR_COUNT] = {
		"the screen's own size",
		"always the whole screen",
		"one screen",
		"always on",
		"metal is linked in"
	};

	if (((int)item < 0) || ((int)item >= VIDSCR_COUNT))
	{
		return (vidrule_t){ 0, "unknown item" };
	}

	switch (plat)
	{
		case VIDPLAT_DESKTOP:
			return (vidrule_t){ 1, NULL };
		case VIDPLAT_IOS:
			return (vidrule_t){ 0, ios_reason[item] };
		default:
			return (vidrule_t){ 0, "unknown platform" };
	}
}

int
VidRules_ScalerModeAvailable(int modemask, int mode)
{
	switch (mode)
	{
		case 0:
			return 1;
		case 1:
			return (modemask & VIDMFX_SPATIAL) != 0;
		case 2:
			return (modemask & VIDMFX_TEMPORAL) != 0;
		case 3:
			return (modemask & VIDMFX_DENOISED) != 0;
		default:
			return 0;
	}
}

int
VidRules_ScalerStep(int modemask, int prev, int cur)
{
	int dir;

	if (prev < 0 || prev > 3)
	{
		prev = 0;
	}
	if (VidRules_ScalerModeAvailable(modemask, cur))
	{
		return cur;
	}

	dir = (cur > prev) ? 1 : -1;
	for (int i = cur + dir; i >= 0 && i <= 3; i += dir)
	{
		if (VidRules_ScalerModeAvailable(modemask, i))
		{
			return i;
		}
	}

	return prev;
}

static int
HasAnyScaler(int modemask)
{
	return (modemask & (VIDMFX_SPATIAL | VIDMFX_TEMPORAL | VIDMFX_DENOISED)) != 0;
}

/* Metal reads gl1's cvars by gl1's names: gl1's settings apply to it */
static vidref_t
SettingsOf(vidref_t r)
{
	return (r == VIDREF_METAL) ? VIDREF_GL1 : r;
}

const char *
VidRules_Cvar(viditem_t item, const char *renderer)
{
	const vidref_t r = SettingsOf(VidRules_Renderer(renderer));
	const int metal = VidRules_Renderer(renderer) == VIDREF_METAL;

	switch (item)
	{
		case VIDITEM_INTENSITY:
			return (r == VIDREF_GL1) ? "gl1_intensity" :
				(r == VIDREF_GL3) ? "gl3_intensity" :
				(r == VIDREF_GL4) ? "gl4_intensity" :
				(r == VIDREF_VK) ? "vk_intensity" : NULL;
		case VIDITEM_OVERBRIGHT:
			return (r == VIDREF_GL1) ? "gl1_overbrightbits" :
				(r == VIDREF_GL3) ? "gl3_overbrightbits" :
				(r == VIDREF_GL4) ? "gl4_overbrightbits" :
				(r == VIDREF_VK) ? "vk_overbrightbits" : NULL;
		case VIDITEM_MINLIGHT:
			return (r == VIDREF_GL1) ? "gl1_minlight" : NULL;
		case VIDITEM_COLORLIGHT:
			return (r == VIDREF_GL3) ? "gl3_colorlight" :
				(r == VIDREF_GL4) ? "gl4_colorlight" : NULL;
		case VIDITEM_BLOOM:
			return (r == VIDREF_GL4) ? "r_bloom" : NULL;
		case VIDITEM_DYNLIGHT:
			return (r == VIDREF_VK) ? "vk_dynamic" : NULL;
		case VIDITEM_MSAA:
			return metal ? NULL : "r_msaa_samples";
		case VIDITEM_FILTER:
			return (r == VIDREF_GL1 || r == VIDREF_GL3) ? "gl_texturemode" :
				(r == VIDREF_SOFT) ? "sw_texture_filtering" : NULL;
		case VIDITEM_MFX_SCALER:
			return metal ? "r_metalfx" : NULL;
		case VIDITEM_MFX_SCALE:
			return metal ? "r_metalfx_scale" : NULL;
		case VIDITEM_MFX_INTERP:
			return metal ? "r_metalfx_interp" : NULL;
		case VIDITEM_DENOISER:
			return metal ? "r_rt_denoiser" : NULL;
		case VIDITEM_RTW_HITS:
			return metal ? "r_rt_water_hits" : NULL;
		case VIDITEM_AO_SCALE:
			return metal ? "r_rt_ao_scale" : NULL;
		case VIDITEM_AO_RAYS:
			return metal ? "r_rt_ao_rays" : NULL;
		case VIDITEM_COLORSPACE:
			return metal ? "metal_colorspace" : NULL;
		default:
			return NULL;
	}
}

vidrule_t
VidRules_Item(viditem_t item, const char *renderer, int modemask, int scaler)
{
	const vidref_t ref = VidRules_Renderer(renderer);
	const vidref_t r = SettingsOf(ref);
	vidrule_t ok = { 1, NULL };

	switch (item)
	{
		case VIDITEM_INTENSITY:
		case VIDITEM_OVERBRIGHT:
			if (r == VIDREF_GL1 || r == VIDREF_GL3 || r == VIDREF_GL4 || r == VIDREF_VK)
			{
				return ok;
			}
			return (vidrule_t){ 0, "not used by this renderer" };

		case VIDITEM_MINLIGHT:
			if (r == VIDREF_GL1)
			{
				return ok;
			}
			return (vidrule_t){ 0, "OpenGL 1.4 and Metal only" };

		case VIDITEM_COLORLIGHT:
			if (r == VIDREF_GL3 || r == VIDREF_GL4)
			{
				return ok;
			}
			return (vidrule_t){ 0, "OpenGL 3.2 and 4.6 only" };

		case VIDITEM_BLOOM:
			if (r == VIDREF_GL4)
			{
				return ok;
			}
			return (vidrule_t){ 0, "OpenGL 4.6 only" };

		case VIDITEM_DYNLIGHT:
			if (r == VIDREF_VK)
			{
				return ok;
			}
			return (vidrule_t){ 0, "Vulkan only" };

		case VIDITEM_MSAA:
			if (ref == VIDREF_METAL)
			{
				return (vidrule_t){ 0, "no multisampling in the Metal renderer" };
			}
			return ok;

		case VIDITEM_FILTER:
			if (r == VIDREF_GL1 || r == VIDREF_GL3 || r == VIDREF_SOFT)
			{
				return ok;
			}
			return (vidrule_t){ 0, "not available in this renderer" };

		case VIDITEM_MFX_SCALER:
			if (ref != VIDREF_METAL)
			{
				return (vidrule_t){ 0, "Metal renderer only" };
			}
			if (!HasAnyScaler(modemask))
			{
				return (vidrule_t){ 0, "not supported on this GPU" };
			}
			return (vidrule_t){ 1, "applies on apply" };

		case VIDITEM_MFX_SCALE:
			if (ref != VIDREF_METAL)
			{
				return (vidrule_t){ 0, "Metal renderer only" };
			}
			if (!HasAnyScaler(modemask))
			{
				return (vidrule_t){ 0, "not supported on this GPU" };
			}
			if (scaler == 0)
			{
				return (vidrule_t){ 0, "needs a scaler" };
			}
			return (vidrule_t){ 1, "applies on apply" };

		case VIDITEM_MFX_INTERP:
			if (ref != VIDREF_METAL)
			{
				return (vidrule_t){ 0, "Metal renderer only" };
			}
			/* The renderer forces r_metalfx_interp back to 0 until frame
			   generation is built, so bit 16 does not make it available. */
			return (vidrule_t){ 0, "not built yet; adds input delay" };

		case VIDITEM_DENOISER:
			if (ref != VIDREF_METAL)
			{
				return (vidrule_t){ 0, "Metal renderer only" };
			}
			return (vidrule_t){ 0, "needs ray tracing" };

		case VIDITEM_RTW_HITS:
			if (ref != VIDREF_METAL)
			{
				return (vidrule_t){ 0, "Metal renderer only" };
			}
			/* scaler: whether r_rt_water is on */
			if (!scaler)
			{
				return (vidrule_t){ 0, "needs r_rt_water 1" };
			}
			return (vidrule_t){ 1, "world space costs more GPU time" };

		case VIDITEM_AO_SCALE:
		case VIDITEM_AO_RAYS:
			if (ref != VIDREF_METAL)
			{
				return (vidrule_t){ 0, "Metal renderer only" };
			}
			/* scaler: whether r_rt_ao is on */
			if (!scaler)
			{
				return (vidrule_t){ 0, "needs r_rt_ao 1" };
			}
			return (vidrule_t){ 1, "applies on apply" };

		case VIDITEM_COLORSPACE:
			if (ref != VIDREF_METAL)
			{
				return (vidrule_t){ 0, "Metal renderer only" };
			}
			return (vidrule_t){ 1, "applies on apply" };

		default:
			return (vidrule_t){ 0, "unknown item" };
	}
}

/* auto, then 1/3, 1/2, 2/3, 3/4 and the full size of the view */
static const float scale_values[] = { 0.0f, 1.0f / 3.0f, 0.5f, 2.0f / 3.0f, 0.75f, 1.0f };

int
VidRules_NumScales(void)
{
	return ARRSIZE(scale_values);
}

float
VidRules_ScaleValue(int index)
{
	if (index < 0 || index >= ARRSIZE(scale_values))
	{
		return 0.0f;
	}
	return scale_values[index];
}

int
VidRules_ScaleIndex(float value)
{
	if (!(value == value) || value < 0.0f)
	{
		return 0;
	}

	for (int i = 0; i < ARRSIZE(scale_values); i++)
	{
		if (fabsf(value - scale_values[i]) < 0.005f)
		{
			return i;
		}
	}

	return ARRSIZE(scale_values);
}

int
VidRules_ModeMask(float value)
{
	if (!(value >= 0.0f && value <= (float)VIDMFX_ALL))
	{
		return 0;
	}

	return (int)value;
}

int
VidRules_ScalerIndex(float value)
{
	if (!(value >= 1.0f))
	{
		return 0;
	}

	return (value >= 3.0f) ? 3 : (int)value;
}

int
VidRules_DenoiserIndex(float value)
{
	if (!(value >= 1.0f))
	{
		return 0;
	}

	return (value >= 2.0f) ? 2 : 1;
}

int
VidRules_WaterOn(float value)
{
	/* as rm_rtw_on: a value that is not finite is off */
	return isfinite(value) && (value != 0.0f);
}

int
VidRules_WaterHitsIndex(float value)
{
	/* as rm_rtw_hits */
	return isfinite(value) && (value >= 1.0f);
}

/* full, 1/2 and 1/3 of the view, the divisors 1 to 3 of rm_rtao_div */
static const float ao_scale_values[] = { 1.0f, 0.5f, 1.0f / 3.0f };

int
VidRules_NumAOScales(void)
{
	return ARRSIZE(ao_scale_values);
}

float
VidRules_AOScaleValue(int index)
{
	if (index < 0 || index >= ARRSIZE(ao_scale_values))
	{
		return 0.5f;
	}
	return ao_scale_values[index];
}

int
VidRules_AOScaleIndex(float value)
{
	float d;

	/* as rm_rtao_div: a value that is not finite or not above 0 is the default */
	if (!isfinite(value) || !(value > 0.0f))
	{
		return 1;
	}

	/* the nearest divisor to 1 / value; 1.5 goes up to 2 and 2.5 down to 2 */
	d = 1.0f / value;

	if (d < 1.5f)
	{
		return 0;
	}

	return (d <= 2.5f) ? 1 : 2;
}

int
VidRules_AORaysIndex(float value)
{
	/* as rm_rtao_rays: a value that is not finite is the default, 2 */
	if (!isfinite(value))
	{
		return 1;
	}

	return (value < 1.5f) ? 0 : 1;
}

/*
 * The ray-tracing screen's controls (TASK194). The defaults are ref_metal's
 * (the scenes' Cvar_Get); the sliders cover the ranges doc/040_cvarlist.md
 * gives, r_rt_ao_range only its useful part (the cvar takes 1 to 8192).
 * r_rt_ao_scale takes three sizes the renderer tells apart (full, 1/2,
 * 1/3), so it is a choice row (VidRules_AOScaleIndex and Value map it),
 * not a slider; its min, max and step are only the range of the cvar.
 */
enum { RT_SHADOWS, RT_SUN, RT_AO, RT_WATER, RT_GUNSHADOW };

static const char *const rt_ao_scale_names[] = { "full", "1/2", "1/3", NULL };

/* the cvar first in each row: tests/check-cvar-docs.sh reads it as the row's name */
static const vidrt_control_t rt_controls[] = {
	{ "r_rt_shadows", "shadows", "0", CVAR_ARCHIVE, -1, 0, 0, 0, NULL },
	{ "r_rt_sun", "sun", "0", CVAR_ARCHIVE, -1, 0, 0, 0, NULL },
	{ "r_rt_ao", "ambient occlusion", "0", CVAR_ARCHIVE, -1, 0, 0, 0, NULL },
	{ "r_rt_water", "water reflections", "0", CVAR_ARCHIVE, -1, 0, 0, 0, NULL },
	{ "r_rt_gunshadow", "gun shadow", "0", CVAR_ARCHIVE, -1, 0, 0, 0, NULL },
	{ "r_rt_sunfloor", "sun floor", "0.5", CVAR_ARCHIVE, RT_SUN, 0.0f, 1.0f, 0.05f, "%.2f" },
	{ "r_rt_sunscale", "sun scale", "0.35", CVAR_ARCHIVE, RT_SUN, 0.0f, 4.0f, 0.05f, "%.2f" },
	{ "r_rt_ao_range", "AO range", "128", CVAR_ARCHIVE, RT_AO, 16.0f, 1024.0f, 16.0f, "%.0f" },
	{ "r_rt_ao_min", "AO minimum", "0.4", CVAR_ARCHIVE, RT_AO, 0.0f, 1.0f, 0.05f, "%.2f" },
	{ "r_rt_ao_scale", "AO scale", "0.5", CVAR_ARCHIVE, RT_AO, 0.25f, 1.0f, 0.25f, "%.2f", rt_ao_scale_names },
	{ "r_rt_water_reflect", "water reflect", "0.5", CVAR_ARCHIVE, RT_WATER, 0.0f, 1.0f, 0.05f, "%.2f" },
	{ "r_rt_water_normal", "water ripple", "0.08", CVAR_ARCHIVE, RT_WATER, 0.0f, 1.0f, 0.02f, "%.2f" },
};

int
VidRules_RTNum(void)
{
	return ARRSIZE(rt_controls);
}

const vidrt_control_t *
VidRules_RTControl(int i)
{
	if (i < 0 || i >= ARRSIZE(rt_controls))
	{
		return NULL;
	}
	return &rt_controls[i];
}

vidrule_t
VidRules_RTEntry(const char *renderer)
{
	if (VidRules_Renderer(renderer) != VIDREF_METAL)
	{
		return (vidrule_t){ 0, "needs the Metal renderer" };
	}
	return (vidrule_t){ 1, "shadows, sun, AO, water; applies live" };
}

vidrule_t
VidRules_RTItem(int i, const char *renderer, int available, int effect_on)
{
	/* the switches' labels, for "turn on <effect> first" */
	static const char *const turn_on[] = {
		"turn on shadows first",
		"turn on sun first",
		"turn on ambient occlusion first",
		"turn on water reflections first",
		"turn on gun shadow first"
	};
	const vidrt_control_t *c = VidRules_RTControl(i);

	if (!c && i != -1)
	{
		return (vidrule_t){ 0, "unknown item" };
	}
	if (VidRules_Renderer(renderer) != VIDREF_METAL)
	{
		return (vidrule_t){ 0, "needs the Metal renderer" };
	}
	if (!available)
	{
		return (vidrule_t){ 0, "ray tracing is not available on this device" };
	}
	if (!c)
	{
		return (vidrule_t){ 1, "every setting here back to its default" };
	}
	if (c->effect >= 0 && !effect_on)
	{
		return (vidrule_t){ 0, (c->effect < ARRSIZE(turn_on)) ? turn_on[c->effect] : "turn on its effect first" };
	}
	return (vidrule_t){ 1, "applies from the next frame" };
}

/* TASK183: the names in the order of r_presentation's values, never renumbered */
static const char *const pres_names[VIDPRES_COUNT + 1] = { "Stock", "Enhanced", "Retro", "Ray traced", NULL };

static const vidrt_control_t pres_control = {
	"r_presentation", "presentation", "0", CVAR_ARCHIVE, -1, 0, 0, 0, NULL, pres_names
};

const vidrt_control_t *
VidRules_Presentation(void)
{
	return &pres_control;
}

int
VidRules_PresentationIndex(float value)
{
	if (!isfinite(value) || value < 0.0f || value >= (float)VIDPRES_COUNT)
	{
		return VIDPRES_STOCK;
	}
	return (int)value;
}

vidrule_t
VidRules_PresentationMode(int mode, const char *renderer, int available)
{
	if (mode < -1 || mode >= VIDPRES_COUNT)
	{
		return (vidrule_t){ 0, "unknown mode" };
	}
	if (VidRules_Renderer(renderer) != VIDREF_METAL)
	{
		return (vidrule_t){ 0, "ref_metal only" };
	}
	if (mode == -1)
	{
		return (vidrule_t){ 1, "the look; applies at apply (vid_restart)" };
	}
	if (mode == VIDPRES_RETRO)
	{
		return (vidrule_t){ 0, "awaiting review" };
	}
	if ((mode == VIDPRES_ENHANCED || mode == VIDPRES_RTPRIMARY) && !available)
	{
		return (vidrule_t){ 0, "needs ray tracing" };
	}
	if (mode == VIDPRES_RTPRIMARY)
	{
		return (vidrule_t){ 0, "awaiting review" };
	}
	if (mode == VIDPRES_ENHANCED)
	{
		return (vidrule_t){ 1, "stock lighting with the ray-traced effects on" };
	}
	return (vidrule_t){ 1, "the original look; your own settings" };
}

const char *
VidRules_RTStatus(const char *renderer, int available, float presentation)
{
	if (VidRules_PresentationIndex(presentation) == VIDPRES_ENHANCED &&
		VidRules_PresentationMode(VIDPRES_ENHANCED, renderer, available).enabled)
	{
		return "Enhanced overrides the switches; your own values shown";
	}
	return "Esc: back; each change shows on the next frame";
}
