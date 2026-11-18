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
 * pass's last draw (N4, section 19); the entities (N7, section 22) with
 * the sprites, beams and null models, the flash blobs and the stencil
 * clear of the shadows (N8, section 23). Entry points not implemented yet are
 * valid no-ops that count their calls. R_SetMode and SetMode_impl are
 * from gl1_main.c, without the MSAA retry (ref_metal has no MSAA).
 *
 * =======================================================================
 */

#include <TargetConditionals.h>

#include "header/local.h"
#include "header/rm_rtscene.h"
#include "header/rm_rtaoscene.h"
#include "header/rm_rtsunscene.h"
#include "header/rm_rtwaterscene.h"
#include "header/rm_gbufscene.h"
#include "header/rm_fxscene.h"
#include "common/mtl_matrix.h"
#include "common/mtl_rate.h"
#include <time.h>

refimport_t ri;

cvar_t *r_clear;
cvar_t *r_mode;
cvar_t *r_customwidth;
cvar_t *r_customheight;
cvar_t *r_norefresh;
cvar_t *r_vsync;
cvar_t *vid_fullscreen;
cvar_t *metal_stats;
static cvar_t *metal_rate_start;   /* seconds after the first frame ended at which the presented-rate window begins (TASK161) */
static mtl_rate_t rm_rate;
static double rm_rate_start;       /* metal_rate_start, read once in RI_Init, as the shim reads it (TASK161) */
static unsigned long rm_rate_frames;   /* the frames count at the last sample: a frame skipped is not a sample */
cvar_t *metal_lightmap_filter;
cvar_t *gl_texturemode;
cvar_t *gl_anisotropic;
cvar_t *gl_nolerp_list;
cvar_t *r_lerp_list;
cvar_t *r_2D_unfiltered;
cvar_t *r_videos_unfiltered;
cvar_t *r_scale8bittextures;
cvar_t *metal_colorspace;
cvar_t *metal_2d_sharp;
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
cvar_t *r_drawentities;
cvar_t *r_lerpmodels;
cvar_t *gl_lefthand;
cvar_t *r_gunfov;
cvar_t *gl_shadows;
cvar_t *gl1_stencilshadow;
cvar_t *gl1_particle_min_size;
cvar_t *gl1_particle_max_size;
cvar_t *gl1_particle_size;
cvar_t *gl1_particle_att_a;
cvar_t *gl1_particle_att_b;
cvar_t *gl1_particle_att_c;
cvar_t *gl1_particle_square;
cvar_t *metal_particle_cap;
cvar_t *gl1_pointparameters;

unsigned long rm_noop_calls;

