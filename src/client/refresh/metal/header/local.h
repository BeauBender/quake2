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
 * The front end of ref_metal: its globals and the functions its C files
 * share (docs/plans/ref-metal-native.md, section 2). No graphics API type
 * and no graphics API header; the back end is reached through rmb.h.
 *
 * =======================================================================
 */

#ifndef RM_LOCAL_H
#define RM_LOCAL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../ref_shared.h"
#include "rmb.h"

#define REF_VERSION DISPLAYNAME " Metal Refresher"

/* the cvars, by gl1's names where gl1 has them */
extern cvar_t *r_clear;
extern cvar_t *r_mode;
extern cvar_t *r_customwidth;
extern cvar_t *r_customheight;
extern cvar_t *r_norefresh;
extern cvar_t *r_vsync;
extern cvar_t *vid_fullscreen;
extern cvar_t *metal_stats;

/* calls to entry points that do nothing yet (rm_main.c) */
extern unsigned long rm_noop_calls;

/* rm_sdl.c: the window seat */
extern qboolean IsHighDPIaware;
int RI_PrepareForWindow(void);
int RI_InitContext(void *win);
void RI_GetDrawableSize(int *width, int *height);
void RI_ShutdownContext(void);
qboolean RI_IsVSyncActive(void);
int RI_GetSDLVersion(void);

/* rm_misc.c */
void R_ScreenShot(void);
void RM_PrintStats(void);

#endif /* RM_LOCAL_H */
