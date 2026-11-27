/*
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
 * The presentation modes' overlay tables (TASK183, TASK237;
 * docs/plans/presentation-modes.md), the one copy that ref_metal
 * (rm_rtgate.c: rm_pres_decide, rm_pres_value, rm_effect_overlaid) and the
 * client's video menu (src/client/menu/vidrules.c: VidRules_RTOverridden,
 * VidRules_RTEffective) both read. Header-only: data and static inline
 * code, compiled into each module on its own, so neither links a symbol
 * of the other and no module API changes. No Metal, no SDL, no engine
 * state. tests/metal/rm_pres_parity_test.c holds the two readers in step.
 *
 * =======================================================================
 */

#ifndef RM_PRES_TABLE_H
#define RM_PRES_TABLE_H

#include <stddef.h>
#include <string.h>

/*
 * The modes, the values of the archived integer r_presentation; never
 * renumbered (config.cfg stores them). The menu's VIDPRES_* are the same
 * values (the parity test checks it).
 */
#define RM_PRES_STOCK 0
#define RM_PRES_ENHANCED 1
#define RM_PRES_RETRO 2
#define RM_PRES_RTPRIMARY 3
#define RM_PRES_COUNT 4

/* one row of a mode's overlay: a cvar and the value the mode reads it as */
typedef struct
{
	const char *name;
	float value;
} rm_pres_row_t;

/*
 * Each mode's rows, as a macro body so a host test can compile other rows
 * in (scripts/rt/preset-to-table.sh prints a saved preset's rows as
 * RM_PRES_<MODE>_ROWS, TASK195). Stock decides none.
 *
 * Enhanced is the stock lighting model with the ray-traced layers on at
 * their defaults (the plan's section 2.2): each effect switch below reads
 * 1, and every other setting (the AO's size, the sun's angles, r_metalfx,
 * ...) stays the user's.
 */
#ifndef RM_PRES_ENHANCED_ROWS
#define RM_PRES_ENHANCED_ROWS \
	{ "r_rt_shadows", 1.0f }, \
	{ "r_rt_sun", 1.0f }, \
	{ "r_rt_ao", 1.0f }, \
	{ "r_rt_water", 1.0f },
#endif

/*
 * Retro (TASK184, PM2) needs no ray tracing and runs on every device. Its
 * post-process decides no cvar of its own, but plan call P8 (TASK197) has
 * it read every ray-traced layer, the gun shadow and r_metalfx as 0, so no
 * layer or scaler smears or relights the quantized view. r_rt_direct is
 * not a row and keeps the user's value: that switch runs nothing outside
 * RT-primary (rm_rtdir_on).
 */
#ifndef RM_PRES_RETRO_ROWS
#define RM_PRES_RETRO_ROWS \
	{ "r_rt_shadows", 0.0f }, \
	{ "r_rt_sun", 0.0f }, \
	{ "r_rt_ao", 0.0f }, \
	{ "r_rt_water", 0.0f }, \
	{ "r_rt_gunshadow", 0.0f }, \
	{ "r_metalfx", 0.0f },
#endif

/*
 * RT-primary (TASK188, PM6 slice 1) turns on its direct light
 * (r_rt_direct) and, as the plan's section 2.2 has it, the water
 * reflections; AO is off (the bounce, PM7, replaces it), and so are the
 * sun and the dlight shadows until PM7 and PM6's second slice make them
 * terms of the light model, so no layer adds the lightmap's light back.
 * TASK190 slice 3: the scaler row is RM_FX_TEMPORAL of
 * header/rm_metalfx.h, which ref_metal reads where the internal size is
 * below the output (rm_pres_fx decides it); the scale stays the user's.
 */
#ifndef RM_PRES_RTPRIMARY_ROWS
#define RM_PRES_RTPRIMARY_ROWS \
	{ "r_rt_direct", 1.0f }, \
	{ "r_rt_shadows", 0.0f }, \
	{ "r_rt_sun", 0.0f }, \
	{ "r_rt_ao", 0.0f }, \
	{ "r_rt_water", 1.0f }, \
	{ "r_metalfx", 2.0f },
#endif

/* the rows mode overlays, with their count in *n; NULL and 0 for a mode that decides nothing */
static inline const rm_pres_row_t *
rm_pres_rows(int mode, int *n)
{
	static const rm_pres_row_t enhanced[] = { RM_PRES_ENHANCED_ROWS };
	static const rm_pres_row_t retro[] = { RM_PRES_RETRO_ROWS };
	static const rm_pres_row_t rtprimary[] = { RM_PRES_RTPRIMARY_ROWS };

	switch (mode)
	{
		case RM_PRES_ENHANCED:
			*n = (int)(sizeof(enhanced) / sizeof(enhanced[0]));
			return enhanced;
		case RM_PRES_RETRO:
			*n = (int)(sizeof(retro) / sizeof(retro[0]));
			return retro;
		case RM_PRES_RTPRIMARY:
			*n = (int)(sizeof(rtprimary) / sizeof(rtprimary[0]));
			return rtprimary;
		default:
			*n = 0;
			return NULL;
	}
}

/* 1 when mode decides the cvar name, with the value it reads in *value (when not NULL); else 0 */
static inline int
rm_pres_row(int mode, const char *name, float *value)
{
	int i, n;
	const rm_pres_row_t *t = rm_pres_rows(mode, &n);

	for (i = 0; t && name && (i < n); i++)
	{
		if (!strcmp(t[i].name, name))
		{
			if (value)
			{
				*value = t[i].value;
			}

			return 1;
		}
	}

	return 0;
}

/* 1 for a mode that runs only where the device traces rays (Enhanced, RT-primary: decision 0014); Retro runs on every device */
static inline int
rm_pres_needs_rt(int mode)
{
	return (mode == RM_PRES_ENHANCED) || (mode == RM_PRES_RTPRIMARY);
}

#endif
