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
 * What the video menu shows for each renderer (TASK112,
 * src/client/menu/vidrules.c): which items are greyed and why, the cvar
 * each sets, the MetalFX mode mask and the internal scale steps.
 *
 * =======================================================================
 */

#include <string.h>

#include "test.h"
#include "../../src/client/menu/header/vidrules.h"

#define NREFS 7
static const char *refs[NREFS] = { "metal", "gl1", "gl3", "gl4", "vk", "soft", "mylib" };

static int
on(viditem_t item, const char *ref, int mask, int scaler)
{
	return VidRules_Item(item, ref, mask, scaler).enabled;
}

static const char *
why(viditem_t item, const char *ref, int mask, int scaler)
{
	return VidRules_Item(item, ref, mask, scaler).text;
}

TEST(vidrules_renderer_names)
{
	T_EQ_INT(VidRules_Renderer("metal"), VIDREF_METAL);
	T_EQ_INT(VidRules_Renderer("gl1"), VIDREF_GL1);
	T_EQ_INT(VidRules_Renderer("gl3"), VIDREF_GL3);
	T_EQ_INT(VidRules_Renderer("gl4"), VIDREF_GL4);
	T_EQ_INT(VidRules_Renderer("vk"), VIDREF_VK);
	T_EQ_INT(VidRules_Renderer("soft"), VIDREF_SOFT);
	T_EQ_INT(VidRules_Renderer("SOFT"), VIDREF_SOFT);
	T_EQ_INT(VidRules_Renderer("softx"), VIDREF_UNKNOWN);
	T_EQ_INT(VidRules_Renderer("mylib"), VIDREF_UNKNOWN);
	T_EQ_INT(VidRules_Renderer(""), VIDREF_UNKNOWN);
	T_EQ_INT(VidRules_Renderer(NULL), VIDREF_UNKNOWN);
}

/* every greyed item has a one-line reason that fits the status bar */
TEST(vidrules_every_greyed_item_has_a_reason)
{
	static const int masks[] = { 0, 1, 3, 7, 15, 31 };

	for (int r = 0; r < NREFS; r++)
	{
		for (int m = 0; m < 6; m++)
		{
			for (int s = 0; s < 4; s++)
			{
				for (int i = 0; i < VIDITEM_COUNT; i++)
				{
					vidrule_t v = VidRules_Item((viditem_t)i, refs[r], masks[m], s);

					if (!v.enabled)
					{
						T_REQUIRE(v.text != NULL);
						T_CHECK(strlen(v.text) > 0);
						T_CHECK(strlen(v.text) <= 40);
						T_CHECK(strchr(v.text, '\n') == NULL);
					}
				}
			}
		}
	}
}

TEST(vidrules_non_metal_renderers_grey_the_metal_items)
{
	for (int r = 1; r < NREFS; r++)
	{
		for (int m = 1; m <= 31; m += 2)
		{
			T_EQ_INT(on(VIDITEM_MFX_SCALER, refs[r], m, 1), 0);
			T_EQ_INT(on(VIDITEM_MFX_SCALE, refs[r], m, 1), 0);
			T_EQ_INT(on(VIDITEM_MFX_INTERP, refs[r], m, 1), 0);
			T_EQ_INT(on(VIDITEM_DENOISER, refs[r], m, 1), 0);
			T_EQ_STR(why(VIDITEM_MFX_SCALER, refs[r], m, 1), "Metal renderer only");
			T_EQ_STR(why(VIDITEM_MFX_SCALE, refs[r], m, 1), "Metal renderer only");
			T_EQ_STR(why(VIDITEM_MFX_INTERP, refs[r], m, 1), "Metal renderer only");
			T_EQ_STR(why(VIDITEM_DENOISER, refs[r], m, 1), "Metal renderer only");
		}
	}
}