/* the entities of the current view not drawn, by class (metal_stats) */
static unsigned long skip_sprite, skip_beam, skip_null, skip_btrans, skip_ent;

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
	metal_rate_start = ri.Cvar_Get("metal_rate_start", "1", 0);
	metal_lightmap_filter = ri.Cvar_Get("metal_lightmap_filter", "0", CVAR_ARCHIVE);

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
	r_scale8bittextures = ri.Cvar_Get("r_scale8bittextures", "0", CVAR_ARCHIVE);
	metal_2d_sharp = ri.Cvar_Get("metal_2d_sharp", "0", CVAR_ARCHIVE);
	metal_colorspace = ri.Cvar_Get("metal_colorspace", "0", CVAR_ARCHIVE);
	rmb_colorspace_set(rm_colorspace_mode(metal_colorspace->value));   /* applied when the layer is attached */
	metal_colorspace->modified = false;
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
	r_drawentities = ri.Cvar_Get("r_drawentities", "1", 0);
	r_lerpmodels = ri.Cvar_Get("r_lerpmodels", "1", 0);
	gl_lefthand = ri.Cvar_Get("hand", "0", CVAR_USERINFO | CVAR_ARCHIVE);
	r_gunfov = ri.Cvar_Get("r_gunfov", "80", CVAR_ARCHIVE);
	gl_shadows = ri.Cvar_Get("r_shadows", "0", CVAR_ARCHIVE);
	gl1_stencilshadow = ri.Cvar_Get("gl1_stencilshadow", "0", CVAR_ARCHIVE);
	gl1_particle_min_size = ri.Cvar_Get("gl1_particle_min_size", "2", CVAR_ARCHIVE);
	gl1_particle_max_size = ri.Cvar_Get("gl1_particle_max_size", "40", CVAR_ARCHIVE);
	gl1_particle_size = ri.Cvar_Get("gl1_particle_size", "40", CVAR_ARCHIVE);
	gl1_particle_att_a = ri.Cvar_Get("gl1_particle_att_a", "0.01", CVAR_ARCHIVE);
	gl1_particle_att_b = ri.Cvar_Get("gl1_particle_att_b", "0.0", CVAR_ARCHIVE);
	gl1_particle_att_c = ri.Cvar_Get("gl1_particle_att_c", "0.01", CVAR_ARCHIVE);
	gl1_particle_square = ri.Cvar_Get("gl1_particle_square", "0", CVAR_ARCHIVE);
	metal_particle_cap = ri.Cvar_Get("metal_particle_cap", "0", CVAR_ARCHIVE);
	gl1_pointparameters = ri.Cvar_Get("gl1_pointparameters", "1", CVAR_ARCHIVE);

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

	/* The drawable size where the client takes it for viddef: the
	   client's rule, GLimp_DrawableInPixels() (glimp_hidpi.c, linked
	   in), on the inputs the client and soft give it. vid_fullscreen
	   holds the value the window was just made with: GLimp_InitGraphics()
	   writes the value passed into it (0 in the fallback below, 2 for a
	   cvar of 2.5), and 0 when it fell back to a window on its own.
	   tests/check-mode-size.sh keeps this call equal to soft's. */
	if (GLimp_DrawableInPixels(IsHighDPIaware, (int)vid_fullscreen->value, r_mode->value))
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

	/* the presented rate over a window (TASK161), beside the totals */
	if (metal_stats && metal_stats->value && rm_rate.seen &&
		mtl_rate_format("ref_metal: metal_rate:", &rm_rate, rm_rate_start, s.present_held,
			s.no_drawable, line, sizeof(line)))
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

	/* before the first command buffer: its completion handler is this image's code */
	if (!RM_KeepMapped())
	{
		R_Printf(PRINT_ALL, "ref_metal: cannot keep the module mapped (RTLD_NODELETE)\n");
	}

	if (!rmb_open(&caps, err, sizeof(err)))
	{
		R_Printf(PRINT_ALL, "ref_metal: %s\n", err);
		return false;
	}

	rm_opened = true;
	mtl_rate_reset(&rm_rate);
	rm_rate_start = metal_rate_start->value;
	rm_rate_frames = 0;

	/* the capability report, once per context */
	rm_caps_family_list(caps.families, families, sizeof(families));
	R_Printf(PRINT_ALL, "Metal device: %s\n", caps.device);
	R_Printf(PRINT_ALL, "Metal supportsFamily (apple5 to apple9): %s\n", families);
	R_Printf(PRINT_ALL, "Metal supportsRaytracing: %s, supportsRaytracingFromRender: %s\n",
			caps.raytracing ? "yes" : "no", caps.raytracing_render ? "yes" : "no");

	if (!rm_caps_floor_ok(caps.families))
	{
#if defined(TARGET_OS_SIMULATOR) && TARGET_OS_SIMULATOR
		/* the iOS, tvOS and visionOS simulators report no Apple GPU
		   family; the floor is the device's, checked on the device */
		R_Printf(PRINT_ALL, "ref_metal: the device floor (apple%d) is not checked in the simulator\n",
				RMB_FAMILY_FLOOR);
#else
		R_Printf(PRINT_ALL, "ref_metal: the Metal device supports no Apple GPU family from apple%d on; ref_metal needs one\n",
				RMB_FAMILY_FLOOR);
		RM_Close();
		return false;
#endif
	}

	if (!R_SetMode())
	{
		R_Printf(PRINT_ALL, "ref_metal::%s - could not R_SetMode()\n", __func__);
		RI_ShutdownContext();
		RM_Close();
		return false;
	}

	R_InitParticles();    /* gl1's R_SetDefaultState: the point state, gl1_pointparameters taken */
	R_InitImages();   /* runs R_TextureMode: the flags RI_BeginFrame tests start clear */
	RM_ClearTextureModeFlags();
	R_InitLight();    /* gl1's minlight table, which gl1 makes in R_InitImages */
	Mod_Init();
	R_InitBuiltinTextures();
	RM_InitDraw();
	RM_RT_Init(&caps);   /* the r_rt_* cvars (TASK53); builds nothing */
	RM_GB_Init();        /* the G-buffer's development cvars (TASK54) */
	RM_FX_Init(&caps);   /* the MetalFX cvars and what the device supports (TASK111); makes no scaler */

	ri.Vid_MenuInit();
	return true;
}

