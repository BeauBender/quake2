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
 * ref_metal: GetRefAPI, the cvars, Init and Shutdown, and the frame
 * (docs/plans/ref-metal-native.md). Skeleton (TASK84, N1): the frame is
 * cleared and presented, the 3D and 2D passes are opened, and every
 * registration and drawing entry point is a valid no-op that returns a
 * NULL handle and counts its call. R_SetMode and SetMode_impl are from
 * gl1_main.c, without the MSAA retry (ref_metal has no MSAA).
 *
 * =======================================================================
 */

#include "header/local.h"

refimport_t ri;

cvar_t *r_clear;
cvar_t *r_mode;
cvar_t *r_customwidth;
cvar_t *r_customheight;
cvar_t *r_norefresh;
cvar_t *r_vsync;
cvar_t *vid_fullscreen;
cvar_t *metal_stats;
cvar_t *gl_texturemode;
cvar_t *gl_anisotropic;
cvar_t *gl_nolerp_list;
cvar_t *r_lerp_list;
cvar_t *r_2D_unfiltered;
cvar_t *r_videos_unfiltered;
cvar_t *r_retexturing;
cvar_t *r_validation;
cvar_t *vid_gamma;

unsigned long rm_noop_calls;

static qboolean rm_opened;
static int rm_prev_mode = 4;

/* gl1's clear colour for r_clear 1 */
static const float rm_clear_color[4] = { 1.0f, 0.0f, 0.5f, 0.5f };

static void
R_Register(void)
{
	r_clear = ri.Cvar_Get("r_clear", "0", 0);
	r_mode = ri.Cvar_Get("r_mode", "4", CVAR_ARCHIVE);
	r_customwidth = ri.Cvar_Get("r_customwidth", "1024", CVAR_ARCHIVE);
	r_customheight = ri.Cvar_Get("r_customheight", "768", CVAR_ARCHIVE);
	r_norefresh = ri.Cvar_Get("r_norefresh", "0", 0);
	r_vsync = ri.Cvar_Get("r_vsync", "1", CVAR_ARCHIVE);
	vid_fullscreen = ri.Cvar_Get("vid_fullscreen", "0", CVAR_ARCHIVE);
	metal_stats = ri.Cvar_Get("metal_stats", "0", 0);

	/* gl1's, by gl1's names and defaults (gl1_main.c, R_Register) */
	gl_texturemode = ri.Cvar_Get("gl_texturemode", "GL_LINEAR_MIPMAP_NEAREST", CVAR_ARCHIVE);
	gl_anisotropic = ri.Cvar_Get("r_anisotropic", "0", CVAR_ARCHIVE);
	vid_gamma = ri.Cvar_Get("vid_gamma", "1.2", CVAR_ARCHIVE);
	r_retexturing = ri.Cvar_Get("r_retexturing", "1", CVAR_ARCHIVE);
	r_validation = ri.Cvar_Get("r_validation", "0", CVAR_ARCHIVE);
	gl_nolerp_list = ri.Cvar_Get("r_nolerp_list", "pics/conchars.pcx pics/ch1.pcx pics/ch2.pcx pics/ch3.pcx", CVAR_ARCHIVE);
	r_lerp_list = ri.Cvar_Get("r_lerp_list", "", CVAR_ARCHIVE);
	r_2D_unfiltered = ri.Cvar_Get("r_2D_unfiltered", "0", CVAR_ARCHIVE);
	r_videos_unfiltered = ri.Cvar_Get("r_videos_unfiltered", "0", CVAR_ARCHIVE);

	ri.Cmd_AddCommand("screenshot", R_ScreenShot);
	ri.Cmd_AddCommand("imagelist", R_ImageList_f);
}

/* from gl1_main.c */
static qboolean
SetMode_impl(int *pwidth, int *pheight, int mode, int fullscreen)
{
	R_Printf(PRINT_ALL, "Setting mode %d:", mode);

	if ((mode >= 0) && !ri.Vid_GetModeInfo(pwidth, pheight, mode))
	{
		R_Printf(PRINT_ALL, " invalid mode\n");
		return false;
	}

	if ((mode == -2) && !ri.GLimp_GetDesktopMode(pwidth, pheight))
	{
		R_Printf(PRINT_ALL, " can't detect mode\n");
		return false;
	}

	R_Printf(PRINT_ALL, " %dx%d (vid_fullscreen %i)\n", *pwidth, *pheight, fullscreen);

	if (!ri.GLimp_InitGraphics(fullscreen, pwidth, pheight))
	{
		return false;
	}

	/* the drawable size, as gl1 takes it (see the comment in gl1_main.c) */
	if (IsHighDPIaware && ((vid_fullscreen->value != 2) || (r_mode->value == -2)))
	{
		RI_GetDrawableSize(pwidth, pheight);
	}

	return true;
}

