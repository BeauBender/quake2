/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 *
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
 * This is the refresher dependend video menu. If you add a new
 * refresher this menu must be altered.
 *
 * =======================================================================
 */

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include "../../client/header/client.h"
#include "../../client/menu/header/qmenu.h"
#include "header/qmenu.h"
#include "header/vidrules.h"

extern void M_ForceMenuOff(void);
extern const char *Default_MenuKey(menuframework_s *m, int key);
extern void Default_MenuDraw(menuframework_s *m);

static cvar_t *r_mode;
static cvar_t *vid_displayindex;
static cvar_t *r_hudscale;
static cvar_t *r_consolescale;
static cvar_t *r_menuscale;
static cvar_t *crosshair_scale;
extern cvar_t *scr_viewsize;
extern cvar_t *vid_fullscreen;
extern cvar_t *vid_renderer;
static cvar_t *r_vsync;
static cvar_t *gl_anisotropic;
static cvar_t *gl_msaa_samples;

static menuframework_s s_opengl_menu;
static menuframework_s s_ropts_menu;
static menuaction_s s_ropts_action;
static menuaction_s s_ropts_apply_action;

static menulist_s s_renderer_list;
static menulist_s s_mode_list;
static menulist_s s_display_list;   /* in the menu on iOS only (VID_PLATFORM) */
static menulist_s s_uiscale_list;
static menuslider_s s_brightness_slider;
static menuslider_s s_fov_slider;
/* Every renderer-specific item is always in the menu, greyed with its
   reason when the renderer does not have it (vidrules.c). */
static menuslider_s s_intensity_slider;
static menuslider_s s_overbrightbits_slider;
static menuslider_s s_minlight_slider;
static menulist_s s_colorlight_list;
static menulist_s s_bloom_list;
static menulist_s s_dynlight_list;
static menulist_s s_mfx_scaler_list;
static menulist_s s_mfx_scale_list;
static menulist_s s_mfx_interp_list;
static menulist_s s_denoiser_list;
static menulist_s s_colorspace_list;   /* TASK121: metal_colorspace */
static menulist_s s_rtw_hits_list;   /* TASK158: r_rt_water_hits */
static menulist_s s_ao_scale_list;
static menulist_s s_ao_rays_list;
static menulist_s s_fs_box;
static menulist_s s_vsync_list;
static menulist_s s_af_list;
static menulist_s s_msaa_list;
static menulist_s s_filter_list;
static menuaction_s s_defaults_action;
static menuaction_s s_apply_action;

// --------

// gl1, gl3, gl4, vk, soft and, on macOS, metal
#define MAXRENDERERS 6

typedef enum
{
	ref_custom,
	ref_gl1,
	ref_gl3,
	ref_gl4,
	ref_vk,
	ref_soft
} renderer_type;

typedef struct
{
	const char *boxstr;
	const char *cvarstr;
} renderer;

static renderer rendererlist[MAXRENDERERS];
static int numrenderer;

static void
Renderer_FillRenderdef(void)
{
	static const renderer fulllist[] = {
		{"[OpenGL 1.4]", "gl1"},
		{"[OpenGL 3.2]", "gl3"},
		{"[OpenGL 4.6]", "gl4"},
		{"[Vulkan    ]", "vk"},
		{"[Software  ]", "soft"},
#ifdef __APPLE__
		// The native Metal renderer, macOS only.
		{"[Metal     ]", "metal"},
#endif
		{NULL, NULL}
	};
	int idx = 0;

	// Every entry but the terminator fits in rendererlist.
	_Static_assert(ARRLEN(fulllist) - 1 <= MAXRENDERERS, "MAXRENDERERS is too small");

	numrenderer = -1;

	while (fulllist[idx].cvarstr)
	{
		if (VID_HasRenderer(fulllist[idx].cvarstr))
		{
			numrenderer++;
			rendererlist[numrenderer].boxstr  = fulllist[idx].boxstr;
			rendererlist[numrenderer].cvarstr = fulllist[idx].cvarstr;
		}
		idx++;
	}

	// The custom renderer. Must be known to the menu,
	// but nothing more. The display string is hard
	// coded below, the cvar is unknown.
	numrenderer++;
}

static int
Renderer_GetRenderer(void)
{
	for (int i = 0; i < numrenderer; i++)
	{
		if (strcmp(vid_renderer->string, rendererlist[i].cvarstr) == 0)
		{
			return i;
		}
	}

	// Unknown renderer.
	return numrenderer;
}

// --------

#define NUMMODES 33

typedef struct
{
	int modenum;
	char *modestr;
} mode;

// Sorted by string.
static mode modes[] = {
	{ 0, "[320 240   ]" },
	{ 1, "[400 300   ]" },
	{ 2, "[512 384   ]" },
	{ 3, "[640 400   ]" },
	{ 4, "[640 480   ]" },
	{ 5, "[800 500   ]" },
	{ 6, "[800 600   ]" },
	{ 7, "[960 720   ]" },
	{ 8, "[1024 480  ]" },
	{ 9, "[1024 640  ]" },
	{ 10, "[1024 768  ]" },
	{ 11, "[1152 768  ]" },
	{ 12, "[1152 864  ]" },
	{ 13, "[1280 800  ]" },
	{ 14, "[1280 720  ]" },
	{ 15, "[1280 960  ]" },
	{ 16, "[1280 1024 ]" },
	{ 17, "[1366 768  ]" },
	{ 18, "[1440 900  ]" },
	{ 32, "[1600 900  ]" },
	{ 19, "[1600 1200 ]" },
	{ 20, "[1680 1050 ]" },
	{ 21, "[1920 1080 ]" },
	{ 22, "[1920 1200 ]" },
	{ 23, "[2048 1536 ]" },
	{ 24, "[2560 1080 ]" },
	{ 25, "[2560 1440 ]" },
	{ 26, "[2560 1600 ]" },
	{ 27, "[3440 1440 ]" },
	{ 28, "[3840 1600 ]" },
	{ 29, "[3840 2160 ]" },
	{ 30, "[4096 2160 ]" },
	{ 31, "[5120 2880 ]" }
};

static int
GetModePos(int modenum)
{
	for (int i = 0; i < ARRLEN(modes); i++)
	{
		if (modes[i].modenum == modenum)
		{
			return i;
		}
	}

	return 3;
}

// --------