static void
RI_Shutdown(void)
{
	RM_RT_Shutdown();   /* the ray-tracing objects, before the models and the device */
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
	int w = 0, h = 0, dw = 0, dh = 0;

	(void)camera_separation;   /* no stereo in ref_metal */

	/* the layer's displaySyncEnabled, read back by IsVSyncActive (gl1 sets the swap interval) */
	if (r_vsync->modified)
	{
		r_vsync->modified = false;
		rmb_set_vsync(r_vsync->value != 0);
		R_Printf(PRINT_DEVELOPER, "ref_metal: displaySyncEnabled %s\n", rmb_vsync() ? "on" : "off");
	}

	/* metal_colorspace (TASK121): the layer's colour space, between frames */
	if (metal_colorspace->modified)
	{
		metal_colorspace->modified = false;
		rmb_colorspace_set(rm_colorspace_mode(metal_colorspace->value));
	}

	/* gl1's RI_BeginFrame: vid_gamma takes effect in R_InitImages, so through vid_restart */
	if (vid_gamma->modified)
	{
		vid_gamma->modified = false;
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

	/* gl1's RI_BeginFrame: GL_POINT_SMOOTH, or the particle texture without point parameters */
	if (gl1_particle_square->modified)
	{
		R_ParticleSquare();
		gl1_particle_square->modified = false;
	}

	/*
	 * gl1's R_Clear, the last step of its RI_BeginFrame: the colour clear
	 * with r_clear, for every frame, with or without a 3D view. Without
	 * r_clear gl1 clears no colour, so the frame starts on what the
	 * buffer holds, which GL leaves undefined; here it starts black, as
	 * on the shim (the reference), which clears its first pass of a
	 * frame to black (TASK87: the cracks of the world show it).
	 */
#if defined(__APPLE__) && TARGET_OS_IPHONE
	/*
	 * TASK172: the window follows the screen (a rotation, a fold or an
	 * unfold of an iPhone Duo) with no vid_restart. The frame is the safe
	 * rectangle of the window now, the size the client's viddef took
	 * from RI_GetDrawableSize too (GLimp_ScreenEvent), and
	 * rmb_frame_begin remakes the targets and the drawable for it. A
	 * window with no pixels keeps the last size.
	 */
	{
		int sw, sh;

		RI_GetDrawableSize(&sw, &sh);

		if ((sw > 0) && (sh > 0) && ((sw != vid.width) || (sh != vid.height)))
		{
			vid.width = sw;
			vid.height = sh;
		}
	}
#endif

	/* the mode's size, as gl1 draws and reads it; the window's pixels for the drawable (FIX45) */
	rm_frame_size(vid.width, vid.height, &w, &h);
	{
		/* the layer is the window's pixels, the frame goes into its safe rectangle (iOS; no inset elsewhere) */
		rm_inset_t in;

		RI_GetLayer(&dw, &dh, &in);
		rmb_present_inset_set(&in);
	}
	rmb_frame_begin(w, h, dw, dh, r_clear->value ? rm_clear_color : NULL);
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

}

/*
 * gl1's clear of the RDF_NOWORLDMODEL rectangle (the player model of the
 * menu, R_SetupFrame): the colour 0.3 grey inside the view, drawn as one
 * quad over the viewport without a depth test; the 3D pass cleared the
 * depth when it opened (rmb_pass_3d) and nothing drew before the view.
 */
static void
R_ClearNoWorld(void)
{
	static const float quad[] = {
		-1, -1, 0,
		1, -1, 0,
		1, 1, 0,
		-1, 1, 0
	};
	static const float grey[4] = { 0.3f, 0.3f, 0.3f, 1.0f };
	float id[16];

	if (!(r_newrefdef.rdflags & RDF_NOWORLDMODEL))
	{
		return;
	}

	mtl_mat_identity(id);
	rmb_draw_color(quad, 4, grey, id, &r_view.vp, 0);
}

void
RM_EntitySkips(rmb_stats_t *s)
{
	s->sprite_skips = skip_sprite;
	s->beam_skips = skip_beam;
	s->null_skips = skip_null;
	s->bmodel_trans_skips = skip_btrans;
	s->ent_skips = skip_ent;
}

/*
 * one entity of the list, in the pass whose depth writes are
 * *depth_write: a beam gl1 draws ends with glDepthMask(GL_TRUE), so the
 * translucent entities after it write depth (gl1's state, kept)
 */
static void
R_DrawEntity(const entity_t *currententity, int *depth_write)
{
	const model_t *currentmodel = currententity->model;

	if (currententity->flags & RF_BEAM)
	{
		int drawn;

		if (!R_DrawBeam(currententity, &r_view, &drawn))
		{
			skip_beam++;
		}

		if (drawn)
		{
			*depth_write = 1;
		}

		return;
	}

	if (!currentmodel)
	{
		if (!R_DrawNullModel(currententity, &r_view, *depth_write))
		{
			skip_null++;
		}

		return;
	}

	switch (currentmodel->type)
	{
		case mod_alias:
			if (!R_DrawAliasModel(currententity, currentmodel, &r_view, *depth_write))
			{
				skip_ent++;
			}

			break;
		case mod_brush:
			/* gl1's translucent brush entity depends on its texture units' history; none in stock content (section 22) */
			if (currententity->flags & RF_TRANSLUCENT)
			{
				skip_btrans++;
			}
			else if (!R_DrawBrushModel(currententity, currentmodel, &r_view))
			{
				skip_ent++;
			}

			break;
		case mod_sprite:
			if (!R_DrawSpriteModel(currententity, currentmodel, &r_view, *depth_write))
			{
				skip_sprite++;
			}

			break;
		default:
			ri.Sys_Error(ERR_DROP, "Bad modeltype");
			break;
	}
}

/*
 * from gl1_main.c: the opaque entities, then the translucent ones without
 * depth writes (gl1's glDepthMask(GL_FALSE)) until a beam turns them on
 * again, each in list order
 */
static void
R_DrawEntitiesOnList(void)
{
	int i, n, depth_write;

	if (!r_drawentities->value)
	{
		return;
	}

	n = rm_entity_count(r_newrefdef.num_entities);

	/* draw non-transparent first */
	for (i = 0; i < n; i++)
	{
		if (!(r_newrefdef.entities[i].flags & RF_TRANSLUCENT))
		{
			depth_write = 1;
			R_DrawEntity(&r_newrefdef.entities[i], &depth_write);
		}
	}

	/* draw transparent entities; we could sort these if it ever becomes a problem... */
	depth_write = 0;

	for (i = 0; i < n; i++)
	{
		if (r_newrefdef.entities[i].flags & RF_TRANSLUCENT)
		{
			R_DrawEntity(&r_newrefdef.entities[i], &depth_write);
		}
	}
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
 * from gl1_main.c, R_RenderView up to its draws. The dynamic lights are
 * the renderer's checked copy from here on (R_TakeDlights)
 */
static void
R_RenderViewSetup(refdef_t *fd)
{
	r_newrefdef = *fd;
	R_TakeDlights();

	if (!r_worldmodel && !(r_newrefdef.rdflags & RDF_NOWORLDMODEL))
	{
		ri.Sys_Error(ERR_DROP, "%s: NULL worldmodel", __func__);
	}

	RM_WorldSkipsClear();
	skip_sprite = skip_beam = skip_null = skip_btrans = skip_ent = 0;
	R_PushDlights();
	R_SetupFrame();
	R_SetFrustum(vup, vpn, vright, r_origin, r_newrefdef.fov_x, r_newrefdef.fov_y, frustum);
	R_SetupGL();
}

/* from gl1_main.c, R_RenderView's draws */
static void
R_RenderViewDraw(void)
{
	R_ClearNoWorld();

	if (!(r_newrefdef.rdflags & RDF_NOWORLDMODEL))
	{
		R_MarkLeaves(); /* done here so we know if we're in water */
		R_DrawWorld(&r_view);
	}

	R_DrawEntitiesOnList();

	/* gl1_flashblend's blobs, then the particles (N9), with or without a world (as gl1) */
	R_RenderDlights(&r_view);
	R_DrawParticles(&r_view);

	if (!(r_newrefdef.rdflags & RDF_NOWORLDMODEL))
	{
		R_DrawAlphaSurfaces(&r_view);
	}

	R_PolyBlend();
}

static void
R_RenderView(refdef_t *fd)
{
	R_RenderViewSetup(fd);
	R_RenderViewDraw();
}

/*
 * The passes of a GBUFFER view that draw an entity (RT2, TASK54;
 * rm_gb_entity_split): the raster the opaque alias and brush models but
 * the view weapon, a beam and a shell, the forward pass over the resolved
 * image every other one and the shadows of the raster's alias models.
 */
static int
R_EntitySplit(const entity_t *e)
{
	const model_t *m = e->model;
	int kind = RM_GB_KIND_NONE;

	if (m)
	{
		kind = (m->type == mod_alias) ? RM_GB_KIND_ALIAS : (m->type == mod_brush) ? RM_GB_KIND_BRUSH : RM_GB_KIND_OTHER;
	}

	return rm_gb_entity_split(e->flags, kind, gl_shadows->value);
}

/*
 * R_DrawEntitiesOnList split at the resolve: raster 1 draws the raster's
 * entities (an alias model without its shadow), raster 0 the forward
 * pass's: the other opaque ones and the shadows of the raster's alias
 * models, then the translucent ones as R_DrawEntitiesOnList, each in
 * list order. The stock order holds within each pass only: every
 * rastered model is drawn before any draw of the forward pass, so a beam
 * or a stencil shadow comes after all of them.
 */
static void
R_DrawEntitiesSplit(int raster)
{
	int i, n, depth_write;

	if (!r_drawentities->value)
	{
		return;
	}

	n = rm_entity_count(r_newrefdef.num_entities);

	for (i = 0; i < n; i++)
	{
		const entity_t *e = &r_newrefdef.entities[i];
		int split;

		if (e->flags & RF_TRANSLUCENT)
		{
			continue;
		}

		depth_write = 1;
		split = R_EntitySplit(e);

		if (raster ? (split & RM_GB_ENT_RASTER) : (split & RM_GB_ENT_FORWARD))
		{
			R_AliasPassMode(raster ? RM_ALIAS_MODEL : RM_ALIAS_ALL);
			R_DrawEntity(e, &depth_write);
		}
		else if (!raster && (split & RM_GB_ENT_SHADOW))
		{
			R_AliasPassMode(RM_ALIAS_SHADOW);
			R_DrawEntity(e, &depth_write);
		}
	}

	R_AliasPassMode(RM_ALIAS_ALL);

	if (raster)
	{
		return;
	}

	depth_write = 0;

	for (i = 0; i < n; i++)
	{
		if (r_newrefdef.entities[i].flags & RF_TRANSLUCENT)
		{
			R_DrawEntity(&r_newrefdef.entities[i], &depth_write);
		}
	}
}

/*
 * A world view on the MOTION or GBUFFER path (RT2, TASK54; design
 * section 3): the G-buffer raster of the world and the raster's
 * entities, the lighting resolve, then the forward pass of the other
 * entities, the flash blobs, the particles, the translucent surfaces and
 * the screen blend over the resolved image; MOTION draws the stock view
 * into a pass with the motion attachment. Where the view cannot be
 * opened it is the stock one.
 */
static void
R_RenderViewGB(refdef_t *fd, int stencil)
{
	R_RenderViewSetup(fd);

	if (!RM_GB_Begin(&r_view, stencil, r_clear->value ? rm_clear_color : NULL))
	{
		r_dlight_term = true;   /* the stock view: gl1's dynamic lights in the lightmaps (RT3) */
		rmb_pass_3d(stencil);
		R_RenderViewDraw();
		return;
	}

	if (RM_GB_Path() == RM_GB_PATH_MOTION)
	{
		R_RenderViewDraw();
		RM_GB_End();
		return;
	}

	/* RT3: the view and lights of the shadow rays, traced at the resolve; with them the dynamic lights leave the lightmaps */
	RM_RTS_View(&r_view);
	RM_RTAO_View(&r_view);   /* RT5: the view of the AO rays, traced at the resolve */
	RM_RTSun_View(&r_view);  /* RT4: the view of the sun rays, traced at the resolve after the AO */
	RM_RTW_View(&r_view);    /* RT6: the view of the reflections, traced after the resolve */
	R_MarkLeaves();
	R_DrawWorld(&r_view);
	R_DrawEntitiesSplit(1);
	RM_GB_Resolve();
	r_dlight_term = true;   /* nothing after the resolve is resolved: a lightmap built from here keeps gl1's term */

	/* r_rt_shadows_show, r_rt_ao_show, r_rt_sun_show and r_rt_water_show draw their pass's view alone, the forward pass left out */
	if (!RM_RTS_ShowOnly() && !RM_RTAO_ShowOnly() && !RM_RTSun_ShowOnly() && !RM_RTW_ShowOnly())
	{
		R_DrawEntitiesSplit(0);
		R_RenderDlights(&r_view);
		R_DrawParticles(&r_view);
		R_DrawAlphaSurfaces(&r_view);
		R_PolyBlend();
	}

	RM_GB_End();
}

static void
RI_RenderFrame(refdef_t *fd)
{
	RM_Flush2D();   /* menu rendering when needed */

	if (r_norefresh->value)
	{
		return;
	}

	/* the acceleration structures of the view, before the raster (design section 3, step 1; TASK53) */
	RM_RT_Frame(fd);

	/* whether this frame traces RT3's shadows: gl1's dynamic-light term leaves the lightmaps when it does */
	RM_RTS_Frame(fd, RM_RT_SceneReady());

	/* whether it traces RT5's ambient occlusion */
	RM_RTAO_Frame(fd, RM_RT_SceneReady());

	/* whether it traces RT4's sun and sky shadow (never on a map under 2 % sky) */
	RM_RTSun_Frame(fd, RM_RT_SceneReady());

	/* whether it traces RT6's water reflections */
	RM_RTW_Frame(fd, RM_RT_SceneReady());

	/*
	 * Colour loaded, so 2D drawn before a view (the player-config menu)
	 * stays. Depth to 1, and stencil to 1 with stencil shadows, else 0:
	 * gl1 clears them in R_Clear, at the start of the frame (the stencil
	 * with r_shadows and gl1_stencilshadow, the context having 8 stencil
	 * bits), and no 2D draw writes depth or stencil before the view, so a
	 * clear here gives the same.
	 */
	if (RM_GB_Choose(fd) != RM_GB_PATH_FORWARD)
	{
		R_RenderViewGB(fd, (gl_shadows->value && gl1_stencilshadow->value) ? 1 : 0);
		R_SetLightLevel(NULL);
		return;
	}

	rmb_pass_3d((gl_shadows->value && gl1_stencilshadow->value) ? 1 : 0);
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
	RM_FX_EndFrame();
	rmb_frame_end();

	/* with metal_stats on, each frame ended is a sample of the presented-rate window (TASK161) */
	if (metal_stats && metal_stats->value)
	{
		unsigned long frames, no_drawable;

		/* only the two counts the rate needs are read; a frame skipped did not count, and is not a sample */
		rmb_rate_counts(&frames, &no_drawable);

		if (frames != rm_rate_frames)
		{
			rm_rate_frames = frames;
			mtl_rate_frame(&rm_rate, clock_gettime_nsec_np(CLOCK_UPTIME_RAW), frames - no_drawable,
				rm_rate_start);
		}
	}
}

/* from gl1_image.c */
static struct image_s *
RI_RegisterSkin(const char *name)
{
	return R_FindImage(name, it_skin);
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