TEST(vidrules_metal_scaler_follows_the_mask)
{
	/* 1: off only; 3: spatial; 7: and temporal; 15: and denoised; 31: and the interpolator */
	T_EQ_INT(on(VIDITEM_MFX_SCALER, "metal", 1, 0), 0);
	T_EQ_STR(why(VIDITEM_MFX_SCALER, "metal", 1, 0), "not supported on this GPU");
	T_EQ_INT(on(VIDITEM_MFX_SCALER, "metal", 0, 0), 0);
	T_EQ_INT(on(VIDITEM_MFX_SCALER, "metal", 3, 0), 1);
	T_EQ_INT(on(VIDITEM_MFX_SCALER, "metal", 7, 0), 1);
	T_EQ_INT(on(VIDITEM_MFX_SCALER, "metal", 15, 0), 1);
	T_EQ_INT(on(VIDITEM_MFX_SCALER, "metal", 31, 0), 1);
	/* each scaler bit alone is a scaler; the interpolator is not */
	T_EQ_INT(on(VIDITEM_MFX_SCALER, "metal", 2, 0), 1);
	T_EQ_INT(on(VIDITEM_MFX_SCALER, "metal", 4, 0), 1);
	T_EQ_INT(on(VIDITEM_MFX_SCALER, "metal", 8, 0), 1);
	T_EQ_INT(on(VIDITEM_MFX_SCALER, "metal", 16, 0), 0);
	T_EQ_INT(on(VIDITEM_MFX_SCALER, "metal", 17, 0), 0);
	/* a value outside the mask is cut to it */
	T_EQ_INT(on(VIDITEM_MFX_SCALER, "metal", 32, 0), 0);
	T_EQ_INT(on(VIDITEM_MFX_SCALER, "metal", 34, 0), 1);
	T_EQ_INT(on(VIDITEM_MFX_SCALER, "metal", -1, 0), 1);
	T_EQ_INT(on(VIDITEM_MFX_SCALER, "metal", -2147483647 - 1, 0), 0);
}

TEST(vidrules_scaler_modes_available)
{
	static const int masks[] = { 1, 3, 7, 15, 31 };
	static const int expect[5][4] = {
		{ 1, 0, 0, 0 },
		{ 1, 1, 0, 0 },
		{ 1, 1, 1, 0 },
		{ 1, 1, 1, 1 },
		{ 1, 1, 1, 1 }
	};

	for (int m = 0; m < 5; m++)
	{
		for (int mode = 0; mode < 4; mode++)
		{
			T_EQ_INT(VidRules_ScalerModeAvailable(masks[m], mode), expect[m][mode]);
		}
	}

	/* a gap in the mask is a gap in the modes; off is always there; others never */
	T_EQ_INT(VidRules_ScalerModeAvailable(0, 0), 1);
	T_EQ_INT(VidRules_ScalerModeAvailable(8, 1), 0);
	T_EQ_INT(VidRules_ScalerModeAvailable(8, 2), 0);
	T_EQ_INT(VidRules_ScalerModeAvailable(8, 3), 1);
	T_EQ_INT(VidRules_ScalerModeAvailable(6, 1), 1);
	T_EQ_INT(VidRules_ScalerModeAvailable(6, 2), 1);
	T_EQ_INT(VidRules_ScalerModeAvailable(6, 3), 0);
	T_EQ_INT(VidRules_ScalerModeAvailable(31, 4), 0);
	T_EQ_INT(VidRules_ScalerModeAvailable(31, -1), 0);
}

TEST(vidrules_metal_scale_needs_a_scaler)
{
	T_EQ_INT(on(VIDITEM_MFX_SCALE, "metal", 7, 0), 0);
	T_EQ_STR(why(VIDITEM_MFX_SCALE, "metal", 7, 0), "needs a scaler");
	T_EQ_INT(on(VIDITEM_MFX_SCALE, "metal", 7, 1), 1);
	T_EQ_INT(on(VIDITEM_MFX_SCALE, "metal", 7, 3), 1);
	T_EQ_INT(on(VIDITEM_MFX_SCALE, "metal", 1, 1), 0);
	T_EQ_STR(why(VIDITEM_MFX_SCALE, "metal", 1, 1), "not supported on this GPU");
	T_EQ_STR(why(VIDITEM_MFX_SCALE, "metal", 1, 0), "not supported on this GPU");
}

