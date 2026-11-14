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

	ri.Cmd_AddCommand("screenshot", R_ScreenShot);
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
	char line[256];

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

static qboolean
RI_Init(void)
{
	rmb_caps_t caps;
	char err[256], families[64];

	R_Printf(PRINT_ALL, "Refresh: " REF_VERSION "\n");
	R_Printf(PRINT_ALL, "Client: " YQ2VERSION "\n\n");

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

	ri.Vid_MenuInit();
	return true;
}

static void
RI_Shutdown(void)
{
	ri.Cmd_RemoveCommand("screenshot");
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

	RI_GetDrawableSize(&w, &h);
	rmb_frame_begin(w, h);
}

static void
RI_RenderFrame(refdef_t *fd)
{
	(void)fd;   /* nothing is drawn yet */

	if (r_norefresh->value)
	{
		return;
	}

	/* the clear of gl1's R_Clear: colour with r_clear, depth to 1 */
	rmb_pass_3d(r_clear->value ? rm_clear_color : NULL);
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
	rm_noop_calls++;
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
	rm_noop_calls++;
}

static struct image_s *
RDraw_FindPic(const char *name)
{
	(void)name;
	rm_noop_calls++;
	return NULL;
}

/* ref.h: "will return 0 0 if not found", and nothing is found yet */
static void
RDraw_GetPicSize(int *w, int *h, const char *name)
{
	(void)name;
	rm_noop_calls++;

	if (w)
	{
		*w = 0;
	}

	if (h)
	{
		*h = 0;
	}
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

static void
RDraw_Fill(int x, int y, int w, int h, int c)
{
	(void)x; (void)y; (void)w; (void)h; (void)c;
	rm_noop_calls++;
}

static void
RDraw_FadeScreen(void)
{
	rm_noop_calls++;
}

static void
RDraw_StretchRaw(int x, int y, int w, int h, int cols, int rows, const byte *data, int bits)
{
	(void)x; (void)y; (void)w; (void)h; (void)cols; (void)rows; (void)data; (void)bits;
	rm_noop_calls++;
}

static void
RI_SetPalette(const unsigned char *palette)
{
	(void)palette;
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
