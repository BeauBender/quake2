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
 * What the video menu shows for each renderer (TASK112): which items
 * apply, the reason of those that do not, and the cvar each one sets.
 * Pure functions of the renderer's name, the mask of the MetalFX modes
 * the GPU has (r_metalfx_modes) and the scaler chosen in the menu, so
 * that the unit tests run them. An item that does not apply is greyed
 * with its reason, never hidden. The mask is advisory: the renderer
 * stays the authority on what it runs.
 *
 * =======================================================================
 */

#ifndef CL_MENU_VIDRULES_H
#define CL_MENU_VIDRULES_H

typedef enum
{
	VIDREF_UNKNOWN,
	VIDREF_GL1,
	VIDREF_GL3,
	VIDREF_GL4,
	VIDREF_VK,
	VIDREF_SOFT,
	VIDREF_METAL
} vidref_t;

/* The renderer-specific items of the video menu. */
typedef enum
{
	VIDITEM_INTENSITY,
	VIDITEM_OVERBRIGHT,
	VIDITEM_MINLIGHT,
	VIDITEM_COLORLIGHT,
	VIDITEM_BLOOM,
	VIDITEM_DYNLIGHT,
	VIDITEM_MSAA,
	VIDITEM_FILTER,
	VIDITEM_MFX_SCALER,
	VIDITEM_MFX_SCALE,
	VIDITEM_MFX_INTERP,
	VIDITEM_DENOISER,
	VIDITEM_AO_SCALE,
	VIDITEM_AO_RAYS,
	VIDITEM_COUNT
} viditem_t;

/* The bits of r_metalfx_modes. */
#define VIDMFX_OFF     1
#define VIDMFX_SPATIAL 2
#define VIDMFX_TEMPORAL 4
#define VIDMFX_DENOISED 8
#define VIDMFX_INTERP  16
#define VIDMFX_ALL     31

typedef struct
{
	int enabled;
	/* the reason when the item is greyed, a note about it (or NULL) when not; one line */
	const char *text;
} vidrule_t;

vidref_t VidRules_Renderer(const char *name);

/* enabled and the reason or note of an item; an unknown item is greyed.
   scaler is the MetalFX scaler's index for the MetalFX items, and for the
   ray-traced AO's items (VIDITEM_AO_*) whether r_rt_ao is on. */
vidrule_t VidRules_Item(viditem_t item, const char *renderer, int modemask, int scaler);

/* the cvar an item sets under this renderer, NULL when it sets none */
const char *VidRules_Cvar(viditem_t item, const char *renderer);

/* whether the GPU has scaler mode 0 (off) to 3 (temporal denoised) */
int VidRules_ScalerModeAvailable(int modemask, int mode);

/* The scaler list was moved from index prev to index cur: cur when the GPU
   has that mode, else the next mode the GPU has in the direction of the move,
   else prev. Never a mode the GPU lacks, never out of 0 to 3. */
int VidRules_ScalerStep(int modemask, int prev, int cur);

/* the internal scale steps: index 0 is auto (the cvar's 0); returns the
   index nearest a cvar value, or VidRules_NumScales() for none within reach (custom) */
int VidRules_NumScales(void);
float VidRules_ScaleValue(int index);
int VidRules_ScaleIndex(float value);

/* What the menu reads from cvars, as the index it shows. r_metalfx_modes as
   a mask (a value that is not one, or not finite, is no mode; a fraction is
   truncated); r_metalfx as a scaler index 0 to 3 (a fraction is truncated,
   above 3 is 3, below 1 or not a number is 0); r_rt_denoiser as 0 to 2 in the same way. */
int VidRules_ModeMask(float value);
int VidRules_ScalerIndex(float value);
int VidRules_DenoiserIndex(float value);

/* The ray-traced AO's items (TASK157), read as the renderer reads the cvars:
   r_rt_ao_scale as index 0 (full), 1 (1/2) or 2 (1/3), the nearest to
   1 / value, the default 1/2 for 0, below 0 or not a number, and the value
   of each index (1/2 for one out of range); r_rt_ao_rays as index 0 (1 ray)
   or 1 (2 rays), the nearest, 2 rays when not a number. */
int VidRules_NumAOScales(void);
float VidRules_AOScaleValue(int index);
int VidRules_AOScaleIndex(float value);
int VidRules_AORaysIndex(float value);

#endif