TEST(vidrules_metal_frame_generation_follows_bit_16)
{
	T_EQ_INT(on(VIDITEM_MFX_INTERP, "metal", 1, 0), 0);
	T_EQ_INT(on(VIDITEM_MFX_INTERP, "metal", 3, 0), 0);
	T_EQ_INT(on(VIDITEM_MFX_INTERP, "metal", 7, 0), 0);
	T_EQ_INT(on(VIDITEM_MFX_INTERP, "metal", 15, 0), 0);
	T_EQ_INT(on(VIDITEM_MFX_INTERP, "metal", 31, 0), 1);
	T_EQ_INT(on(VIDITEM_MFX_INTERP, "metal", 16, 0), 1);
	T_CHECK(strstr(why(VIDITEM_MFX_INTERP, "metal", 3, 0), "not built yet") != NULL);
	T_CHECK(strstr(why(VIDITEM_MFX_INTERP, "metal", 31, 0), "input delay") != NULL);
}

TEST(vidrules_denoiser_is_always_greyed_today)
{
	static const int masks[] = { 1, 3, 7, 15, 31 };

	for (int r = 0; r < NREFS; r++)
	{
		for (int m = 0; m < 5; m++)
		{
			for (int s = 0; s < 4; s++)
			{
				T_EQ_INT(on(VIDITEM_DENOISER, refs[r], masks[m], s), 0);
			}
		}
	}
	T_EQ_STR(why(VIDITEM_DENOISER, "metal", 31, 3), "needs ray tracing");
}

TEST(vidrules_renderer_specific_settings)
{
	/* gl1 and metal share gl1's settings */
	for (int r = 0; r < 2; r++)
	{
		T_EQ_INT(on(VIDITEM_INTENSITY, refs[r], 1, 0), 1);
		T_EQ_INT(on(VIDITEM_OVERBRIGHT, refs[r], 1, 0), 1);
		T_EQ_INT(on(VIDITEM_MINLIGHT, refs[r], 1, 0), 1);
		T_EQ_INT(on(VIDITEM_COLORLIGHT, refs[r], 1, 0), 0);
		T_EQ_INT(on(VIDITEM_BLOOM, refs[r], 1, 0), 0);
		T_EQ_INT(on(VIDITEM_DYNLIGHT, refs[r], 1, 0), 0);
		T_EQ_INT(on(VIDITEM_FILTER, refs[r], 1, 0), 1);
	}

	T_EQ_INT(on(VIDITEM_INTENSITY, "gl3", 1, 0), 1);
	T_EQ_INT(on(VIDITEM_MINLIGHT, "gl3", 1, 0), 0);
	T_EQ_INT(on(VIDITEM_COLORLIGHT, "gl3", 1, 0), 1);
	T_EQ_INT(on(VIDITEM_BLOOM, "gl3", 1, 0), 0);
	T_EQ_INT(on(VIDITEM_FILTER, "gl3", 1, 0), 1);

	T_EQ_INT(on(VIDITEM_COLORLIGHT, "gl4", 1, 0), 1);
	T_EQ_INT(on(VIDITEM_BLOOM, "gl4", 1, 0), 1);
	T_EQ_INT(on(VIDITEM_MINLIGHT, "gl4", 1, 0), 0);
	T_EQ_INT(on(VIDITEM_FILTER, "gl4", 1, 0), 0);

	T_EQ_INT(on(VIDITEM_DYNLIGHT, "vk", 1, 0), 1);
	T_EQ_INT(on(VIDITEM_COLORLIGHT, "vk", 1, 0), 0);
	T_EQ_INT(on(VIDITEM_INTENSITY, "vk", 1, 0), 1);
	T_EQ_INT(on(VIDITEM_OVERBRIGHT, "vk", 1, 0), 1);
	T_EQ_INT(on(VIDITEM_FILTER, "vk", 1, 0), 0);

	T_EQ_INT(on(VIDITEM_INTENSITY, "soft", 1, 0), 0);
	T_EQ_INT(on(VIDITEM_OVERBRIGHT, "soft", 1, 0), 0);
	T_EQ_INT(on(VIDITEM_FILTER, "soft", 1, 0), 1);
	T_EQ_INT(on(VIDITEM_MSAA, "soft", 1, 0), 1);

	T_EQ_INT(on(VIDITEM_INTENSITY, "mylib", 1, 0), 0);
	T_EQ_INT(on(VIDITEM_FILTER, "mylib", 1, 0), 0);
	T_EQ_INT(on(VIDITEM_MSAA, "mylib", 1, 0), 1);

	T_EQ_INT(on(VIDITEM_MSAA, "gl1", 1, 0), 1);
	T_EQ_INT(on(VIDITEM_MSAA, "metal", 31, 0), 0);
	T_EQ_STR(why(VIDITEM_MSAA, "metal", 31, 0), "no multisampling in the Metal renderer");
}