static qboolean
R_SetMode(void)
{
	int fullscreen = (int)vid_fullscreen->value;

	vid.width = r_customwidth->value;
	vid.height = r_customheight->value;

	if (SetMode_impl(&vid.width, &vid.height, r_mode->value, fullscreen))
	{
		rm_prev_mode = (r_mode->value == -1) ? 4 : (int)r_mode->value;
		return true;
	}

	if (r_mode->value == rm_prev_mode)
	{
		return false;
	}

	ri.Cvar_SetValue("r_mode", rm_prev_mode);
	r_mode->modified = false;

	if (!SetMode_impl(&vid.width, &vid.height, rm_prev_mode, 0))
	{
		R_Printf(PRINT_ALL, "ref_metal::R_SetMode() - could not revert to safe mode\n");
		return false;
	}

	return true;
}

static void
RM_Close(void)
{
	rmb_stats_t s;
	rm_ledger_t l;
	char line[RM_STATS_LINE];

	if (!rm_opened)
	{
		return;
	}

	rmb_close(&s, &l);
	s.noop_calls = rm_noop_calls;
	rm_opened = false;

	if (metal_stats && metal_stats->value && rm_stats_format(&s, &l, line, sizeof(line)))
	{
		R_Printf(PRINT_ALL, "%s\n", line);
	}

	rm_ledger_format(&l, line, sizeof(line));
	R_Printf(PRINT_ALL, "%s\n", line);
}

/* gl1's RI_BeginFrame clears these six together once R_TextureMode ran */
static void
RM_ClearTextureModeFlags(void)
{
	gl_texturemode->modified = false;
	gl_anisotropic->modified = false;
	gl_nolerp_list->modified = false;
	r_lerp_list->modified = false;
	r_2D_unfiltered->modified = false;
	r_videos_unfiltered->modified = false;
}

static qboolean
RI_Init(void)
{
	rmb_caps_t caps;
	char err[256], families[64];

	R_Printf(PRINT_ALL, "Refresh: " REF_VERSION "\n");
	R_Printf(PRINT_ALL, "Client: " YQ2VERSION "\n\n");

	Swap_Init();   /* LittleShort and the rest, which the loaders in files/ call (gl1_main.c) */
	R_Register();
	rm_noop_calls = 0;

	if (!rmb_open(&caps, err, sizeof(err)))
	{
		R_Printf(PRINT_ALL, "ref_metal: %s\n", err);
		return false;
	}

	rm_opened = true;

	/* the capability report, once per context */
	rm_caps_family_list(caps.families, families, sizeof(families));
	R_Printf(PRINT_ALL, "Metal device: %s\n", caps.device);
	R_Printf(PRINT_ALL, "Metal supportsFamily (apple5 to apple9): %s\n", families);
	R_Printf(PRINT_ALL, "Metal supportsRaytracing: %s, supportsRaytracingFromRender: %s\n",
			caps.raytracing ? "yes" : "no", caps.raytracing_render ? "yes" : "no");

	if (!rm_caps_floor_ok(caps.families))
	{
		R_Printf(PRINT_ALL, "ref_metal: the Metal device supports no Apple GPU family from apple%d on; ref_metal needs one\n",
				RMB_FAMILY_FLOOR);
		RM_Close();
		return false;
	}

	if (!R_SetMode())
	{
		R_Printf(PRINT_ALL, "ref_metal::%s - could not R_SetMode()\n", __func__);
		RI_ShutdownContext();
		RM_Close();
		return false;
	}

	R_InitImages();   /* runs R_TextureMode: the flags RI_BeginFrame tests start clear */
	RM_ClearTextureModeFlags();

	ri.Vid_MenuInit();
	return true;
}

static void
RI_Shutdown(void)
{
	ri.Cmd_RemoveCommand("screenshot");
	ri.Cmd_RemoveCommand("imagelist");
	RM_ShutdownDraw();
	R_ShutdownImages();
	RI_ShutdownContext();
	RM_Close();
}

static void
RI_BeginFrame(float camera_separation)
{
	int w = 0, h = 0;

	(void)camera_separation;   /* no stereo in ref_metal */

	if (r_vsync->modified)
	{
		r_vsync->modified = false;
		rmb_set_vsync(r_vsync->value != 0);
	}

	/*
	 * gl1's RI_BeginFrame: the sampler table follows the mode, and each
	 * image's sampler the lists and r_2D_unfiltered, between frames (gl1
	 * also tests r_videos_unfiltered, which RDraw_StretchRaw reads per call)
	 */
	if (gl_texturemode->modified || gl_anisotropic->modified
		|| gl_nolerp_list->modified || r_lerp_list->modified
		|| r_2D_unfiltered->modified || r_videos_unfiltered->modified)
	{
		R_TextureMode(gl_texturemode->string);
		RM_ClearTextureModeFlags();
	}

	/*
	 * gl1's R_Clear, the last step of its RI_BeginFrame: the colour clear
	 * with r_clear, for every frame, with or without a 3D view. Without
	 * r_clear gl1 clears no colour, so the frame starts on what the
	 * buffer holds; here that is the last frame (the target is kept),
	 * or black on a target no pass has written.
	 */
	RI_GetDrawableSize(&w, &h);
	rmb_frame_begin(w, h, r_clear->value ? rm_clear_color : NULL);
}