static int
GetCustomValue(menulist_s *list)
{
	static menulist_s *last;
	static int i;

	if (list != last)
	{
		last = list;
		i = list->curvalue;
		do
		{
			i++;
		}
		while (list->itemnames[i]);
		i--;
	}

	return i;
}

static void
ResetDefaults(void *unused)
{
	VID_MenuInit();
}

#define CUSTOM_MODE_NAME "[Custom    ]"
#define AUTO_MODE_NAME   "[Auto      ]"

static renderer_type
CurrentRendererByCvar(void)
{
	if (Q_stricmp(vid_renderer->string, "gl3") == 0)
	{
		return ref_gl3;
	}
	if (Q_stricmp(vid_renderer->string, "gl1") == 0)
	{
		return ref_gl1;
	}
	if (Q_stricmp(vid_renderer->string, "soft") == 0)
	{
		return ref_soft;
	}
	if (Q_stricmp(vid_renderer->string, "gl4") == 0)
	{
		return ref_gl4;
	}
	if (Q_stricmp(vid_renderer->string, "vk") == 0)
	{
		return ref_vk;
	}
#ifdef __APPLE__
	// Metal reads gl1's cvars by gl1's names: gl1's settings apply.
	if (Q_stricmp(vid_renderer->string, "metal") == 0)
	{
		return ref_gl1;
	}
#endif
	return ref_custom;
}

// See filter_names in VID_MenuInit()
static const char *gl_filter_list[] = {
	"GL_NEAREST",	// pixelated
	"GL_LINEAR",	// linear
	"GL_LINEAR_MIPMAP_NEAREST",	// standard
	"GL_LINEAR_MIPMAP_LINEAR"	// trilinear
};

static const int gl_filter_list_len = ARRLEN(gl_filter_list);

static void
ApplyFilter(void* unused)
{
	switch (CurrentRendererByCvar())
	{
		case ref_gl3:
		case ref_gl1:
			if (s_filter_list.curvalue < gl_filter_list_len)
			{
				Cvar_Set("gl_texturemode", gl_filter_list[s_filter_list.curvalue]);
			}
			break;
		case ref_soft:
			if (s_filter_list.curvalue == 0)
			{
				Cvar_Set("sw_texture_filtering", "0");
			}
			else if (s_filter_list.curvalue == 1)
			{
				Cvar_Set("sw_texture_filtering", "1");
			}
			break;
		default:
			break;	// avoid compiler warning
	}
}

/*
 * The modes of MetalFX the GPU has, r_metalfx_modes. A cvar a user or a
 * server can write, read again each time the menu opens, and only
 * advisory: it greys items, the renderer decides what it runs. A value
 * that is not a mask counts as none.
 */
static int
MetalModeMask(void)
{
	return VidRules_ModeMask(Cvar_VariableValue("r_metalfx_modes"));
}

/*
 * The renderer the menu was built for: every item's greying, value and
 * cvar name belong to it. "apply" writes the renderer's own cvar first, so
 * vid_renderer no longer names it by then.
 */
static char s_built_renderer[32];

/* Greys an item with its reason, or enables it with its note. */
static void
SetRule(menucommon_s *item, viditem_t id, int scaler)
{
	const vidrule_t rule = VidRules_Item(id, s_built_renderer,
			MetalModeMask(), scaler);

	if (rule.enabled)
	{
		item->flags &= ~QMF_GRAYED;
	}
	else
	{
		item->flags |= QMF_GRAYED;
	}

	item->statusbar = rule.text;
}

/*
 * The screen's own items (TASK147): where the window is the whole screen
 * (iOS) they are greyed with their reason (vidrules.c), never hidden;
 * where it is not, the item is left as it is.
 */
#ifndef VID_PLATFORM   /* the menu test builds this file for the other one too */
#if defined(__APPLE__) && TARGET_OS_IPHONE
#define VID_PLATFORM VIDPLAT_IOS
#else
#define VID_PLATFORM VIDPLAT_DESKTOP
#endif
#endif

static void
SetScreenRule(menucommon_s *item, vidscr_t id, const char **names)
{
	const vidrule_t rule = VidRules_Screen(VID_PLATFORM, id);

	if (!rule.enabled)
	{
		menulist_s *list = (menulist_s *)item;

		item->flags |= QMF_GRAYED;
		item->statusbar = rule.text;

		/* a greyed list shows the one value it has */
		list->itemnames = names;
		list->curvalue = 0;
	}
}

static int s_mfx_scaler_prev;

/* The scale item's choices: the steps, and "custom" while the cvar holds
   a value that is not a step. */
static const char *s_scale_names[] = { "auto", "1/3", "1/2", "2/3", "3/4", "full", NULL };
static const char *s_scale_names_custom[] = { "auto", "1/3", "1/2", "2/3", "3/4", "full", "custom", NULL };
static const char *s_scale_na_names[] = { "n/a", NULL };

/*
 * The scale item follows its rule: greyed it shows "n/a", enabled it
 * shows its choices with the value read from r_metalfx_scale. Called when
 * the menu is built and whenever the scaler item moves.
 */
static void
ScaleItemUpdate(int scaler)
{
	SetRule(&s_mfx_scale_list.generic, VIDITEM_MFX_SCALE, scaler);

	if (s_mfx_scale_list.generic.flags & QMF_GRAYED)
	{
		s_mfx_scale_list.itemnames = s_scale_na_names;
		s_mfx_scale_list.curvalue = 0;
	}
	else if (s_mfx_scale_list.itemnames == s_scale_na_names)
	{
		const int idx = VidRules_ScaleIndex(Cvar_VariableValue("r_metalfx_scale"));

		s_mfx_scale_list.itemnames = (idx < VidRules_NumScales()) ? s_scale_names : s_scale_names_custom;
		s_mfx_scale_list.curvalue = idx;
	}
}

/* Stepping off "custom" removes it: it is shown, never chosen. */
static void
MetalScaleChanged(void *self)
{
	menulist_s *list = (menulist_s *)self;

	if (list->curvalue < VidRules_NumScales())
	{
		list->itemnames = s_scale_names;
	}
}

/*
 * The scaler list was moved one step: a mode the GPU lacks is skipped
 * (never selected), and the scale item follows whether a scaler is on.
 */
static void
MetalScalerChanged(void *self)
{
	menulist_s *list = (menulist_s *)self;

	list->curvalue = VidRules_ScalerStep(MetalModeMask(), s_mfx_scaler_prev, list->curvalue);
	s_mfx_scaler_prev = list->curvalue;
	ScaleItemUpdate(list->curvalue);
}