TEST(vidrules_unknown_item_is_greyed)
{
	T_EQ_INT(on(VIDITEM_COUNT, "metal", 31, 1), 0);
	T_EQ_INT(on((viditem_t)-1, "metal", 31, 1), 0);
	T_EQ_INT(on((viditem_t)100, "gl1", 31, 1), 0);
	T_CHECK(VidRules_Cvar(VIDITEM_COUNT, "metal") == NULL);
	T_CHECK(VidRules_Cvar((viditem_t)-1, "metal") == NULL);
}

TEST(vidrules_cvar_names)
{
	T_EQ_STR(VidRules_Cvar(VIDITEM_INTENSITY, "gl1"), "gl1_intensity");
	T_EQ_STR(VidRules_Cvar(VIDITEM_INTENSITY, "metal"), "gl1_intensity");
	T_EQ_STR(VidRules_Cvar(VIDITEM_INTENSITY, "gl3"), "gl3_intensity");
	T_EQ_STR(VidRules_Cvar(VIDITEM_INTENSITY, "gl4"), "gl4_intensity");
	T_EQ_STR(VidRules_Cvar(VIDITEM_INTENSITY, "vk"), "vk_intensity");
	T_CHECK(VidRules_Cvar(VIDITEM_INTENSITY, "soft") == NULL);
	T_CHECK(VidRules_Cvar(VIDITEM_INTENSITY, "mylib") == NULL);
	T_EQ_STR(VidRules_Cvar(VIDITEM_OVERBRIGHT, "gl1"), "gl1_overbrightbits");
	T_EQ_STR(VidRules_Cvar(VIDITEM_OVERBRIGHT, "metal"), "gl1_overbrightbits");
	T_EQ_STR(VidRules_Cvar(VIDITEM_OVERBRIGHT, "gl3"), "gl3_overbrightbits");
	T_EQ_STR(VidRules_Cvar(VIDITEM_OVERBRIGHT, "gl4"), "gl4_overbrightbits");
	T_EQ_STR(VidRules_Cvar(VIDITEM_OVERBRIGHT, "vk"), "vk_overbrightbits");
	T_CHECK(VidRules_Cvar(VIDITEM_OVERBRIGHT, "soft") == NULL);
	T_EQ_STR(VidRules_Cvar(VIDITEM_MINLIGHT, "metal"), "gl1_minlight");
	T_EQ_STR(VidRules_Cvar(VIDITEM_MINLIGHT, "gl1"), "gl1_minlight");
	T_CHECK(VidRules_Cvar(VIDITEM_MINLIGHT, "gl3") == NULL);
	T_EQ_STR(VidRules_Cvar(VIDITEM_COLORLIGHT, "gl3"), "gl3_colorlight");
	T_EQ_STR(VidRules_Cvar(VIDITEM_COLORLIGHT, "gl4"), "gl4_colorlight");
	T_CHECK(VidRules_Cvar(VIDITEM_COLORLIGHT, "gl1") == NULL);
	T_EQ_STR(VidRules_Cvar(VIDITEM_BLOOM, "gl4"), "r_bloom");
	T_CHECK(VidRules_Cvar(VIDITEM_BLOOM, "gl3") == NULL);
	T_EQ_STR(VidRules_Cvar(VIDITEM_DYNLIGHT, "vk"), "vk_dynamic");
	T_CHECK(VidRules_Cvar(VIDITEM_DYNLIGHT, "gl1") == NULL);
	T_EQ_STR(VidRules_Cvar(VIDITEM_MSAA, "gl1"), "r_msaa_samples");
	T_EQ_STR(VidRules_Cvar(VIDITEM_MSAA, "soft"), "r_msaa_samples");
	T_CHECK(VidRules_Cvar(VIDITEM_MSAA, "metal") == NULL);
	T_EQ_STR(VidRules_Cvar(VIDITEM_FILTER, "gl1"), "gl_texturemode");
	T_EQ_STR(VidRules_Cvar(VIDITEM_FILTER, "metal"), "gl_texturemode");
	T_EQ_STR(VidRules_Cvar(VIDITEM_FILTER, "gl3"), "gl_texturemode");
	T_EQ_STR(VidRules_Cvar(VIDITEM_FILTER, "soft"), "sw_texture_filtering");
	T_CHECK(VidRules_Cvar(VIDITEM_FILTER, "vk") == NULL);
	T_EQ_STR(VidRules_Cvar(VIDITEM_MFX_SCALER, "metal"), "r_metalfx");
	T_EQ_STR(VidRules_Cvar(VIDITEM_MFX_SCALE, "metal"), "r_metalfx_scale");
	T_EQ_STR(VidRules_Cvar(VIDITEM_MFX_INTERP, "metal"), "r_metalfx_interp");
	T_EQ_STR(VidRules_Cvar(VIDITEM_DENOISER, "metal"), "r_rt_denoiser");
	T_CHECK(VidRules_Cvar(VIDITEM_MFX_SCALER, "gl1") == NULL);
	T_CHECK(VidRules_Cvar(VIDITEM_MFX_SCALE, "soft") == NULL);
	T_CHECK(VidRules_Cvar(VIDITEM_MFX_INTERP, "mylib") == NULL);
	T_CHECK(VidRules_Cvar(VIDITEM_DENOISER, NULL) == NULL);
}