static void
RI_RenderFrame(refdef_t *fd)
{
	(void)fd;   /* nothing is drawn yet */

	if (r_norefresh->value)
	{
		return;
	}

	/*
	 * Colour loaded, so 2D drawn before a view (the player-config menu)
	 * stays. Depth to 1 and stencil to 0: gl1 clears depth in R_Clear,
	 * at the start of the frame, and no 2D draw writes depth before the
	 * view, so a clear here gives the same depth.
	 */
	rmb_pass_3d();
}

static qboolean
RI_EndWorldRenderpass(void)
{
	rmb_pass_2d();
	return true;
}

static void
RI_EndFrame(void)
{
	rmb_frame_end();
}

/* ---- not implemented yet: valid no-ops that count their calls ---------------- */

static void
RI_BeginRegistration(const char *map)
{
	(void)map;
	R_BeginImageRegistration();
	rm_noop_calls++;   /* the map is N4's */
}

static struct model_s *
RI_RegisterModel(const char *name)
{
	(void)name;
	rm_noop_calls++;
	return NULL;
}

static struct image_s *
RI_RegisterSkin(const char *name)
{
	(void)name;
	rm_noop_calls++;
	return NULL;
}

static void
RI_SetSky(const char *name, float rotate, vec3_t axis)
{
	(void)name; (void)rotate; (void)axis;
	rm_noop_calls++;
}

static void
RI_EndRegistration(void)
{
	R_FreeUnusedImages();
}

/* from gl1_draw.c: the pic is loaded and registered; drawing it is N3's */
static struct image_s *
RDraw_FindPic(const char *name)
{
	return R_FindPic(name, (findimage_t)R_FindImage);
}

/* from gl1_draw.c, which answers -1 -1 for a missing pic */
static void
RDraw_GetPicSize(int *w, int *h, const char *name)
{
	const image_t *image = R_FindPic(name, (findimage_t)R_FindImage);

	*w = image ? image->width : -1;
	*h = image ? image->height : -1;
}

static void
RDraw_PicScaled(int x, int y, const char *pic, float factor)
{
	(void)x; (void)y; (void)pic; (void)factor;
	rm_noop_calls++;
}

static void
RDraw_PicScaledCol(int x, int y, const char *pic, float factor, const float color[3])
{
	(void)x; (void)y; (void)pic; (void)factor; (void)color;
	rm_noop_calls++;
}

static void
RDraw_StretchPic(int x, int y, int w, int h, const char *name)
{
	(void)x; (void)y; (void)w; (void)h; (void)name;
	rm_noop_calls++;
}

static void
RDraw_CharScaled(int x, int y, int num, float scale)
{
	(void)x; (void)y; (void)num; (void)scale;
	rm_noop_calls++;
}

static void
RDraw_TileClear(int x, int y, int w, int h, const char *name)
{
	(void)x; (void)y; (void)w; (void)h; (void)name;
	rm_noop_calls++;
}


Q2_DLL_EXPORTED refexport_t
GetRefAPI(refimport_t imp)
{
	refexport_t re = {0};

	ri = imp;

	re.api_version = API_VERSION;
	re.framework_version = RI_GetSDLVersion();

	re.Init = RI_Init;
	re.Shutdown = RI_Shutdown;
	re.PrepareForWindow = RI_PrepareForWindow;
	re.InitContext = RI_InitContext;
	re.GetDrawableSize = RI_GetDrawableSize;
	re.ShutdownContext = RI_ShutdownContext;
	re.IsVSyncActive = RI_IsVSyncActive;
	re.BeginRegistration = RI_BeginRegistration;
	re.RegisterModel = RI_RegisterModel;
	re.RegisterSkin = RI_RegisterSkin;
	re.SetSky = RI_SetSky;
	re.EndRegistration = RI_EndRegistration;
	re.RenderFrame = RI_RenderFrame;
	re.DrawFindPic = RDraw_FindPic;
	re.DrawGetPicSize = RDraw_GetPicSize;
	re.DrawPicScaled = RDraw_PicScaled;
	re.DrawPicScaledCol = RDraw_PicScaledCol;
	re.DrawStretchPic = RDraw_StretchPic;
	re.DrawCharScaled = RDraw_CharScaled;
	re.DrawTileClear = RDraw_TileClear;
	re.DrawFill = RDraw_Fill;
	re.DrawFadeScreen = RDraw_FadeScreen;
	re.DrawStretchRaw = RDraw_StretchRaw;
	re.SetPalette = RI_SetPalette;
	re.BeginFrame = RI_BeginFrame;
	re.EndWorldRenderpass = RI_EndWorldRenderpass;
	re.EndFrame = RI_EndFrame;

	/* the new renderer restart API, as gl1 */
	ri.Vid_RequestRestart(RESTART_NO);

	return re;
}