/* A yes/no item writes its cvar when it is not greyed. */
static void
ApplyList(menulist_s *list, viditem_t id)
{
	const char *name = VidRules_Cvar(id, s_built_renderer);

	if (!name || (list->generic.flags & QMF_GRAYED))
	{
		return;
	}

	if ((int)(Cvar_VariableValue(name) != 0) != list->curvalue)
	{
		Cvar_SetValue(name, list->curvalue);
	}
}

static void
ApplyChanges(void *unused)
{
	qboolean restart = false;

	/* Renderer */
	if (!(s_renderer_list.generic.flags & QMF_GRAYED) &&
		s_renderer_list.curvalue != Renderer_GetRenderer())
	{
		// The custom renderer (the last known renderer) cannot be
		// set, because the menu doesn't know it's cvar value. TODO:
		// Hack something that it cannot be selected.
		if (s_renderer_list.curvalue != numrenderer)
		{
			Cvar_Set("vid_renderer", (char *)rendererlist[s_renderer_list.curvalue].cvarstr);
			restart = true;
		}
	}

	/* auto mode; a greyed mode list (the screen's own size) is not a setting */
	if (s_mode_list.generic.flags & QMF_GRAYED)
	{
		/* nothing to write */
	}
	else if (strcmp(s_mode_list.itemnames[s_mode_list.curvalue], AUTO_MODE_NAME) == 0)
	{
		Cvar_SetValue("r_mode", -2);
		restart = true;
	}
	else if (strcmp(s_mode_list.itemnames[s_mode_list.curvalue], CUSTOM_MODE_NAME) == 0)
	{
		Cvar_SetValue("r_mode", -1);
		restart = true;
	}
	else
	{
		Cvar_SetValue("r_mode", modes[s_mode_list.curvalue].modenum);
		restart = true;
	}

	/* UI scaling */
	if (s_uiscale_list.curvalue == 0)
	{
		Cvar_SetValue("r_hudscale", -1);
	}
	else if (s_uiscale_list.curvalue < GetCustomValue(&s_uiscale_list))
	{
		Cvar_SetValue("r_hudscale", s_uiscale_list.curvalue);
	}

	if (s_uiscale_list.curvalue != GetCustomValue(&s_uiscale_list))
	{
		Cvar_SetValue("r_consolescale", r_hudscale->value);
		Cvar_SetValue("r_menuscale", r_hudscale->value);
		Cvar_SetValue("crosshair_scale", r_hudscale->value);
	}

	/* Restarts automatically; a greyed item is not a setting */
	if (!(s_fs_box.generic.flags & QMF_GRAYED))
	{
		if (vid_fullscreen->value != s_fs_box.curvalue)
		{
			Cvar_SetValue("vid_fullscreen", s_fs_box.curvalue);
			restart = true;
		}

		if (s_fs_box.curvalue == 2)
		{
			Cvar_SetValue("r_mode", -2.0f);
		}
	}

	/* vertical sync */
	if (!(s_vsync_list.generic.flags & QMF_GRAYED) &&
		r_vsync->value != s_vsync_list.curvalue)
	{
		Cvar_SetValue("r_vsync", s_vsync_list.curvalue);
		restart = true;
	}

	/* The ray-traced AO's trace size and rays (TASK157): read from the next frame */
	if (!(s_ao_scale_list.generic.flags & QMF_GRAYED) &&
		VidRules_AOScaleIndex(Cvar_VariableValue("r_rt_ao_scale")) != s_ao_scale_list.curvalue)
	{
		Cvar_SetValue("r_rt_ao_scale", VidRules_AOScaleValue(s_ao_scale_list.curvalue));
	}

	if (!(s_ao_rays_list.generic.flags & QMF_GRAYED) &&
		VidRules_AORaysIndex(Cvar_VariableValue("r_rt_ao_rays")) != s_ao_rays_list.curvalue)
	{
		Cvar_SetValue("r_rt_ao_rays", s_ao_rays_list.curvalue + 1);
	}

	ApplyList(&s_colorlight_list, VIDITEM_COLORLIGHT);
	ApplyList(&s_bloom_list, VIDITEM_BLOOM);
	ApplyList(&s_dynlight_list, VIDITEM_DYNLIGHT);

	/* The MetalFX options: the renderer reads them at the restart below
	   and writes back what the GPU kept. */
	ApplyList(&s_mfx_interp_list, VIDITEM_MFX_INTERP);

	if (!(s_mfx_scaler_list.generic.flags & QMF_GRAYED))
	{
		if (VidRules_ScalerIndex(Cvar_VariableValue("r_metalfx")) != s_mfx_scaler_list.curvalue)
		{
			Cvar_SetValue("r_metalfx", s_mfx_scaler_list.curvalue);
		}
	}

	/* the Metal layer's colour space (TASK121): read from the next frame */
	if (!(s_colorspace_list.generic.flags & QMF_GRAYED) &&
		(Cvar_VariableValue("metal_colorspace") == 1.0f) != (s_colorspace_list.curvalue == 1))
	{
		Cvar_SetValue("metal_colorspace", s_colorspace_list.curvalue);
	}

	/* the water reflections' hit shading (TASK158): its index, written when it changed */
	if (!(s_rtw_hits_list.generic.flags & QMF_GRAYED) &&
		(VidRules_WaterHitsIndex(Cvar_VariableValue("r_rt_water_hits")) != s_rtw_hits_list.curvalue))
	{
		Cvar_SetValue("r_rt_water_hits", s_rtw_hits_list.curvalue);
	}

	if (!(s_mfx_scale_list.generic.flags & QMF_GRAYED) &&
		s_mfx_scale_list.curvalue < VidRules_NumScales())
	{
		const float want = VidRules_ScaleValue(s_mfx_scale_list.curvalue);

		if (VidRules_ScaleIndex(Cvar_VariableValue("r_metalfx_scale")) !=
			s_mfx_scale_list.curvalue)
		{
			Cvar_SetValue("r_metalfx_scale", want);
		}
	}

	/* anisotropic filtering */
	if (s_af_list.curvalue == 0)
	{
		if (gl_anisotropic->value != 0)
		{
			Cvar_SetValue("r_anisotropic", 0);
			restart = true;
		}
	}
	else
	{
		if (gl_anisotropic->value != pow(2, s_af_list.curvalue))
		{
			Cvar_SetValue("r_anisotropic", pow(2, s_af_list.curvalue));
			restart = true;
		}
	}

	/* multisample anti-aliasing; a greyed item is not a setting */
	if (s_msaa_list.generic.flags & QMF_GRAYED)
	{
		/* nothing to write */
	}
	else if (s_msaa_list.curvalue == 0)
	{
		if (gl_msaa_samples->value != 0)
		{
			Cvar_SetValue("r_msaa_samples", 0);
			restart = true;
		}
	}
	else
	{
		if (gl_msaa_samples->value != pow(2, s_msaa_list.curvalue))
		{
			Cvar_SetValue("r_msaa_samples", pow(2, s_msaa_list.curvalue));
			restart = true;
		}
	}

	if (restart)
	{
		Cbuf_AddText("vid_restart\n");
	}

	M_ForceMenuOff();
}