/* a renderer item with a cvar under a renderer is enabled there, one without is greyed */
TEST(vidrules_cvar_and_enabled_agree_for_renderer_items)
{
	for (int r = 0; r < NREFS; r++)
	{
		for (int i = VIDITEM_INTENSITY; i <= VIDITEM_DYNLIGHT; i++)
		{
			T_EQ_INT(on((viditem_t)i, refs[r], 31, 1),
				VidRules_Cvar((viditem_t)i, refs[r]) != NULL);
		}
		T_EQ_INT(on(VIDITEM_FILTER, refs[r], 31, 1),
			VidRules_Cvar(VIDITEM_FILTER, refs[r]) != NULL);
	}
}

TEST(vidrules_scale_steps)
{
	T_EQ_INT(VidRules_NumScales(), 6);
	T_EQ_FLT(VidRules_ScaleValue(0), 0.0f);
	T_EQ_FLT(VidRules_ScaleValue(1), 1.0f / 3.0f);
	T_EQ_FLT(VidRules_ScaleValue(2), 0.5f);
	T_EQ_FLT(VidRules_ScaleValue(3), 2.0f / 3.0f);
	T_EQ_FLT(VidRules_ScaleValue(4), 0.75f);
	T_EQ_FLT(VidRules_ScaleValue(5), 1.0f);
	T_EQ_FLT(VidRules_ScaleValue(6), 0.0f);
	T_EQ_FLT(VidRules_ScaleValue(-1), 0.0f);

	for (int i = 0; i < 6; i++)
	{
		T_EQ_INT(VidRules_ScaleIndex(VidRules_ScaleValue(i)), i);
	}
	T_EQ_INT(VidRules_ScaleIndex(0.5f), 2);
	T_EQ_INT(VidRules_ScaleIndex(0.504f), 2);
	T_EQ_INT(VidRules_ScaleIndex(0.51f), 6);
	T_EQ_INT(VidRules_ScaleIndex(0.4f), 6);
	T_EQ_INT(VidRules_ScaleIndex(0.33f), 1);
	T_EQ_INT(VidRules_ScaleIndex(2.0f), 6);
	T_EQ_INT(VidRules_ScaleIndex(-0.5f), 0);
	T_EQ_INT(VidRules_ScaleIndex(0.0f / 0.0f), 0);
}
