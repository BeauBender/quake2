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
	VIDITEM_RTW_HITS,   /* TASK158: the water reflections' hit shading, r_rt_water_hits */
	VIDITEM_AO_SCALE,
	VIDITEM_AO_RAYS,
	VIDITEM_COLORSPACE, /* TASK121: the Metal layer's colour space, metal_colorspace */
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

/*
 * The platform input of the screen's items (TASK147): the desktop (a
 * window of a mode on a display, fullscreen or not, with a swap
 * interval) or iOS (the whole screen at its own size, always in step
 * with the display, the one renderer linked in). The menu takes it from
 * its build; the unit tests give either.
 */
typedef enum
{
	VIDPLAT_DESKTOP,
	VIDPLAT_IOS
} vidplat_t;

/* The items of the video menu that depend on the screen, not on the renderer. */
typedef enum
{
	VIDSCR_MODE,
	VIDSCR_FULLSCREEN,
	VIDSCR_DISPLAY,
	VIDSCR_VSYNC,
	VIDSCR_RENDERER,
	VIDSCR_COUNT
} vidscr_t;

/* enabled and the reason of a screen item; an unknown platform or item is greyed */
vidrule_t VidRules_Screen(vidplat_t plat, vidscr_t item);

vidref_t VidRules_Renderer(const char *name);

/* enabled and the reason or note of an item; an unknown item is greyed.
   scaler is the MetalFX scaler's index for the MetalFX items, and for the
   items that need a ray-traced effect on, whether it is: VIDITEM_RTW_HITS
   whether r_rt_water is on (VidRules_WaterOn), the ray-traced AO's items
   (VIDITEM_AO_*) whether r_rt_ao is on. */
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

/* TASK158: r_rt_water as the renderer reads it, 1 when finite and not 0;
   r_rt_water_hits as its index, 1 (world space) from 1 up, 0 (screen space)
   below 1 or not finite. */
int VidRules_WaterOn(float value);
int VidRules_WaterHitsIndex(float value);

/* The ray-traced AO's items (TASK157), read as the renderer reads the cvars:
   r_rt_ao_scale as index 0 (full), 1 (1/2) or 2 (1/3), the nearest to
   1 / value (1.5 goes to 2, 2.5 to 2), the default 1/2 for 0, below 0, or
   not finite, and the value of each index (1/2 for one out of range);
   r_rt_ao_rays as index 0 (1 ray) or 1 (2 rays), the nearest, 2 rays when not
   finite. */
int VidRules_NumAOScales(void);
float VidRules_AOScaleValue(int index);
int VidRules_AOScaleIndex(float value);
int VidRules_AORaysIndex(float value);

/*
 * The ray-tracing screen (TASK194): its controls, one row each, in the
 * order the screen shows them. A switch (effect -1) turns a pass on or
 * off; a slider tunes one and needs the switch at row effect on. flags is
 * what the menu registers the cvar with: CVAR_ARCHIVE, so a value set
 * here is saved with the configuration (tests/unit/test_vidrules.c
 * proves the round trip). def is the renderer's own default.
 */
typedef struct
{
	const char *cvar;
	const char *label;
	const char *def;
	int flags;
	int effect;         /* -1 for a switch, else the row of its switch */
	float min, max, step;   /* a slider's range and step; 0 for a switch */
	const char *format;     /* a slider's value as printed */
	const char *const *choices;   /* NULL, or the names of a choice row's steps (a spin control, not a slider) */
	int part;           /* TASK195: 0 the whole value, 1 or 2 its first or second word, 3 the debug view */
	int restart;        /* TASK195: 1 when a change shows only after vid_restart */
} vidrt_control_t;

int VidRules_RTNum(void);
/* row i, NULL out of range */
const vidrt_control_t *VidRules_RTControl(int i);

/* The "ray tracing" entry of the video menu: greyed unless the renderer is metal. */
vidrule_t VidRules_RTEntry(const char *renderer);

/*
 * Row i of the screen, and its "reset to defaults" for i == -1:
 * available is whether the device traces rays (r_rt_available, which
 * ref_metal sets at start), effect_on whether the row's switch is on
 * (ignored for a switch and the reset). Greyed with the reason when the
 * renderer is not metal, when the device has no ray tracing, and for a
 * slider while its effect is off.
 */
vidrule_t VidRules_RTItem(int i, const char *renderer, int available, int effect_on);

/*
 * The presentation item of the video menu (TASK183,
 * docs/plans/presentation-modes.md): r_presentation, one archived
 * integer whose values are never renumbered. VidRules_Presentation is the
 * menu's row: cvar, label, default, flags and the names of the modes
 * (Stock, Enhanced, Retro, Ray traced), in the order of their values.
 */
#define VIDPRES_STOCK 0
#define VIDPRES_ENHANCED 1
#define VIDPRES_RETRO 2
#define VIDPRES_RTPRIMARY 3
#define VIDPRES_COUNT 4

