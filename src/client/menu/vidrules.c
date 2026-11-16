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
		case VIDITEM_AO_SCALE:
			return metal ? "r_rt_ao_scale" : NULL;
		case VIDITEM_AO_RAYS:
			return metal ? "r_rt_ao_rays" : NULL;
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