static void
RestartNeededSDL3Msg(void *unused)
{
	// gl1 and Metal take vid_gamma at vid_restart.
	if (CurrentRendererByCvar() == ref_gl1)
	{
		Menu_SetStatusBar(&s_opengl_menu, "apply required");
	}
}

static void
RestartNeededForAllMsg(void *unused)
{
	Menu_SetStatusBar(&s_opengl_menu, "apply required");
	Menu_SetStatusBar(&s_ropts_menu, "apply required");
}

/*
 * The renderer's own settings are a page of their own: one list of every
 * item is 240 units tall, as tall as the smallest virtual screen, and
 * centred it starts over the banner. Each page is centred and fits below
 * the banner and above the bottom edge down to that height;
 * tests/menu/videomenu_test.c asserts it.
 */
static void
OpenRendererOptions(void *unused)
{
	s_ropts_menu.draw = Default_MenuDraw;
	s_ropts_menu.key = Default_MenuKey;

	M_PushMenu(&s_ropts_menu);
}

void
VID_MenuInit(void)
{
	int y = 30;
	int sy = 20;	/* the renderer page's own rows */

	if (!vid_renderer)
	{
		vid_renderer = Cvar_Get("vid_renderer", "gl3", CVAR_ARCHIVE);
	}

	Q_strlcpy(s_built_renderer, vid_renderer->string, sizeof(s_built_renderer));

	// Renderer selection box.
	// MAXRENDERERS + Custom + NULL.
	static const char *renderers[MAXRENDERERS + 2] = { NULL };
	Renderer_FillRenderdef();

	for (int i = 0; i < numrenderer; i++)
	{
		renderers[i] = rendererlist[i].boxstr;
	}

	renderers[numrenderer] = CUSTOM_MODE_NAME;

	// Generate mode list.
	static const char *resolutions[NUMMODES + 3];

	for (int i = 0; i < NUMMODES; i++)
	{
		resolutions[i] = modes[i].modestr;
	}

	resolutions[NUMMODES] = AUTO_MODE_NAME;
	resolutions[NUMMODES + 1] = CUSTOM_MODE_NAME;
	resolutions[NUMMODES + 2] = NULL;

	const renderer_type current_renderer = CurrentRendererByCvar();

	static const char *uiscale_names[] = {
		"auto",
		"1x",
		"2x",
		"3x",
		"4x",
		"5x",
		"6x",
		"custom",
		NULL
	};

	static const char *onoff_names[] = {
		"off",
		"on",
		NULL
	};

	static const char *yesno_names[] = {
		"no",
		"yes",
		NULL
	};

	/* what a greyed screen item shows (SetScreenRule) */
	static const char *on_names[] = { "on", NULL };
	static const char *whole_names[] = { "the whole screen", NULL };
	static const char *native_names[] = { "native", NULL };
	static const char *metal_names[] = { "metal", NULL };

	static const char *fullscreen_names[] = {
		"no",
		"native fullscreen",
		"fullscreen window",
		NULL
	};

	static const char *pow2_names[] = {
		"off",
		"2x",
		"4x",
		"8x",
		"16x",
		NULL
	};

	static const char *na_filter_names[] = {
		"n/a",
		NULL
	};

	// This must match 'gl_filter_list' values (except "custom")
	static const char *filter_names[] = {
		"pixelated",
		"linear",
		"standard",
		"trilinear",
		"custom",
		NULL
	};

	if (!r_mode)
	{
		r_mode = Cvar_Get("r_mode", "4", 0);
	}

	if (!vid_displayindex)
	{
		vid_displayindex = Cvar_Get("vid_displayindex", "0", CVAR_ARCHIVE);
	}

	if (!r_hudscale)
	{
		r_hudscale = Cvar_Get("r_hudscale", "-1", CVAR_ARCHIVE);
	}

	if (!r_consolescale)
	{
		r_consolescale = Cvar_Get("r_consolescale", "-1", CVAR_ARCHIVE);
	}

	if (!r_menuscale)
	{
		r_menuscale = Cvar_Get("r_menuscale", "-1", CVAR_ARCHIVE);
	}

	if (!crosshair_scale)
	{
		crosshair_scale = Cvar_Get("crosshair_scale", "-1", CVAR_ARCHIVE);
	}

	if (!vid_renderer)
	{
		vid_renderer = Cvar_Get("vid_renderer", "gl3", CVAR_ARCHIVE);
	}

	if (!r_vsync)
	{
		r_vsync = Cvar_Get("r_vsync", "1", CVAR_ARCHIVE);
	}

	if (!gl_anisotropic)
	{
		gl_anisotropic = Cvar_Get("r_anisotropic", "0", CVAR_ARCHIVE);
	}

	if (!gl_msaa_samples)
	{
		gl_msaa_samples = Cvar_Get("r_msaa_samples", "0", CVAR_ARCHIVE);
	}

	s_opengl_menu.x = viddef.width * 0.50;
	s_opengl_menu.nitems = 0;
	s_opengl_menu.banner = "m_banner_video";

	s_renderer_list.generic.type = MTYPE_SPINCONTROL;
	s_renderer_list.generic.name = "renderer";
	s_renderer_list.generic.x = 0;
	s_renderer_list.generic.y = y;
	s_renderer_list.itemnames = renderers;
	s_renderer_list.curvalue = Renderer_GetRenderer();

	s_mode_list.generic.type = MTYPE_SPINCONTROL;
	s_mode_list.generic.name = "video mode";
	s_mode_list.generic.x = 0;
	s_mode_list.generic.y = (y += 10);
	s_mode_list.itemnames = resolutions;

	if (r_mode->value >= 0)
	{
		s_mode_list.curvalue = GetModePos(r_mode->value);
	}
	else if (r_mode->value == -2)
	{
		// 'auto' is before 'custom'
		s_mode_list.curvalue = GetCustomValue(&s_mode_list) - 1;
	}
	else
	{
		// 'custom'
		s_mode_list.curvalue = GetCustomValue(&s_mode_list);
	}

	s_brightness_slider.generic.type = MTYPE_SLIDER;
	s_brightness_slider.generic.name = "brightness";
	s_brightness_slider.generic.x = 0;
	s_brightness_slider.generic.y = (y += 10);
	s_brightness_slider.generic.callback = RestartNeededSDL3Msg;
	s_brightness_slider.cvar = "vid_gamma";
	s_brightness_slider.minvalue = 0.1f;
	s_brightness_slider.maxvalue = 2.0f;

	s_fov_slider.generic.type = MTYPE_SLIDER;
	s_fov_slider.generic.name = "field of view";
	s_fov_slider.generic.x = 0;
	s_fov_slider.generic.y = (y += 10);
	s_fov_slider.cvar = "fov";
	s_fov_slider.minvalue = 60;
	s_fov_slider.maxvalue = 120;
	s_fov_slider.slidestep = 1;
	s_fov_slider.printformat = "%.0f";

	/* The sliders and lists of the renderer. Each is here with every
	   renderer; where it does not apply it is greyed with its reason
	   and sets no cvar. */
	{
		const char *rname = s_built_renderer;
		const vidref_t ref = VidRules_Renderer(rname);
		const char *cv;

		s_intensity_slider.generic.type = MTYPE_SLIDER;
		s_intensity_slider.generic.name = "color intensity";
		s_intensity_slider.generic.x = 0;
		s_intensity_slider.generic.y = (sy += 10);
		s_intensity_slider.generic.callback = NULL;
		s_intensity_slider.cvar = (char *)VidRules_Cvar(VIDITEM_INTENSITY, rname);
		s_intensity_slider.minvalue = 0.1f;
		s_intensity_slider.maxvalue = 5.0f;
		s_intensity_slider.slidestep = 0;
		s_intensity_slider.printformat = NULL;

		s_overbrightbits_slider.generic.type = MTYPE_SLIDER;
		s_overbrightbits_slider.generic.name = "overbrights";
		s_overbrightbits_slider.generic.x = 0;
		s_overbrightbits_slider.generic.y = (sy += 10);
		s_overbrightbits_slider.cvar = (char *)VidRules_Cvar(VIDITEM_OVERBRIGHT, rname);
		s_overbrightbits_slider.minvalue = 0.1f;
		s_overbrightbits_slider.maxvalue = 5.0f;
		s_overbrightbits_slider.slidestep = 0;
		s_overbrightbits_slider.printformat = NULL;

		if (ref == VIDREF_GL1 || ref == VIDREF_METAL)
		{
			/* gl1's ranges; Metal reads gl1's cvars */
			s_intensity_slider.generic.callback = RestartNeededForAllMsg;
			s_intensity_slider.minvalue = 1.0f;
			s_intensity_slider.maxvalue = 10.0f;

			s_overbrightbits_slider.minvalue = 0;
			s_overbrightbits_slider.maxvalue = 2;
			s_overbrightbits_slider.slidestep = 1;
			s_overbrightbits_slider.printformat = "%.0f";
		}
		else if (ref == VIDREF_VK)
		{
			s_intensity_slider.minvalue = 0;
			s_intensity_slider.maxvalue = 5;
			s_intensity_slider.slidestep = 1;
			s_intensity_slider.printformat = "%.0f";
		}

		s_minlight_slider.generic.type = MTYPE_SLIDER;
		s_minlight_slider.generic.name = "min. light level";
		s_minlight_slider.generic.x = 0;
		s_minlight_slider.generic.y = (sy += 10);
		s_minlight_slider.generic.callback = RestartNeededForAllMsg;
		s_minlight_slider.cvar = (char *)VidRules_Cvar(VIDITEM_MINLIGHT, rname);
		s_minlight_slider.minvalue = 0;
		s_minlight_slider.maxvalue = 32;
		s_minlight_slider.slidestep = 4;
		s_minlight_slider.printformat = "%.0f";

		/* color light: gl3 and gl4 */
		s_colorlight_list.generic.type = MTYPE_SPINCONTROL;
		s_colorlight_list.generic.name = "color light";
		s_colorlight_list.generic.x = 0;
		s_colorlight_list.generic.y = (sy += 10);
		s_colorlight_list.itemnames = yesno_names;
		cv = VidRules_Cvar(VIDITEM_COLORLIGHT, rname);
		s_colorlight_list.curvalue = cv ? (Cvar_Get(cv, "1", CVAR_ARCHIVE)->value != 0) : 0;

		/* bloom: gl4 */
		s_bloom_list.generic.type = MTYPE_SPINCONTROL;
		s_bloom_list.generic.name = "bloom";
		s_bloom_list.generic.x = 0;
		s_bloom_list.generic.y = (sy += 10);
		s_bloom_list.itemnames = yesno_names;
		cv = VidRules_Cvar(VIDITEM_BLOOM, rname);
		s_bloom_list.curvalue = cv ? (Cvar_Get(cv, "1", CVAR_ARCHIVE)->value != 0) : 0;

		/* dynamic light: vk */
		s_dynlight_list.generic.type = MTYPE_SPINCONTROL;
		s_dynlight_list.generic.name = "dynamic light";
		s_dynlight_list.generic.x = 0;
		s_dynlight_list.generic.y = (sy += 10);
		s_dynlight_list.itemnames = yesno_names;
		cv = VidRules_Cvar(VIDITEM_DYNLIGHT, rname);
		s_dynlight_list.curvalue = cv ? (Cvar_Get(cv, "1", CVAR_ARCHIVE)->value != 0) : 0;

		SetRule(&s_intensity_slider.generic, VIDITEM_INTENSITY, 0);
		SetRule(&s_overbrightbits_slider.generic, VIDITEM_OVERBRIGHT, 0);
		SetRule(&s_minlight_slider.generic, VIDITEM_MINLIGHT, 0);
		SetRule(&s_colorlight_list.generic, VIDITEM_COLORLIGHT, 0);
		SetRule(&s_bloom_list.generic, VIDITEM_BLOOM, 0);
		SetRule(&s_dynlight_list.generic, VIDITEM_DYNLIGHT, 0);

		/* A greyed list shows that it has no value here. */
		static const char *na_names[] = { "n/a", NULL };
		menulist_s *lists[] = { &s_colorlight_list, &s_bloom_list, &s_dynlight_list };
		for (int i = 0; i < ARRLEN(lists); i++)
		{
			if (lists[i]->generic.flags & QMF_GRAYED)
			{
				lists[i]->itemnames = na_names;
				lists[i]->curvalue = 0;
			}
		}
	}

	s_uiscale_list.generic.type = MTYPE_SPINCONTROL;
	s_uiscale_list.generic.name = "ui scale";
	s_uiscale_list.generic.x = 0;
	s_uiscale_list.generic.y = (y += 10);
	s_uiscale_list.itemnames = uiscale_names;
	if (r_hudscale->value != r_consolescale->value ||
		r_hudscale->value != r_menuscale->value ||
		r_hudscale->value != crosshair_scale->value)
	{
		s_uiscale_list.curvalue = GetCustomValue(&s_uiscale_list);
	}
	else if (r_hudscale->value < 0)
	{
		s_uiscale_list.curvalue = 0;
	}
	else if (r_hudscale->value > 0 &&
			r_hudscale->value < GetCustomValue(&s_uiscale_list) &&
			r_hudscale->value == (int)r_hudscale->value)
	{
		s_uiscale_list.curvalue = r_hudscale->value;
	}
	else
	{
		s_uiscale_list.curvalue = GetCustomValue(&s_uiscale_list);
	}

	s_fs_box.generic.type = MTYPE_SPINCONTROL;
	s_fs_box.generic.name = "fullscreen";
	s_fs_box.generic.x = 0;
	s_fs_box.generic.y = (y += 10);
	s_fs_box.itemnames = fullscreen_names;
	s_fs_box.curvalue = (int)vid_fullscreen->value;

	/* iOS has one screen: the display is shown greyed with its reason (the desktop menu has no such row, its display is a cvar) */
	if (VID_PLATFORM == VIDPLAT_IOS)
	{
		static const char *display_names[] = { "this screen", NULL };

		s_display_list.generic.type = MTYPE_SPINCONTROL;
		s_display_list.generic.name = "display";
		s_display_list.generic.x = 0;
		s_display_list.generic.y = (y += 10);
		s_display_list.itemnames = display_names;
		s_display_list.curvalue = 0;
		SetScreenRule(&s_display_list.generic, VIDSCR_DISPLAY, display_names);
	}

	s_vsync_list.generic.type = MTYPE_SPINCONTROL;
	s_vsync_list.generic.name = "vertical sync";
	s_vsync_list.generic.x = 0;
	s_vsync_list.generic.y = (y += 10);
	s_vsync_list.itemnames = yesno_names;
	s_vsync_list.curvalue = (r_vsync->value != 0);
	SetScreenRule(&s_vsync_list.generic, VIDSCR_VSYNC, on_names);

	SetScreenRule(&s_fs_box.generic, VIDSCR_FULLSCREEN, whole_names);
	SetScreenRule(&s_mode_list.generic, VIDSCR_MODE, native_names);
	SetScreenRule(&s_renderer_list.generic, VIDSCR_RENDERER, metal_names);

	s_af_list.generic.type = MTYPE_SPINCONTROL;
	s_af_list.generic.name = "aniso filtering";
	s_af_list.generic.x = 0;
	s_af_list.generic.y = (y += 10);
	s_af_list.itemnames = pow2_names;
	s_af_list.curvalue = 0;
	if (gl_anisotropic->value)
	{
		do
		{
			s_af_list.curvalue++;
		} while (pow2_names[s_af_list.curvalue] &&
				pow(2, s_af_list.curvalue) <= gl_anisotropic->value);
		s_af_list.curvalue--;
	}

	s_msaa_list.generic.type = MTYPE_SPINCONTROL;
	s_msaa_list.generic.name = "multisampling";
	s_msaa_list.generic.x = 0;
	s_msaa_list.generic.y = (y += 10);
	s_msaa_list.itemnames = pow2_names;
	s_msaa_list.curvalue = 0;
	if (gl_msaa_samples->value)
	{
		do
		{
			s_msaa_list.curvalue++;
		} while (pow2_names[s_msaa_list.curvalue] &&
				pow(2, s_msaa_list.curvalue) <= gl_msaa_samples->value);
		s_msaa_list.curvalue--;
	}

	/* ref_metal has no MSAA: greyed with the reason, with a single choice */
	s_msaa_list.generic.flags = 0;
	s_msaa_list.generic.statusbar = NULL;
	SetRule(&s_msaa_list.generic, VIDITEM_MSAA, 0);
	if (s_msaa_list.generic.flags & QMF_GRAYED)
	{
		static const char *msaa_off_names[] = {"off", NULL};

		s_msaa_list.itemnames = msaa_off_names;
		s_msaa_list.curvalue = 0;
	}

	s_filter_list.generic.type = MTYPE_SPINCONTROL;
	s_filter_list.generic.name = "texture filter";
	s_filter_list.curvalue = 0;
	s_filter_list.generic.callback = ApplyFilter;
	s_filter_list.generic.x = 0;

	const char* filter = NULL;
	int mode = 0;

	s_filter_list.generic.y = (y += 10);
	s_filter_list.generic.flags = 0;
	s_filter_list.itemnames = filter_names;

	switch (current_renderer)
	{
		case ref_gl3:
		case ref_gl1:
			filter = Cvar_VariableString("gl_texturemode");
			mode = gl_filter_list_len;	// "custom" by default

			for (int i = 0; i < gl_filter_list_len; i++)
			{
				if (Q_stricmp(filter, gl_filter_list[i]) == 0)
				{
					mode = i;
					break;
				}
			}
			break;

		case ref_soft:
			s_filter_list.itemnames = onoff_names;

			filter = Cvar_VariableString("sw_texture_filtering");
			mode = 0;

			if (Q_stricmp(filter, "1") == 0)
			{
				mode = 1;
			}
			break;

		default:
			s_filter_list.itemnames = na_filter_names;
			break;
	}

	SetRule(&s_filter_list.generic, VIDITEM_FILTER, 0);

	s_filter_list.curvalue = mode;

	/* The Metal options. The scaler applies from the next frame, after
	   "apply" here; a mode the GPU lacks is marked and cannot be chosen,
	   and what the renderer kept after the restart is what shows next. */
	{
		const int mask = MetalModeMask();
		float v = Cvar_VariableValue("r_metalfx");
		static const char *scaler_base[] = { "off", "spatial", "temporal", "temporal denoised" };
		static char scaler_text[4][40];
		static const char *scaler_names[5];
		static const char *denoiser_names[] = { "off", "bilateral", "MetalFX", NULL };
		static const char *hits_names[] = { "screen space", "world space", NULL };
		static const char *na_names[] = { "n/a", NULL };
		/* read without creating it: the ray-tracing code owns it */
		const float dn = Cvar_VariableValue("r_rt_denoiser");
		const float hits = Cvar_VariableValue("r_rt_water_hits");
		const int water = VidRules_WaterOn(Cvar_VariableValue("r_rt_water"));

		for (int i = 0; i < 4; i++)
		{
			if (VidRules_ScalerModeAvailable(mask, i))
			{
				scaler_names[i] = scaler_base[i];
			}
			else
			{
				Com_sprintf(scaler_text[i], sizeof(scaler_text[i]), "%s (n/a)", scaler_base[i]);
				scaler_names[i] = scaler_text[i];
			}
		}
		scaler_names[4] = NULL;

		s_mfx_scaler_list.generic.type = MTYPE_SPINCONTROL;
		s_mfx_scaler_list.generic.name = "MetalFX scaler";
		s_mfx_scaler_list.generic.x = 0;
		s_mfx_scaler_list.generic.y = (sy += 20);
		s_mfx_scaler_list.generic.callback = MetalScalerChanged;
		s_mfx_scaler_list.itemnames = scaler_names;
		s_mfx_scaler_list.curvalue = VidRules_ScalerIndex(v);
		s_mfx_scaler_prev = s_mfx_scaler_list.curvalue;

		s_mfx_scale_list.generic.type = MTYPE_SPINCONTROL;
		s_mfx_scale_list.generic.name = "MetalFX scale";
		s_mfx_scale_list.generic.x = 0;
		s_mfx_scale_list.generic.y = (sy += 10);
		s_mfx_scale_list.generic.callback = MetalScaleChanged;
		s_mfx_scale_list.itemnames = s_scale_na_names;
		s_mfx_scale_list.curvalue = 0;

		s_mfx_interp_list.generic.type = MTYPE_SPINCONTROL;
		s_mfx_interp_list.generic.name = "frame generation";
		s_mfx_interp_list.generic.x = 0;
		s_mfx_interp_list.generic.y = (sy += 10);
		s_mfx_interp_list.itemnames = onoff_names;
		s_mfx_interp_list.curvalue = (Cvar_VariableValue("r_metalfx_interp") != 0);

		s_denoiser_list.generic.type = MTYPE_SPINCONTROL;
		s_denoiser_list.generic.name = "denoiser";
		s_denoiser_list.generic.x = 0;
		s_denoiser_list.generic.y = (sy += 10);
		s_denoiser_list.itemnames = denoiser_names;
		s_denoiser_list.curvalue = VidRules_DenoiserIndex(dn);

		s_rtw_hits_list.generic.type = MTYPE_SPINCONTROL;
		s_rtw_hits_list.generic.name = "water reflections";
		s_rtw_hits_list.generic.x = 0;
		s_rtw_hits_list.generic.y = (sy += 10);
		s_rtw_hits_list.itemnames = hits_names;
		s_rtw_hits_list.curvalue = VidRules_WaterHitsIndex(hits);

		SetRule(&s_mfx_scaler_list.generic, VIDITEM_MFX_SCALER, s_mfx_scaler_list.curvalue);
		ScaleItemUpdate(s_mfx_scaler_list.curvalue);
		SetRule(&s_mfx_interp_list.generic, VIDITEM_MFX_INTERP, s_mfx_scaler_list.curvalue);
		SetRule(&s_denoiser_list.generic, VIDITEM_DENOISER, s_mfx_scaler_list.curvalue);
		SetRule(&s_rtw_hits_list.generic, VIDITEM_RTW_HITS, water);

		/* A greyed list shows that it has no value here. */
		if (s_mfx_scaler_list.generic.flags & QMF_GRAYED)
		{
			s_mfx_scaler_list.itemnames = na_names;
			s_mfx_scaler_list.curvalue = 0;
			s_mfx_scaler_prev = 0;
		}
		if (s_mfx_interp_list.generic.flags & QMF_GRAYED)
		{
			s_mfx_interp_list.itemnames = na_names;
			s_mfx_interp_list.curvalue = 0;
		}
		if (s_denoiser_list.generic.flags & QMF_GRAYED)
		{
			s_denoiser_list.itemnames = na_names;
			s_denoiser_list.curvalue = 0;
		}
		if (s_rtw_hits_list.generic.flags & QMF_GRAYED)
		{
			s_rtw_hits_list.itemnames = na_names;
			s_rtw_hits_list.curvalue = 0;
		}
	}

	/* The ray-traced AO's trace size and rays (TASK157): greyed on other
	   renderers and while r_rt_ao is off, read without creating them. */
	{
		static const char *ao_scale_names[] = { "full", "1/2", "1/3", NULL };
		static const char *ao_rays_names[] = { "1", "2", NULL };
		static const char *ao_na_names[] = { "n/a", NULL };
		const int ao_on = (Cvar_VariableValue("r_rt_ao") != 0);

		s_ao_scale_list.generic.type = MTYPE_SPINCONTROL;
		s_ao_scale_list.generic.name = "AO trace size";
		s_ao_scale_list.generic.x = 0;
		s_ao_scale_list.generic.y = (sy += 10);
		s_ao_scale_list.itemnames = ao_scale_names;
		s_ao_scale_list.curvalue = VidRules_AOScaleIndex(Cvar_VariableValue("r_rt_ao_scale"));

		s_ao_rays_list.generic.type = MTYPE_SPINCONTROL;
		s_ao_rays_list.generic.name = "AO rays";
		s_ao_rays_list.generic.x = 0;
		s_ao_rays_list.generic.y = (sy += 10);
		s_ao_rays_list.itemnames = ao_rays_names;
		s_ao_rays_list.curvalue = VidRules_AORaysIndex(Cvar_VariableValue("r_rt_ao_rays"));

		SetRule(&s_ao_scale_list.generic, VIDITEM_AO_SCALE, ao_on);
		SetRule(&s_ao_rays_list.generic, VIDITEM_AO_RAYS, ao_on);

		if (s_ao_scale_list.generic.flags & QMF_GRAYED)
		{
			s_ao_scale_list.itemnames = ao_na_names;
			s_ao_scale_list.curvalue = 0;
		}
		if (s_ao_rays_list.generic.flags & QMF_GRAYED)
		{
			s_ao_rays_list.itemnames = ao_na_names;
			s_ao_rays_list.curvalue = 0;
		}
	}

	/* The Metal layer's colour space (TASK121): greyed on other renderers */
	{
		static const char *cs_names[] = { "untagged", "sRGB", NULL };
		static const char *cs_na_names[] = { "n/a", NULL };

		s_colorspace_list.generic.type = MTYPE_SPINCONTROL;
		s_colorspace_list.generic.name = "colour space";
		s_colorspace_list.generic.x = 0;
		s_colorspace_list.generic.y = (sy += 10);
		s_colorspace_list.itemnames = cs_names;
		s_colorspace_list.curvalue = (Cvar_VariableValue("metal_colorspace") == 1.0f);

		SetRule(&s_colorspace_list.generic, VIDITEM_COLORSPACE, 0);

		if (s_colorspace_list.generic.flags & QMF_GRAYED)
		{
			s_colorspace_list.itemnames = cs_na_names;
			s_colorspace_list.curvalue = 0;
		}
	}

	/* The renderer's own settings are a page of their own, with
	   every item in it always shown. */
	s_ropts_action.generic.type = MTYPE_ACTION;
	s_ropts_action.generic.name = "renderer options...";
	s_ropts_action.generic.x = 0;
	s_ropts_action.generic.y = (y += 20);
	s_ropts_action.generic.callback = OpenRendererOptions;
	s_ropts_action.generic.statusbar = "lighting, MetalFX; greyed: n/a here";

	s_defaults_action.generic.type = MTYPE_ACTION;
	s_defaults_action.generic.name = "reset to default";
	s_defaults_action.generic.x = 0;
	s_defaults_action.generic.y = (y += 10);
	s_defaults_action.generic.callback = ResetDefaults;

	s_ropts_apply_action.generic.type = MTYPE_ACTION;
	s_ropts_apply_action.generic.name = "apply";
	s_ropts_apply_action.generic.x = 0;
	s_ropts_apply_action.generic.y = (sy += 20);
	s_ropts_apply_action.generic.callback = ApplyChanges;

	s_apply_action.generic.type = MTYPE_ACTION;
	s_apply_action.generic.name = "apply";
	s_apply_action.generic.x = 0;
	s_apply_action.generic.y = (y += 10);
	s_apply_action.generic.callback = ApplyChanges;

	Menu_AddItem(&s_opengl_menu, (void *)&s_renderer_list);
	Menu_AddItem(&s_opengl_menu, (void *)&s_mode_list);

	Menu_AddItem(&s_opengl_menu, (void *)&s_brightness_slider);
	Menu_AddItem(&s_opengl_menu, (void *)&s_fov_slider);
	Menu_AddItem(&s_opengl_menu, (void *)&s_uiscale_list);
	Menu_AddItem(&s_opengl_menu, (void *)&s_fs_box);
	if (VID_PLATFORM == VIDPLAT_IOS)
	{
		Menu_AddItem(&s_opengl_menu, (void *)&s_display_list);
	}
	Menu_AddItem(&s_opengl_menu, (void *)&s_vsync_list);
	Menu_AddItem(&s_opengl_menu, (void *)&s_af_list);
	Menu_AddItem(&s_opengl_menu, (void *)&s_msaa_list);
	Menu_AddItem(&s_opengl_menu, (void *)&s_filter_list);
	Menu_AddItem(&s_opengl_menu, (void *)&s_ropts_action);
	Menu_AddItem(&s_opengl_menu, (void *)&s_defaults_action);
	Menu_AddItem(&s_opengl_menu, (void *)&s_apply_action);

	Menu_SetStatusBar(&s_opengl_menu, NULL);
	Menu_Center(&s_opengl_menu);
	s_opengl_menu.x -= 8;

	/* the renderer's page */
	s_ropts_menu.x = viddef.width * 0.50;
	s_ropts_menu.nitems = 0;
	s_ropts_menu.cursor = 0;
	s_ropts_menu.banner = "m_banner_video";

	Menu_AddItem(&s_ropts_menu, (void *)&s_intensity_slider);
	Menu_AddItem(&s_ropts_menu, (void *)&s_overbrightbits_slider);
	Menu_AddItem(&s_ropts_menu, (void *)&s_minlight_slider);
	Menu_AddItem(&s_ropts_menu, (void *)&s_colorlight_list);
	Menu_AddItem(&s_ropts_menu, (void *)&s_bloom_list);
	Menu_AddItem(&s_ropts_menu, (void *)&s_dynlight_list);
	Menu_AddItem(&s_ropts_menu, (void *)&s_mfx_scaler_list);
	Menu_AddItem(&s_ropts_menu, (void *)&s_mfx_scale_list);
	Menu_AddItem(&s_ropts_menu, (void *)&s_mfx_interp_list);
	Menu_AddItem(&s_ropts_menu, (void *)&s_denoiser_list);
	Menu_AddItem(&s_ropts_menu, (void *)&s_rtw_hits_list);
	Menu_AddItem(&s_ropts_menu, (void *)&s_ao_scale_list);
	Menu_AddItem(&s_ropts_menu, (void *)&s_ao_rays_list);
	Menu_AddItem(&s_ropts_menu, (void *)&s_colorspace_list);
	Menu_AddItem(&s_ropts_menu, (void *)&s_ropts_apply_action);

	Menu_SetStatusBar(&s_ropts_menu, "Esc: back to the video menu");
	Menu_Center(&s_ropts_menu);
	s_ropts_menu.x -= 8;
}

/*
 * VIDEO MENU
 */

void
M_Menu_Video_f(void)
{
	VID_MenuInit();
	s_opengl_menu.draw = Default_MenuDraw;
	s_opengl_menu.key  = Default_MenuKey;

	M_PushMenu(&s_opengl_menu);
}