const vidrt_control_t *VidRules_Presentation(void);

/* r_presentation as the menu's index: a fraction truncated, 0 (stock) for one that is no mode or not finite */
int VidRules_PresentationIndex(float value);

/*
 * Whether mode can be chosen, and the reason when not; mode -1 is the
 * item itself. Every mode is shown; one that does not apply is greyed
 * with its reason: off Metal "ref_metal only"; Retro and Ray traced
 * "awaiting review" (the owner has not accepted their frames); Enhanced
 * and Ray traced "needs ray tracing" where the device has none
 * (available, r_rt_available 0). An unknown mode is greyed.
 */
vidrule_t VidRules_PresentationMode(int mode, const char *renderer, int available);

/*
 * The ray-tracing screen's switches under the presentation in use (the
 * plan's section 2.3). VidRules_RTOverridden: whether the mode decides
 * switch i (Enhanced: shadows, sun, ambient occlusion, water).
 * VidRules_RTEffective: the switch as the renderer runs it, 1 when the
 * mode decides it, else user (the cvar's value) not 0. VidRules_RTRow is
 * VidRules_RTItem under the mode: a switch the mode decides is greyed
 * "set by the presentation", a slider is live while its effect is on as
 * run, effect_user being the cvar of its switch. Mode 0 is VidRules_RTItem.
 */
int VidRules_RTOverridden(int i, const char *renderer, int available, float presentation);
int VidRules_RTEffective(int i, const char *renderer, int available, float presentation, float user);
vidrule_t VidRules_RTRow(int i, const char *renderer, int available, float presentation, float effect_user);

/*
 * The ray-tracing screen's status bar under the presentation mode in use
 * (r_presentation): the screen shows the user's own values, and under a
 * mode that decides the switches it says the mode overrides them.
 */
const char *VidRules_RTStatus(const char *renderer, int available, float presentation);

/*
 * The tuning screen (TASK195), reached from the ray-tracing screen: every
 * r_rt_* setting that changes the look and is not on that screen, the
 * scaler (r_metalfx, whose mode 3 is the denoised one), the debug view
 * and the presentation. A row with part 1 or 2 is one word of a pair
 * (r_rt_sun_angles: pitch, yaw) as a choice of min to max by step; part 3
 * is the debug view, one *_show cvar at 1 at a time (VidRules_DebugView);
 * a row with restart applies only after vid_restart. A choice row's step
 * i is the value min + i * step. Every row is always shown:
 * VidRules_TuneItem greys it with its reason (effect_on as for
 * VidRules_RTItem). VidRules_TuneRestart is the "apply (restart)" action:
 * live only after a row with restart changed.
 */
#define VIDTUNE_PART_DEBUG 3

int VidRules_TuneNum(void);
const vidrt_control_t *VidRules_TuneControl(int i);
vidrule_t VidRules_TuneItem(int i, const char *renderer, int available, int effect_on);
vidrule_t VidRules_TuneRestart(const char *renderer, int changed);
/* the cvar of debug view v (1 to VidRules_NumDebugViews() - 1), NULL for 0 (none) or out of range */
int VidRules_NumDebugViews(void);
const char *VidRules_DebugView(int v);

/*
 * The tuning cvars a preset holds and the A/B toggle swaps (TASK195):
 * every row of the ray-tracing screen and every tuning row that is not
 * the debug view or a restart, each cvar once, with its default.
 */
int VidRules_PresetNum(void);
const char *VidRules_PresetCvar(int i, const char **def);
/* 1 when name is one of them */
int VidRules_IsPresetCvar(const char *name);

/*
 * A preset's name (rt_preset): NULL when it may name a file, else why
 * not. Refused: empty, longer than VIDPRESET_MAXNAME, "..", '/', '\', a
 * leading '.', and any character outside [A-Za-z0-9_-].
 */
#define VIDPRESET_MAXNAME 32
const char *VidRules_PresetNameError(const char *name);

/*
 * rtpreset.c's commands (TASK195): rt_preset save|load|ab|list and rt_ab.
 * Save and the loads check the name themselves (VidRules_PresetNameError)
 * and return 0 when refused or when the file cannot be written or read.
 * Load writes the tuning cvars and nothing else; LoadAB and AB write none
 * of them, only r_rt_ab, the B side the renderer overlays. RTPreset_Side
 * is the side showing: "A: your settings", "B: stock" or "B: preset <name>".
 */
void RTPreset_Init(void);
int RTPreset_Save(const char *name);
int RTPreset_Load(const char *name);
int RTPreset_LoadAB(const char *name);
void RTPreset_AB(void);
const char *RTPreset_Side(void);
void RTPreset_SetDir(const char *dir);   /* the tests: NULL is <writedir> */

#endif
