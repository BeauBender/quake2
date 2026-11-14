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
 * (docs/plans/ref-metal-native.md). The frame is cleared and presented
 * and the 3D and 2D passes are opened (N1); the view is set up and the
 * world drawn as gl1's R_RenderView does, with the screen blend as the 3D
 * pass's last draw (N4, section 19). Entry points not implemented yet are
 * valid no-ops that count their calls. R_SetMode and SetMode_impl are
 * from gl1_main.c, without the MSAA retry (ref_metal has no MSAA).
 *
 * =======================================================================
 */

#include "header/local.h"
#include "common/mtl_matrix.h"

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
cvar_t *r_drawworld;
cvar_t *r_novis;
cvar_t *r_lockpvs;
cvar_t *r_cull;
cvar_t *r_fullbright;
cvar_t *r_fixsurfsky;
cvar_t *r_farsee;
cvar_t *gl_znear;
cvar_t *gl_polyblend;
cvar_t *gl1_waterwarp;
cvar_t *r_modulate;
cvar_t *gl1_minlight;
cvar_t *gl1_overbrightbits;
cvar_t *gl_lightmap;
cvar_t *gl1_dynamic;
cvar_t *gl1_flashblend;
cvar_t *r_lightlevel;

unsigned long rm_noop_calls;

/* the view of the current frame (R_SetupGL) */
static rm_view_t r_view;

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
	r_farsee = ri.Cvar_Get("r_farsee", "0", CVAR_LATCH | CVAR_ARCHIVE);
	r_fullbright = ri.Cvar_Get("r_fullbright", "0", 0);
	r_drawworld = ri.Cvar_Get("r_drawworld", "1", 0);
	r_novis = ri.Cvar_Get("r_novis", "0", 0);
	r_cull = ri.Cvar_Get("r_cull", "1", 0);
	gl_polyblend = ri.Cvar_Get("gl_polyblend", "1", 0);
	r_fixsurfsky = ri.Cvar_Get("r_fixsurfsky", "0", CVAR_ARCHIVE);
	gl_znear = ri.Cvar_Get("gl_znear", "4", CVAR_ARCHIVE);
	r_lockpvs = ri.Cvar_Get("r_lockpvs", "0", 0);
	gl1_waterwarp = ri.Cvar_Get("gl1_waterwarp", "1.0", CVAR_ARCHIVE);
	r_lightlevel = ri.Cvar_Get("r_lightlevel", "0", 0);
	gl1_overbrightbits = ri.Cvar_Get("gl1_overbrightbits", "0", CVAR_ARCHIVE);
	r_modulate = ri.Cvar_Get("r_modulate", "1", CVAR_ARCHIVE);
	gl_lightmap = ri.Cvar_Get("r_lightmap", "0", 0);
	gl1_dynamic = ri.Cvar_Get("gl1_dynamic", "1", 0);
	gl1_flashblend = ri.Cvar_Get("gl1_flashblend", "0", 0);
	gl1_minlight = ri.Cvar_Get("gl1_minlight", "0", CVAR_ARCHIVE);

	ri.Cmd_AddCommand("screenshot", R_ScreenShot);
	ri.Cmd_AddCommand("imagelist", R_ImageList_f);
	ri.Cmd_AddCommand("modellist", Mod_Modellist_f);
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
	R_InitLight();    /* gl1's minlight table, which gl1 makes in R_InitImages */
	Mod_Init();
	R_InitBuiltinTextures();
	RM_InitDraw();

	ri.Vid_MenuInit();
	return true;
}

static void
RI_Shutdown(void)
{
	ri.Cmd_RemoveCommand("screenshot");
	ri.Cmd_RemoveCommand("imagelist");
	ri.Cmd_RemoveCommand("modellist");
	Mod_FreeAll();
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

	/* gl1's RI_BeginFrame: gl1_overbrightbits is 0, 1, 2 or 4 */
	if (gl1_overbrightbits->modified)
	{
		ri.Cvar_SetValue("gl1_overbrightbits", rm_overbright(gl1_overbrightbits->value));
		gl1_overbrightbits->modified = false;
	}

	/*
	 * gl1's R_Clear, the last step of its RI_BeginFrame: the colour clear
	 * with r_clear, for every frame, with or without a 3D view. Without
	 * r_clear gl1 clears no colour, so the frame starts on what the
	 * buffer holds, which GL leaves undefined; here it starts black, as
	 * on the shim (the reference), which clears its first pass of a
	 * frame to black (TASK87: the cracks of the world show it).
	 */
	RI_GetDrawableSize(&w, &h);
	rmb_frame_begin(w, h, r_clear->value ? rm_clear_color : NULL);
}

