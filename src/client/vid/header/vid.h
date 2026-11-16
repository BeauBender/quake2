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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
 * USA.
 *
 * =======================================================================
 *
 * API between client and renderer.
 *
 * =======================================================================
 */

#ifndef CL_VID_H
#define CL_VID_H

#include "../../../common/header/common.h"

// FIXME: Remove it, it's unused.
typedef struct vrect_s {
	int				x,y,width,height;
} vrect_t;

// Hold the video state.
typedef struct {
	int height;
	int width;
} viddef_t;

// Global video state.
extern viddef_t viddef;

// Generic stuff.
qboolean VID_HasRenderer(const char *renderer);
void	VID_Init(void);
void	VID_Shutdown(void);
void	VID_CheckChanges(void);

/* pins_replay: a renderer-API trace fed to the loaded renderer */
qboolean VID_OpenReplay(const char *path);
int VID_ReplayFrame(void);
void VID_CloseReplay(void);

void	VID_MenuInit(void);
void	VID_MenuDraw(void);
const char *VID_MenuKey(int);

// Stuff provided by platform backend.
extern float glimp_refreshRate;

const char **GLimp_GetDisplayIndices(void);
int GLimp_GetWindowDisplayIndex(void);
int GLimp_GetNumVideoDisplays(void);
qboolean GLimp_Init(void);
void GLimp_Shutdown(void);
qboolean GLimp_InitGraphics(int fullscreen, int *pwidth, int *pheight);
void GLimp_ShutdownGraphics(void);
void GLimp_GrabInput(qboolean grab);
float GLimp_GetRefreshRate(void);
qboolean GLimp_GetDesktopMode(int *pwidth, int *pheight);
int GLimp_GetFrameworkVersion(void);

/* The high pixel density decisions of the window code (glimp_hidpi.c). */
qboolean GLimp_HighDPIWanted(float highdpiaware, const char *driver);
qboolean GLimp_DrawableInPixels(qboolean highdpi, int fullscreen, float r_mode);

/* uikit only: forces fullscreen 2 and r_mode -2 (the screen's own size), true when it did. */
qboolean GLimp_OwnScreenOnly(const char *driver, int *fullscreen, float *r_mode);

/* The screen a window was made for: width and height in pixels, the safe rectangle's x, y, width and height in points. */
#define GLIMP_SCREEN_INTS 6
qboolean GLimp_ScreenChanged(const int *made, const int *now);

/* Relative mouse mode requests: none to turn it on without a mouse on uikit; a failure is reported once per run of failures. */
qboolean GLimp_RelativeMouseWanted(const char *driver, qboolean have_mouse, qboolean grab);
qboolean GLimp_RelativeMouseReport(qboolean ok, qboolean *reported);

/* iOS only (glimp_sdl3.c): text input and the on-screen keyboard on or off; the window does not start it there. */
void GLimp_TextInput(qboolean on);
qboolean GLimp_TextInputActive(void);

/* iOS only (glimp_sdl3.c): the safe area or the pixel size moved; asks for a vid_restart when the screen is not the one the video was made for. */
void GLimp_ScreenEvent(void);

#endif
