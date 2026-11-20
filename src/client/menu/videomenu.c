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
static int s_colorspace_pending = -1;  /* the choice held while HDR greys the item; -1 none */
static menulist_s s_hdr_list;          /* TASK196 (HD5): r_hdr, live */
static menuslider_s s_hdr_peak_slider; /* TASK196 (HD5): r_hdr_peak, live */
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

static qboolean PresentationApply(void);

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

	/* the presentation (TASK183): ref_metal reads it at vid_restart */
	if (PresentationApply())
	{
		restart = true;
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
static char s_hdr_peak_note[64];

static void RTSetRule(menucommon_s *item, vidrule_t rule);
static vidrule_t HdrRule(int item, const char *renderer, char *note, size_t len);

/* the HDR items' greying and the colour space's, which r_hdr 1 sets, from the cvars as they are now */
static void
HdrMenu_Rules(void)
{
	static const char *cs_names[] = { "untagged", "sRGB", NULL };
	static const char *cs_na_names[] = { "n/a", NULL };
	const int metal = (VidRules_Renderer(s_built_renderer) == VIDREF_METAL);

	RTSetRule(&s_hdr_list.generic, HdrRule(VIDHDR_ITEM_ON, s_built_renderer, NULL, 0));
	RTSetRule(&s_hdr_peak_slider.generic, HdrRule(VIDHDR_ITEM_PEAK, s_built_renderer,
			s_hdr_peak_note, sizeof(s_hdr_peak_note)));
	s_hdr_list.curvalue = (Cvar_VariableValue("r_hdr") == 1.0f);

	const int was_grayed = (s_colorspace_list.generic.flags & QMF_GRAYED) != 0;

	SetRule(&s_colorspace_list.generic, VIDITEM_COLORSPACE,
		metal && VidRules_HdrInForce(Cvar_VariableValue("r_hdr"), Cvar_VariableValue("r_presentation")));
	if (s_colorspace_list.generic.flags & QMF_GRAYED)
	{
		/* keep a pending choice while the item shows n/a */
		if (!was_grayed)
		{
			s_colorspace_pending = s_colorspace_list.curvalue;
		}
		s_colorspace_list.itemnames = cs_na_names;
		s_colorspace_list.curvalue = 0;
	}
	else
	{
		s_colorspace_list.itemnames = cs_names;
		if (was_grayed)
		{
			/* back from greyed: the choice held while it was, else the cvar */
			s_colorspace_list.curvalue = (s_colorspace_pending >= 0) ? s_colorspace_pending :
				(Cvar_VariableValue("metal_colorspace") == 1.0f);
			s_colorspace_pending = -1;
		}
	}
}

/* r_hdr from the menu: written at once, so the renderer switches at the next frame */
static void
HdrChanged(void *unused)
{
	if (!(s_hdr_list.generic.flags & QMF_GRAYED))
	{
		Cvar_SetValue("r_hdr", (float)(s_hdr_list.curvalue == 1));
	}
	HdrMenu_Rules();
}

/* the slider writes r_hdr_peak itself; the note follows */
static void
HdrPeakChanged(void *unused)
{
	HdrMenu_Rules();
}

static void
OpenRendererOptions(void *unused)
{
	HdrMenu_Rules();   /* a change on the tuning screen or the console shows here */
	s_ropts_menu.draw = Default_MenuDraw;
	s_ropts_menu.key = Default_MenuKey;

	M_PushMenu(&s_ropts_menu);
}

/*
 * The ray-tracing screen (TASK194): a switch for each pass and a slider for
 * each of its settings, rows of vidrules.c's table. Every change is written
 * to its cvar at once, so the renderer applies it from the next frame; the
 * screen keeps the 3D view behind it undimmed (keepview) and its items on
 * the left. Every item is always shown: greyed with its reason off metal,
 * on a device without ray tracing (r_rt_available, which ref_metal sets at
 * start), and for a slider while its effect is off.
 */
#define RT_MAX_ROWS 16

static menuframework_s s_rt_menu;
static menuaction_s s_rt_action;   /* the entry in the video menu */
/* TASK183: r_presentation; a mode that does not apply is shown with its reason */
static menulist_s s_pres_list;
static char s_pres_text[VIDPRES_COUNT][48];
static const char *s_pres_names[VIDPRES_COUNT + 1];
static menulist_s s_rt_switches[RT_MAX_ROWS];
static menuslider_s s_rt_sliders[RT_MAX_ROWS];
static menuaction_s s_rt_defaults_action;
static menucommon_s *s_rt_items[RT_MAX_ROWS];
static int s_rt_rows;

static void
RTSetRule(menucommon_s *item, vidrule_t rule)
{
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
 * The HDR items (TASK196, HD5; vidrules.h): the platform the HDR rules
 * take, the display's potential headroom as ref_metal publishes it
 * (r_hdr_headroom; empty, below 0 or not finite is no reading), and the
 * peak's note with that headroom beside it.
 */
#ifndef VID_HDRPLAT   /* the menu test builds this file for the Mac */
#if defined(__APPLE__) && defined(TARGET_OS_TV) && TARGET_OS_TV
#define VID_HDRPLAT VIDHDR_TVOS
#elif defined(__APPLE__) && defined(TARGET_OS_VISION) && TARGET_OS_VISION
#define VID_HDRPLAT VIDHDR_VISIONOS
#elif defined(__APPLE__) && TARGET_OS_IPHONE
#define VID_HDRPLAT VIDHDR_IOS
#else
#define VID_HDRPLAT VIDHDR_MAC
#endif
#endif

static float
HdrHeadroom(void)
{
	const char *v = Cvar_VariableString("r_hdr_headroom");

	return (v && v[0]) ? (float)atof(v) : -1.0f;
}

static vidrule_t
HdrRule(int item, const char *renderer, char *note, size_t len)
{
	const vidrule_t rule = VidRules_Hdr(item, renderer, VID_HDRPLAT, HdrHeadroom(),
			Cvar_VariableValue("r_presentation"), Cvar_VariableValue("r_hdr"));

	if (rule.enabled && item == VIDHDR_ITEM_PEAK)
	{
		snprintf(note, len, "headroom %.1f; peak held to it", HdrHeadroom());
		return (vidrule_t){ 1, note };
	}
	return rule;
}

/*
 * The tuning screen (TASK195): vidrules.c's rt_tune rows, each written to
 * its cvar at once (live), then the A/B toggle and the presets, which run
 * the console commands of rtpreset.c (rt_ab, rt_preset), and "apply
 * (restart)", live only after a row that needs vid_restart changed. As
 * the ray-tracing screen, it keeps the view undimmed and every item shown.
 */
#define TUNE_MAX_ROWS 12
#define TUNE_MAX_STEPS 80

static menuframework_s s_tune_menu;
static menuaction_s s_rt_tune_action;   /* the entry on the ray-tracing screen */
static menulist_s s_tune_lists[TUNE_MAX_ROWS];
static menuslider_s s_tune_sliders[TUNE_MAX_ROWS];
static menucommon_s *s_tune_items[TUNE_MAX_ROWS];
static char s_tune_steps[2][TUNE_MAX_STEPS][8];
static const char *s_tune_step_names[2][TUNE_MAX_STEPS + 1];
static menuaction_s s_tune_ab, s_tune_stock, s_tune_save, s_tune_load, s_tune_asb, s_tune_restart;
static menufield_s s_tune_name;
static char s_tune_ab_text[64], s_tune_status[96], s_tune_hdr_note[64];
static float s_tune_pres_open;
static int s_tune_rows;

/* the pair r_rt_sun_angles as two numbers, its default when it is not two */
static void
TuneReadPair(const vidrt_control_t *c, float *a, float *b)
{
	if (sscanf(Cvar_VariableString(c->cvar), "%f %f", a, b) != 2 &&
		sscanf(c->def, "%f %f", a, b) != 2)
	{
		*a = *b = 0.0f;
	}
}

static int
TuneIndex(const vidrt_control_t *c, float v)
{
	int n = (int)((c->max - c->min) / c->step + 0.5f), i;

	if (!isfinite(v))
	{
		v = (float)atof(c->def);
	}

	/* clamped before the cast: a float out of int's range (r_metalfx 1e30) is undefined to convert */
	if (!(v >= c->min))
	{
		return 0;
	}
	if (v > c->max)
	{
		return n;
	}
	i = (int)((v - c->min) / c->step + 0.5f);
	return (i < 0) ? 0 : ((i > n) ? n : i);
}

static void
TuneMenu_Values(void)
{
	for (int i = 0; i < s_tune_rows; i++)
	{
		const vidrt_control_t *c = VidRules_TuneControl(i);
		menulist_s *l = (menulist_s *)s_tune_items[i];
		float a, b;

		if (c->part == 1 || c->part == 2)
		{
			TuneReadPair(c, &a, &b);
			l->curvalue = TuneIndex(c, (c->part == 1) ? a : b);
		}
		else if (c->part == VIDTUNE_PART_DEBUG)
		{
			l->curvalue = 0;
			for (int v = VidRules_NumDebugViews() - 1; v > 0; v--)
			{
				if (Cvar_VariableValue(VidRules_DebugView(v)) != 0)
				{
					l->curvalue = v;
				}
			}
		}
		else if (c->choices)
		{
			l->curvalue = TuneIndex(c, Cvar_VariableValue(c->cvar));
		}
	}
}

/* the side rt_ab shows: the label r_rt_ab carries up to its '|', A when empty */
static const char *
TuneSide(void)
{
	const char *spec = Cvar_VariableString("r_rt_ab");
	const char *bar = strchr(spec, '|');

	if (!spec[0] || !bar)
	{
		return "A: your settings";
	}
	snprintf(s_tune_status, sizeof(s_tune_status), "%.*s", (int)(bar - spec), spec);
	return s_tune_status;
}

static void
TuneMenu_Rules(void)
{
	const int available = (Cvar_VariableValue("r_rt_available") != 0);
	const float pres = Cvar_VariableValue("r_presentation");
	const int metal = VidRules_RTEntry(s_built_renderer).enabled;
	const char *nameerr = VidRules_PresetNameError(s_tune_name.buffer);
	const vidrule_t norule = { 0, "needs the Metal renderer" };

	for (int i = 0; i < s_tune_rows; i++)
	{
		const vidrt_control_t *c = VidRules_TuneControl(i);
		const vidrt_control_t *e = VidRules_RTControl(c->effect);
		const int on = e ? VidRules_RTEffective(c->effect, s_built_renderer, available, pres,
				Cvar_VariableValue(e->cvar)) : 1;

		if (VidRules_HdrItem(c->cvar) >= 0)
		{
			/* TASK196 (HD5): the HDR rows by the display's reading */
			RTSetRule(s_tune_items[i], HdrRule(VidRules_HdrItem(c->cvar), s_built_renderer,
					s_tune_hdr_note, sizeof(s_tune_hdr_note)));
			continue;
		}
		RTSetRule(s_tune_items[i], VidRules_TuneOverridden(i, s_built_renderer, pres) ?
			(vidrule_t){ 0, "set by the presentation" } :
			VidRules_TuneItem(i, s_built_renderer, available, on));
	}

	snprintf(s_tune_ab_text, sizeof(s_tune_ab_text), "A/B: %s", TuneSide());
	RTSetRule(&s_tune_ab.generic, metal ? (vidrule_t){ 1, "Enter flips A and B; bind a key to rt_ab" } : norule);
	RTSetRule(&s_tune_stock.generic, metal ? (vidrule_t){ 1, "B is every setting at its default" } : norule);
	RTSetRule(&s_tune_name.generic, (vidrule_t){ 1, "a preset's name: letters, digits, '_' and '-'" });
	RTSetRule(&s_tune_save.generic, nameerr ? (vidrule_t){ 0, nameerr } :
		(vidrule_t){ 1, "saves to presets/rt-<name>.cfg" });
	RTSetRule(&s_tune_load.generic, nameerr ? (vidrule_t){ 0, nameerr } :
		(vidrule_t){ 1, "sets every tuning value from the preset" });
	RTSetRule(&s_tune_asb.generic, !metal ? norule : nameerr ? (vidrule_t){ 0, nameerr } :
		(vidrule_t){ 1, "the preset as B; writes no setting" });
	RTSetRule(&s_tune_restart.generic, VidRules_TuneRestart(s_built_renderer, pres != s_tune_pres_open));

	Menu_SetStatusBar(&s_tune_menu, (pres != s_tune_pres_open) ?
		"the presentation waits for apply (restart)" : "Esc: back; each change shows on the next frame");
}

static void
TuneChanged(void *self)
{
	menulist_s *l = (menulist_s *)self;
	const vidrt_control_t *c = VidRules_TuneControl(l->generic.localdata[0]);
	const float v = c ? c->min + c->step * (float)l->curvalue : 0.0f;
	char buf[32];
	float a, b;

	if (!c)
	{
		return;
	}
	if (c->part == 1 || c->part == 2)
	{
		TuneReadPair(c, &a, &b);
		Com_sprintf(buf, sizeof(buf), "%g %g", (c->part == 1) ? v : a, (c->part == 2) ? v : b);
		Cvar_Set(c->cvar, buf);
	}
	else if (c->part == VIDTUNE_PART_DEBUG)
	{
		/* one view at a time */
		for (int i = 1; i < VidRules_NumDebugViews(); i++)
		{
			Cvar_SetValue(VidRules_DebugView(i), (i == l->curvalue) ? 1.0f : 0.0f);
		}
	}
	else if (c->restart && !VidRules_PresentationMode(l->curvalue, s_built_renderer,
				Cvar_VariableValue("r_rt_available") != 0).enabled)
	{
		/* a mode that does not apply is shown, never chosen */
		l->curvalue = VidRules_PresentationIndex(Cvar_VariableValue(c->cvar));
	}
	else
	{
		Cvar_SetValue(c->cvar, v);
	}

	TuneMenu_Rules();
}

static void
TuneCommand(const char *verb)
{
	char cmd[96];

	/* the name is checked here too: it goes into a command line */
	if (VidRules_PresetNameError(s_tune_name.buffer))
	{
		return;
	}
	Com_sprintf(cmd, sizeof(cmd), "rt_preset %s %s\n", verb, s_tune_name.buffer);
	Cbuf_AddText(cmd);
}

static void TuneAB(void *unused) { Cbuf_AddText("rt_ab\n"); }
static void TuneStock(void *unused) { Cbuf_AddText("rt_ab stock\n"); }
static void TuneSave(void *unused) { TuneCommand("save"); }
static void TuneLoad(void *unused) { TuneCommand("load"); }
static void TuneAsB(void *unused) { TuneCommand("ab"); }

static void
TuneRestart(void *unused)
{
	if (!(s_tune_restart.generic.flags & QMF_GRAYED))
	{
		Cbuf_AddText("vid_restart\n");
		s_tune_pres_open = Cvar_VariableValue("r_presentation");
	}
}

static void
TuneDraw(menuframework_s *m)
{
	TuneMenu_Values();   /* a load or an rt_ab from the console shows at once */
	TuneMenu_Rules();
	Default_MenuDraw(m);
}

static void
TuneAddAction(menuaction_s *a, const char *name, int y, void (*fn)(void *))
{
	memset(a, 0, sizeof(*a));
	a->generic.type = MTYPE_ACTION;
	a->generic.name = name;
	a->generic.x = 0;
	a->generic.y = y;
	a->generic.callback = fn;
	Menu_AddItem(&s_tune_menu, (void *)a);
}

static void
TuneMenu_Init(void)
{
	int y = 0, nl = 0, ns = 0;

	memset(&s_tune_menu, 0, sizeof(s_tune_menu));
	s_tune_menu.banner = "m_banner_video";
	s_tune_menu.keepview = 1;
	s_tune_rows = 0;
	s_tune_pres_open = Cvar_VariableValue("r_presentation");

	for (int i = 0; (i < VidRules_TuneNum()) && (i < TUNE_MAX_ROWS); i++)
	{
		const vidrt_control_t *c = VidRules_TuneControl(i);
		menucommon_s *g;

		/* registered as the renderer does: saved with the configuration where it saves them */
		Cvar_Get(c->cvar, (c->part == VIDTUNE_PART_DEBUG) ? "0" : c->def, c->flags);

		if (c->choices || c->part)
		{
			menulist_s *l = &s_tune_lists[nl++];

			memset(l, 0, sizeof(*l));
			l->generic.type = MTYPE_SPINCONTROL;
			l->generic.callback = TuneChanged;
			l->itemnames = (const char **)c->choices;
			if (c->part == 1 || c->part == 2)
			{
				int k = 0;

				for (float v = c->min; v <= c->max + 0.001f && k < TUNE_MAX_STEPS; v += c->step, k++)
				{
					snprintf(s_tune_steps[c->part - 1][k], sizeof(s_tune_steps[0][0]), c->format, v);
					s_tune_step_names[c->part - 1][k] = s_tune_steps[c->part - 1][k];
				}
				s_tune_step_names[c->part - 1][k] = NULL;
				l->itemnames = (const char **)s_tune_step_names[c->part - 1];
			}
			g = &l->generic;
		}
		else
		{
			menuslider_s *s = &s_tune_sliders[ns++];

			memset(s, 0, sizeof(*s));
			s->generic.type = MTYPE_SLIDER;
			s->cvar = (char *)c->cvar;
			s->minvalue = c->min;
			s->maxvalue = c->max;
			s->slidestep = c->step;
			s->printformat = (char *)c->format;
			g = &s->generic;
		}

		g->name = c->label;
		g->x = 0;
		g->y = (y += 10);
		g->localdata[0] = i;
		s_tune_items[s_tune_rows++] = g;
		Menu_AddItem(&s_tune_menu, (void *)g);
	}

	TuneAddAction(&s_tune_ab, s_tune_ab_text, (y += 20), TuneAB);
	TuneAddAction(&s_tune_stock, "B: stock", (y += 10), TuneStock);

	memset(&s_tune_name, 0, sizeof(s_tune_name));
	s_tune_name.generic.type = MTYPE_FIELD;
	s_tune_name.generic.name = "preset name";
	s_tune_name.generic.flags = QMF_LEFT_JUSTIFY;
	s_tune_name.generic.x = 0;
	s_tune_name.generic.y = (y += 20);
	Field_InitState(&s_tune_name, "", VIDPRESET_MAXNAME, 16);
	Menu_AddItem(&s_tune_menu, (void *)&s_tune_name);

	TuneAddAction(&s_tune_save, "save preset", (y += 20), TuneSave);
	TuneAddAction(&s_tune_load, "load preset", (y += 10), TuneLoad);
	TuneAddAction(&s_tune_asb, "preset as B", (y += 10), TuneAsB);
	TuneAddAction(&s_tune_restart, "apply (restart)", (y += 20), TuneRestart);

	TuneMenu_Values();
	TuneMenu_Rules();
	Menu_Center(&s_tune_menu);
	s_tune_menu.x = (int)(viddef.width * 0.30f);
}

static void
OpenTuning(void *unused)
{
	TuneMenu_Init();
	s_tune_menu.draw = TuneDraw;
	s_tune_menu.key = Default_MenuKey;

	M_PushMenu(&s_tune_menu);
}

/* each row's greying, from the cvars as they are now */
static void
RTMenu_Rules(void)
{
	const int available = (Cvar_VariableValue("r_rt_available") != 0);
	const float pres = Cvar_VariableValue("r_presentation");

	for (int i = 0; i < s_rt_rows; i++)
	{
		const vidrt_control_t *c = VidRules_RTControl(i);
		const vidrt_control_t *e = VidRules_RTControl(c->effect);

		RTSetRule(s_rt_items[i], VidRules_RTRow(i, s_built_renderer, available, pres,
				e ? Cvar_VariableValue(e->cvar) : 0.0f));
	}

	RTSetRule(&s_rt_defaults_action.generic, VidRules_RTItem(-1, s_built_renderer, available, 1));

	/* TASK195: the tuning screen, greyed for the reasons the reset is */
	{
		vidrule_t r = VidRules_RTItem(-1, s_built_renderer, available, 1);

		RTSetRule(&s_rt_tune_action.generic,
			r.enabled ? (vidrule_t){ 1, "every other setting, A/B and presets, live" } : r);
	}
}

static void
RTSwitchChanged(void *self)
{
	menulist_s *l = (menulist_s *)self;
	const vidrt_control_t *c = VidRules_RTControl(l->generic.localdata[0]);

	if (c)
	{
		Cvar_SetValue(c->cvar, (float)l->curvalue);
	}

	RTMenu_Rules();
}

static void
RTChoiceChanged(void *self)
{
	menulist_s *l = (menulist_s *)self;
	const vidrt_control_t *c = VidRules_RTControl(l->generic.localdata[0]);

	if (c)
	{
		/* the only choice row is r_rt_ao_scale: full, 1/2, 1/3 */
		Cvar_SetValue(c->cvar, VidRules_AOScaleValue(l->curvalue));
	}
}

/* each switch and choice shows its cvar; a slider reads its own as it draws */
static void
RTMenu_Values(void)
{
	for (int i = 0; i < s_rt_rows; i++)
	{
		const vidrt_control_t *c = VidRules_RTControl(i);

		if (c->choices)
		{
			((menulist_s *)s_rt_items[i])->curvalue = VidRules_AOScaleIndex(Cvar_VariableValue(c->cvar));
		}
		else if (c->effect < 0)
		{
			((menulist_s *)s_rt_items[i])->curvalue = VidRules_RTEffective(i, s_built_renderer,
				(Cvar_VariableValue("r_rt_available") != 0), Cvar_VariableValue("r_presentation"),
				Cvar_VariableValue(c->cvar));
		}
	}
}

static void
RTResetDefaults(void *unused)
{
	const int available = (Cvar_VariableValue("r_rt_available") != 0);
	const float pres = Cvar_VariableValue("r_presentation");
	int kept = 0;

	for (int i = 0; i < s_rt_rows; i++)
	{
		const vidrt_control_t *c = VidRules_RTControl(i);

		/* a switch the presentation decides is the user's own: not written */
		if (VidRules_RTOverridden(i, s_built_renderer, available, pres))
		{
			kept = 1;
			continue;
		}

		Cvar_Set(c->cvar, c->def);
	}

	RTMenu_Values();
	RTMenu_Rules();

	if (kept)
	{
		s_rt_defaults_action.generic.statusbar = "the presentation's switches are kept";
	}
}

static void
RTMenu_Init(void)
{
	static const char *onoff[] = { "off", "on", NULL };
	int y = 0, nsw = 0, nsl = 0;

	memset(&s_rt_menu, 0, sizeof(s_rt_menu));
	s_rt_menu.banner = "m_banner_video";
	s_rt_menu.keepview = 1;
	s_rt_rows = 0;

	for (int i = 0; (i < VidRules_RTNum()) && (i < RT_MAX_ROWS); i++)
	{
		const vidrt_control_t *c = VidRules_RTControl(i);
		menucommon_s *g;

		/* the menu registers each one saved with the configuration, as the renderer does */
		Cvar_Get(c->cvar, c->def, c->flags);

		if (c->effect < 0 || c->choices)
		{
			menulist_s *l = &s_rt_switches[nsw++];

			memset(l, 0, sizeof(*l));
			l->generic.type = MTYPE_SPINCONTROL;
			l->generic.callback = c->choices ? RTChoiceChanged : RTSwitchChanged;
			l->itemnames = c->choices ? c->choices : onoff;
			g = &l->generic;
		}
		else
		{
			menuslider_s *s = &s_rt_sliders[nsl++];

			memset(s, 0, sizeof(*s));
			s->generic.type = MTYPE_SLIDER;
			s->cvar = (char *)c->cvar;
			s->minvalue = c->min;
			s->maxvalue = c->max;
			s->slidestep = c->step;
			s->printformat = (char *)c->format;
			g = &s->generic;
			if (i > 0 && VidRules_RTControl(i - 1)->effect < 0)
			{
				y += 10;   /* a gap between the switches and the sliders */
			}
		}

		g->name = c->label;
		g->x = 0;
		g->y = (y += 10);
		g->localdata[0] = i;
		s_rt_items[s_rt_rows++] = g;
		Menu_AddItem(&s_rt_menu, (void *)g);
	}

	memset(&s_rt_defaults_action, 0, sizeof(s_rt_defaults_action));
	s_rt_defaults_action.generic.type = MTYPE_ACTION;
	s_rt_defaults_action.generic.name = "reset to defaults";
	s_rt_defaults_action.generic.x = 0;
	s_rt_defaults_action.generic.y = (y += 20);
	s_rt_defaults_action.generic.callback = RTResetDefaults;
	Menu_AddItem(&s_rt_menu, (void *)&s_rt_defaults_action);

	memset(&s_rt_tune_action, 0, sizeof(s_rt_tune_action));
	s_rt_tune_action.generic.type = MTYPE_ACTION;
	s_rt_tune_action.generic.name = "tuning...";
	s_rt_tune_action.generic.x = 0;
	s_rt_tune_action.generic.y = (y += 10);
	s_rt_tune_action.generic.callback = OpenTuning;
	Menu_AddItem(&s_rt_menu, (void *)&s_rt_tune_action);

	RTMenu_Values();
	RTMenu_Rules();

	Menu_SetStatusBar(&s_rt_menu, VidRules_RTStatus(s_built_renderer, Cvar_VariableValue("r_rt_available") != 0,
			Cvar_VariableValue("r_presentation")));
	Menu_Center(&s_rt_menu);
	/* the items on the left, the view to their right left clear to judge */
	s_rt_menu.x = (int)(viddef.width * 0.30f);
}

/* the presentation's status bar: the rule of the mode the list shows */
static void
PresentationRule(void)
{
	const int avail = (Cvar_VariableValue("r_rt_available") != 0);
	const vidrule_t item = VidRules_PresentationMode(-1, s_built_renderer, avail);

	RTSetRule(&s_pres_list.generic, item);
	if (item.enabled)
	{
		s_pres_list.generic.statusbar = VidRules_PresentationMode(s_pres_list.curvalue, s_built_renderer, avail).text;
	}
}

static void
PresentationChanged(void *unused)
{
	PresentationRule();
}

static void
PresentationInit(int y)
{
	const vidrt_control_t *p = VidRules_Presentation();
	const int avail = (Cvar_VariableValue("r_rt_available") != 0);

	Cvar_Get(p->cvar, p->def, p->flags);   /* saved with the configuration, as the renderer registers it */

	for (int m = 0; m < VIDPRES_COUNT; m++)
	{
		const vidrule_t r = VidRules_PresentationMode(m, s_built_renderer, avail);

		/* every mode shown; one that does not apply carries its reason */
		if (r.enabled || VidRules_Renderer(s_built_renderer) != VIDREF_METAL)
		{
			snprintf(s_pres_text[m], sizeof(s_pres_text[m]), "%s", p->choices[m]);
		}
		else
		{
			snprintf(s_pres_text[m], sizeof(s_pres_text[m]), "%s (%s)", p->choices[m], r.text);
		}
		s_pres_names[m] = s_pres_text[m];
	}
	s_pres_names[VIDPRES_COUNT] = NULL;

	memset(&s_pres_list, 0, sizeof(s_pres_list));
	s_pres_list.generic.type = MTYPE_SPINCONTROL;
	s_pres_list.generic.name = p->label;
	s_pres_list.generic.x = 0;
	s_pres_list.generic.y = y;
	s_pres_list.generic.callback = PresentationChanged;
	s_pres_list.itemnames = s_pres_names;
	s_pres_list.curvalue = VidRules_PresentationIndex(Cvar_VariableValue(p->cvar));
	PresentationRule();
}

/* the presentation at apply: written when it changed to a mode that applies, then a vid_restart */
static qboolean
PresentationApply(void)
{
	const vidrt_control_t *p = VidRules_Presentation();
	const int avail = (Cvar_VariableValue("r_rt_available") != 0);

	if ((s_pres_list.generic.flags & QMF_GRAYED) ||
		!VidRules_PresentationMode(s_pres_list.curvalue, s_built_renderer, avail).enabled ||
		VidRules_PresentationIndex(Cvar_VariableValue(p->cvar)) == s_pres_list.curvalue)
	{
		return false;
	}

	Cvar_SetValue(p->cvar, s_pres_list.curvalue);
	return true;
}

static void
OpenRTOptions(void *unused)
{
	RTMenu_Init();
	s_rt_menu.draw = Default_MenuDraw;
	s_rt_menu.key = Default_MenuKey;

	M_PushMenu(&s_rt_menu);
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

		s_colorspace_pending = -1;
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

	/* HDR (TASK196, HD5): after the colour space, live, greyed with its reason (HdrMenu_Rules) */
	{
		static const char *hdr_names[] = { "off", "on", NULL };

		s_hdr_list.generic.type = MTYPE_SPINCONTROL;
		s_hdr_list.generic.name = "HDR";
		s_hdr_list.generic.x = 0;
		s_hdr_list.generic.y = (sy += 10);
		s_hdr_list.generic.callback = HdrChanged;
		s_hdr_list.itemnames = hdr_names;

		s_hdr_peak_slider.generic.type = MTYPE_SLIDER;
		s_hdr_peak_slider.generic.name = "HDR peak";
		s_hdr_peak_slider.generic.x = 0;
		s_hdr_peak_slider.generic.y = (sy += 10);
		s_hdr_peak_slider.generic.callback = HdrPeakChanged;
		s_hdr_peak_slider.cvar = "r_hdr_peak";
		s_hdr_peak_slider.minvalue = VIDHDR_PEAK_MIN;
		s_hdr_peak_slider.maxvalue = VIDHDR_PEAK_MAX;
		s_hdr_peak_slider.slidestep = VIDHDR_PEAK_STEP;
		s_hdr_peak_slider.printformat = "%.1f";

		HdrMenu_Rules();
	}

	/* The renderer's own settings are a page of their own, with
	   every item in it always shown. */
	s_ropts_action.generic.type = MTYPE_ACTION;
	s_ropts_action.generic.name = "renderer options...";
	s_ropts_action.generic.x = 0;
	s_ropts_action.generic.y = (y += 20);
	s_ropts_action.generic.callback = OpenRendererOptions;
	s_ropts_action.generic.statusbar = "lighting, MetalFX; greyed: n/a here";

	/* The ray-tracing screen (TASK194): greyed unless the renderer is metal. */
	s_rt_action.generic.type = MTYPE_ACTION;
	s_rt_action.generic.name = "ray tracing...";
	s_rt_action.generic.x = 0;
	s_rt_action.generic.y = (y += 10);
	s_rt_action.generic.callback = OpenRTOptions;
	RTSetRule(&s_rt_action.generic, VidRules_RTEntry(s_built_renderer));

	/* the presentation (TASK183): every mode shown, greyed with its reason */
	PresentationInit(y += 10);

	s_defaults_action.generic.type = MTYPE_ACTION;
	s_defaults_action.generic.name = "reset to default";
	s_defaults_action.generic.x = 0;
	s_defaults_action.generic.y = (y += 10);
	s_defaults_action.generic.callback = ResetDefaults;

	s_ropts_apply_action.generic.type = MTYPE_ACTION;
	s_ropts_apply_action.generic.name = "apply";
	s_ropts_apply_action.generic.x = 0;
	/* one line, not two, above apply: with the HDR items (HD5) the page fits below the banner at 240 lines */
	s_ropts_apply_action.generic.y = (sy += 10);
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
	Menu_AddItem(&s_opengl_menu, (void *)&s_rt_action);
	Menu_AddItem(&s_opengl_menu, (void *)&s_pres_list);
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
	Menu_AddItem(&s_ropts_menu, (void *)&s_hdr_list);
	Menu_AddItem(&s_ropts_menu, (void *)&s_hdr_peak_slider);
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