/* from gl1_main.c */
static void
R_SetupFrame(void)
{
	mleaf_t *leaf;

	r_framecount++;

	/* build the transformation matrix for the given view angles */
	VectorCopy(r_newrefdef.vieworg, r_origin);

	AngleVectors(r_newrefdef.viewangles, vpn, vright, vup);

	/* current viewcluster */
	if (!(r_newrefdef.rdflags & RDF_NOWORLDMODEL))
	{
		if (!r_worldmodel)
		{
			ri.Sys_Error(ERR_DROP, "%s: bad world model", __func__);
			return;
		}

		r_oldviewcluster = r_viewcluster;
		r_oldviewcluster2 = r_viewcluster2;
		leaf = Mod_PointInLeaf(r_origin, r_worldmodel->nodes);
		r_viewcluster = r_viewcluster2 = leaf->cluster;

		/* check above and below so crossing solid water doesn't draw wrong */
		if (!leaf->contents)
		{
			/* look down a bit */
			vec3_t temp;

			VectorCopy(r_origin, temp);
			temp[2] -= 16;
			leaf = Mod_PointInLeaf(temp, r_worldmodel->nodes);

			if (!(leaf->contents & CONTENTS_SOLID) && (leaf->cluster != r_viewcluster2))
			{
				r_viewcluster2 = leaf->cluster;
			}
		}
		else
		{
			/* look up a bit */
			vec3_t temp;

			VectorCopy(r_origin, temp);
			temp[2] += 16;
			leaf = Mod_PointInLeaf(temp, r_worldmodel->nodes);

			if (!(leaf->contents & CONTENTS_SOLID) && (leaf->cluster != r_viewcluster2))
			{
				r_viewcluster2 = leaf->cluster;
			}
		}
	}

	/* the clear of the RDF_NOWORLDMODEL rectangle (the player model of the menu) is N7's */
}

/*
 * from gl1_main.c, R_SetupGL and R_SetPerspective: the viewport, the
 * projection and the modelview as the shim computes them (common/
 * mtl_matrix.c), and their product as the shim's draws take it
 * (mgl_mat_mul(projection, modelview)). Culling of GL's front faces with
 * r_cull, depth LEQUAL (gl1's R_Clear), no blending: the back end's 3D
 * draws.
 */
static void
R_SetupGL(void)
{
	int tw = 0, th = 0;

	rmb_target_size(&tw, &th);
	rm_view_rect(r_newrefdef.x, r_newrefdef.y, r_newrefdef.width, r_newrefdef.height,
			vid.width, vid.height, th, &r_view.vp);

	if (!rm_projection(r_view.proj, r_newrefdef.fov_y, r_newrefdef.width, r_newrefdef.height,
			gl_znear->value, r_farsee->value != 0, (r_newrefdef.rdflags & RDF_UNDERWATER) != 0,
			r_newrefdef.time, gl1_waterwarp->value))
	{
		mtl_mat_identity(r_view.proj);   /* glFrustum refused: the identity glLoadIdentity left */
	}

	rm_view_matrix(r_view.world, r_newrefdef.viewangles, r_newrefdef.vieworg);
	mtl_mat_mul(r_view.mvp, r_view.proj, r_view.world);
	r_view.cull = r_cull->value != 0;
}

/* from gl1_main.c: the screen blend, the 3D pass's last draw (design section 11) */
static void
R_PolyBlend(void)
{
	static const float vtx[] = {
		10, 100, 100,
		10, -100, 100,
		10, -100, -100,
		10, 100, -100
	};
	float rgba[4], mv[16], mvp[16];

	if (!rm_polyblend(r_newrefdef.blend, rm_sw_gamma, gl_polyblend->value, rgba))
	{
		return;
	}

	rm_blend_matrix(mv);
	mtl_mat_mul(mvp, r_view.proj, mv);
	rmb_draw_color(vtx, 4, rgba, mvp, &r_view.vp, r_cull->value != 0);
}

/*
 * from gl1_main.c; entities (N7), the light of dynamic lights (N8: N5 marks
 * the surfaces they touch and counts them) and particles (N9) are not
 * drawn yet
 */
static void
R_RenderView(refdef_t *fd)
{
	r_newrefdef = *fd;

	if (!r_worldmodel && !(r_newrefdef.rdflags & RDF_NOWORLDMODEL))
	{
		ri.Sys_Error(ERR_DROP, "%s: NULL worldmodel", __func__);
	}

	RM_WorldSkipsClear();
	R_PushDlights();
	R_SetupFrame();
	R_SetFrustum(vup, vpn, vright, r_origin, r_newrefdef.fov_x, r_newrefdef.fov_y, frustum);
	R_SetupGL();

	if (!(r_newrefdef.rdflags & RDF_NOWORLDMODEL))
	{
		R_MarkLeaves(); /* done here so we know if we're in water */
		R_DrawWorld(&r_view);
		/* the entities (N7) and the particles (N9) are drawn here, before the translucent surfaces */
		R_DrawAlphaSurfaces(&r_view);
	}

	R_PolyBlend();
}

static void
RI_RenderFrame(refdef_t *fd)
{
	RM_Flush2D();   /* menu rendering when needed */

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
	R_RenderView(fd);
	R_SetLightLevel(NULL);
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
	RM_Flush2D();   /* to draw buffered 2D text */
	rmb_frame_end();
}

/* ---- not implemented yet: valid no-ops that count their calls ---------------- */

static struct image_s *
RI_RegisterSkin(const char *name)
{
	(void)name;
	rm_noop_calls++;
	return NULL;
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
	re.SetSky = RM_SetSky;
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
